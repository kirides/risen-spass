#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <tuple>

#include <spdlog/spdlog.h>

#include "game/abi.h"
#include "game/lua_entity.h"
#include "game/npc_balance_lua.h"
#include "game/properties.h"
#include "game/template_object.h"

// NPC and monster balancing: native access to the skills of a character (docs/RE.md section 24).
//
// gCSkills_PS (Game.dll) has no reflected properties, so hit points, level, attributes and the combat/protection values cannot be
// edited through the property mechanism. Its storage is two int arrays of 34 entries (gESkill 0..33, RE.md section 23):
//
//   +0x20  + 4*skill   base value      (what the .tple file holds; copied into every spawn by OnCopyFrom, saved with the entity)
//   +0xa8  + 4*skill   modification    (worn items, effects; saved too, zero in templates)
//
// The property set of an NPC *template* is a real gCSkills_PS object, so the very same two arrays can be read and written there
// (`target` = template name); on a live entity the engine's own gCSkills_PS::SetSkillBaseValue is used (range clamping, the
// hit/mana point coupling to the maximum and the changed-callback), `target` = an Entity.

namespace spass::game {
namespace {

constexpr int kSkillCount = 34;
constexpr std::size_t kBaseOffset = 0x20;
constexpr std::size_t kModOffset = 0xa8;
constexpr std::size_t kInfoStride = 0x80;  // gSSkillInfo: +0 min, +4 max, +8 default (ms_arrSkillInfos)

using Two = std::tuple<sol::object, sol::object>;
using Five = std::tuple<sol::object, sol::object, sol::object, sol::object, sol::object>;

Two failResult(sol::state_view lua, const std::string& reason) {
    return {sol::make_object(lua, sol::lua_nil), sol::make_object(lua, reason)};
}

struct SkillsTarget {
    char* ps = nullptr;        // the gCSkills_PS
    bool isTemplate = false;   // template name given (raw writes) as opposed to a live entity (engine writes)
    std::string error;
};

SkillsTarget resolveSkills(const sol::object& target) {
    SkillsTarget t;
    if (target.get_type() == sol::type::string) {
        t.isTemplate = true;
        std::string error;
        t.ps = static_cast<char*>(templatePropertySet(target.as<std::string>(), "gCSkills_PS", error));
        t.error = error;
        return t;
    }
    if (target.is<LuaEntity>()) {
        void* entity = ptr(target.as<LuaEntity>());
        if (entity == nullptr) {
            t.error = "entity is gone";
            return t;
        }
        const Symbols& s = sym();
        if (s.ent_get_property_set == nullptr) {
            t.error = "engine function eCEntity::GetPropertySet is unavailable";
            return t;
        }
        void* ps = nullptr;
        if (!guarded([&] {
                const BString cls("gCSkills_PS");
                ps = s.ent_get_property_set(entity, cls.get());
            })) {
            t.error = "the engine raised an exception (not an entity?)";
            return t;
        }
        t.ps = static_cast<char*>(ps);
        if (t.ps == nullptr) {
            t.error = "entity has no skills (gCSkills_PS)";
        }
        return t;
    }
    t.error = "expected a template name or an entity";
    return t;
}

int infoMin(int skill) {
    const unsigned char* infos = sym().skills_infos;
    if (infos == nullptr) {
        return 0;
    }
    return *reinterpret_cast<const std::int32_t*>(infos + static_cast<std::size_t>(skill) * kInfoStride);
}

}  // namespace

void installNpcBalanceHelpers(sol::state_view lua, sol::table risen) {
    const std::string chunk = std::string(kNpcBalanceChunk1) + kNpcBalanceChunk2;
    sol::load_result loaded = lua.load(chunk, "=risen.npc.balance");
    if (!loaded.valid()) {
        spdlog::error("lua: npc balance helpers failed to load: {}", loaded.get<sol::error>().what());
        return;
    }
    sol::protected_function fn = loaded;
    sol::protected_function_result r = fn(risen);
    if (!r.valid()) {
        spdlog::error("lua: npc balance helpers failed to run: {}", r.get<sol::error>().what());
    }
}

void bindNpcBalance(sol::state_view lua, sol::table risen) {
    sol::table npc = risen["npc"];
    if (!npc.valid()) {
        npc = lua.create_table();
        risen["npc"] = npc;
    }

    // risen.npc.skill_raw(target, skill) -> base, modification, total, min, max | nil, reason
    //   target: template name (the template's gCSkills_PS) or an NPC entity; skill: gESkill number 0..33 (RE.md section 23).
    //   base/modification are the stored values; total is the engine's clamped GetSkillValue; min/max the limits of the skill
    //   (max 0 = unlimited; for hit points / mana points the maximum is the current Stat_HP / Stat_MP).
    npc.set_function("skill_raw", [](sol::this_state ts, sol::object target, int skill) -> Five {
        sol::state_view L(ts);
        auto fail = [&](const std::string& why) -> Five {
            return {sol::make_object(L, sol::lua_nil), sol::make_object(L, why), sol::object(), sol::object(), sol::object()};
        };
        if (skill < 0 || skill >= kSkillCount) {
            return fail("skill must be 0.." + std::to_string(kSkillCount - 1));
        }
        const SkillsTarget t = resolveSkills(target);
        if (t.ps == nullptr) {
            return fail(t.error);
        }
        const Symbols& s = sym();
        int base = 0, mod = 0, total = 0, maxv = 0;
        if (!guarded([&] {
                base = *reinterpret_cast<const std::int32_t*>(t.ps + kBaseOffset + 4 * skill);
                mod = *reinterpret_cast<const std::int32_t*>(t.ps + kModOffset + 4 * skill);
                total = base + mod;
                if (s.skills_get_value != nullptr) {
                    total = s.skills_get_value(t.ps, skill);
                }
                if (s.skills_get_max != nullptr) {
                    maxv = s.skills_get_max(t.ps, skill);
                }
            })) {
            return fail("the engine raised an exception while reading the skills");
        }
        return {sol::make_object(L, base), sol::make_object(L, mod), sol::make_object(L, total), sol::make_object(L, infoMin(skill)),
                sol::make_object(L, maxv)};
    });

    // risen.npc.set_skill_base(target, skill, value) -> base, total | nil, reason
    //   Writes the BASE value of a skill. A template name writes the template's storage directly (clamped to the skill's limits);
    //   an entity goes through the engine's gCSkills_PS::SetSkillBaseValue (limits, hit point coupling, change callback).
    npc.set_function("set_skill_base", [](sol::this_state ts, sol::object target, int skill, double value) -> Two {
        sol::state_view L(ts);
        if (skill < 0 || skill >= kSkillCount) {
            return failResult(L, "skill must be 0.." + std::to_string(kSkillCount - 1));
        }
        if (value != value || value < -2147483648.0 || value > 2147483647.0) {
            return failResult(L, "value out of range");
        }
        const SkillsTarget t = resolveSkills(target);
        if (t.ps == nullptr) {
            return failResult(L, t.error);
        }
        const Symbols& s = sym();
        if (s.skills_get_value == nullptr || s.skills_get_max == nullptr) {
            return failResult(L, "engine functions gCSkills_PS::GetSkillValue/GetSkillMaxValue are unavailable");
        }
        std::int32_t v = static_cast<std::int32_t>(value >= 0 ? value + 0.5 : value - 0.5);
        int base = 0, total = 0;
        if (!guarded([&] {
                if (t.isTemplate) {
                    const int maxv = s.skills_get_max(t.ps, skill);
                    if (maxv > 0 && v > maxv) {
                        v = maxv;
                    }
                    v = std::max<std::int32_t>(v, infoMin(skill));
                    *reinterpret_cast<std::int32_t*>(t.ps + kBaseOffset + 4 * skill) = v;
                } else if (s.skills_set_base != nullptr) {
                    s.skills_set_base(t.ps, skill, v);
                }
                base = *reinterpret_cast<const std::int32_t*>(t.ps + kBaseOffset + 4 * skill);
                total = s.skills_get_value(t.ps, skill);
            })) {
            return failResult(L, "the engine raised an exception while writing the skill");
        }
        if (!t.isTemplate && s.skills_set_base == nullptr) {
            return failResult(L, "engine function gCSkills_PS::SetSkillBaseValue is unavailable");
        }
        return {sol::make_object(L, base), sol::make_object(L, total)};
    });

    // risen.npc.combat_species(name) -> { {name=, type=, value=}, ... } | nil, reason
    //   The properties of a combat species of gCCombatAdmin (the CombatSpecies of gCCombatSystem_PS: "Human", "Wolf", "Lizard", ...):
    //   attack ranges, the fist items (LeftFistItem / RightFistItem: the item templates monsters hit with, whose gCDamage_PS
    //   DamageAmount is their melee damage), animations.
    npc.set_function("combat_species", [](sol::this_state ts, const std::string& name) -> Two {
        sol::state_view L(ts);
        const Symbols& s = sym();
        if (s.combat_admin_instance == nullptr || s.combat_get_species == nullptr) {
            return failResult(L, "engine functions gCCombatAdmin::GetInstance/GetSpecies are unavailable");
        }
        void* species = nullptr;
        if (!guarded([&] {
                void* admin = s.combat_admin_instance();
                if (admin != nullptr) {
                    const BString n(name);
                    species = s.combat_get_species(admin, n.get());
                }
            })) {
            return failResult(L, "the engine raised an exception (combat admin not ready?)");
        }
        if (species == nullptr) {
            return failResult(L, "unknown combat species '" + name + "'");
        }
        sol::table out = L.create_table();
        int i = 1;
        for (const auto& [prop, type] : propertyList(species)) {
            sol::table entry = L.create_table();
            entry["name"] = prop;
            entry["type"] = type;
            const auto v = propertyGet(species, prop);
            entry["value"] = v ? *v : std::string();
            out[i++] = entry;
        }
        return {sol::make_object(L, out), sol::make_object(L, sol::lua_nil)};
    });

    // risen.npc.set_combat_species_property(name, property, value) -> true | nil, reason: edits a combat species (shared by every NPC that uses it)
    npc.set_function("set_combat_species_property", [](sol::this_state ts, const std::string& name, const std::string& prop,
                                                        const std::string& value) -> Two {
        sol::state_view L(ts);
        const Symbols& s = sym();
        if (s.combat_admin_instance == nullptr || s.combat_get_species == nullptr) {
            return failResult(L, "engine functions gCCombatAdmin::GetInstance/GetSpecies are unavailable");
        }
        void* species = nullptr;
        if (!guarded([&] {
                void* admin = s.combat_admin_instance();
                if (admin != nullptr) {
                    const BString n(name);
                    species = s.combat_get_species(admin, n.get());
                }
            })) {
            return failResult(L, "the engine raised an exception (combat admin not ready?)");
        }
        if (species == nullptr) {
            return failResult(L, "unknown combat species '" + name + "'");
        }
        if (!propertyGet(species, prop)) {
            return failResult(L, "combat species '" + name + "' has no property '" + prop + "'");
        }
        if (!propertySet(species, prop, value)) {
            return failResult(L, "the engine refused to set '" + prop + "'");
        }
        return {sol::make_object(L, true), sol::make_object(L, sol::lua_nil)};
    });

    // ---- inventory of a template (loot) -------------------------------------------------------------------------------------
    // The inventory of an NPC template is a real gCInventory_PS holding stacks of item templates; a spawn clones it, so the items
    // an NPC template carries are what each new NPC starts with (and drops when plundered). PSInventory works on the template
    // entity as it does on a live NPC.

    // risen.npc.template_inventory(name) -> { { item = "It_Apple", amount = 3 }, ... } | nil, reason
    npc.set_function("template_inventory", [](sol::this_state ts, const std::string& name) -> Two {
        sol::state_view L(ts);
        const Symbols& s = sym();
        if (s.inv_stack_count == nullptr || s.inv_template_item == nullptr || s.inv_stack_amount == nullptr || s.ent_get_name == nullptr) {
            return failResult(L, "engine inventory functions are unavailable");
        }
        std::string error;
        void* tpl = templateEntityPtr(name, error);
        if (tpl == nullptr) {
            return failResult(L, error);
        }
        sol::table out = L.create_table();
        const bool ran = guarded([&] {
            EntityHolder inv{tpl};
            const int n = s.inv_stack_count(&inv);
            for (int i = 0; i < n && i < 256; ++i) {
                EntityHolder item;
                s.inv_template_item(&inv, &item, i);
                if (item.entity == nullptr) {
                    continue;
                }
                OutString nm;
                s.ent_get_name(&item, nm.ptr());
                sol::table e = L.create_table();
                e["item"] = nm.take();
                e["amount"] = s.inv_stack_amount(&inv, i);
                out[out.size() + 1] = e;
            }
        });
        if (!ran) {
            return failResult(L, "the engine raised an exception while reading the inventory");
        }
        return {sol::make_object(L, out), sol::make_object(L, sol::lua_nil)};
    });

    // risen.npc.template_give(name, item [, count]) -> true | nil, reason: puts `count` (default 1) items of the item template into the
    // inventory of the NPC template: every NPC spawned afterwards carries them (equip them with Entity:equip_all()).
    npc.set_function("template_give", [](sol::this_state ts, const std::string& name, const std::string& itemName, sol::optional<int> count) -> Two {
        sol::state_view L(ts);
        const Symbols& s = sym();
        if (s.inv_create_items == nullptr) {
            return failResult(L, "engine function PSInventory::CreateItems is unavailable");
        }
        const int n = count.value_or(1);
        if (n < 1 || n > 10000) {
            return failResult(L, "count must be between 1 and 10000");
        }
        std::string error;
        void* tpl = templateEntityPtr(name, error);
        if (tpl == nullptr) {
            return failResult(L, error);
        }
        bool valid = true;
        int stack = -1;
        if (!guarded([&] {
                const TemplateObject t(itemName);
                valid = t.valid();
                if (valid) {
                    EntityHolder inv{tpl};
                    stack = s.inv_create_items(&inv, t.get(), n, 0);
                }
            })) {
            return failResult(L, "the engine raised an exception while creating the items");
        }
        if (!valid) {
            return failResult(L, "unknown item template '" + itemName + "'");
        }
        if (stack < 0) {
            return failResult(L, "the engine refused to create the items");
        }
        return {sol::make_object(L, true), sol::make_object(L, sol::lua_nil)};
    });

    // risen.npc.template_take(name, item [, count]) -> removed amount | nil, reason: removes up to `count` (default: all) items of that
    // template from the NPC template's inventory (not equipped stacks).
    npc.set_function("template_take", [](sol::this_state ts, const std::string& name, const std::string& itemName, sol::optional<int> count) -> Two {
        sol::state_view L(ts);
        const Symbols& s = sym();
        if (s.inv_find_stack == nullptr || s.inv_delete_items == nullptr || s.inv_stack_amount == nullptr) {
            return failResult(L, "engine inventory functions are unavailable");
        }
        std::string error;
        void* tpl = templateEntityPtr(name, error);
        if (tpl == nullptr) {
            return failResult(L, error);
        }
        int removed = 0;
        bool valid = true;
        if (!guarded([&] {
                const TemplateObject t(itemName);
                valid = t.valid();
                if (!valid) {
                    return;
                }
                EntityHolder inv{tpl};
                int wanted = count.value_or(1000000);
                for (int guard = 0; wanted > 0 && guard < 64; ++guard) {
                    const int stack = s.inv_find_stack(&inv, t.get(), -1, true);
                    if (stack < 0) {
                        break;
                    }
                    const int here = s.inv_stack_amount(&inv, stack);
                    const int take = here < wanted ? here : wanted;
                    if (take <= 0 || !s.inv_delete_items(&inv, stack, take)) {
                        break;
                    }
                    removed += take;
                    wanted -= take;
                }
            })) {
            return failResult(L, "the engine raised an exception while removing the items");
        }
        if (!valid) {
            return failResult(L, "unknown item template '" + itemName + "'");
        }
        return {sol::make_object(L, removed), sol::make_object(L, sol::lua_nil)};
    });

    installNpcBalanceHelpers(lua, risen);
}

}  // namespace spass::game
