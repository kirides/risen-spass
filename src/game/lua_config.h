#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <sol/sol.hpp>

#include "lua/mods.h"

namespace spass::game {

// Reads the options of a mod: `file` is a TOML file (usually `<mod dir>/config.toml`), `defaults` the Lua table of default values that also defines each
// option's type. Returns a new table with every default, overridden by the file's value when it has the same type (an integer fits a number default);
// nested tables merge. Everything that does not fit (a wrong type, an unknown key, a parse error) is described in `problems` and ignored. A missing
// file is not a problem. Pure of the engine: unit-tested.
sol::table loadModConfig(sol::state_view lua, const std::filesystem::path& file, const sol::table& defaults, std::vector<std::string>& problems);

// Adds `risen.config(defaults) -> table, problems` to the per-mod view (the file is `<mod dir>/config.toml`).
void bindConfigModApi(sol::state_view lua, sol::table api, const lua::ModInfo& mod);

}  // namespace spass::game
