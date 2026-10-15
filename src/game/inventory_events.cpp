#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <spdlog/spdlog.h>

#include "common/trace.h"
#include "game/abi.h"
#include "game/event_bus.h"
#include "game/events.h"
#include "game/symbols.h"
#include "hook/hook_manager.h"

// Inventory events: inventory.added / removed / changed, item.picked_up / equipped / unequipped / consumed (docs/RE.md section 27).
//
// Every change of an inventory ends in one of the exported gCInventory_PS operations (CreateItems, DeleteItems, TransferItemsTo, LootSelections,
// DropItemsFromStack, InsertStack, ...), and those end in the private stack list at gCInventory_PS+0x30 (array pointer, count, stride 0x60; the
// amount is the low 21 bits of the qword at +0x58, the template proxy sits at +8). The stack list only announces a change *after* it was made
// and never says what the amount was before, and a loot or a trade is many small changes, so the hooks work with scopes:
//
//   * the outermost hooked operation opens a scope and names the cause ("script", "loot", "dialogue", ...);
//   * every hooked operation, nested or not, snapshots the inventories it is about to touch (template -> total amount) the first time;
//   * when the outermost operation returns, the touched inventories are compared with their snapshots: one event per template and
//     inventory whose total changed, exactly once, with the counterpart inventory of a transfer as `other`.
//
// The listener callbacks of the stack list (OnStackCreate/OnStackDelete) catch the changes that no hooked operation announced (a fallback; the cause is
// "other"). Events are queued (event_bus.h) and reach Lua at the end of the frame.

