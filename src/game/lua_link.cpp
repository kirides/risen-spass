#include "game/lua_link.h"

#include <windows.h>

#include <atomic>
#include <fstream>
#include <map>
#include <mutex>

#include <nlohmann/json.hpp>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

#include "common/crash.h"
#include "common/link.h"
#include "common/paths.h"
#include "game/link_inspect_lua.h"
#include "game/lua_http.h"
#include "lua/runtime.h"

// The live link: protocol and built-in commands.
//
//   request   {"id": 7, "cmd": "reload", "args": {"mod": "my_mod"}}
//   reply     {"id": 7, "ok": true, "result": {...}}   |   {"id": 7, "ok": false, "error": "reason"}
//   event     {"event": "log", "data": {"level": "warn", "text": "..."}}   (pushed; no id)
//
// Built-in commands: hello, ping, mods, reload {mod | "all"}, subscribe {log = "off|trace|debug|info|warn|error"}, commands.
// A mod adds commands with risen.link.on("name", fn): the command is "<mod id>.<name>", fn(args) returns result | nil, reason; risen.link.send(event, data) pushes
// "<mod id>.<event>".

namespace spass::game {
namespace {

using nlohmann::json;

constexpr int kProtocol = 1;
constexpr const char* kRegistryKey = "spass.link.handlers";

std::mutex g_mutex;
std::map<std::string, std::string> g_handlers;  // "<mod>.<name>" -> mod id (the function lives in the registry table)
std::atomic<int> g_logLevel{spdlog::level::off};
std::string g_pipe;
std::string g_instance;

std::string dump(const json& j) { return j.dump(-1, ' ', false, json::error_handler_t::replace); }

sol::table handlerTable(sol::state_view L) {
    sol::table reg = L.registry();
    sol::object o = reg[kRegistryKey];
    if (o.is<sol::table>()) {
        return o.as<sol::table>();
    }
    sol::table t = L.create_table();
    reg[kRegistryKey] = t;
    return t;
}

// Forwards the log lines at or above the subscribed level to the client.
class LinkSink : public spdlog::sinks::base_sink<std::mutex> {
protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        if (msg.level < g_logLevel.load() || !link::connected()) {
            return;
        }
        json j;
        j["event"] = "log";
        j["data"] = {{"level", std::string(spdlog::level::to_string_view(msg.level).data(), spdlog::level::to_string_view(msg.level).size())},
                     {"text", std::string(msg.payload.data(), msg.payload.size())}};
        link::send(dump(j));
    }
    void flush_() override {}
};

void sendEvent(const std::string& name, const json& data) {
    json j;
    j["event"] = name;
    j["data"] = data;
    link::send(dump(j));
}

json actionJson(const lua::ModAction& a) {
    json j;
    j["id"] = a.id;
    j["action"] = a.action;
    if (!a.error.empty()) j["error"] = a.error;
    j["version_before"] = a.versionBefore;
    j["version_after"] = a.versionAfter;
    j["handlers_removed"] = a.handlersRemoved;
    j["handlers_added"] = a.handlersAdded;
    j["released"] = a.released;
    j["restart_required"] = a.restart;
    return j;
}

json reportJson(const lua::ReloadReport& r) {
    json j;
    j["target"] = r.target;
    j["ok"] = r.ok;
    if (!r.error.empty()) j["error"] = r.error;
    json actions = json::array();
    for (const auto& a : r.actions) actions.push_back(actionJson(a));
    j["actions"] = std::move(actions);
    return j;
}

json modJson(const lua::ModView& v) {
    json j;
    j["id"] = v.info.id;
    j["name"] = v.info.name;
    j["version"] = v.info.version;
    j["state"] = lua::modStateName(v.state);
    j["enabled"] = v.userEnabled;
    if (!v.kind.empty()) j["kind"] = v.kind;
    if (!v.reason.empty()) j["reason"] = v.reason;
    j["reloadable"] = v.reloadable;
    j["load_count"] = v.loadCount;
    j["network"] = {{"declared", v.info.network}, {"hosts", v.info.networkHosts}};
    return j;
}

json reply(const json& id, bool ok) {
    json j;
    if (!id.is_null()) j["id"] = id;
    j["ok"] = ok;
    return j;
}

json fail(const json& id, const std::string& why) {
    json j = reply(id, false);
    j["error"] = why;
    return j;
}

// Calls a Lua command function with the request's args and turns its result (value | nil, reason) into a reply.
json runLua(const json& id, sol::state_view L, sol::object fnObj, const json& args, const std::string& cmd, const std::string& mod) {
    std::string error;
    auto luaArgs = jsonDecode(L, dump(args), error);
    if (!luaArgs) {
        return fail(id, "bad args: " + error);
    }
    crash::Scope trail("link command", cmd);
    sol::protected_function fn = fnObj.as<sol::protected_function>();
    sol::protected_function_result res = fn(*luaArgs);
    if (!res.valid()) {
        const std::string what = res.get<sol::error>().what();
        spdlog::error("lua: [{}] link command '{}' failed: {}", mod, cmd, what);
        return fail(id, what);
    }
    sol::object result = res.get_type(0) == sol::type::none ? sol::object{} : res.get<sol::object>(0);
    sol::object err = res.return_count() > 1 ? res.get<sol::object>(1) : sol::object{};
    if (err.valid() && err.get_type() == sol::type::string) {
        return fail(id, err.as<std::string>());
    }
    json j = reply(id, true);
    if (result.valid() && result.get_type() != sol::type::lua_nil) {
        auto text = jsonEncode(result, false, error);
        if (!text) {
            return fail(id, "the result cannot be encoded: " + error);
        }
        j["result"] = json::parse(*text, nullptr, false);
    }
    return j;
}

// The table of inspection functions (the chunk of link_inspect_lua.h run once per Lua state with the shared `risen` table).
sol::table inspectTable(sol::state_view L) {
    sol::table reg = L.registry();
    sol::object o = reg["spass.link.inspect"];
    if (o.is<sol::table>()) {
        return o.as<sol::table>();
    }
    sol::load_result loaded = L.load(kLinkInspectLua, "=link.inspect");
    sol::table t = L.create_table();
    if (loaded.valid()) {
        sol::protected_function fn = loaded;
        sol::protected_function_result r = fn(lua::Runtime::instance().apiTable());
        if (r.valid() && r.get_type() == sol::type::table) {
            t = r.get<sol::table>();
        } else if (!r.valid()) {
            spdlog::error("link: the inspection chunk failed: {}", r.get<sol::error>().what());
        }
    } else {
        spdlog::error("link: the inspection chunk does not load: {}", loaded.get<sol::error>().what());
    }
    reg["spass.link.inspect"] = t;
    return t;
}

json handle(const json& msg) {
    const json id = msg.contains("id") ? msg["id"] : json();
    if (!msg.is_object() || !msg.contains("cmd") || !msg["cmd"].is_string()) {
        return fail(id, "a request is an object with a string `cmd`");
    }
    const std::string cmd = msg["cmd"].get<std::string>();
    const json args = msg.contains("args") && msg["args"].is_object() ? msg["args"] : json::object();
    auto& rt = lua::Runtime::instance();
    if (!rt.running()) {
        return fail(id, "the Lua runtime is not running");
    }

    if (cmd == "ping") {
        json j = reply(id, true);
        j["result"] = {{"pong", true}};
        return j;
    }
    if (cmd == "hello") {
        json j = reply(id, true);
        j["result"] = {{"protocol", kProtocol},
                       {"layer", SPASS_VERSION},
                       {"pid", static_cast<int>(GetCurrentProcessId())},
                       {"pipe", g_pipe},
                       {"instance", g_instance},
                       {"session", rt.sessionMode().value_or("")},
                       {"world", rt.worldMode().value_or("")},
                       {"mods", static_cast<int>(rt.modViews().size())}};
        return j;
    }
    if (cmd == "mods") {
        json list = json::array();
        for (const auto& v : rt.modViews()) list.push_back(modJson(v));
        json j = reply(id, true);
        j["result"] = {{"mods", std::move(list)}, {"order", rt.loadOrder()}};
        return j;
    }
    if (cmd == "reload") {
        if (!args.contains("mod") || !args["mod"].is_string()) {
            return fail(id, "reload needs args.mod (a mod id or \"all\")");
        }
        const std::string target = args["mod"].get<std::string>();
        std::vector<std::string> ids;
        if (target == "all") {
            ids = rt.loadOrder();
        } else {
            ids.push_back(target);
        }
        json reports = json::array();
        bool allOk = true;
        for (const std::string& m : ids) {
            const lua::ReloadReport r = rt.reload(m, true);
            allOk = allOk && r.ok;
            reports.push_back(reportJson(r));
        }
        json j = reply(id, allOk);
        j["result"] = {{"reports", std::move(reports)}};
        if (!allOk) j["error"] = "a reload failed (see the reports)";
        return j;
    }
    if (cmd == "subscribe") {
        if (args.contains("log")) {
            if (!args["log"].is_string()) return fail(id, "args.log must be a level name");
            const std::string lvl = args["log"].get<std::string>();
            if (lvl == "off") {
                g_logLevel = spdlog::level::off;
            } else if (lvl == "trace" || lvl == "debug" || lvl == "info" || lvl == "warn" || lvl == "error") {
                g_logLevel = spdlog::level::from_str(lvl);
            } else {
                return fail(id, "unknown level '" + lvl.substr(0, 20) + "' (off, trace, debug, info, warn, error)");
            }
        }
        json j = reply(id, true);
        j["result"] = {{"log", spdlog::level::to_string_view(static_cast<spdlog::level::level_enum>(g_logLevel.load())).data()}};
        return j;
    }
    if (cmd == "commands") {
        json list = json::array({"hello", "ping", "mods", "reload", "subscribe", "commands", "inspect.state", "inspect.nearby", "inspect.actors", "inspect.entity", "inspect.props", "inspect.inventory", "inspect.quests", "inspect.gamevars", "edit.move"});
        std::lock_guard lock(g_mutex);
        for (const auto& [name, mod] : g_handlers) list.push_back(name);
        json j = reply(id, true);
        j["result"] = {{"commands", std::move(list)}};
        return j;
    }

    // the built-in read-only inspection commands (link_inspect_lua.h)
    if (cmd.rfind("inspect.", 0) == 0) {
        auto lock = rt.lock();
        sol::state_view L = rt.lua();
        sol::table tbl = inspectTable(L);
        sol::object fnObj = tbl[cmd.substr(8)];
        if (fnObj.get_type() != sol::type::function) {
            return fail(id, "unknown command '" + cmd.substr(0, 60) + "'");
        }
        return runLua(id, L, fnObj, args, cmd, "link");
    }

    // the editor's write commands (link_inspect_lua.h `M.edit`)
    if (cmd.rfind("edit.", 0) == 0) {
        auto lock = rt.lock();
        sol::state_view L = rt.lua();
        sol::table tbl = inspectTable(L);
        sol::object editObj = tbl["edit"];
        sol::object fnObj = editObj.is<sol::table>() ? editObj.as<sol::table>()[cmd.substr(5)] : sol::object();
        if (!fnObj.valid() || fnObj.get_type() != sol::type::function) {
            return fail(id, "unknown command '" + cmd.substr(0, 60) + "'");
        }
        return runLua(id, L, fnObj, args, cmd, "link");
    }

    // a command of a mod
    std::string mod;
    {
        std::lock_guard lock(g_mutex);
        const auto it = g_handlers.find(cmd);
        if (it == g_handlers.end()) {
            return fail(id, "unknown command '" + cmd.substr(0, 60) + "'");
        }
        mod = it->second;
    }
    auto lock = rt.lock();
    sol::state_view L = rt.lua();
    sol::object fnObj = handlerTable(L)[cmd];
    if (fnObj.get_type() != sol::type::function) {
        return fail(id, "the command '" + cmd + "' is gone");
    }
    return runLua(id, L, fnObj, args, cmd, mod);
}

bool validName(const std::string& s) {
    if (s.empty() || s.size() > 40) return false;
    for (char c : s) {
        if (!(std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '_' || c == '.')) return false;
    }
    return true;
}

}  // namespace

