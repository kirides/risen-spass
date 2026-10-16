#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <windows.h>

#include <spdlog/spdlog.h>

#include "common/trace.h"
#include "game/abi.h"
#include "game/ai_lua.h"
#include "game/ai_names.h"
#include "game/bindings.h"
#include "game/event_bus.h"
#include "game/lua_entity.h"
#include "game/script_bridge.h"
#include "lua/runtime.h"

// AI scripting (docs/RE.md section 36, docs/LUA_API.md "AI states and perception").
//
// The engine's NPC AI is a stack of "script states" (gCScriptProcessingUnit, embedded in the NPC's gCScriptRoutine_PS): an AI state is a native function
// `bool ZS_X(bTObjStack<gScriptRunTimeSingleState>&, gCScriptProcessingUnit*)` found by name in the AIState namespace of the script registry (the same
// registry the script bridge swaps function slots in, only another hash map). Every frame in which the NPC has no instruction running (a walk, an animation, a
// wait) the engine calls the top state; returning true means "pass finished, start from the first step next time" (a loop), the step counter lives in the
// stack frame. A task callback (AICallback namespace, `bool cb(unit*)`, roughly once a second) runs after it: for the routine states that is `OnRoutine`,
// which sweeps the perceivable entities and calls the `Assess*` scripts (AssessActive -> AssessEnemy, AssessNuisance, ...).
//
// This file registers Lua functions in those namespaces with pre-instantiated thunks (like the script bridge does for plain scripts):
//   * risen.ai.state(name, { begin, loop, finish, on = { notice = fn, damage = fn, ... }, task, interval }) defines a Lua AI state. The thunk runs `begin` once per
//     start, `loop` every `interval` seconds of state time, and an exit script (AISetExitScript, run by SetCurrentAIState when anything replaces the state) calls
//     `finish`.
//   * the perception hooks intercept the Assess* scripts natively: the interceptor looks the NPC up in the table of running Lua states and only enters Lua when
//     that state has a handler for the script (every other NPC pays one hash lookup).
//   * risen.ai.callback(name, fn) defines an AI callback (usable as a state's task callback, `task = "MyCallback"` or `task = function`).
// Every callback runs under the Lua runtime lock; a Lua error is a warning and ends the state, never an exception into the engine.

