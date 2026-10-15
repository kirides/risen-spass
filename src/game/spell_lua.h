#pragma once

// The Lua part of spells (docs/RE.md section 29, docs/LUA_API.md "Spells"): the spell catalogue (risen.spell.names/info/get), the `spells` section
// of risen.balance (modify the shipped spells), the `new_spells` section and risen.spell.create (new spells made of the pieces of existing ones) and
// the per-shot overrides of new projectile spells. Pure Lua on top of the natives of lua_spell.cpp (equipped, shooter, can_cast, consume),
// risen.template.patch/create and the item sections of risen.balance, so the unit tests run it against mocks.
namespace spass::game {

inline const char* kSpellChunk = R"LUA(
local risen = ...
local spell = risen.spell
local balance = risen.balance
local tmpl = risen.template

local function sorted_keys(t)
    local keys = {}
    for k in pairs(t) do keys[#keys + 1] = k end
    table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
    return keys
end
local function round(x) return x >= 0 and math.floor(x + 0.5) or -math.floor(-x + 0.5) end
local function is_int(v) return type(v) == "number" and v == math.floor(v) and v >= 0 and v < 2 ^ 31 end
local function is_num(v) return type(v) == "number" and v == v and v >= 0 and v < 1e9 end
local function has(list, v) for _, x in ipairs(list) do if x == v then return true end end return false end
local function tr(...) local t = risen.trace if t and t.enabled("balance") then t("balance", ...) end end

-- ---- the catalogue -------------------------------------------------------------------------------------------------------------------
-- Projectile spells are cast with a crystal (equip slot 11); the projectile templates are what the engine spawns when the crystal is used
-- (RE.md section 29). Rune spells come as rune (permanent), scroll (used up) and orb (free, used up) items of UseType Cast.
local PROJECTILE = {
    Fireball = { crystal = "It_Crystal_Fire", skill = "Fireball", projectiles = { "It_Projectile_Fireball_S0", "It_Projectile_Fireball_S1", "It_Projectile_Fireball_S2" } },
    Frost = { crystal = "It_Crystal_Frost", skill = "Frost", projectiles = { "It_Projectile_Frost_S0", "It_Projectile_Frost_S1", "It_Projectile_Frost_S2" } },
    Missile = { crystal = "It_Crystal_Magic", skill = "Missile", projectiles = { "It_Projectile_Missile_S0" } },
}
local RUNE_SPELLS = { "Beast", "Berserker", "BlueBarrier", "EnhancedHeal", "Illusion", "Inferno", "Joke", "Levitate", "Light", "MediumHeal", "MinorHeal",
    "Nautilus", "OpenLock", "Protection", "RedBarrier", "Skeleton", "Speed", "Telekinesis" }
local DAMAGE_TYPES = { "None", "Blunt", "Edge", "Fire", "Ice", "Magic", "Physics", "Point" }
local PROJECTILE_OF = {}   -- projectile template -> spell name
for name, p in pairs(PROJECTILE) do for _, t in ipairs(p.projectiles) do PROJECTILE_OF[t] = name end end

local function catalogue(name)
    if type(name) ~= "string" then return nil end
    local p = PROJECTILE[name]
    if p then
        local info = { name = name, kind = "projectile", skill = p.skill, items = { crystal = p.crystal }, projectiles = {} }
        for i, t in ipairs(p.projectiles) do info.projectiles[i] = t end
        return info
    end
    if has(RUNE_SPELLS, name) then
        local info = { name = name, kind = "rune", items = {}, projectiles = {} }
        for _, form in ipairs({ { "rune", "It_Ru_" }, { "scroll", "It_Scr_" }, { "orb", "It_Orb_" } }) do
            local t = form[2] .. name
            if tmpl.exists(t) then info.items[form[1]] = t end
        end
        return info
    end
    return nil
end

local function name_list()
    local out = {}
    for k in pairs(PROJECTILE) do out[#out + 1] = k end
    for _, k in ipairs(RUNE_SPELLS) do out[#out + 1] = k end
    table.sort(out)
    return out
end
local NAME_LIST = table.concat(name_list(), ", ")

-- risen.spell.names() -> { "BlueBarrier", "Fireball", ... }
function risen.spell.names() return name_list() end

-- risen.spell.info(name) -> { name, kind = "projectile"|"rune", skill, items = { crystal | rune, scroll, orb = template }, projectiles = { template, ... } } | nil, reason
function risen.spell.info(name)
    local info = catalogue(name)
    if not info then return nil, ("unknown spell '%s' (valid: %s)"):format(tostring(name), NAME_LIST) end
    return info
end

local function ptext(t, set, prop)
    local tp = tmpl.get(t)
    return tp and tp:get_property(set, prop)
end
local function pnum(t, set, prop)
    return tonumber(ptext(t, set, prop))
end
local function bool_of(v) return type(v) == "string" and v:lower() == "true" end

-- risen.spell.get(name) -> { name, kind, items = { form = { template, mana_cost, consume, requires, value } }, projectiles = { { template, damage, damage_type, speed, radius, effect } } }
-- The numbers the templates hold right now (shipped values, or what a `spells` section wrote).
function risen.spell.get(name)
    local info, why = spell.info(name)
    if not info then return nil, why end
    local out = { name = info.name, kind = info.kind, skill = info.skill, items = {}, projectiles = {} }
    for form, t in pairs(info.items) do
        out.items[form] = { template = t, mana_cost = pnum(t, "gCCastInfo_PS", "ManaCost"), consume = bool_of(ptext(t, "gCCastInfo_PS", "ConsumeItem")),
            requires = risen.item.requirements(t), value = pnum(t, "gCItem_PS", "GoldValue") }
    end
    for i, t in ipairs(info.projectiles) do
        local dt = ptext(t, "gCDamage_PS", "DamageType")
        out.projectiles[i] = { template = t, damage = pnum(t, "gCDamage_PS", "DamageAmount"), damage_type = dt and dt:gsub("^gEDamageType_", "") or nil,
            speed = pnum(t, "gCProjectile2_PS", "StartVelocity"), radius = pnum(t, "gCProjectile2_PS", "SweepTestRadius"), effect = ptext(t, "gCEffect_PS", "Effect") }
    end
    return out
end

-- ---- the values the templates had when this process first touched them ---------------------------------------------------------------
local shipped = {}
local function shipped_of(t, kind)
    local key = kind .. ":" .. t
    if shipped[key] == nil then
        if kind == "mana" then shipped[key] = pnum(t, "gCCastInfo_PS", "ManaCost") or false
        elseif kind == "damage" then shipped[key] = pnum(t, "gCDamage_PS", "DamageAmount") or false
        elseif kind == "speed" then shipped[key] = pnum(t, "gCProjectile2_PS", "StartVelocity") or false
        elseif kind == "radius" then shipped[key] = pnum(t, "gCProjectile2_PS", "SweepTestRadius") or false
        elseif kind == "value" then shipped[key] = pnum(t, "gCItem_PS", "GoldValue") or false end
    end
    return shipped[key] or nil
end

-- ---- validation of the settings shared by `spells` entries and new spells ------------------------------------------------------------
local PROJ_KEYS = { "damage", "damage_mult", "damage_type", "effect", "projectile_properties", "radius", "radius_mult", "speed", "speed_mult" }
local PROJ_KEY = {}
for _, k in ipairs(PROJ_KEYS) do PROJ_KEY[k] = true end
local SPELL_KEYS = { "consume", "mana_cost", "mana_mult", "orb_mana_cost", "properties", "requires", "scroll_mana_cost" }
for _, k in ipairs(PROJ_KEYS) do SPELL_KEYS[#SPELL_KEYS + 1] = k end
table.sort(SPELL_KEYS)
local SPELL_KEY = {}
for _, k in ipairs(SPELL_KEYS) do SPELL_KEY[k] = true end

local function damage_type_text(v)
    if type(v) ~= "string" then return nil end
    local short = v:gsub("^gEDamageType_", "")
    if has(DAMAGE_TYPES, short) then return "gEDamageType_" .. short end
    return nil
end

-- plain numbers / enums of the projectile settings -> nil | problem text; `levels` = number of projectile templates
local function check_projectile_settings(entry, levels, bad)
    local function num_key(key, pred, what)
        local v = entry[key]
        if v ~= nil and not pred(v) then bad(("%s must be %s, got %s"):format(key, what, tostring(v))) end
    end
    if type(entry.damage) == "table" then
        if #entry.damage ~= levels then bad(("damage: expected %d values (one per projectile level), got %d"):format(levels, #entry.damage)) end
        for i, v in ipairs(entry.damage) do if not is_int(v) then bad(("damage[%d] must be a non-negative integer, got %s"):format(i, tostring(v))) end end
    else
        num_key("damage", is_int, "a non-negative integer (or a list with one value per projectile level)")
    end
    num_key("damage_mult", is_num, "a number >= 0")
    num_key("speed", function(v) return is_num(v) and v > 0 end, "a number > 0")
    num_key("speed_mult", function(v) return is_num(v) and v > 0 end, "a number > 0")
    num_key("radius", is_num, "a number >= 0")
    num_key("radius_mult", is_num, "a number >= 0")
    if entry.damage_type ~= nil and not damage_type_text(entry.damage_type) then
        bad(("damage_type: unknown value '%s' (valid: %s)"):format(tostring(entry.damage_type), table.concat(DAMAGE_TYPES, ", ")))
    end
    if entry.effect ~= nil and (type(entry.effect) ~= "string" or entry.effect == "") then bad("effect must be an effect name (a string)") end
    for _, pair in ipairs({ { "damage", "damage_mult" }, { "speed", "speed_mult" }, { "radius", "radius_mult" } }) do
        if entry[pair[1]] ~= nil and entry[pair[2]] ~= nil then bad(("%s and %s exclude each other"):format(pair[1], pair[2])) end
    end
    if entry.projectile_properties ~= nil and type(entry.projectile_properties) ~= "table" then
        bad("projectile_properties must be a table { PropertySet = { Property = value } }")
    end
end

-- the property sets one projectile template gets from `entry` ({ set = { prop = value } }); `index` = the level (1..n) of the template; relative
-- settings are computed from the shipped values of `t`
local function projectile_sets(entry, t, index)
    local sets = {}
    local function put(set, prop, v) sets[set] = sets[set] or {}; sets[set][prop] = v end
    local dmg = entry.damage
    if type(dmg) == "table" then dmg = dmg[index] end
    if dmg ~= nil then put("gCDamage_PS", "DamageAmount", dmg)
    elseif entry.damage_mult ~= nil then
        local base = shipped_of(t, "damage")
        if base then put("gCDamage_PS", "DamageAmount", round(base * entry.damage_mult)) end
    end
    if entry.damage_type ~= nil then put("gCDamage_PS", "DamageType", damage_type_text(entry.damage_type)) end
    if entry.speed ~= nil then put("gCProjectile2_PS", "StartVelocity", entry.speed)
    elseif entry.speed_mult ~= nil then
        local base = shipped_of(t, "speed")
        if base then put("gCProjectile2_PS", "StartVelocity", base * entry.speed_mult) end
    end
    if entry.radius ~= nil then put("gCProjectile2_PS", "SweepTestRadius", entry.radius)
    elseif entry.radius_mult ~= nil then
        local base = shipped_of(t, "radius")
        if base then put("gCProjectile2_PS", "SweepTestRadius", base * entry.radius_mult) end
    end
    if entry.effect ~= nil then put("gCEffect_PS", "Effect", entry.effect) end
    for set, props in pairs(entry.projectile_properties or {}) do
        if type(props) == "table" then for p, v in pairs(props) do put(set, p, v) end end
    end
    return sets
end

-- ---- the `spells` section: modify the shipped spells -----------------------------------------------------------------------------------
-- spells = { Fireball = { mana_cost = 12, damage = 150, requires = { Fireball = 2 } }, Light = { mana_cost = 5, scroll_mana_cost = 2 } }
local function spells_section(data, _)
    local jobs, problems = {}, {}
    if type(data) ~= "table" then return jobs, { "spells: expected a table { SpellName = { mana_cost = ..., damage = ... } }" } end
    for _, name in ipairs(sorted_keys(data)) do
        local entry = data[name]
        local what = "spells." .. tostring(name)
        local function bad(msg) problems[#problems + 1] = what .. ": " .. msg end
        local info = catalogue(name)
        if type(entry) ~= "table" then bad("expected a table of settings")
        elseif not info then bad(("unknown spell (valid: %s)"):format(NAME_LIST))
        else
            local known = true
            for key in pairs(entry) do
                if not SPELL_KEY[key] then known = false; bad(("unknown setting '%s' (valid: %s)"):format(tostring(key), table.concat(SPELL_KEYS, ", "))) end
            end
            if known then
                local main = info.items.crystal or info.items.rune
                for _, key in ipairs({ "mana_cost", "scroll_mana_cost", "orb_mana_cost" }) do
                    if entry[key] ~= nil and not is_int(entry[key]) then bad(("%s must be a non-negative integer, got %s"):format(key, tostring(entry[key]))) end
                end
                if entry.mana_mult ~= nil and not is_num(entry.mana_mult) then bad("mana_mult must be a number >= 0") end
                if entry.mana_cost ~= nil and entry.mana_mult ~= nil then bad("mana_cost and mana_mult exclude each other") end
                if entry.consume ~= nil and type(entry.consume) ~= "boolean" then bad("consume must be true or false") end
                if info.kind == "projectile" then
                    for _, key in ipairs({ "scroll_mana_cost", "orb_mana_cost" }) do
                        if entry[key] ~= nil then bad(key .. " only applies to rune spells (projectile spells have a crystal only)") end
                    end
                    check_projectile_settings(entry, #info.projectiles, bad)
                else
                    for _, key in ipairs(PROJ_KEYS) do
                        if entry[key] ~= nil then bad(key .. " only applies to projectile spells (Fireball, Frost, Missile): the rune spells have no projectile") end
                    end
                end
                -- the item entries (validated by the items section) and the projectile property sets
                local item_entries, order = {}, {}
                local function item_entry(form) local t = info.items[form]
                    if not t then return nil end
                    item_entries[t] = item_entries[t] or {}
                    order[#order + 1] = t
                    return item_entries[t]
                end
                for _, form in ipairs({ "crystal", "rune" }) do
                    local e = item_entry(form)
                    if e then
                        if entry.mana_cost ~= nil then e.mana_cost = entry.mana_cost end
                        if entry.requires ~= nil then e.requires = entry.requires end
                        local props = {}
                        for set, p in pairs(entry.properties or {}) do props[set] = p end
                        if entry.consume ~= nil then
                            props.gCCastInfo_PS = props.gCCastInfo_PS or {}
                            props.gCCastInfo_PS.ConsumeItem = entry.consume
                        end
                        if next(props) then e.properties = props end
                    end
                end
                if entry.scroll_mana_cost ~= nil and info.items.scroll then item_entry("scroll").mana_cost = entry.scroll_mana_cost end
                if entry.orb_mana_cost ~= nil and info.items.orb then item_entry("orb").mana_cost = entry.orb_mana_cost end
                if entry.mana_mult ~= nil then
                    for _, form in ipairs({ "crystal", "rune", "scroll" }) do
                        local t = info.items[form]
                        local base = t and shipped_of(t, "mana")
                        if base then
                            local e = item_entries[t] or item_entry(form)
                            e.mana_cost = round(base * entry.mana_mult)
                        end
                    end
                end
                local sub = {}
                local seen = {}
                for _, t in ipairs(order) do
                    if not seen[t] and next(item_entries[t]) then
                        seen[t] = true
                        local j, p = balance.sections.items({ [t] = item_entries[t] }, {})
                        for _, m in ipairs(p or {}) do bad(m) end
                        for _, job in ipairs(j or {}) do sub[#sub + 1] = job end
                    end
                end
                if info.kind == "projectile" then
                    for i, t in ipairs(info.projectiles) do
                        local sets = projectile_sets(entry, t, i)
                        if next(sets) then
                            local ok, err = tmpl.patch(t, sets, { dry_run = true })
                            if not ok then bad(tostring(err))
                            else
                                sub[#sub + 1] = { label = t, run = function(opts)
                                    local ok2, err2, touched = tmpl.patch(t, projectile_sets(entry, t, i), { instances = opts.instances })
                                    if not ok2 then return nil, err2 end
                                    return true, nil, touched or 0
                                end }
                            end
                        end
                    end
                end
                if #sub == 0 and #problems == 0 then bad("nothing to do (settings: " .. table.concat(SPELL_KEYS, ", ") .. ")")
                else
                    local keys = {}
                    for _, k in ipairs(sorted_keys(entry)) do keys[#keys + 1] = tostring(k) end
                    jobs[#jobs + 1] = {
                        label = ("spell %s (%s)"):format(name, table.concat(keys, ", ")),
                        run = function(opts)
                            local touched = 0
                            for _, job in ipairs(sub) do
                                local ok, err, n = job.run(opts)
                                if not ok then return nil, ("%s: %s"):format(job.label, tostring(err)) end
                                touched = touched + (n or 0)
                            end
                            return true, nil, touched
                        end,
                    }
                end
            end
        end
    end
    return jobs, problems
end
balance.register_section("spells", spells_section)

-- risen.spell.modify(name, settings [, { instances = true }]) -> report | nil, reason, problems: one spell of the `spells` section
function risen.spell.modify(name, settings, opts)
    return balance.apply({ spells = { [name] = settings } }, opts)
end

-- ---- new spells -----------------------------------------------------------------------------------------------------------------------
-- new_spells = { Spass_Fireball_Strong = { base = "Fireball", name = "Strong Fireball", mana_cost = 16, damage_mult = 2, forms = { "crystal", "scroll" } } }
-- Every form is a new item template cloned from the base spell's item (risen.template.create); a projectile spell's per-shot numbers are applied to
-- the projectile at the moment it is shot (damage, speed, ... -- the engine picks the projectile template per spell type, not per crystal).
local FORMS = { projectile = { "crystal", "scroll" }, rune = { "rune", "scroll", "orb" } }
local FORM_KEYS = { "crystal", "orb", "rune", "scroll" }
local ITEM_KEYS = { "consume", "description", "icon", "mana_cost", "name", "properties", "requires", "value" }
local NEW_KEYS = { "base", "forms", "mana_mult" }
for _, k in ipairs(ITEM_KEYS) do NEW_KEYS[#NEW_KEYS + 1] = k end
for _, k in ipairs(FORM_KEYS) do NEW_KEYS[#NEW_KEYS + 1] = k end
for _, k in ipairs(PROJ_KEYS) do NEW_KEYS[#NEW_KEYS + 1] = k end
table.sort(NEW_KEYS)
local NEW_KEY = {}
for _, k in ipairs(NEW_KEYS) do NEW_KEY[k] = true end
local ITEM_KEY = {}
for _, k in ipairs(ITEM_KEYS) do ITEM_KEY[k] = true end
local SUFFIX = { crystal = "", rune = "", scroll = "_Scroll", orb = "_Orb" }
local LABEL_SUFFIX = { scroll = " (Scroll)", orb = " (Orb)" }

local function with_suffix(text, suffix)
    if suffix == nil or suffix == "" or text == nil then return text end
    if type(text) == "string" then return text .. suffix end
    local out = {}
    for lang, t in pairs(text) do out[lang] = t .. suffix end
    return out
end

-- shot overrides of new projectile spells: item template -> { projectiles = { template -> true }, settings }
local defs = {}
function risen.spell.overrides() local out = {} for k, v in pairs(defs) do out[k] = v end return out end
spell.has_overrides = function(template) return defs[template] ~= nil end

local function new_spells_section(data, opts)
    local jobs, problems = {}, {}
    if type(data) ~= "table" then return jobs, { "new_spells: expected a table { NewSpellName = { base = \"Fireball\", ... } }" } end
    for _, name in ipairs(sorted_keys(data)) do
        local entry = data[name]
        local what = "new_spells." .. tostring(name)
        local function bad(msg) problems[#problems + 1] = what .. ": " .. msg end
        if type(name) ~= "string" or type(entry) ~= "table" then bad("expected a table { base = \"SpellName\", ... }")
        else
            local info = catalogue(entry.base)
            if entry.base == nil then bad("'base' (the spell to build on: " .. NAME_LIST .. ") is required")
            elseif not info then bad(("base: unknown spell '%s' (valid: %s)"):format(tostring(entry.base), NAME_LIST)) end
            local known = true
            for key in pairs(entry) do
                if not NEW_KEY[key] then known = false; bad(("unknown setting '%s' (valid: %s)"):format(tostring(key), table.concat(NEW_KEYS, ", "))) end
            end
            if info and known then
                local allowed = FORMS[info.kind]
                -- which forms: `forms` list, plus every form that has its own table
                local want, seen = {}, {}
                local function add(form)
                    if not seen[form] then seen[form] = true; want[#want + 1] = form end
                end
                if entry.forms ~= nil then
                    if type(entry.forms) ~= "table" then bad("forms must be a list such as { \"crystal\", \"scroll\" }")
                    else for _, f in ipairs(entry.forms) do add(f) end end
                end
                for _, f in ipairs(FORM_KEYS) do if entry[f] ~= nil then add(f) end end
                if #want == 0 then add(allowed[1]) end
                local main_form = info.kind == "projectile" and "crystal" or "rune"
                for _, f in ipairs(want) do
                    if not has(allowed, f) then
                        bad(("form '%s' does not exist for %s spells (valid: %s)"):format(tostring(f), info.kind, table.concat(allowed, ", ")))
                    end
                end
                local first = want[1]
                local before = #problems
                if info.kind == "projectile" then check_projectile_settings(entry, #info.projectiles, bad)
                else
                    for _, key in ipairs(PROJ_KEYS) do
                        if entry[key] ~= nil then bad(key .. " only applies to projectile spells (Fireball, Frost, Missile)") end
                    end
                end
                if entry.mana_cost ~= nil and entry.mana_mult ~= nil then bad("mana_cost and mana_mult exclude each other") end
                if entry.mana_mult ~= nil and not is_num(entry.mana_mult) then bad("mana_mult must be a number >= 0") end
                -- the cost of the main item: what a scroll of a projectile spell derives its own cost from
                local main_base = info.items[main_form]
                local main_cost = entry.mana_cost
                if main_cost == nil and type(entry[main_form]) == "table" then main_cost = entry[main_form].mana_cost end
                if main_cost == nil and entry.mana_mult ~= nil and main_base and shipped_of(main_base, "mana") then main_cost = round(shipped_of(main_base, "mana") * entry.mana_mult) end
                if main_cost == nil and main_base then main_cost = shipped_of(main_base, "mana") end
                local new_items, labels, forms_of = {}, {}, {}
                for _, form in ipairs(want) do
                    if has(allowed, form) then
                        local tname = name .. SUFFIX[form]
                        local base_t = info.items[form]
                        if info.kind == "projectile" and form == "scroll" then base_t = info.items.crystal end
                        if not base_t then bad(("the base spell has no %s item"):format(form))
                        else
                            local over = {}
                            local own = entry[form]
                            if own ~= nil and type(own) ~= "table" then bad(form .. " must be a table of item settings"); own = nil end
                            for key in pairs(own or {}) do
                                if not ITEM_KEY[key] then bad(("%s: unknown setting '%s' (valid: %s)"):format(form, tostring(key), table.concat(ITEM_KEYS, ", "))) end
                            end
                            -- generic item settings go to the main form, or to the first wanted form when the main one is not wanted
                            local generic = (form == main_form) or (not seen[main_form] and form == first)
                            if generic then for _, key in ipairs(ITEM_KEYS) do if entry[key] ~= nil then over[key] = entry[key] end end end
                            for k, v in pairs(own or {}) do over[k] = v end
                            local function shipped_mana() return shipped_of(base_t, "mana") end
                            if over.mana_cost == nil and entry.mana_mult ~= nil and shipped_mana() then over.mana_cost = round(shipped_mana() * entry.mana_mult) end
                            if form == "scroll" and info.kind == "projectile" then
                                -- a scroll of a projectile spell is the crystal used up by the cast: no requirements, half the cost, cheap
                                if over.consume == nil then over.consume = true end
                                if over.requires == nil then over.requires = {} end
                                if over.mana_cost == nil and main_cost then over.mana_cost = math.ceil(main_cost / 2) end
                                if over.value == nil and shipped_of(base_t, "value") then over.value = math.max(1, round(shipped_of(base_t, "value") / 5)) end
                                if over.name == nil and entry.name ~= nil then over.name = with_suffix(entry.name, LABEL_SUFFIX.scroll) end
                            elseif form == "scroll" or form == "orb" then
                                if over.name == nil and entry.name ~= nil and form ~= main_form then over.name = with_suffix(entry.name, LABEL_SUFFIX[form]) end
                            end
                            local consume = over.consume
                            over.consume = nil
                            if consume ~= nil then
                                if type(consume) ~= "boolean" then bad(form .. ": consume must be true or false")
                                else
                                    over.properties = over.properties or {}
                                    over.properties.gCCastInfo_PS = over.properties.gCCastInfo_PS or {}
                                    over.properties.gCCastInfo_PS.ConsumeItem = consume
                                end
                            end
                            over.base = base_t
                            new_items[tname] = over
                            forms_of[tname] = form
                            labels[#labels + 1] = tname
                        end
                    end
                end
                do
                    local j, p = balance.sections.new_items(new_items, opts)
                    for _, m in ipairs(p or {}) do problems[#problems + 1] = m end
                    local sub = {}
                    for _, job in ipairs(j or {}) do sub[#sub + 1] = job end
                    -- the per-shot overrides of the new items
                    local shot = nil
                    if info.kind == "projectile" then
                        for _, key in ipairs(PROJ_KEYS) do if entry[key] ~= nil then shot = shot or {}; shot[key] = entry[key] end end
                    end
                    if #problems == before then
                        table.sort(labels)
                        jobs[#jobs + 1] = {
                            label = ("new spell %s from %s (%s)"):format(name, entry.base, table.concat(labels, ", ")),
                            run = function(o)
                                for _, job in ipairs(sub) do
                                    local ok, err = job.run(o)
                                    if not ok then return nil, ("%s: %s"):format(job.label, tostring(err)) end
                                end
                                if info.kind == "projectile" then
                                    for _, t in ipairs(labels) do
                                        local ps = {}
                                        for _, pt in ipairs(info.projectiles) do ps[pt] = true end
                                        defs[t] = { spell = name, base = entry.base, projectiles = ps, settings = shot or {}, scroll = forms_of[t] == "scroll" }
                                    end
                                end
                                return true, nil, 0
                            end,
                        }
                    end
                end
            end
        end
    end
    return jobs, problems
end
balance.register_section("new_spells", new_spells_section)

-- risen.spell.create(name, spec [, { instances = true }]) -> report | nil, reason, problems: one new spell (see the `new_spells` section)
function risen.spell.create(name, spec, opts)
    return balance.apply({ new_spells = { [name] = spec } }, opts)
end

-- ---- per-shot overrides ----------------------------------------------------------------------------------------------------------------
-- Called for every spawned entity (entity.spawned): a projectile of a spell type whose shooter has a registered new spell equipped gets that spell's
-- numbers written into the projectile entity (its own copy of the template values: the template and other shots stay untouched).
-- Returns item, shooter, def when the shot belongs to a registered new spell (nil otherwise).
spell.on_projectile = function(e, template)
    if not PROJECTILE_OF[template] or next(defs) == nil then return nil end
    local shooter = spell.shooter(e)
    if not shooter then return nil end
    local item = spell.equipped(shooter, spell.SLOT_MAGIC)
    local d = item and defs[item]
    if not d or not d.projectiles[template] then return nil end
    local level = 1
    local info = catalogue(d.base)
    for i, t in ipairs(info.projectiles) do if t == template then level = i end end
    local sets = projectile_sets(d.settings, template, level)
    -- relative settings of a shot are relative to what this projectile carries now (template values, including a `spells` edit)
    if d.settings.damage_mult ~= nil and d.settings.damage == nil then
        local cur = tonumber(e:get_property("gCDamage_PS", "DamageAmount"))
        if cur then sets.gCDamage_PS = sets.gCDamage_PS or {}; sets.gCDamage_PS.DamageAmount = round(cur * d.settings.damage_mult) end
    end
    if d.settings.speed_mult ~= nil and d.settings.speed == nil then
        local cur = tonumber(e:get_property("gCProjectile2_PS", "StartVelocity"))
        if cur then sets.gCProjectile2_PS = sets.gCProjectile2_PS or {}; sets.gCProjectile2_PS.StartVelocity = cur * d.settings.speed_mult end
    end
    if d.settings.radius_mult ~= nil and d.settings.radius == nil then
        local cur = tonumber(e:get_property("gCProjectile2_PS", "SweepTestRadius"))
        if cur then sets.gCProjectile2_PS = sets.gCProjectile2_PS or {}; sets.gCProjectile2_PS.SweepTestRadius = cur * d.settings.radius_mult end
    end
    local n = 0
    for set, props in pairs(sets) do
        for prop, v in pairs(props) do
            local text = type(v) == "number" and (v == math.floor(v) and string.format("%d", v) or string.format("%.9g", v)) or tostring(v)
            if e:set_property(set, prop, text) then n = n + 1 end
        end
    end
    tr("spell", "shot", template, "by", item, n .. " value(s)")
    return item, shooter, d
end
)LUA";

// The per-mod part: one `entity.spawned` handler per mod that uses per-shot overrides (risen.spell.create / the new_spells section through
// risen.balance.install enable it).
inline const char* kSpellModChunk = R"LUA(
local api = ...
local shared = api.spell
local s = setmetatable({}, { __index = shared })
api.spell = s
local enabled = false
local decremented = {}   -- "shooter|item" -> true: the engine took one of the stack during the shot that is being handled
local exhausted = {}     -- "shooter|item" -> { shooter, item }: the last scroll of a projectile spell, which the engine does not remove

-- risen.spell.enable_overrides(): registers this mod's handlers: entity.spawned applies the per-shot numbers of new projectile spells, and the last scroll
-- of a projectile spell is taken away after its shot (the engine's PSCastInfo::Consume unequips from the rune slot only, so a scroll cast from the magic
-- slot would keep its last item and be usable forever: once the shot is over the caster puts the weapon away and the scroll is removed).
-- Done by risen.spell.create and by risen.balance.install for a spec with new_spells.
function s.enable_overrides()
    if enabled then return false end
    enabled = true
    api.on("inventory.changed", function(owner, template, delta)
        if delta < 0 and owner and shared.has_overrides(template) then
            decremented[owner:name() .. "|" .. template] = true
            local t = api.trace if t and t.enabled("spell") then t("spell", "amount of", template, "changed by", delta) end
        end
    end)
    api.on("entity.spawned", function(e, template)
        local ok, item, shooter, d = pcall(shared.on_projectile, e, template)
        if not ok then api.log.error("spell: " .. tostring(item)) return end
        if item and d.scroll and shooter then
            local key = shooter:name() .. "|" .. item
            local t = api.trace if t and t.enabled("spell") then t("spell", "scroll shot", item, "decremented:", tostring(decremented[key]), "left:", api.item.count(shooter, item)) end
            if decremented[key] then decremented[key] = nil
            elseif api.item.count(shooter, item) >= 1 then exhausted[key] = { shooter = shooter, item = item } end
        end
    end)
    api.on("tick", function()
        if next(exhausted) == nil then return end
        for key, x in pairs(exhausted) do
            local who = x.shooter
            local st = who:valid() and tostring(who:state()) or nil
            if st == nil then exhausted[key] = nil
            elseif x.wait then
                x.wait = x.wait - 1   -- the item has to leave the hand and the slot first (the state machine does that on its next steps)
                if x.wait <= 0 then
                    exhausted[key] = nil
                    local n = shared.discard(who, x.item)
                    local t = api.trace if t and t.enabled("spell") then t("spell", "last scroll", x.item, "taken:", n, "state", tostring(who:state()), "equipped", tostring(shared.equipped(who))) end
                end
            elseif st == "PS_MagicBall_Loop" or not st:find("^PS_Magic") then
                local un = api.item.unequip(who, x.item)
                if who:is_player() then who:set_state("PS_Normal") end
                x.wait = 30
                local t = api.trace if t and t.enabled("spell") then t("spell", "last scroll", x.item, "unequip:", tostring(un), "state was", st) end
            end
        end
    end)
    return true
end

function s.create(name, spec, opts)
    local report, why, problems = shared.create(name, spec, opts)
    if report then s.enable_overrides() end
    return report, why, problems
end
)LUA";

}  // namespace spass::game
