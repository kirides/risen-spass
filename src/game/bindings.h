#pragma once

#include <string>

#include <sol/sol.hpp>

namespace spass::game {

// Registers the native `risen.*` API tables (quest, gamevar, ...) with the Lua runtime.
// Call before Runtime::start().
void registerBindings();

// risen.debug.* (file_trace.cpp) and the CreateFile hooks behind it (installed by installHooks()).
void bindDebug(sol::state_view lua, sol::table risen);
void installFileTrace();
// Starts tracing right away (config.toml [debug] trace_files); the same as risen.debug.trace_files(filter).
void setFileTraceFilter(const std::string& filter);

// risen.engine.* (lua_engine_info.cpp): which engine symbols were bound, and the game build.
void bindEngineInfo(sol::state_view lua, sol::table risen);

// risen.vfs.* (lua_vfs.cpp): the engine's virtual file system, including packed archives.
void bindVfs(sol::state_view lua, sol::table risen);

}  // namespace spass::game
