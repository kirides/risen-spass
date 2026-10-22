#include <cmath>
#include <cstring>
#include <string>
#include <tuple>
#include <vector>

#include <spdlog/spdlog.h>

#include "game/abi.h"
#include "game/lua_entity.h"
#include "game/properties.h"

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

    // entity:align({x, y, z} [, immediate]) / entity:align(nil): gCCharacterMovement_PS::SetAlignmentLookAt, the world direction the character's body turns toward
    // (y ignored) with its own turn speed; it stays until replaced (the engine's own writers: the camera-space movement with CharacterRotationInCameraSpace, the
    // vanilla turn input). nil / a zero vector clears it (the character then faces its goal pose). Clears an alignment target entity (RE.md section 61).
    ut["align"] = [&s](sol::this_state ts, const LuaEntity& e, sol::object dir, sol::optional<bool> immediate) -> std::tuple<sol::object, sol::object> {
        sol::state_view L(ts);
        auto fail = [&](const char* why) { return std::make_tuple(sol::make_object(L, sol::lua_nil), sol::make_object(L, why)); };
        if (s.cm_set_alignment_lookat == nullptr) return fail("engine function gCCharacterMovement_PS::SetAlignmentLookAt is unavailable");
        float d[3] = {0, 0, 0};
        if (dir.get_type() == sol::type::table) {
            sol::table t = dir.as<sol::table>();
            const char* keys[3] = {"x", "y", "z"};
            for (int i = 0; i < 3; ++i) {
                sol::object v = t[i + 1];
                if (v.get_type() != sol::type::number) v = t[keys[i]];
                if (v.get_type() != sol::type::number) return fail("direction must be {x, y, z}");
                const double n = v.as<double>();
                if (!std::isfinite(n) || std::fabs(n) > 1e7) return fail("direction must be finite");
                d[i] = static_cast<float>(n);
            }
        } else if (dir.get_type() != sol::type::lua_nil) {
            return fail("direction must be {x, y, z} or nil");
        }
        void* ent = ptr(e);
        if (ent == nullptr) return fail("stale entity");
        void* ps = nullptr;
        if (s.ent_get_property_set != nullptr) {
            guarded([&] {
                const BString cls("gCCharacterMovement_PS");
                ps = s.ent_get_property_set(ent, cls.get());
            });
        }
        if (ps == nullptr) return fail("the entity has no gCCharacterMovement_PS");
        if (!guarded([&] { s.cm_set_alignment_lookat(ps, d, immediate.value_or(false)); })) return fail("the engine raised an exception");
        return std::make_tuple(sol::make_object(L, true), sol::make_object(L, sol::lua_nil));
    };

    // ---- any property of any property set, by name ----------------------------------------------------------------
    // entity:get_property("gCNPC_PS", "Guild") -> "gEGuild_..." (enums by name, numbers/bools as text)
    // entity:set_property("gCNPC_PS", "Guild", "gEGuild_...") -> bool. The set is named by its engine class (gCNPC_PS,
    // gCInventory_PS, gCSkills_PS, gCItem_PS, gCScriptRoutine_PS, ...); nil/false when the entity lacks the set or the property.
    ut["get_property"] = [&s](const LuaEntity& e, const std::string& set, const std::string& name) -> sol::optional<std::string> {
        void* ps = nullptr;
        if (s.ent_get_property_set == nullptr || !guarded([&] {
                const BString cls(set);
                ps = s.ent_get_property_set(ptr(e), cls.get());
            }) || ps == nullptr) {
            return sol::nullopt;
        }
        return propertyGet(ps, name);
    };
    ut["list_properties"] = [&s](sol::this_state ts, const LuaEntity& e, const std::string& set) -> sol::object {
        void* ps = nullptr;
        if (s.ent_get_property_set == nullptr || !guarded([&] {
                const BString cls(set);
                ps = s.ent_get_property_set(ptr(e), cls.get());
            }) || ps == nullptr) {
            return sol::make_object(ts, sol::lua_nil);
        }
        return sol::make_object(ts, propertyListTable(ts, ps));
    };
    ut["set_property"] = [&s](const LuaEntity& e, const std::string& set, const std::string& name, const std::string& value) {
        void* ps = nullptr;
        if (s.ent_get_property_set == nullptr || !guarded([&] {
                const BString cls(set);
                ps = s.ent_get_property_set(ptr(e), cls.get());
            }) || ps == nullptr) {
            return false;
        }
        return propertySet(ps, name, value);
    };

    // ---- area names (M5e): an entity made from the template AreaZone_Default is an area (a box of +-500 units around it) whose `PropertyNameList` names what the
    // area means to the scripts: SL_Public, SL_Watched, SL_Forbidden, SL_Inquisition, SL_Graveyard, SL_Vulcan, SL_Casern (security levels; taking an item inside one is
    // theft to the AssessTheft script). The list is a bTObjArray<bCString> {data*, count, capacity} that the generic property mechanism cannot write.
    ut["area_names"] = [&s](sol::this_state ts, const LuaEntity& e) -> sol::object {
        sol::state_view L(ts);
        void* ps = nullptr;
        if (s.ent_get_property_set == nullptr || s.area_access_names == nullptr || !guarded([&] {
                const BString cls("eCArea_StringProperty_PS");
                ps = s.ent_get_property_set(ptr(e), cls.get());
            }) || ps == nullptr) {
            return sol::make_object(L, sol::lua_nil);
        }
        sol::table out = L.create_table();
        guarded([&] {
            const char* array = static_cast<const char*>(s.area_access_names(ps));
            const char* data = *reinterpret_cast<const char* const*>(array);
            const int n = *reinterpret_cast<const int*>(array + 8);
            for (int i = 0; i < n && i < 64 && data != nullptr; ++i) {
                out[i + 1] = std::string(s.bstring_get_text(data + static_cast<std::size_t>(i) * 8));
            }
        });
        return sol::make_object(L, out);
    };
    ut["set_area_names"] = [&s](const LuaEntity& e, sol::object list) -> bool {
        if (!list.valid() || list.get_type() != sol::type::table) {
            return false;
        }
        std::vector<std::string> names;
        for (const auto& kv : list.as<sol::table>()) {
            if (kv.second.get_type() != sol::type::string || names.size() >= 32) {
                return false;
            }
            const std::string name = kv.second.as<std::string>();
            if (name.empty() || name.size() > 60) {
                return false;
            }
            names.push_back(name);
        }
        void* ps = nullptr;
        if (s.ent_get_property_set == nullptr || s.area_access_names == nullptr || s.mem_realloc == nullptr || s.mem_free == nullptr || !guarded([&] {
                const BString cls("eCArea_StringProperty_PS");
                ps = s.ent_get_property_set(ptr(e), cls.get());
            }) || ps == nullptr) {
            return false;
        }
        bool ok = false;
        guarded([&] {
            char* array = static_cast<char*>(s.area_access_names(ps));
            auto*& data = *reinterpret_cast<char**>(array);
            auto& count = *reinterpret_cast<int*>(array + 8);
            auto& capacity = *reinterpret_cast<int*>(array + 12);
            char* fresh = nullptr;
            if (!names.empty()) {
                fresh = static_cast<char*>(s.mem_realloc(s.mem_instance(), nullptr, static_cast<unsigned long>(names.size()) * 8, nullptr, nullptr, nullptr, -1));
                if (fresh == nullptr) {
                    return;
                }
                std::memset(fresh, 0, names.size() * 8);
                for (std::size_t i = 0; i < names.size(); ++i) {
                    s.bstring_ctor_cstr(fresh + i * 8, names[i].c_str());
                }
            }
            for (int i = 0; i < count && data != nullptr; ++i) {  // the old elements and block go back to the engine
                s.bstring_dtor(data + static_cast<std::size_t>(i) * 8);
            }
            if (data != nullptr) {
                s.mem_free(s.mem_instance(), data);
            }
            data = fresh;
            count = static_cast<int>(names.size());
            capacity = count;  // the engine treats the whole capacity as constructed elements
            ok = true;
        });
        return ok;
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
    // entity:stack_item(i) -> Entity | nil: the item entity that represents inventory stack `i` (0-based); its item_value() etc. reflect the template
    ut["stack_item"] = [&s](sol::this_state ts, const LuaEntity& e, int stack) -> sol::object {
        EntityHolder ret;
        if (s.inv_template_item == nullptr || stack < 0 || !guarded([&] { EntityHolder h{ptr(e)}; s.inv_template_item(&h, &ret, stack); })) {
            return sol::make_object(ts, sol::lua_nil);
        }
        return makeEntityObject(ts, ret.entity);
    };
    ut["stack_count"] = [&s](const LuaEntity& e) { return tryGet<int>([&] { EntityHolder h{ptr(e)}; return s.inv_stack_count(&h); }); };
}