void linkInit(bool enabled, const std::string& pipeName, const std::string& instance) {
    if (!enabled) {
        return;
    }
    g_instance = instance;
    const std::string name = pipeName.empty() ? "risen-spass-" + std::to_string(GetCurrentProcessId()) : pipeName;
    std::string error;
    if (!link::start(name, error)) {
        spdlog::warn("link: not started: {}", error);
        return;
    }
    g_pipe = link::pipePath();
    static std::once_flag sinkOnce;
    std::call_once(sinkOnce, [] {
        auto logger = spdlog::default_logger();
        if (logger) {
            logger->sinks().push_back(std::make_shared<LinkSink>());
        }
    });
    try {
        json j = {{"pipe", g_pipe}, {"pid", static_cast<int>(GetCurrentProcessId())}, {"protocol", kProtocol}, {"layer", SPASS_VERSION}, {"instance", instance}};
        std::ofstream(paths::dataDir() / "link.json", std::ios::binary | std::ios::trunc) << j.dump(2);
    } catch (...) {
    }
    spdlog::info("link: listening on {} (named pipe, this user only)", g_pipe);
}

void linkShutdown() {
    if (!link::running()) {
        return;
    }
    link::stop();
    std::error_code ec;
    std::filesystem::remove(paths::dataDir() / "link.json", ec);
}

