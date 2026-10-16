#include "game/bindings.h"

#include <spdlog/spdlog.h>

#include "game/abi.h"
#include "game/loc_files.h"
#include "game/loc_overlay.h"
#include "game/lua_config.h"
#include "game/lua_input.h"
#include "game/lua_save.h"
#include "game/lua_entity.h"
#include "game/lua_test.h"
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
    vars.set_function("exists", [&s](const std::string& name) {
        bool r = false;
        if (s.gamevar_exists != nullptr) {
            const BString key(name);
            guarded([&] { r = s.gamevar_exists(key.get()); });
        }
        return r;
    });
    vars.set_function("remove", [&s](const std::string& name) {
        bool r = false;
        if (s.gamevar_remove != nullptr) {
            const BString key(name);
            guarded([&] { r = s.gamevar_remove(key.get()); });
        }
        return r;
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
    // Saving is a request too: the engine writes the savegame on its next processing step; watch save.after.
    world.set_function("quick_save", [&s] { return s.world_quick_save != nullptr && guarded([&] { s.world_quick_save(nullptr); }); });
    world.set_function("auto_save", [&s](const std::string& name) {
        const BString n(name);
        return s.world_auto_save != nullptr && guarded([&] { s.world_auto_save(nullptr, n.get()); });
    });
    world.set_function("is_save_allowed", [&s] {
        bool r = false;
        if (s.world_is_save_allowed != nullptr) {
            guarded([&] { r = s.world_is_save_allowed(nullptr); });
        }
        return r;
    });
    risen["world"] = world;
}

}  // namespace

void registerBindings() {
    auto& rt = lua::Runtime::instance();
    rt.addBinding(test::bindTest);  // first: other bindings may report through risen.test
    rt.addModBinding([](sol::state_view lua, sol::table api, const lua::ModInfo&) { test::bindTestForMod(lua, api); });
    rt.addBinding(bindQuest);
    rt.addBinding(bindGameVars);
    rt.addBinding(bindWorld);
    rt.addBinding(bindDebug);
    rt.addBinding(bindEngineInfo);
    rt.addBinding(bindLoc);
    rt.addBinding(bindEntity);
    rt.addBinding(bindItems);
    rt.addBinding(bindTemplates);  // after bindItems: extends risen.template
    rt.addModBinding([](sol::state_view lua, sol::table api, const lua::ModInfo&) { installBalanceModApi(lua, api); });  // risen.balance.install
    rt.addBinding(bindBalance);  // after bindItems/bindTemplates: extends risen.item, uses risen.template.patch
    rt.addBinding(bindAssetNatives);
    rt.addModBinding(bindAssetModApi);
    rt.addBinding(bindVoiceNatives);  // after bindAssetNatives
    rt.addBinding(bindVfs);  // after bindItems: adds risen.template.list
    rt.addBinding(bindNpcHelpers);
    rt.addBinding(bindNpcBalance);  // after bindNpcHelpers/bindBalance: adds to risen.npc
    rt.addBinding([](sol::state_view lua, sol::table risen) {  // after bindWorld/bindNpcHelpers: risen.npc.set_routine, risen.world.points/time (M10)
        bindRoutine(lua, risen);
        installRoutineHelpers(lua, risen);
    });
    rt.addBinding([](sol::state_view lua, sol::table risen) {  // after bindRoutine: risen.math, risen.world.raycast / line_of_sight / can_see / is_seen_by (M13)
        bindGeometry(lua, risen);
        installGeometryHelpers(lua, risen);
    });
    rt.addModBinding([](sol::state_view lua, sol::table api, const lua::ModInfo& mod) { installRandomModApi(lua, api, mod.id); });  // per-mod seedable streams (M13); risen.random.persist uses the mod's risen.save and event handlers when it is called
    rt.addBinding(bindTemplateCreate);  // after bindNpcBalance: risen.template.create and the new_items/new_npcs/templates sections
    rt.addBinding(bindSpell);           // after bindTemplateCreate: risen.spell and the spells/new_spells sections
    rt.addModBinding([](sol::state_view lua, sol::table api, const lua::ModInfo&) { installSpellModApi(lua, api); });  // after installBalanceModApi: per-mod shot overrides
    rt.addBinding(bindRecipe);          // after bindSpell: risen.recipe and the recipes/new_recipes sections
    rt.addBinding(bindInteractExtras);  // after bindRecipe: risen.interact.focus/info/...
    rt.addBinding(bindInput);           // risen.input
    rt.addBinding(bindHud);             // risen.hud
    rt.addModBinding(bindConfigModApi);  // risen.config(defaults) reads <mod dir>/config.toml
    rt.addModBinding([](sol::state_view lua, sol::table api, const lua::ModInfo&) { installRecipeModApi(lua, api); });  // after installBalanceModApi: learn = true recipes
    rt.addBinding(bindDialog);
    rt.addModBinding(bindVoiceModApi);  // after bindAssetModApi and bindDialog: wraps risen.dialog.create per mod
    rt.addBinding(bindQuestObjects);
    rt.addBinding(scripts::bindScripts);
    rt.addBinding(bindAi);  // after the script bridge: risen.ai (M11)
    bindSave();
    rt.addModBinding([](sol::state_view lua, sol::table api, const lua::ModInfo& mod) { installAiModApi(lua, api, mod.id); });  // after bindSave: risen.ai.start(persist = true) keeps its records in the mod's risen.save (M11)
    rt.addModBinding([](sol::state_view lua, sol::table api, const lua::ModInfo& mod) { installRoutineModApi(lua, api, mod.id); });  // after bindSave, before the persistent NPCs (their `routine` option calls risen.npc.set_routine)
    rt.addModBinding([](sol::state_view lua, sol::table api, const lua::ModInfo& mod) { installPersistentNpcModApi(lua, api, mod.id); });  // after bindSave: risen.npc.persistent keeps its records in the mod's risen.save
    rt.addModBinding([](sol::state_view lua, sol::table api, const lua::ModInfo& mod) { installSpawnWhenModApi(lua, api, mod.id); });  // after the persistent NPCs: risen.spawn.when / risen.cond (M8)
    rt.addModBinding([](sol::state_view lua, sol::table api, const lua::ModInfo& mod) { installDialogCondModApi(lua, api, mod.id); });  // after risen.cond and the voice wrapper: dialogue conditions (M9)
    bindModStrings();
}

}  // namespace spass::game
