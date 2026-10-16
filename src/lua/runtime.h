#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include <sol/sol.hpp>
#include <spdlog/spdlog.h>

#include "common/crash.h"
#include "lua/mods.h"

namespace spass::lua {

enum class ModState { Loaded, Failed, Skipped, Disabled, Unloaded };
const char* modStateName(ModState s);

// What one reload / unload / load did to one mod (docs/LUA_API.md "Hot reload").
struct ModAction {
    std::string id;
    std::string action;  // "reloaded" | "unloaded" | "loaded" | "failed" | "skipped"
    std::string error;
    std::string versionBefore, versionAfter;
    int handlersRemoved = 0;
    int handlersAdded = 0;
    std::vector<std::string> released;  // registrations the teardown undid ("script X", "ai state Y", "damage handler", ...)
    std::vector<std::string> restart;   // things this mod made that only a restart can change
    bool stateCarried = false;          // the old instance's on_unload returned a value for the new one
};

struct ReloadReport {
    std::string target;
    bool ok = true;
    bool queued = false;  // the request waits for the next frame
    std::string error;    // why the request was refused (nothing was touched then)
    std::vector<ModAction> actions;
};

// A copy of one mod's state for the API (risen.mods.list).
struct ModView {
    ModInfo info;
    ModState state = ModState::Skipped;
    std::string kind;    // for Skipped / Disabled / Failed: "disabled", "missing_dependency", "dependency_unavailable", "cycle", "manifest", "load_error", "duplicate"
    std::string reason;  // why, in words
    int loadCount = 0;
    bool userEnabled = true;
    int orderIndex = -1;  // position in the load order (loaded mods), else -1
    std::vector<std::string> neededBy;  // mods that `requires` this one (transitively)
    std::vector<std::string> restartNotes;
    bool reloadable = false;
    std::string reloadNote;
    std::map<std::string, std::vector<std::string>> content;  // what the running instance made in the engine: "quests", "dialogs" (names)
};

// Owns the LuaJIT state, loads mods and dispatches engine events to Lua handlers.
//
// Threading: the game calls our hooks from several threads, so every entry point takes
// `mutex_` (recursive: a handler may trigger another hooked game call).
//
// Lua-side API (see docs/LUA_API.md):
//   risen.on(event, fn [, priority]) -> handle     risen.off(handle)
//   risen.emit(event, ...)                         risen.log.{trace,debug,info,warn,error}(...)
//   risen.version, mod.{id,name,version,dir}
//
// Every mod lives in its own instance (environment + API view). Everything a mod registers is tracked per mod, so an instance can be torn
// down and the mod run again without restarting the game (risen.mods.reload, docs/LUA_API.md "Hot reload"): handlers are removed by the
// runtime; native registries (script overrides, AI states, damage handlers ...) register a teardown with addTeardown() and learn the
// registering mod from currentOwner().
class Runtime {
public:
    static Runtime& instance();

    // Native bindings extend the `risen` table before any mod runs (e.g. risen.quest, risen.world).
    using Binding = std::function<void(sol::state_view lua, sol::table risen)>;
    void addBinding(Binding binding);

    // Per-mod additions to the `risen` view each mod gets (e.g. risen.save, which is namespaced by mod id).
    using ModBinding = std::function<void(sol::state_view lua, sol::table api, const ModInfo& mod)>;
    void addModBinding(ModBinding binding);

    // Called when an instance of a mod is torn down (reload, unload, a failed load): undo everything registered with `currentOwner() == mod` and append what was
    // released to `released` (for the report). Registered once at start-up like the bindings.
    using Teardown = std::function<void(const std::string& mod, std::vector<std::string>& released)>;
    void addTeardown(Teardown teardown);

    // `userConfigFile`: the player's mods.toml (enabled flags, custom order); empty = none.
    bool start(const std::filesystem::path& modsDir, bool unsafeLua = false, const std::filesystem::path& userConfigFile = {});
    void stop();
    bool running() const { return state_ != nullptr; }

    // The mod whose code is making a registration call right now ("" outside mod code). Set by the per-mod wrappers of the registering functions.
    static const std::string& currentOwner();

