#include <string>
#include <tuple>

#include <spdlog/spdlog.h>

#include "game/abi.h"
#include "game/lua_entity.h"
#include "game/spell_lua.h"
#include "game/template_object.h"

// Spells (docs/RE.md section 29, docs/LUA_API.md "Spells"): the native part of risen.spell.
//
// A spell is not one engine object. A rune/scroll/orb/crystal item carries a gCCastInfo_PS (ManaCost, CastItem, CastAmmo, CastFail, StartTask,
// ConsumeItem), the engine's PSCastInfo wrapper (Script.dll) does the cast accounting on that property set (`CanCast`: ManaCost <= the caster's
// total mana, `Consume`: base mana -= ManaCost and, with ConsumeItem, one item of the stack is used up), and a projectile spell spawns a template
// with gCDamage_PS + gCProjectile2_PS whose shooter PSProjectile::GetShooter returns. The natives below expose exactly those engine functions; the
// catalogue, the `spells` / `new_spells` balancing sections and the per-shot overrides are Lua (spell_lua.h).

namespace spass::game {
namespace {

using Two = std::tuple<sol::object, sol::object>;
Two failResult(sol::state_view lua, const std::string& reason) {
    return {sol::make_object(lua, sol::lua_nil), sol::make_object(lua, reason)};
}

constexpr int kSlotCast = 10;   // gEEquipSlot of runes / scrolls (UseType Cast)
constexpr int kSlotMagic = 11;  // gEEquipSlot of the crystals (MagicFireball / MagicFrost / MagicMissile)

}  // namespace

void bindSpell(sol::state_view lua, sol::table risen) {
    sol::table spell = lua.create_table();
    risen["spell"] = spell;

    // risen.spell.equipped(entity [, slot]) -> template name | nil
    //   The item in an equip slot of a character: slot 11 (default) is the magic slot of the crystals (and of fireball/frost/missile scrolls), slot 10 the
    //   slot of runes and scrolls with UseType Cast. nil when nothing is equipped there. PSInventory::FindStackIndex(gEEquipSlot) + GetTemplateItem.
    spell.set_function("equipped", [](sol::this_state ts, const LuaEntity& e, sol::optional<int> slot) -> sol::object {
        sol::state_view L(ts);
        const Symbols& s = sym();
        if (s.inv_find_slot == nullptr || s.inv_template_item == nullptr || s.ent_get_name == nullptr) {
            return sol::make_object(L, sol::lua_nil);
        }
        void* entity = ptr(e);
        if (entity == nullptr) {
            return sol::make_object(L, sol::lua_nil);
        }
        std::string name;
        guarded([&] {
            EntityHolder inv{entity};
            const int stack = s.inv_find_slot(&inv, slot.value_or(kSlotMagic));
            if (stack < 0) {
                return;
            }
            EntityHolder item;
            s.inv_template_item(&inv, &item, stack);
            if (item.entity == nullptr) {
                return;
            }
            OutString nm;
            s.ent_get_name(&item, nm.ptr());
            name = nm.take();
        });
        if (name.empty()) {
            return sol::make_object(L, sol::lua_nil);
        }
        return sol::make_object(L, name);
    });

    // risen.spell.can_cast(caster, item) -> bool | nil, reason
    //   The engine's own mana check for casting with `item` (a template name; PSCastInfo::CanCast): the item's gCCastInfo_PS.ManaCost <= the caster's
    //   total mana. False for an item without a gCCastInfo_PS.
    spell.set_function("can_cast", [](sol::this_state ts, const LuaEntity& caster, const std::string& item) -> Two {
        sol::state_view L(ts);
        const Symbols& s = sym();
        if (s.cast_can_cast == nullptr) {
            return failResult(L, "engine function PSCastInfo::CanCast is unavailable");
        }
        void* entity = ptr(caster);
        if (entity == nullptr) {
            return failResult(L, "entity is gone");
        }
        std::string error;
        void* tpl = templateEntityPtr(item, error);
        if (tpl == nullptr) {
            return failResult(L, error);
        }
        bool result = false;
        if (!guarded([&] {
                EntityHolder info{tpl};
                EntityHolder who{entity};
                result = s.cast_can_cast(&info, &who);
            })) {
            return failResult(L, "the engine raised an exception during the check");
        }
        return {sol::make_object(L, result), sol::make_object(L, sol::lua_nil)};
    });

    // risen.spell.consume(caster, item) -> bool | nil, reason
    //   What the cast scripts do when a spell is cast (PSCastInfo::Consume): the item's ManaCost is taken from the caster's base mana and, when the item's
    //   ConsumeItem is set (scrolls), one item of its stack is used up. `item` is a template name; the caster must carry it.
    spell.set_function("consume", [](sol::this_state ts, const LuaEntity& caster, const std::string& item) -> Two {
        sol::state_view L(ts);
        const Symbols& s = sym();
        if (s.cast_consume == nullptr || s.inv_find_stack == nullptr) {
            return failResult(L, "engine function PSCastInfo::Consume is unavailable");
        }
        void* entity = ptr(caster);
        if (entity == nullptr) {
            return failResult(L, "entity is gone");
        }
        std::string error;
        void* tpl = templateEntityPtr(item, error);
        if (tpl == nullptr) {
            return failResult(L, error);
        }
        int stack = -1;
        bool result = false;
        if (!guarded([&] {
                EntityHolder inv{entity};
                const TemplateObject t(item);
                stack = t.valid() ? s.inv_find_stack(&inv, t.get(), -1, false) : -1;
                if (stack >= 0) {
                    EntityHolder info{tpl};
                    result = s.cast_consume(&info, &inv, stack);
                }
            })) {
            return failResult(L, "the engine raised an exception");
        }
        if (stack < 0) {
            return failResult(L, "the caster does not carry '" + item + "'");
        }
        return {sol::make_object(L, result), sol::make_object(L, sol::lua_nil)};
    });

    // risen.spell.discard(owner, item) -> bool: deletes the whole stack of `item` from the owner's inventory (gCInventory_PS::DeleteStack). Unlike
    //   risen.item.take it also removes a stack the engine left behind with a zero amount: PSCastInfo::Consume used the last scroll of a spell cast from the
    //   magic slot up, but only unequips from the rune slot, so DeleteItems then leaves the emptied stack in place (RE.md section 29).
    spell.set_function("discard", [](const LuaEntity& owner, const std::string& item) -> bool {
        const Symbols& s = sym();
        if (s.gi_delete_stack == nullptr || s.inv_find_stack == nullptr) {
            return false;
        }
        bool ok = false;
        guarded([&] {
            EntityHolder inv{ptr(owner)};
            const TemplateObject t(item);
            if (!t.valid() || inv.entity == nullptr) {
                return;
            }
            const int stack = s.inv_find_stack(&inv, t.get(), -1, false);
            if (stack < 0) {
                return;
            }
            void* ps = reinterpret_cast<void* (*)(void*, int)>((*static_cast<void***>(inv.entity))[0x198 / 8])(inv.entity, 0x1f);
            ok = ps != nullptr && s.gi_delete_stack(ps, stack);
        });
        return ok;
    });

    // risen.spell.shooter(projectile) -> Entity | nil: who shot a projectile (PSProjectile::GetShooter); nil before it was shot.
    spell.set_function("shooter", [](sol::this_state ts, const LuaEntity& projectile) -> sol::object {
        sol::state_view L(ts);
        const Symbols& s = sym();
        if (s.proj_get_shooter == nullptr) {
            return sol::make_object(L, sol::lua_nil);
        }
        void* entity = ptr(projectile);
        if (entity == nullptr) {
            return sol::make_object(L, sol::lua_nil);
        }
        EntityHolder ret;
        if (!guarded([&] {
                EntityHolder h{entity};
                s.proj_get_shooter(&h, &ret);
            })) {
            return sol::make_object(L, sol::lua_nil);
        }
        return makeEntityObject(L, ret.entity);
    });

    spell["SLOT_CAST"] = kSlotCast;
    spell["SLOT_MAGIC"] = kSlotMagic;
    installSpellHelpers(lua, risen);
}

void installSpellHelpers(sol::state_view lua, sol::table risen) {
    sol::load_result loaded = lua.load(kSpellChunk, "=risen.spell");
    if (!loaded.valid()) {
        spdlog::error("lua: spell helpers failed to load: {}", loaded.get<sol::error>().what());
        return;
    }
    sol::protected_function fn = loaded;
    sol::protected_function_result res = fn(risen);
    if (!res.valid()) {
        spdlog::error("lua: spell helpers failed to run: {}", res.get<sol::error>().what());
    }
}

void installSpellModApi(sol::state_view lua, sol::table api) {
    sol::load_result loaded = lua.load(kSpellModChunk, "=risen.spell.mod");
    if (!loaded.valid()) {
        spdlog::error("lua: spell mod api failed to load: {}", loaded.get<sol::error>().what());
        return;
    }
    sol::protected_function fn = loaded;
    sol::protected_function_result res = fn(api);
    if (!res.valid()) {
        spdlog::error("lua: spell mod api failed to run: {}", res.get<sol::error>().what());
    }
}

}  // namespace spass::game
