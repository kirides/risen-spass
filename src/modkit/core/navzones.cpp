#include "core/navzones.h"

#include <algorithm>
#include <cmath>

namespace mk {

bool point_in_polygon(const std::vector<std::array<double, 2>>& poly, double x, double z) {
    bool in = false;
    for (size_t i = 0, j = poly.size() ? poly.size() - 1 : 0; i < poly.size(); j = i++) {
        const auto &a = poly[i], &b = poly[j];
        if ((a[1] > z) != (b[1] > z) && x < (b[0] - a[0]) * (z - a[1]) / (b[1] - a[1]) + a[0]) in = !in;
    }
    return in;
}

namespace {

double distance_to_segment(double px, double pz, const std::array<double, 2>& a, const std::array<double, 2>& b) {
    const double dx = b[0] - a[0], dz = b[1] - a[1], len2 = dx * dx + dz * dz;
    double t = len2 > 0 ? ((px - a[0]) * dx + (pz - a[1]) * dz) / len2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    return std::hypot(px - (a[0] + t * dx), pz - (a[1] + t * dz));
}

}  // namespace

bool NavMap::on_ground(double x, double y, double z) const {
    for (const NavZone& n : zones) {
        if (x < n.min_x || x > n.max_x || z < n.min_z || z > n.max_z || y < n.low || y > n.high) continue;
        if (point_in_polygon(n.poly, x, z)) return true;
    }
    return false;
}

double NavMap::distance_to_ground(double x, double y, double z, double max_distance) const {
    double best = -1;
    for (const NavZone& n : zones) {
        if (y < n.low || y > n.high || x < n.min_x - max_distance || x > n.max_x + max_distance || z < n.min_z - max_distance || z > n.max_z + max_distance) continue;
        if (x >= n.min_x && x <= n.max_x && z >= n.min_z && z <= n.max_z && point_in_polygon(n.poly, x, z)) return 0;
        for (size_t i = 0, j = n.poly.size() - 1; i < n.poly.size(); j = i++) {
            const double d = distance_to_segment(x, z, n.poly[i], n.poly[j]);
            if (d <= max_distance && (best < 0 || d < best)) best = d;
        }
    }
    return best;
}

std::shared_ptr<NavMap> build_navmap(const GameData& g, const World& w) {
    auto map = std::make_shared<NavMap>();
    for (const WorldEntity& e : w.entities) {
        if (e.template_name.empty()) continue;
        const TemplateInfo* t = g.tpl(e.template_name);
        if (!t || !t->has_set("gCNavZone_PS")) continue;
        NavZone z;
        z.name = e.name;
        z.min_x = z.min_z = 1e30;
        z.max_x = z.max_z = -1e30;
        double low = 1e30, high = -1e30, top = 150, bottom = 100, sum_y = 0;
        for (const p2m::Prop& p : entity_properties(g, w, e)) {
            const std::string k = p.key();
            if (k == "gCNavZone_PS.TopToleranz") top = p.value.k == p2m::Val::Float ? p.value.d : static_cast<double>(p.value.i);
            else if (k == "gCNavZone_PS.BottomToleranz") bottom = p.value.k == p2m::Val::Float ? p.value.d : static_cast<double>(p.value.i);
            else if (k == "gCNavZone_PS.Point" && p.value.k == p2m::Val::Vec)
                for (size_t i = 0; i + 2 < p.value.v.size(); i += 3) {
                    // the entity's matrix: row vectors, the translation in the last row
                    const double lx = p.value.v[i], ly = p.value.v[i + 1], lz = p.value.v[i + 2];
                    const auto& m = e.matrix;
                    const double wx = lx * m[0] + ly * m[4] + lz * m[8] + m[12], wy = lx * m[1] + ly * m[5] + lz * m[9] + m[13], wz = lx * m[2] + ly * m[6] + lz * m[10] + m[14];
                    z.poly.push_back({wx, wz});
                    z.min_x = std::min(z.min_x, wx);
                    z.max_x = std::max(z.max_x, wx);
                    z.min_z = std::min(z.min_z, wz);
                    z.max_z = std::max(z.max_z, wz);
                    sum_y += wy;
                    low = std::min(low, wy);
                    high = std::max(high, wy);
                }
        }
        if (z.poly.size() < 3) continue;
        z.y = sum_y / static_cast<double>(z.poly.size());
        z.low = low - bottom;
        z.high = high + top;
        map->zones.push_back(std::move(z));
    }
    return map;
}

}  // namespace mk
