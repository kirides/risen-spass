#include "game/lua_http.h"

#include <chrono>
#include <cmath>
#include <map>
#include <mutex>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "common/crash.h"
#include "common/httpclient/httpclient.h"
#include "lua/runtime.h"

// risen.json and risen.http (docs/LUA_API.md "Network access").
//
//   risen.http.get_text(url [, opts], callback)        callback(text, err, info)
//   risen.http.get_json(url [, opts], callback)        callback(table, err, info)
//   risen.http.post_json(url, data [, opts], callback) callback(table|true, err, info)
//   risen.http.request(opts, callback)                 the general form: opts.url, opts.method, opts.headers, opts.body | opts.json, opts.as
//   risen.http.cancel(id), risen.http.available()
//
// The request runs on a worker thread; the callback runs on the game thread in a later frame (httpPump). The mod needs `network` in mod.toml
// and the player must not have switched network access off (mods.toml / the mod manager).

namespace spass::game {
namespace {

using nlohmann::json;
using Two = std::tuple<sol::object, sol::object>;

constexpr int kMaxDepth = 64;
char g_nullTag = 0;  // the address is the `risen.json.null` sentinel

Two fail(sol::state_view L, const std::string& why) { return {sol::make_object(L, sol::lua_nil), sol::make_object(L, why)}; }

// ---- JSON <-> Lua -------------------------------------------------------------------------------------------------------------------

bool toLua(sol::state_view L, const json& j, int depth, sol::object& out, std::string& error, bool inArray) {
    if (depth > kMaxDepth) {
        error = "nested deeper than 64 levels";
        return false;
    }
    switch (j.type()) {
        case json::value_t::null:
            out = inArray ? sol::make_object(L, sol::lightuserdata_value(&g_nullTag)) : sol::object{};
            return true;
        case json::value_t::boolean: out = sol::make_object(L, j.get<bool>()); return true;
        case json::value_t::number_integer:
        case json::value_t::number_unsigned:
        case json::value_t::number_float: out = sol::make_object(L, j.get<double>()); return true;
        case json::value_t::string: out = sol::make_object(L, j.get_ref<const std::string&>()); return true;
        case json::value_t::array: {
            sol::table t = L.create_table(static_cast<int>(j.size()), 0);
            int i = 1;
            for (const json& el : j) {
                sol::object v;
                if (!toLua(L, el, depth + 1, v, error, true)) {
                    return false;
                }
                t[i++] = v;
            }
            out = t;
            return true;
        }
        case json::value_t::object: {
            sol::table t = L.create_table(0, static_cast<int>(j.size()));
            for (auto it = j.begin(); it != j.end(); ++it) {
                if (it->is_null()) {
                    continue;
                }
                sol::object v;
                if (!toLua(L, *it, depth + 1, v, error, false)) {
                    return false;
                }
                t[it.key()] = v;
            }
            out = t;
            return true;
        }
        default: error = "unsupported JSON value"; return false;
    }
}

bool numberKey(const sol::object& k, double& v) {
    if (k.get_type() != sol::type::number) {
        return false;
    }
    v = k.as<double>();
    return true;
}

bool fromLua(const sol::object& o, int depth, json& out, std::string& error) {
    if (depth > kMaxDepth) {
        error = "nested deeper than 64 levels (a table that contains itself?)";
        return false;
    }
    switch (o.get_type()) {
        case sol::type::lua_nil:
        case sol::type::none: out = nullptr; return true;
        case sol::type::boolean: out = o.as<bool>(); return true;
        case sol::type::string: out = o.as<std::string>(); return true;
        case sol::type::number: {
            const double d = o.as<double>();
            if (!std::isfinite(d)) {
                error = "NaN and infinity cannot be encoded";
                return false;
            }
            if (d == std::floor(d) && std::fabs(d) < 9007199254740992.0) {
                out = static_cast<std::int64_t>(d);
            } else {
                out = d;
            }
            return true;
        }
        case sol::type::lightuserdata:
            if (o.as<void*>() == &g_nullTag) {
                out = nullptr;
                return true;
            }
            error = "cannot encode a userdata value";
            return false;
        case sol::type::table: {
            const sol::table t = o.as<sol::table>();
            std::size_t n = 0;
            double maxIndex = 0;
            bool arrayLike = true;
            for (const auto& kv : t) {
                ++n;
                double k = 0;
                if (!numberKey(kv.first, k) || k < 1 || k != std::floor(k)) {
                    arrayLike = false;
                } else {
                    maxIndex = std::max(maxIndex, k);
                }
            }
            if (arrayLike && static_cast<double>(n) == maxIndex) {
                out = json::array();
                for (std::size_t i = 1; i <= n; ++i) {
                    json el;
                    sol::object v = t[i];
                    if (!fromLua(v, depth + 1, el, error)) {
                        return false;
                    }
                    out.push_back(std::move(el));
                }
                return true;
            }
            out = json::object();
            for (const auto& kv : t) {
                std::string key;
                if (kv.first.get_type() == sol::type::string) {
                    key = kv.first.as<std::string>();
                } else if (kv.first.get_type() == sol::type::number) {
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "%.14g", kv.first.as<double>());
                    key = buf;
                } else {
                    error = "table keys must be strings or numbers";
                    return false;
                }
                json el;
                if (!fromLua(kv.second, depth + 1, el, error)) {
                    return false;
                }
                out[key] = std::move(el);
            }
            return true;
        }
        default: error = std::string("cannot encode a ") + sol::type_name(o.lua_state(), o.get_type()) + " value"; return false;
    }
}

}  // namespace

