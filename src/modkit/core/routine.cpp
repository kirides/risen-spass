#include "core/routine.h"

namespace mk {

namespace {

RoutinePoint read_point(const json& pt, std::string key, std::string label) {
    RoutinePoint r;
    r.key = std::move(key);
    r.label = std::move(label);
    if (pt.is_object()) {
        r.d = {pt.value("dx", 0.0), pt.value("dy", 0.0), pt.value("dz", 0.0)};
        r.kind = pt.value("kind", std::string());
        if (pt.contains("yaw") && pt["yaw"].is_number()) r.yaw = pt["yaw"].get<double>();
    }
    return r;
}

json* point_json(json& routine, const std::string& key) {
    if (!routine.is_object()) return nullptr;
    if (key == "at" || key == "work" || key == "relax" || key == "sleep") {
        auto it = routine.find(key);
        return it != routine.end() && it->is_object() ? &*it : nullptr;
    }
    if (key.size() > 1 && key[0] == 'p') {
        auto pit = routine.find("patrol");
        if (pit == routine.end() || !pit->is_object()) return nullptr;
        auto ptsit = pit->find("points");
        if (ptsit == pit->end() || !ptsit->is_array()) return nullptr;
        size_t idx = 0;
        try {
            idx = static_cast<size_t>(std::stoul(key.substr(1)));
        } catch (...) {
            return nullptr;
        }
        return idx < ptsit->size() && (*ptsit)[idx].is_object() ? &(*ptsit)[idx] : nullptr;
    }
    return nullptr;
}

}  // namespace

std::vector<RoutinePoint> routine_points(const json& routine) {
    std::vector<RoutinePoint> out;
    if (!routine.is_object()) return out;
    if (auto it = routine.find("at"); it != routine.end()) {
        out.push_back(read_point(*it, "at", "post"));
    } else if (auto pit = routine.find("patrol"); pit != routine.end() && pit->is_object()) {
        auto ptsit = pit->find("points");
        if (ptsit != pit->end() && ptsit->is_array())
            for (size_t i = 0; i < ptsit->size(); ++i) out.push_back(read_point((*ptsit)[i], "p" + std::to_string(i), std::to_string(i + 1)));
    } else {
        for (const char* k : {"work", "relax", "sleep"})
            if (auto kit = routine.find(k); kit != routine.end()) out.push_back(read_point(*kit, k, k));
    }
    return out;
}

bool routine_is_loop(const json& routine) { return routine.is_object() && routine.contains("patrol") && !routine.contains("at"); }

bool routine_set_point(json& routine, const std::string& key, const Vec3& d) {
    json* pt = point_json(routine, key);
    if (!pt) return false;
    (*pt)["dx"] = d[0];
    (*pt)["dy"] = d[1];
    (*pt)["dz"] = d[2];
    return true;
}

Vec3 routine_world(const Vec3& home, const RoutinePoint& p) { return {home[0] + p.d[0], home[1] + p.d[1], home[2] + p.d[2]}; }

}  // namespace mk
