#include "core/behavior.h"

#include <luajit/lua.hpp>

#include <cmath>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>

#include "core/paths.h"

namespace mk {

namespace {

// the keys risen.behavior.define takes (src/game/behavior_lua.h)
const char* const kHandlers[] = {"start", "update", "stop", "on_death", "on_damage", "on_talk", "on_items", "on_quest", "on_player_near", "on_player_far"};
const char* const kOther[] = {"params", "interval", "near", "description", "events"};

void count_hook(lua_State* L, lua_Debug*) { luaL_error(L, "the script runs too long while it is loaded (a loop at the top level?)"); }

// plain data -> JSON; false when something else is in it (a function, a cycle)
bool plain_json(lua_State* L, int idx, int depth, json& out) {
    if (idx < 0) idx = lua_gettop(L) + idx + 1;  // LuaJIT has no lua_absindex
    switch (lua_type(L, idx)) {
        case LUA_TBOOLEAN: out = lua_toboolean(L, idx) != 0; return true;
        case LUA_TSTRING: out = std::string(lua_tostring(L, idx)); return true;
        case LUA_TNUMBER: {
            const double d = lua_tonumber(L, idx);
            if (!std::isfinite(d)) return false;
            if (d == std::floor(d) && std::fabs(d) < 9e15) out = static_cast<long long>(d);
            else out = d;
            return true;
        }
        case LUA_TTABLE: {
            if (depth > 16) return false;
            // a sequence 1..n is an array, anything else an object
            const size_t n = lua_objlen(L, idx);
            size_t count = 0;
            lua_pushnil(L);
            while (lua_next(L, idx)) {
                ++count;
                lua_pop(L, 1);
            }
            if (n > 0 && count == n) {
                out = json::array();
                for (size_t i = 1; i <= n; ++i) {
                    lua_rawgeti(L, idx, static_cast<int>(i));
                    json v;
                    const bool ok = plain_json(L, -1, depth + 1, v);
                    lua_pop(L, 1);
                    if (!ok) return false;
                    out.push_back(v);
                }
                return true;
            }
            out = json::object();
            lua_pushnil(L);
            while (lua_next(L, idx)) {
                std::string key;
                if (lua_type(L, -2) == LUA_TSTRING) key = lua_tostring(L, -2);
                else if (lua_type(L, -2) == LUA_TNUMBER) {
                    lua_pushvalue(L, -2);
                    key = lua_tostring(L, -1);
                    lua_pop(L, 1);
                } else {
                    lua_pop(L, 2);
                    return false;
                }
                json v;
                if (!plain_json(L, -1, depth + 1, v)) {
                    lua_pop(L, 2);
                    return false;
                }
                out[key] = v;
                lua_pop(L, 1);
            }
            return true;
        }
        default: return false;
    }
}

// `risen` while a script is read: every field is again such a stand-in, and calling one returns one (`local log = risen.log` at the top works)
constexpr const char* kStandIn = R"LUA(
local mt = {}
local function standin() return setmetatable({}, mt) end
mt.__index = function() return standin() end
mt.__call = function() return standin() end
risen = standin()
dofile, loadfile, require, load, loadstring = nil, nil, nil, nil, nil
)LUA";

}  // namespace

