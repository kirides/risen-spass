#include "lua/runtime.h"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

#include "common/trace.h"

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

// Functions that register something a teardown has to be able to undo: the per-mod view of `risen` wraps them so the registering mod is known
// (Runtime::currentOwner) to the native registries.
constexpr const char* kOwned[][2] = {
    {"script", "define"}, {"script", "override"}, {"ai", "state"}, {"ai", "callback"}, {"ai", "perceive"},
    {"combat", "on_damage"}, {"dialog", "create"}, {"quest", "define"}, {"mods", "note_restart"},
};

std::string g_owner;  // see Runtime::currentOwner

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

// lua_CFunction closure: upvalue 1 = the function to call, upvalue 2 = the id of the mod that owns the view. Sets Runtime::currentOwner while the
// function runs, so the native registries can attribute what it registers. Errors are passed on unchanged.
int ownedCall(lua_State* L) {
    const int nargs = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    const char* id = lua_tostring(L, lua_upvalueindex(2));
    std::string previous = g_owner;
    g_owner = id != nullptr ? id : "";
    const int rc = lua_pcall(L, nargs, LUA_MULTRET, 0);
    g_owner = previous;
    if (rc != 0) {
        return lua_error(L);
    }
    return lua_gettop(L);
}

}  // namespace

const char* modStateName(ModState s) {
    switch (s) {
        case ModState::Loaded: return "loaded";
        case ModState::Failed: return "failed";
        case ModState::Skipped: return "skipped";
        case ModState::Disabled: return "disabled";
        case ModState::Unloaded: return "unloaded";
    }
    return "?";
}

Runtime& Runtime::instance() {
    static Runtime rt;
    return rt;
}

const std::string& Runtime::currentOwner() { return g_owner; }

void Runtime::addBinding(Binding binding) {
    std::lock_guard lock(mutex_);
    bindings_.push_back(std::move(binding));
}

void Runtime::addModBinding(ModBinding binding) {
    std::lock_guard lock(mutex_);
    modBindings_.push_back(std::move(binding));
}

void Runtime::addTeardown(Teardown teardown) {
    std::lock_guard lock(mutex_);
    teardowns_.push_back(std::move(teardown));
}

bool Runtime::start(const std::filesystem::path& modsDir, bool unsafeLua, const std::filesystem::path& userConfigFile) {
    std::lock_guard lock(mutex_);
    if (state_) {
        return true;
    }
    try {
        state_ = std::make_unique<sol::state>();
        sol::state& lua = *state_;
        unsafeLua_ = unsafeLua;
        lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math, sol::lib::os,
                           sol::lib::bit32, sol::lib::jit, sol::lib::debug);  // debug: tracebacks only, never in a mod environment
        if (unsafeLua) {
            lua.open_libraries(sol::lib::package, sol::lib::io, sol::lib::debug, sol::lib::ffi);
            spdlog::warn("lua: unsafe_lua is enabled, mods can use io/ffi/debug/package");
        }
        sol::protected_function::set_default_handler(lua["debug"]["traceback"]);
        sol::main_protected_function::set_default_handler(lua["debug"]["traceback"]);

        base_ = makeBaseEnv(unsafeLua);

        risen_ = lua.create_table();
        risen_["version"] = SPASS_VERSION;
        risen_["api_version"] = kApiVersion;
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

        // risen.trace(channel, ...) writes "[trace:channel] ..." when the channel is on; risen.trace.enable("a,b") / disable / enabled(ch)
        sol::table tr = lua.create_table();
        tr.set_function("enable", [](const std::string& list) {
            trace::enable(list);
            return trace::enabledList();
        });
        tr.set_function("disable", [](const std::string& list) {
            trace::disable(list);
            return trace::enabledList();
        });
        tr.set_function("enabled", [](const std::string& channel) { return trace::enabledName(channel); });
        tr.set_function("channels", [] { return trace::enabledList(); });
        sol::table trMeta = lua.create_table();
        trMeta.set_function("__call", [](sol::this_state ts, sol::table, const std::string& channel, sol::variadic_args va) {
            if (trace::enabledName(channel)) {
                trace::emitNamed(channel, joinArgs(ts, va));
            }
        });
        tr[sol::metatable_key] = trMeta;
        risen_["trace"] = tr;

        for (const auto& b : bindings_) {
            b(lua, risen_);
        }

        modsDir_ = modsDir;
        userConfigFile_ = userConfigFile;
        reloadRequestFile_ = userConfigFile.empty() ? std::filesystem::path() : userConfigFile.parent_path() / "reload.txt";
        configProblems_.clear();
        userConfig_ = loadUserConfig(userConfigFile_, configProblems_);
        for (const auto& p : configProblems_) {
            spdlog::warn("mods: mods.toml: {}", p);
        }

        std::vector<ModIssue> issues;
        std::vector<ModInfo> found = discoverMods(modsDir, &issues);
        const LoadPlan plan = planLoad(found, userConfig_);
        for (const auto& w : plan.warnings) {
            spdlog::warn("mods: {}", w);
        }
        for (const auto& issue : issues) {
            auto rec = std::make_unique<Record>();
            rec->info.id = "!" + issue.folder;
            rec->info.name = issue.folder;
            rec->info.dir = modsDir / issue.folder;
            rec->state = ModState::Failed;
            rec->kind = "manifest";
            rec->reason = issue.reason;
            records_[rec->info.id] = std::move(rec);
        }
        for (const auto& s : plan.skipped) {
            auto rec = std::make_unique<Record>();
            rec->info = s.mod;
            rec->state = s.kind == "disabled" ? ModState::Disabled : ModState::Skipped;
            rec->kind = s.kind;
            rec->reason = s.reason;
            if (!records_.contains(s.mod.id)) {
                records_[s.mod.id] = std::move(rec);
            }
        }
        spdlog::info("lua: LuaJIT started, {} mod(s) to load", plan.ordered.size());
        for (const auto& m : plan.ordered) {
            auto rec = std::make_unique<Record>();
            rec->info = m;
            Record& ref = *rec;
            records_[m.id] = std::move(rec);
            loadOne(ref, /*replay=*/false);
        }
        rebuildOrder();
        SPASS_TRACE(trace::Channel::Events, "mods.loaded()");
        emit("mods.loaded");
        return true;
    } catch (const std::exception& e) {
        spdlog::error("lua: startup failed: {}", e.what());
        handlers_.clear();
        records_.clear();
        state_.reset();
        return false;
    }
}