    // ---- the mod list and hot reload (risen.mods) ----
    std::vector<ModView> modViews();
    std::optional<ModView> modView(const std::string& id);
    // Reload `id` (and the mods that require it, after it). now = false queues the request for the start of the next frame (the safe default; also used
    // when a handler of an affected mod is on the stack). Returns the report (queued = true while it waits).
    ReloadReport reload(const std::string& id, bool now);
    // Tears the mod down and leaves it unloaded (with `cascade` also the mods that require it). The mod stays discoverable; load() brings it back.
    ReloadReport unload(const std::string& id, bool cascade);
    // Loads a mod that is not loaded (a failed one gets another try, a switched-off one is loaded for this run only). Mods it requires must be loaded.
    ReloadReport load(const std::string& id);
    // Persistent choices (mods.toml). Disabling a mod that others require is refused unless `cascade`. Disabled mods are unloaded when `unloadNow`.
    ReloadReport setEnabled(const std::string& id, bool enabled, bool cascade, bool applyNow);
    // The custom order (empty = automatic). Returns "" or why it was refused.
    std::string setOrder(const std::vector<std::string>& order);
    std::vector<std::string> loadOrder();      // ids of the loaded mods in load order
    std::vector<std::string> plannedOrder(bool useCustom);  // the order the next start would use (useCustom = false: the automatic order)
    UserConfig userConfig();
    std::filesystem::path userConfigPath() const { return userConfigFile_; }
    std::vector<std::string> configProblems() const { return configProblems_; }
    std::optional<ReloadReport> lastReport(const std::string& id);
    void noteRestart(const std::string& mod, const std::string& what);
    void noteContent(const std::string& mod, const std::string& kind, const std::string& name);

    // File watcher for development: a changed file of a loaded mod (Lua, TOML, CSV) reloads it.
    void setWatch(bool on, double intervalSeconds = 1.0);
    bool watching() const { return watch_; }

    // Executes queued requests and the watcher; called once per frame, before the `tick` event.
    void processPending();

    // Lifecycle bookkeeping (what a freshly (re)loaded mod has to be told about): called by the engine event hooks.
    void noteSessionStart(const std::string& mode);
    void noteSessionStop();
    void noteWorldReady(const std::string& mode);
    void noteWorldNotReady();
    std::optional<std::string> sessionMode() {
        std::lock_guard lock(mutex_);
        return sessionActive_ ? std::optional<std::string>(sessionMode_) : std::nullopt;
    }
    std::optional<std::string> worldMode() {
        std::lock_guard lock(mutex_);
        return worldReady_ ? std::optional<std::string>(worldMode_) : std::nullopt;
    }

    // Runs every handler registered for `event` in priority order (higher first).
    // Returns the first non-nil result (or nil). Exceptions and Lua errors are logged, never rethrown.
    template <class... Args>
    sol::object emit(const std::string& event, Args&&... args) {
        return dispatch<false>(event, std::string(), args...);
    }

    // Like emit(), but stops at the first handler returning a non-nil value ("replace/override" events).
    template <class... Args>
    sol::object emitUntilHandled(const std::string& event, Args&&... args) {
        return dispatch<true>(event, std::string(), args...);
    }

    // Like emit(), but only the handlers of one mod (what a freshly loaded mod missed).
    template <class... Args>
    sol::object emitTo(const std::string& mod, const std::string& event, Args&&... args) {
        return dispatch<false>(event, mod, args...);
    }

    // Like emit() with an argument list whose length is only known at run time (the queued gameplay events).
    sol::object emitList(const std::string& event, const std::vector<sol::object>& args) {
        std::unique_lock lock(mutex_);
        if (!state_) {
            return sol::object{};
        }
        const auto it = handlers_.find(event);
        if (it == handlers_.end()) {
            return sol::object{};
        }
        const std::vector<Handler> snapshot = it->second;
        for (const Handler& h : snapshot) {
            ActiveGuard active(*this, h.mod);
            crash::Scope trail(event, h.mod);
            try {
                sol::protected_function_result r = h.fn(sol::as_args(args));
                if (!r.valid()) {
                    logHandlerError(h, event, r.get<sol::error>());
                }
            } catch (const std::exception& e) {
                spdlog::error("lua: [{}] handler for '{}' threw: {}", h.mod, event, e.what());
            }
        }
        return sol::object{};
    }

    // Cheap check so hot paths (per-frame events) cost nothing when no mod listens.
    bool hasHandlers(const std::string& event) {
        std::lock_guard lock(mutex_);
        const auto it = handlers_.find(event);
        return it != handlers_.end() && !it->second.empty();
    }

    // Only valid while holding the lock returned by lock(); use for native code that must touch Lua.
    sol::state_view lua() { return sol::state_view(state_->lua_state()); }
    std::unique_lock<std::recursive_mutex> lock() { return std::unique_lock(mutex_); }

private:
    struct Handler {
        std::uint64_t id;
        std::string mod;
        int priority;
        sol::main_protected_function fn;  // main thread: a handler registered from a scenario coroutine must not run on that coroutine
    };

    // The live pieces of one mod instance; dropping it (after the teardown) lets the Lua garbage collector take the instance.
    struct Live {
        sol::environment env;
        sol::table api;
        std::vector<sol::main_protected_function> unloadFns;
    };

