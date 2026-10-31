#include "core/speedtree.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "pack2mod/util.h"

namespace mk {

using p2m::Bytes;
using p2m::Error;

SpeedTreeInfo read_speedtree(const Bytes& file) {
    if (file.size() < 0x80 || file.compare(0, 8, "GR01ST01") != 0) throw Error("not a GR01/ST01 SpeedTree");
    SpeedTreeInfo info;
    // the property stream: ... "Boundary" "bCBox" u16 0x1e, u32 24, six floats
    const size_t bnd = file.find("bCBox");
    if (bnd == std::string::npos || bnd + 5 + 6 + 24 > file.size()) throw Error("the SpeedTree has no boundary");
    const size_t f = bnd + 5 + 6;
    for (size_t i = 0; i < 3; ++i) {
        info.box_min[i] = p2m::rdf(file, f + i * 4);
        info.box_max[i] = p2m::rdf(file, f + 12 + i * 4);
    }
    if (!(info.box_max[1] > info.box_min[1]) || info.box_max[1] - info.box_min[1] > 20000.0f) throw Error("implausible SpeedTree boundary");
    // the texture names are plain strings in the .spt part: "...Bark_05_Diffuse_01.tga"
    for (size_t p = file.find(".tga"); p != std::string::npos && info.textures.size() < 32; p = file.find(".tga", p + 4)) {
        size_t s = p;
        while (s > 0 && (std::isalnum(static_cast<unsigned char>(file[s - 1])) || file[s - 1] == '_' || file[s - 1] == '-')) --s;
        if (s < p) {
            const std::string name = file.substr(s, p + 4 - s);
            if (std::find(info.textures.begin(), info.textures.end(), name) == info.textures.end()) info.textures.push_back(name);
        }
    }
    return info;
}

std::string speedtree_file(const std::string& resource_path) {
    std::string n = resource_path;
    if (const size_t sl = n.find_last_of("/\\"); sl != std::string::npos) n = n.substr(sl + 1);
    if (const size_t dot = n.rfind('.'); dot != std::string::npos) n.resize(dot);
    return n.empty() ? std::string() : n + "._xspt";
}

namespace {

struct Builder {
    std::string verts;
    std::string idx;
    uint32_t nv = 0, ni = 0;
    void vertex(float x, float y, float z, float nx, float ny, float nz, float u, float v) {
        const float rec[8] = {x, y, z, nx, ny, nz, u, v};
        verts.append(reinterpret_cast<const char*>(rec), 32);
        ++nv;
    }
    void tri(uint32_t a, uint32_t b, uint32_t c) {
        idx.append(reinterpret_cast<const char*>(&a), 4);
        idx.append(reinterpret_cast<const char*>(&b), 4);
        idx.append(reinterpret_cast<const char*>(&c), 4);
        ni += 3;
    }
};

bool has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

}  // namespace

MeshData speedtree_proxy(const Bytes& file, const std::string& tree_name) {
    const SpeedTreeInfo info = read_speedtree(file);
    const float height = info.box_max[1];
    const float cx = (info.box_min[0] + info.box_max[0]) * 0.5f, cz = (info.box_min[2] + info.box_max[2]) * 0.5f;
    const float rx = std::max((info.box_max[0] - info.box_min[0]) * 0.5f, 10.0f), rz = std::max((info.box_max[2] - info.box_min[2]) * 0.5f, 10.0f);
    const bool bush = has(tree_name, "_Bush_") || has(tree_name, "_Marine_");
    const bool palm = has(tree_name, "Palm");
    const bool conifer = has(tree_name, "Pine") || has(tree_name, "Fir") || has(tree_name, "Juniper") || has(tree_name, "Bristle");

    // the crown's region of the composite image (0..1, v down): coniferous needles, palm fronds or a leafy cluster picked by the name
    float u0 = 0.245f, u1 = 0.49f, v0 = 0.245f, v1 = 0.366f;
    if (conifer) u0 = 0.0f, u1 = 0.49f, v0 = 0.73f, v1 = 0.98f;
    else if (palm) u0 = 0.49f, u1 = 0.73f, v0 = 0.06f, v1 = 0.30f;
    else if (const size_t h = std::hash<std::string>{}(tree_name) % 3; h == 1) u0 = 0.49f, u1 = 0.73f, v0 = 0.305f, v1 = 0.43f;
    else if (h == 2) u0 = 0.73f, u1 = 0.98f, v0 = 0.37f, v1 = 0.49f;

    Builder b;
    MeshData m;
    m.stride = 32;
    m.pos = 0;
    m.normal = 12;
    m.uv = 24;
    m.index32 = true;

    // trunk: a tapered prism of 8 sides from the ground to the crown's middle (bushes have none)
    SubMesh trunk;
    trunk.texture.clear();
    for (const std::string& t : info.textures)
        if (has(t, "Bark")) {
            trunk.texture = t.substr(0, t.size() - 4);
            break;
        }
    trunk.first_index = 0;
    if (!bush) {
        const int sides = 8;
        const float r0 = std::clamp(height * 0.03f, 6.0f, 60.0f), r1 = r0 * 0.5f, top = palm ? height * 0.92f : height * 0.6f;
        const uint32_t base = b.nv;
        for (int ring = 0; ring < 2; ++ring)
            for (int s = 0; s <= sides; ++s) {
                const float a = 6.2831853f * static_cast<float>(s) / static_cast<float>(sides);
                const float r = ring ? r1 : r0, x = std::cos(a), z = std::sin(a);
                b.vertex(x * r, ring ? top : 0.0f, z * r, x, 0, z, static_cast<float>(s) / static_cast<float>(sides) * 2.0f, ring ? top / 120.0f : 0.0f);
            }
        for (int s = 0; s < sides; ++s) {
            const uint32_t a = base + static_cast<uint32_t>(s), c = base + static_cast<uint32_t>(sides + 1 + s);
            b.tri(a, c, a + 1);
            b.tri(a + 1, c, c + 1);
        }
    }
    trunk.index_count = b.ni;
    trunk.first_vertex = 0;
    trunk.vertex_count = b.nv;

    // crown: an ellipsoid of the box's width; a conifer's is tall and narrow, a bush sits on the ground
    SubMesh crown;
    crown.texture = "ST_Composite_01_Diffuse_01";
    crown.first_index = b.ni;
    const float cy = bush ? height * 0.5f : palm ? height * 0.9f : conifer ? height * 0.55f : height * 0.66f;
    const float ry = bush ? height * 0.5f : palm ? height * 0.12f : conifer ? height * 0.46f : height * 0.34f;
    const float sx = palm ? rx * 0.6f : conifer ? rx * 0.7f : rx, sz = palm ? rz * 0.6f : conifer ? rz * 0.7f : rz;
    const int stacks = 8, slices = 12;
    const uint32_t cbase = b.nv;
    for (int i = 0; i <= stacks; ++i) {
        const float phi = 3.14159265f * static_cast<float>(i) / static_cast<float>(stacks);
        for (int j = 0; j <= slices; ++j) {
            const float th = 6.2831853f * static_cast<float>(j) / static_cast<float>(slices);
            const float nx = std::sin(phi) * std::cos(th), ny = std::cos(phi), nz = std::sin(phi) * std::sin(th);
            b.vertex(cx + nx * sx, cy + ny * ry, cz + nz * sz, nx, ny, nz, u0 + (u1 - u0) * static_cast<float>(j) / static_cast<float>(slices),
                     v0 + (v1 - v0) * static_cast<float>(i) / static_cast<float>(stacks));
        }
    }
    for (int i = 0; i < stacks; ++i)
        for (int j = 0; j < slices; ++j) {
            const uint32_t a = cbase + static_cast<uint32_t>(i * (slices + 1) + j), c = a + static_cast<uint32_t>(slices + 1);
            b.tri(a, a + 1, c);
            b.tri(a + 1, c + 1, c);
        }
    crown.index_count = b.ni - crown.first_index;
    crown.first_vertex = 0;
    crown.vertex_count = b.nv;

    if (trunk.index_count) m.submeshes.push_back(trunk);
    m.submeshes.push_back(crown);
    m.vertices = std::move(b.verts);
    m.indices = std::move(b.idx);
    m.vertex_count = b.nv;
    m.index_count = b.ni;
    m.box_min = info.box_min;
    m.box_max = info.box_max;
    return m;
}

}  // namespace mk
