#pragma once

// The Lua part of NPC balancing (docs/RE.md section 24, docs/LUA_API.md "Balancing NPCs"): risen.npc.stats / set_stats, the `npcs`,
// `npc_groups` and `item_groups` sections of risen.balance and risen.balance.scale_npcs. Pure Lua on top of the native functions
// risen.npc.skill_raw / set_skill_base / template_inventory / template_give / template_take / combat_species, risen.template.* and
// the Entity property methods, so the unit tests run it against mocks.
namespace spass::game {

inline const char* kNpcBalanceChunk1 = R"LUA(
local risen = ...
local npc = risen.npc
local balance = risen.balance

-- ---- names ---------------------------------------------------------------------------------------------------------------------

-- gESkill ids (docs/RE.md section 23): friendly name, engine suffix, id
local SKILLS = {
    { "HitPoints", "Atrib_HP", 0 }, { "ManaPoints", "Atrib_MP", 1 }, { "Level", "Stat_LV", 2 }, { "Experience", "Stat_XP", 3 },
    { "LearnPoints", "Stat_LP", 4 }, { "MaxHitPoints", "Stat_HP", 5 }, { "MaxManaPoints", "Stat_MP", 6 },
    { "Strength", "Stat_STR", 7 }, { "Dexterity", "Stat_DEX", 8 }, { "Intelligence", "Stat_INT", 9 },
    { "ProtEdge", "Prot_Edge", 10 }, { "ProtBlunt", "Prot_Blunt", 11 }, { "ProtPoint", "Prot_Point", 12 },
    { "ProtFire", "Prot_Fire", 13 }, { "ProtIce", "Prot_Ice", 14 }, { "ProtMagic", "Prot_Magic", 15 },
    { "Sword", "Combat_Sword", 16 }, { "Axe", "Combat_Axe", 17 }, { "Staff", "Combat_Staff", 18 }, { "Bow", "Combat_Bow", 19 },
    { "CrossBow", "Combat_CrossBow", 20 }, { "Circle", "Magic_Circle", 21 }, { "Fireball", "Magic_Fireball", 22 },
    { "Frost", "Magic_Frost", 23 }, { "Missile", "Magic_Missile", 24 }, { "Smith", "Misc_Smith", 25 }, { "Mining", "Misc_Mining", 26 },
    { "Lockpick", "Misc_Lockpick", 27 }, { "Pickpocket", "Misc_Pickpocket", 28 }, { "Sneak", "Misc_Sneak", 29 },
    { "Acrobat", "Misc_Acrobat", 30 }, { "Alchemy", "Misc_Alchemy", 31 }, { "Scribe", "Misc_Scribe", 32 }, { "Trophy", "Misc_Trophy", 33 },
}
local ID_OF, NAME_OF, SKILL_NAMES = {}, {}, {}
for _, s in ipairs(SKILLS) do
    ID_OF[s[1]] = s[3]
    ID_OF[s[2]] = s[3]
    ID_OF["gESkill_" .. s[2]] = s[3]
    NAME_OF[s[3]] = s[1]
    SKILL_NAMES[#SKILL_NAMES + 1] = s[1]
end
local SKILL_LIST = table.concat(SKILL_NAMES, ", ")
local ID_HP, ID_MANA, ID_LEVEL, ID_HP_MAX, ID_MANA_MAX, ID_STR = 0, 1, 2, 5, 6, 7

-- enum-valued settings: key -> { set, property, enum prefix, valid short names }
local ENUMS = {
    guild = { "gCNPC_PS", "Guild", "gEGuild_", { "None", "Don", "Dig", "Grd", "Cit", "Inq", "Mag", "Pir" } },
    species = { "gCNPC_PS", "Species", "gESpecies_", { "None", "Ashbeast", "Boar", "Brontok", "Chicken", "Cow", "Dragonfly", "Ghost", "Gnome",
        "Human", "Lizard", "Lurker", "Mantis", "Nautilus", "Ogre", "Ogre_Tame", "Pig", "Rotworm", "Scorpion", "Skeleton", "Skeleton_Tame",
        "Stingrat", "Swampmummy", "Thundertail", "Vulture", "Wolf", "Wolf_Tame" } },
    bravery = { "gCNPC_PS", "BraveryOverride", "gEBraveryOverride_", { "None", "Brave", "Coward" } },
    attitude = { "gCNPC_PS", "AttitudeLock", "gEAttitude_", { "None", "Neutral", "Friendly", "Angry", "Hostile" } },
    damage_calc = { "gCNPC_PS", "DamageCalculationType", "gEDamageCalculationType_", { "Normal", "Monster", "Immortal" } },
    damage_type = { "gCDamage_PS", "DamageType", "gEDamageType_", { "None", "Blunt", "Edge", "Fire", "Ice", "Magic", "Physics", "Point" } },
}
local GUILD_NUMBERS = { [0] = "None", "Don", "Dig", "Grd", "Cit", "Inq", "Mag", "Pir" }
local TEXTS = { ai_group = { "gCNPC_PS", "Group" }, combat_species = { "gCCombatSystem_PS", "CombatSpecies" } }
local DAMAGE = { "gCDamage_PS", "DamageAmount" }

-- the keys of one `npcs` entry
local NPC_KEYS = { "ai_group", "attitude", "bravery", "combat_species", "damage", "damage_calc", "damage_mult", "damage_type", "guild", "hp", "hp_max", "hp_mult",
    "level", "level_add", "level_mult", "loot", "mana", "mana_max", "mana_mult", "properties", "skills", "skills_add", "skills_mult", "species", "xp", "xp_mult" }
local NPC_KEY = {}
for _, k in ipairs(NPC_KEYS) do NPC_KEY[k] = true end
local NPC_KEY_LIST = table.concat(NPC_KEYS, ", ")

local function sorted_keys(t)
    local keys = {}
    for k in pairs(t) do keys[#keys + 1] = k end
    table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
    return keys
end
local function round(x) return x >= 0 and math.floor(x + 0.5) or -math.floor(-x + 0.5) end
local function is_int(v) return type(v) == "number" and v == math.floor(v) and math.abs(v) < 2 ^ 31 end
local function strip(prefix, v) return type(v) == "string" and v:sub(1, #prefix) == prefix and v:sub(#prefix + 1) or v end

local function has(list, v) for _, x in ipairs(list) do if x == v then return true end end return false end

-- one enum setting -> the engine's text ("gEGuild_Pir") | nil, problem
local function enum_text(key, v)
    local e = ENUMS[key]
    if key == "guild" and type(v) == "number" then v = GUILD_NUMBERS[v] end
    if type(v) ~= "string" then return nil, ("%s: expected a name (valid: %s)"):format(key, table.concat(e[4], ", ")) end
    local short = strip(e[3], v)
    if not has(e[4], short) then return nil, ("%s: unknown value '%s' (valid: %s)"):format(key, v, table.concat(e[4], ", ")) end
    return e[3] .. short
end

-- ---- reading ---------------------------------------------------------------------------------------------------------------------

-- a template name, a Template handle or an Entity -> template name | nil, entity | nil
local function split_target(x)
    if type(x) == "string" then return x, nil end
    local s = tostring(x)
    local name = s:match("^Template%((.*)%)$")
    if name then return name, nil end
    if s:match("^Entity%(") then return nil, x end
    return nil, nil
end

local function get_prop(t, e, set, prop)
    if t then
        local tpl = risen.template.get(t)
        return tpl and tpl:get_property(set, prop)
    end
    return e:get_property(set, prop)
end

local SMALL = { Chicken = true, Nautilus = true }  -- the species the engine's IsSmall script accepts (verified live)

-- experience a kill gives: the GiveXP script (Script_Game) pays 10 * level, at least 50 (for IsSmall species 10 * level)
local function xp_reward(level, species)
    local v = math.max(level, 1) * 10
    if SMALL[species or ""] then return v end
    return math.max(v, 50)
end

local function num(v) return tonumber(v) end

-- risen.npc.stats(target [, { base = true }]) -> table | nil, reason
function risen.npc.stats(target, opts)
    opts = opts or {}
    local t, e = split_target(target)
    if not t and not e then return nil, "expected a template name, a Template or an NPC entity" end
    local ref = t or e
    local out = { base = opts.base and true or false, skills = {} }
    if t then out.template = t else
        local tpl = e:template()
        out.template = tpl and tostring(tpl):match("^Template%((.*)%)$") or nil
    end
    for id = 0, 33 do
        local b, m, tot = npc.skill_raw(ref, id)
        if b == nil then return nil, m end
        local v = opts.base and b or tot
        local name = NAME_OF[id]
        if id == 0 then out.hp = v elseif id == 1 then out.mana = v elseif id == 5 then out.hp_max = v elseif id == 6 then out.mana_max = v
        elseif id == 2 then out.level = v elseif id == 3 then out.experience = v
        elseif id ~= 4 then out.skills[name] = v end
    end
    local function p(set, prop) return get_prop(t, e, set, prop) end
    out.guild = strip("gEGuild_", p("gCNPC_PS", "Guild"))
    out.species = strip("gESpecies_", p("gCNPC_PS", "Species"))
    out.bravery = strip("gEBraveryOverride_", p("gCNPC_PS", "BraveryOverride"))
    out.attitude = strip("gEAttitude_", p("gCNPC_PS", "AttitudeLock"))
    out.damage_calc = strip("gEDamageCalculationType_", p("gCNPC_PS", "DamageCalculationType"))
    out.ai_group = p("gCNPC_PS", "Group")
    out.xp_reward = xp_reward(out.level or 1, out.species)
    out.damage = { amount = num(p("gCDamage_PS", "DamageAmount")), type = strip("gEDamageType_", p("gCDamage_PS", "DamageType")) }
    local cs = p("gCCombatSystem_PS", "CombatSpecies")
    out.combat = { species = cs }
    if cs and cs ~= "" then
        for _, prop in ipairs(npc.combat_species(cs) or {}) do
            if prop.name == "RightFistItem" and prop.value ~= "(no template)" then out.combat.right_fist = prop.value end
            if prop.name == "LeftFistItem" and prop.value ~= "(no template)" then out.combat.left_fist = prop.value end
        end
    end
    if t then out.loot = npc.template_inventory(t) else out.stacks = e:stack_count() end
    return out
end

-- ---- planning: validate and resolve one settings table to absolute values ---------------------------------------------------------

-- What the unscaled template holds: the values captured the first time a template is touched in this process. Relative settings
-- (hp_mult, level_add, ...) are always computed from these, so applying a spec twice -- first tick, then every session.start, then on
-- live entities that were saved with the first result -- gives the same numbers instead of compounding.
local shipped = {}
local not_npc = {}

local function capture(name)
    if shipped[name] then return shipped[name] end
    if not_npc[name] then return nil, not_npc[name] end
    local c = { sk = {}, lim = {} }
    for id = 0, 33 do
        local b, m, _, mn, mx = npc.skill_raw(name, id)
        if b == nil then
            if tostring(m):find("no property set", 1, true) then not_npc[name] = m end  -- a settled answer; other failures may pass
            return nil, m
        end
        c.sk[id], c.lim[id] = b, { mn, mx }
    end
    local tpl = risen.template.get(name)
    c.damage = tpl and num(tpl:get_property("gCDamage_PS", "DamageAmount")) or nil
    c.species = tpl and strip("gESpecies_", tpl:get_property("gCNPC_PS", "Species")) or nil
    shipped[name] = c
    return c
end

-- the same shape for one live entity: its own current base values (relative settings on a single entity are relative to the entity)
local function capture_entity(e)
    local c = { sk = {}, lim = {}, own = true }
    for id = 0, 33 do
        local b, m, _, mn, mx = npc.skill_raw(e, id)
        if b == nil then return nil, m end
        c.sk[id], c.lim[id] = b, { mn, mx }
    end
    c.damage = num(e:get_property("gCDamage_PS", "DamageAmount"))
    c.species = strip("gESpecies_", e:get_property("gCNPC_PS", "Species"))
    return c
end

-- entry (one settings table) + the reference values -> plan | nil, problems
--   plan.sk[id] = base value to write; plan.hp_start / plan.mana_start = { value, explicit } current points for a full NPC;
--   plan.props = { { set, prop, text } }; plan.loot = { item = amount }; plan.notes = { "..." } (clamped values, ...)
local function make_plan(entry, ref, what)
    local plan = { sk = {}, props = {}, notes = {}, any = false }
    local problems = {}
    local function bad(msg) problems[#problems + 1] = what .. ": " .. msg end
    if type(entry) ~= "table" then return nil, { what .. ": expected a table of settings" } end
    for key in pairs(entry) do
        if not NPC_KEY[key] then bad(("unknown setting '%s' (valid: %s)"):format(tostring(key), NPC_KEY_LIST)) end
    end
    local function number_setting(key)
        local v = entry[key]
        if v == nil then return nil end
        if not is_int(v) then bad(("%s must be an integer, got %s"):format(key, tostring(v))) return nil end
        return v
    end
    local function factor(key)
        local v = entry[key]
        if v == nil then return nil end
        if type(v) ~= "number" or v < 0 then bad(("%s must be a number >= 0, got %s"):format(key, tostring(v))) return nil end
        return v
    end
    local function conflict(a, b) if entry[a] ~= nil and entry[b] ~= nil then bad(("%s and %s exclude each other"):format(a, b)) return true end end

    -- every skill id a setting names: abs[id], mult[id], add[id]
    local abs, mult, add = {}, {}, {}
    local function skill_table(key, into, check)
        local t = entry[key]
        if t == nil then return end
        if type(t) ~= "table" then bad(key .. " must be a table { Strength = 40, ... }") return end
        for _, name in ipairs(sorted_keys(t)) do
            local id = ID_OF[name]
            local v = t[name]
            if not id then bad(("%s: unknown skill '%s' (valid: %s)"):format(key, tostring(name), SKILL_LIST))
            elseif not check(v) then bad(("%s.%s: unsuitable value %s"):format(key, tostring(name), tostring(v)))
            else
                if into[id] ~= nil then bad(("%s.%s given twice"):format(key, tostring(name))) end
                into[id] = v
            end
        end
    end
    skill_table("skills", abs, is_int)
    skill_table("skills_mult", mult, function(v) return type(v) == "number" and v >= 0 end)
    skill_table("skills_add", add, is_int)

    -- hit points, mana points, level
    local function pool(maxkey, curkey, multkey, max_id, cur_id)
        if conflict(maxkey, multkey) then return end
        local nmax = number_setting(maxkey)
        local ncur = number_setting(curkey)
        local m = factor(multkey)
        if nmax ~= nil then abs[max_id] = nmax end
        if m ~= nil and mult[max_id] == nil then mult[max_id] = m end
        plan[curkey .. "_explicit"] = ncur
        if ncur ~= nil and (ncur < 0) then bad(curkey .. " must not be negative") end
    end
    pool("hp_max", "hp", "hp_mult", ID_HP_MAX, ID_HP)
    pool("mana_max", "mana", "mana_mult", ID_MANA_MAX, ID_MANA)
    conflict("level", "xp"); conflict("level", "level_mult"); conflict("xp", "level_mult"); conflict("level", "xp_mult"); conflict("xp", "xp_mult")
    conflict("level_mult", "xp_mult")
    local lv = number_setting("level")
    local xp = number_setting("xp")
    if xp ~= nil then
        if xp < 0 then bad("xp must not be negative")
        else
            lv = math.max(1, round(xp / 10))
            if xp < 50 and not SMALL[ref.species or ""] then
                plan.notes[#plan.notes + 1] = ("xp %d is below the engine's minimum reward of 50 for this species"):format(xp)
            end
        end
    end
    if lv ~= nil then abs[ID_LEVEL] = lv end
    local lm = factor("level_mult") or factor("xp_mult")
    if lm ~= nil then mult[ID_LEVEL] = lm end
    local la = number_setting("level_add")
    if la ~= nil then add[ID_LEVEL] = la end
    -- damage_mult: the NPC-side terms of the melee damage formula: Strength (and the NPC's own gCDamage_PS.DamageAmount, if it has one)
    local dm = factor("damage_mult")
    if dm ~= nil then
        if abs[ID_STR] ~= nil or mult[ID_STR] ~= nil then bad("damage_mult and an explicit Strength exclude each other") else mult[ID_STR] = dm end
    end

    -- resolve to base values
    local seen = {}
    for id in pairs(abs) do seen[id] = true end
    for id in pairs(mult) do seen[id] = true end
    for id in pairs(add) do seen[id] = true end
    for id in pairs(seen) do
        if abs[id] ~= nil and mult[id] ~= nil then bad(("%s: an absolute value and a factor exclude each other"):format(NAME_OF[id]))
        else
            local v
            if abs[id] ~= nil then v = abs[id] else v = ref.sk[id] end
            if mult[id] ~= nil then v = round(v * mult[id]) end
            if add[id] ~= nil then v = v + add[id] end
            local lim = ref.lim[id]
            local lo, hi = lim[1], lim[2]
            if id ~= 0 and id ~= 1 then
                if abs[id] ~= nil and add[id] == nil and ((hi > 0 and v > hi) or v < lo) then
                    bad(("%s: %d is outside the allowed range %d..%s"):format(NAME_OF[id], v, lo, hi > 0 and tostring(hi) or "unlimited"))
                else
                    if hi > 0 and v > hi then v = hi; plan.notes[#plan.notes + 1] = ("%s clamped to %d"):format(NAME_OF[id], hi) end
                    if v < lo then v = lo; plan.notes[#plan.notes + 1] = ("%s raised to %d"):format(NAME_OF[id], lo) end
                end
            end
            plan.sk[id] = v
            plan.any = true
        end
    end
    -- current hit/mana points of a full NPC: the new maximum unless `hp`/`mana` says otherwise
    for _, spec in ipairs({ { "hp", ID_HP_MAX, ID_HP }, { "mana", ID_MANA_MAX, ID_MANA } }) do
        local key, max_id, cur_id = spec[1], spec[2], spec[3]
        local explicit = plan[key .. "_explicit"]
        if explicit ~= nil then
            plan.sk[cur_id] = explicit
            plan[key .. "_start"] = explicit
            plan.any = true
        elseif plan.sk[max_id] ~= nil then
            plan[key .. "_start"] = plan.sk[max_id]
        end
    end
    if plan.sk[ID_HP] ~= nil and plan.sk[ID_HP_MAX] ~= nil and plan.sk[ID_HP] > plan.sk[ID_HP_MAX] then
        bad(("hp %d is above hp_max %d"):format(plan.sk[ID_HP], plan.sk[ID_HP_MAX]))
    end
    if plan.sk[ID_MANA] ~= nil and plan.sk[ID_MANA_MAX] ~= nil and plan.sk[ID_MANA] > plan.sk[ID_MANA_MAX] then
        bad(("mana %d is above mana_max %d"):format(plan.sk[ID_MANA], plan.sk[ID_MANA_MAX]))
    end

    -- enum and text properties
    for _, key in ipairs(sorted_keys(ENUMS)) do
        if entry[key] ~= nil then
            local text, why = enum_text(key, entry[key])
            if text then plan.props[#plan.props + 1] = { ENUMS[key][1], ENUMS[key][2], text } else bad(why) end
        end
    end
    for _, key in ipairs(sorted_keys(TEXTS)) do
        if entry[key] ~= nil then
            if type(entry[key]) ~= "string" then bad(key .. " must be a string")
            else plan.props[#plan.props + 1] = { TEXTS[key][1], TEXTS[key][2], entry[key] } end
        end
    end
    -- the NPC's own damage (gCDamage_PS.DamageAmount: only special monsters use it, e.g. Titan, Inferno)
    conflict("damage", "damage_mult")
    local dmg = number_setting("damage")
    if dmg ~= nil then
        if dmg < 0 then bad("damage must not be negative") else plan.props[#plan.props + 1] = { DAMAGE[1], DAMAGE[2], tostring(dmg) } end
    elseif dm ~= nil and ref.damage and ref.damage > 0 then
        plan.props[#plan.props + 1] = { DAMAGE[1], DAMAGE[2], tostring(round(ref.damage * dm)) }
    end
    if entry.properties ~= nil then
        if type(entry.properties) ~= "table" then bad("properties must be a table { PropertySet = { Property = value } }")
        else
            for _, set in ipairs(sorted_keys(entry.properties)) do
                local props = entry.properties[set]
                if type(props) ~= "table" then bad(("properties.%s must be a table"):format(tostring(set)))
                else
                    for _, prop in ipairs(sorted_keys(props)) do
                        local v = props[prop]
                        local text = type(v) == "number" and (v == math.floor(v) and string.format("%d", v) or string.format("%.9g", v))
                            or type(v) == "boolean" and tostring(v) or type(v) == "string" and v or nil
                        if text == nil then bad(("properties.%s.%s: unsupported value type %s"):format(set, prop, type(v)))
                        else plan.props[#plan.props + 1] = { set, prop, text } end
                    end
                end
            end
        end
    end
    if entry.loot ~= nil then
        if type(entry.loot) ~= "table" then bad("loot must be a table { It_Apple = 3, ... }")
        else
            plan.loot = {}
            for _, item in ipairs(sorted_keys(entry.loot)) do
                local n = entry.loot[item]
                if type(item) ~= "string" or not risen.template.exists(item) then bad(("loot: unknown item template '%s'"):format(tostring(item)))
                elseif not is_int(n) or n < 0 then bad(("loot.%s must be a non-negative integer"):format(item))
                else plan.loot[item] = n end
            end
        end
    end
    plan.any = plan.any or #plan.props > 0 or plan.loot ~= nil
    if #problems > 0 then return nil, problems end
    return plan
end
)LUA";

inline const char* kNpcBalanceChunk2 = R"LUA(
-- ---- applying a plan ---------------------------------------------------------------------------------------------------------------

local WRITE_ORDER = { 2, 3, 4, 5, 6 }
for id = 7, 33 do WRITE_ORDER[#WRITE_ORDER + 1] = id end

local function total_of(target, id)
    local b, m, t = npc.skill_raw(target, id)
    return t, b, m
end

-- Writes the plan into an NPC template (raw storage). Hit/mana points follow the maximum: the template always describes a full NPC.
local function apply_template(name, plan)
    local written = 0
    for _, id in ipairs(WRITE_ORDER) do
        if plan.sk[id] ~= nil then
            local ok, why = npc.set_skill_base(name, id, plan.sk[id])
            if not ok then return nil, ("%s: %s"):format(NAME_OF[id], tostring(why)) end
            written = written + 1
        end
    end
    for _, spec in ipairs({ { "hp_start", ID_HP }, { "mana_start", ID_MANA } }) do
        if plan[spec[1]] ~= nil then
            local ok, why = npc.set_skill_base(name, spec[2], plan[spec[1]])
            if not ok then return nil, ("%s: %s"):format(NAME_OF[spec[2]], tostring(why)) end
            written = written + 1
        end
    end
    if #plan.props > 0 then
        local tpl, why = risen.template.get(name)
        if not tpl then return nil, why end
        for _, pr in ipairs(plan.props) do
            local ok, err = tpl:set_property(pr[1], pr[2], pr[3])
            if not ok then return nil, tostring(err) end
            written = written + 1
        end
    end
    if plan.loot then
        local have = {}
        for _, s in ipairs(npc.template_inventory(name) or {}) do have[s.item] = (have[s.item] or 0) + s.amount end
        for item, want in pairs(plan.loot) do
            local cur = have[item] or 0
            if want > cur then
                local ok, err = npc.template_give(name, item, want - cur)
                if not ok then return nil, ("loot %s: %s"):format(item, tostring(err)) end
            elseif want < cur then
                local n, err = npc.template_take(name, item, cur - want)
                if not n then return nil, ("loot %s: %s"):format(item, tostring(err)) end
            end
            written = written + 1
        end
    end
    return true, written
end

-- Writes the plan into a live entity. Hit points follow the maximum only for an NPC that is at full health: a damaged (or dead) NPC keeps its
-- hit points, the engine only clamps them to a lower maximum. `hp`/`mana` (the explicit current points) are written only for full NPCs too, unless
-- `force` (a single entity named by the caller).
local function apply_entity(e, plan, force)
    local hp, hp_max = total_of(e, ID_HP), total_of(e, ID_HP_MAX)
    local mana, mana_max = total_of(e, ID_MANA), total_of(e, ID_MANA_MAX)
    if hp == nil then return nil, "the entity has no skills" end
    local full_hp = hp_max > 0 and hp >= hp_max
    local full_mana = mana_max > 0 and mana >= mana_max
    for _, id in ipairs(WRITE_ORDER) do
        if plan.sk[id] ~= nil then
            local ok, why = npc.set_skill_base(e, id, plan.sk[id])
            if not ok then return nil, ("%s: %s"):format(NAME_OF[id], tostring(why)) end
        end
    end
    for _, spec in ipairs({ { "hp_start", ID_HP, full_hp, ID_HP_MAX }, { "mana_start", ID_MANA, full_mana, ID_MANA_MAX } }) do
        local start = plan[spec[1]]
        if start ~= nil and (spec[3] or (force and plan[(spec[1]:gsub("_start$", "")) .. "_explicit"] ~= nil)) then
            local _, _, mod = total_of(e, spec[2])
            npc.set_skill_base(e, spec[2], start - (mod or 0))
        end
    end
    for _, pr in ipairs(plan.props) do e:set_property(pr[1], pr[2], pr[3]) end
    return true
end

-- the live entities of NPC templates, with one pass over the scene
local function live_instances(name, census)
    if census and (census[name] or 0) == 0 then return {} end
    local tpl = risen.template.get(name)
    return tpl and tpl:instances(1000000) or {}
end

-- ---- risen.npc.set_stats -----------------------------------------------------------------------------------------------------------

-- risen.npc.set_stats(target, spec [, { instances = true, dry_run = true }]) -> report | nil, reason
-- target: a template name or Template (new spawns; with `instances = true` also every live NPC made from it) or an NPC entity.
-- spec: the settings of one `npcs` entry (docs/LUA_API.md). Numbers are BASE values (what the template stores). Validated before anything is written.
-- report = { template = bool, written = values written into the template/entity, instances = live entities written, notes = { ... } }
function risen.npc.set_stats(target, spec, opts)
    opts = opts or {}
    local t, e = split_target(target)
    if not t and not e then return nil, "expected a template name, a Template or an NPC entity" end
    local ref, why
    if t then ref, why = capture(t) else ref, why = capture_entity(e) end
    if not ref then return nil, (t and ("'" .. t .. "' is not an NPC template: ") or "") .. tostring(why) end
    local plan, problems = make_plan(spec, ref, "set_stats")
    if not plan then return nil, table.concat(problems, "; ") end
    if t and #plan.props > 0 then
        local sets = {}
        for _, pr in ipairs(plan.props) do sets[pr[1]] = sets[pr[1]] or {}; sets[pr[1]][pr[2]] = pr[3] end
        local ok, err = risen.template.patch(t, sets, { dry_run = true })
        if not ok then return nil, tostring(err) end
    end
    local report = { template = t ~= nil, written = 0, instances = 0, notes = plan.notes, dry_run = opts.dry_run and true or false }
    if opts.dry_run then return report end
    if t then
        local ok, n = apply_template(t, plan)
        if not ok then return nil, tostring(n) end
        report.written = n
        if opts.instances then
            for _, inst in ipairs(live_instances(t)) do
                if apply_entity(inst, plan, false) then report.instances = report.instances + 1 end
            end
        end
    else
        local ok, err = apply_entity(e, plan, true)
        if not ok then return nil, tostring(err) end
        report.written = 1
        if plan.loot then
            for item, want in pairs(plan.loot) do
                local cur = risen.item.count(e, item)
                if want > cur then risen.item.give(e, item, want - cur) elseif want < cur then risen.item.take(e, item, cur - want) end
            end
        end
    end
    return report
end

-- ---- balance sections -------------------------------------------------------------------------------------------------------------

local function job_for_template(name, plan, label)
    return {
        label = label or ("%s (%s)"):format(name, "npc"),
        run = function(opts)
            local ok, n = apply_template(name, plan)
            if not ok then return nil, n end
            local touched = 0
            if opts.instances then
                for _, inst in ipairs(live_instances(name, opts.census)) do
                    if apply_entity(inst, plan, false) then touched = touched + 1 end
                end
            end
            return true, nil, touched
        end,
    }
end

local function check_props(name, plan, problems, what)
    if #plan.props == 0 then return end
    local sets = {}
    for _, pr in ipairs(plan.props) do sets[pr[1]] = sets[pr[1]] or {}; sets[pr[1]][pr[2]] = pr[3] end
    local ok, err = risen.template.patch(name, sets, { dry_run = true })
    if not ok then problems[#problems + 1] = what .. ": " .. tostring(err) end
end

local function keys_text(entry)
    local labels = {}
    for _, key in ipairs(NPC_KEYS) do if entry[key] ~= nil then labels[#labels + 1] = key end end
    return table.concat(labels, ", ")
end

-- npcs = { Wolf = { hp_max = 200, level = 8, skills = { Strength = 40 }, guild = "Pir", xp = 50, loot = { It_Apple = 2 }, properties = { ... } } }
local function npcs_section(data, _)
    local jobs, problems = {}, {}
    if type(data) ~= "table" then return jobs, { "npcs: expected a table { TemplateName = { ... } }" } end
    for _, name in ipairs(sorted_keys(data)) do
        local what = "npcs." .. tostring(name)
        if type(name) ~= "string" then problems[#problems + 1] = what .. ": expected a template name"
        else
            local ref, why = capture(name)
            if not ref then
                problems[#problems + 1] = ("%s: %s (NPC and monster names: docs/TEMPLATES.md, risen.template.list('NPC'))"):format(what,
                    why and tostring(why):find("no property set") and "not an NPC template (no gCSkills_PS)" or tostring(why or "not an NPC template"))
            else
                local plan, p = make_plan(data[name], ref, what)
                if not plan then
                    for _, m in ipairs(p) do problems[#problems + 1] = m end
                else
                    check_props(name, plan, problems, what)
                    jobs[#jobs + 1] = job_for_template(name, plan, ("%s (%s)"):format(name, keys_text(data[name])))
                end
            end
        end
    end
    return jobs, problems
end

-- the templates a group selects: group = "Monster/Kapitel 2" (a template group path), match / exclude = Lua patterns on the name, names = { ... };
-- default universe: every template below NPC and Monster (without PC_Hero, the player's template), for items every template below Items
local GROUP_KEYS = { "exclude", "group", "match", "names" }
local function select_templates(g, kind, what, problems)
    for _, k in ipairs({ "group", "match", "exclude" }) do
        if g[k] ~= nil and type(g[k]) ~= "string" then problems[#problems + 1] = ("%s: %s must be a string"):format(what, k) return {} end
    end
    if g.names ~= nil and type(g.names) ~= "table" then problems[#problems + 1] = what .. ": names must be a list of template names" return {} end
    local out, seen = {}, {}
    local function push(n) if not seen[n] then seen[n] = true out[#out + 1] = n end end
    local function from(list, filter)
        for _, n in ipairs(list or {}) do
            local okm = true
            if filter and g.match then
                local ok, r = pcall(string.find, n, g.match)
                if not ok then problems[#problems + 1] = ("%s: bad pattern '%s'"):format(what, g.match) return false end
                okm = r ~= nil
            end
            if okm and filter and g.exclude then
                local ok, r = pcall(string.find, n, g.exclude)
                if not ok then problems[#problems + 1] = ("%s: bad pattern '%s'"):format(what, g.exclude) return false end
                okm = r == nil
            end
            if okm and not (kind == "npc" and filter and not g.group and n == "PC_Hero") then push(n) end
        end
        return true
    end
    if g.group then
        local list = risen.template.list(g.group)
        if not list or #list == 0 then problems[#problems + 1] = ("%s: no templates in group '%s' (see docs/TEMPLATES.md)"):format(what, g.group) return {} end
        if not from(list, true) then return {} end
    elseif g.match or g.exclude or not g.names then
        if kind == "npc" then
            if not from(risen.template.list("NPC"), true) or not from(risen.template.list("Monster"), true) then return {} end
        else
            if not from(risen.template.list("Items"), true) then return {} end
        end
    end
    for _, n in ipairs(g.names or {}) do push(n) end
    return out
end

local function group_keys_ok(g, what, extra, problems)
    local valid = {}
    for _, k in ipairs(GROUP_KEYS) do valid[k] = true end
    for _, k in ipairs(extra) do valid[k] = true end
    for k in pairs(g) do
        if not valid[k] then
            local list = {}
            for _, x in ipairs(GROUP_KEYS) do list[#list + 1] = x end
            for _, x in ipairs(extra) do list[#list + 1] = x end
            table.sort(list)
            problems[#problems + 1] = ("%s: unknown setting '%s' (valid: %s)"):format(what, tostring(k), table.concat(list, ", "))
            return false
        end
    end
    return true
end

-- npc_groups = { { match = "^Wolf", hp_mult = 1.5 }, { group = "Monster/Kapitel 2", level_add = 2, skills_mult = { Strength = 1.2 } } }
local function npc_groups_section(data, _)
    local jobs, problems = {}, {}
    if type(data) ~= "table" then return jobs, { "npc_groups: expected a list of { match = ..., hp_mult = ... }" } end
    for i, g in ipairs(data) do
        local what = ("npc_groups[%d]"):format(i)
        if type(g) ~= "table" then problems[#problems + 1] = what .. ": expected a table"
        else
            local entry = {}
            for k, v in pairs(g) do if NPC_KEY[k] then entry[k] = v end end
            local sub = {}
            local keep = group_keys_ok(g, what, NPC_KEYS, sub)
            for _, m in ipairs(sub) do problems[#problems + 1] = m end
            if keep then
                local sel_problems = {}
                local names = select_templates(g, "npc", what, sel_problems)
                for _, m in ipairs(sel_problems) do problems[#problems + 1] = m end
                if #sel_problems == 0 then
                    local plans, skipped = {}, 0
                    local plan_problems = {}
                    for _, name in ipairs(names) do
                        local ref = capture(name)
                        if not ref then skipped = skipped + 1
                        else
                            local plan, p = make_plan(entry, ref, what .. " " .. name)
                            if not plan then
                                for _, m in ipairs(p) do plan_problems[#plan_problems + 1] = m end
                                break  -- the same entry fails for every template: report once
                            end
                            check_props(name, plan, plan_problems, what .. " " .. name)
                            plans[#plans + 1] = { name = name, plan = plan }
                        end
                    end
                    for _, m in ipairs(plan_problems) do problems[#problems + 1] = m end
                    if #plans == 0 and #plan_problems == 0 then
                        problems[#problems + 1] = ("%s: matches no NPC template%s"):format(what, g.match and (" (match '" .. g.match .. "')") or "")
                    elseif #plan_problems == 0 then
                        local group_jobs = {}
                        for _, pl in ipairs(plans) do group_jobs[#group_jobs + 1] = job_for_template(pl.name, pl.plan, pl.name) end
                        jobs[#jobs + 1] = {
                            label = ("%s %s (%d templates: %s)"):format(what, g.group or g.match or "all NPCs", #plans, keys_text(entry)),
                            run = function(opts)
                                local touched = 0
                                if opts.instances and not opts.census then
                                    local census = risen.template.census()  -- one pass over the scene instead of one per template
                                    opts = { instances = true, census = census or false }
                                end
                                for _, j in ipairs(group_jobs) do
                                    local ok, err, n = j.run(opts)
                                    if not ok then return nil, ("%s: %s"):format(j.label, tostring(err)) end
                                    touched = touched + (n or 0)
                                end
                                return true, nil, touched
                            end,
                        }
                    end
                end
            end
        end
    end
    return jobs, problems
end

-- item_groups = { { group = "Items/Potions", value_mult = 0.5 }, { match = "^It_Fist_Monster", damage_mult = 1.2 } }
-- value_mult scales the gold value, damage_mult the weapon damage (gCDamage_PS.DamageAmount) of every selected item that has one; relative to the
-- shipped values, so repeated application does not compound.
local item_shipped = {}
local function item_groups_section(data, opts)
    local jobs, problems = {}, {}
    if type(data) ~= "table" then return jobs, { "item_groups: expected a list of { match = ..., value_mult = ... }" } end
    for i, g in ipairs(data) do
        local what = ("item_groups[%d]"):format(i)
        if type(g) ~= "table" then problems[#problems + 1] = what .. ": expected a table"
        elseif group_keys_ok(g, what, { "damage_mult", "value_mult" }, problems) then
            local vm, dmm = g.value_mult, g.damage_mult
            if (vm ~= nil and (type(vm) ~= "number" or vm < 0)) or (dmm ~= nil and (type(dmm) ~= "number" or dmm < 0)) then
                problems[#problems + 1] = what .. ": value_mult and damage_mult must be numbers >= 0"
            elseif vm == nil and dmm == nil then
                problems[#problems + 1] = what .. ": nothing to do (value_mult, damage_mult)"
            else
                local sel_problems = {}
                local names = select_templates(g, "item", what, sel_problems)
                for _, m in ipairs(sel_problems) do problems[#problems + 1] = m end
                if #sel_problems == 0 then
                    local entries = {}
                    for _, name in ipairs(names) do
                        local tpl = risen.template.get(name)
                        if tpl then
                            local c = item_shipped[name]
                            if not c then
                                c = { value = num(tpl:get_property("gCItem_PS", "GoldValue")), damage = num(tpl:get_property("gCDamage_PS", "DamageAmount")) }
                                item_shipped[name] = c
                            end
                            local entry = {}
                            if vm ~= nil and c.value then entry.value = round(c.value * vm) end
                            if dmm ~= nil and c.damage and c.damage > 0 then entry.damage = round(c.damage * dmm) end
                            if next(entry) then entries[name] = entry end
                        end
                    end
                    if next(entries) == nil then
                        problems[#problems + 1] = ("%s: matches no item with a value or damage to scale"):format(what)
                    else
                        local item_jobs, p = balance.sections.items(entries, opts)
                        for _, m in ipairs(p or {}) do problems[#problems + 1] = m end
                        local count = #item_jobs
                        jobs[#jobs + 1] = {
                            label = ("%s %s (%d items)"):format(what, g.group or g.match or "all items", count),
                            run = function(o)
                                local touched = 0
                                for _, j in ipairs(item_jobs) do
                                    local ok, err, n = j.run(o)
                                    if not ok then return nil, ("%s: %s"):format(j.label, tostring(err)) end
                                    touched = touched + (n or 0)
                                end
                                return true, nil, touched
                            end,
                        }
                    end
                end
            end
        end
    end
    return jobs, problems
end

risen.balance.register_section("npcs", npcs_section)
risen.balance.register_section("npc_groups", npc_groups_section)
risen.balance.register_section("item_groups", item_groups_section)

-- risen.balance.scale_npcs{ filter = "Monster", hp = 1.5, damage = 1.2, level = 1.1, mana = , xp = , skills = { Strength = 1.2 }, instances = true }
-- One-call relative balancing: factors for hit points, damage (Strength and the NPC's own damage), level, mana, experience reward and skills of every
-- NPC template `filter` selects (a template group such as "Monster" / "NPC/Harbour" / "Monster/Kapitel 2", else a Lua pattern on the name; or use
-- group / match / exclude / names). Returns the apply() report. Factors are relative to the shipped values, so calling it twice does not compound.
function risen.balance.scale_npcs(o, apply_opts)
    if type(o) ~= "table" then return nil, "scale_npcs: expected a table { filter = ..., hp = 1.5 }", {} end
    local g = {}
    local alias = { hp = "hp_mult", mana = "mana_mult", level = "level_mult", xp = "xp_mult", damage = "damage_mult", skills = "skills_mult" }
    for k, v in pairs(o) do
        if alias[k] then g[alias[k]] = v
        elseif k == "filter" then
            local list = type(v) == "string" and risen.template.list(v) or nil
            if list and #list > 0 then g.group = v else g.match = v end
        elseif k == "instances" then -- handled below
        else g[k] = v end
    end
    local opts = apply_opts or {}
    if o.instances ~= nil then opts.instances = o.instances end
    return balance.apply({ npc_groups = { g } }, opts)
end
)LUA";

}  // namespace spass::game
