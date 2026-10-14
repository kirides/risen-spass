#include "game/bindings.h"

#include <spdlog/spdlog.h>

#include "game/abi.h"
#include "game/loc_overlay.h"
#include "game/lua_entity.h"
#include "game/script_bridge.h"
#include "lua/runtime.h"

namespace spass::game {
namespace {

using QuestFn = bool (*)(const void*);

// Calls a `static bool QuestManager::X(const bCString&)` export. Returns false (and logs) if the
// engine raised a hardware exception, e.g. because the quest manager does not exist yet.
bool callQuest(const char* what, QuestFn fn, const std::string& name) {
    if (fn == nullptr) {
        spdlog::error("risen.quest.{}: engine function unavailable", what);
        return false;
    }
    bool result = false;
    const BString s(name);
    const bool ok = guarded([&] { result = fn(s.get()); });
    if (!ok) {
        spdlog::error("risen.quest.{}('{}'): engine raised an exception (game not ready?)", what, name);
        return false;
    }
    return result;
}

void bindQuest(sol::state_view lua, sol::table risen) {
    sol::table quest = lua.create_table();
    const Symbols& s = sym();
    // Names are the quest ids used in the game's quest definitions, e.g. "Q_Example".
    quest.set_function("open", [&s](const std::string& n) { return callQuest("open", s.quest_open, n); });
    quest.set_function("run", [&s](const std::string& n) { return callQuest("run", s.quest_run, n); });
    quest.set_function("close", [&s](const std::string& n) { return callQuest("close", s.quest_close, n); });
    quest.set_function("fail", [&s](const std::string& n) { return callQuest("fail", s.quest_fail, n); });
    quest.set_function("succeed", [&s](const std::string& n) { return callQuest("succeed", s.quest_succeed, n); });
    quest.set_function("is_running",
                       [&s](const std::string& n) { return callQuest("is_running", s.quest_is_running, n); });
    risen["quest"] = quest;
}

void bindGameVars(sol::state_view lua, sol::table risen) {
    // The "game variables" are the integer flags dialogue/info scripts read and write
    // (InfoSystem::Get/SetGameVariable).
    sol::table vars = lua.create_table();
    const Symbols& s = sym();
    vars.set_function("get", [&s](const std::string& name) -> sol::optional<int> {
        if (s.gamevar_get == nullptr) {
            return sol::nullopt;
        }
        const BString key(name);
        int value = 0;
        bool found = false;
        if (!guarded([&] { found = s.gamevar_get(key.get(), &value); }) || !found) {
            return sol::nullopt;
        }
        return value;
    });
    vars.set_function("set", [&s](const std::string& name, int value) {
        if (s.gamevar_set == nullptr) {
            return false;
        }
        const BString key(name);
        bool ok = false;
        return guarded([&] { ok = s.gamevar_set(key.get(), value); }) && ok;
    });
    risen["gamevar"] = vars;
}

void bindWorld(sol::state_view lua, sol::table risen) {
    // Session-level actions (Script.dll `World`, whose `this` is unused: verified in the decompilation).
    // new_game/quick_load are *requests*: the engine performs them on its next processing step.
    sol::table world = lua.create_table();
    const Symbols& s = sym();
    world.set_function("is_game_running", [&s] {
        bool r = false;
        guarded([&] { r = s.world_is_game_running(nullptr); });
        return r;
    });
    world.set_function("new_game", [&s] { return guarded([&] { s.world_new_game(nullptr); }); });
    world.set_function("quick_load", [&s] { return guarded([&] { s.world_quick_load(nullptr); }); });
    risen["world"] = world;
}

}  // namespace

void registerBindings() {
    auto& rt = lua::Runtime::instance();
    rt.addBinding(bindQuest);
    rt.addBinding(bindGameVars);
    rt.addBinding(bindWorld);
    rt.addBinding(bindLoc);
    rt.addBinding(bindEntity);
    rt.addBinding(bindItems);
    rt.addBinding(bindNpcHelpers);
    rt.addBinding(scripts::bindScripts);
}

}  // namespace spass::game
