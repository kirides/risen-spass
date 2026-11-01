// Which of a family's motions the live mirror (stage E35d) plays for an actor that moves at a given speed. The game does not report the animation it plays, so
// the editor derives it from what it sees: the horizontal speed between two samples. Motion files are named
// `<Family>_Stand_None_None_P0_Move_<Stand|Walk|Run|Sprint>_N_Fwd_00_%_00_P0_<speed>` (docs/RE.md section 66); the last number is the speed (units per
// second) the cycle was made for, so the playback rate that keeps the feet on the ground is `speed / cycle speed`.
#pragma once

#include <string>
#include <vector>

namespace mk {

struct MotionChoice {
    std::string name;     // the file name of the motion ("" = none: the bind pose)
    float rate = 1.0f;    // playback speed
    bool moving = false;  // a walking / running cycle (the caller feeds it back for the hysteresis)
};

// `names`: the motion file names of one family (any others are ignored). `speed`: units per second. `was_moving`: what the last call said, so an actor
// hovering around the threshold does not flicker between standing and walking.
MotionChoice choose_motion(const std::vector<std::string>& names, float speed, bool was_moving);

// the speed a move cycle was made for, from its file name (`..._P0_150._xmot` -> 150); 0 when the name has none
float motion_cycle_speed(const std::string& name);

}  // namespace mk
