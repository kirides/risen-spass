#include "game/lua_input.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>

#include <windows.h>

#include <spdlog/spdlog.h>

#include "common/input_names.h"
#include "common/trace.h"
#include "game/abi.h"
#include "game/bindings.h"
#include "hook/hook_manager.h"
#include "lua/runtime.h"

// risen.input (docs/RE.md section 31, docs/LUA_API.md "Input").
//
// Two kinds of keys:
//   * ACTION keys: the engine's gEActionKey (0..0x57, names such as "PrimaryAction", "SecondaryAction", "Jump"): the input map (per game mode: normal, melee,
//     bow, magic, inventory, ...) turns scan codes and mouse buttons into actions. The state is `gCActionAdmin+0x60+key`; Script.dll's `Input` wrappers
//     (`Input::IsKeyPressed`, `WasKeyPressedThisFrame`, `GetKeyPressDuration`, `GetCurrentActionEvent`) read it. The names are the table of gSActionKeyInfo
//     (`0x28` bytes each, a bCString first, Game.dll RVA 0x11c1fb0).
//   * RAW keys: Windows virtual keys, polled with GetAsyncKeyState, only while the game window is the foreground window.
// `was_pressed` / `was_released` compare the state at the last two `tick`s (sampled by onFrame() once a mod asked for input at all).
//
// The gameplay input reaches the game as action events: ProcessBegin calls gCActionAdmin::SendActionEvent for every event of the frame (pressed, held every frame,
// released, analog), which runs the camera, the key script of the current player state and gCCharacterControl::OnAction. SendActionEvent is hooked: a Lua
// handler of `input.action` that returns a value other than nil/false consumes the event.

