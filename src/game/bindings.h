#pragma once

#include <string>

#include <sol/sol.hpp>

#include "lua/mods.h"

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

// risen.interact.focus/set_focus/info/current (lua_interact.cpp): adds to risen.interact (bound by bindRecipe, so register it after).
void bindInteractExtras(sol::state_view lua, sol::table risen);
void installFocusHook();
void focusEntityKilled(void* entity);  // eCEntity::Kill hook: unpins a pinned focus entity  // pins for risen.interact.set_focus(entity, true)

// risen.hud.* (lua_hud.cpp): the HUD game log and item log.
void bindHud(sol::state_view lua, sol::table risen);

// The `calls` and `hud` trace channels (call_trace.cpp): installCallTrace needs Engine.dll/Game.dll, installHudTrace Script.dll (after PostInitialize).
void installCallTrace(const std::string& scriptFilter);
void installHudTrace();

// risen.vfs.* (lua_vfs.cpp): the engine's virtual file system, including packed archives.
void bindVfs(sol::state_view lua, sol::table risen);

// risen.assets natives (lua_assets.cpp): resource state, GUI bitmaps.
void bindAssetNatives(sol::state_view lua, sol::table risen);
void bindAssetModApi(sol::state_view lua, sol::table api, const lua::ModInfo& mod);  // risen.assets.write/mount of the calling mod

// risen.voice (lua_voice.cpp): custom voice lines. The hook needs Script.dll (installed from installScriptEventHooks()).
void installVoiceHooks();
void bindVoiceNatives(sol::state_view lua, sol::table risen);  // after bindAssetNatives: also adds risen.assets.sound
void bindVoiceModApi(sol::state_view lua, sol::table api, const lua::ModInfo& mod);  // risen.assets.import_sound, risen.voice.set/check/load_dir, risen.dialog.create(sound)

}  // namespace spass::game
