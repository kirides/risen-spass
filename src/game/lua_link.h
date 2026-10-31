#pragma once

#include <string>
#include <vector>

#include <sol/sol.hpp>

#include "lua/mods.h"

namespace spass::game {

// The live link (docs/LUA_API.md "Live link"): `[link] enabled = true` in config.toml starts a named pipe `\\.\pipe\risen-spass-<pid>` (or `[link] name`),
// described by <spass>\link.json, through which an editor reloads mods, lists them and follows the log of the running game.

// Starts the pipe when enabled (after the Lua runtime started). `pipeName` empty = "risen-spass-<pid>".
void linkInit(bool enabled, const std::string& pipeName, const std::string& instance);
void linkShutdown();

// Once per frame on the game thread: runs the received commands and raises `link.connected` / `link.disconnected`.
void linkPump();

// Per-mod view: risen.link.{available,connected,on,send}.
void bindLinkModApi(sol::state_view lua, sol::table api, const lua::ModInfo& mod);
// Hot reload: the commands the mod registered are removed.
void linkReleaseOwner(const std::string& mod, std::vector<std::string>& released);

}  // namespace spass::game
