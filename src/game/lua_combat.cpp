#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

#include <spdlog/spdlog.h>

#include "common/trace.h"
#include "game/abi.h"
#include "game/bindings.h"
#include "game/combat_names.h"
#include "game/lua_entity.h"
#include "game/properties.h"
#include "game/script_bridge.h"
#include "game/symbols.h"
#include "lua/runtime.h"

// Damage hooks (M14, docs/LUA_API.md "Combat", docs/RE.md section 39): risen.combat.on_damage.
//
// Every hit in the game ends in the script `DoDamage` (Script_Game.dll): DoMeleeDamage (sword, axe, staff, fist, monsters), DoMagicFireballDamage /
// DoMagicFrostDamage / DoMagicMissileDamage, DoProjectileDamage, DoArrowDamage, DoBoltDamage, DoInfernoDamage, DoFireDamage, DoPhysicalDamage, DoTitanDamage call it
// through a script proxy, i.e. through the registry slot that the script bridge swaps. `DoDamage(self = the damaging item, other = the victim, arg = damage)`
// subtracts the victim's protection for the item's DamageType, applies the minimum, the difficulty and party rules and takes the hit points. The interceptor below runs
// the Lua handlers with the damage that is about to go in (area falloff and the Fireball skill bonus are already part of it) and passes the handlers' verdict on:
// the original script with the new number, or nothing at all (a cancelled hit).

