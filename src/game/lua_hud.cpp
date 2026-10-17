#include <string>
#include <tuple>

#include <spdlog/spdlog.h>

#include "game/abi.h"
#include "game/bindings.h"
#include "game/lua_entity.h"

// risen.hud (docs/RE.md section 31, docs/LUA_API.md "HUD"): the on-screen game log the engine's scripts print to.
//   GUI2::PrintGameLog(text, type, flag, extra)   the text line (HUD page "log"); `*gui2 == 1` (the first byte of the `gui2` object) means the GUI exists
//   GUI2::PrintItemLog(Entity item, amount)       the item line with icon and name ("+ 2 Apple"); the entity is passed by value (a one-pointer holder)

namespace spass::game {

void bindHud(sol::state_view lua, sol::table risen) {
    sol::table hud = lua.create_table();
    risen["hud"] = hud;

    using Two = std::tuple<sol::object, sol::object>;
    const auto fail = [](sol::state_view L, const std::string& reason) -> Two { return {sol::make_object(L, sol::lua_nil), sol::make_object(L, reason)}; };
    const auto ok = [](sol::state_view L) -> Two { return {sol::make_object(L, true), sol::make_object(L, sol::lua_nil)}; };

    // risen.hud.log(text [, type]) -> true | nil, reason: prints a line into the HUD's game log (type = the engine's gELogMessageType, default 0)
    hud.set_function("log", [fail, ok](sol::this_state ts, const std::string& text, sol::optional<int> type) -> Two {
        sol::state_view L(ts);
        const Symbols& s = sym();
        if (s.gui_print_log == nullptr || s.gui2_object == nullptr) {
            return fail(L, "engine function GUI2::PrintGameLog is unavailable");
        }
        if (!guarded([&] {
                const UString u(text);
                s.gui_print_log(s.gui2_object, u.get(), type.value_or(0), false, nullptr);
            })) {
            return fail(L, "the engine raised an exception");
        }
        return ok(L);
    });

    // risen.hud.item_log(item [, amount]) -> true | nil, reason: the HUD's item line (icon, name, amount). `item` is a template name ("It_Apple") or an
    // item entity (the one lying in the world, or an inventory template item).
    hud.set_function("item_log", [fail, ok](sol::this_state ts, sol::object item, sol::optional<int> amount) -> Two {
        sol::state_view L(ts);
        const Symbols& s = sym();
        if (s.gui_print_item_log == nullptr || s.gui2_object == nullptr) {
            return fail(L, "engine function GUI2::PrintItemLog is unavailable");
        }
        void* entity = nullptr;
        if (item.get_type() == sol::type::string) {
            std::string error;
            entity = templateEntityPtr(item.as<std::string>(), error);
            if (entity == nullptr) {
                return fail(L, error);
            }
        } else if (item.is<LuaEntity>()) {
            entity = ptr(item.as<LuaEntity>());
            if (entity == nullptr) {
                return fail(L, "entity is gone");
            }
        } else {
            return fail(L, "expected a template name or an item entity");
        }
        if (!guarded([&] {
                EntityHolder byValue{entity};  // Entity is one pointer with a trivial destructor
                s.gui_print_item_log(s.gui2_object, &byValue, amount.value_or(1));
            })) {
            return fail(L, "the engine raised an exception");
        }
        return ok(L);
    });

    // risen.hud.state() -> { mode = number, browsing = bool, loot = bool, game = bool, pages = { gEHudPage numbers that are visible } } | nil: what the GUI shows (browsing = an inventory / loot / map /
    // menu page is open; game = the plain game view; loot = the loot window)
    // risen.hud.tutorial_stop(name) -> true | nil, reason: ends the tutorial hint `name` (`Tutorial::StopEvent(name, true)`: what the game's take/loot scripts do for "takeitem" and
    // "loot"). risen.hud.tutorial_done(name) -> boolean | nil: `Tutorial::IsCompleted`.
    const auto tutorial_name_ok = [](const std::string& n) { return !n.empty() && n.size() <= 64 && n.find_first_of("\r\n\t") == std::string::npos; };
    hud.set_function("tutorial_stop", [fail, ok, tutorial_name_ok](sol::this_state ts, const std::string& name) -> Two {
        sol::state_view L(ts);
        const Symbols& s = sym();
        if (s.tutorial_object == nullptr || s.tut_stop_event == nullptr) {
            return fail(L, "engine function Tutorial::StopEvent is unavailable");
        }
        if (!tutorial_name_ok(name)) {
            return fail(L, "the tutorial name must be 1..64 characters without line breaks");
        }
        if (!guarded([&] {
                ByValueString n(name);  // by value: the callee destroys it
                s.tut_stop_event(s.tutorial_object, n.ptr(), true);
            })) {
            return fail(L, "the engine raised an exception");
        }
        return ok(L);
    });
    hud.set_function("tutorial_done", [tutorial_name_ok](sol::this_state ts, const std::string& name) -> sol::object {
        sol::state_view L(ts);
        const Symbols& s = sym();
        if (s.tutorial_object == nullptr || s.tut_is_completed == nullptr || !tutorial_name_ok(name)) {
            return sol::make_object(L, sol::lua_nil);
        }
        bool done = false;
        if (!guarded([&] {
                ByValueString n(name);
                done = s.tut_is_completed(s.tutorial_object, n.ptr());
            })) {
            return sol::make_object(L, sol::lua_nil);
        }
        return sol::make_object(L, done);
    });

    hud.set_function("state", [](sol::this_state ts) -> sol::object {
        sol::state_view L(ts);
        const Symbols& s = sym();
        if (s.gui2_object == nullptr || s.gui_is_browsing == nullptr) {
            return sol::make_object(L, sol::lua_nil);
        }
        sol::table t = L.create_table();
        bool good = guarded([&] {
            t["browsing"] = s.gui_is_browsing(s.gui2_object);
            t["loot"] = s.gui_is_browsing_loot != nullptr && s.gui_is_browsing_loot(s.gui2_object);
            t["game"] = s.gui_is_browsing_game != nullptr && s.gui_is_browsing_game(s.gui2_object);
            t["mode"] = s.gui_get_mode != nullptr ? s.gui_get_mode(s.gui2_object) : -1;
            sol::table pages = L.create_table();
            int n = 0;
            if (s.gui_is_visible != nullptr) {
                for (int page = 0; page < 40; ++page) {
                    if (s.gui_is_visible(s.gui2_object, page)) {
                        pages[++n] = page;
                    }
                }
            }
            t["pages"] = pages;
        });
        return good ? sol::make_object(L, t) : sol::make_object(L, sol::lua_nil);
    });
}

}  // namespace spass::game