namespace spass::game {
namespace {

using Two = std::tuple<sol::object, sol::object>;

Two okResult(sol::state_view lua, sol::object value) { return {std::move(value), sol::make_object(lua, sol::lua_nil)}; }
Two failResult(sol::state_view lua, const std::string& reason) {
    return {sol::make_object(lua, sol::lua_nil), sol::make_object(lua, reason)};
}

constexpr std::size_t kMaxStates = 128;
constexpr std::size_t kMaxCallbacks = 128;
constexpr std::size_t kEntryFn = 0x20;  // eSScriptBase: the function slot
const char* const kExitScript = "SPASS_AI_Exit";
const char* const kFallbackState = "ZS_HangAround";

enum class Kind { State = 0, Function = 1, Callback = 2, Script = 3 };
constexpr std::size_t kMapOffset[] = {0x00, 0x20, 0x40, 0x60};  // the four hash maps of eCScriptFunctionMap (RE.md section 36)
const char* const kKindNames[] = {"state", "function", "callback", "script"};

bool parseKind(const std::string& s, Kind& out) {
    for (int i = 0; i < 4; ++i) {
        if (s == kKindNames[i]) {
            out = static_cast<Kind>(i);
            return true;
        }
    }
    return false;
}

bool stringArg(const sol::object& o, std::string& out) {
    if (!o.valid() || o.get_type() != sol::type::string) {
        return false;
    }
    out = o.as<std::string>();
    return true;
}

bool entityArg(const sol::object& o, LuaEntity& out) {
    if (!o.valid() || !o.is<LuaEntity>()) {
        return false;
    }
    out = o.as<LuaEntity>();
    return true;
}

// ---- the registry ---------------------------------------------------------------------------------------------------------------------------

struct Entry {
    std::string name;
    void* base = nullptr;  // eSScriptBase*
    void* fn = nullptr;
};

bool haveRegistry() { return sym().script_map_instance != nullptr && sym().bstring_get_text != nullptr; }

// Walks one of the four hash maps: array of buckets at map+8 (count at map+0x10), nodes chained through node+0x38, key bCString at node+0, the eSScriptBase value at node+8.
bool enumerate(Kind kind, std::vector<Entry>& out) {
    const Symbols& s = sym();
    if (!haveRegistry()) {
        return false;
    }
    return guarded([&] {
        char* map = static_cast<char*>(s.script_map_instance()) + kMapOffset[static_cast<int>(kind)];
        char** buckets = *reinterpret_cast<char***>(map + 8);
        const unsigned count = *reinterpret_cast<unsigned*>(map + 0x10);
        if (buckets == nullptr || count == 0 || count > (1u << 22)) {
            return;
        }
        std::size_t guard = 0;
        for (unsigned i = 0; i < count; ++i) {
            for (char* node = buckets[i]; node != nullptr && guard < 2000000; node = *reinterpret_cast<char**>(node + 0x38), ++guard) {
                Entry e;
                e.name = s.bstring_get_text(node);
                e.base = node + 8;
                e.fn = *reinterpret_cast<void**>(node + 0x28);
                out.push_back(std::move(e));
            }
        }
    });
}

// The registry entry of `name` in a namespace (nullptr when there is none; does not create it).
void* lookupEntry(Kind kind, const std::string& name) {
    const Symbols& s = sym();
    if (!haveRegistry()) {
        return nullptr;
    }
    void* r = nullptr;
    guarded([&] {
        void* map = s.script_map_instance();
        const BString key(name);
        switch (kind) {
            case Kind::State: r = s.script_map_get_state != nullptr ? s.script_map_get_state(map, key.get()) : nullptr; break;
            case Kind::Function: r = s.script_map_get_function != nullptr ? s.script_map_get_function(map, key.get()) : nullptr; break;
            case Kind::Callback: r = s.script_map_get_callback != nullptr ? s.script_map_get_callback(map, key.get()) : nullptr; break;
            case Kind::Script: r = s.script_map_get != nullptr ? s.script_map_get(map, key.get()) : nullptr; break;
        }
    });
    return r;
}

// Get-or-create (state / callback only: the namespaces the bridge defines).
void* registerEntry(Kind kind, const std::string& name) {
    const Symbols& s = sym();
    if (!haveRegistry()) {
        return nullptr;
    }
    void* r = nullptr;
    guarded([&] {
        void* map = s.script_map_instance();
        const BString key(name);
        if (kind == Kind::State && s.script_map_register_state != nullptr) {
            r = s.script_map_register_state(map, key.get());
        } else if (kind == Kind::Callback && s.script_map_register_callback != nullptr) {
            r = s.script_map_register_callback(map, key.get());
        }
    });
    return r;
}

void* entryFn(void* entry) {
    void* fn = nullptr;
    if (entry != nullptr) {
        guarded([&] { fn = *reinterpret_cast<void**>(static_cast<char*>(entry) + kEntryFn); });
    }
    return fn;
}

bool entryExists(Kind kind, const std::string& name) { return entryFn(lookupEntry(kind, name)) != nullptr; }

// A registry entry is only "valid" for the engine's script proxies when its module record (eSScriptBase+0x18, the Script_*.dll that registered it) is set:
// eCScriptProxy::IsScriptValid is `entry != 0 && entry->module != 0` and UpdateScript / ProcessExitScript skip a frame whose proxies are not valid
// (RE.md section 36). Entries made by the bridge borrow the record of a stock entry; nothing reads it except that test and the unload of the DLL.
void* g_moduleRecord = nullptr;

void adoptModuleRecord(void* entry) {
    if (entry == nullptr || g_moduleRecord == nullptr) {
        return;
    }
    guarded([&] {
        void** field = reinterpret_cast<void**>(static_cast<char*>(entry) + 0x18);
        if (*field == nullptr) {
            *field = g_moduleRecord;
        }
    });
}

// Which module a code address belongs to ("Script_Game.dll") and its offset in it.
std::pair<std::string, std::uintptr_t> moduleOf(void* fn) {
    HMODULE h = nullptr;
    if (fn == nullptr || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(fn), &h) ||
        h == nullptr) {
        return {"", 0};
    }
    wchar_t buf[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(h, buf, MAX_PATH);
    std::wstring path(buf, n);
    const auto slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        path = path.substr(slash + 1);
    }
    std::string name;
    for (const wchar_t c : path) {
        name.push_back(static_cast<char>(c));
    }
    return {name, reinterpret_cast<std::uintptr_t>(fn) - reinterpret_cast<std::uintptr_t>(h)};
}

// ---- state definitions, runs ----------------------------------------------------------------------------------------------------------------

struct StateDef {
    bool used = false;
    std::string name;
    void** fnSlot = nullptr;
    sol::main_protected_function begin, loop, finish;
    std::map<std::string, sol::main_protected_function> on;  // engine script name -> handler
    std::string task;                                          // AI callback made the task callback at begin ("" = leave the NPC's)
    double interval = 0.25;
    std::string owner;                                         // the mod that defined it (hot reload retires it)
    bool retired = false;                                      // the defining mod was torn down: the entry stays registered but does nothing and sends NPCs back to their routine
};

struct CallbackDef {
    bool used = false;
    std::string name;
    void** fnSlot = nullptr;
    sol::main_protected_function fn;
    bool failed = false;
    std::string owner;
};

std::array<StateDef, kMaxStates> g_states;
std::array<CallbackDef, kMaxCallbacks> g_callbacks;

struct AiRun {
    std::uint64_t id = 0;
    std::size_t def = 0;
    void* self = nullptr;
    void* unit = nullptr;
    sol::object args;     // what risen.ai.start was given (nil after a load or when the engine started the state)
    sol::table data;      // scratch space of the run
    bool resumed = false;
    int loops = 0;
    double startedAt = 0;
    double lastLoop = 0;
    double lastTime = 0;
    std::string endReason;    // "done" / "stopped" / "error" once this side ended the run
    std::string next;         // the state to go to when it ends ("" = back to the routine)
    bool endRequested = false;
    bool leaving = false;     // the leave was started; the engine did not replace the state (yet)
};

std::unordered_map<void*, AiRun> g_runs;  // by eCEntity*
struct Pending {
    std::string state;
    sol::object args;
    bool resumed = false;   // risen.ai.start(..., { resumed = true }): the run counts as resumed (the layer restores persisted states with it)
};
std::unordered_map<void*, Pending> g_pending;
// A start that waits for the NPC's first engine frame: an NPC made by Entity::Spawn has no bound script unit yet (the trace shows "unit of -"), and the engine's own
// initialisation runs ZS_HangAround on it in that first frame, replacing a state set before it.
struct WaitingStart {
    LuaEntity npc;
    std::string name;
    sol::object args;
    bool interrupt = true;
    bool resumed = false;
    int frames = 0;
};
std::vector<WaitingStart> g_waiting;
std::uint64_t g_nextRunId = 1;
int g_perceiveDepth = 0;                            // > 0 while a perception handler runs inside the engine's Assess* chain: the code after it may still set a state of its own
int g_finishDepth = 0;                              // > 0 while the engine runs an exit script (SetCurrentAIState is mid-flight: state changes must wait)
std::vector<std::function<void()>> g_deferred;
sol::table g_store;                                 // main-state table used to turn a value of a coroutine into a reference of the main state
std::uint64_t g_storeKey = 1;
bool g_ready = false;                               // the exit script is installed
std::set<std::string> g_hookedScripts;

sol::object toMain(const sol::object& o) {
    if (!o.valid() || o.get_type() == sol::type::lua_nil) {
        return sol::object{};
    }
    const std::uint64_t key = g_storeKey++;
    g_store[key] = o;
    sol::object r = g_store[key];
    g_store[key] = sol::lua_nil;
    return r;
}

void* selfOf(void* unit) {
    void* e = nullptr;
    if (unit != nullptr && sym().unit_get_self != nullptr) {
        guarded([&] { e = sym().unit_get_self(unit); });
    }
    return e;
}

void* otherOf(void* unit) {
    void* e = nullptr;
    if (unit != nullptr && sym().unit_get_other != nullptr) {
        guarded([&] { e = sym().unit_get_other(unit); });
    }
    return e;
}

double stateTimeOf(void* unit) {
    float t = 0;
    if (unit != nullptr && sym().unit_get_state_time != nullptr) {
        guarded([&] { t = sym().unit_get_state_time(unit); });
    }
    return t;
}

bool setEngineState(void* entity, const std::string& name) {
    const Symbols& s = sym();
    if (entity == nullptr || s.rt_set_state == nullptr) {
        return false;
    }
    return guarded([&] {
        EntityHolder h{entity};
        ByValueString n(name);  // by value: the callee destroys it
        s.rt_set_state(&h, n.ptr());
    });
}

std::string engineStateOf(void* entity) {
    const Symbols& s = sym();
    std::string out;
    if (entity == nullptr || s.rt_get_state == nullptr) {
        return out;
    }
    guarded([&] {
        EntityHolder h{entity};
        OutString o;
        s.rt_get_state(&h, o.ptr());
        out = o.take();
    });
    return out;
}

// The script unit of an NPC (embedded in its gCScriptRoutine_PS at +0x118), or nullptr.
void* unitOfEntity(void* e) {
    void* ps = nullptr;
    if (e != nullptr && sym().ent_get_property_set != nullptr) {
        guarded([&] {
            const BString cls("gCScriptRoutine_PS");
            ps = sym().ent_get_property_set(e, cls.get());
        });
    }
    return ps != nullptr ? static_cast<char*>(ps) + 0x118 : nullptr;
}

// Whether the NPC went through its first engine frame (his unit knows him).
bool unitBound(void* e) {
    void* unit = unitOfEntity(e);
    return unit != nullptr && selfOf(unit) == e;
}

// ---- the context handed to Lua ---------------------------------------------------------------------------------------------------------------

struct AiContext {
    std::uint64_t run = 0;
    void* self = nullptr;
    void* other = nullptr;
    void* unit = nullptr;
    void* a = nullptr;
    void* b = nullptr;
    std::int64_t arg = 0;
    double time = 0;
    double dt = 0;
    int count = 0;
    bool resumed = false;
    std::string name;
    std::string reason;
    std::string kind;
    std::int64_t (*original)(void*, void*, void*, std::int64_t) = nullptr;
};

AiRun* findRun(void* self, std::uint64_t id) {
    if (self == nullptr || id == 0) {
        return nullptr;
    }
    const auto it = g_runs.find(self);
    return it != g_runs.end() && it->second.id == id ? &it->second : nullptr;
}

// Ends the Lua state of `self` from our side: the engine is told to go back to the routine (or to `next`), which replaces the state and so runs the exit script.
void leaveState(void* self, const std::string& next) {
    const Symbols& s = sym();
    if (!next.empty() && entryExists(Kind::State, next)) {
        setEngineState(self, next);
    } else if (s.rt_continue_routine != nullptr) {
        guarded([&] {
            EntityHolder h{self};
            s.rt_continue_routine(&h);
        });
    }
    // The routine of some NPCs does not change the state (no Routine script): make sure the Lua state is gone. (The run is erased by the exit script when the state
    // was replaced; one that is still there means the engine left the NPC in the state.)
    const auto it = g_runs.find(self);
    if (it != g_runs.end()) {
        it->second.leaving = true;
        if (engineStateOf(self) == g_states[it->second.def].name) {
            setEngineState(self, kFallbackState);
        }
    }
}

void requestEnd(AiRun& run, const std::string& reason, const std::string& next) {
    if (!run.endRequested) {
        run.endRequested = true;
        run.endReason = reason;
        run.next = next;
    }
}

int g_failures = 0;           // Lua callbacks of the bridge that raised an error (risen.ai.failures)
std::string g_lastFailure;

void noteFailure(const std::string& text) {
    ++g_failures;
    g_lastFailure = text;
}

// Calls a Lua function of the bridge; a Lua error is a warning (once per source), never an exception.
bool callLua(sol::main_protected_function& fn, const char* what, const std::string& owner, const AiContext& ctx, ai::LoopResult* loopResult, sol::object* rawResult = nullptr) {
    try {
        sol::protected_function_result r = fn(ctx);
        if (!r.valid()) {
            const sol::error err = r;
            spdlog::warn("lua: ai {} '{}' failed: {}", what, owner, err.what());
            noteFailure(std::string(what) + " '" + owner + "' failed: " + err.what());
            SPASS_TRACE(trace::Channel::Lua, "ai {} '{}' failed: {}", what, owner, std::string(err.what()));
            return false;
        }
        sol::object o = r.get<sol::object>();
        if (loopResult != nullptr) {
            std::string text;
            const bool isString = o.valid() && o.get_type() == sol::type::string;
            if (isString) {
                text = o.as<std::string>();
            }
            *loopResult = ai::interpretLoopResult(!o.valid() || o.get_type() == sol::type::lua_nil, o.valid() && o.get_type() == sol::type::boolean,
                                                  o.valid() && o.get_type() == sol::type::boolean && o.as<bool>(), isString, text);
        }
        if (rawResult != nullptr) {
            *rawResult = o;
        }
        return true;
    } catch (const std::exception& e) {
        spdlog::warn("lua: ai {} '{}' threw: {}", what, owner, e.what());
        noteFailure(std::string(what) + " '" + owner + "' threw: " + e.what());
        return false;
    }
}

AiContext makeContext(const AiRun& run, const char* kind, void* unit) {
    AiContext c;
    c.run = run.id;
    c.self = run.self;
    c.unit = unit;
    c.other = otherOf(unit);
    const double now = stateTimeOf(run.unit != nullptr ? run.unit : unit);  // the NPC's own unit (the unit of a perception script is the script admin's)
    c.time = now - run.startedAt;
    c.dt = now - run.lastLoop;
    c.count = run.loops;
    c.resumed = run.resumed;
    c.name = g_states[run.def].name;
    c.kind = kind;
    return c;
}

// ---- the state thunk ----------------------------------------------------------------------------------------------------------------------

void applyTaskCallback(void* unit, const std::string& task) {
    if (task.empty() || sym().unit_set_task_callback == nullptr) {
        return;
    }
    guarded([&] {
        ByValueString n(task);
        sym().unit_set_task_callback(unit, n.ptr());
    });
}

bool runStateImpl(std::size_t idx, void* /*stack*/, void* unit) {
    auto& rt = lua::Runtime::instance();
    auto lock = rt.lock();
    StateDef& def = g_states[idx];
    if (!rt.running() || !def.used) {
        return true;
    }
    void* self = selfOf(unit);
    if (self == nullptr) {
        return true;
    }
    if (def.retired) {
        // the mod that defined the state was torn down (reload / unload) while the NPC was in it, or a savegame put him in it: back to the routine
        SPASS_TRACE(trace::Channel::Ai, "retired state '{}' on {}: back to the routine", def.name, entityNameOf(self));
        leaveState(self, "");
        return true;
    }
    const double t = stateTimeOf(unit);
    auto it = g_runs.find(self);
    if (it != g_runs.end() && (it->second.def != idx || t + 0.0005 < it->second.lastTime)) {
        // a record whose state was replaced without an exit script (the entity went away and its address was reused)
        SPASS_TRACE(trace::Channel::Ai, "dropped a stale run of '{}' (entity {})", g_states[it->second.def].name, self);
        g_runs.erase(it);
        it = g_runs.end();
    }

    std::uint64_t runId = it != g_runs.end() ? it->second.id : 0;
    if (it == g_runs.end()) {
        AiRun run;
        run.id = g_nextRunId++;
        run.def = idx;
        run.self = self;
        run.unit = unit;
        run.data = rt.lua().create_table();
        const auto pend = g_pending.find(self);
        if (pend != g_pending.end() && pend->second.state == def.name) {
            run.args = pend->second.args;
            run.resumed = pend->second.resumed;
        } else {
            run.resumed = true;
        }
        if (pend != g_pending.end()) {
            g_pending.erase(pend);
        }
        run.startedAt = t;
        run.lastLoop = t;
        run.lastTime = t;
        const std::uint64_t id = run.id;
        runId = id;
        const bool resumed = run.resumed;
        it = g_runs.emplace(self, std::move(run)).first;
        if (sym().unit_set_exit_script != nullptr) {
            guarded([&] {
                ByValueString n(kExitScript);
                sym().unit_set_exit_script(unit, n.ptr());
            });
        }
        applyTaskCallback(unit, def.task);
        SPASS_TRACE(trace::Channel::Ai, "begin '{}' on {} (run {}{})", def.name, entityNameOf(self), id, resumed ? ", resumed" : "");
        if (def.begin.valid()) {
            const AiContext ctx = makeContext(it->second, "begin", unit);
            ai::LoopResult res;
            const bool ok = callLua(def.begin, "begin of state", def.name, ctx, &res);
            AiRun* now = findRun(self, id);
            if (now == nullptr) {
                return true;  // the callback itself changed the state
            }
            if (!ok) {
                requestEnd(*now, "error", "");
            } else if (res.end) {
                requestEnd(*now, "done", res.next);
            }
        }
    }

    AiRun* cur = findRun(self, runId);               // (begin may have changed the table: no iterator or reference from before the call is used)
    if (cur == nullptr) {
        return true;
    }
    AiRun& run0 = *cur;
    const std::uint64_t id = run0.id;
    run0.lastTime = t;

    if (run0.endRequested) {
        // the end was asked for (ctx:finish, risen.ai.stop, a callback result or an error): leave now
        const std::string next = run0.next;
        if (run0.leaving) {
            // already tried once and the state is still the Lua state: force the fallback
            run0.leaving = false;
            if (!setEngineState(self, next.empty() ? kFallbackState : next)) {
                setEngineState(self, kFallbackState);
            }
            return true;
        }
        run0.leaving = true;
        leaveState(self, next);
        return true;
    }

    if (def.loop.valid() && ai::dueForLoop(run0.loops, t, run0.lastLoop, def.interval)) {
        AiContext ctx = makeContext(run0, "loop", unit);
        ctx.time = t - run0.startedAt;
        run0.lastLoop = t;
        ++run0.loops;
        ai::LoopResult res;
        const bool ok = callLua(def.loop, "loop of state", def.name, ctx, &res);
        AiRun* now = findRun(self, id);
        if (now == nullptr) {
            return true;
        }
        if (!ok) {
            requestEnd(*now, "error", "");
        } else if (res.end) {
            requestEnd(*now, "done", res.next);
        }
        if (now->endRequested) {
            now->leaving = true;
            const std::string next = now->next;
            leaveState(self, next);
        }
    }
    return true;
}

bool runState(std::size_t idx, void* stack, void* unit) noexcept {
    try {
        return runStateImpl(idx, stack, unit);
    } catch (const std::exception& e) {
        spdlog::error("lua: ai state '{}' threw: {}", g_states[idx].name, e.what());
    } catch (...) {
        spdlog::error("lua: ai state '{}' threw an unknown exception", g_states[idx].name);
    }
    return true;
}

template <std::size_t I>
bool stateThunk(void* stack, void* unit) {
    return runState(I, stack, unit);
}

template <std::size_t... I>
constexpr std::array<bool (*)(void*, void*), sizeof...(I)> makeStateThunks(std::index_sequence<I...>) {
    return {&stateThunk<I>...};
}
constexpr auto kStateThunks = makeStateThunks(std::make_index_sequence<kMaxStates>{});

// ---- callback thunks (AICallback namespace: bool cb(unit*)) --------------------------------------------------------------------------------

bool runCallback(std::size_t idx, void* unit) noexcept {
    try {
        auto& rt = lua::Runtime::instance();
        auto lock = rt.lock();
        CallbackDef& cb = g_callbacks[idx];
        if (!rt.running() || !cb.used || cb.failed || !cb.fn.valid()) {
            return true;
        }
        AiContext c;
        c.unit = unit;
        c.self = selfOf(unit);
        c.other = otherOf(unit);
        c.time = stateTimeOf(unit);
        c.name = cb.name;
        c.kind = "callback";
        if (c.self != nullptr) {
            const auto it = g_runs.find(c.self);
            if (it != g_runs.end()) {
                c.run = it->second.id;
                c.count = it->second.loops;
                c.resumed = it->second.resumed;
            }
        }
        sol::object result;
        if (!callLua(cb.fn, "callback", cb.name, c, nullptr, &result)) {
            cb.failed = true;  // a callback that raised an error is switched off (one warning, no flood)
            return true;
        }
        return !(result.valid() && result.get_type() == sol::type::boolean && !result.as<bool>());
    } catch (...) {
        return true;
    }
}

template <std::size_t I>
bool callbackThunk(void* unit) {
    return runCallback(I, unit);
}

template <std::size_t... I>
constexpr std::array<bool (*)(void*), sizeof...(I)> makeCallbackThunks(std::index_sequence<I...>) {
    return {&callbackThunk<I>...};
}
constexpr auto kCallbackThunks = makeCallbackThunks(std::make_index_sequence<kMaxCallbacks>{});

// ---- the exit script (runs when anything replaces a Lua state) ----------------------------------------------------------------------------

std::int64_t exitScript(void* unit, void* a, void* b, std::int64_t /*arg*/, std::int64_t (*)(void*, void*, void*, std::int64_t)) {
    (void)a;
    (void)b;
    auto& rt = lua::Runtime::instance();
    auto lock = rt.lock();
    void* self = selfOf(unit);
    if (self == nullptr) {
        return 0;
    }
    const auto it = g_runs.find(self);
    if (it == g_runs.end()) {
        return 0;
    }
    AiRun run = std::move(it->second);
    g_runs.erase(it);
    StateDef& def = g_states[run.def];
    const std::string reason = ai::finishReason(run.endReason);
    SPASS_TRACE(trace::Channel::Ai, "finish '{}' on {} ({}, {} loops)", def.name, entityNameOf(self), reason, run.loops);
    if (rt.running() && def.finish.valid()) {
        AiContext c;
        c.run = run.id;
        c.self = self;
        c.unit = unit;
        c.time = run.lastTime - run.startedAt;
        c.count = run.loops;
        c.resumed = run.resumed;
        c.name = def.name;
        c.reason = reason;
        c.kind = "finish";
        // keep the record readable for the callback (ctx.args / ctx.data) without letting it act on a run that no longer exists
        AiRun keep = run;
        g_runs.emplace(self, std::move(keep));
        ++g_finishDepth;
        callLua(def.finish, "finish of state", def.name, c, nullptr);
        --g_finishDepth;
        g_runs.erase(self);
    }
    if (bus::active() && bus::wanted("ai.finished")) {
        bus::post("ai.finished", {bus::capture(self), def.name, reason, static_cast<double>(run.loops), static_cast<double>(run.id)});
    }
    return 0;
}

// ---- perception interception --------------------------------------------------------------------------------------------------------------

void* entityOfScriptArg(void* holder, void* fallback) {
    void* e = nullptr;
    guarded([&] { e = holder != nullptr ? *static_cast<void**>(holder) : fallback; });
    return e;
}

// A perception handler registered with risen.ai.perceive: not tied to a Lua state, called for the NPCs of its filter.
struct GlobalHandler {
    std::uint64_t id = 0;
    std::string script;
    sol::main_protected_function fn;
    std::vector<LuaEntity> npcs;
    bool all = false;
    bool failed = false;
    std::string owner;
};
std::vector<GlobalHandler> g_global;
std::uint64_t g_nextGlobalId = 1;

bool globalMatches(const GlobalHandler& h, void* self) {
    if (h.all) {
        return true;
    }
    for (const LuaEntity& e : h.npcs) {
        if (ptr(e) == self) {
            return true;
        }
    }
    return false;
}

std::int64_t resultOf(const sol::object& result) {
    if (result.valid() && result.get_type() == sol::type::boolean) {
        return result.as<bool>() ? 1 : 0;
    }
    if (result.valid() && result.get_type() == sol::type::number) {
        return static_cast<std::int64_t>(result.as<double>());
    }
    return 0;
}

// The interceptor of an Assess* / Perceive* script: the handler of the NPC's running Lua state (`on`), then the global handlers (risen.ai.perceive). A handler returning nil
// lets the stock script run (and the next handler look), any other value is the script's result and ends the chain.
std::int64_t perceive(const std::string& script, void* unit, void* a, void* b, std::int64_t arg, std::int64_t (*original)(void*, void*, void*, std::int64_t)) {
    const auto runOriginal = [&]() -> std::int64_t { return original != nullptr ? original(unit, a, b, arg) : 0; };
    if ((g_runs.empty() && g_global.empty()) || g_finishDepth > 0) {
        return runOriginal();
    }
    void* self = entityOfScriptArg(a, selfOf(unit));
    if (self == nullptr) {
        return runOriginal();
    }
    struct Candidate {
        sol::main_protected_function fn;
        std::string owner;
        std::uint64_t run = 0;       // the Lua state's run (scoped handler)
        std::uint64_t global = 0;    // the id of the global handler
    };
    std::vector<Candidate> candidates;
    const auto it = g_runs.find(self);
    if (it != g_runs.end()) {
        const StateDef& def = g_states[it->second.def];
        const auto h = def.on.find(script);
        if (h != def.on.end() && h->second.valid()) {
            candidates.push_back({h->second, def.name, it->second.id, 0});
        }
    }
    for (const GlobalHandler& gh : g_global) {
        if (gh.script == script && !gh.failed && globalMatches(gh, self)) {
            candidates.push_back({gh.fn, "risen.ai.perceive(" + script + ")", 0, gh.id});
        }
    }
    if (candidates.empty()) {
        return runOriginal();
    }
    auto& rt = lua::Runtime::instance();
    auto lock = rt.lock();
    if (!rt.running()) {
        return runOriginal();
    }
    void* other = entityOfScriptArg(b, otherOf(unit));
    for (Candidate& cand : candidates) {
        AiContext c;
        c.run = cand.run;
        c.self = self;
        c.other = other;
        c.unit = unit;
        c.a = a;
        c.b = b;
        c.arg = arg;
        c.name = script;
        c.kind = "perceive";
        c.original = original;
        if (cand.run != 0) {
            if (const AiRun* r = findRun(self, cand.run)) {
                c.count = r->loops;
                c.resumed = r->resumed;
                c.time = stateTimeOf(r->unit) - r->startedAt;
            }
        }
        SPASS_TRACE(trace::Channel::Ai, "perceive {} -> handler of {} on {} (other {})", script, cand.owner, entityNameOf(self), entityNameOf(other));
        sol::object result;
        ++g_perceiveDepth;
        const bool called = callLua(cand.fn, ("perception handler " + script + " of").c_str(), cand.owner, c, nullptr, &result);
        --g_perceiveDepth;
        if (!called) {
            // a broken handler is switched off, the stock reaction goes on
            if (cand.global != 0) {
                for (GlobalHandler& gh : g_global) {
                    if (gh.id == cand.global) {
                        gh.failed = true;
                    }
                }
            } else if (const auto r = g_runs.find(self); r != g_runs.end() && r->second.id == cand.run) {
                g_states[r->second.def].on.erase(script);
            }
            continue;
        }
        if (result.valid() && result.get_type() != sol::type::lua_nil) {
            return resultOf(result);
        }
        // nil: look at the next handler, then the stock script
        if (!g_runs.empty() && it != g_runs.end() && g_runs.count(self) == 0) {
            // a handler replaced the NPC's state: the remaining scoped handler is gone with it (the global ones still run)
        }
    }
    return runOriginal();
}

std::string ensurePerceptionHook(const std::string& script) {
    if (g_hookedScripts.count(script) != 0) {
        return {};
    }
    if (!entryExists(Kind::Script, script)) {
        return "unknown engine script '" + script + "'";
    }
    const std::string err = scripts::installNative(
        script,
        [script](void* unit, void* a, void* b, std::int64_t arg, std::int64_t (*original)(void*, void*, void*, std::int64_t)) -> std::int64_t {
            return perceive(script, unit, a, b, arg, original);
        },
        /*isOverride=*/true);
    if (err.empty()) {
        g_hookedScripts.insert(script);
    }
    return err;
}

std::size_t findState(const std::string& name) {
    for (std::size_t i = 0; i < kMaxStates; ++i) {
        if (g_states[i].used && g_states[i].name == name) {
            return i;
        }
    }
    return kMaxStates;
}

std::size_t findCallback(const std::string& name) {
    for (std::size_t i = 0; i < kMaxCallbacks; ++i) {
        if (g_callbacks[i].used && g_callbacks[i].name == name) {
            return i;
        }
    }
    return kMaxCallbacks;
}

// Registers `fn` as the AI callback `name`. Returns an error message, or empty.
std::string installCallback(const std::string& name, sol::main_protected_function fn) {
    std::size_t idx = findCallback(name);
    if (idx == kMaxCallbacks) {
        for (std::size_t i = 0; i < kMaxCallbacks; ++i) {
            if (!g_callbacks[i].used) {
                idx = i;
                break;
            }
        }
        if (idx == kMaxCallbacks) {
            return "too many Lua AI callbacks (limit " + std::to_string(kMaxCallbacks) + ")";
        }
    }
    void* entry = registerEntry(Kind::Callback, name);
    if (entry == nullptr) {
        return "could not access the script registry (game not ready?)";
    }
    void** slot = reinterpret_cast<void**>(static_cast<char*>(entry) + kEntryFn);
    adoptModuleRecord(entry);
    const bool ours = g_callbacks[idx].used && g_callbacks[idx].fnSlot == slot;
    if (*slot != nullptr && !ours) {
        const auto [mod, rva] = moduleOf(*slot);
        (void)rva;
        return "the AI callback '" + name + "' already exists in the game (" + mod + ")";
    }
    CallbackDef& d = g_callbacks[idx];
    d.used = true;
    d.name = name;
    d.fnSlot = slot;
    d.fn = std::move(fn);
    d.failed = false;
    d.owner = lua::Runtime::currentOwner();
    *slot = reinterpret_cast<void*>(kCallbackThunks[idx]);
    SPASS_TRACE(trace::Channel::Ai, "callback '{}' defined (slot {})", name, idx);
    return {};
}

bool isFunction(const sol::object& o) { return o.valid() && o.get_type() == sol::type::function; }
bool isNil(const sol::object& o) { return !o.valid() || o.get_type() == sol::type::lua_nil; }

std::string validKeys(const char* const* list, std::size_t n) {
    std::string out;
    for (std::size_t i = 0; i < n; ++i) {
        out += (i ? ", " : "") + std::string(list[i]);
    }
    return out;
}

// risen.ai.state(name, { begin, loop, finish, on, task, interval }) -> true | nil, reason
Two defineState(sol::this_state ts, sol::object nameArg, sol::object defArg) {
    sol::state_view L(ts);
    if (!g_ready) {
        return failResult(L, "the AI bridge is unavailable (script registry or AI functions missing, see risen.engine.missing())");
    }
    std::string name;
    if (!stringArg(nameArg, name)) {
        return failResult(L, "the first argument must be the state name (a string)");
    }
    if (const std::string why = ai::validateName(name); !why.empty()) {
        return failResult(L, "invalid state name '" + name + "': " + why);
    }
    if (!defArg.valid() || !defArg.is<sol::table>()) {
        return failResult(L, "the second argument must be a table { begin, loop, finish, on, task, interval }");
    }
    sol::table def = defArg.as<sol::table>();
    static const char* const kKeys[] = {"begin", "loop", "finish", "on", "task", "interval"};
    for (const auto& kv : def) {
        std::string key;
        bool known = false;
        if (stringArg(kv.first, key)) {
            for (const char* k : kKeys) {
                known = known || key == k;
            }
        }
        if (!known) {
            return failResult(L, "unknown key '" + (kv.first.get_type() == sol::type::string ? key : std::string("?")) + "' (valid: " + validKeys(kKeys, 6) + ")");
        }
    }
    StateDef nd;
    nd.name = name;
    const char* const fnKeys[] = {"begin", "loop", "finish"};
    sol::main_protected_function* targets[] = {&nd.begin, &nd.loop, &nd.finish};
    for (int i = 0; i < 3; ++i) {
        sol::object o = def[fnKeys[i]];
        if (isNil(o)) {
            continue;
        }
        if (!isFunction(o)) {
            return failResult(L, std::string("'") + fnKeys[i] + "' must be a function");
        }
        *targets[i] = def.get<sol::main_protected_function>(fnKeys[i]);
    }
    sol::object onArg = def["on"];
    if (!isNil(onArg)) {
        if (!onArg.is<sol::table>()) {
            return failResult(L, "'on' must be a table { notice = fn, damage = fn, ... }");
        }
        sol::table on = onArg.as<sol::table>();
        for (const auto& kv : on) {
            std::string key;
            if (!stringArg(kv.first, key)) {
                return failResult(L, "the keys of 'on' must be strings");
            }
            const std::string script = ai::perceptionScript(key);
            if (script.empty()) {
                std::string list;
                for (const ai::Perception& p : ai::kPerceptions) {
                    list += (list.empty() ? "" : ", ") + std::string(p.key);
                }
                return failResult(L, "unknown perception '" + key + "' (valid: " + list + ", or the name of an Assess*/Perceive* script)");
            }
            if (!isFunction(kv.second)) {
                return failResult(L, "the handler of perception '" + key + "' must be a function");
            }
            if (!entryExists(Kind::Script, script)) {
                return failResult(L, "the engine script '" + script + "' of perception '" + key + "' does not exist");
            }
            nd.on[script] = on.get<sol::main_protected_function>(key);
        }
    }
    if (!nd.begin.valid() && !nd.loop.valid() && !nd.finish.valid() && nd.on.empty()) {
        return failResult(L, "define at least one of begin, loop, finish or on");
    }
    sol::object taskArg = def["task"];
    std::string taskFunctionName;
    sol::main_protected_function taskFn;
    if (!isNil(taskArg)) {
        if (taskArg.get_type() == sol::type::string) {
            nd.task = taskArg.as<std::string>();
            if (!entryExists(Kind::Callback, nd.task)) {
                return failResult(L, "task: there is no AI callback '" + nd.task + "' (risen.ai.list('callback') shows the registered ones, risen.ai.callback defines one)");
            }
        } else if (isFunction(taskArg)) {
            taskFunctionName = name + "_Task";
            taskFn = def.get<sol::main_protected_function>("task");
            nd.task = taskFunctionName;
        } else {
            return failResult(L, "task must be the name of an AI callback or a function");
        }
    }
    sol::object intervalArg = def["interval"];
    if (!isNil(intervalArg) && intervalArg.get_type() != sol::type::number) {
        return failResult(L, "interval must be a number of seconds");
    }
    nd.interval = ai::normalizeInterval(!isNil(intervalArg), isNil(intervalArg) ? 0.0 : intervalArg.as<double>());

    // everything checked: now touch the engine
    std::size_t idx = findState(name);
    const bool redefine = idx != kMaxStates;
    if (!redefine) {
        for (std::size_t i = 0; i < kMaxStates; ++i) {
            if (!g_states[i].used) {
                idx = i;
                break;
            }
        }
        if (idx == kMaxStates) {
            return failResult(L, "too many Lua AI states (limit " + std::to_string(kMaxStates) + ")");
        }
    }
    void* entry = registerEntry(Kind::State, name);
    if (entry == nullptr) {
        return failResult(L, "could not access the script registry (game not ready?)");
    }
    void** slot = reinterpret_cast<void**>(static_cast<char*>(entry) + kEntryFn);
    adoptModuleRecord(entry);
    if (*slot != nullptr && !(redefine && g_states[idx].fnSlot == slot)) {
        const auto [mod, rva] = moduleOf(*slot);
        (void)rva;
        return failResult(L, "the AI state '" + name + "' already exists in the game (" + mod + "); choose another name");
    }
    for (const auto& [script, fn] : nd.on) {
        (void)fn;
        if (const std::string err = ensurePerceptionHook(script); !err.empty()) {
            return failResult(L, err);
        }
    }
    if (taskFn.valid()) {
        if (const std::string err = installCallback(taskFunctionName, taskFn); !err.empty()) {
            return failResult(L, err);
        }
    }
    nd.used = true;
    nd.fnSlot = slot;
    nd.owner = lua::Runtime::currentOwner();
    g_states[idx] = std::move(nd);
    *slot = reinterpret_cast<void*>(kStateThunks[idx]);
    spdlog::info("lua: ai state '{}' {} (slot {})", name, redefine ? "redefined" : "defined", idx);
    SPASS_TRACE(trace::Channel::Ai, "state '{}' {} (slot {}, interval {}, task '{}', {} perception handler(s))", name, redefine ? "redefined" : "defined", idx,
                g_states[idx].interval, g_states[idx].task, g_states[idx].on.size());
    return okResult(L, sol::make_object(L, true));
}

// risen.ai.callback(name, fn) -> true | nil, reason
Two defineCallback(sol::this_state ts, sol::object nameArg, sol::object fnArg) {
    sol::state_view L(ts);
    if (!g_ready) {
        return failResult(L, "the AI bridge is unavailable (script registry or AI functions missing, see risen.engine.missing())");
    }
    std::string name;
    if (!stringArg(nameArg, name)) {
        return failResult(L, "the first argument must be the callback name (a string)");
    }
    if (const std::string why = ai::validateName(name); !why.empty()) {
        return failResult(L, "invalid callback name '" + name + "': " + why);
    }
    if (!isFunction(fnArg)) {
        return failResult(L, "the second argument must be a function");
    }
    sol::main_protected_function fn = fnArg.as<sol::main_protected_function>();
    if (const std::string err = installCallback(name, fn); !err.empty()) {
        return failResult(L, err);
    }
    return okResult(L, sol::make_object(L, true));
}

// Whether `e` can have an AI state set; the reason otherwise.
std::string checkNpc(void* e) {
    const Symbols& s = sym();
    bool npc = false, player = false, dead = false;
    if (!guarded([&] {
            EntityHolder h{e};
            npc = s.ent_is_npc(&h) || s.ent_is_character(&h);
            player = s.ent_is_player(&h);
            dead = s.ent_is_dead(&h);
        })) {
        return "the engine raised an exception while inspecting the entity";
    }
    if (player) {
        return "the player has no AI state of his own";
    }
    if (!npc) {
        return "the entity is not an NPC or monster";
    }
    if (dead) {
        return "the NPC is dead";
    }
    if (s.ent_get_property_set != nullptr) {
        void* ps = nullptr;
        guarded([&] {
            const BString cls("gCScriptRoutine_PS");
            ps = s.ent_get_property_set(e, cls.get());
        });
        if (ps == nullptr) {
            return "the NPC has no AI (no gCScriptRoutine_PS)";
        }
    }
    return {};
}

bool startNow(void* e, const std::string& name, const sol::object& args, bool interrupt, bool resumed, std::string& error) {
    if (interrupt && sym().rt_full_stop != nullptr) {
        // the NPC finishes the action he is in (an animation, a walk) before the engine looks at the new state: stop it so the state starts at once
        guarded([&] {
            EntityHolder h{e};
            sym().rt_full_stop(&h);
        });
    }
    if (findState(name) != kMaxStates) {
        g_pending[e] = Pending{name, args, resumed};
    } else {
        g_pending.erase(e);
    }
    if (!setEngineState(e, name)) {
        g_pending.erase(e);
        error = "the engine refused to set the state (stale entity?)";
        return false;
    }
    SPASS_TRACE(trace::Channel::Ai, "start '{}' on {}", name, entityNameOf(e));
    return true;
}

// risen.ai.start(npc, state [, args [, { interrupt = true, resumed = false }]]) -> true | nil, reason
Two startAi(sol::this_state ts, sol::object npcArg, sol::object nameArg, sol::object argsArg, sol::object optsArg) {
    sol::state_view L(ts);
    LuaEntity npc;
    if (!entityArg(npcArg, npc)) {
        return failResult(L, "the first argument must be an NPC entity");
    }
    std::string name;
    if (!stringArg(nameArg, name)) {
        return failResult(L, "the second argument must be the AI state name (a string)");
    }
    void* e = ptr(npc);
    if (e == nullptr) {
        return failResult(L, "entity is gone");
    }
    if (const std::string why = checkNpc(e); !why.empty()) {
        return failResult(L, why);
    }
    if (findState(name) == kMaxStates && !entryExists(Kind::State, name)) {
        return failResult(L, "unknown AI state '" + name + "' (risen.ai.list('state') shows the registered ones)");
    }
    if (const std::size_t si = findState(name); si != kMaxStates && g_states[si].retired) {
        return failResult(L, "the AI state '" + name + "' was retired (its mod was unloaded or reloaded and has not defined it again)");
    }
    bool interrupt = true, resumed = false;
    if (!isNil(optsArg)) {
        if (!optsArg.is<sol::table>()) {
            return failResult(L, "the fourth argument must be a table { interrupt = true|false, resumed = true|false } or nil");
        }
        sol::table opts = optsArg.as<sol::table>();
        for (const auto& kv : opts) {
            std::string key;
            if (!stringArg(kv.first, key) || (key != "interrupt" && key != "resumed")) {
                return failResult(L, "unknown option (valid: interrupt, resumed)");
            }
            if (kv.second.get_type() != sol::type::boolean) {
                return failResult(L, key + " must be true or false");
            }
            (key == "interrupt" ? interrupt : resumed) = kv.second.as<bool>();
        }
    }
    sol::object args = toMain(argsArg);
    if (!unitBound(e)) {
        g_waiting.push_back(WaitingStart{npc, name, args, interrupt, resumed, 0});
        SPASS_TRACE(trace::Channel::Ai, "start '{}' on {} waits for the NPC's first frame", name, npc.name);
        return okResult(L, sol::make_object(L, true));
    }
    if (g_finishDepth > 0 || g_perceiveDepth > 0) {
        // inside a `finish` callback the engine is replacing the state, and the code that called a perception handler goes on to set a state of its own (a hit NPC:
        // ResumeTaskAfterStumble -> AssessDamage -> ResumeTask): do it at the end of the frame
        g_deferred.push_back([npc, name, args, interrupt, resumed]() {
            void* ent = ptr(npc);
            std::string error;
            if (ent != nullptr && checkNpc(ent).empty() && !startNow(ent, name, args, interrupt, resumed, error)) {
                spdlog::warn("lua: ai.start('{}') deferred: {}", name, error);
            }
        });
        return okResult(L, sol::make_object(L, true));
    }
    std::string error;
    if (!startNow(e, name, args, interrupt, resumed, error)) {
        return failResult(L, error);
    }
    return okResult(L, sol::make_object(L, true));
}

// risen.ai.stop(npc [, next_state]) -> true | nil, reason
Two stopAi(sol::this_state ts, sol::object npcArg, sol::object nextArg) {
    sol::state_view L(ts);
    LuaEntity npc;
    if (!entityArg(npcArg, npc)) {
        return failResult(L, "the first argument must be an NPC entity");
    }
    std::string next;
    if (!isNil(nextArg) && !stringArg(nextArg, next)) {
        return failResult(L, "the second argument must be an AI state name (a string) or nil");
    }
    void* e = ptr(npc);
    if (e == nullptr) {
        return failResult(L, "entity is gone");
    }
    const auto it = g_runs.find(e);
    if (it == g_runs.end()) {
        return failResult(L, "the NPC is not running a Lua AI state");
    }
    if (!next.empty() && findState(next) == kMaxStates && !entryExists(Kind::State, next)) {
        return failResult(L, "unknown AI state '" + next + "'");
    }
    if (g_finishDepth > 0 || g_perceiveDepth > 0) {
        g_deferred.push_back([npc, next]() {
            void* ent = ptr(npc);
            if (ent == nullptr) {
                return;
            }
            const auto r = g_runs.find(ent);
            if (r != g_runs.end()) {
                requestEnd(r->second, "stopped", next);
                r->second.leaving = true;
                leaveState(ent, next);
            }
        });
        return okResult(L, sol::make_object(L, true));
    }
    requestEnd(it->second, "stopped", next);
    it->second.leaving = true;
    leaveState(e, next);
    return okResult(L, sol::make_object(L, true));
}

// risen.ai.current(npc) -> { state, time, loops, resumed, args, data } | nil
sol::object currentAi(sol::this_state ts, sol::object npcArg) {
    sol::state_view L(ts);
    LuaEntity npc;
    if (!entityArg(npcArg, npc)) {
        return sol::make_object(L, sol::lua_nil);
    }
    void* e = ptr(npc);
    const auto it = e != nullptr ? g_runs.find(e) : g_runs.end();
    if (it == g_runs.end()) {
        return sol::make_object(L, sol::lua_nil);
    }
    const AiRun& r = it->second;
    sol::table t = L.create_table();
    t["state"] = g_states[r.def].name;
    t["time"] = stateTimeOf(r.unit) - r.startedAt;
    t["loops"] = r.loops;
    t["resumed"] = r.resumed;
    t["ending"] = r.endRequested;
    if (r.args.valid()) {
        t["args"] = r.args;
    }
    t["data"] = r.data;
    return sol::make_object(L, t);
}

// risen.ai.list(kind) -> sorted names | nil, reason
Two listRegistry(sol::this_state ts, sol::object kindArg) {
    sol::state_view L(ts);
    std::string k;
    Kind kind = Kind::State;
    if (!stringArg(kindArg, k) || !parseKind(k, kind)) {
        return failResult(L, "the kind must be 'state', 'function', 'callback' or 'script'");
    }
    std::vector<Entry> entries;
    if (!enumerate(kind, entries)) {
        return failResult(L, "the script registry is unavailable");
    }
    std::vector<std::string> names;
    for (const Entry& e : entries) {
        if (e.fn != nullptr) {
            names.push_back(e.name);
        }
    }
    std::sort(names.begin(), names.end());
    sol::table out = L.create_table();
    int n = 0;
    for (const std::string& s : names) {
        out[++n] = s;
    }
    return okResult(L, out);
}

// risen.ai.info(kind, name) -> { kind, name, module, rva, lua } | nil
sol::object infoRegistry(sol::this_state ts, sol::object kindArg, sol::object nameArg) {
    sol::state_view L(ts);
    std::string k, name;
    Kind kind = Kind::State;
    if (!stringArg(kindArg, k) || !parseKind(k, kind) || !stringArg(nameArg, name)) {
        return sol::make_object(L, sol::lua_nil);
    }
    void* fn = entryFn(lookupEntry(kind, name));
    if (fn == nullptr) {
        return sol::make_object(L, sol::lua_nil);
    }
    const auto [mod, rva] = moduleOf(fn);
    sol::table t = L.create_table();
    t["kind"] = k;
    t["name"] = name;
    t["module"] = mod;
    t["rva"] = static_cast<double>(rva);
    t["lua"] = findState(name) != kMaxStates || findCallback(name) != kMaxCallbacks || mod.find("sentry") != std::string::npos;
    return sol::make_object(L, t);
}

// risen.ai.perceive(kind, fn, { npc = Entity | { Entity, ... }, all = true }) -> handle | nil, reason
Two perceiveAi(sol::this_state ts, sol::object kindArg, sol::object fnArg, sol::object filterArg) {
    sol::state_view L(ts);
    if (!g_ready) {
        return failResult(L, "the AI bridge is unavailable (script registry or AI functions missing, see risen.engine.missing())");
    }
    std::string key;
    if (!stringArg(kindArg, key)) {
        return failResult(L, "the first argument must be a perception (a string, see risen.ai.perceptions)");
    }
    const std::string script = ai::perceptionScript(key);
    if (script.empty()) {
        std::string list;
        for (const ai::Perception& p : ai::kPerceptions) {
            list += (list.empty() ? "" : ", ") + std::string(p.key);
        }
        return failResult(L, "unknown perception '" + key + "' (valid: " + list + ", or the name of an Assess*/Perceive* script)");
    }
    if (!isFunction(fnArg)) {
        return failResult(L, "the second argument must be a function");
    }
    if (!filterArg.valid() || !filterArg.is<sol::table>()) {
        return failResult(L, "the third argument must say whose perception it is: { npc = entity | { entities } } or { all = true }");
    }
    GlobalHandler gh;
    sol::table filter = filterArg.as<sol::table>();
    for (const auto& kv : filter) {
        std::string k;
        if (!stringArg(kv.first, k) || (k != "npc" && k != "all")) {
            return failResult(L, "unknown filter key (valid: npc, all)");
        }
        if (k == "all") {
            if (kv.second.get_type() != sol::type::boolean) {
                return failResult(L, "'all' must be true or false");
            }
            gh.all = kv.second.as<bool>();
        } else if (kv.second.is<LuaEntity>()) {
            gh.npcs.push_back(kv.second.as<LuaEntity>());
        } else if (kv.second.is<sol::table>()) {
            sol::table list = kv.second.as<sol::table>();
            for (const auto& item : list) {
                if (!item.second.is<LuaEntity>()) {
                    return failResult(L, "'npc' must be an entity or a list of entities");
                }
                gh.npcs.push_back(item.second.as<LuaEntity>());
            }
        } else {
            return failResult(L, "'npc' must be an entity or a list of entities");
        }
    }
    if (!gh.all && gh.npcs.empty()) {
        return failResult(L, "the filter selects no NPC: give { npc = ... } or { all = true }");
    }
    if (const std::string err = ensurePerceptionHook(script); !err.empty()) {
        return failResult(L, err);
    }
    gh.id = g_nextGlobalId++;
    gh.script = script;
    gh.owner = lua::Runtime::currentOwner();
    gh.fn = fnArg.as<sol::main_protected_function>();
    const std::uint64_t id = gh.id;
    g_global.push_back(std::move(gh));
    SPASS_TRACE(trace::Channel::Ai, "perception handler {} for {} ({})", id, script, g_global.back().all ? "all NPCs" : "selected NPCs");
    return okResult(L, sol::make_object(L, static_cast<double>(id)));
}

// risen.ai.unperceive(handle) -> bool
bool unperceiveAi(sol::object handle) {
    if (!handle.valid() || handle.get_type() != sol::type::number) {
        return false;
    }
    const auto id = static_cast<std::uint64_t>(handle.as<double>());
    for (auto it = g_global.begin(); it != g_global.end(); ++it) {
        if (it->id == id) {
            g_global.erase(it);
            return true;
        }
    }
    return false;
}

// ---- the engine's perception broadcast (M5e, docs/RE.md section 40) ---------------------------------------------------------------------------
// Script_Game.dll has one helper, not exported (RVA 0xed3e0), that every "an NPC may react to this" event of the game ends in: Interact_Player_TakeItem
// and PreInteract_Player_Chest / _Cupboard call it with (the item or container, the proxy of AssessTheft), Kill / Defeat / ResumeTaskAfterStumble with
// other scripts. It runs the given script for every NPC of the world (self = the NPC, other = the entity), collects the reactions the script recorded (a
// crime, a comment, a new AI mode), orders them and applies them (LastPlayerCrime, LastPlayerComment, the AI mode: the NPC comments, threatens, attacks...).
// The distance (PerceiveTheft: 700 units) and the line of sight (CanSee) are tested by the scripts themselves, so a caller only has to name the entity.
// `bool f(?, Entity byValue, eCScriptProxyScript byValue)`: the first register is not an argument (the callee only uses its home slot as a scratch Entity, the
// callers pass whatever is in it), the other two are pointers to copies that the callee destroys.
using BroadcastFn = bool (*)(void* unused, void* byValueEntity, void* byValueProxy);
constexpr std::uintptr_t kBroadcastRva = 0xed3e0;
constexpr unsigned char kBroadcastPrologue[] = {0x4c, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10, 0x48, 0x89, 0x4c, 0x24, 0x08, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41,
                                                0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8d, 0xac, 0x24, 0xc8, 0xfe, 0xff, 0xff, 0x48, 0x81, 0xec, 0x38, 0x02, 0x00, 0x00};
BroadcastFn g_broadcast = nullptr;

// Looks the helper up by its offset and checks the first bytes of the function, so another build of Script_Game.dll is refused instead of called.
void resolveBroadcast(HMODULE scriptGame) {
    g_broadcast = nullptr;
    if (scriptGame == nullptr) {
        return;
    }
    const auto* code = reinterpret_cast<const unsigned char*>(scriptGame) + kBroadcastRva;
    bool same = false;
    guarded([&] { same = std::memcmp(code, kBroadcastPrologue, sizeof(kBroadcastPrologue)) == 0; });
    if (same) {
        g_broadcast = reinterpret_cast<BroadcastFn>(const_cast<unsigned char*>(code));
    } else {
        spdlog::warn("ai: the perception broadcast of Script_Game.dll (RVA {:#x}) is not the known function: risen.ai.broadcast is unavailable", kBroadcastRva);
    }
}

// risen.ai.broadcast(script, entity) -> bool | nil, reason: runs the engine's perception broadcast of `script` (an Assess* script such as "AssessTheft") for
// `entity`, exactly as the game does when the player takes an item or opens a chest: every NPC of the world is asked, the reactions are applied. Returns
// whether any NPC reacted.
Two broadcastAi(sol::this_state ts, sol::object scriptArg, sol::object targetArg) {
    sol::state_view L(ts);
    if (g_broadcast == nullptr || sym().script_proxy_ctor_name == nullptr) {
        return failResult(L, "the perception broadcast is unavailable (see risen.engine.missing() and the log)");
    }
    std::string script;
    if (!stringArg(scriptArg, script) || script.empty() || script.size() > 100) {
        return failResult(L, "the first argument must be the name of a script (a string, e.g. \"AssessTheft\")");
    }
    LuaEntity target;
    if (!entityArg(targetArg, target)) {
        return failResult(L, "the second argument must be an entity");
    }
    if (!scripts::scriptExists(script)) {
        return failResult(L, "no script named '" + script + "'");
    }
    void* e = ptr(target);
    if (e == nullptr) {
        return failResult(L, "the entity is gone");
    }
    // by-value arguments: the callee destroys its copies, so nothing is destroyed here
    EntityHolder holder{e};
    alignas(16) unsigned char proxy[64] = {};
    bool reacted = false;
    const bool ok = guarded([&] {
        const BString name(script);
        sym().script_proxy_ctor_name(proxy, name.get());
        reacted = g_broadcast(nullptr, &holder, proxy);
    });
    SPASS_TRACE(trace::Channel::Ai, "broadcast {} for {}: {}", script, target.name, ok ? (reacted ? "an NPC reacted" : "nobody reacted") : "the engine raised an exception");
    if (!ok) {
        return failResult(L, "the engine raised an exception");
    }
    return okResult(L, sol::make_object(L, reacted));
}

}  // namespace

