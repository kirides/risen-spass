#include <spdlog/spdlog.h>

#include "game/abi.h"
#include "game/lua_entity.h"

// NPC configuration (docs/RE.md §10). All PS* wrappers of Script.dll are "holders": their first qword is the
// eCEntity*, and they fetch the property set they need through eCEntity's vtable (+0x198, by type id:
// 0x1e = gCNPC_PS, 0x1f = gCInventory_PS, 0x24 = gCScriptRoutine_PS). So an EntityHolder works as `this` for
// every one of them, and each call quietly does nothing when the entity lacks that property set.

namespace spass::game {
namespace {

constexpr int kSkillModifierBase = 0;  // gESkillModifier default used by the game's own scripts (see RE.md)

template <class R, class F>
sol::optional<R> tryGet(F&& f) {
    R r{};
    if (!guarded([&] { r = f(); })) {
        return sol::nullopt;
    }
    return r;
}

template <class F>
bool tryDo(F&& f) {
    return guarded([&] { f(); });
}

}  // namespace

void addNpcMethods(sol::usertype<LuaEntity>& ut) {
    const Symbols& s = sym();

    // ---- guild / group ----------------------------------------------------------------------------------
    ut["guild"] = [&s](const LuaEntity& e) { return tryGet<int>([&] { EntityHolder h{ptr(e)}; return s.npc_get_guild(&h); }); };
    ut["set_guild"] = [&s](const LuaEntity& e, int guild) {
        return tryDo([&] { EntityHolder h{ptr(e)}; s.npc_set_guild(&h, &guild); });
    };
    ut["group"] = [&s](const LuaEntity& e) {
        return tryGet<std::string>([&] {
            EntityHolder h{ptr(e)};
            OutString out;
            s.npc_get_group(&h, out.ptr());
            return out.take();
        });
    };
    ut["set_group"] = [&s](const LuaEntity& e, const std::string& group) {
        return tryDo([&] { EntityHolder h{ptr(e)}; const BString g(group); s.npc_set_group(&h, g.get()); });
    };

    // ---- skills / attributes ------------------------------------------------------------------------------
    ut["hp"] = [&s](const LuaEntity& e) { return tryGet<int>([&] { EntityHolder h{ptr(e)}; return s.sk_get_hp(&h); }); };
    ut["hp_max"] = [&s](const LuaEntity& e) { return tryGet<int>([&] { EntityHolder h{ptr(e)}; return s.sk_get_hp_max(&h); }); };
    ut["level"] = [&s](const LuaEntity& e) { return tryGet<int>([&] { EntityHolder h{ptr(e)}; return s.sk_get_level(&h); }); };
    ut["mana"] = [&s](const LuaEntity& e) { return tryGet<int>([&] { EntityHolder h{ptr(e)}; return s.sk_get_mana(&h); }); };
    // The engine's PSSkills::Set*(value, 0) ADDS `value` to the current total (observed: level 5 + 7 = 12, then
    // +3 = 15), so the absolute setters below add the difference to the target instead.
    ut["add_hp"] = [&s](const LuaEntity& e, int d) { return tryDo([&] { EntityHolder h{ptr(e)}; s.sk_set_hp(&h, d, kSkillModifierBase); }); };
    ut["add_hp_max"] = [&s](const LuaEntity& e, int d) { return tryDo([&] { EntityHolder h{ptr(e)}; s.sk_set_hp_max(&h, d, kSkillModifierBase); }); };
    ut["add_level"] = [&s](const LuaEntity& e, int d) { return tryDo([&] { EntityHolder h{ptr(e)}; s.sk_set_level(&h, d, kSkillModifierBase); }); };
    ut["add_mana"] = [&s](const LuaEntity& e, int d) { return tryDo([&] { EntityHolder h{ptr(e)}; s.sk_set_mana(&h, d, kSkillModifierBase); }); };
    ut["add_mana_max"] = [&s](const LuaEntity& e, int d) { return tryDo([&] { EntityHolder h{ptr(e)}; s.sk_set_mana_max(&h, d, kSkillModifierBase); }); };
    ut["set_hp_max"] = [&s](const LuaEntity& e, int v) {
        return tryDo([&] { EntityHolder h{ptr(e)}; s.sk_set_hp_max(&h, v - s.sk_get_hp_max(&h), kSkillModifierBase); });
    };
    // Order matters for hit points: raise the maximum first (set_hp_max), then set_hp.
    ut["set_hp"] = [&s](const LuaEntity& e, int v) {
        return tryDo([&] { EntityHolder h{ptr(e)}; s.sk_set_hp(&h, v - s.sk_get_hp(&h), kSkillModifierBase); });
    };
    ut["set_level"] = [&s](const LuaEntity& e, int v) {
        return tryDo([&] { EntityHolder h{ptr(e)}; s.sk_set_level(&h, v - s.sk_get_level(&h), kSkillModifierBase); });
    };
    ut["set_mana"] = [&s](const LuaEntity& e, int v) {
        return tryDo([&] { EntityHolder h{ptr(e)}; s.sk_set_mana(&h, v - s.sk_get_mana(&h), kSkillModifierBase); });
    };
    ut["mana_max"] = [&s](const LuaEntity& e) { return tryGet<int>([&] { EntityHolder h{ptr(e)}; return s.sk_get_mana_max(&h); }); };
    ut["set_mana_max"] = [&s](const LuaEntity& e, int v) {
        return tryDo([&] { EntityHolder h{ptr(e)}; s.sk_set_mana_max(&h, v - s.sk_get_mana_max(&h), kSkillModifierBase); });
    };

    // ---- AI / movement -----------------------------------------------------------------------------------
    // entity:set_state("ZS_HangAround")  -- name of a registered AI state script (docs/LUA_API.md lists them)
    ut["set_state"] = [&s](const LuaEntity& e, const std::string& state) {
        return tryDo([&] {
            EntityHolder h{ptr(e)};
            ByValueString name(state);  // by-value bCString: the callee destroys it
            s.rt_set_state(&h, name.ptr());
        });
    };
    ut["state"] = [&s](const LuaEntity& e) {
        return tryGet<std::string>([&] {
            EntityHolder h{ptr(e)};
            OutString out;
            s.rt_get_state(&h, out.ptr());
            return out.take();
        });
    };
    ut["full_stop"] = [&s](const LuaEntity& e) { return tryDo([&] { EntityHolder h{ptr(e)}; s.rt_full_stop(&h); }); };
    // entity:goto(target_entity [, walk_mode]) or entity:goto(x, y, z [, walk_mode])
    ut["goto_entity"] = [&s](const LuaEntity& e, const LuaEntity& target, sol::optional<int> mode) {
        return tryGet<bool>([&] {
                   EntityHolder h{ptr(e)};
                   EntityHolder t{ptr(target)};
                   return s.ent_goto_entity(&h, &t, mode.value_or(0));
               })
            .value_or(false);
    };
    ut["goto_pos"] = [&s](const LuaEntity& e, float x, float y, float z, sol::optional<int> mode) {
        return tryGet<bool>([&] {
                   EntityHolder h{ptr(e)};
                   const float p[3] = {x, y, z};
                   return s.ent_goto_pos(&h, p, mode.value_or(0));
               })
            .value_or(false);
    };
    ut["stop_goto"] = [&s](const LuaEntity& e) { return tryDo([&] { EntityHolder h{ptr(e)}; s.ent_stop_goto(&h); }); };
    ut["turn_to"] = [&s](const LuaEntity& e, const LuaEntity& target) {
        return tryGet<bool>([&] {
                   EntityHolder h{ptr(e)};
                   EntityHolder t{ptr(target)};
                   return s.ent_turn_to(&h, &t);
               })
            .value_or(false);
    };

    // ---- gear --------------------------------------------------------------------------------------------
    // entity:equip_all() equips every inventory stack that can be equipped; returns how many succeeded.
    ut["equip_all"] = [&s](const LuaEntity& e) {
        return tryGet<int>([&] {
                   EntityHolder h{ptr(e)};
                   const int n = s.inv_stack_count(&h);
                   int ok = 0;
                   for (int i = 0; i < n; ++i) {
                       ok += s.inv_equip_stack(&h, i) ? 1 : 0;
                   }
                   return ok;
               })
            .value_or(0);
    };
    ut["stack_count"] = [&s](const LuaEntity& e) { return tryGet<int>([&] { EntityHolder h{ptr(e)}; return s.inv_stack_count(&h); }); };
}

// risen.npc.spawn(template, x, y, z [, opts]) -> Entity | nil
//   opts: name, hp, hp_max, level, mana, guild, group, state, items = { {"It_1H_Sword", 1}, ... }, equip (bool)
// written in Lua on top of the primitives above so mods can read and copy it.
void bindNpcHelpers(sol::state_view lua, sol::table risen) {
    sol::table npc = lua.create_table();
    risen["npc"] = npc;
    const char* chunk = R"LUA(
local risen = ...
function risen.npc.spawn(template, x, y, z, opts)
    opts = opts or {}
    local e = risen.entity.spawn(template, x, y, z)
    if not e then return nil end
    if opts.name then e:set_name(opts.name) end
    if opts.level then e:set_level(opts.level) end
    if opts.hp_max then e:set_hp_max(opts.hp_max) end
    if opts.hp then e:set_hp(opts.hp) elseif opts.hp_max then e:set_hp(opts.hp_max) end
    if opts.mana then e:set_mana_max(opts.mana); e:set_mana(opts.mana) end
    if opts.guild then e:set_guild(opts.guild) end
    if opts.group then e:set_group(opts.group) end
    for _, it in ipairs(opts.items or {}) do risen.item.give(e, it[1], it[2] or 1) end
    if opts.equip then e:equip_all() end
    if opts.state then e:set_state(opts.state) end
    return e
end
)LUA";
    sol::load_result loaded = lua.load(chunk, "=risen.npc");
    if (!loaded.valid()) {
        spdlog::error("lua: npc helpers failed to load: {}", loaded.get<sol::error>().what());
        return;
    }
    sol::protected_function fn = loaded;
    sol::protected_function_result r = fn(risen);
    if (!r.valid()) {
        spdlog::error("lua: npc helpers failed to run: {}", r.get<sol::error>().what());
    }
}

}  // namespace spass::game