void linkPump() {
    if (!link::running()) {
        return;
    }
    link::Inbox in;
    link::poll(in);
    if (in.messages.empty() && in.connects == 0 && in.disconnects == 0) {
        return;
    }
    auto& rt = lua::Runtime::instance();
    if (in.connects > 0 && rt.running()) {
        rt.emit("link.connected");
    }
    for (const std::string& m : in.messages) {
        json msg = json::parse(m, nullptr, false);
        json answer;
        if (msg.is_discarded()) {
            answer = fail(json(), "the message is not valid JSON");
        } else {
            try {
                answer = handle(msg);
            } catch (const std::exception& e) {
                answer = fail(msg.is_object() && msg.contains("id") ? msg["id"] : json(), std::string("internal error: ") + e.what());
            }
        }
        const std::string text = dump(answer);
        const bool sent = link::send(text);
        spdlog::debug("link: {} -> {} bytes, sent {}", m.substr(0, 80), text.size(), sent);
    }
    if (in.disconnects > 0) {
        g_logLevel = spdlog::level::off;
        if (rt.running()) {
            rt.emit("link.disconnected");
        }
    }
}

void bindLinkModApi(sol::state_view lua, sol::table api, const lua::ModInfo& mod) {
    sol::table linkApi = lua.create_table();
    const std::string id = mod.id;
    // risen.link.available() -> bool: the live link is on (config.toml [link] enabled)
    linkApi.set_function("available", [] { return link::running(); });
    // risen.link.connected() -> bool: an editor is connected right now
    linkApi.set_function("connected", [] { return link::connected(); });
    // risen.link.on(command, handler(args) -> result | nil, reason) -> true | nil, reason
    linkApi.set_function("on", [id](sol::this_state ts, sol::object name, sol::object handler) -> std::tuple<sol::object, sol::object> {
        sol::state_view L(ts);
        const auto bad = [&](const std::string& why) { return std::tuple<sol::object, sol::object>{sol::make_object(L, sol::lua_nil), sol::make_object(L, why)}; };
        if (name.get_type() != sol::type::string || !validName(name.as<std::string>())) {
            return bad("the command name must be a string of a-z 0-9 _ . (40 characters at most)");
        }
        if (handler.get_type() != sol::type::function) {
            return bad("the handler must be a function");
        }
        const std::string cmd = id + "." + name.as<std::string>();
        handlerTable(L)[cmd] = handler;
        std::lock_guard lock(g_mutex);
        g_handlers[cmd] = id;
        return {sol::make_object(L, true), sol::make_object(L, sol::lua_nil)};
    });
    // risen.link.send(event, data) -> bool: pushes "<mod id>.<event>" to the connected editor (false when none is connected)
    linkApi.set_function("send", [id](sol::object name, sol::object data) -> bool {
        if (name.get_type() != sol::type::string || !validName(name.as<std::string>()) || !link::connected()) {
            return false;
        }
        json payload;
        if (data.valid() && data.get_type() != sol::type::lua_nil) {
            std::string error;
            auto text = jsonEncode(data, false, error);
            if (!text) {
                return false;
            }
            payload = json::parse(*text, nullptr, false);
        }
        sendEvent(id + "." + name.as<std::string>(), payload);
        return true;
    });
    api["link"] = linkApi;
}

void linkReleaseOwner(const std::string& mod, std::vector<std::string>& released) {
    std::vector<std::string> gone;
    {
        std::lock_guard lock(g_mutex);
        for (auto it = g_handlers.begin(); it != g_handlers.end();) {
            if (it->second == mod) {
                gone.push_back(it->first);
                it = g_handlers.erase(it);
            } else {
                ++it;
            }
        }
    }
    if (gone.empty()) {
        return;
    }
    auto& rt = lua::Runtime::instance();
    if (rt.running()) {
        sol::state_view L = rt.lua();
        sol::table handlers = handlerTable(L);
        for (const std::string& cmd : gone) {
            handlers[cmd] = sol::lua_nil;
        }
    }
    released.push_back(std::to_string(gone.size()) + " link command(s)");
}

}  // namespace spass::game