namespace spass::game {
namespace {

using Two = std::tuple<sol::object, sol::object>;
Two failResult(sol::state_view lua, const std::string& reason) {
    return {sol::make_object(lua, sol::lua_nil), sol::make_object(lua, reason)};
}

constexpr const char* kScript = "DoDamage";
constexpr std::size_t kMaxHandlers = 64;
constexpr int kMaxWarnings = 10;   // per handler: a handler that fails on every hit would otherwise fill the log

struct Handler {
    std::uint64_t id = 0;
    sol::main_protected_function fn;
    combat::Filter filter;
    int warnings = 0;
    std::string owner;  // the mod that registered it (hot reload removes it)
};

std::vector<Handler> g_handlers;
std::uint64_t g_nextId = 1;
int g_failures = 0;
std::string g_lastFailure;
thread_local int t_depth = 0;   // inside a handler: DoDamage calls made from there are not offered to the handlers again

// ---- reading the engine --------------------------------------------------------------------------------------------------------------------

void* selfOf(void* unit) {
    void* e = nullptr;
    if (unit != nullptr && sym().unit_get_self != nullptr) {
        guarded([&] { e = sym().unit_get_self(unit); });
    }
    return e;
}

void* otherOf(void* unit) {
    void* e = nullptr;
    if (unit != nullptr && sym().unit_get_other != nullptr) {
        guarded([&] { e = sym().unit_get_other(unit); });
    }
    return e;
}

// A script argument is an `Entity` holder (its first qword is the eCEntity*) or null (then the unit's self/other entity is meant).
void* entityOfArg(void* holder, void* fallback) {
    void* e = nullptr;
    guarded([&] { e = holder != nullptr ? *static_cast<void**>(holder) : fallback; });
    return e;
}

bool isPlayer(void* entity) {
    bool r = false;
    if (entity != nullptr && sym().ent_is_player != nullptr) {
        guarded([&] {
            EntityHolder h{entity};
            r = sym().ent_is_player(&h);
        });
    }
    return r;
}

// The template an entity was created from: entity vtable +0x170 (template entity) -> GetName (+0x178).
std::string templateOfEntity(void* entity) {
    std::string tpl;
    guarded([&] {
        void** vt = *reinterpret_cast<void***>(entity);
        if (void* tplEntity = reinterpret_cast<void* (*)(void*)>(vt[0x170 / 8])(entity)) {
            void** tvt = *reinterpret_cast<void***>(tplEntity);
            if (const void* n = reinterpret_cast<const void* (*)(void*)>(tvt[0x178 / 8])(tplEntity)) {
                tpl = sym().bstring_get_text(n);
            }
        }
    });
    return tpl;
}

void* propertySetOf(void* entity, const char* cls) {
    void* ps = nullptr;
    if (entity != nullptr && sym().ent_get_property_set != nullptr) {
        guarded([&] {
            const BString name(cls);
            ps = sym().ent_get_property_set(entity, name.get());
        });
    }
    return ps;
}

// Everything the hook reads about the damaging item's gCDamage_PS.
struct DamageSource {
    void* entity = nullptr;
    void* attacker = nullptr;
    std::string script;
    std::string type;
    std::string tpl;
    double multiplier = 1.0;
    bool valid = false;   // the item has a gCDamage_PS
};

DamageSource readSource(void* source) {
    DamageSource d;
    d.entity = source;
    if (source == nullptr) {
        return d;
    }
    d.tpl = templateOfEntity(source);
    void* ps = propertySetOf(source, "gCDamage_PS");
    if (ps == nullptr) {
        return d;
    }
    d.valid = true;
    guarded([&] {
        // gCDamage_PS: DamageAmount +0x40, DamageBonus +0x44 (long), DamageMultiplier +0x48 (float), the Agressor proxy +0x30 (RE.md section 27)
        auto* base = static_cast<char*>(ps);
        d.multiplier = static_cast<double>(*reinterpret_cast<float*>(base + 0x48));
        d.attacker = sym().eproxy_get_entity != nullptr ? sym().eproxy_get_entity(base + 0x30) : nullptr;
    });
    if (const auto s = propertyGet(ps, "DamageScript")) {
        d.script = *s;
    }
    if (const auto t = propertyGet(ps, "DamageType")) {
        d.type = combat::damageTypeName(*t);
    }
    return d;
}

// ---- calling the handlers --------------------------------------------------------------------------------------------------------------------

void noteFailure(Handler& h, const std::string& text) {
    ++g_failures;
    g_lastFailure = text;
    if (h.warnings < kMaxWarnings) {
        ++h.warnings;
        spdlog::warn("lua: damage handler {} {}{}", h.id, text, h.warnings == kMaxWarnings ? " (further warnings of this handler are not logged)" : "");
    }
    SPASS_TRACE(trace::Channel::Combat, "handler {} failed: {}", h.id, text);
}

struct ScopedDepth {
    ScopedDepth() { ++t_depth; }
    ~ScopedDepth() { --t_depth; }
};

std::int64_t onDoDamage(void* unit, void* a, void* b, std::int64_t arg,
                        std::int64_t (*original)(void* unit, void* a, void* b, std::int64_t arg)) {
    const auto stock = [&](std::int64_t damage) -> std::int64_t { return original != nullptr ? original(unit, a, b, damage) : 0; };
    if (g_handlers.empty() || t_depth > 0) {
        return stock(arg);
    }
    auto& rt = lua::Runtime::instance();
    auto lock = rt.lock();
    if (!rt.running()) {
        return stock(arg);
    }

    void* sourceEntity = entityOfArg(a, selfOf(unit));
    void* victim = entityOfArg(b, otherOf(unit));
    if (victim == nullptr) {
        return stock(arg);
    }
    DamageSource src = readSource(sourceEntity);
    const combat::Source cls = combat::classifyDamageScript(src.script);
    combat::Hit hit;
    hit.kind = cls.kind;
    hit.spell = cls.spell;
    hit.type = src.type;
    hit.weapon = src.tpl;
    hit.script = src.script;
    hit.attackerIsPlayer = isPlayer(src.attacker);
    hit.victimIsPlayer = isPlayer(victim);
    // unique names are only needed when some handler filters by entity
    bool wantNames = false;
    for (const Handler& h : g_handlers) {
        wantNames = wantNames || !h.filter.attackers.empty() || !h.filter.victims.empty();
    }
    if (wantNames) {
        hit.attacker = src.attacker != nullptr ? entityNameOf(src.attacker) : std::string();
        hit.victim = entityNameOf(victim);
    }

    std::vector<std::uint64_t> due;   // by id: a handler may unregister others while it runs
    for (const Handler& h : g_handlers) {
        if (combat::matches(h.filter, hit)) {
            due.push_back(h.id);
        }
    }
    if (due.empty()) {
        return stock(arg);
    }

    int damage = static_cast<int>(arg);
    const int original_damage = damage;
    int protection = 0;
    bool haveProtection = false;
    if (const int skill = combat::protectionSkill(src.type); skill >= 0 && sym().sk_get_total != nullptr) {
        guarded([&] {
            EntityHolder h{victim};
            protection = sym().sk_get_total(&h, skill);
            haveProtection = true;
        });
    }
    sol::state_view L = rt.lua();
    // a handler may register or remove handlers: look each one up again by id, and never keep a pointer into the vector across a call
    const auto find = [](std::uint64_t id) -> Handler* {
        for (Handler& cand : g_handlers) {
            if (cand.id == id) {
                return &cand;
            }
        }
        return nullptr;
    };
    for (const std::uint64_t id : due) {
        Handler* h = find(id);
        if (h == nullptr || !h->fn.valid()) {
            continue;
        }
        sol::main_protected_function fn = h->fn;
        sol::table t = L.create_table();
        t["attacker"] = makeEntityObject(L, src.attacker);
        t["victim"] = makeEntityObject(L, victim);
        t["source"] = makeEntityObject(L, src.entity);
        t["weapon"] = hit.weapon;
        t["kind"] = hit.kind;
        if (!hit.spell.empty()) {
            t["spell"] = hit.spell;
        }
        t["type"] = hit.type;
        t["damage_script"] = hit.script;
        t["damage"] = damage;
        t["original_damage"] = original_damage;
        t["multiplier"] = src.multiplier;
        if (haveProtection) {
            t["protection"] = protection;
        }
        sol::object result;
        bool ok = false;
        {
            ScopedDepth depth;
            try {
                sol::protected_function_result r = fn(t);
                if (r.valid()) {
                    result = r.get<sol::object>();
                    ok = true;
                } else {
                    const sol::error err = r;
                    if (Handler* again = find(id)) {
                        noteFailure(*again, std::string("failed: ") + err.what());
                    }
                }
            } catch (const std::exception& e) {
                if (Handler* again = find(id)) {
                    noteFailure(*again, std::string("threw: ") + e.what());
                }
            }
        }
        if (!ok) {
            continue;   // the stock damage stays
        }
        const bool isNil = !result.valid() || result.get_type() == sol::type::lua_nil;
        const bool isBool = result.valid() && result.get_type() == sol::type::boolean;
        const bool isNumber = result.valid() && result.get_type() == sol::type::number;
        const combat::Decision d = combat::interpretResult(isNil, isBool, isBool && result.as<bool>(), isNumber, isNumber ? result.as<double>() : 0.0);
        if (d.verdict == combat::Verdict::Invalid) {
            if (Handler* again = find(id)) {
                noteFailure(*again, "returned a bad value: " + d.why);
            }
            continue;
        }
        if (d.verdict == combat::Verdict::Cancel) {
            SPASS_TRACE(trace::Channel::Combat, "hit {} -> {} ({} {}, {}): {} cancelled by handler {}", hit.attacker.empty() ? std::string("-") : hit.attacker, entityNameOf(victim),
                        hit.kind, hit.weapon, hit.type, damage, id);
            return 0;
        }
        if (d.verdict == combat::Verdict::Replace) {
            damage = d.value;
        }
    }
    SPASS_TRACE(trace::Channel::Combat, "hit {} -> {} ({} {}, {}, x{:.2f}): {} -> {}", hit.attacker.empty() ? std::string("-") : hit.attacker, entityNameOf(victim), hit.kind,
                hit.weapon, hit.type, src.multiplier, original_damage, damage);
    return stock(damage);
}

// ---- risen.combat -----------------------------------------------------------------------------------------------------------------------------

bool stringArg(const sol::object& o, std::string& out) {
    if (!o.valid() || o.get_type() != sol::type::string) {
        return false;
    }
    out = o.as<std::string>();
    return true;
}

// A filter value: one string or a list of strings.
bool stringList(const sol::object& o, std::vector<std::string>& out, std::string& why) {
    std::string s;
    if (stringArg(o, s)) {
        out.push_back(s);
        return true;
    }
    if (o.valid() && o.is<sol::table>()) {
        sol::table list = o.as<sol::table>();
        for (const auto& kv : list) {
            if (!stringArg(kv.second, s)) {
                why = "a list must hold strings";
                return false;
            }
            out.push_back(s);
        }
        if (out.empty()) {
            why = "the list is empty";
            return false;
        }
        return true;
    }
    why = "must be a string or a list of strings";
    return false;
}

// attacker / victim: an entity, "player", or a list of those.
bool entityList(const sol::object& o, std::vector<std::string>& names, bool& player, std::string& why) {
    const auto one = [&](const sol::object& v) -> bool {
        std::string s;
        if (stringArg(v, s)) {
            if (s != "player") {
                why = "a string must be \"player\" (or give the entity)";
                return false;
            }
            player = true;
            return true;
        }
        if (v.valid() && v.is<LuaEntity>()) {
            const LuaEntity& e = v.as<LuaEntity>();
            void* p = ptr(e);
            const std::string name = p != nullptr ? entityNameOf(p) : std::string();
            if (name.empty()) {
                why = "the entity is gone or has no name";
                return false;
            }
            names.push_back(name);
            return true;
        }
        why = "must be an entity, \"player\" or a list of those";
        return false;
    };
    if (o.valid() && o.is<sol::table>() && !o.is<LuaEntity>()) {
        sol::table list = o.as<sol::table>();
        int n = 0;
        for (const auto& kv : list) {
            ++n;
            if (!one(kv.second)) {
                return false;
            }
        }
        if (n == 0) {
            why = "the list is empty";
            return false;
        }
        return true;
    }
    return one(o);
}

// risen.combat.on_damage(fn [, filter]) -> handle | nil, reason
Two onDamage(sol::this_state ts, sol::object fnArg, sol::object filterArg) {
    sol::state_view L(ts);
    if (!fnArg.valid() || fnArg.get_type() != sol::type::function) {
        return failResult(L, "the first argument must be a function");
    }
    Handler h;
    if (filterArg.valid() && filterArg.get_type() != sol::type::lua_nil) {
        if (filterArg.get_type() != sol::type::table || filterArg.is<LuaEntity>()) {
            return failResult(L, "the second argument must be a filter table { kind, spell, type, weapon, script, attacker, victim } or nil");
        }
        sol::table filter = filterArg.as<sol::table>();
        for (const auto& kv : filter) {
            std::string key;
            if (!stringArg(kv.first, key) || !combat::validFilterKey(key)) {
                return failResult(L, "unknown filter key (valid: kind, spell, type, weapon, script, attacker, victim)");
            }
            std::string why;
            bool ok = true;
            if (key == "attacker") {
                ok = entityList(kv.second, h.filter.attackers, h.filter.attackerPlayer, why);
            } else if (key == "victim") {
                ok = entityList(kv.second, h.filter.victims, h.filter.victimPlayer, why);
            } else {
                std::vector<std::string>* target = key == "kind"     ? &h.filter.kinds
                                                   : key == "spell"  ? &h.filter.spells
                                                   : key == "type"   ? &h.filter.types
                                                   : key == "weapon" ? &h.filter.weapons
                                                                     : &h.filter.scripts;
                ok = stringList(kv.second, *target, why);
                if (ok && key == "kind") {
                    for (const std::string& k : *target) {
                        if (!combat::validKind(k)) {
                            ok = false;
                            why = "unknown kind '" + k.substr(0, 40) + "' (valid: spell, melee, ranged, other)";
                        }
                    }
                }
                if (ok && key == "spell") {
                    for (const std::string& k : *target) {
                        if (!combat::validSpell(k)) {
                            ok = false;
                            why = "unknown spell '" + k.substr(0, 40) + "' (valid: fireball, frost, missile, inferno)";
                        }
                    }
                }
            }
            if (!ok) {
                return failResult(L, "filter." + key + ": " + why);
            }
        }
    }
    if (g_handlers.size() >= kMaxHandlers) {
        return failResult(L, "too many damage handlers (limit " + std::to_string(kMaxHandlers) + ")");
    }
    // the hook is the registry entry of DoDamage: refuse when a mod took it over with risen.script.override (that would lose its override), put it back when it is gone
    const std::string kind = scripts::hookKind(kScript);
    if (kind == "lua") {
        return failResult(L, "the script DoDamage is overridden by risen.script.override: use that or risen.combat.on_damage, not both");
    }
    if (kind.empty()) {
        if (!scripts::scriptExists(kScript)) {
            return failResult(L, "the engine script DoDamage is not available (script system not ready?)");
        }
        const std::string err = scripts::installNative(kScript, onDoDamage, /*isOverride=*/true);
        if (!err.empty()) {
            return failResult(L, err);
        }
    }
    h.id = g_nextId++;
    h.owner = lua::Runtime::currentOwner();
    h.fn = fnArg.as<sol::main_protected_function>();
    const std::uint64_t id = h.id;
    g_handlers.push_back(std::move(h));
    SPASS_TRACE(trace::Channel::Combat, "damage handler {} registered", id);
    return {sol::make_object(L, static_cast<double>(id)), sol::make_object(L, sol::lua_nil)};
}

// risen.combat.off_damage(handle) -> bool
bool offDamage(sol::object handle) {
    if (!handle.valid() || handle.get_type() != sol::type::number) {
        return false;
    }
    const double d = handle.as<double>();
    if (!(d >= 1.0) || d > 1e15) {
        return false;
    }
    const auto id = static_cast<std::uint64_t>(d);
    for (auto it = g_handlers.begin(); it != g_handlers.end(); ++it) {
        if (it->id == id) {
            g_handlers.erase(it);
            SPASS_TRACE(trace::Channel::Combat, "damage handler {} removed", id);
            return true;
        }
    }
    return false;
}

}  // namespace

void combatShutdown() {
    g_handlers.clear();
}

void combatReleaseOwner(const std::string& mod, std::vector<std::string>& released) {
    if (mod.empty()) {
        return;
    }
    for (auto it = g_handlers.begin(); it != g_handlers.end();) {
        if (it->owner == mod) {
            released.push_back("damage handler " + std::to_string(it->id));
            it = g_handlers.erase(it);
        } else {
            ++it;
        }
    }
}

void bindCombat(sol::state_view lua, sol::table risen) {
    sol::table combatApi = lua.create_table();
    combatApi.set_function("on_damage", onDamage);
    combatApi.set_function("off_damage", offDamage);
    // risen.combat.handlers() -> { handle, ... }: the registered damage handlers
    combatApi.set_function("handlers", [](sol::this_state ts) {
        sol::state_view L(ts);
        sol::table out = L.create_table();
        int n = 0;
        for (const Handler& h : g_handlers) {
            out[++n] = static_cast<double>(h.id);
        }
        return out;
    });
    // risen.combat.failures() -> count, last: how many times a damage handler raised an error or returned a bad value (each time the stock damage was applied)
    combatApi.set_function("failures", []() { return std::make_tuple(g_failures, g_lastFailure); });
    // risen.combat.classify(damage_script) -> kind, spell: how a DamageScript name is sorted (the `kind` / `spell` of a hit)
    combatApi.set_function("classify", [](sol::this_state ts, sol::object script) -> std::tuple<sol::object, sol::object> {
        sol::state_view L(ts);
        std::string s;
        if (!stringArg(script, s)) {
            return {sol::make_object(L, sol::lua_nil), sol::make_object(L, sol::lua_nil)};
        }
        const combat::Source c = combat::classifyDamageScript(s);
        return {sol::make_object(L, c.kind), c.spell.empty() ? sol::make_object(L, sol::lua_nil) : sol::make_object(L, c.spell)};
    });
    risen["combat"] = combatApi;
}

}  // namespace spass::game