void aiEntityKilled(void* entity) {
    if (entity == nullptr) {
        return;
    }
    // an entity that is removed from the world leaves no exit script behind: forget what the bridge knows about it (no `finish`: the entity is going away)
    g_runs.erase(entity);
    g_pending.erase(entity);
}

void aiResetRuns() {
    g_runs.clear();
    g_pending.clear();
    g_deferred.clear();
    g_waiting.clear();
}

void aiReleaseOwner(const std::string& mod, std::vector<std::string>& released) {
    if (mod.empty()) {
        return;
    }
    // handlers of risen.ai.perceive
    for (auto it = g_global.begin(); it != g_global.end();) {
        if (it->owner == mod) {
            released.push_back("perception handler for " + it->script);
            it = g_global.erase(it);
        } else {
            ++it;
        }
    }
    // states: NPCs that run one go back to their routine; the registry entry stays (a savegame may still name it) but does nothing
    for (std::size_t i = 0; i < kMaxStates; ++i) {
        StateDef& d = g_states[i];
        if (!d.used || d.owner != mod) {
            continue;
        }
        std::vector<void*> victims;
        for (auto& [self, run] : g_runs) {
            if (run.def == i) {
                victims.push_back(self);
            }
        }
        d.retired = true;
        d.begin = d.loop = d.finish = sol::main_protected_function{};
        d.on.clear();
        for (void* self : victims) {
            const auto it = g_runs.find(self);
            if (it == g_runs.end()) {
                continue;
            }
            requestEnd(it->second, "stopped", "");
            it->second.leaving = true;
            leaveState(self, "");
        }
        released.push_back("AI state " + d.name + (victims.empty() ? "" : " (" + std::to_string(victims.size()) + " NPC(s) sent back to their routine)"));
    }
    for (CallbackDef& c : g_callbacks) {
        if (c.used && c.owner == mod) {
            c.fn = sol::main_protected_function{};
            released.push_back("AI callback " + c.name);
            c.owner.clear();
        }
    }
}

