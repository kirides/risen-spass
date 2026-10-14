#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <sol/sol.hpp>
#include <spdlog/spdlog.h>

#include "lua/mods.h"

namespace spass::lua {

// Owns the LuaJIT state, loads mods and dispatches engine events to Lua handlers.
//
// Threading: the game calls our hooks from several threads, so every entry point takes
// `mutex_` (recursive: a handler may trigger another hooked game call).
//
// Lua-side API (see docs/LUA_API.md):
//   risen.on(event, fn [, priority]) -> handle     risen.off(handle)
//   risen.emit(event, ...)                         risen.log.{trace,debug,info,warn,error}(...)
//   risen.version, mod.{id,name,version,dir}
class Runtime {
public:
    static Runtime& instance();

    // Native bindings extend the `risen` table before any mod runs (e.g. risen.quest, risen.world).
    using Binding = std::function<void(sol::state_view lua, sol::table risen)>;
    void addBinding(Binding binding);

    bool start(const std::filesystem::path& modsDir, bool unsafeLua = false);
    void stop();
    bool running() const { return state_ != nullptr; }

    // Runs every handler registered for `event` in priority order (higher first).
    // Returns the first non-nil result (or nil). Exceptions and Lua errors are logged, never rethrown.
    template <class... Args>
    sol::object emit(const std::string& event, Args&&... args) {
        return dispatch<false>(event, args...);
    }

    // Like emit(), but stops at the first handler returning a non-nil value ("replace/override" events).
    template <class... Args>
    sol::object emitUntilHandled(const std::string& event, Args&&... args) {
        return dispatch<true>(event, args...);
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
        sol::protected_function fn;
    };

    Runtime() = default;

    std::uint64_t addHandler(const std::string& mod, const std::string& event, sol::protected_function fn, int prio);
    void removeHandler(std::uint64_t id);
    sol::table makeBaseEnv(bool unsafeLua);
    sol::table makeModApi(const ModInfo& mod);
    bool loadMod(const ModInfo& mod);
    void logHandlerError(const Handler& h, const std::string& event, const sol::error& err);

    template <bool Until, class... Args>
    sol::object dispatch(const std::string& event, Args&... args) {
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
    std::map<std::string, std::vector<Handler>> handlers_;
    std::uint64_t nextHandle_ = 1;
    sol::table base_;   // read-only whitelist inherited by every mod environment
    sol::table risen_;  // shared `risen` table (native bindings live here)
    std::vector<ModInfo> loaded_;
};

}  // namespace spass::lua
