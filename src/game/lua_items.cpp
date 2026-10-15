#include <algorithm>

#include <spdlog/spdlog.h>

#include "common/trace.h"
#include "game/abi.h"
#include "game/lua_entity.h"
#include "game/template_object.h"

// Templates, spawning and inventories (Script.dll). Verified from the decompilation (docs/RE.md §6):
//  * `Template` derives from eCTemplateEntityProxy; Template(const char*) fills it from a template name
//    such as "It_1H_Machete" (the names are listed in data/common/templates.pak).
//  * Entity::Spawn takes the Template BY VALUE (the callee destroys it) and a 4x4 pose (bCMatrix, 64 bytes,
//    row-major, translation in the last row).
//  * PSInventory is, like Entity, a holder whose first qword is the eCEntity*.

namespace spass::game {
namespace {

template <class R, class F>
sol::optional<R> itemGet(F&& f) {
    R r{};
    if (!guarded([&] { r = f(); })) {
        return sol::nullopt;
    }
    return r;
}

bool templateValid(const std::string& name) {
    bool ok = false;
    guarded([&] {
        const TemplateObject t(name);
        ok = t.valid();
    });
    return ok;
}

}  // namespace

void addItemMethods(sol::usertype<LuaEntity>& ut) {
    const Symbols& s = sym();
    // PSItem is a holder like the other PS* wrappers: its first qword is the eCEntity*. A missing property set (the
    // entity is not an item) makes the engine calls fail, which the guard turns into nil.
    ut["is_item_valid"] = [&s](const LuaEntity& e) {
        return itemGet<bool>([&] { EntityHolder h{ptr(e)}; return s.psitem_is_valid(&h); }).value_or(false);
    };
    ut["item_value"] = [&s](const LuaEntity& e) { return itemGet<int>([&] { EntityHolder h{ptr(e)}; return s.psitem_gold_value(&h); }); };
    ut["item_amount"] = [&s](const LuaEntity& e) {
        return itemGet<double>([&] { EntityHolder h{ptr(e)}; return static_cast<double>(s.psitem_amount(&h)); });
    };
    ut["set_item_amount"] = [&s](const LuaEntity& e, double amount) {
        return guarded([&] { EntityHolder h{ptr(e)}; s.psitem_set_amount(&h, static_cast<unsigned long>(amount)); });
    };
    ut["item_category"] = [&s](const LuaEntity& e) { return itemGet<int>([&] { EntityHolder h{ptr(e)}; return s.psitem_category(&h); }); };
    ut["item_use_type"] = [&s](const LuaEntity& e) { return itemGet<int>([&] { EntityHolder h{ptr(e)}; return s.psitem_use_type(&h); }); };
    ut["item_icon"] = [&s](const LuaEntity& e) {
        return itemGet<std::string>([&] {
            EntityHolder h{ptr(e)};
            OutString out;
            s.psitem_icon(&h, out.ptr());
            return out.take();
        });
    };
    ut["is_mission_item"] = [&s](const LuaEntity& e) {
        return itemGet<bool>([&] { EntityHolder h{ptr(e)}; return s.psitem_is_mission_item(&h); }).value_or(false);
    };
}

void bindItems(sol::state_view lua, sol::table risen) {
    const Symbols& s = sym();

    sol::table tpl = lua.create_table();
    // risen.template.exists("It_1H_Machete") -> bool
    tpl.set_function("exists", [](const std::string& name) { return templateValid(name); });
    risen["template"] = tpl;

    // risen.entity.spawn("It_1H_Machete", x, y, z [, temporary]) -> Entity | nil
    sol::table entity = risen["entity"];
    entity.set_function("spawn", [&s](sol::this_state ts, const std::string& name, float x, float y, float z,
                                      sol::optional<bool> temporary) -> sol::object {
        EntityHolder ret;
        float pose[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1};
        bool valid = false;
        const bool ok = guarded([&] {
            TemplateObject t(name);
            valid = t.valid();
            if (valid) {
                s.ent_spawn(&ret, t.giveAway(), pose, temporary.value_or(false));  // callee destroys the template
            }
        });
        if (!ok) {
            spdlog::warn("entity.spawn('{}'): engine raised an exception (game not running?)", name);
            return sol::make_object(ts, sol::lua_nil);
        }
        if (!valid) {
            spdlog::warn("entity.spawn('{}'): unknown template", name);
        }
        SPASS_TRACE(trace::Channel::Spawn, "spawn '{}' at ({:.0f}, {:.0f}, {:.0f}){} -> {}", name, x, y, z, temporary.value_or(false) ? " temporary" : "",
                    !valid ? "unknown template" : ret.entity != nullptr ? "ok" : "no entity");
        return makeEntityObject(ts, ret.entity);
    });

    sol::table item = lua.create_table();
    // risen.item.give(entity, "It_1H_Machete", 1 [, stackType]) -> created amount, or -1 on failure
    item.set_function("give", [&s](const LuaEntity& owner, const std::string& name, sol::optional<int> count,
                                   sol::optional<int> stackType) -> int {
        int result = -1;
        guarded([&] {
            EntityHolder inv{ptr(owner)};
            const TemplateObject t(name);
            if (t.valid()) {
                result = s.inv_create_items(&inv, t.get(), count.value_or(1), stackType.value_or(0));
            }
        });
        return result;
    });
    // risen.item.take(entity, "It_Apple", 3) -> removed amount: deletes up to `count` items from the inventory's stacks
    // (unequipped stacks first found; the amount that was really removed is returned).
    item.set_function("take", [&s](const LuaEntity& owner, const std::string& name, sol::optional<int> count) -> int {
        int removed = 0;
        if (s.inv_find_stack == nullptr || s.inv_delete_items == nullptr || s.inv_stack_amount == nullptr) {
            return 0;
        }
        const bool ran = guarded([&] {
            EntityHolder inv{ptr(owner)};
            const TemplateObject t(name);
            if (!t.valid()) {
                spdlog::debug("item.take: unknown template '{}'", name);
                return;
            }
            int wanted = count.value_or(1);
            for (int guard = 0; wanted > 0 && guard < 64; ++guard) {
                const int stack = s.inv_find_stack(&inv, t.get(), -1, true);
                const int here = stack >= 0 ? s.inv_stack_amount(&inv, stack) : -1;
                spdlog::debug("item.take: stack {} holds {} (wanted {})", stack, here, wanted);
                if (stack < 0) {
                    break;
                }
                const int n = here < wanted ? here : wanted;
                if (n <= 0 || !s.inv_delete_items(&inv, stack, n)) {
                    break;
                }
                removed += n;
                wanted -= n;
            }
        });
        if (!ran) {
            spdlog::warn("item.take: the engine raised an exception");
        }
        return removed;
    });
    // risen.item.transfer(from, to, "It_Apple" [, count]) -> moved amount: the engine's own transfer (PSInventory::TransferAmountTo), stack by stack.
    item.set_function("transfer", [&s](const LuaEntity& from, const LuaEntity& to, const std::string& name, sol::optional<int> count) -> int {
        int moved = 0;
        if (s.inv_find_stack == nullptr || s.inv_stack_amount == nullptr || s.psinv_transfer_amount == nullptr) {
            return 0;
        }
        guarded([&] {
            EntityHolder inv{ptr(from)};
            EntityHolder target{ptr(to)};
            const TemplateObject t(name);
            if (!t.valid() || inv.entity == nullptr || target.entity == nullptr) {
                return;
            }
            int wanted = count.value_or(1);
            for (int guard = 0; wanted > 0 && guard < 64; ++guard) {
                const int stack = s.inv_find_stack(&inv, t.get(), -1, true);
                if (stack < 0) {
                    break;
                }
                const int n = std::min(s.inv_stack_amount(&inv, stack), wanted);
                if (n <= 0 || !s.psinv_transfer_amount(&inv, stack, n, &target)) {
                    break;
                }
                moved += n;
                wanted -= n;
            }
        });
        return moved;
    });
    // risen.item.loot(looter, container [, "It_Apple", count]) -> bool: the engine's loot action (PSInventory::LootSelections): everything the
    // container holds (or `count` of one template) moves to the looter, as when the player loots a corpse or chest.
    item.set_function("loot", [&s](const LuaEntity& looter, const LuaEntity& container, sol::optional<std::string> name, sol::optional<int> count) -> bool {
        bool ok = false;
        if (s.psinv_loot == nullptr || s.psinv_set_max_selection == nullptr) {
            return false;
        }
        guarded([&] {
            EntityHolder to{ptr(looter)};
            EntityHolder from{ptr(container)};
            if (to.entity == nullptr || from.entity == nullptr) {
                return;
            }
            if (s.psinv_set_max_select_limit != nullptr) {
                s.psinv_set_max_select_limit(&from);  // a selection cannot exceed the select limit
            }
            if (name && s.inv_find_stack != nullptr && s.psinv_set_selection != nullptr) {
                const TemplateObject t(*name);
                const int stack = t.valid() ? s.inv_find_stack(&from, t.get(), -1, false) : -1;
                if (stack < 0) {
                    return;
                }
                const int n = count.value_or(1);
                s.psinv_set_selection(&from, stack, &n);
            } else {
                s.psinv_set_max_selection(&from);
            }
            ok = s.psinv_loot(&to, &from);
        });
        return ok;
    });
    // risen.item.trade(a, b, "It_Apple", count) -> bool: PSInventory::TradeSelections with `count` of the template selected in `a`'s inventory
    // (the trader's side of buying/selling; the engine does not move gold by itself here).
    item.set_function("trade", [&s](const LuaEntity& a, const LuaEntity& b, const std::string& name, sol::optional<int> count) -> bool {
        bool ok = false;
        if (s.psinv_trade == nullptr || s.psinv_set_selection == nullptr || s.inv_find_stack == nullptr) {
            return false;
        }
        guarded([&] {
            EntityHolder from{ptr(a)};
            EntityHolder with{ptr(b)};
            const TemplateObject t(name);
            if (!t.valid() || from.entity == nullptr || with.entity == nullptr) {
                return;
            }
            const int stack = s.inv_find_stack(&from, t.get(), -1, false);
            if (stack < 0) {
                return;
            }
            if (s.psinv_set_max_select_limit != nullptr) {
                s.psinv_set_max_select_limit(&from);
            }
            const int n = count.value_or(1);
            s.psinv_set_selection(&from, stack, &n);
            ok = s.psinv_trade(&from, &with);
        });
        return ok;
    });
    // risen.item.drop(owner, "It_Apple" [, count]) -> Entity | nil: drops items of an unequipped stack into the world (gCInventory_PS::DropItemsFromStack);
    // the returned entity is the item lying in the world.
    item.set_function("drop", [&s](sol::this_state ts, const LuaEntity& owner, const std::string& name, sol::optional<int> count) -> sol::object {
        void* dropped = nullptr;
        if (s.gi_drop_items == nullptr || s.inv_find_stack == nullptr || s.inv_stack_amount == nullptr) {
            return sol::make_object(ts, sol::lua_nil);
        }
        guarded([&] {
            EntityHolder inv{ptr(owner)};
            const TemplateObject t(name);
            if (!t.valid() || inv.entity == nullptr) {
                return;
            }
            const int stack = s.inv_find_stack(&inv, t.get(), -1, true);
            if (stack < 0) {
                return;
            }
            void* ps = reinterpret_cast<void* (*)(void*, int)>((*static_cast<void***>(inv.entity))[0x198 / 8])(inv.entity, 0x1f);
            const int n = std::min(count.value_or(1), s.inv_stack_amount(&inv, stack));
            if (ps != nullptr && n > 0) {
                dropped = s.gi_drop_items(ps, stack, n, nullptr);
            }
        });
        return makeEntityObject(ts, dropped);
    });
    // risen.item.pick_up(owner, item_entity) -> stack index | nil: the owner takes an item lying in the world (PSInventory::InsertStack: the world entity is
    // consumed, `item.picked_up` fires).
    item.set_function("pick_up", [&s](const LuaEntity& owner, const LuaEntity& worldItem) -> sol::optional<int> {
        sol::optional<int> index;
        if (s.psinv_insert_stack == nullptr) {
            return index;
        }
        guarded([&] {
            EntityHolder inv{ptr(owner)};
            EntityHolder it{ptr(worldItem)};
            if (inv.entity != nullptr && it.entity != nullptr) {
                const int i = s.psinv_insert_stack(&inv, &it);
                if (i >= 0) {
                    index = i;
                }
            }
        });
        return index;
    });
    // risen.item.equip(owner, "It_1H_Sword") / risen.item.unequip(owner, "It_1H_Sword") -> bool: puts the first stack of that template on / takes it off
    // (PSInventory::EquipStack / UnEquipStack: the same calls the inventory screen makes; `item.equipped` / `item.unequipped` fire).
    item.set_function("equip", [&s](const LuaEntity& owner, const std::string& name) -> bool {
        bool ok = false;
        if (s.inv_find_stack == nullptr || s.inv_equip_stack == nullptr) {
            return false;
        }
        guarded([&] {
            EntityHolder inv{ptr(owner)};
            const TemplateObject t(name);
            const int stack = t.valid() && inv.entity != nullptr ? s.inv_find_stack(&inv, t.get(), -1, false) : -1;
            ok = stack >= 0 && s.inv_equip_stack(&inv, stack);
        });
        return ok;
    });
    item.set_function("unequip", [&s](const LuaEntity& owner, const std::string& name) -> bool {
        bool ok = false;
        if (s.inv_find_stack == nullptr || s.psinv_unequip_stack == nullptr) {
            return false;
        }
        guarded([&] {
            EntityHolder inv{ptr(owner)};
            const TemplateObject t(name);
            const int stack = t.valid() && inv.entity != nullptr ? s.inv_find_stack(&inv, t.get(), -1, false) : -1;
            ok = stack >= 0 && s.psinv_unequip_stack(&inv, stack);
        });
        return ok;
    });
    // risen.item.count(entity, "It_1H_Machete") -> integer
    item.set_function("count", [&s](const LuaEntity& owner, const std::string& name) -> int {
        int result = 0;
        guarded([&] {
            EntityHolder inv{ptr(owner)};
            const TemplateObject t(name);
            if (t.valid()) {
                result = s.inv_item_amount(&inv, t.get());
            }
        });
        return result;
    });
    // risen.item.money(entity) -> number
    item.set_function("money", [&s](const LuaEntity& owner) -> float {
        float result = 0;
        guarded([&] {
            EntityHolder inv{ptr(owner)};
            result = s.inv_money(&inv);
        });
        return result;
    });
    risen["item"] = item;
}

}  // namespace spass::game
