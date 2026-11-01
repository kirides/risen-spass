// Which of a family's motions the live mirror (stages E35d / E35e) plays for an actor. The game does not report the animation it plays, so the editor derives it
// from what it can see: the horizontal speed between two samples (stage E35d), and what the actor's AI routine says it is doing (E35e: the stance
// `gCScriptRoutine_PS.AniState` = Stand, SitBench, SleepBed, Kneel, ...; the interaction's `ActionString` = HandsOnHips, Think, Dance, ...; dead).
// Motion files are named `<Family>_<stance>_None_None_P0_<action>_<variant>_N_<Fwd|Left|Right|Back>_00_%_00_P0_<speed>` (docs/RE.md section 66); for the move
// cycles the last number is the speed (units per second) the cycle was made for, so the playback rate that keeps the feet on the ground is `speed / cycle speed`.
#pragma once

#include <string>
#include <vector>

namespace mk {

struct MotionChoice {
    std::string name;     // the file name of the motion ("" = none: the bind pose)
    float rate = 1.0f;    // playback speed
    bool moving = false;  // a walking / running cycle (the caller feeds it back for the hysteresis)
};

struct MotionRequest {
    float speed = 0;           // units per second
    bool was_moving = false;   // what the last call said, so an actor hovering around the threshold does not flicker between standing and walking
    bool dead = false;
    std::string stance;        // "Stand" (also when empty), "SitBench", "SleepGround", ... without the `gEAniState_` prefix
    std::string action;        // the routine's ActionString ("HandsOnHips", "Think", ...), "" = none
};

// `names`: the motion file names of one family (any others are ignored).
MotionChoice choose_motion(const std::vector<std::string>& names, const MotionRequest& request);

// the plain walk / run / sprint of an actor at ease (E35d's signature)
MotionChoice choose_motion(const std::vector<std::string>& names, float speed, bool was_moving);

// the speed a move cycle was made for, from its file name (`..._P0_150._xmot` -> 150); 0 when the name has none
float motion_cycle_speed(const std::string& name);

}  // namespace mk
