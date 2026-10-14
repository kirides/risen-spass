#include "lua/runtime.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace spass::lua {
namespace {

// Globals every mod may use. Everything that can touch the OS, load native code, load bytecode
// or inspect other mods' state (io, os.execute, ffi, debug, package, dofile, ...) is left out unless
// the user opted into `unsafe_lua` in config.toml.
constexpr const char* kSafeGlobals[] = {
    "assert", "error",  "ipairs",   "pairs",     "next",     "pcall",   "xpcall",
    "select", "tonumber", "tostring", "type",    "unpack",   "rawget",  "rawset",
    "rawequal", "rawlen", "setmetatable", "getmetatable", "_VERSION",
};
constexpr const char* kSafeLibs[] = {"string", "table", "math", "bit"};
constexpr const char* kSafeOs[] = {"time", "clock", "date", "difftime"};

sol::table copyTable(sol::state_view lua, const sol::table& src) {
    sol::table dst = lua.create_table();
    for (const auto& [k, v] : src) {
        dst[k] = v;
    }
    return dst;
}

std::string joinArgs(sol::this_state ts, const sol::variadic_args& va) {
    sol::state_view lua(ts);
    sol::function tostring = lua["tostring"];
    std::string out;
    for (const auto& arg : va) {
        if (!out.empty()) {
            out += ' ';
        }
        out += tostring(arg).get<std::string>();
    }
    return out;
}

bool readFile(const std::filesystem::path& p, std::string& out) {
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

// Precompiled LuaJIT bytecode starts with ESC; refusing it keeps mods auditable as plain text and
// closes the well-known bytecode-based sandbox escapes.
bool isBytecode(const std::string& s) {
    return !s.empty() && s[0] == '\x1b';
}

}  // namespace

Runtime& Runtime::instance() {
    static Runtime rt;
    return rt;
}

void Runtime::addBinding(Binding binding) {
    std::lock_guard lock(mutex_);
    bindings_.push_back(std::move(binding));
}

bool Runtime::start(const std::filesystem::path& modsDir, bool unsafeLua) {
    std::lock_guard lock(mutex_);
    if (state_) {
        return true;
    }
    try {
        state_ = std::make_unique<sol::state>();
        sol::state& lua = *state_;
        lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math, sol::lib::os,
                           sol::lib::bit32, sol::lib::jit);
        if (unsafeLua) {
            lua.open_libraries(sol::lib::package, sol::lib::io, sol::lib::debug, sol::lib::ffi);
            spdlog::warn("lua: unsafe_lua is enabled, mods can use io/ffi/debug/package");
        }
        sol::protected_function::set_default_handler(lua["debug"]["traceback"]);

        base_ = makeBaseEnv(unsafeLua);

        risen_ = lua.create_table();
        risen_["version"] = SPASS_VERSION;
        sol::table log = lua.create_table();
        const auto bindLevel = [&](const char* name, spdlog::level::level_enum lvl) {
            log.set_function(name, [lvl](sol::this_state ts, sol::variadic_args va) {
                spdlog::log(lvl, "lua: {}", joinArgs(ts, va));
            });
        };
        bindLevel("trace", spdlog::level::trace);
        bindLevel("debug", spdlog::level::debug);
        bindLevel("info", spdlog::level::info);
        bindLevel("warn", spdlog::level::warn);
        bindLevel("error", spdlog::level::err);
        risen_["log"] = log;

        for (const auto& b : bindings_) {
            b(lua, risen_);
        }

        const auto mods = resolveLoadOrder(discoverMods(modsDir));
        spdlog::info("lua: LuaJIT started, {} mod(s) to load", mods.size());
        for (const auto& m : mods) {
            if (loadMod(m)) {
                loaded_.push_back(m);
            }
        }
        emit("mods.loaded");
        return true;
    } catch (const std::exception& e) {
        spdlog::error("lua: startup failed: {}", e.what());
        handlers_.clear();
        state_.reset();
        return false;
    }
}

void Runtime::stop() {
    std::lock_guard lock(mutex_);
    if (!state_) {
        return;
    }
    emit("shutdown");
    handlers_.clear();
    loaded_.clear();
    base_ = sol::table{};
    risen_ = sol::table{};
    state_.reset();
}

sol::table Runtime::makeBaseEnv(bool unsafeLua) {
    sol::state& lua = *state_;
    if (unsafeLua) {
        return lua.globals();
    }
    sol::table base = lua.create_table();
    for (const char* name : kSafeGlobals) {
        base[name] = lua[name];
    }
    for (const char* lib : kSafeLibs) {
        base[lib] = copyTable(lua, lua[lib]);
    }
    sol::table os = lua.create_table();
    for (const char* name : kSafeOs) {
        os[name] = lua["os"][name];
    }
    base["os"] = os;
    return base;
}