void Runtime::stop() {
    std::lock_guard lock(mutex_);
    if (!state_) {
        return;
    }
    SPASS_TRACE(trace::Channel::Events, "shutdown()");
    emit("shutdown");
    handlers_.clear();
    records_.clear();
    order_.clear();
    pendingReloads_.clear();
    active_.clear();
    sessionActive_ = worldReady_ = false;
    watch_ = false;
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

// Wraps the registering functions of the per-mod view so currentOwner() names the mod while they run.
void Runtime::wrapOwned(sol::state_view lua, sol::table api, const std::string& modId) {
    for (const auto& entry : kOwned) {
        sol::object shared = api[entry[0]];  // through the __index chain: the shared table, or a per-mod proxy an earlier binding made
        if (!shared.valid() || shared.get_type() != sol::type::table) {
            continue;
        }
        sol::table sharedTable = shared.as<sol::table>();
        sol::object own = api.raw_get<sol::object>(entry[0]);
        sol::table proxy;
        if (own.valid() && own.get_type() == sol::type::table) {
            proxy = own.as<sol::table>();
        } else {
            proxy = lua.create_table();
            sol::table meta = lua.create_table();
            meta["__index"] = sharedTable;
            proxy[sol::metatable_key] = meta;
            api.raw_set(entry[0], proxy);
        }
        sol::object fn = proxy[entry[1]];
        if (!fn.valid() || fn.get_type() != sol::type::function) {
            continue;
        }
        lua_State* L = lua.lua_state();
        fn.push();
        lua_pushlstring(L, modId.data(), modId.size());
        lua_pushcclosure(L, ownedCall, 2);
        sol::object wrapped(L, -1);
        lua_pop(L, 1);
        proxy.raw_set(entry[1], wrapped);
    }
}

sol::table Runtime::makeModApi(Record& rec) {
    sol::state& lua = *state_;
    const ModInfo& mod = rec.info;
    // Per-mod view of `risen`: reads fall through to the shared table, `on` remembers the mod id.
    sol::table api = lua.create_table();
    sol::table meta = lua.create_table();
    meta["__index"] = risen_;
    api[sol::metatable_key] = meta;

    api.set_function("on", [this, id = mod.id](const std::string& event, sol::main_protected_function fn,
                                               sol::optional<int> priority) {
        return addHandler(id, event, std::move(fn), priority.value_or(0));
    });
    api.set_function("off", [this](std::uint64_t handle) { removeHandler(handle); });
    api.set_function("emit", [this, id = mod.id](sol::this_state ts, const std::string& event, sol::variadic_args va) {
        std::vector<sol::object> args(va.begin(), va.end());
        if (trace::enabled(trace::Channel::Events)) {
            trace::emit(trace::Channel::Events, "mod event " + event + "(" + joinArgs(ts, va) + ") from '" + id + "'");
        }
        // Mod-to-mod events: forward the arguments verbatim.
        std::lock_guard lock(mutex_);
        const auto it = handlers_.find(event);
        if (it == handlers_.end()) {
            return;
        }
        const auto snapshot = it->second;
        for (const Handler& h : snapshot) {
            ActiveGuard active(*this, h.mod);
            sol::protected_function_result r = h.fn(sol::as_args(args));
            if (!r.valid()) {
                logHandlerError(h, event, r.get<sol::error>());
            }
        }
    });
    // risen.on_unload(fn): fn(reason) runs when this instance of the mod is torn down (reason "reload" | "unload" | "disable" | "failed"). Its first
    // non-nil result is handed to the next instance as `mod.reload.state`.
    api.set_function("on_unload", [this, id = mod.id](sol::main_protected_function fn) {
        std::lock_guard lock(mutex_);
        if (Record* r = findRecord(id); r != nullptr && r->live) {
            r->live->unloadFns.push_back(std::move(fn));
        }
    });
    for (const auto& b : modBindings_) {
        b(lua, api, mod);
    }
    wrapOwned(lua, api, mod.id);
    return api;
}

std::uint64_t Runtime::addHandler(const std::string& mod, const std::string& event, sol::main_protected_function fn,
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

int Runtime::removeHandlersOf(const std::string& mod) {
    int n = 0;
    for (auto it = handlers_.begin(); it != handlers_.end();) {
        n += static_cast<int>(std::erase_if(it->second, [&](const Handler& h) { return h.mod == mod; }));
        it = it->second.empty() ? handlers_.erase(it) : std::next(it);
    }
    return n;
}

int Runtime::countHandlersOf(const std::string& mod) {
    int n = 0;
    for (const auto& [event, list] : handlers_) {
        n += static_cast<int>(std::count_if(list.begin(), list.end(), [&](const Handler& h) { return h.mod == mod; }));
    }
    return n;
}

void Runtime::logHandlerError(const Handler& h, const std::string& event, const sol::error& err) {
    spdlog::error("lua: [{}] error in handler for '{}': {}", h.mod, event, err.what());
    SPASS_TRACE(trace::Channel::Lua, "error in handler for '{}' of mod '{}': {}", event, h.mod, err.what());
}

Runtime::Record* Runtime::findRecord(const std::string& id) {
    const auto it = records_.find(id);
    return it == records_.end() ? nullptr : it->second.get();
}

std::string Runtime::syntaxProblem(const std::filesystem::path& file) {
    std::string source;
    if (!readFile(file, source)) {
        return "cannot read " + file.string();
    }
    if (isBytecode(source)) {
        return file.string() + " is precompiled bytecode, which is not allowed";
    }
    sol::load_result chunk = state_->load(source, "@" + file.generic_string());
    if (!chunk.valid()) {
        return chunk.get<sol::error>().what();
    }
    return {};
}

// ---- running a mod ----------------------------------------------------------------------------------------------------------------------

bool Runtime::runMod(Record& rec, std::string& error) {
    sol::state& lua = *state_;
    const ModInfo& mod = rec.info;
    const auto mainPath = mod.dir / mod.main;
    std::string source;
    if (!readFile(mainPath, source)) {
        error = "cannot read " + mainPath.string();
        spdlog::error("lua: [{}] {}", mod.id, error);
        return false;
    }
    if (isBytecode(source)) {
        error = mainPath.string() + " is precompiled bytecode, which is not allowed";
        spdlog::error("lua: [{}] {}", mod.id, error);
        return false;
    }

    rec.live = std::make_unique<Live>();
    Live& live = *rec.live;
    // The environment inherits only from the whitelist; writes stay local to the mod.
    live.env = sol::environment(lua, sol::create);
    sol::environment& env = live.env;
    sol::table meta = lua.create_table();
    meta["__index"] = base_;
    env[sol::metatable_key] = meta;
    env["_G"] = env;
    live.api = makeModApi(rec);
    env["risen"] = live.api;

    sol::table info = lua.create_table();
    info["id"] = mod.id;
    info["name"] = mod.name;
    info["version"] = mod.version;
    info["author"] = mod.author;
    info["dir"] = mod.dir.generic_string();
    if (rec.loadCount > 0 || !rec.previousVersion.empty()) {
        // a re-run of the mod inside the same process: what the previous instance left behind
        sol::table reload = lua.create_table();
        reload["count"] = rec.loadCount;
        reload["from_version"] = rec.previousVersion;
        if (rec.carried.valid() && rec.carried.get_type() != sol::type::lua_nil) {
            reload["state"] = rec.carried;
        }
        info["reload"] = reload;
    }
    if (!mod.retire.empty()) {
        sol::table retire = lua.create_table();
        for (const auto& [kind, names] : mod.retire) {
            sol::table list = lua.create_table();
            int i = 1;
            for (const std::string& n : names) {
                list[i++] = n;
            }
            retire[kind] = list;
        }
        info["retire"] = retire;
    }
    env["mod"] = info;

    env.set_function("print", [id = mod.id](sol::this_state ts, sol::variadic_args va) {
        spdlog::info("lua: [{}] {}", id, joinArgs(ts, va));
    });

    // Mod-local require(): "a.b" -> <mod dir>/a/b.lua, cached per mod, same environment.
    sol::table loadedModules = lua.create_table();
    env.set_function("require", [env, loadedModules, dir = mod.dir, id = mod.id](
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
        error = std::string("syntax error: ") + chunk.get<sol::error>().what();
        spdlog::error("lua: [{}] {}", mod.id, error);
        return false;
    }
    sol::protected_function fn = chunk;
    sol::set_environment(env, fn);
    const std::string previousOwner = g_owner;
    g_owner = mod.id;  // registrations made while main.lua runs belong to this mod even when a binding is called without the per-mod view
    sol::protected_function_result r = fn();
    g_owner = previousOwner;
    if (!r.valid()) {
        error = std::string("error while loading: ") + r.get<sol::error>().what();
        spdlog::error("lua: [{}] {}", mod.id, error);
        SPASS_TRACE(trace::Channel::Lua, "mod '{}' failed to load: {}", mod.id, r.get<sol::error>().what());
        return false;
    }
    spdlog::info("lua: loaded mod '{}' {} ({})", mod.id, mod.version, mod.name);
    return true;
}

ModAction Runtime::loadOne(Record& rec, bool replay) {
    ModAction a;
    a.id = rec.info.id;
    a.versionBefore = rec.previousVersion;
    a.versionAfter = rec.info.version;
    std::string error;
    const bool again = rec.loadCount > 0 || !rec.previousVersion.empty();
    if (runMod(rec, error)) {
        rec.state = ModState::Loaded;
        rec.kind.clear();
        rec.reason.clear();
        ++rec.loadCount;
        a.action = again ? "reloaded" : "loaded";
        a.handlersAdded = countHandlersOf(rec.info.id);
        stampMod(rec);
        if (replay) {
            replayLifecycle(rec.info.id);
        }
    } else {
        // a half-run mod would leave its first registrations behind: undo them
        ModAction td = tearDown(rec, "failed");
        a.released = td.released;
        rec.state = ModState::Failed;
        rec.kind = "load_error";
        rec.reason = error;
        a.action = "failed";
        a.error = error;
        stampMod(rec);  // the watcher reloads it once the file changes
    }
    a.restart = rec.restartNotes;
    a.stateCarried = rec.carried.valid() && rec.carried.get_type() != sol::type::lua_nil;
    return a;
}

ModAction Runtime::tearDown(Record& rec, const char* why) {
    ModAction a;
    a.id = rec.info.id;
    a.versionBefore = rec.info.version;
    a.handlersRemoved = countHandlersOf(rec.info.id);
    rec.carried = sol::object{};
    if (rec.live) {
        // the mod's own cleanup first (it may still use its API), newest first
        auto fns = std::move(rec.live->unloadFns);
        bool haveState = false;
        for (auto it = fns.rbegin(); it != fns.rend(); ++it) {
            try {
                sol::protected_function_result r = (*it)(std::string(why));
                if (!r.valid()) {
                    spdlog::warn("lua: [{}] on_unload failed: {}", rec.info.id, r.get<sol::error>().what());
                    continue;
                }
                sol::object o = r.get<sol::object>();
                if (!haveState && o.valid() && o.get_type() != sol::type::lua_nil) {
                    rec.carried = o;
                    haveState = true;
                }
            } catch (const std::exception& e) {
                spdlog::warn("lua: [{}] on_unload threw: {}", rec.info.id, e.what());
            }
        }
    }
    for (const auto& t : teardowns_) {
        try {
            t(rec.info.id, a.released);
        } catch (const std::exception& e) {
            spdlog::warn("lua: teardown of '{}' threw: {}", rec.info.id, e.what());
        }
    }
    removeHandlersOf(rec.info.id);
    rec.content.clear();
    rec.live.reset();
    rec.previousVersion = rec.info.version;
    a.restart = rec.restartNotes;
    SPASS_TRACE(trace::Channel::Lua, "teardown of mod '{}' ({}): {} handler(s), {} registration(s) released", rec.info.id, why, a.handlersRemoved, a.released.size());
    return a;
}

void Runtime::replayLifecycle(const std::string& id) {
    // what the mod missed because it was not there when it happened (docs/LUA_API.md "Hot reload")
    Record* rec = findRecord(id);
    SPASS_TRACE(trace::Channel::Events, "replay for '{}': mods.loaded{}{}", id, sessionActive_ ? ", session.start" : "", worldReady_ ? ", world.ready" : "");
    emitTo(id, "mods.loaded");
    if (rec != nullptr && !rec->previousVersion.empty() && rec->previousVersion != rec->info.version) {
        emitTo(id, "mods.migrate", rec->previousVersion, rec->info.version);
    }
    if (sessionActive_) {
        emitTo(id, "session.start", sessionMode_);
    }
    if (worldReady_) {
        emitTo(id, "world.ready", worldMode_);
    }
}

// ---- the mod list -----------------------------------------------------------------------------------------------------------------------

std::vector<ModInfo> Runtime::knownInfos() {
    std::vector<ModInfo> out;
    for (const auto& [id, rec] : records_) {
        if (!id.empty() && id[0] != '!') {
            out.push_back(rec->info);
        }
    }
    return out;
}

std::vector<std::string> Runtime::neededBy(const std::string& id) { return dependentsOf(knownInfos(), id); }

void Runtime::rebuildOrder() {
    order_.clear();
    const LoadPlan plan = planLoad(knownInfos(), userConfig_);
    for (const auto& m : plan.ordered) {
        const Record* rec = findRecord(m.id);
        if (rec != nullptr && rec->state == ModState::Loaded) {
            order_.push_back(m.id);
        }
    }
}

std::vector<std::string> Runtime::loadOrder() {
    std::lock_guard lock(mutex_);
    return order_;
}

std::vector<std::string> Runtime::plannedOrder(bool useCustom) {
    std::lock_guard lock(mutex_);
    std::vector<ModInfo> infos;
    for (const ModInfo& m : knownInfos()) {
        infos.push_back(m);
    }
    UserConfig cfg = userConfig_;
    if (!useCustom) {
        cfg.order.clear();
    }
    std::vector<std::string> out;
    for (const auto& m : planLoad(infos, cfg).ordered) {
        out.push_back(m.id);
    }
    return out;
}

UserConfig Runtime::userConfig() {
    std::lock_guard lock(mutex_);
    return userConfig_;
}

static ModView makeView(const ModInfo& info, ModState state, const std::string& kind, const std::string& reason, int loadCount) {
    ModView v;
    v.info = info;
    v.state = state;
    v.kind = kind;
    v.reason = reason;
    v.loadCount = loadCount;
    return v;
}

std::vector<ModView> Runtime::modViews() {
    std::lock_guard lock(mutex_);
    std::vector<ModView> out;
    for (const auto& [id, rec] : records_) {
        ModView v = makeView(rec->info, rec->state, rec->kind, rec->reason, rec->loadCount);
        v.userEnabled = id[0] == '!' || userConfig_.isEnabled(id);
        const auto pos = std::find(order_.begin(), order_.end(), id);
        v.orderIndex = pos == order_.end() ? -1 : static_cast<int>(pos - order_.begin());
        v.neededBy = id[0] == '!' ? std::vector<std::string>{} : neededBy(id);
        v.restartNotes = rec->restartNotes;
        v.content = rec->content;
        v.reloadable = rec->state == ModState::Loaded || rec->state == ModState::Failed || rec->state == ModState::Unloaded;
        if (rec->state == ModState::Disabled) v.reloadNote = "switched off in mods.toml";
        else if (rec->state == ModState::Skipped) v.reloadNote = rec->reason;
        else if (id[0] == '!') v.reloadNote = "the manifest is invalid";
        else if (rec->info.dataOverlay) v.reloadNote = "data_overlay is mounted at start-up only";
        out.push_back(std::move(v));
    }
    std::sort(out.begin(), out.end(), [](const ModView& a, const ModView& b) {
        const bool al = a.orderIndex >= 0, bl = b.orderIndex >= 0;
        if (al != bl) return al;
        if (al) return a.orderIndex < b.orderIndex;
        return a.info.id < b.info.id;
    });
    return out;
}

std::optional<ModView> Runtime::modView(const std::string& id) {
    for (ModView& v : modViews()) {
        if (v.info.id == id) {
            return v;
        }
    }
    return std::nullopt;
}

std::optional<ReloadReport> Runtime::lastReport(const std::string& id) {
    std::lock_guard lock(mutex_);
    const Record* rec = findRecord(id);
    return rec != nullptr ? rec->last : std::nullopt;
}

void Runtime::noteContent(const std::string& mod, const std::string& kind, const std::string& name) {
    std::lock_guard lock(mutex_);
    if (Record* rec = findRecord(mod); rec != nullptr && rec->live) {
        auto& list = rec->content[kind];
        if (std::find(list.begin(), list.end(), name) == list.end() && list.size() < 500) {
            list.push_back(name);
        }
    }
}

void Runtime::noteRestart(const std::string& mod, const std::string& what) {
    std::lock_guard lock(mutex_);
    if (Record* rec = findRecord(mod); rec != nullptr && std::find(rec->restartNotes.begin(), rec->restartNotes.end(), what) == rec->restartNotes.end()) {
        rec->restartNotes.push_back(what);
        spdlog::warn("mods: [{}] needs a restart: {}", mod, what);
    }
}

// ---- lifecycle bookkeeping ----------------------------------------------------------------------------------------------------------------

void Runtime::noteSessionStart(const std::string& mode) {
    std::lock_guard lock(mutex_);
    sessionActive_ = true;
    sessionMode_ = mode;
    worldReady_ = false;
}
void Runtime::noteSessionStop() {
    std::lock_guard lock(mutex_);
    sessionActive_ = false;
    worldReady_ = false;
}
void Runtime::noteWorldReady(const std::string& mode) {
    std::lock_guard lock(mutex_);
    worldReady_ = true;
    worldMode_ = mode;
}
void Runtime::noteWorldNotReady() {
    std::lock_guard lock(mutex_);
    worldReady_ = false;
}

// ---- reload / unload / load ---------------------------------------------------------------------------------------------------------------

bool Runtime::activeIn(const std::vector<std::string>& ids) {
    for (const std::string& id : ids) {
        if (active_.contains(id)) {
            return true;
        }
    }
    return false;
}

ReloadReport Runtime::reload(const std::string& id, bool now) {
    std::lock_guard lock(mutex_);
    ReloadReport report;
    report.target = id;
    Record* rec = state_ ? findRecord(id) : nullptr;
    if (rec == nullptr || id.empty() || id[0] == '!') {
        report.ok = false;
        report.error = "no mod '" + id.substr(0, 60) + "'";
        return report;
    }
    if (rec->state == ModState::Disabled || rec->state == ModState::Skipped) {
        report.ok = false;
        report.error = "the mod is " + std::string(modStateName(rec->state)) + ": " + rec->reason;
        return report;
    }
    std::vector<std::string> affected = dependentsOf(knownInfos(), id);
    affected.insert(affected.begin(), id);
    if ((!now && sessionActive_) || activeIn(affected)) {
        if (std::find(pendingReloads_.begin(), pendingReloads_.end(), id) == pendingReloads_.end()) {
            pendingReloads_.push_back(id);
        }
        report.queued = true;
        return report;
    }
    report = doReload(id);
    rec->last = report;
    return report;
}

ReloadReport Runtime::doReload(const std::string& id) {
    ReloadReport report;
    report.target = id;
    Record* target = findRecord(id);
    const auto refuse = [&](const std::string& why) {
        report.ok = false;
        report.error = why;
        spdlog::warn("mods: reload of '{}' refused: {}", id, why);
        return report;
    };
    if (target == nullptr) {
        return refuse("no such mod");
    }
    // everything that can be checked before the old instance is touched
    std::string error;
    std::optional<ModInfo> fresh = readManifest(target->info.dir, error);
    if (!fresh) {
        return refuse("mod.toml: " + error);
    }
    if (fresh->id != id) {
        return refuse("mod.toml now says id '" + fresh->id + "': a mod cannot change its id in a reload");
    }
    if (const std::string why = syntaxProblem(fresh->dir / fresh->main); !why.empty()) {
        return refuse(why);
    }
    for (const std::string& r : fresh->requires_) {
        const Record* dep = findRecord(r);
        if (dep == nullptr || dep->state != ModState::Loaded) {
            return refuse("it now requires '" + r + "' which is not loaded");
        }
    }

    std::vector<std::string> dependents;
    for (const std::string& d : dependentsOf(knownInfos(), id)) {
        const Record* r = findRecord(d);
        if (r != nullptr && r->state == ModState::Loaded) {
            dependents.push_back(d);
        }
    }

    // teardown: dependents first (newest first), then the target
    std::map<std::string, ModAction> torn;
    for (auto it = dependents.rbegin(); it != dependents.rend(); ++it) {
        torn[*it] = tearDown(*findRecord(*it), "reload");
    }
    torn[id] = tearDown(*target, "reload");
    target->info = *fresh;

    // run again: the target, then the dependents in load order
    const auto finish = [&](const std::string& mod, bool dependent) {
        Record& rec = *findRecord(mod);
        ModAction a = loadOne(rec, /*replay=*/true);
        const ModAction& old = torn[mod];
        a.handlersRemoved = old.handlersRemoved;
        a.released = old.released;
        a.versionBefore = old.versionBefore;
        if (dependent && a.action == "reloaded") {
            a.action = "reloaded";
        }
        report.actions.push_back(std::move(a));
        Record& r2 = *findRecord(mod);
        r2.last = report;
    };
    finish(id, false);
    const bool targetOk = findRecord(id)->state == ModState::Loaded;
    for (const std::string& d : dependents) {
        if (!targetOk) {
            Record& rec = *findRecord(d);
            rec.state = ModState::Skipped;
            rec.kind = "dependency_unavailable";
            rec.reason = "requires '" + id + "' which failed to reload";
            ModAction a;
            a.id = d;
            a.action = "skipped";
            a.error = rec.reason;
            a.handlersRemoved = torn[d].handlersRemoved;
            a.released = torn[d].released;
            report.actions.push_back(std::move(a));
            continue;
        }
        finish(d, true);
    }
    report.ok = targetOk;
    if (!targetOk) {
        report.error = findRecord(id)->reason;
    }
    rebuildOrder();
    for (const ModAction& a : report.actions) {
        spdlog::info("mods: reload '{}': {} {} ({} handler(s) removed, {} added, {} registration(s) released{})", id, a.id, a.action, a.handlersRemoved, a.handlersAdded,
                     a.released.size(), a.error.empty() ? "" : "; " + a.error);
    }
    return report;
}

ReloadReport Runtime::unload(const std::string& id, bool cascade) {
    std::lock_guard lock(mutex_);
    ReloadReport report;
    report.target = id;
    Record* rec = state_ ? findRecord(id) : nullptr;
    if (rec == nullptr || id.empty() || id[0] == '!') {
        report.ok = false;
        report.error = "no mod '" + id.substr(0, 60) + "'";
        return report;
    }
    if (rec->state != ModState::Loaded && rec->state != ModState::Failed) {
        report.ok = false;
        report.error = "the mod is not loaded";
        return report;
    }
    std::vector<std::string> dependents;
    for (const std::string& d : dependentsOf(knownInfos(), id)) {
        const Record* r = findRecord(d);
        if (r != nullptr && r->state == ModState::Loaded) {
            dependents.push_back(d);
        }
    }
    if (!dependents.empty() && !cascade) {
        std::string list;
        for (const auto& d : dependents) {
            list += (list.empty() ? "" : ", ") + d;
        }
        report.ok = false;
        report.error = "required by loaded mod(s): " + list + " (unload them too with cascade)";
        return report;
    }
    if (activeIn(dependents) || active_.contains(id)) {
        report.ok = false;
        report.error = "a handler of the mod is running; try again from another mod or next frame";
        return report;
    }
    const auto drop = [&](const std::string& mod) {
        Record& r = *findRecord(mod);
        ModAction a = tearDown(r, "unload");
        a.action = "unloaded";
        r.state = ModState::Unloaded;
        r.kind = "unloaded";
        r.reason = "unloaded at run time";
        report.actions.push_back(std::move(a));
    };
    for (auto it = dependents.rbegin(); it != dependents.rend(); ++it) {
        drop(*it);
    }
    drop(id);
    rebuildOrder();
    rec->last = report;
    return report;
}

ReloadReport Runtime::load(const std::string& id) {
    std::lock_guard lock(mutex_);
    ReloadReport report;
    report.target = id;
    Record* rec = state_ ? findRecord(id) : nullptr;
    const auto refuse = [&](const std::string& why) {
        report.ok = false;
        report.error = why;
        return report;
    };
    if (rec == nullptr || id.empty() || id[0] == '!') {
        return refuse("no mod '" + id.substr(0, 60) + "'");
    }
    if (rec->state == ModState::Loaded) {
        return refuse("the mod is already loaded");
    }
    if (rec->state == ModState::Skipped && rec->kind != "dependency_unavailable") {
        return refuse(rec->reason);
    }
    std::string error;
    std::optional<ModInfo> fresh = readManifest(rec->info.dir, error);
    if (!fresh) {
        return refuse("mod.toml: " + error);
    }
    for (const std::string& r : fresh->requires_) {
        const Record* dep = findRecord(r);
        if (dep == nullptr || dep->state != ModState::Loaded) {
            return refuse("requires '" + r + "' which is not loaded");
        }
    }
    if (const std::string why = syntaxProblem(fresh->dir / fresh->main); !why.empty()) {
        return refuse(why);
    }
    rec->info = *fresh;
    ModAction a = loadOne(*rec, /*replay=*/true);
    report.ok = rec->state == ModState::Loaded;
    report.error = report.ok ? "" : rec->reason;
    report.actions.push_back(std::move(a));
    rebuildOrder();
    rec->last = report;
    return report;
}

ReloadReport Runtime::setEnabled(const std::string& id, bool enabled, bool cascade, bool applyNow) {
    std::lock_guard lock(mutex_);
    ReloadReport report;
    report.target = id;
    Record* rec = state_ ? findRecord(id) : nullptr;
    const auto refuse = [&](const std::string& why) {
        report.ok = false;
        report.error = why;
        return report;
    };
    if (rec == nullptr || id.empty() || id[0] == '!') {
        return refuse("no mod '" + id.substr(0, 60) + "'");
    }
    UserConfig cfg = userConfig_;
    std::vector<std::string> changed;
    if (!enabled) {
        std::vector<std::string> needing;
        for (const std::string& d : dependentsOf(knownInfos(), id)) {
            if (cfg.isEnabled(d)) {
                needing.push_back(d);
            }
        }
        if (!needing.empty() && !cascade) {
            std::string list;
            for (const auto& d : needing) {
                list += (list.empty() ? "" : ", ") + d;
            }
            return refuse("required by " + list + ": disable those too (cascade) or keep it");
        }
        for (const auto& d : needing) {
            cfg.enabled[d] = false;
            changed.push_back(d);
        }
        cfg.enabled[id] = false;
        changed.push_back(id);
    } else {
        // everything it requires must be switched on too
        std::vector<std::string> wanted;
        std::set<std::string> seen;
        std::vector<std::string> stack{id};
        while (!stack.empty()) {
            const std::string cur = stack.back();
            stack.pop_back();
            if (!seen.insert(cur).second) {
                continue;
            }
            const Record* r = findRecord(cur);
            if (r == nullptr) {
                return refuse("'" + cur + "' is not installed");
            }
            if (!cfg.isEnabled(cur)) {
                wanted.push_back(cur);
            }
            for (const std::string& req : r->info.requires_) {
                stack.push_back(req);
            }
        }
        if (wanted.size() > 1 && !cascade) {
            std::string list;
            for (const auto& d : wanted) {
                if (d != id) {
                    list += (list.empty() ? "" : ", ") + d;
                }
            }
            return refuse("it requires " + list + " which are switched off: enable those too (cascade)");
        }
        for (const auto& w : wanted) {
            cfg.enabled[w] = true;
            changed.push_back(w);
        }
        if (wanted.empty()) {
            cfg.enabled[id] = true;
        }
    }
    std::string error;
    if (!userConfigFile_.empty() && !saveUserConfig(userConfigFile_, cfg, error)) {
        return refuse("cannot write mods.toml: " + error);
    }
    userConfig_ = cfg;
    if (applyNow) {
        if (!enabled) {
            // dependents first (newest first), then the mod; every one of them ends up Disabled
            std::vector<std::string> loaded;
            for (const auto& m : changed) {
                const Record* r = findRecord(m);
                if (r != nullptr && r->state == ModState::Loaded) {
                    loaded.push_back(m);
                }
            }
            if (activeIn(loaded)) {
                report.error = "saved, but a handler of the mod is running: it stays loaded until the next start";
            } else {
                std::sort(loaded.begin(), loaded.end(), [&](const std::string& a, const std::string& b) {
                    return std::find(order_.begin(), order_.end(), a) > std::find(order_.begin(), order_.end(), b);
                });
                for (const auto& m : loaded) {
                    ModAction a = tearDown(*findRecord(m), "disable");
                    a.action = "unloaded";
                    report.actions.push_back(std::move(a));
                }
                for (const auto& m : changed) {
                    Record& r = *findRecord(m);
                    r.state = ModState::Disabled;
                    r.kind = "disabled";
                    r.reason = "switched off in mods.toml";
                }
            }
        } else {
            // load what was switched on, requirements first
            const LoadPlan plan = planLoad(knownInfos(), userConfig_);
            for (const ModInfo& m : plan.ordered) {
                Record& r = *findRecord(m.id);
                if (r.state == ModState::Disabled || (r.state == ModState::Skipped && r.kind == "dependency_unavailable")) {
                    std::string err;
                    if (auto fresh = readManifest(r.info.dir, err)) {
                        r.info = *fresh;
                    }
                    report.actions.push_back(loadOne(r, /*replay=*/true));
                }
            }
        }
        rebuildOrder();
    }
    rec->last = report;
    return report;
}

std::string Runtime::setOrder(const std::vector<std::string>& order) {
    std::lock_guard lock(mutex_);
    if (!state_) {
        return "the Lua runtime is not running";
    }
    if (const std::string why = checkOrder(knownInfos(), order); !why.empty()) {
        return why;
    }
    UserConfig cfg = userConfig_;
    cfg.order = order;
    std::string error;
    if (!userConfigFile_.empty() && !saveUserConfig(userConfigFile_, cfg, error)) {
        return "cannot write mods.toml: " + error;
    }
    userConfig_ = cfg;
    return {};
}

// ---- pending requests and the watcher -------------------------------------------------------------------------------------------------------

void Runtime::setWatch(bool on, double intervalSeconds) {
    std::lock_guard lock(mutex_);
    watch_ = on;
    if (intervalSeconds >= 0.2 && intervalSeconds <= 60) {
        watchInterval_ = intervalSeconds;
    }
    if (on) {
        lastWatch_ = std::chrono::steady_clock::now();
        for (auto& [id, rec] : records_) {
            if (rec->state == ModState::Loaded || rec->state == ModState::Failed) {
                stampMod(*rec);
            }
        }
    }
}

void Runtime::stampMod(Record& rec) {
    rec.stamps.clear();
    std::error_code ec;
    if (rec.info.dir.empty() || !std::filesystem::is_directory(rec.info.dir, ec)) {
        return;
    }
    for (std::filesystem::recursive_directory_iterator it(rec.info.dir, std::filesystem::directory_options::skip_permission_denied, ec), end; it != end && !ec;
         it.increment(ec)) {
        if (it.depth() > 4) {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_directory(ec)) {
            const std::string name = it->path().filename().string();
            if (name == "assets" || name == "generated" || name == "fixtures") {
                it.disable_recursion_pending();
            }
            continue;
        }
        const std::string ext = it->path().extension().string();
        if (ext == ".lua" || ext == ".toml" || ext == ".csv") {
            rec.stamps[it->path().generic_string()] = it->last_write_time(ec);
        }
    }
}

void Runtime::scanWatch(bool) {
    for (auto& [id, rec] : records_) {
        if (id[0] == '!' || (rec->state != ModState::Loaded && rec->state != ModState::Failed)) {
            continue;
        }
        const auto before = rec->stamps;
        stampMod(*rec);
        if (before != rec->stamps && !before.empty()) {
            spdlog::info("mods: [{}] changed on disk: reloading", id);
            if (std::find(pendingReloads_.begin(), pendingReloads_.end(), id) == pendingReloads_.end()) {
                pendingReloads_.push_back(id);
            }
        }
    }
}

void Runtime::processPending() {
    std::unique_lock lock(mutex_);
    if (!state_) {
        return;
    }
    // a request file from outside the game (an editor, a script): `echo my_mod> bin\spass\reload.txt` reloads that mod, `all` every loaded one; looked at once per second
    if (!reloadRequestFile_.empty()) {
        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<double>(now - lastRequestPoll_).count() >= 1.0) {
            lastRequestPoll_ = now;
            std::error_code ec;
            if (std::filesystem::exists(reloadRequestFile_, ec)) {
                std::ifstream in(reloadRequestFile_);
                std::string line;
                std::vector<std::string> wanted;
                while (std::getline(in, line)) {
                    line.erase(0, line.find_first_not_of(" \t\r\n"));
                    line.erase(line.find_last_not_of(" \t\r\n") + 1);
                    if (!line.empty()) {
                        wanted.push_back(line);
                    }
                }
                in.close();
                std::filesystem::remove(reloadRequestFile_, ec);
                std::vector<std::string> targets;
                for (const std::string& w : wanted) {
                    if (w == "all" || w == "*") {
                        targets.insert(targets.end(), order_.begin(), order_.end());
                    } else {
                        targets.push_back(w);
                    }
                }
                for (const std::string& w : targets) {
                    const Record* r = findRecord(w);
                    if (r != nullptr && (r->state == ModState::Loaded || r->state == ModState::Failed || r->state == ModState::Unloaded) &&
                        std::find(pendingReloads_.begin(), pendingReloads_.end(), w) == pendingReloads_.end()) {
                        spdlog::info("mods: reload requested through {}: {}", reloadRequestFile_.filename().string(), w);
                        pendingReloads_.push_back(w);
                    }
                }
            }
        }
    }
    if (watch_) {
        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<double>(now - lastWatch_).count() >= watchInterval_) {
            lastWatch_ = now;
            scanWatch(false);
        }
    }
    if (pendingReloads_.empty()) {
        return;
    }
    std::vector<std::string> todo;
    todo.swap(pendingReloads_);
    for (const std::string& id : todo) {
        Record* rec = findRecord(id);
        if (rec == nullptr) {
            continue;
        }
        if (rec->state == ModState::Disabled || rec->state == ModState::Skipped) {
            continue;
        }
        std::vector<std::string> affected = dependentsOf(knownInfos(), id);
        affected.push_back(id);
        if (activeIn(affected)) {
            pendingReloads_.push_back(id);  // a handler of the mod is on the stack: next frame
            continue;
        }
        ReloadReport r = doReload(id);
        if (Record* again = findRecord(id)) {
            again->last = r;
        }
    }
}

}  // namespace spass::lua