namespace spass::game {
namespace {

constexpr std::size_t kListArray = 0x30, kListCount = 0x38, kStackStride = 0x60;
constexpr std::size_t kStackTemplateProxy = 0x08, kStackEquipSlot = 0x48, kStackAmount = 0x58, kStackType = 0x38;
// gEStackType 2: the temporary copies the animations hold in the hands (the anvil's hammer, the recipe, the result item: AssureItems(.., 1, 2)); they are not
// possessions (HasRecipes skips them too), so they are neither counted nor reported (docs/RE.md section 30).
constexpr int kStackTypeTemporary = 2;
constexpr std::uint32_t kAmountMask = 0x1fffff;
constexpr std::size_t kListenerOffset = 0x20;  // the gCInventoryListener base inside gCInventory_PS

using Snap = std::map<void*, int>;  // template entity -> total amount in all stacks

struct Touched {
    void* ps;
    Snap before;
};

struct Scope {
    const char* cause = "";
    std::vector<Touched> touched;
};

thread_local Scope* t_scope = nullptr;

std::mutex g_namesMutex;
std::unordered_map<void*, std::string> g_templateNames;   // template entity -> name (templates live as long as the process)

std::string templateName(void* tpl) {
    if (tpl == nullptr || sym().tple_get_name == nullptr) {
        return {};
    }
    {
        std::lock_guard lock(g_namesMutex);
        const auto it = g_templateNames.find(tpl);
        if (it != g_templateNames.end()) {
            return it->second;
        }
    }
    std::string name;
    guarded([&] {
        if (const void* n = sym().tple_get_name(tpl)) {
            name = sym().bstring_get_text(n);
        }
    });
    if (!name.empty()) {
        std::lock_guard lock(g_namesMutex);
        g_templateNames[tpl] = name;
    }
    return name;
}

bool wantItemEvent(const char* event) { return bus::active() && bus::wanted(event); }

char* stackAt(void* ps, int index) {
    char* arr = *reinterpret_cast<char**>(static_cast<char*>(ps) + kListArray);
    const int n = *reinterpret_cast<int*>(static_cast<char*>(ps) + kListCount);
    if (arr == nullptr || index < 0 || index >= n) {
        return nullptr;
    }
    return arr + static_cast<std::size_t>(index) * kStackStride;
}

void* stackTemplate(char* stack) {
    return stack != nullptr && sym().tpl_proxy_get_entity != nullptr ? sym().tpl_proxy_get_entity(stack + kStackTemplateProxy) : nullptr;
}

int stackAmount(const char* stack) { return static_cast<int>(*reinterpret_cast<const std::uint32_t*>(stack + kStackAmount) & kAmountMask); }

Snap collect(void* ps) {
    Snap s;
    guarded([&] {
        char* arr = *reinterpret_cast<char**>(static_cast<char*>(ps) + kListArray);
        const int n = *reinterpret_cast<int*>(static_cast<char*>(ps) + kListCount);
        if (arr == nullptr || n <= 0 || n > 8192) {
            return;
        }
        for (int i = 0; i < n; ++i) {
            char* st = arr + static_cast<std::size_t>(i) * kStackStride;
            const int amount = stackAmount(st);
            if (amount <= 0 || *reinterpret_cast<const int*>(st + kStackType) == kStackTypeTemporary) {
                continue;
            }
            if (void* tpl = stackTemplate(st)) {
                s[tpl] += amount;
            }
        }
    });
    return s;
}

void* ownerOf(void* ps) {
    void* owner = nullptr;
    if (sym().ps_get_entity != nullptr) {
        guarded([&] { owner = sym().ps_get_entity(ps); });
    }
    return owner;
}

bool listening() {
    return bus::active() && (bus::wanted("inventory.added") || bus::wanted("inventory.removed") || bus::wanted("inventory.changed"));
}

struct Delta {
    void* ps;
    void* tpl;
    int delta;
    int total;
};

void finish(Scope& scope) {
    std::vector<Delta> deltas;
    for (const Touched& t : scope.touched) {
        const Snap after = collect(t.ps);
        for (const auto& [tpl, amount] : after) {
            const auto b = t.before.find(tpl);
            const int before = b != t.before.end() ? b->second : 0;
            if (amount != before) {
                deltas.push_back({t.ps, tpl, amount - before, amount});
            }
        }
        for (const auto& [tpl, before] : t.before) {
            if (after.find(tpl) == after.end() && before != 0) {
                deltas.push_back({t.ps, tpl, -before, 0});
            }
        }
    }
    for (const Delta& d : deltas) {
        const std::string tplName = templateName(d.tpl);
        if (tplName.empty()) {
            continue;
        }
        bus::Arg other;
        for (const Delta& o : deltas) {
            if (o.tpl == d.tpl && o.ps != d.ps && (o.delta < 0) != (d.delta < 0)) {
                other = bus::capture(ownerOf(o.ps));
                break;
            }
        }
        const bus::EntityRef owner = bus::capture(ownerOf(d.ps));
        const double count = d.delta < 0 ? -d.delta : d.delta;
        const char* kind = d.delta > 0 ? "inventory.added" : "inventory.removed";
        bus::post(kind, {owner, tplName, count, static_cast<double>(d.total), std::string(scope.cause), other});
        bus::post("inventory.changed", {owner, tplName, static_cast<double>(d.delta), static_cast<double>(d.total), std::string(scope.cause), other});
    }
}

// RAII scope of one hooked operation. The outermost one owns the Scope; touch() snapshots an inventory once, before the operation changes it.
class Op {
public:
    explicit Op(const char* cause) {
        if (t_scope == nullptr && listening()) {
            scope_.cause = cause;
            t_scope = &scope_;
            outer_ = true;
        }
    }
    ~Op() {
        if (outer_) {
            t_scope = nullptr;
            finish(scope_);
        }
    }
    Op(const Op&) = delete;
    Op& operator=(const Op&) = delete;

