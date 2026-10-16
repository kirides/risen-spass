#include <spdlog/spdlog.h>

#include "game/lua_entity.h"
#include "game/persistent_npc_lua.h"
#include "game/spawn_when_lua.h"

// risen.npc.persistent (docs/RE.md section 32, docs/LUA_API.md "Persistent NPCs"): the per-mod part (the declarations, their records in risen.save and the
// handlers that restore them) is Lua, see persistent_npc_lua.h. risen.spawn.when / risen.cond (conditional spawning, M8) is a second chunk on top of it, see spawn_when_lua.h.

namespace spass::game {

void installPersistentNpcModApi(sol::state_view lua, sol::table api, const std::string& modId) {
    sol::load_result loaded = lua.load(kPersistentNpcChunk, "=risen.npc.persistent");
    if (!loaded.valid()) {
        spdlog::error("lua: persistent npc api failed to load: {}", loaded.get<sol::error>().what());
        return;
    }
    sol::protected_function fn = loaded;
    sol::protected_function_result res = fn(api, modId);
    if (!res.valid()) {
        spdlog::error("lua: persistent npc api failed to run: {}", res.get<sol::error>().what());
    }
}

void installSpawnWhenModApi(sol::state_view lua, sol::table api, const std::string& modId) {
    sol::load_result loaded = lua.load(kSpawnWhenChunk, "=risen.spawn");
    if (!loaded.valid()) {
        spdlog::error("lua: spawn-when api failed to load: {}", loaded.get<sol::error>().what());
        return;
    }
    sol::protected_function fn = loaded;
    sol::protected_function_result res = fn(api, modId);
    if (!res.valid()) {
        spdlog::error("lua: spawn-when api failed to run: {}", res.get<sol::error>().what());
    }
}

}  // namespace spass::game
