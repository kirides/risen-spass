#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <tuple>

#include <spdlog/spdlog.h>

#include "game/abi.h"
#include "game/bindings.h"
#include "game/symbols.h"

// Renderer testing (docs/RE.md section 58, the D3D9-vs-Vulkan reference comparison of risen-enhanced-graphics): the engine's own clock controls.
//   eCApplication::SetAppState(8)          the debug "time freeze" (Script_Game OnDebugToggleTimeFreeze toggles 0 <-> 8); a one-store setter
//   eCTimer::SetTimeScaling(f)             multiplies the measured frame time (OnDebugToggleSloMo)
//   eCTimer::SetFixedFrameTime(ms)         every frame counts as `ms` long; Update sleeps when the real frame was shorter
// The camera, weather and console runner of the design come later in this file.

namespace spass::game {
namespace {

constexpr unsigned long kAppStateTimeFreeze = 8;
constexpr std::ptrdiff_t kGameTimeOffset = 0x10;  // eCTimer::GetGameTime = mov eax, [rcx+0x10]; ret (Engine.dll 0x4ceee0)

void* app() {
    const Symbols& s = sym();
    return s.app_instance != nullptr ? s.app_instance() : nullptr;
}

}  // namespace

bool timeFrozen() {
    const Symbols& s = sym();
    if (s.app_instance == nullptr || s.app_get_state == nullptr) {
        return false;
    }
    unsigned long state = 0;
    if (!guarded([&] {
            if (void* a = app()) {
                state = s.app_get_state(a);
            }
        })) {
        return false;
    }
    return state == kAppStateTimeFreeze;
}

void bindRender(sol::state_view, sol::table risen) {
    sol::table dbg = risen["debug"];
    using Two = std::tuple<sol::object, sol::object>;

    // risen.debug.app_state() -> integer | nil: eCApplication::GetAppState (0 running, 1 paused by a menu page, 8 time freeze)
    dbg.set_function("app_state", [](sol::this_state ts) -> sol::object {
        const Symbols& s = sym();
        if (s.app_instance == nullptr || s.app_get_state == nullptr) {
            return sol::make_object(ts, sol::lua_nil);
        }
        unsigned long state = 0;
        bool have = false;
        if (!guarded([&] {
                if (void* a = app()) {
                    state = s.app_get_state(a);
                    have = true;
                }
            }) ||
            !have) {
            return sol::make_object(ts, sol::lua_nil);
        }
        return sol::make_object(ts, static_cast<double>(state));
    });

    // risen.debug.time_freeze(on) -> true | nil, reason: app state 8 (the engine's debug time freeze) or back to 0 (running)
    dbg.set_function("time_freeze", [](sol::this_state ts, bool on) -> Two {
        sol::state_view L(ts);
        const auto fail = [&](const std::string& why) -> Two { return {sol::make_object(L, sol::lua_nil), sol::make_object(L, why)}; };
        const Symbols& s = sym();
        if (s.app_instance == nullptr || s.app_get_state == nullptr || s.app_set_state == nullptr) {
            return fail("engine function eCApplication::SetAppState is unavailable");
        }
        std::string why;
        if (!guarded([&] {
                void* a = app();
                if (a == nullptr) {
                    why = "no application";
                    return;
                }
                const unsigned long state = s.app_get_state(a);
                if (on && state != kAppStateTimeFreeze) {
                    s.app_set_state(a, kAppStateTimeFreeze);
                    spdlog::info("debug: time freeze on (app state {} -> 8)", state);
                } else if (!on && state == kAppStateTimeFreeze) {
                    s.app_set_state(a, 0);  // what OnDebugToggleTimeFreeze does
                    spdlog::info("debug: time freeze off (app state 8 -> 0)");
                }
            })) {
            return fail("the engine raised an exception");
        }
        if (!why.empty()) {
            return fail(why);
        }
        return {sol::make_object(L, true), sol::make_object(L, sol::lua_nil)};
    });

    // risen.debug.time_scale([f]) -> scale | nil, reason: eCTimer::SetTimeScaling(f), f in [0, 10] (1 = normal); without f only reads it
    dbg.set_function("time_scale", [](sol::this_state ts, sol::optional<double> f) -> Two {
        sol::state_view L(ts);
        const auto fail = [&](const std::string& why) -> Two { return {sol::make_object(L, sol::lua_nil), sol::make_object(L, why)}; };
        const Symbols& s = sym();
        if (s.timer_instance == nullptr || s.timer_get_scaling == nullptr || s.timer_set_scaling == nullptr) {
            return fail("engine function eCTimer::SetTimeScaling is unavailable");
        }
        if (f && !(std::isfinite(*f) && *f >= 0.0 && *f <= 10.0)) {
            return fail("the scale must be a number in [0, 10]");
        }
        float now = 0;
        if (!guarded([&] {
                void* t = s.timer_instance();
                if (f) {
                    s.timer_set_scaling(t, static_cast<float>(*f));
                }
                now = s.timer_get_scaling(t);
            })) {
            return fail("the engine raised an exception");
        }
        return {sol::make_object(L, static_cast<double>(now)), sol::make_object(L, sol::lua_nil)};
    });

    // risen.debug.timer() -> { frame_ms, game_ms, total_ms, scale, frame, playing_s } | nil: the engine timer's readings (eCTimer) and the world's playing time
    dbg.set_function("timer", [](sol::this_state ts) -> sol::object {
        sol::state_view L(ts);
        const Symbols& s = sym();
        if (s.timer_instance == nullptr) {
            return sol::make_object(L, sol::lua_nil);
        }
        sol::table t = L.create_table();
        if (!guarded([&] {
                const void* tm = s.timer_instance();
                if (s.timer_frame_time != nullptr) t["frame_ms"] = s.timer_frame_time(tm);
                if (s.timer_game_time != nullptr) t["game_ms"] = static_cast<double>(s.timer_game_time(tm));
                if (s.timer_total_time != nullptr) t["total_ms"] = static_cast<double>(s.timer_total_time(tm));
                if (s.timer_get_scaling != nullptr) t["scale"] = s.timer_get_scaling(tm);
                if (s.timer_master_frame != nullptr) t["frame"] = static_cast<double>(s.timer_master_frame(tm));
                if (s.world_playing_seconds != nullptr) t["playing_s"] = s.world_playing_seconds();
            })) {
            return sol::make_object(L, sol::lua_nil);
        }
        return sol::make_object(L, t);
    });

    // risen.debug.set_game_time(ms) -> true | nil, reason: writes eCTimer's game time (the field GetGameTime returns, +0x10, RE.md section 60).
    // The timed shader elements (texture scrollers/oscillators/rotators, animated colours: the sea's and the clouds' motion) are a
    // pure function of it (eCShaderMaterialAdmin::Process feeds them GameTime / 1000 every frame), so two runs that set the same
    // value a frame before the time freeze show the same phase. The layout is checked against GetGameTime before and after.
    dbg.set_function("set_game_time", [](sol::this_state ts, double ms) -> Two {
        sol::state_view L(ts);
        const auto fail = [&](const std::string& why) -> Two { return {sol::make_object(L, sol::lua_nil), sol::make_object(L, why)}; };
        const Symbols& s = sym();
        if (s.timer_instance == nullptr || s.timer_game_time == nullptr) {
            return fail("engine function eCTimer::GetGameTime is unavailable");
        }
        if (!(std::isfinite(ms) && ms >= 0.0 && ms <= 4000000000.0 && ms == std::floor(ms))) {
            return fail("ms must be a whole number in [0, 4000000000]");
        }
        std::string why;
        unsigned long before = 0;
        if (!guarded([&] {
                auto* t = static_cast<char*>(s.timer_instance());
                if (t == nullptr) {
                    why = "no engine timer";
                    return;
                }
                auto* field = reinterpret_cast<std::uint32_t*>(t + kGameTimeOffset);
                before = s.timer_game_time(t);
                if (*field != before) {
                    why = "unexpected eCTimer layout (GetGameTime does not read +0x10)";
                    return;
                }
                *field = static_cast<std::uint32_t>(ms);
                if (s.timer_game_time(t) != static_cast<unsigned long>(ms)) {
                    *field = static_cast<std::uint32_t>(before);
                    why = "the write did not take";
                }
            })) {
            return fail("the engine raised an exception");
        }
        if (!why.empty()) {
            return fail(why);
        }
        spdlog::info("debug: game time {} -> {} ms", before, static_cast<std::uint32_t>(ms));
        return {sol::make_object(L, true), sol::make_object(L, sol::lua_nil)};
    });

    // risen.debug.fixed_frame_time(ms) -> true | nil, reason: eCTimer::SetFixedFrameTime(ms), ms in [0, 1000]; 0 = off (the measured frame time)
    dbg.set_function("fixed_frame_time", [](sol::this_state ts, double ms) -> Two {
        sol::state_view L(ts);
        const auto fail = [&](const std::string& why) -> Two { return {sol::make_object(L, sol::lua_nil), sol::make_object(L, why)}; };
        const Symbols& s = sym();
        if (s.timer_instance == nullptr || s.timer_set_fixed_frame == nullptr) {
            return fail("engine function eCTimer::SetFixedFrameTime is unavailable");
        }
        if (!(std::isfinite(ms) && ms >= 0.0 && ms <= 1000.0)) {
            return fail("ms must be a number in [0, 1000]");
        }
        if (!guarded([&] { s.timer_set_fixed_frame(s.timer_instance(), static_cast<float>(ms)); })) {
            return fail("the engine raised an exception");
        }
        return {sol::make_object(L, true), sol::make_object(L, sol::lua_nil)};
    });
}

}  // namespace spass::game
