#pragma once

#include <string>
#include <vector>

#include <sol/sol.hpp>

#include "lua/mods.h"

namespace spass::game {

// Registers the native `risen.*` API tables (quest, gamevar, ...) with the Lua runtime.
// Call before Runtime::start().
void registerBindings();

// risen.debug.* (file_trace.cpp) and the CreateFile hooks behind it (installed by installHooks()).
void bindDebug(sol::state_view lua, sol::table risen);
void installFileTrace();
// risen.debug.screenshot / screenshot_status (screenshot.cpp): a PNG of the D3D9 back buffer through a Present hook that is installed on the first request. Register after bindDebug.
void bindScreenshot(sol::state_view lua, sol::table risen);
// Starts tracing right away (config.toml [debug] trace_files); the same as risen.debug.trace_files(filter).
void setFileTraceFilter(const std::string& filter);

// risen.engine.* (lua_engine_info.cpp): which engine symbols were bound, and the game build.
void bindEngineInfo(sol::state_view lua, sol::table risen);

// risen.interact.focus/set_focus/info/current (lua_interact.cpp): adds to risen.interact (bound by bindRecipe, so register it after).
void bindInteractExtras(sol::state_view lua, sol::table risen);
void installFocusHook();
void focusEntityKilled(void* entity);  // eCEntity::Kill hook: unpins a pinned focus entity  // pins for risen.interact.set_focus(entity, true)

// risen.ai.* (lua_ai.cpp, M11): Lua AI states, AI callbacks, perception handlers and the AI registry. Register after the script bridge (it needs the script registry).
void bindAi(sol::state_view lua, sol::table risen);
void aiResetRuns();      // forgets the running Lua states (a savegame is loaded, the session starts / stops: the NPCs are rebuilt)
void aiFlushDeferred();  // runs the state changes that were asked for inside a `finish` callback (end of frame)
void aiEntityKilled(void* entity);  // eCEntity::Kill hook: an entity that is removed from the world leaves no exit script behind
void aiShutdown();       // releases every Lua reference (before the Lua state closes)
// Hot reload (Runtime::addTeardown): retires the AI states / callbacks and removes the perception handlers the mod registered; NPCs in its states go back to their routine.
void aiReleaseOwner(const std::string& mod, std::vector<std::string>& released);
// Per mod, after bindSave(): risen.ai.start learns `persist` (kept in the mod's risen.save and started again after a load), risen.ai.stop / forget / persisted. (ai_lua.h)
void installAiModApi(sol::state_view lua, sol::table api, const std::string& modId);

// risen.combat.* (lua_combat.cpp, M14): damage handlers on the script DoDamage. Register after the script bridge.
void bindCombat(sol::state_view lua, sol::table risen);
void combatShutdown();   // releases every Lua reference (before the Lua state closes)
void combatReleaseOwner(const std::string& mod, std::vector<std::string>& released);  // hot reload: removes the damage handlers the mod registered

// risen.mods.* (lua_mods.cpp, M15): the mod list, reload / unload / load, enable / disable, order, orphans and risen.retire's native part. Register last.
void bindMods(sol::state_view lua, sol::table risen);
// Per mod, after bindSave(): risen.migrate(from_version, fn) (migrate_lua.h).
void installMigrateModApi(sol::state_view lua, sol::table api, const lua::ModInfo& mod);
// Per mod: risen.retire{ npcs, quests, game_vars, items, save_keys } and the `retire` table of mod.toml (migrate_lua.h).
void installRetireModApi(sol::state_view lua, sol::table api, const lua::ModInfo& mod);

// risen.gui.* (lua_gui.cpp, M16): the engine's GUI pages and eCGuiWindow2 trees.
void bindGui(sol::state_view lua, sol::table risen);

// risen.hud.* (lua_hud.cpp): the HUD game log and item log.
void bindHud(sol::state_view lua, sol::table risen);

// The `calls` and `hud` trace channels (call_trace.cpp): installCallTrace needs Engine.dll/Game.dll, installHudTrace Script.dll (after PostInitialize).
void installCallTrace(const std::string& scriptFilter);
void installHudTrace();
int effectStartCount(const std::string& prefix);  // call_trace.cpp: how often the engine started effects whose name starts with `prefix` ("eff_*" only)
void installSoundTrace();  // the `sound` trace channel (Engine.dll + Script.dll)

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
