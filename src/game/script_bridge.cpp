#include "game/script_bridge.h"

#include <array>
#include <atomic>
#include <utility>

#include <windows.h>

#include <spdlog/spdlog.h>

#include "common/trace.h"
#include "game/abi.h"
#include "game/lua_entity.h"
#include "lua/runtime.h"

// How Risen finds scripts (docs/RE.md §4): a global registry maps names to `eSScriptBase`, whose function
// slot (+0x20) is read on every call by gCScriptAdmin::RunScript. Replacing that slot with one of the
// thunks below makes the game call into Lua, without any inline hook.
//
// Every script has the signature  __int64 f(gCScriptProcessingUnit*, void* a, void* b, __int64 arg),
// but the signature has no context pointer, so each Lua script needs its own thunk. They are
// pre-instantiated templates (no executable memory allocation, no JIT).

namespace spass::game::scripts {
namespace {

using ScriptFn = std::int64_t (*)(void* unit, void* a, void* b, std::int64_t arg);

constexpr std::size_t kScriptFnOffset = 0x20;
constexpr std::size_t kMaxSlots = 1024;

struct Slot {
    bool used = false;
    std::string name;
    void** fnSlot = nullptr;         // &eSScriptBase->function
    ScriptFn original = nullptr;     // what was registered before (nullptr for new scripts)
    sol::main_protected_function lua;
};

std::array<Slot, kMaxSlots> g_slots;
std::atomic<void*> g_admin{nullptr};
thread_local int g_luaCallDepth = 0;  // risen.script.call in progress
bool g_layoutOk = false;

// What a Lua script receives.
struct ScriptContext {
    void* unit = nullptr;
    void* a = nullptr;
    void* b = nullptr;
    std::int64_t arg = 0;
    std::size_t slot = 0;
};

std::int64_t runOriginal(const Slot& s, void* unit, void* a, void* b, std::int64_t arg) {
    return s.original != nullptr ? s.original(unit, a, b, arg) : 0;
}

std::int64_t toResult(const sol::object& o) {
    switch (o.get_type()) {
        case sol::type::number:
            return static_cast<std::int64_t>(o.as<double>());
        case sol::type::boolean:
            return o.as<bool>() ? 1 : 0;
        default:
            return 0;
    }
}

std::int64_t dispatch(std::size_t idx, void* unit, void* a, void* b, std::int64_t arg) noexcept {
    Slot& s = g_slots[idx];
    try {
        auto& rt = lua::Runtime::instance();
        auto lock = rt.lock();
        if (!rt.running() || !s.lua.valid()) {
            return runOriginal(s, unit, a, b, arg);
        }
        SPASS_TRACE(trace::Channel::Script, "engine calls Lua script '{}' (arg {})", s.name, arg);
        sol::protected_function_result r = s.lua(ScriptContext{unit, a, b, arg, idx});
        if (!r.valid()) {
            SPASS_TRACE(trace::Channel::Lua, "script '{}' failed: {}", s.name, std::string(r.get<sol::error>().what()));
            const sol::error err = r;
            spdlog::error("lua: script '{}' failed: {}", s.name, err.what());
            return runOriginal(s, unit, a, b, arg);
        }
        return toResult(r.get<sol::object>());
    } catch (const std::exception& e) {
        spdlog::error("lua: script '{}' threw: {}", s.name, e.what());
    } catch (...) {
        spdlog::error("lua: script '{}' threw an unknown exception", s.name);
    }
    return 0;
}

template <std::size_t I>
std::int64_t thunk(void* unit, void* a, void* b, std::int64_t arg) {
    return dispatch(I, unit, a, b, arg);
}

template <std::size_t... I>
constexpr std::array<ScriptFn, sizeof...(I)> makeThunks(std::index_sequence<I...>) {
    return {&thunk<I>...};
}

constexpr auto kThunks = makeThunks(std::make_index_sequence<kMaxSlots>{});

// Looks up (or creates) the registry entry for `name`; returns the address of its function slot.
void** registryFnSlot(const std::string& name, bool create) {
    const Symbols& s = sym();
    void** result = nullptr;
    const bool ok = guarded([&] {
        void* map = s.script_map_instance();
        const BString key(name);
        void* entry = create ? s.script_map_register(map, key.get()) : s.script_map_get(map, key.get());
        if (entry != nullptr) {
            result = reinterpret_cast<void**>(static_cast<char*>(entry) + kScriptFnOffset);
        }
    });
    return ok ? result : nullptr;
}

std::size_t findSlotByFn(void** fnSlot) {
    for (std::size_t i = 0; i < kMaxSlots; ++i) {
        if (g_slots[i].used && g_slots[i].fnSlot == fnSlot) {
            return i;
        }
    }
    return kMaxSlots;
}

// Shared by define() and override(). Returns an error message, or empty on success.
std::string install(const std::string& name, sol::main_protected_function fn, bool isOverride) {
    if (!g_layoutOk) {
        return "script bridge disabled (registry layout self-test failed, see log)";
    }
    if (name.empty() || name.size() > 200) {
        return "invalid script name";
    }
    void** fnSlot = registryFnSlot(name, /*create=*/true);
    if (fnSlot == nullptr) {
        return "could not access the script registry (game not ready?)";
    }

    // Hot swap: this entry already points to one of our thunks.
    if (const std::size_t existing = findSlotByFn(fnSlot); existing != kMaxSlots) {
        g_slots[existing].lua = std::move(fn);
        return {};
    }

    const auto current = reinterpret_cast<ScriptFn>(*fnSlot);
    if (!isOverride && current != nullptr) {
        return "script '" + name + "' already exists; use risen.script.override() to replace it";
    }
    if (isOverride && current == nullptr) {
        spdlog::warn("lua: override('{}'): no such script was registered, defining a new one", name);
    }

    std::size_t idx = 0;
    while (idx < kMaxSlots && g_slots[idx].used) {
        ++idx;
    }
    if (idx == kMaxSlots) {
        return "too many Lua scripts (limit " + std::to_string(kMaxSlots) + ")";
    }
    Slot& slot = g_slots[idx];
    slot.used = true;
    slot.name = name;
    slot.fnSlot = fnSlot;
    slot.original = current;
    slot.lua = std::move(fn);
    *fnSlot = reinterpret_cast<void*>(kThunks[idx]);
    spdlog::info("lua: script '{}' {} (slot {})", name, isOverride ? "overridden" : "defined", idx);
    SPASS_TRACE(trace::Channel::Script, "{} '{}' (slot {}, original {})", isOverride ? "override" : "define", name, idx, current != nullptr ? "kept" : "none");
    return {};
}

}  // namespace

void setAdmin(void* admin) { g_admin = admin; }

bool inLuaScriptCall() { return g_luaCallDepth > 0; }

bool selfTest() {
    // Compare the registry against the real Script_Game.dll export table for a handful of scripts.
    const HMODULE mod = GetModuleHandleW(L"Script_Game.dll");
    if (mod == nullptr) {
        spdlog::warn("scripts: Script_Game.dll is not loaded, cannot validate the registry layout");
        g_layoutOk = false;
        return false;
    }
    static const char* const kProbe[] = {
        "?AssessActive@@YA_JPEAVgCScriptProcessingUnit@@PEAX1_J@Z",
        "?AssessDamage@@YA_JPEAVgCScriptProcessingUnit@@PEAX1_J@Z",
        "?Arrow_OnHit@@YA_JPEAVgCScriptProcessingUnit@@PEAX1_J@Z",
    };
    int checked = 0;
    for (const char* sym : kProbe) {
        const void* expected = reinterpret_cast<const void*>(GetProcAddress(mod, sym));
        if (expected == nullptr) {
            continue;
        }
        // Registry names are the undecorated function names (the key used by game data).
        std::string name = sym + 1;
        name = name.substr(0, name.find('@'));
        void** fnSlot = registryFnSlot(name, /*create=*/false);
        if (fnSlot == nullptr || *fnSlot != expected) {
            spdlog::error("scripts: layout self-test failed for '{}' (registry {}, export {})", name,
                          fnSlot ? *fnSlot : nullptr, expected);
            g_layoutOk = false;
            return false;
        }
        ++checked;
    }
    g_layoutOk = checked > 0;
    if (g_layoutOk) {
        spdlog::info("scripts: registry layout verified against {} Script_Game.dll export(s)", checked);
    } else {
        spdlog::error("scripts: no probe script found, registry layout unverified");
    }
    return g_layoutOk;
}

void restoreAll() {
    for (Slot& s : g_slots) {
        if (s.used && s.fnSlot != nullptr) {
            *s.fnSlot = reinterpret_cast<void*>(s.original);
        }
        s = Slot{};
    }
}

void bindScripts(sol::state_view lua, sol::table risen) {
    lua.new_usertype<ScriptContext>(
        "ScriptContext", sol::no_constructor,
        "self", sol::property([](const ScriptContext& c, sol::this_state ts) -> sol::object {
            void* e = nullptr;
            guarded([&] { e = sym().unit_get_self(c.unit); });
            return makeEntityObject(ts, e);
        }),
        "other", sol::property([](const ScriptContext& c, sol::this_state ts) -> sol::object {
            void* e = nullptr;
            guarded([&] { e = sym().unit_get_other(c.unit); });
            return makeEntityObject(ts, e);
        }),
        "arg", sol::property([](const ScriptContext& c) { return c.arg; }),
        "a", sol::property([](const ScriptContext& c) { return reinterpret_cast<std::uintptr_t>(c.a); }),
        "b", sol::property([](const ScriptContext& c) { return reinterpret_cast<std::uintptr_t>(c.b); }),
        "name", sol::property([](const ScriptContext& c) { return g_slots[c.slot].name; }),
        // Calls the function that was registered before override() (0 for define()d scripts).
        "call_original", [](const ScriptContext& c) -> std::int64_t {
            const Slot& s = g_slots[c.slot];
            std::int64_t r = 0;
            if (!guarded([&] { r = runOriginal(s, c.unit, c.a, c.b, c.arg); })) {
                spdlog::error("lua: original of script '{}' raised an exception", s.name);
            }
            return r;
        });

    sol::table script = lua.create_table();
    // risen.script.define("MyScript", function(ctx) ... return 0 end)  -- new script name
    script.set_function("define", [](const std::string& name, sol::main_protected_function fn) {
        const std::string err = install(name, std::move(fn), /*isOverride=*/false);
        if (!err.empty()) {
            throw sol::error("risen.script.define: " + err);
        }
    });
    // risen.script.override("AssessActive", function(ctx) return ctx:call_original() end)
    script.set_function("override", [](const std::string& name, sol::main_protected_function fn) {
        const std::string err = install(name, std::move(fn), /*isOverride=*/true);
        if (!err.empty()) {
            throw sol::error("risen.script.override: " + err);
        }
    });
    script.set_function("exists", [](const std::string& name) {
        void** fnSlot = registryFnSlot(name, /*create=*/false);
        return fnSlot != nullptr && *fnSlot != nullptr;
    });
    // risen.script.call("Name", self_entity|nil, other_entity|nil, arg) -> integer result
    script.set_function("call", [](const std::string& name, sol::optional<LuaEntity> self,
                                   sol::optional<LuaEntity> other, sol::optional<std::int64_t> arg) -> std::int64_t {
        void* admin = g_admin.load();
        if (admin == nullptr) {
            throw sol::error("risen.script.call: script system is not initialised yet");
        }
        std::int64_t result = 0;
        ++g_luaCallDepth;
        const bool ok = guarded([&] {
            const BString n(name);
            result = sym().script_admin_call(admin, n.get(), self ? ptr(*self) : nullptr, other ? ptr(*other) : nullptr,
                                             arg.value_or(0));
        });
        --g_luaCallDepth;
        SPASS_TRACE(trace::Channel::Script, "call '{}' -> {}{}", name, result, ok ? "" : " (engine exception)");
        if (!ok) {
            throw sol::error("risen.script.call('" + name + "'): engine raised an exception");
        }
        return result;
    });
    risen["script"] = script;
}

}  // namespace spass::game::scripts
