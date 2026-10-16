#include <cstdint>
#include <set>
#include <string>

#include <spdlog/spdlog.h>

#include "common/trace.h"
#include "game/abi.h"
#include "game/bindings.h"
#include "game/event_bus.h"
#include "game/events.h"
#include "game/lua_entity.h"
#include "game/script_bridge.h"
#include "game/symbols.h"
#include "hook/hook_manager.h"

// Entity events: npc.killed / npc.defeated with the killer, entity.spawned, entity.destroyed (docs/RE.md section 27).
//
//   * The game's own scripts `Kill` and `Defeat` (Script_Game.dll, called with self = the attacker, other = the victim) are wrapped to learn who
//     did it; gCQuestManager::OnNPCKilled/OnNPCDefeated (hooked in events.cpp) fire inside them and pick the attacker up from here.
//   * Entity::Spawn (Script.dll) is what scripts, the game's own AI/effect scripts and risen.entity.spawn use to create entities.
//   * eCEntity::Kill (Engine.dll) is the engine's single entry for removing an entity from the world.

namespace spass::game {
namespace {

struct KillContext {
    void* attacker = nullptr;
    void* victim = nullptr;
};
thread_local KillContext* t_kill = nullptr;
std::set<std::string> g_died;           // entities entity.died was reported for (an engine death is reported more than once)
std::set<std::string> g_cheatVictims;   // entities the OnCheatKill script was run on: their death follows from the AI state it sets

Hook<std::int64_t (*)(void*, void*, void*, std::int64_t)> g_scriptKill;
Hook<std::int64_t (*)(void*, void*, void*, std::int64_t)> g_scriptDefeat;
Hook<void* (*)(void*, void*, const float*, bool)> g_spawnAt;
Hook<void* (*)(void*, void*, void*, bool, int)> g_spawnNear;
Hook<void (*)(void*)> g_entityKill;
Hook<bool (*)(void*, void*)> g_doDamage;
Hook<std::int64_t (*)(void*, void*, void*, std::int64_t)> g_scriptCheatKill;
Hook<bool (*)(void*, const void*, int)> g_gameVarSet;
Hook<bool (*)(void*, const void*)> g_gameVarRemove;

// A script argument is an `Entity` holder (its first qword is the eCEntity*) or null (then the unit's self/other entity is meant).
void* entityArg(void* holder, void* unitFallback) {
    void* e = nullptr;
    guarded([&] { e = holder != nullptr ? *static_cast<void**>(holder) : unitFallback; });
    return e;
}

std::int64_t runAttackScript(Hook<std::int64_t (*)(void*, void*, void*, std::int64_t)>& hook, void* unit, void* self, void* other, std::int64_t arg) {
    KillContext ctx;
    guarded([&] {
        ctx.attacker = entityArg(self, sym().unit_get_self != nullptr ? sym().unit_get_self(unit) : nullptr);
        ctx.victim = entityArg(other, sym().unit_get_other != nullptr ? sym().unit_get_other(unit) : nullptr);
    });
    KillContext* previous = t_kill;
    t_kill = &ctx;
    const std::int64_t r = hook.original(unit, self, other, arg);
    t_kill = previous;
    return r;
}

std::int64_t detourScriptCheatKill(void* unit, void* self, void* other, std::int64_t arg) {
    void* victim = nullptr;
    guarded([&] { victim = entityArg(other, sym().unit_get_other != nullptr ? sym().unit_get_other(unit) : nullptr); });
    const std::string name = victim != nullptr ? entityNameOf(victim) : std::string();
    if (!name.empty()) {
        g_cheatVictims.insert(name);  // the script starts the death state synchronously: Kill runs inside the call below
    }
    const std::int64_t r = g_scriptCheatKill.original(unit, self, other, arg);
    if (r == 0 && !name.empty()) {
        g_cheatVictims.erase(name);  // the cheat did nothing (player, not an NPC)
    }
    return r;
}

std::int64_t detourScriptKill(void* unit, void* self, void* other, std::int64_t arg) { return runAttackScript(g_scriptKill, unit, self, other, arg); }
std::int64_t detourScriptDefeat(void* unit, void* self, void* other, std::int64_t arg) { return runAttackScript(g_scriptDefeat, unit, self, other, arg); }

std::string kindOf(void* entity) {
    const Symbols& s = sym();
    std::string kind = "other";
    guarded([&] {
        EntityHolder h{entity};
        if (s.ent_is_player != nullptr && s.ent_is_player(&h)) {
            kind = "player";
        } else if (s.ent_is_npc != nullptr && s.ent_is_npc(&h)) {
            kind = "npc";
        } else if (s.ent_is_item != nullptr && s.ent_is_item(&h)) {
            kind = "item";
        }
    });
    return kind;
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

// `byValueTemplate` is a Template (an eCTemplateEntityProxy) that the callee destroys: its name is read first.
std::string templateOfProxy(void* proxy) {
    std::string tpl;
    guarded([&] {
        if (sym().tpl_proxy_get_entity != nullptr && sym().tple_get_name != nullptr) {
            if (void* te = sym().tpl_proxy_get_entity(proxy)) {
                if (const void* n = sym().tple_get_name(te)) {
                    tpl = sym().bstring_get_text(n);
                }
            }
        }
    });
    return tpl;
}

// Effect entities (water splashes, particles, ...) come and go by the hundred and are not gameplay.
bool isEffect(const std::string& tpl) { return tpl.rfind("VFX_", 0) == 0 || tpl.rfind("PFX_", 0) == 0; }

void postSpawned(void* entity, const std::string& tpl) {
    if (entity != nullptr && !isEffect(tpl) && bus::active() && bus::wanted("entity.spawned")) {
        bus::post("entity.spawned", {bus::capture(entity), tpl});
    }
}

thread_local int t_spawnNearDepth = 0;  // Spawn(near) is built on Spawn(at): the inner one must not report the same entity again

void* detourSpawnAt(void* ret, void* tplObj, const float* pose, bool temporary) {
    const std::string tpl = bus::wanted("entity.spawned") ? templateOfProxy(tplObj) : std::string();
    void* r = g_spawnAt.original(ret, tplObj, pose, temporary);
    if (t_spawnNearDepth == 0) {
        guarded([&] { postSpawned(*static_cast<void**>(ret), tpl); });
    }
    return r;
}

void* detourSpawnNear(void* ret, void* tplObj, void* entityByValue, bool temporary, int direction) {
    const std::string tpl = bus::wanted("entity.spawned") ? templateOfProxy(tplObj) : std::string();
    ++t_spawnNearDepth;
    void* r = g_spawnNear.original(ret, tplObj, entityByValue, temporary, direction);
    --t_spawnNearDepth;
    guarded([&] { postSpawned(*static_cast<void**>(ret), tpl); });
    return r;
}

// gCDamage_PS::DoDamage(victim): a weapon (or fist, or the floor) hits `victim`: the damage script runs from here. The attacker is the damage
// property set's Agressor proxy (+0x30), the raw damage (DamageAmount + DamageBonus) * DamageMultiplier; what the victim really lost is read from its hit points.
bool detourDoDamage(void* self, void* victim) {
    if (victim == nullptr || !bus::active() || !bus::wanted("entity.damaged")) {
        return g_doDamage.original(self, victim);
    }
    int before = -1;
    int raw = 0;
    void* attacker = nullptr;
    guarded([&] {
        EntityHolder h{victim};
        before = sym().sk_get_hp != nullptr ? sym().sk_get_hp(&h) : -1;
        auto* base = static_cast<char*>(self);
        raw = static_cast<int>(static_cast<float>(*reinterpret_cast<int*>(base + 0x44) + *reinterpret_cast<int*>(base + 0x40)) * *reinterpret_cast<float*>(base + 0x48));
        attacker = sym().eproxy_get_entity != nullptr ? sym().eproxy_get_entity(base + 0x30) : nullptr;
    });
    const bool r = g_doDamage.original(self, victim);
    int after = before;
    guarded([&] {
        EntityHolder h{victim};
        after = sym().sk_get_hp != nullptr ? sym().sk_get_hp(&h) : before;
    });
    if (before >= 0 && after < before) {  // a hit that took no hit points (a corpse, a parried blow) is not damage
        bus::post("entity.damaged", {bus::capture(victim), bus::capture(attacker), static_cast<double>(before - after), static_cast<double>(raw)});
    }
    return r;
}

void detourEntityKill(void* self) {
    if (self != nullptr) {
        noteEntityRemoved(self);  // Lua handles to this entity are stale from now on
        focusEntityKilled(self);
        aiEntityKilled(self);
        const bool wantDestroyed = bus::active() && bus::wanted("entity.destroyed");
        if (wantDestroyed || !g_died.empty()) {
            const std::string name = entityNameOf(self);
            g_died.erase(name);
            if (wantDestroyed) {
                const std::string tpl = templateOfEntity(self);
                if (!isEffect(tpl)) {
                    bus::post("entity.destroyed", {name, kindOf(self), tpl});
                }
            }
        }
    }
    g_entityKill.original(self);
}

// gamevar.changed(name, value, previous): gCInfoSystem::SetGameVariable / RemoveGameVariable (Game.dll). The dialogue commands SetGameEvent (a variable with
// value 0) / ClearGameEvent call these members directly; the Script.dll statics (InfoSystem::Set/RemoveGameVariable: risen.gamevar.set/remove, the game's scripts)
// are thin wrappers around them. `value` is nil for a removed variable, `previous` nil for a new one.
bool detourGameVarSet(void* self, const void* name, int value) {
    const bool want = name != nullptr && bus::active() && bus::wanted("gamevar.changed");
    std::string key;
    int previous = 0;
    bool had = false;
    if (want) {
        guarded([&] {
            key = sym().bstring_get_text(name);
            had = sym().gamevar_get != nullptr && sym().gamevar_get(name, &previous);
        });
    }
    const bool ok = g_gameVarSet.original(self, name, value);
    if (want && ok && !key.empty() && (!had || previous != value)) {
        bus::post("gamevar.changed", {key, static_cast<double>(value), had ? bus::Arg(static_cast<double>(previous)) : bus::Arg()});
    }
    return ok;
}

bool detourGameVarRemove(void* self, const void* name) {
    const bool want = name != nullptr && bus::active() && bus::wanted("gamevar.changed");
    std::string key;
    int previous = 0;
    bool had = false;
    if (want) {
        guarded([&] {
            key = sym().bstring_get_text(name);
            had = sym().gamevar_get != nullptr && sym().gamevar_get(name, &previous);
        });
    }
    const bool ok = g_gameVarRemove.original(self, name);
    if (want && ok && had && !key.empty()) {
        bus::post("gamevar.changed", {key, bus::Arg(), static_cast<double>(previous)});
    }
    return ok;
}

}  // namespace

void resetEntityEvents() {
    g_died.clear();
    g_cheatVictims.clear();
}

void reportNpcKilled(void* victim) {
    void* attacker = currentAttacker();
    if (bus::active() && bus::wanted("npc.killed")) {
        bus::post("npc.killed", {bus::capture(victim), bus::capture(attacker)});
    }
    if (victim == nullptr || !bus::active() || !bus::wanted("entity.died")) {
        return;
    }
    bool dead = false;
    guarded([&] {
        EntityHolder h{victim};
        dead = sym().ent_is_dead != nullptr && sym().ent_is_dead(&h);
    });
    const std::string name = entityNameOf(victim);
    if (!dead || !g_died.insert(name).second) {
        return;  // not dead yet (a later report says so) or already reported
    }
    const bool cheat = g_cheatVictims.erase(name) > 0;
    const char* cause = cheat ? "cheat" : scripts::inLuaScriptCall() ? "script" : attacker != nullptr ? "combat" : "other";
    bus::post("entity.died", {bus::capture(victim), bus::capture(attacker), std::string(cause)});
}

void reportNpcDefeated(void* victim) {
    void* attacker = currentAttacker();
    if (bus::active() && bus::wanted("npc.defeated")) {
        bus::post("npc.defeated", {bus::capture(victim), bus::capture(attacker)});
    }
    if (victim != nullptr && bus::active() && bus::wanted("entity.defeated")) {
        bus::post("entity.defeated", {bus::capture(victim), bus::capture(attacker)});
    }
}

// The attacker of the Kill/Defeat script that is running right now (nullptr outside of one or when the script has no attacker).
void* currentAttacker() { return t_kill != nullptr ? t_kill->attacker : nullptr; }

void installEntityEventHooks() {
    const Symbols& s = sym();
    if (s.gdmg_do_damage != nullptr && !g_doDamage.install("gCDamage_PS::DoDamage", s.gdmg_do_damage, &detourDoDamage)) {
        spdlog::warn("event hook 'gCDamage_PS::DoDamage' could not be installed");
    }
    if (s.gamevar_sys_set != nullptr && !g_gameVarSet.install("gCInfoSystem::SetGameVariable", s.gamevar_sys_set, &detourGameVarSet)) {
        spdlog::warn("event hook 'gCInfoSystem::SetGameVariable' could not be installed");
    }
    if (s.gamevar_sys_remove != nullptr && !g_gameVarRemove.install("gCInfoSystem::RemoveGameVariable", s.gamevar_sys_remove, &detourGameVarRemove)) {
        spdlog::warn("event hook 'gCInfoSystem::RemoveGameVariable' could not be installed");
    }
    if (s.eent_kill != nullptr) {
        if (!g_entityKill.install("eCEntity::Kill", s.eent_kill, &detourEntityKill)) {
            spdlog::warn("event hook 'eCEntity::Kill' could not be installed");
        }
    }
}

// Needs Script.dll and Script_Game.dll (loaded by gCScriptAdmin::PostInitialize), so it runs from the PostInitialize hook; the caller enables the hooks.
void installScriptEventHooks() {
    const Symbols& s = sym();
    const auto install = [](auto& hook, const char* name, auto target, auto detour) {
        if (target == nullptr) {
            spdlog::warn("event hook '{}' unavailable (engine symbol missing): its Lua events will not fire", name);
        } else if (!hook.install(name, target, detour)) {
            spdlog::warn("event hook '{}' could not be installed", name);
        }
    };
    install(g_scriptKill, "script Kill", s.sg_kill, &detourScriptKill);
    install(g_scriptDefeat, "script Defeat", s.sg_defeat, &detourScriptDefeat);
    install(g_scriptCheatKill, "script OnCheatKill", s.sg_cheat_kill, &detourScriptCheatKill);
    install(g_spawnAt, "Entity::Spawn(at)", s.ent_spawn, &detourSpawnAt);
    install(g_spawnNear, "Entity::Spawn(near)", s.ent_spawn_near, &detourSpawnNear);
    installVoiceHooks();  // Entity::GetSampleTitle: custom voice lines (lua_voice.cpp)
}

}  // namespace spass::game