std::optional<sol::object> jsonDecode(sol::state_view lua, const std::string& text, std::string& error) {
    const json j = json::parse(text, nullptr, false);
    if (j.is_discarded()) {
        // run again with exceptions only to learn the position and reason
        try {
            (void)json::parse(text);
        } catch (const std::exception& e) {
            error = e.what();
            return std::nullopt;
        }
        error = "invalid JSON";
        return std::nullopt;
    }
    sol::object out;
    if (!toLua(lua, j, 0, out, error, false)) {
        return std::nullopt;
    }
    return out;
}

std::optional<std::string> jsonEncode(const sol::object& value, bool pretty, std::string& error) {
    json j;
    if (!fromLua(value, 0, j, error)) {
        return std::nullopt;
    }
    return j.dump(pretty ? 2 : -1, ' ', false, json::error_handler_t::replace);
}

void bindJson(sol::state_view lua, sol::table risen) {
    sol::table t = lua.create_table();
    t["null"] = sol::lightuserdata_value(&g_nullTag);
    // risen.json.decode(text) -> value | nil, reason
    t.set_function("decode", [](sol::this_state ts, sol::object text) -> Two {
        sol::state_view L(ts);
        if (text.get_type() != sol::type::string) {
            return fail(L, "decode: the text must be a string");
        }
        std::string error;
        auto v = jsonDecode(L, text.as<std::string>(), error);
        if (!v) {
            return fail(L, error);
        }
        return {*v, sol::make_object(L, sol::lua_nil)};
    });
    // risen.json.encode(value [, pretty]) -> text | nil, reason
    t.set_function("encode", [](sol::this_state ts, sol::object value, sol::object pretty) -> Two {
        sol::state_view L(ts);
        std::string error;
        const bool p = pretty.get_type() == sol::type::boolean && pretty.as<bool>();
        auto s = jsonEncode(value, p, error);
        if (!s) {
            return fail(L, error);
        }
        return {sol::make_object(L, *s), sol::make_object(L, sol::lua_nil)};
    });
    risen["json"] = t;
}

// ---- risen.http ---------------------------------------------------------------------------------------------------------------------------