void aiFlushDeferred() {
    if (g_deferred.empty() && g_waiting.empty()) {
        return;
    }
    auto& rt = lua::Runtime::instance();
    auto lock = rt.lock();
    if (!g_waiting.empty() && g_finishDepth == 0) {
        std::vector<WaitingStart> still;
        for (WaitingStart& w : g_waiting) {
            void* e = ptr(w.npc);
            if (e == nullptr) {
                continue;  // the NPC is gone
            }
            ++w.frames;
            if (unitBound(e) && w.frames >= 2) {
                std::string error;
                if (checkNpc(e).empty() && !startNow(e, w.name, w.args, w.interrupt, w.resumed, error)) {
                    spdlog::warn("lua: ai.start('{}') after the NPC's first frame: {}", w.name, error);
                }
            } else if (w.frames > 600) {
                spdlog::warn("lua: ai.start('{}') on {}: the NPC never got his first frame, dropped", w.name, w.npc.name);
            } else {
                still.push_back(std::move(w));
            }
        }
        g_waiting = std::move(still);
    }
    std::vector<std::function<void()>> batch;
    batch.swap(g_deferred);
    for (auto& f : batch) {
        try {
            f();
        } catch (const std::exception& e) {
            spdlog::warn("lua: a deferred ai action failed: {}", e.what());
        }
    }
}