    static void touch(void* ps) {
        if (t_scope == nullptr || ps == nullptr) {
            return;
        }
        for (const Touched& t : t_scope->touched) {
            if (t.ps == ps) {
                return;
            }
        }
        t_scope->touched.push_back({ps, collect(ps)});
    }

private:
    Scope scope_;
    bool outer_ = false;
};

bool touched(void* ps) {
    if (t_scope == nullptr) {
        return false;
    }
    for (const Touched& t : t_scope->touched) {
        if (t.ps == ps) {
            return true;
        }
    }
    return false;
}

// ---- hooks ---------------------------------------------------------------------------------------------------------

Hook<int (*)(void*, const void*, unsigned long, int)> g_createId;
Hook<int (*)(void*, const void*, unsigned long, int)> g_createTpl;
Hook<int (*)(void*, const void*, unsigned long, int)> g_assure;
Hook<int (*)(void*, void*)> g_insertStack;
Hook<bool (*)(void*, int, unsigned long)> g_deleteItems;
Hook<bool (*)(void*, int, unsigned long)> g_consume;
Hook<bool (*)(void*, int)> g_deleteStack;
Hook<void (*)(void*, int, const int*, bool)> g_setAmount;
Hook<void (*)(void*, int)> g_setMoney;
Hook<bool (*)(void*, void*, int, int, int)> g_transferItems;
Hook<bool (*)(void*, void*, int, int)> g_transferStack;
Hook<bool (*)(void*, void*, int)> g_transferAll;
Hook<bool (*)(void*, void*)> g_loot;
Hook<bool (*)(void*, void*)> g_trade;
Hook<void* (*)(void*, int, int, const float*)> g_dropItems;
Hook<void* (*)(void*, int, const float*)> g_dropStack;
Hook<bool (*)(void*, int, int)> g_equip;
Hook<bool (*)(void*, int, bool)> g_unequip;
Hook<void (*)(void*, int, const void*)> g_onCreate;
Hook<void (*)(void*, int, const void*)> g_onDelete;
Hook<bool (*)(void*, void*, bool)> g_cmdCreate;
Hook<bool (*)(void*, void*, bool)> g_cmdDestroy;
Hook<bool (*)(void*, void*, bool)> g_cmdGive;
Hook<void (*)(void*)> g_merge;
Hook<bool (*)(void*, int, unsigned long, int*)> g_split;
Hook<int (*)(void*, const void*, int)> g_synthesize;

int detourCreateId(void* self, const void* id, unsigned long n, int type) {
    Op op("script");
    Op::touch(self);
    return g_createId.original(self, id, n, type);
}
int detourCreateTpl(void* self, const void* tpl, unsigned long n, int type) {
    Op op("script");
    Op::touch(self);
    return g_createTpl.original(self, tpl, n, type);
}
int detourAssure(void* self, const void* id, unsigned long n, int type) {
    Op op("script");
    Op::touch(self);
    return g_assure.original(self, id, n, type);
}
bool detourDeleteItems(void* self, int stack, unsigned long n) {
    Op op("script");
    Op::touch(self);
    return g_deleteItems.original(self, stack, n);
}
bool detourDeleteStack(void* self, int stack) {
    Op op("script");
    Op::touch(self);
    return g_deleteStack.original(self, stack);
}
void detourSetAmount(void* self, int stack, const int* amount, bool notify) {
    Op op("script");
    Op::touch(self);
    g_setAmount.original(self, stack, amount, notify);
}
void detourSetMoney(void* self, int amount) {
    Op op("script");
    Op::touch(self);
    g_setMoney.original(self, amount);
}
bool detourTransferItems(void* self, void* target, int stack, int count, int type) {
    Op op("script");
    Op::touch(self);
    Op::touch(target);
    return g_transferItems.original(self, target, stack, count, type);
}
bool detourTransferStack(void* self, void* target, int stack, int type) {
    Op op("script");
    Op::touch(self);
    Op::touch(target);
    return g_transferStack.original(self, target, stack, type);
}
bool detourTransferAll(void* self, void* target, int type) {
    Op op("script");
    Op::touch(self);
    Op::touch(target);
    return g_transferAll.original(self, target, type);
}
bool detourLoot(void* self, void* from) {
    Op op("loot");
    Op::touch(self);
    Op::touch(from);
    return g_loot.original(self, from);
}
bool detourTrade(void* self, void* with) {
    Op op("trade");
    Op::touch(self);
    Op::touch(with);
    return g_trade.original(self, with);
}
void* detourDropItems(void* self, int stack, int count, const float* pose) {
    Op op("drop");
    Op::touch(self);
    return g_dropItems.original(self, stack, count, pose);
}
void* detourDropStack(void* self, int stack, const float* pose) {
    Op op("drop");
    Op::touch(self);
    return g_dropStack.original(self, stack, pose);
}

// Rearranging stacks changes no totals; the scope only keeps the listener fallback from reporting the stacks that disappear or appear.
void detourMerge(void* self) {
    Op op("rearrange");
    Op::touch(self);
    g_merge.original(self);
}
bool detourSplit(void* self, int stack, unsigned long n, int* newStack) {
    Op op("rearrange");
    Op::touch(self);
    return g_split.original(self, stack, n, newStack);
}

// PSInventory::Synthesize(recipe, count) (Script.dll): the one function every crafting station calls once it let the player craft (anvil, forge,
// grindstone, goldsmith, alchemy, cool weapon; also OnPlayerSynthRecipe): deletes `count` times the ingredients, creates `count` result items. It names the
// cause "crafting" for the inventory events inside and posts one `crafting(crafter, recipe, result, count, ingredients)` event after them (docs/RE.md
// section 30). The recipe is read beforehand: gCRecipe_PS (property set 0x5a) has the ingredient array at +0x30 (stride 0x18, proxy at +8, amount at +0x10)
// and the result item proxy at +0x50.
int detourSynthesize(void* self, const void* recipe, int count) {
    const bool want = wantItemEvent("crafting");
    std::string recipeName, resultName;
    bus::ItemList ingredients;
    void* crafterEntity = nullptr;
    void* inventoryPs = nullptr;
    guarded([&] {
        void* const* holder = static_cast<void* const*>(self);
        crafterEntity = holder != nullptr ? *holder : nullptr;
        if (crafterEntity != nullptr) {
            inventoryPs = reinterpret_cast<void* (*)(void*, int)>((*reinterpret_cast<void***>(crafterEntity))[0x198 / 8])(crafterEntity, 0x1f);
        }
        void* tpl = recipe != nullptr ? *static_cast<void* const*>(recipe) : nullptr;
        if (!want || tpl == nullptr) {
            return;
        }
        recipeName = templateName(tpl);
        void* set = reinterpret_cast<void* (*)(void*, int)>((*reinterpret_cast<void***>(tpl))[0x198 / 8])(tpl, 0x5a);
        if (set == nullptr || sym().tpl_proxy_get_entity == nullptr) {
            return;
        }
        char* base = static_cast<char*>(set);
        resultName = templateName(sym().tpl_proxy_get_entity(base + 0x50));
        char* data = *reinterpret_cast<char**>(base + 0x30);
        const int n = *reinterpret_cast<int*>(base + 0x38);
        for (int i = 0; data != nullptr && i < n && i < 64; ++i) {
            char* el = data + static_cast<std::size_t>(i) * 0x18;
            const std::string name = templateName(sym().tpl_proxy_get_entity(el + 8));
            const int amount = static_cast<int>(*reinterpret_cast<std::uint32_t*>(el + 0x10));
            if (!name.empty() && amount > 0) {
                ingredients.emplace_back(name, static_cast<double>(amount) * count);
            }
        }
    });
    int stack = -1;
    {
        Op op("crafting");
        Op::touch(inventoryPs);
        stack = g_synthesize.original(self, recipe, count);
    }
    if (stack >= 0 && want && !recipeName.empty()) {
        bus::post("crafting", {bus::capture(crafterEntity), recipeName, resultName, static_cast<double>(count), std::move(ingredients)});
    }
    return stack;
}

// ConsumeItems(stack, count): applies the item's modifiers and deletes `count` (the use of food, potions, ...).
bool detourConsume(void* self, int stack, unsigned long n) {
    std::string tpl;
    const bool want = wantItemEvent("item.consumed");
    if (want) {
        guarded([&] { tpl = templateName(stackTemplate(stackAt(self, stack))); });
    }
    Op op("consume");
    Op::touch(self);
    const bool ok = g_consume.original(self, stack, n);
    if (ok && want && !tpl.empty()) {
        bus::post("item.consumed", {bus::capture(ownerOf(self)), tpl, static_cast<double>(n)});
    }
    return ok;
}

// InsertStack(item entity): the inventory takes an item lying in the world and kills the entity; everything is read beforehand.
int detourInsertStack(void* self, void* item) {
    std::string tpl;
    int amount = 0;
    const bool want = wantItemEvent("item.picked_up");
    guarded([&] {
        if (item == nullptr || !want) {
            return;
        }
        void** vt = *reinterpret_cast<void***>(item);
        if (void* tplEntity = reinterpret_cast<void* (*)(void*)>(vt[0x170 / 8])(item)) {
            void** tvt = *reinterpret_cast<void***>(tplEntity);
            if (const void* n = reinterpret_cast<const void* (*)(void*)>(tvt[0x178 / 8])(tplEntity)) {
                tpl = sym().bstring_get_text(n);
            }
        }
        if (void* ps = reinterpret_cast<void* (*)(void*, int)>(vt[0x198 / 8])(item, 0x27)) {
            amount = *reinterpret_cast<int*>(static_cast<char*>(ps) + 0x20);
        }
    });
    Op op("pickup");
    Op::touch(self);
    const int index = g_insertStack.original(self, item);
    if (index >= 0 && want) {
        bus::post("item.picked_up", {bus::capture(ownerOf(self)), tpl, static_cast<double>(amount)});
    }
    return index;
}

bool detourEquip(void* self, int stack, int slot) {
    std::string tpl;
    const bool want = wantItemEvent("item.equipped");
    if (want) {
        guarded([&] { tpl = templateName(stackTemplate(stackAt(self, stack))); });
    }
    const bool ok = g_equip.original(self, stack, slot);
    if (ok && want && !tpl.empty()) {
        bus::post("item.equipped", {bus::capture(ownerOf(self)), tpl, static_cast<double>(slot)});
    }
    return ok;
}

bool detourUnequip(void* self, int stack, bool unlink) {
    std::string tpl;
    int slot = 0;
    const bool want = wantItemEvent("item.unequipped");
    if (want) {
        guarded([&] {
            char* st = stackAt(self, stack);
            tpl = templateName(stackTemplate(st));
            if (st != nullptr) {
                slot = *reinterpret_cast<int*>(st + kStackEquipSlot);
            }
        });
    }
    const bool ok = g_unequip.original(self, stack, unlink);
    if (ok && want && !tpl.empty()) {
        bus::post("item.unequipped", {bus::capture(ownerOf(self)), tpl, static_cast<double>(slot)});
    }
    return ok;
}

// Listener callbacks of the stack list (`listener` is the gCInventoryListener base, 0x20 into the property set). Only the ones no hooked
// operation announced get here: the stack is already in (OnStackCreate) or still in (OnStackDelete) the array.
void reportUnscoped(void* listener, int index, bool created) {
    void* ps = static_cast<char*>(listener) - kListenerOffset;
    if (touched(ps) || !bus::active() || !listening()) {
        return;
    }
    guarded([&] {
        char* st = stackAt(ps, index);
        void* tpl = stackTemplate(st);
        const std::string name = templateName(tpl);
        if (st == nullptr || name.empty() || *reinterpret_cast<const int*>(st + kStackType) == kStackTypeTemporary) {
            return;
        }
        const int amount = stackAmount(st);
        const int total = collect(ps)[tpl] - (created ? 0 : amount);
        const bus::EntityRef owner = bus::capture(ownerOf(ps));
        const double delta = created ? amount : -amount;
        bus::post(created ? "inventory.added" : "inventory.removed", {owner, name, static_cast<double>(amount), static_cast<double>(total), std::string("other"), bus::Arg{}});
        bus::post("inventory.changed", {owner, name, delta, static_cast<double>(total), std::string("other"), bus::Arg{}});
    });
}

void detourOnCreate(void* listener, int index, const void* stack) {
    g_onCreate.original(listener, index, stack);
    reportUnscoped(listener, index, true);
}
void detourOnDelete(void* listener, int index, const void* stack) {
    reportUnscoped(listener, index, false);  // before the stack is removed
    g_onDelete.original(listener, index, stack);
}

// Dialogue commands: they name the cause; the CreateItem command bypasses the exported operations, so its target inventory is snapshotted here
// (gCInfoCommand::GetEntityProxy1 is virtual slot +0x118, the gCInventory_PS is property set 0x1f).
void* commandTargetInventory(void* cmd, void* runtimeInfo) {
    void* ps = nullptr;
    guarded([&] {
        void** vt = *reinterpret_cast<void***>(cmd);
        void* proxy = reinterpret_cast<void* (*)(void*, void*)>(vt[0x118 / 8])(cmd, runtimeInfo);
        void* entity = proxy != nullptr && sym().eproxy_get_entity != nullptr ? sym().eproxy_get_entity(proxy) : nullptr;
        if (entity != nullptr) {
            ps = reinterpret_cast<void* (*)(void*, int)>((*reinterpret_cast<void***>(entity))[0x198 / 8])(entity, 0x1f);
        }
    });
    return ps;
}

bool detourCmdCreate(void* self, void* rt, bool flag) {
    Op op("dialogue");
    Op::touch(commandTargetInventory(self, rt));
    return g_cmdCreate.original(self, rt, flag);
}
bool detourCmdDestroy(void* self, void* rt, bool flag) {
    Op op("dialogue");
    return g_cmdDestroy.original(self, rt, flag);
}
bool detourCmdGive(void* self, void* rt, bool flag) {
    Op op("dialogue");
    return g_cmdGive.original(self, rt, flag);
}

template <class H, class Fn>
void install(H& hook, const char* name, Fn target, Fn detour) {
    if (target == nullptr) {
        spdlog::warn("event hook '{}' unavailable (engine symbol missing): its Lua events will not fire", name);
        return;
    }
    if (!hook.install(name, target, detour)) {
        spdlog::warn("event hook '{}' could not be installed", name);
    }
}

}  // namespace

void installInventoryEventHooks() {
    const Symbols& s = sym();
    install(g_createId, "gCInventory_PS::CreateItems(id)", s.gi_create_items_id, &detourCreateId);
    install(g_createTpl, "gCInventory_PS::CreateItems(tpl)", s.gi_create_items_tpl, &detourCreateTpl);
    install(g_assure, "gCInventory_PS::AssureItems", s.gi_assure_items, &detourAssure);
    install(g_insertStack, "gCInventory_PS::InsertStack", s.inv_insert_stack, &detourInsertStack);
    install(g_deleteItems, "gCInventory_PS::DeleteItems", s.gi_delete_items, &detourDeleteItems);
    install(g_consume, "gCInventory_PS::ConsumeItems", s.gi_consume_items, &detourConsume);
    install(g_deleteStack, "gCInventory_PS::DeleteStack", s.gi_delete_stack, &detourDeleteStack);
    install(g_setAmount, "gCInventory_PS::SetAmount", s.gi_set_amount, &detourSetAmount);
    install(g_setMoney, "gCInventory_PS::SetMoneyAmount", s.gi_set_money, &detourSetMoney);
    install(g_transferItems, "gCInventory_PS::TransferItemsTo", s.gi_transfer_items, &detourTransferItems);
    install(g_transferStack, "gCInventory_PS::TransferStackTo", s.gi_transfer_stack, &detourTransferStack);
    install(g_transferAll, "gCInventory_PS::TransferAllTo", s.gi_transfer_all, &detourTransferAll);
    install(g_loot, "gCInventory_PS::LootSelections", s.gi_loot_selections, &detourLoot);
    install(g_trade, "gCInventory_PS::TradeSelections", s.gi_trade_selections, &detourTrade);
    install(g_dropItems, "gCInventory_PS::DropItemsFromStack", s.gi_drop_items, &detourDropItems);
    install(g_dropStack, "gCInventory_PS::DropStack", s.gi_drop_stack, &detourDropStack);
    install(g_equip, "gCInventory_PS::EquipStackToSlot", s.gi_equip_to_slot, &detourEquip);
    install(g_unequip, "gCInventory_PS::UnEquipStack", s.gi_unequip, &detourUnequip);
    install(g_onCreate, "gCInventory_PS::OnStackCreate", s.gi_on_stack_create, &detourOnCreate);
    install(g_onDelete, "gCInventory_PS::OnStackDelete", s.gi_on_stack_delete, &detourOnDelete);
    install(g_merge, "gCInventory_PS::MergeStacks", s.gi_merge_stacks, &detourMerge);
    install(g_split, "gCInventory_PS::SplitStack", s.gi_split_stack, &detourSplit);
    install(g_cmdCreate, "gCInfoCommandCreateItem::Execute", s.infocmd_create_item, &detourCmdCreate);
    install(g_cmdDestroy, "gCInfoCommandDestroyItem::Execute", s.infocmd_destroy_item, &detourCmdDestroy);
    install(g_cmdGive, "gCInfoCommandGive::Execute", s.infocmd_give, &detourCmdGive);
}

// Hooks on Script.dll functions: Script.dll is loaded by gCScriptAdmin::PostInitialize, so this runs after installInventoryEventHooks().
void installInventoryScriptHooks() {
    install(g_synthesize, "PSInventory::Synthesize", sym().psinv_synthesize, &detourSynthesize);
}

}  // namespace spass::game