BehaviorInfo read_behavior_code(const std::string& code, const std::string& chunk_name) {
    BehaviorInfo info;
    lua_State* L = luaL_newstate();
    if (!L) {
        info.error = "out of memory";
        return info;
    }
    struct Closer {
        lua_State* L;
        ~Closer() { lua_close(L); }
    } closer{L};
    for (auto [name, fn] : {std::pair{"", luaopen_base}, std::pair{LUA_STRLIBNAME, luaopen_string}, std::pair{LUA_TABLIBNAME, luaopen_table},
                            std::pair{LUA_MATHLIBNAME, luaopen_math}}) {
        lua_pushcfunction(L, fn);
        lua_pushstring(L, name);
        lua_call(L, 1, 0);
    }
    if (luaL_dostring(L, kStandIn) != 0) {
        info.error = lua_tostring(L, -1);
        return info;
    }
    if (luaL_loadbuffer(L, code.data(), code.size(), ("=" + chunk_name).c_str()) != 0) {
        info.error = lua_tostring(L, -1);
        return info;
    }
    lua_sethook(L, count_hook, LUA_MASKCOUNT, 5000000);
    if (lua_pcall(L, 0, 1, 0) != 0) {
        info.error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "error while loading";
        return info;
    }
    lua_sethook(L, nullptr, 0, 0);
    if (!lua_istable(L, -1)) {
        info.error = "the script must end with `return { ... }` (the behaviour's definition)";
        return info;
    }
    const int def = lua_gettop(L);
    std::set<std::string> known(std::begin(kOther), std::end(kOther));
    for (const char* h : kHandlers) known.insert(h);
    lua_pushnil(L);
    while (lua_next(L, def)) {
        if (lua_type(L, -2) != LUA_TSTRING || !known.count(lua_tostring(L, -2))) {
            const std::string key = lua_type(L, -2) == LUA_TSTRING ? lua_tostring(L, -2) : std::string("(not a name)");
            info.error = "unknown key '" + key + "' (params, interval, near, description, events, start, update, stop, on_...)";
            return info;
        }
        lua_pop(L, 1);
    }
    for (const char* h : kHandlers) {
        lua_getfield(L, def, h);
        if (!lua_isnil(L, -1)) {
            if (!lua_isfunction(L, -1)) {
                info.error = std::string(h) + " must be a function";
                return info;
            }
            info.handlers.push_back(h);
        }
        lua_pop(L, 1);
    }
    lua_getfield(L, def, "description");
    if (lua_type(L, -1) == LUA_TSTRING) info.description = lua_tostring(L, -1);
    else if (!lua_isnil(L, -1)) {
        info.error = "description must be a string";
        return info;
    }
    lua_pop(L, 1);
    for (const char* k : {"interval", "near"}) {
        lua_getfield(L, def, k);
        const bool is_interval = std::string(k) == "interval";
        if (lua_type(L, -1) == LUA_TNUMBER) {
            const double v = lua_tonumber(L, -1);
            if (is_interval && !(v >= 0 && v <= 3600)) {
                info.error = "interval must be a number of seconds from 0 (every frame) to 3600";
                return info;
            }
            if (!is_interval && !(v > 0 && v <= 100000)) {
                info.error = "near must be a distance > 0 (units, 100 = 1 m)";
                return info;
            }
            (is_interval ? info.interval : info.near) = v;
        } else if (!lua_isnil(L, -1)) {
            info.error = std::string(k) + " must be a number";
            return info;
        }
        lua_pop(L, 1);
    }
    lua_getfield(L, def, "params");
    if (!lua_isnil(L, -1)) {
        json p;
        if (!lua_istable(L, -1) || !plain_json(L, -1, 0, p) || !p.is_object()) {
            info.error = "params must be a table of plain values with names (numbers, strings, booleans, tables of those)";
            return info;
        }
        info.params = p;
    }
    lua_pop(L, 1);
    lua_getfield(L, def, "events");
    if (!lua_isnil(L, -1)) {
        if (!lua_istable(L, -1)) {
            info.error = "events must be a table { [\"event.name\"] = function(self, ...) end }";
            return info;
        }
        const int ev = lua_gettop(L);
        lua_pushnil(L);
        while (lua_next(L, ev)) {
            if (lua_type(L, -2) != LUA_TSTRING || !lua_isfunction(L, -1)) {
                info.error = "events: every entry is [\"event.name\"] = function(self, ...) end";
                return info;
            }
            info.events.push_back(lua_tostring(L, -2));
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    info.ok = true;
    return info;
}

BehaviorInfo behavior_info(const std::string& path) {
    namespace fs = std::filesystem;
    static std::mutex mu;
    static std::map<std::string, std::pair<std::string, BehaviorInfo>> cache;  // path -> (stamp, info)
    std::error_code ec;
    const fs::path fp = u8(path);
    const auto size = fs::file_size(fp, ec);
    if (ec) {
        BehaviorInfo none;
        none.error = "the file " + path + " cannot be read";
        return none;
    }
    const auto time = fs::last_write_time(fp, ec);
    const std::string stamp = std::to_string(size) + ":" + std::to_string(time.time_since_epoch().count());
    {
        std::lock_guard<std::mutex> lock(mu);
        auto it = cache.find(path);
        if (it != cache.end() && it->second.first == stamp) return it->second.second;
    }
    BehaviorInfo info;
    try {
        info = read_behavior_code(read_text(fp), u8str(fp.filename()));
    } catch (const std::exception& e) {
        info.error = e.what();
    }
    std::lock_guard<std::mutex> lock(mu);
    cache[path] = {stamp, info};
    return info;
}

std::string behavior_name_problem(const std::string& name) {
    if (name.empty()) return "the behaviour needs a name";
    if (name.size() > 64) return "at most 64 characters";
    if (std::isdigit(static_cast<unsigned char>(name[0]))) return "the name may not start with a digit";
    for (unsigned char c : name)
        if (!(std::isalnum(c) && c < 0x80) && c != '_' && c != '.' && c != '-') return "letters, digits, _ . and - only";
    return {};
}

std::string behavior_template(const std::string& name) {
    return "-- " + name + R"(: a behaviour for risen-modkit (risen.behavior, docs/LUA_API.md "Behaviours").
-- Attach it to a placed NPC or an NPC template in the NPCs panel; every field of `params` can be set there per NPC.
-- `self`: self.entity (the NPC), self.params, self.state (saved with the game: numbers, strings, booleans, tables of those),
-- self.resumed (true after a load), self.time (seconds running), self:detach().
return {
    description = "Greets the player when he comes close.",
    params = { text = "Hello, stranger!", count_greetings = true },
    interval = 0.5,          -- seconds between update() calls (0 = every frame)
    near = 400,              -- the distance of on_player_near / on_player_far (100 = 1 m)

    start = function(self)
        self.state.greeted = self.state.greeted or 0
    end,

    update = function(self, dt)
    end,

    on_player_near = function(self, player, distance)
        risen.hud.log(self.params.text)
        if self.params.count_greetings then self.state.greeted = self.state.greeted + 1 end
    end,

    -- on_player_far = function(self, player, distance) end,
    -- on_quest = function(self, quest, status, previous)   -- every quest, the game's own included
    --     if quest == "Rufo_GoToDruidHut" and status == "Success" then risen.hud.log("The druid is waiting.") end
    -- end,
    -- on_talk = function(self, info) end,                  -- the player chose a dialogue option of this NPC
    -- on_items = function(self, template, delta, total, cause) end,
    -- on_damage = function(self, attacker, damage) end,
    -- on_death = function(self, killer) end,
    -- events = { ["gamevar.changed"] = function(self, name, value) end },
    -- stop = function(self, reason) end,                   -- "detached" | "died" | "removed" | "restarted"
}
)";
}

}  // namespace mk