void installAiModApi(sol::state_view lua, sol::table api, const std::string& modId) {
    sol::load_result loaded = lua.load(kAiModChunk, "=risen.ai");
    if (!loaded.valid()) {
        spdlog::error("lua: ai mod api failed to load: {}", loaded.get<sol::error>().what());
        return;
    }
    sol::protected_function fn = loaded;
    sol::protected_function_result res = fn(api, modId);
    if (!res.valid()) {
        spdlog::error("lua: ai mod api failed to run: {}", res.get<sol::error>().what());
    }
}

void aiShutdown() {
    g_runs.clear();
    g_pending.clear();
    g_deferred.clear();
    g_waiting.clear();
    for (StateDef& d : g_states) {
        d = StateDef{};
    }
    for (CallbackDef& d : g_callbacks) {
        d = CallbackDef{};
    }
    g_global.clear();
    g_store = sol::table{};
}

void bindAi(sol::state_view lua, sol::table risen) {
    const Symbols& s = sym();
    g_store = lua.create_table();

    lua.new_usertype<AiContext>(
        "AiContext", sol::no_constructor,
        "self", sol::property([](const AiContext& c, sol::this_state ts) { return makeEntityObject(ts, c.self); }),
        "other", sol::property([](const AiContext& c, sol::this_state ts) { return makeEntityObject(ts, c.other); }),
        "args", sol::property([](const AiContext& c, sol::this_state ts) -> sol::object {
            const AiRun* r = findRun(c.self, c.run);
            if (r == nullptr || !r->args.valid()) {
                return sol::make_object(ts, sol::lua_nil);
            }
            return r->args;
        }),
        "data", sol::property([](const AiContext& c, sol::this_state ts) -> sol::object {
            const AiRun* r = findRun(c.self, c.run);
            if (r == nullptr) {
                return sol::make_object(ts, sol::lua_nil);
            }
            return sol::make_object(ts, r->data);
        }),
        "time", sol::property([](const AiContext& c) { return c.time; }),
        "dt", sol::property([](const AiContext& c) { return c.dt; }),
        "count", sol::property([](const AiContext& c) { return c.count; }),
        "resumed", sol::property([](const AiContext& c) { return c.resumed; }),
        "name", sol::property([](const AiContext& c) { return c.name; }),
        "kind", sol::property([](const AiContext& c) { return c.kind; }),
        "reason", sol::property([](const AiContext& c) { return c.reason; }),
        "arg", sol::property([](const AiContext& c) { return c.arg; }),
        // ctx:finish([next_state]): ends the Lua state after this callback; the NPC goes back to his routine (or to `next_state`)
        "finish", [](const AiContext& c, sol::object next) {
            AiRun* r = findRun(c.self, c.run);
            if (r == nullptr) {
                return false;
            }
            std::string n;
            if (next.valid() && next.get_type() == sol::type::string) {
                n = next.as<std::string>();
            }
            requestEnd(*r, "done", n);
            return true;
        },
        // ctx:call_original(): in a perception handler, runs the stock script (the game's own reaction) and returns its result
        "call_original", [](const AiContext& c) -> std::int64_t {
            std::int64_t r = 0;
            if (c.original != nullptr) {
                guarded([&] { r = c.original(c.unit, c.a, c.b, c.arg); });
            }
            return r;
        });

    sol::table ai = lua.create_table();
    ai.set_function("state", defineState);
    ai.set_function("callback", defineCallback);
    ai.set_function("start", startAi);
    ai.set_function("stop", stopAi);
    ai.set_function("current", currentAi);
    // risen.ai.failures() -> count, last: how many Lua callbacks of the bridge raised an error (each one was logged as a warning and handled), and the last message
    ai.set_function("failures", []() { return std::make_tuple(g_failures, g_lastFailure); });
    // risen.ai.generation() -> the id the next run will get: runs that began before this call have a smaller one (the `run` of the ai.finished event)
    ai.set_function("generation", []() { return static_cast<double>(g_nextRunId); });
    ai.set_function("perceive", perceiveAi);
    ai.set_function("unperceive", unperceiveAi);
    ai.set_function("broadcast", broadcastAi);
    ai.set_function("list", listRegistry);
    ai.set_function("info", infoRegistry);
    ai.set_function("exists", [](sol::object kindArg, sol::object nameArg) {
        std::string k, name;
        Kind kind = Kind::State;
        return stringArg(kindArg, k) && parseKind(k, kind) && stringArg(nameArg, name) && entryExists(kind, name);
    });
    // risen.ai.defined() -> { states = {names}, callbacks = {names} }: what Lua defined
    ai.set_function("defined", [](sol::this_state ts) {
        sol::state_view L(ts);
        sol::table out = L.create_table();
        sol::table states = L.create_table();
        sol::table callbacks = L.create_table();
        int n = 0;
        for (const StateDef& d : g_states) {
            if (d.used) {
                states[++n] = d.name;
            }
        }
        n = 0;
        for (const CallbackDef& d : g_callbacks) {
            if (d.used) {
                callbacks[++n] = d.name;
            }
        }
        out["states"] = states;
        out["callbacks"] = callbacks;
        return out;
    });
    ai["DONE"] = "done";
    sol::table perceptions = lua.create_table();
    for (const ai::Perception& p : ai::kPerceptions) {
        perceptions[p.key] = p.script;
    }
    ai["perceptions"] = perceptions;
    risen["ai"] = ai;

    // the exit script and a check that the registry layout is the one this file reads
    g_ready = false;
    const bool symbolsOk = haveRegistry() && s.script_map_register_state != nullptr && s.script_map_register_callback != nullptr && s.script_map_get_state != nullptr &&
                           s.script_map_get_callback != nullptr && s.unit_set_exit_script != nullptr && s.rt_set_state != nullptr && s.unit_get_self != nullptr &&
                           s.ent_is_npc != nullptr && s.ent_is_character != nullptr && s.ent_is_player != nullptr && s.ent_is_dead != nullptr;
    if (!symbolsOk) {
        spdlog::warn("ai: bridge disabled (engine symbols missing)");
        return;
    }
    const HMODULE sg = GetModuleHandleW(L"Script_Game.dll");
    resolveBroadcast(sg);
    const void* expected = sg != nullptr ? reinterpret_cast<const void*>(GetProcAddress(sg, "?ZS_HangAround@@YA_NAEAV?$bTObjStack@UgScriptRunTimeSingleState@@@@PEAVgCScriptProcessingUnit@@@Z")) : nullptr;
    std::vector<Entry> states;
    bool layoutOk = false;
    if (expected != nullptr && enumerate(Kind::State, states)) {
        for (const Entry& e : states) {
            if (e.name == "ZS_HangAround") {
                layoutOk = e.fn == expected && lookupEntry(Kind::State, "ZS_HangAround") == e.base;
                guarded([&] { g_moduleRecord = *reinterpret_cast<void**>(static_cast<char*>(e.base) + 0x18); });
            }
        }
    }
    if (!layoutOk || g_moduleRecord == nullptr) {
        spdlog::error("ai: registry layout self-test failed (ZS_HangAround not found with the expected function), AI bridge disabled");
        return;
    }
    const std::string err = scripts::installNative(kExitScript, exitScript, /*isOverride=*/false);
    if (!err.empty()) {
        spdlog::warn("ai: exit script: {}", err);
    }
    adoptModuleRecord(lookupEntry(Kind::Script, kExitScript));  // ProcessExitScript only calls a valid script
    g_ready = true;
    spdlog::info("ai: bridge ready ({} AI states registered by the game)", states.size());
}

}  // namespace spass::game
