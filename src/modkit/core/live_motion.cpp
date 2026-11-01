#include "core/live_motion.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace mk {

namespace {

bool contains(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

// a forward move cycle on two feet, no weapon in the hands: the plain walk / run / sprint of an NPC at ease
bool plain_forward(const std::string& n) { return contains(n, "_Stand_None_None_P0_Move_") && contains(n, "_N_Fwd_00_"); }

}  // namespace

float motion_cycle_speed(const std::string& name) {
    const size_t end = name.find("._xmot");
    const std::string stem = end == std::string::npos ? name : name.substr(0, end);
    const size_t us = stem.rfind('_');
    if (us == std::string::npos || us + 1 >= stem.size()) return 0;
    char* e = nullptr;
    const float v = std::strtof(stem.c_str() + us + 1, &e);
    return (e && *e == 0 && v > 0) ? v : 0;
}

MotionChoice choose_motion(const std::vector<std::string>& names, float speed, bool was_moving) {
    const std::string* stand = nullptr;
    struct Cycle {
        const std::string* name;
        float cycle;
    };
    std::vector<Cycle> cycles;
    for (const std::string& n : names) {
        if (!plain_forward(n)) continue;
        if (contains(n, "_Move_Stand_N_Fwd")) stand = &n;
        else if (contains(n, "_Move_Walk_N_Fwd") || contains(n, "_Move_Run_N_Fwd") || contains(n, "_Move_Sprint_N_Fwd")) {
            const float c = motion_cycle_speed(n);
            if (c > 0) cycles.push_back({&n, c});
        }
    }
    MotionChoice out;
    const float threshold = was_moving ? 12.0f : 30.0f;  // starts walking above 30 units/s, stops below 12
    if (speed < threshold || cycles.empty()) {
        if (stand) out.name = *stand;
        return out;
    }
    // the cycle made for the speed closest to the actor's (in ratio: 100 against 150 is nearer than 100 against 60 is far)
    const Cycle* best = &cycles.front();
    float best_d = 1e30f;
    for (const Cycle& c : cycles) {
        const float d = std::fabs(std::log(speed / c.cycle));
        if (d < best_d) {
            best_d = d;
            best = &c;
        }
    }
    out.name = *best->name;
    out.moving = true;
    out.rate = std::clamp(speed / best->cycle, 0.4f, 1.8f);
    return out;
}

}  // namespace mk
