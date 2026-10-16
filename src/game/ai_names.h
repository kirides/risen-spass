#pragma once

#include <cstring>
#include <string>
#include <utility>

// Pure logic of the AI bridge (lua_ai.cpp, docs/LUA_API.md "AI states", docs/RE.md section 36); kept free of the engine so tests/ai_tests.inc can run it.

namespace spass::game::ai {

// The perception hooks: a friendly key of a Lua state's `on = { ... }` table -> the engine script that carries the stimulus. These are the "Assess"
// scripts the stock task callbacks run for every entity an NPC perceives (RE.md section 36): AssessActive once a second per entity in range, AssessDamage
// when the NPC was hit, AssessTalk when somebody wants to talk, ... A key that is not in the table is accepted when it is the name of an Assess* /
// Perceive* script itself.
struct Perception {
    const char* key;
    const char* script;
};

inline constexpr Perception kPerceptions[] = {
    {"notice", "AssessActive"},          // about once a second for every entity around (the sweep; the player included)
    {"player", "AssessPlayer"},          // the player was noticed
    {"attacker", "AssessAttacker"},
    {"enemy", "AssessEnemy"},
    {"threat", "AssessThreat"},
    {"nuisance", "AssessNuisance"},
    {"fighter", "AssessFighter"},
    {"avoider", "AssessAvoider"},
    {"damage", "AssessDamage"},          // the NPC itself was hit; other = the attacker
    {"other_damage", "AssessOtherDamage"},
    {"talk", "AssessTalk"},              // somebody wants to talk to the NPC
    {"theft", "AssessTheft"},
    {"dead", "AssessDead"},
    {"defeat", "AssessDefeat"},
    {"murder", "AssessMurder"},
    {"near_body", "AssessNearBody"},
    {"weapon", "AssessPlayerWeapon"},    // the player has a weapon drawn
    {"sneaking", "AssessPlayerSneaking"},
    {"looting", "AssessPlayerLooting"},
    {"stand_up", "AssessStandUp"},
    {"down", "AssessDown"},
};

// The engine script behind `key`, or an empty string when the key is neither an alias nor an Assess*/Perceive* script name.
inline std::string perceptionScript(const std::string& key) {
    for (const Perception& p : kPerceptions) {
        if (key == p.key) {
            return p.script;
        }
    }
    if ((key.rfind("Assess", 0) == 0 || key.rfind("Perceive", 0) == 0) && key.size() > 6) {
        return key;
    }
    return {};
}

// Names of states and callbacks: letters, digits and underscores, 1..100 characters, not starting with a digit. Returns the reason, or an empty string when valid.
inline std::string validateName(const std::string& name) {
    if (name.empty()) {
        return "the name is empty";
    }
    if (name.size() > 100) {
        return "the name is longer than 100 characters";
    }
    if (name[0] >= '0' && name[0] <= '9') {
        return "the name must not start with a digit";
    }
    for (const char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) {
            return "the name may only contain letters, digits and underscores";
        }
    }
    return {};
}

// What a `loop` (or `begin`) callback returned: nothing / true = go on; false or "done" = the state is finished (back to the NPC's routine); any other
// string = finished, and that AI state follows.
struct LoopResult {
    bool end = false;
    std::string next;
};

inline LoopResult interpretLoopResult(bool isNil, bool isBool, bool boolValue, bool isString, const std::string& text) {
    LoopResult r;
    if (isNil) {
        return r;
    }
    if (isBool) {
        r.end = !boolValue;
        return r;
    }
    if (isString) {
        r.end = true;
        if (text != "done" && !text.empty()) {
            r.next = text;
        }
    }
    return r;
}

// Whether the `loop` callback is due: always for the first call, after that every `interval` seconds of state time. A state time that went backwards means
// the state was restarted (SetCurrentAIState zeroes it), which counts as due as well.
inline bool dueForLoop(int loopsSoFar, double stateTime, double lastLoopTime, double interval) {
    if (loopsSoFar <= 0) {
        return true;
    }
    if (stateTime < lastLoopTime) {
        return true;
    }
    return stateTime - lastLoopTime >= interval;
}

// The reason `finish` receives: what the Lua side asked for ("done", "stopped", "error"), else the engine replaced the state ("replaced").
inline std::string finishReason(const std::string& requested) { return requested.empty() ? std::string("replaced") : requested; }

// The loop interval in seconds from a state definition: a missing value is the default (0.25 s), a number is clamped to 0 (every frame) .. 60 s.
inline double normalizeInterval(bool present, double value) {
    if (!present) {
        return 0.25;
    }
    if (!(value >= 0.0)) {  // also NaN
        return 0.0;
    }
    return value > 60.0 ? 60.0 : value;
}

}  // namespace spass::game::ai
