#include "core/water.h"

#include <algorithm>
#include <cmath>

#include "core/meshwrite.h"
#include "core/project.h"

namespace mk {

namespace {

using P = std::array<double, 2>;

double cross(const P& o, const P& a, const P& b) { return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]); }

double signed_area(const std::vector<P>& p) {
    double a = 0;
    for (size_t i = 0; i < p.size(); ++i) {
        const P &u = p[i], &v = p[(i + 1) % p.size()];
        a += u[0] * v[1] - v[0] * u[1];
    }
    return a / 2;
}

bool segments_cross(const P& a, const P& b, const P& c, const P& d) {
    const double d1 = cross(a, b, c), d2 = cross(a, b, d), d3 = cross(c, d, a), d4 = cross(c, d, b);
    return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0)) && d1 != 0 && d2 != 0 && d3 != 0 && d4 != 0;
}

bool in_triangle(const P& p, const P& a, const P& b, const P& c) {
    const double d1 = cross(a, b, p), d2 = cross(b, c, p), d3 = cross(c, a, p);
    const bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
    return !(neg && pos);
}

// ear clipping of a simple polygon: triangles as index triples into `pts` (counter-clockwise in the x / z plane)
bool triangulate(const std::vector<P>& pts, std::vector<uint32_t>& out) {
    std::vector<uint32_t> idx(pts.size());
    for (uint32_t i = 0; i < idx.size(); ++i) idx[i] = i;
    if (signed_area(pts) < 0) std::reverse(idx.begin(), idx.end());
    size_t guard = 0;
    while (idx.size() > 3 && guard++ < pts.size() * pts.size() + 8) {
        bool clipped = false;
        for (size_t i = 0; i < idx.size(); ++i) {
            const uint32_t ia = idx[(i + idx.size() - 1) % idx.size()], ib = idx[i], ic = idx[(i + 1) % idx.size()];
            if (cross(pts[ia], pts[ib], pts[ic]) <= 0) continue;  // a reflex corner
            bool inside = false;
            for (uint32_t k : idx)
                if (k != ia && k != ib && k != ic && in_triangle(pts[k], pts[ia], pts[ib], pts[ic])) {
                    inside = true;
                    break;
                }
            if (inside) continue;
            out.insert(out.end(), {ia, ib, ic});
            idx.erase(idx.begin() + static_cast<long>(i));
            clipped = true;
            break;
        }
        if (!clipped) return false;
    }
    if (idx.size() == 3) out.insert(out.end(), {idx[0], idx[1], idx[2]});
    return idx.size() == 3;
}

}  // namespace

const std::vector<std::string>& water_materials() {
    static const std::vector<std::string> v = {"Special_Water_Island_01_Diffuse_01._xmat", "Special_Water_Ocean_01_Diffuse_01._xmat", "Special_Water_River_01_Diffuse_01._xmat", "Special_Water_Swamp_01_Diffuse_01._xmat",
                                               "Special_Water_Falls_01_Diffuse_01._xmat",  "Special_Water_Fall_01_Diffuse_01._xmat"};
    return v;
}

std::array<double, 2> water_center(const WaterBody& w) {
    double x = 0, z = 0;
    for (auto& p : w.points) {
        x += p[0];
        z += p[1];
    }
    const double n = w.points.empty() ? 1.0 : static_cast<double>(w.points.size());
    return {x / n, z / n};
}

double water_area(const WaterBody& w) { return std::fabs(signed_area(w.points)); }

bool water_polygon_simple(const WaterBody& w) {
    const auto& p = w.points;
    if (p.size() < 3 || water_area(w) < 1.0) return false;
    for (size_t i = 0; i < p.size(); ++i)
        for (size_t j = i + 2; j < p.size(); ++j) {
            if (i == 0 && j == p.size() - 1) continue;  // neighbours
            if (segments_cross(p[i], p[(i + 1) % p.size()], p[j], p[(j + 1) % p.size()])) return false;
        }
    return true;
}

bool build_water(const WaterBody& w, std::map<std::string, std::string>& files, std::string& error) {
    if (w.name.empty()) {
        error = "the water body needs a name";
        return false;
    }
    if (!water_polygon_simple(w)) {
        error = "the outline needs at least three points, a surface, and no crossing edges";
        return false;
    }
    const P c = water_center(w);
    std::vector<P> local;
    for (auto& p : w.points) local.push_back({p[0] - c[0], p[1] - c[1]});
    std::vector<uint32_t> tris;
    if (!triangulate(local, tris)) {
        error = "the outline cannot be divided into triangles";
        return false;
    }
    MeshPart part;
    part.material = w.material.empty() ? water_materials().front() : w.material;
    for (auto& p : local) {
        part.positions.push_back({static_cast<float>(p[0]), 0.0f, static_cast<float>(p[1])});
        part.normals.push_back({0, 1, 0});
        part.uvs.push_back({static_cast<float>(p[0] / 512.0), static_cast<float>(p[1] / 512.0)});
    }
    // both faces: the winding the engine culls is not known for a generated plane, and water is seen from above and from below
    for (size_t i = 0; i < tris.size(); i += 3) part.indices.insert(part.indices.end(), {tris[i], tris[i + 1], tris[i + 2]});
    for (size_t i = 0; i < tris.size(); i += 3) part.indices.insert(part.indices.end(), {tris[i + 2], tris[i + 1], tris[i]});
    MeshBuild b;
    b.parts.push_back(std::move(part));
    std::string mesh;
    mesh = build_xmsh(b, error);
    if (mesh.empty()) return false;
    files["assets/" + w.name + "._xmsh"] = std::move(mesh);
    return true;
}

Project with_water(const Project& p) {
    Project q = p;
    for (const WaterBody& w : p.waters) {
        ObjectLook l;
        l.uid = w.uid;
        l.name = w.name;
        l.base = "Obj_Nat_Waterfall_Large";
        l.mesh = w.name + "._xmsh";
        l.generated = true;
        q.looks.push_back(l);
        WorldSpawn s;
        s.uid = w.uid + "s";
        s.template_name = w.name;
        const auto c = water_center(w);
        s.position = {c[0], w.height, c[1]};
        s.world = w.world;
        q.world.spawns.push_back(s);
    }
    return q;
}

}  // namespace mk
