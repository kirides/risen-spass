#include "game/event_bus.h"

#include <atomic>
#include <mutex>
#include <string>

#include <spdlog/spdlog.h>

#include "common/trace.h"
#include "game/abi.h"
#include "game/lua_entity.h"
#include "game/symbols.h"
#include "lua/runtime.h"

namespace spass::game::bus {
namespace {

struct Pending {
    std::string event;
    std::vector<Arg> args;
};

std::mutex g_mutex;
std::vector<Pending> g_queue;
std::atomic<bool> g_active{false};
std::string g_mode;              // session mode, for world.ready
bool g_waitReady = false;        // the player entity does not exist yet
unsigned g_frame = 0;
unsigned g_settleEnd = 0;        // inventory/destroyed events of non-players are dropped until this frame
constexpr unsigned kSettleFrames = 30;

bool playerExists() {
    void* player = nullptr;
    if (sym().ent_get_player != nullptr) {
        guarded([&] {
            EntityHolder ret;
            sym().ent_get_player(&ret);
            player = ret.entity;
        });
    }
    return player != nullptr;
}

bool isPlayer(void* entity) {
    bool yes = false;
    if (entity != nullptr && sym().ent_is_player != nullptr) {
        guarded([&] {
            EntityHolder h{entity};
            yes = sym().ent_is_player(&h);
        });
    }
    return yes;
}

// While the world settles only what happens to the player is gameplay.
bool droppedWhileSettling(const std::string& event, const std::vector<Arg>& args) {
    if (event == "entity.destroyed") {
        return true;
    }
    if ((event.rfind("inventory.", 0) == 0 || event.rfind("item.", 0) == 0) && !args.empty()) {
        const EntityRef* owner = std::get_if<EntityRef>(&args[0]);
        return owner == nullptr || !isPlayer(owner->ptr);
    }
    return false;
}

std::string describe(const Arg& a) {
    struct V {
        std::string operator()(const std::monostate&) const { return "nil"; }
        std::string operator()(const std::string& s) const { return s; }
        std::string operator()(double d) const { return fmt::format("{}", d); }
        std::string operator()(bool b) const { return b ? "true" : "false"; }
        std::string operator()(const EntityRef& e) const { return e.ptr == nullptr ? "nil" : "<" + e.name + ">"; }
        std::string operator()(const ItemList& l) const {
            std::string out = "{";
            const char* sep = "";
            for (const auto& [item, count] : l) {
                out += fmt::format("{}{} x{}", sep, item, count);
                sep = ", ";
            }
            return out + "}";
        }
    };
    return std::visit(V{}, a);
}

sol::object toLua(sol::state_view lua, const Arg& a) {
    struct V {
        sol::state_view lua;
        sol::object operator()(const std::monostate&) const { return sol::make_object(lua, sol::lua_nil); }
        sol::object operator()(const std::string& s) const { return sol::make_object(lua, s); }
        sol::object operator()(double d) const { return sol::make_object(lua, d); }
        sol::object operator()(bool b) const { return sol::make_object(lua, b); }
        sol::object operator()(const ItemList& l) const {
            sol::state_view L = lua;
            sol::table t = L.create_table();
            int i = 1;
            for (const auto& [item, count] : l) {
                sol::table row = L.create_table();
                row["item"] = item;
                row["count"] = count;
                t[i++] = row;
            }
            return sol::make_object(lua, t);
        }
        sol::object operator()(const EntityRef& e) const {
            if (e.ptr == nullptr) {
                return sol::make_object(lua, sol::lua_nil);
            }
            // The event is dispatched in the frame it happened in, so the pointer is still the entity; its name is read again (an NPC
            // spawned by risen.npc.spawn gets its own name right after Entity::Spawn returned).
            std::string name = entityNameOf(e.ptr);
            if (name.empty()) {
                name = e.name;
            }
            LuaEntity h{e.ptr, name, currentEntityGeneration()};
            return sol::make_object(lua, h);
        }
    };
    return std::visit(V{lua}, a);
}

}  // namespace

EntityRef capture(void* entity) {
    EntityRef r;
    if (entity == nullptr) {
        return r;
    }
    r.ptr = entity;
    r.name = entityNameOf(entity);
    return r;
}

bool wanted(const std::string& event) {
    if (trace::enabled(trace::Channel::Events)) {
        return true;
    }
    auto& rt = lua::Runtime::instance();
    return rt.running() && rt.hasHandlers(event);
}

void setActive(bool active, const std::string& mode) {
    g_active = active;
    g_mode = mode;
    g_waitReady = active;
    if (!active) {
        clear();
    }
}

void resume() { g_active = true; }

bool settling() { return g_waitReady || g_frame < g_settleEnd; }

bool active() { return g_active.load(); }

void clear() {
    std::lock_guard lock(g_mutex);
    g_queue.clear();
}

void post(const std::string& event, std::vector<Arg> args) {
    if (!g_active.load()) {
        return;
    }
    if (settling() && droppedWhileSettling(event, args)) {
        return;
    }
    auto& rt = lua::Runtime::instance();
    const bool handled = rt.running() && rt.hasHandlers(event);
    if (trace::enabled(trace::Channel::Events)) {
        std::string joined;
        const char* sep = "";
        for (const Arg& a : args) {
            joined += sep + describe(a);
            sep = ", ";
        }
        trace::emit(trace::Channel::Events, event + "(" + joined + ")" + (handled ? "" : "  [no handler]"));
    }
    if (!handled) {
        return;
    }
    std::lock_guard lock(g_mutex);
    g_queue.push_back({event, std::move(args)});
}

void flush() {
    ++g_frame;
    if (g_waitReady && g_active.load() && playerExists()) {
        g_waitReady = false;
        g_settleEnd = g_frame + kSettleFrames;
        post("world.ready", {g_mode});
    }
    auto& rt = lua::Runtime::instance();
    for (int round = 0; round < 4; ++round) {
        std::vector<Pending> batch;
        {
            std::lock_guard lock(g_mutex);
            batch.swap(g_queue);
        }
        if (batch.empty()) {
            return;
        }
        if (!rt.running()) {
            return;
        }
        auto lock = rt.lock();
        for (const Pending& p : batch) {
            std::vector<sol::object> objs;
            objs.reserve(p.args.size());
            for (const Arg& a : p.args) {
                objs.push_back(toLua(rt.lua(), a));
            }
            rt.emitList(p.event, objs);
        }
    }
}

}  // namespace spass::game::bus