namespace spass::game {
namespace {

constexpr int kActionCount = 0x58;

struct KeyRef {
    bool action = false;
    int id = 0;  // gEActionKey or virtual key
};

struct Snapshot {
    std::array<bool, kActionCount> action{};
    std::array<bool, 256> vk{};
};

std::mutex g_mutex;
bool g_used = false;           // some mod asked for input: sample every frame
bool g_haveState = false;
std::uint64_t g_frame = 0;
Snapshot g_cur;
Snapshot g_prev;
std::array<bool, 256> g_simVk{};  // risen.input.simulate on raw keys
std::array<double, 256 + kActionCount> g_downSince{};  // seconds (GetTickCount64 / 1000) since the key went down, 0 = up
std::vector<std::string> g_actionNames;                  // by key
std::map<std::string, int> g_actionByName;               // folded name -> key

Hook<void (*)(void*, const void*)> g_sendAction;

const char* phaseName(int phase) {
    switch (phase) {
        case 1: return "pressed";
        case 2: return "released";
        case 3: return "held";
        case 4: return "analog";
        default: return "unknown";
    }
}

std::string bstrAt(const void* p) {
    std::string s;
    if (p != nullptr) {
        s = sym().bstring_get_text(p);
    }
    return s;
}

void loadActionNames() {
    if (!g_actionNames.empty()) {
        return;
    }
    const Symbols& s = sym();
    if (s.action_key_info == nullptr) {
        return;
    }
    std::vector<std::string> names(kActionCount);
    const bool ok = guarded([&] {
        for (int i = 0; i < kActionCount; ++i) {
            const void* info = s.action_key_info(i);
            names[i] = info != nullptr ? bstrAt(info) : std::string();  // gSActionKeyInfo: the name is the first member
        }
    });
    if (!ok) {
        return;
    }
    g_actionNames = std::move(names);
    for (int i = 0; i < kActionCount; ++i) {
        if (!g_actionNames[i].empty()) {
            g_actionByName.emplace(input::foldName(g_actionNames[i]), i);
        }
    }
}

// "SecondaryAction" | "RButton" | "action:Name" | "vk:Name" -> key, or an error text
bool resolve(const std::string& raw, KeyRef& out, std::string& error) {
    std::string name = std::string(input::trim(raw));
    bool forceAction = false;
    bool forceVk = false;
    if (name.rfind("action:", 0) == 0) {
        forceAction = true;
        name = name.substr(7);
    } else if (name.rfind("vk:", 0) == 0) {
        forceVk = true;
        name = name.substr(3);
    }
    if (name.empty()) {
        error = "empty key name";
        return false;
    }
    if (!forceVk) {
        loadActionNames();
        const auto it = g_actionByName.find(input::foldName(name));
        if (it != g_actionByName.end()) {
            out = {true, it->second};
            return true;
        }
        if (forceAction) {
            error = "unknown action '" + name + "' (risen.input.actions() lists them)";
            return false;
        }
    }
    if (const auto vk = input::vkFromName(name)) {
        out = {false, *vk};
        return true;
    }
    error = "unknown key '" + name + "' (an action name from risen.input.actions() or a Windows virtual-key name such as RButton, V, F5, Shift)";
    return false;
}

bool gameWindowActive() {
    const HWND fg = GetForegroundWindow();
    if (fg == nullptr) {
        return false;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

bool rawDown(int vk) {
    if (vk < 0 || vk > 255) {
        return false;
    }
    if (g_simVk[vk]) {
        return true;
    }
    if (!gameWindowActive()) {
        return false;
    }
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

bool actionDown(int key) {
    const Symbols& s = sym();
    if (s.input_is_pressed == nullptr || s.input_object == nullptr || key < 0 || key >= kActionCount) {
        return false;
    }
    bool down = false;
    guarded([&] { down = s.input_is_pressed(s.input_object, key); });
    return down;
}

void sample(Snapshot& into) {
    for (int k = 0; k < kActionCount; ++k) {
        into.action[k] = actionDown(k);
    }
    for (int v = 1; v < 256; ++v) {
        into.vk[v] = rawDown(v);
    }
}

double nowSeconds() { return static_cast<double>(GetTickCount64()) / 1000.0; }

void updateTimes() {
    const double now = nowSeconds();
    for (int k = 0; k < kActionCount; ++k) {
        double& since = g_downSince[256 + k];
        if (g_cur.action[k]) {
            if (since == 0) since = now;
        } else {
            since = 0;
        }
    }
    for (int v = 1; v < 256; ++v) {
        double& since = g_downSince[v];
        if (g_cur.vk[v]) {
            if (since == 0) since = now;
        } else {
            since = 0;
        }
    }
}

// Called by the first query of a mod: starts the per-frame sampling.
void ensureSampling() {
    if (!g_used) {
        g_used = true;
        sample(g_cur);
        g_prev = g_cur;
        g_haveState = true;
        updateTimes();
    }
}

bool nowDown(const KeyRef& k) { return k.action ? g_cur.action[k.id] : g_cur.vk[k.id]; }
bool beforeDown(const KeyRef& k) { return k.action ? g_prev.action[k.id] : g_prev.vk[k.id]; }

using Two = std::tuple<sol::object, sol::object>;
Two okValue(sol::state_view L, sol::object v) { return {std::move(v), sol::make_object(L, sol::lua_nil)}; }
Two failValue(sol::state_view L, const std::string& reason) { return {sol::make_object(L, sol::lua_nil), sol::make_object(L, reason)}; }

void* actionAdmin() {
    const Symbols& s = sym();
    void* admin = nullptr;
    if (s.module_admin_instance == nullptr || s.module_find_by_class == nullptr) {
        return nullptr;
    }
    guarded([&] {
        const BString cls("gCActionAdmin");
        admin = s.module_find_by_class(s.module_admin_instance(), cls.get());
    });
    return admin;
}

// gCActionAdmin::SendActionEvent: every action event (key press / hold / release, analog look and move) passes here before the camera, the key script
// of the current player state (OnNormalHandleInput, OnMeleeHandleInput, ... which call OnPlayerGameKeyPressed) and the character control see it. A Lua
// handler of `input.action` that returns a value other than nil/false consumes the event: the original is not called at all.
void detourSendAction(void* self, const void* action) {
    auto& rt = lua::Runtime::instance();
    if (action != nullptr && rt.running() && rt.hasHandlers("input.action")) {
        int key = -1;
        int phase = 0;
        float duration = 0;
        float value = 0;
        guarded([&] {  // gSAction: { int key; int phase; float duration; float amount }
            const auto* ev = static_cast<const unsigned char*>(action);
            std::memcpy(&key, ev, 4);
            std::memcpy(&phase, ev + 4, 4);
            std::memcpy(&duration, ev + 8, 4);
            std::memcpy(&value, ev + 12, 4);
        });
        if (key >= 0 && key < kActionCount) {
            auto lock = rt.lock();
            sol::state_view L = rt.lua();
            loadActionNames();
            const std::string name = key < static_cast<int>(g_actionNames.size()) ? g_actionNames[key] : std::to_string(key);
            sol::table ev = L.create_table();
            ev["key"] = key;
            ev["action"] = name;
            ev["phase"] = std::string(phaseName(phase));
            ev["value"] = value;
            ev["duration"] = duration;
            sol::object r = rt.emitUntilHandled("input.action", ev);
            bool consumed = false;
            if (r.valid() && r.get_type() != sol::type::lua_nil) {
                consumed = !(r.get_type() == sol::type::boolean && !r.as<bool>());
            }
            if (consumed) {
                SPASS_TRACE(trace::Channel::Events, "input.action({}, {}) consumed by a mod: the engine does not see the event", name, phaseName(phase));
                return;
            }
        }
    }
    g_sendAction.original(self, action);
}

}  // namespace

void onInputFrame() {
    if (!g_used) {
        return;
    }
    std::lock_guard lock(g_mutex);
    ++g_frame;
    g_prev = g_cur;
    sample(g_cur);
    updateTimes();
}

void installInputHooks() {
    const Symbols& s = sym();
    if (s.action_send_event == nullptr) {
        spdlog::warn("event hook 'gCActionAdmin::SendActionEvent' unavailable: the input.action event will not fire");
        return;
    }
    if (!g_sendAction.install("gCActionAdmin::SendActionEvent", s.action_send_event, &detourSendAction)) {
        spdlog::warn("event hook 'gCActionAdmin::SendActionEvent' could not be installed");
    }
}

void bindInput(sol::state_view lua, sol::table risen) {
    sol::table input = lua.create_table();
    risen["input"] = input;

    // risen.input.actions() -> { "PrimaryAction", ... } the names of the engine's action keys, index = key number
    input.set_function("actions", [](sol::this_state ts) {
        sol::state_view L(ts);
        std::lock_guard lock(g_mutex);
        loadActionNames();
        sol::table out = L.create_table();
        for (std::size_t i = 0; i < g_actionNames.size(); ++i) {
            out[i + 1] = g_actionNames[i];
        }
        return out;
    });

    // risen.input.action_key(name) -> number | nil, reason: the gEActionKey of an action name (nil for raw keys)
    input.set_function("action_key", [](sol::this_state ts, const std::string& name) -> Two {
        sol::state_view L(ts);
        std::lock_guard lock(g_mutex);
        KeyRef k;
        std::string error;
        if (!resolve(name, k, error)) {
            return failValue(L, error);
        }
        if (!k.action) {
            return failValue(L, "'" + name + "' is a raw key, not an action");
        }
        return okValue(L, sol::make_object(L, k.id));
    });

    const auto query = [](const char* what, auto fn) {
        return [what, fn](sol::this_state ts, const std::string& name) -> Two {
            sol::state_view L(ts);
            std::lock_guard lock(g_mutex);
            KeyRef k;
            std::string error;
            if (!resolve(name, k, error)) {
                return failValue(L, error);
            }
            ensureSampling();
            (void)what;
            return okValue(L, sol::make_object(L, fn(k)));
        };
    };
    // risen.input.pressed(name) -> bool | nil, reason: the key is held down right now (as of the last tick)
    input.set_function("pressed", query("pressed", [](const KeyRef& k) { return nowDown(k); }));
    // risen.input.was_pressed(name): the key went down since the previous tick
    input.set_function("was_pressed", query("was_pressed", [](const KeyRef& k) { return nowDown(k) && !beforeDown(k); }));
    // risen.input.was_released(name): the key went up since the previous tick
    input.set_function("was_released", query("was_released", [](const KeyRef& k) { return !nowDown(k) && beforeDown(k); }));
    // risen.input.held_for(name) -> seconds the key has been down (0 when up)
    input.set_function("held_for", query("held_for", [](const KeyRef& k) {
        const double since = g_downSince[k.action ? 256 + k.id : k.id];
        return since == 0 ? 0.0 : nowSeconds() - since;
    }));

    // risen.input.modifiers() -> { shift = bool, ctrl = bool, alt = bool } (raw keys, only while the game window is in the foreground)
    input.set_function("modifiers", [](sol::this_state ts) {
        sol::state_view L(ts);
        sol::table t = L.create_table();
        t["shift"] = rawDown(0x10);
        t["ctrl"] = rawDown(0x11);
        t["alt"] = rawDown(0x12);
        return t;
    });

    // risen.input.window_active() -> bool: the game window has the keyboard focus (raw keys are only read then)
    input.set_function("window_active", [] { return gameWindowActive(); });

    // risen.input.bindings([name]) -> { { scan = n, text = "Right Mouse Button" }, ... } | { [action] = {...}, ... }: the scan codes (keys, mouse buttons, pad
    // buttons) the engine's CURRENT input map (it changes with the game mode: walking, melee, inventory, ...) turns into the action; without an
    // argument every bound action. An empty list means the action cannot be triggered in the current mode.
    input.set_function("bindings", [](sol::this_state ts, sol::optional<std::string> name) -> Two {
        sol::state_view L(ts);
        std::lock_guard lock(g_mutex);
        const Symbols& s = sym();
        if (s.action_for_scan == nullptr) {
            return failValue(L, "engine function gCActionAdmin::GetActionKey(scan code) is unavailable");
        }
        loadActionNames();
        int wanted = -1;
        if (name) {
            KeyRef k;
            std::string error;
            if (!resolve(*name, k, error)) {
                return failValue(L, error);
            }
            if (!k.action) {
                return failValue(L, "'" + *name + "' is a raw key, not an action");
            }
            wanted = k.id;
        }
        sol::table all = L.create_table();
        guarded([&] {
            for (int scan = 1; scan <= 0x318; ++scan) {
                const int key = s.action_for_scan(scan);
                if (key <= 0 || key >= kActionCount || (wanted >= 0 && key != wanted)) {
                    continue;
                }
                std::string text;
                if (s.scan_description != nullptr) {
                    OutUString out;
                    s.scan_description(out.ptr(), scan);
                    text = out.take();
                }
                sol::table entry = L.create_table();
                entry["scan"] = scan;
                entry["text"] = text;
                const std::string action = key < static_cast<int>(g_actionNames.size()) ? g_actionNames[key] : std::to_string(key);
                if (wanted >= 0) {
                    all[all.size() + 1] = entry;
                } else {
                    sol::object list = all[action];
                    if (!list.valid() || list.get_type() != sol::type::table) {
                        all[action] = L.create_table();
                    }
                    sol::table t = all[action];
                    t[t.size() + 1] = entry;
                }
            }
        });
        return okValue(L, sol::make_object(L, all));
    });

    // risen.input.simulate(name, down) -> bool | nil, reason: test aid. An action key is pressed/released at the input admin (gCActionAdmin::KeyPressed /
    // KeyReleased, what the input device does), so the engine handles it in its next frame exactly like a real key (the `input.action` event,
    // PS_Normal_Loop, ...). A raw key is made to read as down for risen.input.pressed (it reaches no game code).
    input.set_function("simulate", [](sol::this_state ts, const std::string& name, bool down) -> Two {
        sol::state_view L(ts);
        std::lock_guard lock(g_mutex);
        KeyRef k;
        std::string error;
        if (!resolve(name, k, error)) {
            return failValue(L, error);
        }
        if (!k.action) {
            g_simVk[k.id] = down;
            return okValue(L, sol::make_object(L, true));
        }
        const Symbols& s = sym();
        void* admin = actionAdmin();
        const auto fn = down ? s.action_key_pressed : s.action_key_released;
        if (admin == nullptr || fn == nullptr) {
            return failValue(L, "the engine's input admin is unavailable");
        }
        // the exported KeyPressed/KeyReleased belong to the eIInputReceiver base, which sits at +0x18 of the admin (its constructor, RE.md section 31)
        if (!guarded([&] { fn(static_cast<char*>(admin) + 0x18, static_cast<unsigned long>(k.id)); })) {
            return failValue(L, "the engine raised an exception");
        }
        return okValue(L, sol::make_object(L, true));
    });
}

}  // namespace spass::game
