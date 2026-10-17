#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <sol/sol.hpp>

namespace spass::game {

// risen.options (docs/LUA_API.md "Options menu"): a mod declares its options as a table of entries, the mod manager shows them in the game's menu and changes them while the
// game runs (the mod's `on_change` applies the new value). Registry, validation and stepping are a Lua chunk; the stored choices of the player are a TOML file per mod.
void bindOptions(sol::state_view lua, sol::table risen);

// Hot reload: forgets the options a torn-down mod instance defined.
void optionsReleaseOwner(const std::string& mod, std::vector<std::string>& released);

// `<spass dir>/options/<mod>.toml`: the choices the player made in the menu (only the values that differ from the mod's defaults); empty when the layer has no
// directory to keep it in. risen.config() merges this file on top of config.toml.
std::filesystem::path optionsStorePath(const std::string& modId);

}  // namespace spass::game
