#include "game/lua_pipe.h"

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>

#include <spdlog/spdlog.h>

#include "common/pipeclient.h"

// risen.pipe.open(name [, opts]) -> Pipe | nil, reason     opts.retry = seconds between attempts (default 5, at least 1), opts.framing = "lines" | "u16", opts.write = bool
// Pipe:connected(), Pipe:poll([max]) -> string[], Pipe:pending(), Pipe:send(text) -> bool, Pipe:stats(), Pipe:close(), Pipe:name()

namespace spass::game {
namespace {

using Two = std::tuple<sol::object, sol::object>;

constexpr std::size_t kMaxPipesPerMod = 4;

struct LuaPipe {
    std::shared_ptr<pipeclient::Client> client;
    std::string owner;
    bool closed = false;
};

std::mutex g_mutex;
std::map<std::string, std::vector<std::shared_ptr<LuaPipe>>> g_pipes;  // mod id -> its open pipes

Two fail(sol::state_view L, const std::string& why) { return {sol::make_object(L, sol::lua_nil), sol::make_object(L, why)}; }

void closePipe(LuaPipe& p) {
    if (!p.closed) {
        p.closed = true;
        p.client->stop();
    }
}

void registerType(sol::state_view lua) {
    if (lua["__spass_pipe_type"].valid()) {
        return;
    }
    lua["__spass_pipe_type"] = true;
    lua.new_usertype<LuaPipe>(
        "Pipe", sol::no_constructor,
        // Pipe:connected() -> bool
        "connected", [](LuaPipe& p) { return !p.closed && p.client->connected(); },
        // Pipe:poll(max?) -> string[]: the complete lines received since the last call (at most `max`, default 100, up to 1000)
        "poll", [](sol::this_state ts, LuaPipe& p, sol::optional<double> max) {
            sol::state_view L(ts);
            sol::table t = L.create_table();
            if (p.closed) return t;
            double m = max.value_or(100.0);
            if (!(m >= 1.0)) m = 1.0;  // also catches NaN
            if (m > 1000.0) m = 1000.0;
            int i = 1;
            for (std::string& line : p.client->poll(static_cast<std::size_t>(m))) {
                t[i++] = std::move(line);
            }
            return t;
        },
        // Pipe:pending() -> integer: lines waiting
        "pending", [](LuaPipe& p) { return p.closed ? 0 : static_cast<int>(p.client->pending()); },
        // Pipe:send(text) -> bool: queues one line (a line break is appended); false when not connected, closed or the queue is full
        "send", [](LuaPipe& p, sol::object text) {
            if (p.closed || text.get_type() != sol::type::string) return false;
            return p.client->send(text.as<std::string>());
        },
        // Pipe:stats() -> { connects, dropped, pending }
        "stats", [](sol::this_state ts, LuaPipe& p) {
            sol::state_view L(ts);
            sol::table t = L.create_table();
            t["connects"] = p.client->connects();
            t["dropped"] = p.client->dropped();
            t["pending"] = p.closed ? 0 : static_cast<int>(p.client->pending());
            return t;
        },
        // Pipe:name() -> string
        "name", [](LuaPipe& p) { return p.client->name(); },
        // Pipe:close()
        "close", [](LuaPipe& p) {
            closePipe(p);
            std::lock_guard lock(g_mutex);
            auto it = g_pipes.find(p.owner);
            if (it != g_pipes.end()) {
                auto& v = it->second;
                v.erase(std::remove_if(v.begin(), v.end(), [&](const std::shared_ptr<LuaPipe>& q) { return q.get() == &p; }), v.end());
            }
        });
}

}  // namespace

void bindPipeModApi(sol::state_view lua, sol::table api, const lua::ModInfo& mod) {
    registerType(lua);
    sol::table pipeApi = lua.create_table();
    const std::string id = mod.id;
    const std::vector<std::string> allowed = mod.pipes;
    // risen.pipe.open(name [, opts]) -> Pipe | nil, reason
    pipeApi.set_function("open", [id, allowed](sol::this_state ts, sol::object name, sol::optional<sol::table> opts) -> Two {
        sol::state_view L(ts);
        if (name.get_type() != sol::type::string) {
            return fail(L, "the pipe name must be a string");
        }
        const std::string n = name.as<std::string>();
        if (!pipeclient::validName(n)) {
            return fail(L, "the pipe name must be 1..100 characters of a-z A-Z 0-9 _ . - (the part after \\\\.\\pipe\\)");
        }
        if (std::find(allowed.begin(), allowed.end(), n) == allowed.end()) {
            return fail(L, "this mod may not open the pipe '" + n + "': add it to `pipes = [\"" + n + "\"]` in its mod.toml");
        }
        double retry = 5.0;
        if (opts) {
            sol::object r = (*opts)["retry"];
            if (r.valid() && r.get_type() != sol::type::lua_nil) {
                if (r.get_type() != sol::type::number || !(r.as<double>() >= 0.0) || r.as<double>() > 3600.0) {
                    return fail(L, "opts.retry must be a number of seconds (0..3600)");
                }
                retry = r.as<double>();
            }
        }
        pipeclient::Options po;
        if (opts) {
            sol::object f = (*opts)["framing"];
            if (f.valid() && f.get_type() != sol::type::lua_nil) {
                if (f.get_type() != sol::type::string || (f.as<std::string>() != "lines" && f.as<std::string>() != "u16")) {
                    return fail(L, "opts.framing must be \"lines\" or \"u16\"");
                }
                po.framed = f.as<std::string>() == "u16";
            }
            sol::object w = (*opts)["write"];
            if (w.valid() && w.get_type() != sol::type::lua_nil) {
                if (w.get_type() != sol::type::boolean) {
                    return fail(L, "opts.write must be a boolean");
                }
                po.write = w.as<bool>();
            }
        }
        auto pipe = std::make_shared<LuaPipe>();
        pipe->owner = id;
        pipe->client = std::make_shared<pipeclient::Client>(n, retry, po);
        {
            std::lock_guard lock(g_mutex);
            auto& v = g_pipes[id];
            for (const auto& p : v) {
                if (p->client->name() == n) {
                    return fail(L, "this mod already opened the pipe '" + n + "'");
                }
            }
            if (v.size() >= kMaxPipesPerMod) {
                return fail(L, "a mod may have " + std::to_string(kMaxPipesPerMod) + " pipes open at most");
            }
            v.push_back(pipe);
        }
        pipe->client->start();
        spdlog::info("pipe: [{}] connecting to \\\\.\\pipe\\{} (retry {} s)", id, n, retry < 1.0 ? 1.0 : retry);
        return {sol::make_object(L, pipe), sol::make_object(L, sol::lua_nil)};
    });
    api["pipe"] = pipeApi;
}

void pipeReleaseOwner(const std::string& mod, std::vector<std::string>& released) {
    std::vector<std::shared_ptr<LuaPipe>> mine;
    {
        std::lock_guard lock(g_mutex);
        auto it = g_pipes.find(mod);
        if (it == g_pipes.end()) return;
        mine = std::move(it->second);
        g_pipes.erase(it);
    }
    for (auto& p : mine) closePipe(*p);
    released.push_back(std::to_string(mine.size()) + " pipe(s)");
}

void pipeShutdown() {
    std::map<std::string, std::vector<std::shared_ptr<LuaPipe>>> all;
    {
        std::lock_guard lock(g_mutex);
        all.swap(g_pipes);
    }
    for (auto& [mod, v] : all)
        for (auto& p : v) closePipe(*p);
}

}  // namespace spass::game
