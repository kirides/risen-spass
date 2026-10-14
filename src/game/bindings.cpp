#include "game/bindings.h"

#include <spdlog/spdlog.h>

#include "game/abi.h"
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

void bindLoc(sol::state_view lua, sol::table risen) {
    // Localization (eCLocAdmin2). Strings are addressed by id as used in data/ini/loc.ini tables,
    // e.g. "Info_Diego_Hello" or "Quest_MyMod_Title".
    sol::table loc = lua.create_table();
    const Symbols& s = sym();
    loc.set_function("exists", [&s](const std::string& id) {
        bool r = false;
        const BString key(id);
        guarded([&] { r = s.loc_exists_id(s.loc_instance(), key.get()); });
        return r;
    });
    loc.set_function("get", [&s](const std::string& id) -> sol::optional<std::string> {
        const BString key(id);
        std::string text;
        bool found = false;
        const bool ok = guarded([&] {
            void* admin = s.loc_instance();
            if (!s.loc_exists_id(admin, key.get())) {
                return;
            }
            OutUString out;
            s.loc_get_string(admin, out.ptr(), key.get());
            text = out.take();
            found = true;
        });
        if (!ok || !found) {
            return sol::nullopt;
        }
        return text;
    });
    // risen.loc.language() -> name of the active text language (the column strings are read from).
    loc.set_function("language", [&s]() -> sol::optional<std::string> {
        std::string name;
        const bool ok = guarded([&] {
            OutString out;
            s.loc_get_text_language(s.loc_instance(), out.ptr());
            name = out.take();
        });
        if (!ok) {
            return sol::nullopt;
        }
        return name;
    });
    // risen.loc.set(id, text [, language]) -> bool. `text` is UTF-8. Defaults to the active text language.
    // The id is created if it does not exist (SetString inserts the key itself, see docs/RE.md §5).
    loc.set_function("set", [&s](const std::string& id, const std::string& text, sol::optional<std::string> language) {
        bool r = false;
        const BString key(id);
        const UString value(text);
        if (!guarded([&] {
                void* admin = s.loc_instance();
                std::string lang;
                if (language) {
                    lang = *language;
                } else {
                    OutString out;
                    s.loc_get_text_language(admin, out.ptr());
                    lang = out.take();
                }
                const BString langKey(lang);
                r = s.loc_set_string_lang(admin, langKey.get(), key.get(), value.get());
            })) {
            spdlog::error("risen.loc.set('{}'): engine raised an exception", id);
            return false;
        }
        return r;
    });
    risen["loc"] = loc;
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
    rt.addBinding(scripts::bindScripts);
}

}  // namespace spass::game
