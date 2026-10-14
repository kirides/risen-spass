#include "game/events.h"

#include <string>

#include <spdlog/spdlog.h>

#include "game/abi.h"
#include "game/lua_entity.h"
#include "game/lua_save.h"
#include "game/symbols.h"
#include "hook/hook_manager.h"
#include "lua/runtime.h"

// Engine lifecycle events for Lua (docs/RE.md §14, docs/LUA_API.md "Events"). Every hook calls the original
// first or last as noted, never changes its behaviour, and fires the Lua event only when a mod listens.
//
//   session.start(mode)               gCSession::Start        "new" | "load" | "other"; runtime content may have
//                                                             been reset by the engine: (re)create it here
//   session.stop()                    gCSession::Stop
//   world.changed(name)               gCWorld::Activate
//   save.before(name) / save.after(name, ok)   gCSaveGameManager::Save (private core of every save path)
//   load.before(name) / load.after(name, ok)   gCSaveGameManager::Load: *before* the engine reads quests/infos, so
//                                                             this is where runtime quests/infos must be recreated
//   quest.status_changed(name, status, previous)   gCQuest::SetStatus
//   npc.killed(entity) / npc.defeated(entity)      gCQuestManager::OnNPCKilled / OnNPCDefeated
//   info.started(name, owner) / info.finished(name, owner)   gCInfoSystem::ExecuteDialogInfo / OnInfoFinished
//   item.picked_up(owner, template, amount)        gCInventory_PS::InsertStack (an item entity from the world)

