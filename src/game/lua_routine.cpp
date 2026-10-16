#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>
#include <vector>

#include <spdlog/spdlog.h>

#include "common/trace.h"
#include "game/abi.h"
#include "game/lua_entity.h"
#include "game/properties.h"
#include "game/routine_lua.h"
#include "game/template_object.h"

// NPC routines (docs/RE.md section 35, docs/LUA_API.md "NPC routines").
//
// The engine's daily routine is data of the NPC's `gCNavigation_PS`: a named routine owns three points (working, relaxing, sleeping), the script
// `GetRoutineTime` maps the world time to one of them (humans: before 6:00 sleeping, 6:00-20:00 working, from 20:00 relaxing) and `ContinueRoutine` /
// `SelectRoutineDestination` / `SelectRoutineTask` send the NPC there and run the AI state of the point (a freepoint's use type: guard, stand, pace,
// sit, sleep, ...). This file reads and writes those points, lists the freepoints / anchors of the world and reads/sets the world time; the friendly
// API on top (`risen.npc.set_routine`, `risen.world.add_point`, ...) is the Lua chunk in routine_lua.h.

namespace spass::game {
namespace {

using Two = std::tuple<sol::object, sol::object>;

Two okResult(sol::state_view lua, sol::object value) { return {std::move(value), sol::make_object(lua, sol::lua_nil)}; }
Two failResult(sol::state_view lua, const std::string& reason) {
    return {sol::make_object(lua, sol::lua_nil), sol::make_object(lua, reason)};
}

constexpr std::size_t kNavIndex = 0x5c;  // gCNavigation_PS: index of the current routine (long)
constexpr std::size_t kNavCount = 0x68;  // gCNavigation_PS: number of routines of the bTObjArray<gSRoutine> at +0x60 (0x38 bytes each)

void* propertySetOf(void* entity, const char* cls) {
    const Symbols& s = sym();
    if (entity == nullptr || s.ent_get_property_set == nullptr) {
        return nullptr;
    }
    void* ps = nullptr;
    guarded([&] {
        const BString name(cls);
        ps = s.ent_get_property_set(entity, name.get());
    });
    return ps;
}

bool haveNavigationSymbols() {
    const Symbols& s = sym();
    return s.nav_get_work != nullptr && s.nav_set_work != nullptr && s.nav_get_relax != nullptr && s.nav_set_relax != nullptr &&
           s.nav_get_sleep != nullptr && s.nav_set_sleep != nullptr && s.ent_get_property_set != nullptr;
}

// Every native takes its arguments as sol::object and checks them itself: a wrong type must produce `nil, reason`, never an error raised through LuaJIT's frames
// (the game crashed on risen.npc.routine_points(5) with a typed parameter).
bool entityArg(const sol::object& o, LuaEntity& out) {
    if (!o.valid() || !o.is<LuaEntity>()) {
        return false;
    }
    out = o.as<LuaEntity>();
    return true;
}

bool numberArg(const sol::object& o, float& out) {
    if (!o.valid() || !o.is<double>()) {
        return false;
    }
    out = static_cast<float>(o.as<double>());
    return true;
}

bool stringArg(const sol::object& o, std::string& out) {
    if (!o.valid() || !o.is<std::string>() || o.get_type() != sol::type::string) {
        return false;
    }
    out = o.as<std::string>();
    return true;
}

std::string stripPrefix(const std::string& value, const char* prefix) {
    const std::string p(prefix);
    return value.compare(0, p.size(), p) == 0 ? value.substr(p.size()) : value;
}

// ---- the routine of an NPC --------------------------------------------------------------------------------------------------

// risen.npc.routine_points(npc) -> { routine = "Start", index = 0, count = 1, work = Entity|nil, relax = ..., sleep = ..., guard = ... } | nil, reason
Two routinePoints(sol::this_state ts, sol::object npcArg) {
    sol::state_view L(ts);
    const Symbols& s = sym();
    LuaEntity npc;
    if (!entityArg(npcArg, npc)) {
        return failResult(L, "the first argument must be an NPC entity");
    }
    void* entity = ptr(npc);
    if (entity == nullptr) {
        return failResult(L, "entity is gone");
    }
    if (!haveNavigationSymbols()) {
        return failResult(L, "the engine's routine functions are unavailable");
    }
    void* nav = propertySetOf(entity, "gCNavigation_PS");
    if (nav == nullptr) {
        return failResult(L, "entity has no navigation property set (not an NPC)");
    }
    sol::table out = L.create_table();
    void *work = nullptr, *relax = nullptr, *sleep = nullptr, *guard = nullptr;
    int index = -1, count = 0;
    std::string name;
    if (!guarded([&] {
            index = *reinterpret_cast<const int*>(static_cast<const char*>(nav) + kNavIndex);
            count = *reinterpret_cast<const int*>(static_cast<const char*>(nav) + kNavCount);
            if (index >= 0 && index < count) {
                work = s.nav_get_work(nav);
                relax = s.nav_get_relax(nav);
                sleep = s.nav_get_sleep(nav);
                if (s.nav_get_routine_name != nullptr) {
                    name = sym().bstring_get_text(s.nav_get_routine_name(nav));
                }
            }
            if (s.npc_get_guard_point != nullptr) {
                EntityHolder h{entity};
                EntityHolder ret;
                s.npc_get_guard_point(&h, &ret);
                guard = ret.entity;
            }
        })) {
        return failResult(L, "the engine raised an exception while reading the routine");
    }
    out["routine"] = name;
    out["index"] = index;
    out["count"] = count;
    out["work"] = makeEntityObject(L, work);
    out["relax"] = makeEntityObject(L, relax);
    out["sleep"] = makeEntityObject(L, sleep);
    out["guard"] = makeEntityObject(L, guard);
    return okResult(L, out);
}

// Entity argument of set_routine_points: an Entity, `false` (clear the slot) or absent.
enum class Slot { Keep, Clear, Set };

struct SlotValue {
    Slot kind = Slot::Keep;
    void* entity = nullptr;
};

bool readSlot(sol::table t, const char* key, SlotValue& out, std::string& error) {
    sol::object v = t[key];
    if (!v.valid() || v.is<sol::lua_nil_t>()) {
        return true;
    }
    if (v.is<bool>()) {
        if (v.as<bool>()) {
            error = std::string("'") + key + "' must be an entity or false";
            return false;
        }
        out.kind = Slot::Clear;
        return true;
    }
    if (v.is<LuaEntity>()) {
        void* e = ptr(v.as<LuaEntity>());
        if (e == nullptr) {
            error = std::string("'") + key + "' is a stale entity handle";
            return false;
        }
        out.kind = Slot::Set;
        out.entity = e;
        return true;
    }
    error = std::string("'") + key + "' must be an entity or false";
    return false;
}

// risen.npc.set_routine_points(npc, { work = Entity|false, relax = ..., sleep = ..., guard = ... }) -> true | nil, reason
Two setRoutinePoints(sol::this_state ts, sol::object npcArg, sol::object pointsArg) {
    sol::state_view L(ts);
    const Symbols& s = sym();
    LuaEntity npc;
    if (!entityArg(npcArg, npc)) {
        return failResult(L, "the first argument must be an NPC entity");
    }
    if (!pointsArg.valid() || !pointsArg.is<sol::table>()) {
        return failResult(L, "the second argument must be a table { work = , relax = , sleep = , guard = }");
    }
    sol::table points = pointsArg.as<sol::table>();
    void* entity = ptr(npc);
    if (entity == nullptr) {
        return failResult(L, "entity is gone");
    }
    if (!haveNavigationSymbols()) {
        return failResult(L, "the engine's routine functions are unavailable");
    }
    SlotValue work, relax, sleep, guard;
    std::string error;
    if (!readSlot(points, "work", work, error) || !readSlot(points, "relax", relax, error) || !readSlot(points, "sleep", sleep, error) ||
        !readSlot(points, "guard", guard, error)) {
        return failResult(L, error);
    }
    void* nav = propertySetOf(entity, "gCNavigation_PS");
    if (nav == nullptr) {
        return failResult(L, "entity has no navigation property set (not an NPC)");
    }
    bool valid = false;
    guarded([&] {
        const int index = *reinterpret_cast<const int*>(static_cast<const char*>(nav) + kNavIndex);
        const int count = *reinterpret_cast<const int*>(static_cast<const char*>(nav) + kNavCount);
        valid = index >= 0 && index < count;
    });
    if (!valid) {
        return failResult(L, "the NPC has no current routine");
    }
    bool ok = true;
    if (!guarded([&] {
            const auto apply = [&](const SlotValue& v, bool (*fn)(void*, void*)) {
                if (v.kind != Slot::Keep) {
                    ok = fn(nav, v.kind == Slot::Set ? v.entity : nullptr) && ok;
                }
            };
            apply(work, s.nav_set_work);
            apply(relax, s.nav_set_relax);
            apply(sleep, s.nav_set_sleep);
            if (guard.kind != Slot::Keep && s.npc_set_guard_point != nullptr) {
                EntityHolder h{entity};
                EntityHolder g{guard.kind == Slot::Set ? guard.entity : nullptr};
                ok = s.npc_set_guard_point(&h, &g) && ok;
            }
        })) {
        return failResult(L, "the engine raised an exception while setting the points");
    }
    SPASS_TRACE(trace::Channel::Script, "routine points of '{}': work {} relax {} sleep {} guard {}", entityNameOf(entity),
                work.kind == Slot::Keep ? "-" : work.kind == Slot::Set ? "set" : "clear", relax.kind == Slot::Keep ? "-" : relax.kind == Slot::Set ? "set" : "clear",
                sleep.kind == Slot::Keep ? "-" : sleep.kind == Slot::Set ? "set" : "clear", guard.kind == Slot::Keep ? "-" : guard.kind == Slot::Set ? "set" : "clear");
    return okResult(L, sol::make_object(L, ok));
}

// risen.npc.select_routine(npc, name) -> true | nil, reason: makes the named routine of the NPC the current one (the engine's SetRoutine)
Two selectRoutine(sol::this_state ts, sol::object npcArg, sol::object nameArg) {
    sol::state_view L(ts);
    const Symbols& s = sym();
    LuaEntity npc;
    std::string name;
    if (!entityArg(npcArg, npc) || !stringArg(nameArg, name) || name.empty()) {
        return failResult(L, "expected an NPC entity and the name of a routine");
    }
    void* entity = ptr(npc);
    if (entity == nullptr) {
        return failResult(L, "entity is gone");
    }
    void* nav = propertySetOf(entity, "gCNavigation_PS");
    if (nav == nullptr || s.nav_select_routine == nullptr) {
        return failResult(L, "entity has no navigation property set (not an NPC)");
    }
    bool ok = false;
    if (!guarded([&] {
            const BString n(name);
            ok = s.nav_select_routine(nav, n.get());
        })) {
        return failResult(L, "the engine raised an exception");
    }
    if (!ok) {
        return failResult(L, "the NPC has no routine named '" + name + "'");
    }
    return okResult(L, sol::make_object(L, true));
}

// risen.npc.apply_routine(npc) -> true | nil, reason: tells the AI that the routine changed and lets it pick its task again (PSRoutine::ContinueRoutine, what
// the dialogue commands SetRoutine / SetWorkingPoint end with)
Two applyRoutine(sol::this_state ts, sol::object npcArg) {
    sol::state_view L(ts);
    const Symbols& s = sym();
    LuaEntity npc;
    if (!entityArg(npcArg, npc)) {
        return failResult(L, "the argument must be an NPC entity");
    }
    void* entity = ptr(npc);
    if (entity == nullptr) {
        return failResult(L, "entity is gone");
    }
    if (s.rt_continue_routine == nullptr) {
        return failResult(L, "the engine function PSRoutine::ContinueRoutine is unavailable");
    }
    void* rps = propertySetOf(entity, "gCScriptRoutine_PS");
    if (rps == nullptr) {
        return failResult(L, "entity has no routine property set (not an NPC)");
    }
    if (!guarded([&] {
            propertySet(rps, "RoutineChanged", "true");
            EntityHolder h{entity};
            s.rt_continue_routine(&h);
        })) {
        return failResult(L, "the engine raised an exception while continuing the routine");
    }
    return okResult(L, sol::make_object(L, true));
}

// ---- the world's freepoints and anchors --------------------------------------------------------------------------------------

struct Candidate {
    void* entity = nullptr;
    std::string templateName;
    float x = 0, y = 0, z = 0;
    float distance = 0;
};

// risen.world.scan(templates, opts) -> { { entity =, name =, template =, x =, y =, z =, kind =, use =, anchor =, index =, max_users =, distance = }, ... }
//   templates: template names whose instances are looked at; opts: near = Entity | { x, y, z }, radius, name = exact entity name, name_prefix, limit (default 500)
sol::table scanPoints(sol::this_state ts, sol::object templatesArg, sol::object optsArg) {
    sol::state_view L(ts);
    const Symbols& s = sym();
    sol::table out = L.create_table();
    if (!templatesArg.valid() || !templatesArg.is<sol::table>()) {
        return out;
    }
    sol::table templates = templatesArg.as<sol::table>();
    sol::optional<sol::table> opts;
    if (optsArg.valid() && optsArg.is<sol::table>()) {
        opts = optsArg.as<sol::table>();
    }
    std::map<const void*, std::string> wanted;
    for (const auto& kv : templates) {
        if (kv.second.is<std::string>()) {
            const std::string name = kv.second.as<std::string>();
            if (const void* creator = templateCreatorPtr(name)) {
                wanted[creator] = name;
            }
        }
    }
    float nx = 0, ny = 0, nz = 0;
    bool haveNear = false;
    double radius = 0;
    std::string exactName, prefix;
    int limit = 500;
    if (opts) {
        sol::table o = *opts;
        sol::object near = o["near"];
        if (near.is<LuaEntity>()) {
            void* e = ptr(near.as<LuaEntity>());
            if (e != nullptr && guarded([&] {
                    float v[3] = {};
                    EntityHolder h{e};
                    s.ent_get_position(&h, v);
                    nx = v[0];
                    ny = v[1];
                    nz = v[2];
                })) {
                haveNear = true;
            }
        } else if (near.is<sol::table>()) {
            sol::table t = near.as<sol::table>();
            const auto coord = [&t](int i, const char* key) {
                sol::object v = t[i];
                if (!v.is<double>()) {
                    v = t[key];
                }
                return v.is<double>() ? static_cast<float>(v.as<double>()) : 0.0f;
            };
            nx = coord(1, "x");
            ny = coord(2, "y");
            nz = coord(3, "z");
            haveNear = true;
        }
        radius = o.get_or("radius", 0.0);
        exactName = o.get_or<std::string>("name", "");
        prefix = o.get_or<std::string>("name_prefix", "");
        limit = std::clamp(o.get_or("limit", 500), 1, 20000);
    }
    std::vector<Candidate> found;
    if (wanted.empty() || s.ent_get_position == nullptr) {
        return out;
    }
    if (!walkSceneEntities([&](void* entity, const void* creator) {
            const auto it = wanted.find(creator);
            if (it != wanted.end() && found.size() < 100000) {
                found.push_back(Candidate{entity, it->second});
            }
        })) {
        return out;
    }
    std::vector<Candidate> kept;
    for (Candidate& c : found) {
        float v[3] = {};
        if (!guarded([&] {
                EntityHolder h{c.entity};
                s.ent_get_position(&h, v);
            })) {
            continue;
        }
        c.x = v[0];
        c.y = v[1];
        c.z = v[2];
        if (haveNear) {
            const float dx = c.x - nx, dy = c.y - ny, dz = c.z - nz;
            c.distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (radius > 0 && c.distance > radius) {
                continue;
            }
        }
        if (!exactName.empty() || !prefix.empty()) {
            const std::string name = entityNameOf(c.entity);
            if (!exactName.empty() && name != exactName) {
                continue;
            }
            if (!prefix.empty() && name.compare(0, prefix.size(), prefix) != 0) {
                continue;
            }
        }
        kept.push_back(c);
    }
    if (haveNear) {
        std::sort(kept.begin(), kept.end(), [](const Candidate& a, const Candidate& b) { return a.distance < b.distance; });
    }
    int n = 0;
    for (const Candidate& c : kept) {
        if (n >= limit) {
            break;
        }
        sol::table t = L.create_table();
        t["entity"] = makeEntityObject(L, c.entity);
        t["name"] = entityNameOf(c.entity);
        t["template"] = c.templateName;
        t["x"] = c.x;
        t["y"] = c.y;
        t["z"] = c.z;
        if (haveNear) {
            t["distance"] = c.distance;
        }
        if (void* anchor = propertySetOf(c.entity, "gCAnchor_PS")) {
            t["kind"] = "anchor";
            if (const auto v = propertyGet(anchor, "AnchorType")) {
                t["anchor"] = stripPrefix(*v, "gEAnchorType_");
            }
            if (const auto v = propertyGet(anchor, "PatrolIndex")) {
                t["index"] = std::atoi(v->c_str());
            }
            if (const auto v = propertyGet(anchor, "MaxUsers")) {
                t["max_users"] = std::atoi(v->c_str());
            }
        } else if (void* inter = propertySetOf(c.entity, "gCInteraction_PS")) {
            t["kind"] = "freepoint";
            if (const auto v = propertyGet(inter, "UseType")) {
                t["use"] = stripPrefix(*v, "gEInteractionUseType_");
            }
            if (const auto v = propertyGet(inter, "RoutineTask")) {
                t["task"] = *v;
            }
        } else {
            t["kind"] = "helper";
        }
        out[++n] = t;
    }
    return out;
}

// risen.world.spawn_point(template, x, y, z [, yaw_degrees]) -> Entity | nil, reason
// Spawns a freepoint / anchor template (Entity::Spawn), marks it save-relevant and returns it. A spawned entity is only written into savegames when it is
// save-relevant (docs/RE.md section 35: for NPCs the engine sets that itself, for freepoints it does not). The yaw turns the point (and so whoever
// stands at it) around the vertical axis: 0 looks along +z.
Two spawnPoint(sol::this_state ts, sol::object nameArg, sol::object xArg, sol::object yArg, sol::object zArg, sol::object yawArg) {
    sol::state_view L(ts);
    const Symbols& s = sym();
    std::string templateName;
    float x = 0, y = 0, z = 0, yawValue = 0;
    if (!stringArg(nameArg, templateName) || !numberArg(xArg, x) || !numberArg(yArg, y) || !numberArg(zArg, z)) {
        return failResult(L, "expected a template name and a position x, y, z");
    }
    const bool haveYaw = yawArg.valid() && !yawArg.is<sol::lua_nil_t>();
    if (haveYaw && !numberArg(yawArg, yawValue)) {
        return failResult(L, "the yaw must be a number (degrees)");
    }
    const sol::optional<float> yaw = haveYaw ? sol::optional<float>(yawValue) : sol::nullopt;
    if (s.ent_spawn == nullptr) {
        return failResult(L, "the engine function Entity::Spawn is unavailable");
    }
    const float a = yaw.value_or(0.0f) * 3.14159265358979f / 180.0f;
    const float sn = std::sin(a), cs = std::cos(a);
    // row-major pose: x axis, y axis, at axis (+z for yaw 0), translation
    const float pose[16] = {cs, 0, -sn, 0, 0, 1, 0, 0, sn, 0, cs, 0, x, y, z, 1};
    EntityHolder ret;
    bool valid = false;
    if (!guarded([&] {
            TemplateObject t(templateName);
            valid = t.valid();
            if (valid) {
                s.ent_spawn(&ret, t.giveAway(), pose, false);  // callee destroys the template
            }
        })) {
        return failResult(L, "the engine raised an exception while spawning (no world loaded?)");
    }
    if (!valid) {
        return failResult(L, "unknown template '" + templateName + "'");
    }
    if (ret.entity == nullptr) {
        return failResult(L, "the engine did not create the entity");
    }
    if (s.eent_set_save_relevant != nullptr) {
        guarded([&] { s.eent_set_save_relevant(ret.entity, true); });
    }
    SPASS_TRACE(trace::Channel::Spawn, "spawn_point '{}' at ({:.0f}, {:.0f}, {:.0f}) yaw {:.0f}", templateName, x, y, z, yaw.value_or(0.0f));
    return okResult(L, makeEntityObject(L, ret.entity));
}

// risen.world.nav_point(x, y, z [, radius]) -> x, y, z | nil: the walkable point nearest to a position (the engine's own query, 400 units by default)
Two navPoint(sol::this_state ts, sol::object xArg, sol::object yArg, sol::object zArg, sol::object radiusArg) {
    sol::state_view L(ts);
    const Symbols& s = sym();
    float x = 0, y = 0, z = 0, radiusValue = 400.0f;
    if (!numberArg(xArg, x) || !numberArg(yArg, y) || !numberArg(zArg, z)) {
        return failResult(L, "expected a position x, y, z");
    }
    if (radiusArg.valid() && !radiusArg.is<sol::lua_nil_t>() && !numberArg(radiusArg, radiusValue)) {
        return failResult(L, "the radius must be a number");
    }
    const sol::optional<float> radius = radiusValue;
    if (s.ent_nearest_nav_point == nullptr) {
        return failResult(L, "the engine function Entity::GetNearestNavigationPoint is unavailable");
    }
    void* player = nullptr;
    guarded([&] {
        EntityHolder ret;
        s.ent_get_player(&ret);
        player = ret.entity;
    });
    if (player == nullptr) {
        return failResult(L, "no player (no world loaded?)");
    }
    float out[4] = {};
    const float in[4] = {x, y, z, 0};
    if (!guarded([&] {
            EntityHolder h{player};
            s.ent_nearest_nav_point(&h, out, in, radius.value_or(400.0f));
        })) {
        return failResult(L, "the engine raised an exception");
    }
    if (out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f) {
        return failResult(L, "no walkable point near the position");
    }
    sol::table t = L.create_table();
    t[1] = out[0];
    t[2] = out[1];
    t[3] = out[2];
    return okResult(L, t);
}

// ---- the entity tree ------------------------------------------------------------------------------------------------------------

// Entity:children() -> { Entity, ... }: the direct children in the entity tree (first child at +0x20, next sibling at +0x18: the walk of eCSceneAdmin).
// Anchors own their freepoints this way: a freepoint attached below an anchor becomes one of its interact points.
void addTreeMethods(sol::usertype<LuaEntity>& ut) {
    ut["children"] = [](sol::this_state ts, const LuaEntity& e) {
        sol::state_view L(ts);
        sol::table out = L.create_table();
        void* node = ptr(e);
        if (node == nullptr) {
            return out;
        }
        std::vector<void*> kids;
        guarded([&] {
            char* child = *reinterpret_cast<char**>(static_cast<char*>(node) + 0x20);
            while (child != nullptr && kids.size() < 1000) {
                kids.push_back(child);
                char* next = *reinterpret_cast<char**>(child + 0x18);
                if (next == child) {
                    break;
                }
                child = next;
            }
        });
        int n = 0;
        for (void* k : kids) {
            out[++n] = makeEntityObject(L, k);
        }
        return out;
    };
    // Entity:direction() -> dx, dy, dz: the unit vector the entity looks along (Entity::GetAtVector); a point's yaw and what an NPC at it faces can be read back
    ut["direction"] = [](const LuaEntity& e) -> std::tuple<sol::optional<float>, sol::optional<float>, sol::optional<float>> {
        const Symbols& s = sym();
        void* entity = ptr(e);
        float v[4] = {};
        if (entity == nullptr || s.ent_get_at == nullptr || !guarded([&] {
                EntityHolder h{entity};
                s.ent_get_at(&h, v);
            })) {
            return {sol::nullopt, sol::nullopt, sol::nullopt};
        }
        return {v[0], v[1], v[2]};
    };
    // Entity:attach(child) -> bool: makes `child` a child of this entity (eCNode::AttachChild)
    ut["attach"] = [](const LuaEntity& parent, sol::object childArg) {
        const Symbols& s = sym();
        LuaEntity child;
        if (!entityArg(childArg, child)) {
            return false;
        }
        void* p = ptr(parent);
        void* c = ptr(child);
        if (p == nullptr || c == nullptr || p == c || s.node_attach_child == nullptr) {
            return false;
        }
        bool ok = false;
        return guarded([&] { ok = s.node_attach_child(p, c); }) && ok;
    };
}

}  // namespace

void addRoutineMethods(sol::usertype<LuaEntity>& ut) { addTreeMethods(ut); }

void bindRoutine(sol::state_view lua, sol::table risen) {
    const Symbols& s = sym();
    sol::table npc = risen["npc"];
    npc.set_function("routine_points", routinePoints);
    npc.set_function("set_routine_points", setRoutinePoints);
    npc.set_function("select_routine", selectRoutine);
    npc.set_function("apply_routine", applyRoutine);

    sol::table world = risen["world"];
    world.set_function("scan", scanPoints);
    world.set_function("spawn_point", spawnPoint);
    world.set_function("nav_point", navPoint);
    // risen.world.time() -> hour (0..23), minute (0..59): the in-game clock (Entity::GetWorldTime encodes hh.mm as hour + minute / 100)
    world.set_function("time", [&s](sol::this_state ts) -> std::tuple<sol::object, sol::object> {
        sol::state_view L(ts);
        float t = 0;
        if (s.world_get_time == nullptr || !guarded([&] { t = s.world_get_time(); })) {
            return failResult(L, "the world time is unavailable");
        }
        const int hour = static_cast<int>(std::floor(t + 0.0005f));
        const int minute = std::clamp(static_cast<int>(std::lround((t - static_cast<float>(hour)) * 100.0f)), 0, 59);
        return {sol::make_object(L, hour), sol::make_object(L, minute)};
    });
    // risen.world.set_time(hour, minute) -> bool: sets the clock (0..23, 0..59); NPCs follow their routine points at the next look (apply_routine)
    world.set_function("set_time", [&s](sol::object hourArg, sol::object minuteArg) {
        float h = 0, mm = 0;
        if (!numberArg(hourArg, h) || (minuteArg.valid() && !minuteArg.is<sol::lua_nil_t>() && !numberArg(minuteArg, mm))) {
            return false;
        }
        const int hour = static_cast<int>(h);
        const int m = static_cast<int>(mm);
        if (s.world_set_time == nullptr || hour < 0 || hour > 23 || m < 0 || m > 59) {
            return false;
        }
        return guarded([&] { s.world_set_time(static_cast<float>(hour) + static_cast<float>(m) * 0.01f); });
    });
    (void)lua;
}

void installRoutineHelpers(sol::state_view lua, sol::table risen) {
    sol::load_result loaded = lua.load(kRoutineChunk, "=risen.world.points");
    if (!loaded.valid()) {
        spdlog::error("lua: routine api failed to load: {}", loaded.get<sol::error>().what());
        return;
    }
    sol::protected_function fn = loaded;
    sol::protected_function_result res = fn(risen);
    if (!res.valid()) {
        spdlog::error("lua: routine api failed to run: {}", res.get<sol::error>().what());
    }
}

void installRoutineModApi(sol::state_view lua, sol::table api, const std::string& modId) {
    sol::load_result loaded = lua.load(kRoutineModChunk, "=risen.npc.set_routine");
    if (!loaded.valid()) {
        spdlog::error("lua: routine mod api failed to load: {}", loaded.get<sol::error>().what());
        return;
    }
    sol::protected_function fn = loaded;
    sol::protected_function_result res = fn(api, modId);
    if (!res.valid()) {
        spdlog::error("lua: routine mod api failed to run: {}", res.get<sol::error>().what());
    }
}

}  // namespace spass::game