    struct Record {
        ModInfo info;
        ModState state = ModState::Skipped;
        std::string kind;
        std::string reason;
        int loadCount = 0;
        std::unique_ptr<Live> live;
        sol::object carried;                 // returned by the previous instance's on_unload
        std::string previousVersion;         // version of the instance that was torn down last (empty on the first load)
        std::vector<std::string> restartNotes;
        std::map<std::string, std::vector<std::string>> content;  // quests / dialogs the instance defined (cleared by a teardown)
        std::optional<ReloadReport> last;
        std::map<std::string, std::filesystem::file_time_type> stamps;  // watcher
    };

    struct ActiveGuard {
        Runtime& rt;
        std::string mod;
        ActiveGuard(Runtime& r, const std::string& m) : rt(r), mod(m) { ++rt.active_[mod]; }
        ~ActiveGuard() {
            auto it = rt.active_.find(mod);
            if (it != rt.active_.end() && --it->second <= 0) {
                rt.active_.erase(it);
            }
        }
    };

    Runtime() = default;

    std::uint64_t addHandler(const std::string& mod, const std::string& event, sol::main_protected_function fn, int prio);
    void removeHandler(std::uint64_t id);
    int removeHandlersOf(const std::string& mod);
    int countHandlersOf(const std::string& mod);
    sol::table makeBaseEnv(bool unsafeLua);
    sol::table makeModApi(Record& rec);
    void wrapOwned(sol::state_view lua, sol::table api, const std::string& modId);
    bool runMod(Record& rec, std::string& error);  // builds the instance and runs main.lua
    ModAction loadOne(Record& rec, bool replay);   // runMod + failure handling + lifecycle replay
    ModAction tearDown(Record& rec, const char* why);
    void replayLifecycle(const std::string& id);
    void logHandlerError(const Handler& h, const std::string& event, const sol::error& err);
    Record* findRecord(const std::string& id);
    std::vector<ModInfo> knownInfos();
    std::string syntaxProblem(const std::filesystem::path& file);
    void stampMod(Record& rec);
    std::vector<std::string> neededBy(const std::string& id);
    ReloadReport doReload(const std::string& id);
    void rebuildOrder();
    void scanWatch(bool initial);
    bool activeIn(const std::vector<std::string>& ids);

    template <bool Until, class... Args>
    sol::object dispatch(const std::string& event, const std::string& onlyMod, Args&... args) {
        std::unique_lock lock(mutex_);
        if (!state_) {
            return sol::object{};
        }
        const auto it = handlers_.find(event);
        if (it == handlers_.end()) {
            return sol::object{};
        }
        const std::vector<Handler> snapshot = it->second;  // handlers may (un)register while running
        for (const Handler& h : snapshot) {
            if (!onlyMod.empty() && h.mod != onlyMod) {
                continue;
            }
            ActiveGuard active(*this, h.mod);
            crash::Scope trail(event, h.mod);
            try {
                sol::protected_function_result r = h.fn(args...);
                if (!r.valid()) {
                    logHandlerError(h, event, r.get<sol::error>());
                    continue;
                }
                if constexpr (Until) {
                    sol::object o = r.get<sol::object>();
                    if (o.valid() && o.get_type() != sol::type::lua_nil) {
                        return o;
                    }
                }
            } catch (const std::exception& e) {
                spdlog::error("lua: [{}] handler for '{}' threw: {}", h.mod, event, e.what());
            }
        }
        return sol::object{};
    }

    std::recursive_mutex mutex_;
    std::unique_ptr<sol::state> state_;
    std::vector<Binding> bindings_;
    std::vector<ModBinding> modBindings_;
    std::vector<Teardown> teardowns_;
    std::map<std::string, std::vector<Handler>> handlers_;
    std::uint64_t nextHandle_ = 1;
    sol::table base_;   // read-only whitelist inherited by every mod environment
    sol::table risen_;  // shared `risen` table (native bindings live here)
    bool unsafeLua_ = false;

    std::filesystem::path modsDir_;
    std::filesystem::path userConfigFile_;
    UserConfig userConfig_;
    std::vector<std::string> configProblems_;
    std::map<std::string, std::unique_ptr<Record>> records_;  // every mod folder that was found (loaded or not)
    std::vector<std::string> order_;                          // loaded mods, in load order
    std::vector<std::string> pendingReloads_;
    std::map<std::string, int> active_;                       // handlers of a mod on the stack

    bool sessionActive_ = false;
    std::string sessionMode_;
    bool worldReady_ = false;
    std::string worldMode_;

    bool watch_ = false;
    double watchInterval_ = 1.0;
    std::chrono::steady_clock::time_point lastWatch_{};
    std::chrono::steady_clock::time_point lastRequestPoll_{};
    std::filesystem::path reloadRequestFile_;  // <spass>/reload.txt: ids (or "all") of mods to reload, one per line; consumed when read
};

}  // namespace spass::lua