namespace spass::game {
namespace {

Hook<bool (*)(void*, const void*, const void*, unsigned long, bool, void*)> g_save;
Hook<bool (*)(void*, const void*, void*)> g_load;
Hook<void (*)(void*, int)> g_sessionStart;
Hook<void (*)(void*)> g_sessionStop;
Hook<bool (*)(void*)> g_worldActivate;
Hook<bool (*)(void*, const int*, bool, bool)> g_questStatus;
Hook<bool (*)(void*, void*)> g_npcKilled;
Hook<bool (*)(void*, void*)> g_npcDefeated;
Hook<void (*)(void*, const void*)> g_infoFinished;
Hook<bool (*)(void*, void*)> g_infoStarted;
Hook<int (*)(void*, void*)> g_itemPickedUp;

constexpr const char* kQuestStatus[] = {"Open", "Running", "Success", "Failed", "Obsolete", "Cancelled", "Lost", "Won"};

const char* questStatusName(int s) { return s >= 0 && s < 8 ? kQuestStatus[s] : "Unknown"; }

template <class... Args>
void fire(const char* event, Args&&... args) {
    auto& rt = lua::Runtime::instance();
    if (rt.running() && rt.hasHandlers(event)) {
        rt.emit(event, args...);
    }
}

std::string bstr(const void* s) { return s != nullptr ? std::string(sym().bstring_get_text(s)) : std::string(); }

// Name of a gCQuest (virtual GetName, slot +0x100: see lua_quest.cpp).
std::string questName(void* quest) {
    std::string name;
    guarded([&] {
        OutString out;
        reinterpret_cast<void (*)(void*, void*)>((*reinterpret_cast<void***>(quest))[0x100 / 8])(quest, out.ptr());
        name = out.take();
    });
    return name;
}

std::string infoName(void* info) {
    std::string n;
    guarded([&] { n = bstr(sym().info_access_name(info)); });
    return n;
}
std::string infoOwner(void* info) {
    std::string n;
    guarded([&] { n = bstr(sym().info_access_owner(info)); });
    return n;
}

// ---- save / load ----------------------------------------------------------------------------------------------

bool detourSave(void* self, const void* name, const void* title, unsigned long flags, bool overwrite, void* saveGame) {
    const std::string n = bstr(name);
    fire("save.before", n);
    std::string data;
    try {
        data = savestore::serialize();  // after save.before: handlers may still update their data
    } catch (const std::exception& e) {
        spdlog::error("save: serializing mod data failed: {}", e.what());
    }
    const bool ok = g_save.original(self, name, title, flags, overwrite, saveGame);
    if (ok) {
        if (data.empty()) {
            savestore::removeSidecar(n);  // never leave the data of an older save with the same name behind
        } else {
            savestore::writeSidecar(n, data);
        }
        spdlog::info("save: saved '{}'", n);
    }
    fire("save.after", n, ok);
    return ok;
}

bool detourLoad(void* self, const void* name, void* stream) {
    const std::string n = bstr(name);
    spdlog::info("load: loading '{}'", n);
    bumpEntityGeneration();
    savestore::beginLoad(n);   // mods already see the data of the savegame in load.before
    fire("load.before", n);
    const bool ok = g_load.original(self, name, stream);
    bumpEntityGeneration();
    if (ok) {
        savestore::commitLoad();
    } else {
        savestore::restoreBackup();
        spdlog::warn("load: '{}' failed, mod data restored", n);
    }
    fire("load.after", n, ok);
    return ok;
}

// ---- session / world ------------------------------------------------------------------------------------------

void detourSessionStart(void* self, int mode) {
    bumpEntityGeneration();
    if (mode == 0) {
        savestore::clear();  // a new game starts without mod data
    }
    g_sessionStart.original(self, mode);
    bumpEntityGeneration();
    const char* name = mode == 0 ? "new" : mode == 1 ? "load" : "other";
    spdlog::info("session: start ({})", name);
    fire("session.start", std::string(name));
}

void detourSessionStop(void* self) {
    fire("session.stop");
    g_sessionStop.original(self);
    bumpEntityGeneration();
}

bool detourWorldActivate(void* self) {
    const bool ok = g_worldActivate.original(self);
    bumpEntityGeneration();
    std::string name;
    guarded([&] {
        if (sym().element_base_name != nullptr) {
            OutString out;
            sym().element_base_name(self, out.ptr());
            name = out.take();
        }
    });
    spdlog::info("world: activated '{}'", name);
    fire("world.changed", name);
    return ok;
}

// ---- gameplay -------------------------------------------------------------------------------------------------

bool detourQuestStatus(void* self, const int* status, bool a, bool b) {
    int previous = -1;
    int next = -1;
    std::string name;
    guarded([&] {
        previous = *reinterpret_cast<const int*>(static_cast<const char*>(self) + 200);  // gCQuest::Status (+0xC8)
        next = status != nullptr ? *status : -1;
        name = questName(self);
    });
    const bool ok = g_questStatus.original(self, status, a, b);
    if (ok) {
        fire("quest.status_changed", name, std::string(questStatusName(next)), std::string(questStatusName(previous)));
    }
    return ok;
}

// Entity events hand the entity itself to Lua, so the handle object is made under the Lua lock.
void fireEntity(const char* event, void* entity) {
    auto& rt = lua::Runtime::instance();
    if (!rt.running() || !rt.hasHandlers(event)) {
        return;
    }
    auto lock = rt.lock();
    sol::object e = makeEntityObject(rt.lua(), entity);
    rt.emit(event, e);
}

bool detourNpcKilled(void* self, void* entity) {
    const bool r = g_npcKilled.original(self, entity);
    fireEntity("npc.killed", entity);
    return r;
}

bool detourNpcDefeated(void* self, void* entity) {
    const bool r = g_npcDefeated.original(self, entity);
    fireEntity("npc.defeated", entity);
    return r;
}

// gCRuntimeInfo: the gCInfo it runs is at +0x10 (decompilation of gCInfoSystem::OnInfoFinished).
void detourInfoFinished(void* self, const void* runtimeInfo) {
    std::string name, owner;
    guarded([&] {
        if (runtimeInfo != nullptr) {
            void* info = *reinterpret_cast<void* const*>(static_cast<const char*>(runtimeInfo) + 0x10);
            if (info != nullptr) {
                name = infoName(info);
                owner = infoOwner(info);
            }
        }
    });
    g_infoFinished.original(self, runtimeInfo);  // may destroy the runtime info: read it first
    if (!name.empty()) {
        fire("info.finished", name, owner);
    }
}

bool detourInfoStarted(void* self, void* info) {
    const std::string name = info != nullptr ? infoName(info) : std::string();
    const std::string owner = info != nullptr ? infoOwner(info) : std::string();
    const bool ok = g_infoStarted.original(self, info);
    if (ok && !name.empty()) {
        fire("info.started", name, owner);
    }
    return ok;
}

// gCInventory_PS::InsertStack(item entity): the inventory takes the entity and kills it, so everything the
// event reports is read beforehand. Template name: entity vtable +0x170 (template entity) -> GetName (+0x178);
// amount: gCItem_PS (property set 0x27) +0x20.
int detourItemPickedUp(void* self, void* item) {
    std::string tpl;
    int amount = 0;
    void* owner = nullptr;
    guarded([&] {
        if (item == nullptr) {
            return;
        }
        void** vt = *reinterpret_cast<void***>(item);
        if (void* tplEntity = reinterpret_cast<void* (*)(void*)>(vt[0x170 / 8])(item)) {
            void** tvt = *reinterpret_cast<void***>(tplEntity);
            tpl = bstr(reinterpret_cast<const void* (*)(void*)>(tvt[0x178 / 8])(tplEntity));
        }
        if (void* ps = reinterpret_cast<void* (*)(void*, int)>(vt[0x198 / 8])(item, 0x27)) {
            amount = *reinterpret_cast<int*>(static_cast<char*>(ps) + 0x20);
        }
        if (sym().ps_get_entity != nullptr) {
            owner = sym().ps_get_entity(self);
        }
    });
    const int index = g_itemPickedUp.original(self, item);
    auto& rt = lua::Runtime::instance();
    if (index >= 0 && rt.running() && rt.hasHandlers("item.picked_up")) {
        auto lock = rt.lock();
        sol::object o = makeEntityObject(rt.lua(), owner);
        rt.emit("item.picked_up", o, tpl, amount);
    }
    return index;
}

template <class H, class Fn>
void installOptional(H& hook, const char* name, Fn target, Fn detour) {
    if (target == nullptr) {
        spdlog::warn("event hook '{}' unavailable (engine symbol missing): its Lua event will not fire", name);
        return;
    }
    if (!hook.install(name, target, detour)) {
        spdlog::warn("event hook '{}' could not be installed", name);
    }
}

}  // namespace

void installEventHooks() {
    const Symbols& s = sym();
    installOptional(g_save, "gCSaveGameManager::Save", s.save_write, &detourSave);
    installOptional(g_load, "gCSaveGameManager::Load", s.save_load, &detourLoad);
    installOptional(g_sessionStart, "gCSession::Start", s.session_start, &detourSessionStart);
    installOptional(g_sessionStop, "gCSession::Stop", s.session_stop, &detourSessionStop);
    installOptional(g_worldActivate, "gCWorld::Activate", s.world_activate, &detourWorldActivate);
    installOptional(g_questStatus, "gCQuest::SetStatus", s.quest_set_status, &detourQuestStatus);
    installOptional(g_npcKilled, "gCQuestManager::OnNPCKilled", s.qm_on_npc_killed, &detourNpcKilled);
    installOptional(g_npcDefeated, "gCQuestManager::OnNPCDefeated", s.qm_on_npc_defeated, &detourNpcDefeated);
    installOptional(g_infoFinished, "gCInfoSystem::OnInfoFinished", s.is_on_info_finished, &detourInfoFinished);
    installOptional(g_infoStarted, "gCInfoSystem::ExecuteDialogInfo", s.is_execute_info, &detourInfoStarted);
    installOptional(g_itemPickedUp, "gCInventory_PS::InsertStack", s.inv_insert_stack, &detourItemPickedUp);
}

}  // namespace spass::game
