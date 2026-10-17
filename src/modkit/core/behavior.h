// Behaviour scripts (risen.behavior, docs/LUA_API.md "Behaviours"): a Lua file of the project that returns the definition table
// (`return { params = {...}, interval = 0.5, update = function(self, dt) ... end, ... }`). The editor reads it to show the description, the parameters
// with their defaults (editable per NPC) and the handlers; the export checks it the same way before it copies the file into behaviors/ of the mod.
#pragma once

#include <string>
#include <vector>

#include "core/project.h"

namespace mk {

struct BehaviorInfo {
    bool ok = false;
    std::string error;                  // why the script is unusable (syntax error, no table, a key risen.behavior.define refuses)
    std::string description;
    json params = json::object();       // the defaults
    std::vector<std::string> handlers;  // start, update, on_quest, ... (in the order of risen.behavior's list)
    std::vector<std::string> events;    // the names in `events`
    double interval = -1, near = -1;    // -1: not set
};

// runs the code in an empty LuaJIT state (base, string, table, math; `risen` is a stand-in that accepts any call; an instruction limit) and checks the
// returned table like risen.behavior.define does
BehaviorInfo read_behavior_code(const std::string& code, const std::string& chunk_name);
// the same for a file; cached by path, size and modification time (thread-safe)
BehaviorInfo behavior_info(const std::string& path);

// the rule of risen.behavior.define: letters, digits, _ . -, up to 64 characters, not starting with a digit ("" = fine)
std::string behavior_name_problem(const std::string& name);
// a commented starter script for a new behaviour
std::string behavior_template(const std::string& name);

}  // namespace mk