sol::table Runtime::makeModApi(const ModInfo& mod) {
    sol::state& lua = *state_;
    // Per-mod view of `risen`: reads fall through to the shared table, `on` remembers the mod id.
    sol::table api = lua.create_table();
    sol::table meta = lua.create_table();
    meta["__index"] = risen_;
    api[sol::metatable_key] = meta;

    api.set_function("on", [this, id = mod.id](const std::string& event, sol::protected_function fn,
                                               sol::optional<int> priority) {
        return addHandler(id, event, std::move(fn), priority.value_or(0));
    });
    api.set_function("off", [this](std::uint64_t handle) { removeHandler(handle); });
    api.set_function("emit", [this](const std::string& event, sol::variadic_args va) {
        std::vector<sol::object> args(va.begin(), va.end());
        // Mod-to-mod events: forward the arguments verbatim.
        std::lock_guard lock(mutex_);
        const auto it = handlers_.find(event);
        if (it == handlers_.end()) {
            return;
        }
        const auto snapshot = it->second;
        for (const Handler& h : snapshot) {
            sol::protected_function_result r = h.fn(sol::as_args(args));
            if (!r.valid()) {
                logHandlerError(h, event, r.get<sol::error>());
            }
        }
    });
    return api;
}

std::uint64_t Runtime::addHandler(const std::string& mod, const std::string& event, sol::protected_function fn,
                                  int prio) {
    std::lock_guard lock(mutex_);
    const std::uint64_t id = nextHandle_++;
    auto& list = handlers_[event];
    list.push_back(Handler{id, mod, prio, std::move(fn)});
    std::stable_sort(list.begin(), list.end(), [](const Handler& a, const Handler& b) { return a.priority > b.priority; });
    return id;
}

void Runtime::removeHandler(std::uint64_t id) {
    std::lock_guard lock(mutex_);
    for (auto& [event, list] : handlers_) {
        std::erase_if(list, [id](const Handler& h) { return h.id == id; });
    }
}

void Runtime::logHandlerError(const Handler& h, const std::string& event, const sol::error& err) {
    spdlog::error("lua: [{}] error in handler for '{}': {}", h.mod, event, err.what());
}

bool Runtime::loadMod(const ModInfo& mod) {
    sol::state& lua = *state_;
    const auto mainPath = mod.dir / mod.main;
    std::string source;
    if (!readFile(mainPath, source)) {
        spdlog::error("lua: [{}] cannot read {}", mod.id, mainPath.string());
        return false;
    }
    if (isBytecode(source)) {
        spdlog::error("lua: [{}] {} is precompiled bytecode, which is not allowed", mod.id, mainPath.string());
        return false;
    }

    // The environment inherits only from the whitelist; writes stay local to the mod.
    sol::environment env(lua, sol::create);
    sol::table meta = lua.create_table();
    meta["__index"] = base_;
    env[sol::metatable_key] = meta;
    env["_G"] = env;
    env["risen"] = makeModApi(mod);

    sol::table info = lua.create_table();
    info["id"] = mod.id;
    info["name"] = mod.name;
    info["version"] = mod.version;
    info["author"] = mod.author;
    info["dir"] = mod.dir.generic_string();
    env["mod"] = info;

    env.set_function("print", [id = mod.id](sol::this_state ts, sol::variadic_args va) {
        spdlog::info("lua: [{}] {}", id, joinArgs(ts, va));
    });

    // Mod-local require(): "a.b" -> <mod dir>/a/b.lua, cached per mod, same environment.
    sol::table loadedModules = lua.create_table();
    env.set_function("require", [this, env, loadedModules, dir = mod.dir, id = mod.id](
                                    sol::this_state ts, const std::string& name) mutable -> sol::object {
        sol::state_view L(ts);
        if (sol::object cached = loadedModules[name]; cached.valid() && cached.get_type() != sol::type::lua_nil) {
            return cached;
        }
        if (name.find("..") != std::string::npos || name.find(':') != std::string::npos ||
            name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
            throw sol::error("require: invalid module name '" + name + "'");
        }
        std::string rel = name;
        std::replace(rel.begin(), rel.end(), '.', '/');
        std::string src;
        const auto path = dir / (rel + ".lua");
        if (!readFile(path, src)) {
            throw sol::error("require: module '" + name + "' not found in mod '" + id + "'");
        }
        if (isBytecode(src)) {
            throw sol::error("require: bytecode modules are not allowed");
        }
        sol::load_result chunk = L.load(src, "@" + path.generic_string());
        if (!chunk.valid()) {
            throw sol::error(chunk.get<sol::error>().what());
        }
        sol::protected_function fn = chunk;
        sol::set_environment(env, fn);
        sol::protected_function_result r = fn(name);
        if (!r.valid()) {
            throw r.get<sol::error>();
        }
        sol::object result = r.get<sol::object>();
        if (!result.valid() || result.get_type() == sol::type::lua_nil) {
            result = sol::make_object(L, true);
        }
        loadedModules[name] = result;
        return result;
    });

    sol::load_result chunk = lua.load(source, "@" + mainPath.generic_string());
    if (!chunk.valid()) {
        spdlog::error("lua: [{}] syntax error: {}", mod.id, chunk.get<sol::error>().what());
        return false;
    }
    sol::protected_function fn = chunk;
    sol::set_environment(env, fn);
    sol::protected_function_result r = fn();
    if (!r.valid()) {
        spdlog::error("lua: [{}] error while loading: {}", mod.id, r.get<sol::error>().what());
        return false;
    }
    spdlog::info("lua: loaded mod '{}' {} ({})", mod.id, mod.version, mod.name);
    return true;
}

}  // namespace spass::lua