namespace {

constexpr const char* kRegistryKey = "spass.http.callbacks";
constexpr std::size_t kMaxPerMod = 16;
constexpr std::size_t kMaxBody = 4u << 20;

struct Pending {
    std::string mod;
    std::string kind;  // "text" | "json"
    bool cancelled = false;
};

std::mutex g_mutex;
std::map<std::uint64_t, Pending> g_pending;
httpc::CompletionQueue g_queue;  // finished requests, handed from WinHTTP's threads to httpPump on the game thread

// One client for the whole layer (WinHTTP async, no thread per request). Never destroyed: its threads may outlive main().
httpc::Client& httpClient() {
    static httpc::Client* client = [] {
        httpc::ClientOptions o;
        o.userAgent = "risen-spass/" SPASS_VERSION;
        o.maxInFlight = 8;
        return httpc::Client::create_winhttp(o).release();
    }();
    return *client;
}

sol::table callbackTable(sol::state_view L) {
    sol::table reg = L.registry();
    sol::object o = reg[kRegistryKey];
    if (o.is<sol::table>()) {
        return o.as<sol::table>();
    }
    sol::table t = L.create_table();
    reg[kRegistryKey] = t;
    return t;
}

// "" when the mod may use the network right now, else why not.
std::string denyReason(const lua::ModInfo& mod) {
    if (!mod.network) {
        return "this mod has no network access: add `network = true` to its mod.toml";
    }
    const lua::UserConfig cfg = lua::Runtime::instance().userConfig();
    if (!cfg.networkEnabled) {
        return "network access is switched off (mod manager > Network)";
    }
    if (!cfg.networkAllowedFor(mod.id)) {
        return "network access is switched off for this mod (mod manager > Network)";
    }
    return {};
}

std::string describeUrl(const httpc::Url& u) {
    const std::string q = u.path.substr(0, u.path.find('?'));  // the query may carry a token: never logged
    return u.host + q;
}

Two startRequest(sol::state_view L, const lua::ModInfo& mod, sol::table opts, sol::object callback, const std::string& defaultMethod, const std::string& defaultKind) {
    if (callback.get_type() != sol::type::function) {
        return fail(L, "the last argument must be a callback function (the request runs in the background)");
    }
    if (const std::string why = denyReason(mod); !why.empty()) {
        return fail(L, why);
    }
    httpc::Request req;
    req.userAgent = "risen-spass/" SPASS_VERSION " (" + mod.id + ")";
    req.allowedHosts = mod.networkHosts;

    const auto str = [&](const char* key, std::string& out) -> std::string {
        sol::object o = opts[key];
        if (o.valid() && o.get_type() != sol::type::lua_nil) {
            if (o.get_type() != sol::type::string) {
                return std::string("option '") + key + "' must be a string";
            }
            out = o.as<std::string>();
        }
        return {};
    };
    std::string url;
    if (std::string e = str("url", url); !e.empty()) return fail(L, e);
    req.method = defaultMethod;
    if (std::string e = str("method", req.method); !e.empty()) return fail(L, e);
    for (char& c : req.method) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    static const char* const kMethods[] = {"GET", "POST", "PUT", "PATCH", "DELETE", "HEAD"};
    if (std::find(std::begin(kMethods), std::end(kMethods), req.method) == std::end(kMethods)) {
        return fail(L, "unsupported method '" + req.method.substr(0, 16) + "' (GET, POST, PUT, PATCH, DELETE, HEAD)");
    }
    std::string kind = defaultKind;
    if (std::string e = str("as", kind); !e.empty()) return fail(L, e);
    if (kind != "text" && kind != "json") {
        return fail(L, "option 'as' must be \"text\" or \"json\"");
    }

    httpc::Url parsed;
    std::string why;
    if (!httpc::parse_url(url, parsed, why)) {
        return fail(L, why);
    }
    if (!httpc::host_allowed(parsed.host, req.allowedHosts)) {
        return fail(L, "host '" + parsed.host + "' is not in this mod's `network` host list");
    }

    sol::object headers = opts["headers"];
    if (headers.valid() && headers.get_type() != sol::type::lua_nil) {
        if (headers.get_type() != sol::type::table) {
            return fail(L, "option 'headers' must be a table { Name = \"value\" }");
        }
        for (const auto& kv : headers.as<sol::table>()) {
            if (kv.first.get_type() != sol::type::string ||
                (kv.second.get_type() != sol::type::string && kv.second.get_type() != sol::type::number)) {
                return fail(L, "option 'headers' must map header names to strings");
            }
            {
                const std::string name = kv.first.as<std::string>();
                std::string lowered = name;
                for (char& c : lowered) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                bool good = !name.empty();
                for (char c : name) good = good && (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_');
                if (!good) {
                    return fail(L, "invalid header name '" + name.substr(0, 40) + "'");
                }
                if (lowered == "host" || lowered == "content-length" || lowered == "connection") {
                    return fail(L, "the header '" + name + "' cannot be set");
                }
                if (kv.second.get_type() == sol::type::string) {
                    const std::string v = kv.second.as<std::string>();
                    if (v.find('\r') != std::string::npos || v.find('\n') != std::string::npos || v.find('\0') != std::string::npos) {
                        return fail(L, "the value of header '" + name + "' contains a line break");
                    }
                }
            }
            std::string value = kv.second.get_type() == sol::type::number ? std::to_string(static_cast<long long>(kv.second.as<double>())) : kv.second.as<std::string>();
            req.headers.emplace_back(kv.first.as<std::string>(), std::move(value));
            if (req.headers.size() > 32) {
                return fail(L, "too many headers (32 at most)");
            }
        }
    }
    sol::object body = opts["body"];
    sol::object jsonBody = opts["json"];
    if (body.valid() && body.get_type() != sol::type::lua_nil) {
        if (body.get_type() != sol::type::string) {
            return fail(L, "option 'body' must be a string (use 'json' for a table)");
        }
        req.body = body.as<std::string>();
    } else if (jsonBody.valid() && jsonBody.get_type() != sol::type::lua_nil) {
        std::string error;
        auto text = jsonEncode(jsonBody, false, error);
        if (!text) {
            return fail(L, "option 'json': " + error);
        }
        req.body = *text;
        const bool hasType = std::any_of(req.headers.begin(), req.headers.end(), [](const auto& h) {
            std::string n = h.first;
            for (char& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return n == "content-type";
        });
        if (!hasType) {
            req.headers.emplace_back("Content-Type", "application/json");
        }
    }
    if (req.body.size() > kMaxBody) {
        return fail(L, "the request body is larger than 4 MB");
    }
    sol::object timeout = opts["timeout"];
    if (timeout.valid() && timeout.get_type() != sol::type::lua_nil) {
        if (timeout.get_type() != sol::type::number || !std::isfinite(timeout.as<double>())) {
            return fail(L, "option 'timeout' must be a number of seconds");
        }
        req.timeoutMs = static_cast<int>(std::clamp(timeout.as<double>(), 1.0, 120.0) * 1000);
    }
    sol::object maxBytes = opts["max_bytes"];
    if (maxBytes.valid() && maxBytes.get_type() != sol::type::lua_nil) {
        if (maxBytes.get_type() != sol::type::number || !std::isfinite(maxBytes.as<double>())) {
            return fail(L, "option 'max_bytes' must be a number");
        }
        req.maxBytes = static_cast<std::size_t>(std::clamp(maxBytes.as<double>(), 1024.0, 32.0 * 1024 * 1024));
    }
    req.url = url;

    std::uint64_t id = 0;
    {
        std::lock_guard lock(g_mutex);
        std::size_t mine = 0;
        for (const auto& [pid, p] : g_pending) {
            if (p.mod == mod.id && !p.cancelled) ++mine;
        }
        if (mine >= kMaxPerMod) {
            return fail(L, "too many requests in flight for this mod (16 at most)");
        }
        id = httpClient().submit(std::move(req), g_queue.sink());
        g_pending[id] = Pending{mod.id, kind, false};
    }
    callbackTable(L)[id] = callback;
    spdlog::info("http: [{}] {} {}", mod.id, opts["method"].get_type() == sol::type::string ? opts["method"].get<std::string>() : defaultMethod, describeUrl(parsed));
    return {sol::make_object(L, static_cast<double>(id)), sol::make_object(L, sol::lua_nil)};
}

// (url [, opts], callback) / (opts, callback) argument shapes.
Two shortcut(sol::this_state ts, const lua::ModInfo& mod, sol::object url, sol::object second, sol::object third, const char* method, const char* kind) {
    sol::state_view L(ts);
    if (url.get_type() != sol::type::string) {
        return fail(L, "the first argument must be a URL string");
    }
    sol::table opts = L.create_table();
    sol::object callback = third;
    if (second.get_type() == sol::type::function && third.get_type() == sol::type::lua_nil) {
        callback = second;
    } else if (second.get_type() == sol::type::table) {
        for (const auto& kv : second.as<sol::table>()) {
            opts[kv.first] = kv.second;
        }
    } else if (second.valid() && second.get_type() != sol::type::lua_nil) {
        return fail(L, "the second argument must be an options table or the callback");
    }
    opts["url"] = url.as<std::string>();
    return startRequest(L, mod, opts, callback, method, kind);
}

sol::table makeInfo(sol::state_view L, const httpc::Response& r) {
    sol::table info = L.create_table();
    info["status"] = r.status;
    info["url"] = r.finalUrl;
    info["ms"] = static_cast<double>(r.elapsedMs);
    info["redirects"] = r.redirects;
    sol::table h = L.create_table();
    for (const auto& [k, v] : r.headers) {
        sol::object cur = h[k];
        h[k] = cur.get_type() == sol::type::string ? cur.as<std::string>() + ", " + v : v;
    }
    info["headers"] = h;
    return info;
}

}  // namespace

void bindHttpModApi(sol::state_view lua, sol::table api, const lua::ModInfo& mod) {
    sol::table http = lua.create_table();
    const lua::ModInfo info = mod;
    // risen.http.available() -> true | false, reason: may this mod make requests right now?
    http.set_function("available", [info](sol::this_state ts) -> Two {
        sol::state_view L(ts);
        const std::string why = denyReason(info);
        if (why.empty()) {
            return {sol::make_object(L, true), sol::make_object(L, sol::lua_nil)};
        }
        return {sol::make_object(L, false), sol::make_object(L, why)};
    });
    // risen.http.get_text(url [, opts], callback(text, err, info)) -> id | nil, reason
    http.set_function("get_text", [info](sol::this_state ts, sol::object url, sol::object a, sol::object b) -> Two {
        return shortcut(ts, info, url, a, b, "GET", "text");
    });
    // risen.http.get_json(url [, opts], callback(table, err, info)) -> id | nil, reason
    http.set_function("get_json", [info](sol::this_state ts, sol::object url, sol::object a, sol::object b) -> Two {
        return shortcut(ts, info, url, a, b, "GET", "json");
    });
    // risen.http.post_json(url, data [, opts], callback(table | true, err, info)) -> id | nil, reason
    http.set_function("post_json", [info](sol::this_state ts, sol::object url, sol::object data, sol::object a, sol::object b) -> Two {
        sol::state_view L(ts);
        sol::table opts = L.create_table();
        sol::object callback = b;
        if (a.get_type() == sol::type::function && b.get_type() == sol::type::lua_nil) {
            callback = a;
        } else if (a.get_type() == sol::type::table) {
            for (const auto& kv : a.as<sol::table>()) {
                opts[kv.first] = kv.second;
            }
        }
        if (url.get_type() != sol::type::string) {
            return fail(L, "the first argument must be a URL string");
        }
        opts["url"] = url.as<std::string>();
        opts["json"] = data;
        return startRequest(L, info, opts, callback, "POST", "json");
    });
    // risen.http.request(opts, callback(result, err, info)) -> id | nil, reason
    http.set_function("request", [info](sol::this_state ts, sol::object opts, sol::object callback) -> Two {
        sol::state_view L(ts);
        if (opts.get_type() != sol::type::table) {
            return fail(L, "the first argument must be an options table");
        }
        return startRequest(L, info, opts.as<sol::table>(), callback, "GET", "text");
    });
    // risen.http.cancel(id) -> bool: the request still runs but its callback is never called
    http.set_function("cancel", [info](sol::object id) -> bool {
        if (id.get_type() != sol::type::number) {
            return false;
        }
        std::lock_guard lock(g_mutex);
        const auto it = g_pending.find(static_cast<std::uint64_t>(id.as<double>()));
        if (it == g_pending.end() || it->second.mod != info.id || it->second.cancelled) {
            return false;
        }
        it->second.cancelled = true;
        httpClient().cancel(it->first);  // stops the transfer; its completion is dropped by httpPump
        return true;
    });
    api["http"] = http;
}

void httpPump() {
    if (g_queue.empty()) {
        return;
    }
    auto& rt = lua::Runtime::instance();
    if (!rt.running()) {
        return;
    }
    std::vector<httpc::Completed> done;
    g_queue.poll(done);
    if (done.empty()) {
        return;
    }
    auto lock = rt.lock();
    sol::state_view L = rt.lua();
    sol::table callbacks = callbackTable(L);
    for (httpc::Completed& c : done) {
        Pending pending;
        {
            std::lock_guard g(g_mutex);
            const auto it = g_pending.find(c.id);
            if (it == g_pending.end()) {
                continue;
            }
            pending = it->second;
            g_pending.erase(it);
        }
        sol::object cb = callbacks[c.id];
        callbacks[c.id] = sol::lua_nil;
        if (pending.cancelled || cb.get_type() != sol::type::function) {
            continue;
        }
        const httpc::Response& r = c.response;
        sol::object result = sol::make_object(L, sol::lua_nil);
        sol::object err = sol::make_object(L, sol::lua_nil);
        sol::table info = makeInfo(L, r);
        if (!r.ok) {
            err = sol::make_object(L, r.error);
        } else if (r.status < 200 || r.status > 299) {
            err = sol::make_object(L, "HTTP " + std::to_string(r.status));
            info["body"] = r.body.substr(0, 64 * 1024);
        } else if (pending.kind == "json") {
            if (r.body.find_first_not_of(" \t\r\n") == std::string::npos) {
                result = sol::make_object(L, true);  // an empty answer (204 ...)
            } else {
                std::string error;
                auto v = jsonDecode(L, r.body, error);
                if (v) {
                    result = *v;
                } else {
                    err = sol::make_object(L, "invalid JSON: " + error);
                    info["body"] = r.body.substr(0, 64 * 1024);
                }
            }
        } else {
            result = sol::make_object(L, r.body);
        }
        spdlog::debug("http: [{}] request {} finished: {}", pending.mod, c.id, r.ok ? std::to_string(r.status) : r.error);
        crash::Scope trail("http callback", pending.mod);
        try {
            sol::protected_function fn = cb.as<sol::protected_function>();
            sol::protected_function_result res = fn(result, err, info);
            if (!res.valid()) {
                spdlog::error("lua: [{}] http callback failed: {}", pending.mod, res.get<sol::error>().what());
            }
        } catch (const std::exception& e) {
            spdlog::error("lua: [{}] http callback threw: {}", pending.mod, e.what());
        }
    }
}

void httpReleaseOwner(const std::string& mod, std::vector<std::string>& released) {
    std::lock_guard lock(g_mutex);
    int n = 0;
    for (auto& [id, p] : g_pending) {
        if (p.mod == mod && !p.cancelled) {
            p.cancelled = true;
            httpClient().cancel(id);
            ++n;
        }
    }
    if (n > 0) {
        released.push_back(std::to_string(n) + " http request(s)");
    }
}

}  // namespace spass::game
