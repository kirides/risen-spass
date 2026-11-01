#include "core/live_motion.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace mk {

namespace {

bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// the part of a name every motion of a stance shares: "_Stand_None_None_P0_" (two empty hands, one body)
std::string stance_part(const std::string& stance) { return "_" + stance + "_None_None_P0_"; }

// the first motion of the stance whose action starts with `tail` ("Ambient_Loop_N_", "Move_Stand_N_Fwd", ...); nullptr when there is none
const std::string* find_tail(const std::vector<std::string>& names, const std::string& stance, const std::string& tail) {
    const std::string key = stance_part(stance) + tail;
    for (const std::string& n : names)
        if (contains(n, key)) return &n;
    return nullptr;
}

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

MotionChoice choose_motion(const std::vector<std::string>& names, const MotionRequest& r) {
    MotionChoice out;
    std::string stance = r.stance.empty() ? "Stand" : r.stance;
    if (r.dead) {  // lies where it fell: the stance's own resting pose, whatever the routine still says
        for (const char* dead : {"Dead", "DeadBack"})
            if (const std::string* n = find_tail(names, dead, "Ambient_Loop_N_")) {
                out.name = *n;
                return out;
            }
        return out;
    }
    // walking and running exist in the stances an actor can move in (Stand, Sneak, Wade, ...); a sitting or sleeping one does not move, whatever its speed
    struct Cycle {
        const std::string* name;
        float cycle;
    };
    std::vector<Cycle> cycles;
    const bool movable = stance == "Stand" || stance == "Sneak" || stance == "Wade";
    for (const char* use : {stance.c_str(), "Stand"}) {  // the stance's own cycles, else the plain ones
        if (!movable) break;
        for (const std::string& n : names) {
            if (!contains(n, stance_part(use) + "Move_") || !contains(n, "_N_Fwd_00_")) continue;
            if (contains(n, "_Move_Walk_N_Fwd") || contains(n, "_Move_Run_N_Fwd") || contains(n, "_Move_Sprint_N_Fwd")) {
                const float c = motion_cycle_speed(n);
                if (c > 0) cycles.push_back({&n, c});
            }
        }
        if (!cycles.empty()) break;
    }
    const float threshold = r.was_moving ? 12.0f : 30.0f;  // starts walking above 30 units/s, stops below 12
    if (r.speed >= threshold && !cycles.empty()) {
        // the cycle made for the speed closest to the actor's (in ratio: 100 against 150 is nearer than 100 against 60 is far)
        const Cycle* best = &cycles.front();
        float best_d = 1e30f;
        for (const Cycle& c : cycles) {
            const float d = std::fabs(std::log(r.speed / c.cycle));
            if (d < best_d) {
                best_d = d;
                best = &c;
            }
        }
        out.name = *best->name;
        out.moving = true;
        out.rate = std::clamp(r.speed / best->cycle, 0.4f, 1.8f);
        return out;
    }
    // at rest
    if (stance != "Stand" && stance != "Sneak" && stance != "Wade") {  // sitting, sleeping, kneeling...: the stance's ambient loop (a bed has a left and a right one)
        if (const std::string* n = find_tail(names, stance, "Ambient_Loop_N_")) out.name = *n;
        return out;
    }
    if (!r.action.empty() && stance == "Stand") {  // an interaction: HandsOnHips, Think, ArmsCrossed, Dance, ... (not case-sensitive)
        const std::string want = lower(stance_part(stance) + r.action + "_Loop_N_Fwd");
        for (const std::string& n : names)
            if (contains(lower(n), want)) {
                out.name = n;
                return out;
            }
    }
    if (const std::string* n = find_tail(names, stance, "Move_Stand_N_Fwd")) out.name = *n;
    else if (const std::string* s = find_tail(names, "Stand", "Move_Stand_N_Fwd")) out.name = *s;
    return out;
}

MotionChoice choose_motion(const std::vector<std::string>& names, float speed, bool was_moving) {
    MotionRequest r;
    r.speed = speed;
    r.was_moving = was_moving;
    return choose_motion(names, r);
}

}  // namespace mk