// risen.npc.spawn(template, x, y, z [, opts]) -> Entity | nil
//   opts: name, hp, hp_max, level, mana, guild (number or name), group, peaceful, state (an AI state, started in the NPC's first engine frame), items = { {"It_1H_Sword", 1}, ... }, equip
// written in Lua on top of the primitives above so mods can read and copy it.
void bindNpcHelpers(sol::state_view lua, sol::table risen) {
    sol::table npc = lua.create_table();
    risen["npc"] = npc;
    const char* chunk = R"LUA(
local risen = ...
-- gEGuild_* numbers (verified in-game, docs/RE.md section 19)
risen.npc.guilds = { None = 0, Don = 1, Dig = 2, Grd = 3, Cit = 4, Inq = 5, Mag = 6, Pir = 7 }

-- Guild given as a number or a name ("Pir", "gEGuild_Pir") -> number, or nil
local function guild_number(g)
    if type(g) == "number" then return g end
    if type(g) == "string" then return risen.npc.guilds[(g:gsub("^gEGuild_", ""))] end
    return nil
end

function risen.npc.spawn(template, x, y, z, opts)
    opts = opts or {}
    local guild = guild_number(opts.guild)
    if opts.guild ~= nil and guild == nil then
        risen.log.warn("npc.spawn: unknown guild '" .. tostring(opts.guild) .. "' (None, Don, Dig, Grd, Cit, Inq, Mag, Pir)")
        return nil
    end
    -- "peaceful": citizens (and diggers) are left alone by pirates, guards and monsters do not provoke them
    if opts.peaceful and guild == nil then guild = risen.npc.guilds.Cit end
    local e = risen.entity.spawn(template, x, y, z)
    if not e then return nil end
    if opts.name then e:set_name(opts.name) end
    if opts.level then e:set_level(opts.level) end
    if opts.hp_max then e:set_hp_max(opts.hp_max) end
    if opts.hp then e:set_hp(opts.hp) elseif opts.hp_max then e:set_hp(opts.hp_max) end
    if opts.mana then e:set_mana_max(opts.mana); e:set_mana(opts.mana) end
    if guild then e:set_guild(guild) end
    if opts.group then e:set_group(opts.group) end
    for _, it in ipairs(opts.items or {}) do risen.item.give(e, it[1], it[2] or 1) end
    if opts.equip then e:equip_all() end
    if opts.state then
        -- a state set before the NPC's first engine frame is lost (RE.md section 36): risen.ai.start waits for that frame (and takes the game's states as well as Lua's)
        local started = risen.ai and risen.ai.start(e, opts.state)
        if not started then e:set_state(opts.state) end
    end
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
