// NPC routines as the editor shows them (RE.md section 35): the points of a placed NPC's `routine` JSON (one post, work / relax / sleep, patrol), as offsets from the
// NPC's home, for the World view's overlay and gizmo. The JSON itself stays the model (npcs_panel edits the same object).
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/project.h"

namespace mk {

struct RoutinePoint {
    std::string key;    // "at", "work", "relax", "sleep" or "p<i>" (patrol waypoint i, from 0)
    std::string label;  // short text for the view ("work", "3")
    Vec3 d{0, 0, 0};    // offset from the NPC's home (game units)
    std::string kind;   // guard, lookat, ... ("" = the routine's default)
    std::optional<double> yaw;
};

// the points in the order the NPC visits them (day: work, relax, sleep; patrol: waypoints); empty for none / unknown shapes
std::vector<RoutinePoint> routine_points(const json& routine);
// true when the points are visited in a loop (a patrol), false for the one-way listing of a day routine
bool routine_is_loop(const json& routine);
// moves the point `key` to the offset `d`; false for an unknown key
bool routine_set_point(json& routine, const std::string& key, const Vec3& d);
// the point's absolute position for an NPC at `home`
Vec3 routine_world(const Vec3& home, const RoutinePoint& p);

}  // namespace mk
