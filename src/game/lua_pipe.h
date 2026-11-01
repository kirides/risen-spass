#pragma once

#include <string>
#include <vector>

#include <sol/sol.hpp>

#include "lua/mods.h"

namespace spass::game {

// risen.pipe (docs/LUA_API.md "Named pipes"): the game as CLIENT of a named pipe another local program hosts. The mod lists the pipe names it
// may open in `pipes = [...]` of its mod.toml. Lines are read with `Pipe:poll()` when the mod wants them (so a mod can leave them queued while no
// game session runs), written with `Pipe:send()`.
void bindPipeModApi(sol::state_view lua, sol::table api, const lua::ModInfo& mod);
// Hot reload / unload: the mod's pipes are closed.
void pipeReleaseOwner(const std::string& mod, std::vector<std::string>& released);
// Process end: every pipe thread is stopped.
void pipeShutdown();

}  // namespace spass::game
