#pragma once

// The Lua part of crafting recipes (docs/RE.md section 30, docs/LUA_API.md "Recipes"): the catalogue (risen.recipe.names/info/get), the knowledge model
// (known/learn/forget), the engine's craft check and craft (can_craft/craft), risen.recipe.create/modify and the `new_recipes` / `recipes` sections of
// risen.balance. Pure Lua on top of the natives of lua_recipe.cpp (ingredients_raw, set_ingredients_raw, can_craft_raw, craft_raw), the
// "recipe_required" skill array of risen.item.skill_array, risen.template.create/patch and risen.item.give/take/count, so the unit tests run it against mocks.
namespace spass::game {

inline const char* kRecipeChunk = R"LUA(
local risen = ...
local recipe = risen.recipe
local balance = risen.balance
local tmpl = risen.template
local item = risen.item

local function sorted_keys(t)
    local keys = {}
    for k in pairs(t) do keys[#keys + 1] = k end
    table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
    return keys
end
local function is_int(v, lo, hi) return type(v) == "number" and v == math.floor(v) and v >= lo and v <= hi end
local function tr(...) local t = risen.trace if t and t.enabled("balance") then t("balance", ...) end end

-- ---- stations ------------------------------------------------------------------------------------------------------------------------
-- gERecipeCategory (RE.md section 30): the station that lists the recipe. friendly name -> engine enum name
local STATIONS = {
    anvil = "gERecipeCategory_Smith_Anvil", forge = "gERecipeCategory_Smith_Forge", grindstone = "gERecipeCategory_Smith_GrindStone",
    coolweapon = "gERecipeCategory_Smith_CoolWeapon", alchemy = "gERecipeCategory_Alchemy", goldsmith = "gERecipeCategory_Goldsmith",
    scroll = "gERecipeCategory_WriteScroll", cooking = "gERecipeCategory_Cooking", frying = "gERecipeCategory_Frying",
}
local STATION_OF = {}
for friendly, engine in pairs(STATIONS) do STATION_OF[engine] = friendly end
local STATION_NAMES = sorted_keys(STATIONS)
local STATION_TEXT = table.concat(STATION_NAMES, ", ")

-- "anvil" | "Anvil" | "Smith_Anvil" | "gERecipeCategory_Smith_Anvil" -> engine enum name | nil
local function engine_station(v)
    if type(v) ~= "string" then return nil end
    if STATION_OF[v] then return v end
    local lower = v:lower()
    if STATIONS[lower] then return STATIONS[lower] end
    local bare = v:gsub("^gERecipeCategory_", ""):gsub("^Smith_", ""):lower()
    return STATIONS[bare]
end

-- risen.recipe.stations() -> { "alchemy", "anvil", ... }: the valid values of `station`
function risen.recipe.stations() local out = {} for i, n in ipairs(STATION_NAMES) do out[i] = n end return out end

-- ---- the catalogue ---------------------------------------------------------------------------------------------------------------------
local function has_set(t, set)
    for _, s in ipairs(t:property_sets() or {}) do if s == set then return true end end
    return false
end

local shipped_names   -- every shipped template with a gCRecipe_PS (computed once: the shipped set never changes)
local made = {}       -- recipes this layer created: name -> true
local planned_recipes = {}   -- the new_recipes of the spec being validated: a recipe may name another one of them (the smithing stages consume and make each other), itself included

local function all_names()
    if not shipped_names then
        shipped_names = {}
        for _, n in ipairs(tmpl.list("Items") or {}) do
            local t = tmpl.get(n)
            if t and has_set(t, "gCRecipe_PS") then shipped_names[#shipped_names + 1] = n end
        end
    end
    local out, seen = {}, {}
    for _, n in ipairs(shipped_names) do out[#out + 1] = n; seen[n] = true end
    for n in pairs(made) do
        if not seen[n] and tmpl.exists(n) then out[#out + 1] = n end
    end
    table.sort(out)
    return out
end

local function skill_map(list)
    return balance.skill_map(list or {})
end

-- risen.recipe.info(name) -> { name, label, station, category, ingredients = { { item, count } }, result = { item, count }, skills = { Smith = 1 }, shipped } | nil, reason
function risen.recipe.info(name)
    if type(name) ~= "string" then return nil, "expected a recipe template name" end
    local t, why = tmpl.get(name)
    if not t then return nil, ("unknown template '%s' (%s)"):format(name, tostring(why)) end
    if not has_set(t, "gCRecipe_PS") then return nil, ("'%s' is not a recipe (no gCRecipe_PS)"):format(name) end
    local ing, iwhy = recipe.ingredients_raw(name)
    if not ing then return nil, iwhy end
    local req = item.skill_array(name, "recipe_required") or {}
    local category = t:get_property("gCRecipe_PS", "Craft")
    return {
        name = name,
        label = t:focus_name(),
        category = category,
        station = STATION_OF[category] or category,
        ingredients = ing,
        result = { item = t:get_property("gCRecipe_PS", "ResultItem"), count = tonumber(t:get_property("gCRecipe_PS", "ResultAmount")) or 1 },
        skills = skill_map(req),
        shipped = not made[name],
    }
end
function risen.recipe.get(name) return recipe.info(name) end

-- risen.recipe.names([station]) -> sorted template names of all recipes (shipped and created), optionally of one station
function risen.recipe.names(station)
    local engine
    if station ~= nil then
        engine = engine_station(station)
        if not engine then return nil, ("unknown station '%s' (valid: %s)"):format(tostring(station), STATION_TEXT) end
    end
    local out = {}
    for _, n in ipairs(all_names()) do
        if not engine then out[#out + 1] = n
        else
            local t = tmpl.get(n)
            if t and t:get_property("gCRecipe_PS", "Craft") == engine then out[#out + 1] = n end
        end
    end
    return out
end

-- ---- knowing a recipe --------------------------------------------------------------------------------------------------------------------
-- The engine has no list of learned recipes: a character knows a recipe while a stack of the recipe item is in its inventory (the stations list
-- the recipe items of their category from the inventory).
function risen.recipe.known(who, name)
    if who == nil or type(name) ~= "string" then return nil, "expected (character, recipe name)" end
    local i, why = recipe.info(name)
    if not i then return nil, why end
    return item.count(who, name) > 0
end

-- risen.recipe.learn(who, name) -> true (given) | false (already known) | nil, reason
function risen.recipe.learn(who, name)
    local known, why = recipe.known(who, name)
    if known == nil then return nil, why end
    if known then return false end
    local stack = item.give(who, name, 1)
    if type(stack) ~= "number" or stack < 0 then return nil, "could not give the recipe item (the engine refused)" end
    return true
end

-- risen.recipe.forget(who, name) -> true | false (was not known) | nil, reason: takes the recipe item away
function risen.recipe.forget(who, name)
    local known, why = recipe.known(who, name)
    if known == nil then return nil, why end
    if not known then return false end
    local n = item.count(who, name)
    local removed = item.take(who, name, n)
    if (removed or 0) < n then return nil, ("could only take %d of %d recipe items (equipped?)"):format(removed or 0, n) end
    return true
end

local function learned_var(name) return "SPASS_RECIPE_" .. name end

-- risen.recipe.learn_once(who, name) -> true | false (already given once in this savegame) | nil, reason
-- Gives the recipe item the first time and remembers that in a game variable (saved with the savegame), so a player who sold or dropped it does not get it again.
function risen.recipe.learn_once(who, name)
    local var = learned_var(name)
    if risen.gamevar.get(var) == 1 then return false end
    local given, why = recipe.learn(who, name)
    if given == nil then return nil, why end
    risen.gamevar.set(var, 1)
    return true
end

-- ---- crafting ----------------------------------------------------------------------------------------------------------------------------
local function missing_text(who, i, count)
    local parts = {}
    for skill, amount in pairs(i.skills) do
        local total = item.skill_total(who, skill)
        if (total or 0) < amount then parts[#parts + 1] = ("skill %s %d (has %d)"):format(skill, amount, total or 0) end
    end
    table.sort(parts)
    local lacking = {}
    for _, e in ipairs(i.ingredients) do
        local have = item.count(who, e.item)
        if have < e.count * count then lacking[#lacking + 1] = ("%s x%d (has %d)"):format(e.item, e.count * count, have) end
    end
    if #lacking > 0 then parts[#parts + 1] = "ingredients " .. table.concat(lacking, ", ") end
    return table.concat(parts, "; ")
end

-- risen.recipe.can_craft(who, name) -> n | nil, reason: the engine's own check (PSInventory::CanSynthesize): how many times `who` can craft the recipe with
-- the ingredients in the inventory and the required skills. 0 comes with a second value saying what is missing. Stations are not involved.
function risen.recipe.can_craft(who, name)
    local i, why = recipe.info(name)
    if not i then return nil, why end
    local n, nwhy = recipe.can_craft_raw(who, name)
    if not n then return nil, nwhy end
    if n == 0 then
        local text = missing_text(who, i, 1)
        return 0, text ~= "" and text or "the engine says no (recipe without ingredients?)"
    end
    return n
end

-- risen.recipe.craft(who, name [, count [, opts]]) -> true, result_item | nil, reason
-- What a station does once the player chose a recipe and an amount: the engine checks (CanSynthesize) and then crafts (PSInventory::Synthesize): the
-- ingredients are deleted, `count` result items created, a `crafting` event fires. Nothing is touched when the check fails. The character has to know the
-- recipe (carry the recipe item) like at a station unless opts.unknown = true. Only the player is limited by the stations' animations: this call has none.
function risen.recipe.craft(who, name, count, opts)
    count = count == nil and 1 or count
    opts = opts or {}
    if not is_int(count, 1, 99) then return nil, "count must be an integer from 1 to 99" end
    local i, why = recipe.info(name)
    if not i then return nil, why end
    if not opts.unknown then
        local known = recipe.known(who, name)
        if not known then return nil, ("'%s' is not known (the recipe item is not in the inventory; risen.recipe.learn gives it)"):format(name) end
    end
    local n, nwhy = recipe.can_craft_raw(who, name)
    if not n then return nil, nwhy end
    if n < count then
        local text = missing_text(who, i, count)
        return nil, ("cannot craft %d time(s) (the engine allows %d)%s"):format(count, n, text ~= "" and (": missing " .. text) or "")
    end
    local stack, cwhy = recipe.craft_raw(who, name, count)
    if not stack then return nil, cwhy end
    tr("recipe", "crafted", name, "x" .. count, "->", i.result.item)
    return true, i.result.item
end

-- ---- validating specs ----------------------------------------------------------------------------------------------------------------------
local function item_exists(name)
    if tmpl.exists(name) or planned_recipes[name] then return true end
    return tmpl.is_planned ~= nil and tmpl.is_planned(name) == true
end

-- { { item = "It_IronOre", count = 2 }, "It_1H_Sword", { "It_Plant_Berry", 3 } } | { It_IronOre = 2 } -> list | nil (problems reported through bad)
local function parse_ingredients(spec, what, bad)
    if type(spec) ~= "table" then bad(what .. ": expected a list { { item = \"It_IronOre\", count = 2 }, ... } or a table { It_IronOre = 2 }") return nil end
    local list = {}
    local function add(name, count, label)
        if type(name) ~= "string" or name == "" then bad(label .. ": the item must be a template name") return end
        if not is_int(count, 1, 999) then bad(("%s: the count of '%s' must be an integer from 1 to 999, got %s"):format(label, name, tostring(count))) return end
        if not item_exists(name) then bad(("%s: unknown item '%s' (names: docs/TEMPLATES.md, risen.template.list('Items'))"):format(label, name)) return end
        for _, e in ipairs(list) do
            if e.item == name then bad(("%s: '%s' is listed twice"):format(label, name)) return end
        end
        list[#list + 1] = { item = name, count = count }
    end
    if spec[1] ~= nil then
        for n, e in ipairs(spec) do
            local label = ("%s[%d]"):format(what, n)
            if type(e) == "string" then add(e, 1, label)
            elseif type(e) == "table" then add(e.item or e[1], e.count or e[2] or 1, label)
            else bad(label .. ": expected { item =, count = }") end
        end
    else
        for _, k in ipairs(sorted_keys(spec)) do add(k, spec[k], what .. "." .. tostring(k)) end
    end
    if #list == 0 and #sorted_keys(spec) == 0 then bad(what .. ": a recipe needs at least one ingredient") end
    if #list > 16 then bad(what .. ": at most 16 ingredients") end
    return list
end

local function parse_result(v, what, bad)
    local name, count = v, 1
    if type(v) == "table" then name, count = v.item or v[1], v.count or v[2] or 1 end
    if type(name) ~= "string" or name == "" then bad(what .. ": expected an item name or { item = \"...\", count = 1 }") return nil end
    if count ~= 1 then
        bad(what .. ": count must be 1 -- the engine's craft function creates exactly one result item per craft and never reads ResultAmount (RE.md section 30)")
        return nil
    end
    if not item_exists(name) then bad(("%s: unknown item '%s'"):format(what, name)) return nil end
    return { item = name, count = 1 }
end

local function parse_skills(spec, what, bad)
    local entries, problems = balance.skill_entries(spec, what)
    if not entries then for _, m in ipairs(problems) do bad(m) end return nil end
    return entries
end

local RECIPE_PROPS = { Craft = true, CraftIngredients = true, RequiredSkills = true, ResultItem = true, ResultAmount = true }

local function for_instances(name, fn)
    local t = tmpl.get(name)
    local n = 0
    for _, e in ipairs(t and t:instances(1000000) or {}) do
        if fn(e) then n = n + 1 end
    end
    return n
end

-- writes the validated parts into template `name` (and with opts.instances the live entities); parts = { station, ingredients, skills, result, properties }
local function write_parts(name, parts, opts)
    local touched = 0
    local sets = {}
    if parts.station then sets.gCRecipe_PS = { Craft = parts.station } end
    if parts.result then
        sets.gCRecipe_PS = sets.gCRecipe_PS or {}
        sets.gCRecipe_PS.ResultItem = parts.result.item
        sets.gCRecipe_PS.ResultAmount = "1"
    end
    for set, props in pairs(parts.properties or {}) do
        sets[set] = sets[set] or {}
        for p, v in pairs(props) do sets[set][p] = v end
    end
    if next(sets) then
        local ok, err, n = tmpl.patch(name, sets, { instances = opts.instances })
        if not ok then return nil, tostring(err) end
        touched = touched + (n or 0)
    end
    if parts.ingredients then
        local ok, err = recipe.set_ingredients_raw(name, parts.ingredients)
        if not ok then return nil, tostring(err) end
        if opts.instances then
            touched = touched + for_instances(name, function(e) return recipe.set_ingredients_raw(e, parts.ingredients) end)
        end
    end
    if parts.skills then
        local ok, err = item.set_skill_array(name, "recipe_required", parts.skills)
        if not ok then return nil, tostring(err) end
        if opts.instances then
            touched = touched + for_instances(name, function(e) return item.set_skill_array(e, "recipe_required", parts.skills) end)
        end
    end
    return true, nil, touched
end

local function describe_item(name)
    local t = tmpl.get(name)
    local label = t and t:focus_name()
    if label and label ~= "" then return label end
    return name
end

-- ---- the `recipes` section: edit shipped (or created) recipes --------------------------------------------------------------------------------
-- recipes = { It_1H_Steel_Hot = { ingredients = { It_1H_Steel_Hot = 1, It_IronOre = 2 }, skills = { Smith = 2 }, result = "It_1H_Steel_Shaped", station = "anvil" },
--             It_Recipe_Health_01 = { counts = { It_Plant_Herb = 5 } } }
local EDIT_KEYS = { "counts", "ingredients", "properties", "result", "skills", "station" }
local EDIT_KEY = {}
for _, k in ipairs(EDIT_KEYS) do EDIT_KEY[k] = true end

local function check_properties(props, what, bad)
    if props == nil then return nil end
    if type(props) ~= "table" then bad(what .. ": must be a table { PropertySet = { Property = value } }") return nil end
    for set, p in pairs(props) do
        if type(p) ~= "table" then bad(("%s.%s must be a table"):format(what, tostring(set)))
        elseif set == "gCRecipe_PS" then
            for prop in pairs(p) do
                if RECIPE_PROPS[prop] then bad(("%s.gCRecipe_PS.%s: use station / ingredients / skills / result instead"):format(what, tostring(prop))) end
            end
        end
    end
    return props
end

local function recipes_section(data, _)
    local jobs, problems = {}, {}
    if type(data) ~= "table" then return jobs, { "recipes: expected a table { RecipeTemplate = { ingredients = ..., skills = ... } }" } end
    for _, name in ipairs(sorted_keys(data)) do
        local entry = data[name]
        local what = "recipes." .. tostring(name)
        local function bad(msg) problems[#problems + 1] = what .. ": " .. msg end
        if type(name) ~= "string" or type(entry) ~= "table" then bad("expected a table of settings")
        else
            local known = true
            for key in pairs(entry) do
                if not EDIT_KEY[key] then known = false; bad(("unknown setting '%s' (valid: %s)"):format(tostring(key), table.concat(EDIT_KEYS, ", "))) end
            end
            local current, why = recipe.ingredients_raw(name)
            if not current then bad((why or "not a recipe") .. " (recipes: risen.recipe.names())")
            elseif known then
                local parts = {}
                local before = #problems
                if entry.station ~= nil then
                    parts.station = engine_station(entry.station)
                    if not parts.station then bad(("station: unknown '%s' (valid: %s)"):format(tostring(entry.station), STATION_TEXT)) end
                end
                if entry.ingredients ~= nil and entry.counts ~= nil then bad("ingredients and counts exclude each other") end
                if entry.ingredients ~= nil then parts.ingredients = parse_ingredients(entry.ingredients, "ingredients", bad) end
                if entry.counts ~= nil then
                    if type(entry.counts) ~= "table" then bad("counts: expected a table { It_IronOre = 3 }")
                    else
                        local list = {}
                        for _, e in ipairs(current) do list[#list + 1] = { item = e.item, count = e.count } end
                        for _, k in ipairs(sorted_keys(entry.counts)) do
                            local c = entry.counts[k]
                            local found = false
                            for _, e in ipairs(list) do if e.item == k then found = true; if is_int(c, 1, 999) then e.count = c else bad(("counts.%s must be an integer from 1 to 999, got %s"):format(tostring(k), tostring(c))) end end end
                            if not found then bad(("counts.%s: the recipe has no such ingredient (it has: %s); use `ingredients` to add one"):format(tostring(k), (function() local t = {} for _, e in ipairs(current) do t[#t + 1] = e.item end return table.concat(t, ", ") end)())) end
                        end
                        parts.ingredients = list
                    end
                end
                if entry.skills ~= nil then parts.skills = parse_skills(entry.skills, "skills", bad) end
                if entry.result ~= nil then parts.result = parse_result(entry.result, "result", bad) end
                parts.properties = check_properties(entry.properties, "properties", bad)
                if #problems == before then
                    if next(parts) == nil then bad("nothing to do (settings: " .. table.concat(EDIT_KEYS, ", ") .. ")")
                    else
                        local keys = {}
                        for _, k in ipairs(sorted_keys(entry)) do keys[#keys + 1] = tostring(k) end
                        jobs[#jobs + 1] = {
                            label = ("recipe %s (%s)"):format(name, table.concat(keys, ", ")),
                            run = function(opts) return write_parts(name, parts, opts) end,
                        }
                    end
                end
            end
        end
    end
    return jobs, problems
end
balance.register_section("recipes", recipes_section)

-- risen.recipe.modify(name, settings [, { instances = true }]) -> report | nil, reason, problems: one entry of the `recipes` section
function risen.recipe.modify(name, settings, opts)
    return balance.apply({ recipes = { [name] = settings } }, opts)
end

-- ---- new recipes --------------------------------------------------------------------------------------------------------------------------
-- new_recipes = { Spass_Recipe_X = { station = "anvil", ingredients = { It_1H_Sword = 1, It_IronOre = 2 }, result = "Spass_Sword_X", skills = { Smith = 1 },
--                                   name = "Smithing plan: X", learn = true } }
local BASE_DEFAULT = "It_Recipe_Health_01"
local NEW_KEYS = { "base", "description", "icon", "ingredients", "learn", "mesh", "name", "properties", "result", "skills", "station", "value" }
local NEW_KEY = {}
for _, k in ipairs(NEW_KEYS) do NEW_KEY[k] = true end

local function new_recipes_section(data, _)
    local jobs, problems = {}, {}
    if type(data) ~= "table" then return jobs, { "new_recipes: expected a table { NewRecipeName = { station = \"anvil\", ingredients = ..., result = ... } }" } end
    for name in pairs(data) do
        if type(name) == "string" then planned_recipes[name] = true end
    end
    local makers = {}   -- creates every recipe's template first: a recipe's result and ingredients may be another new recipe
    for _, name in ipairs(sorted_keys(data)) do
        local entry = data[name]
        local what = "new_recipes." .. tostring(name)
        local function bad(msg) problems[#problems + 1] = what .. ": " .. msg end
        if type(name) ~= "string" or type(entry) ~= "table" then bad("expected a table { station = ..., ingredients = ..., result = ... }")
        else
            local before = #problems
            local known = true
            for key in pairs(entry) do
                if not NEW_KEY[key] then known = false; bad(("unknown setting '%s' (valid: %s)"):format(tostring(key), table.concat(NEW_KEYS, ", "))) end
            end
            local parts = {}
            if entry.station == nil then bad("'station' is required (" .. STATION_TEXT .. ")")
            else
                parts.station = engine_station(entry.station)
                if not parts.station then bad(("station: unknown '%s' (valid: %s)"):format(tostring(entry.station), STATION_TEXT)) end
            end
            if entry.ingredients == nil then bad("'ingredients' is required") else parts.ingredients = parse_ingredients(entry.ingredients, "ingredients", bad) end
            if entry.result == nil then bad("'result' is required (an item name)") else parts.result = parse_result(entry.result, "result", bad) end
            if entry.skills ~= nil then parts.skills = parse_skills(entry.skills, "skills", bad) else parts.skills = {} end
            parts.properties = check_properties(entry.properties, "properties", bad)
            local base = entry.base or BASE_DEFAULT
            if type(base) ~= "string" then bad("base must be a recipe template name")
            else
                local bt = tmpl.get(base)
                if not bt then bad(("base: unknown template '%s'"):format(base))
                elseif not has_set(bt, "gCRecipe_PS") then bad(("base: '%s' has no gCRecipe_PS: the base of a recipe must be a recipe item (risen.recipe.names())"):format(base)) end
            end
            if entry.learn ~= nil and type(entry.learn) ~= "boolean" then bad("learn must be true or false") end
            -- item settings go to template.create (validated there against the base); the recipe parts are written afterwards
            local over = {}
            for _, k in ipairs({ "name", "description", "icon", "mesh", "value" }) do over[k] = entry[k] end
            if over.name == nil and parts.result then over.name = "Recipe: " .. describe_item(parts.result.item) end
            if over.description == nil and parts.ingredients and parts.result and parts.station then
                local list = {}
                for _, e in ipairs(parts.ingredients) do list[#list + 1] = ("%dx %s"):format(e.count, describe_item(e.item)) end
                over.description = ("%s recipe: %s -> %s."):format(STATION_OF[parts.station] or "?", table.concat(list, ", "), describe_item(parts.result.item))
                over.description = over.description:gsub("^%l", string.upper)
            end
            if over.value ~= nil and not is_int(over.value, 0, 2 ^ 31 - 1) then bad("value must be a non-negative integer") end
            if known and #problems == before then
                if tmpl.exists(name) and tmpl.created()[name] == nil then
                    bad(("a template called '%s' already exists (it is not one this mod layer created): choose a new name"):format(name))
                end
                if #problems == before then
                    made[name] = true
                    local keys = {}
                    for _, k in ipairs(sorted_keys(entry)) do keys[#keys + 1] = tostring(k) end
                    local summary = {}
                    for _, e in ipairs(parts.ingredients) do summary[#summary + 1] = ("%dx %s"):format(e.count, e.item) end
                    local function make(opts)
                        local ov = {}
                        for k, v in pairs(over) do ov[k] = v end
                        ov.properties = {}
                        for set, props in pairs(parts.properties or {}) do ov.properties[set] = props end
                        if next(ov.properties) == nil then ov.properties = nil end
                        return tmpl.create(name, entry.base or BASE_DEFAULT, ov, { instances = opts.instances })
                    end
                    makers[#makers + 1] = make
                    jobs[#jobs + 1] = {
                        label = ("new recipe %s (%s: %s -> %s)"):format(name, STATION_OF[parts.station] or "?", table.concat(summary, " + "), parts.result.item),
                        run = function(opts)
                            local t, err = make(opts)
                            if not t then return nil, err end
                            local ok, werr, n = write_parts(name, { station = parts.station, ingredients = parts.ingredients, skills = parts.skills, result = parts.result }, opts)
                            if not ok then return nil, werr end
                            return true, nil, n or 0
                        end,
                    }
                end
            end
        end
    end
    if #makers > 1 then
        table.insert(jobs, 1, {
            label = ("new recipe templates (%d)"):format(#makers),
            run = function(opts)
                for _, make in ipairs(makers) do
                    local t, err = make(opts)
                    if not t then return nil, err end
                end
                return true, nil, 0
            end,
        })
    end
    return jobs, problems
end
balance.register_section("new_recipes", new_recipes_section)

-- risen.recipe.create(name, spec [, { instances = true }]) -> report | nil, reason, problems: one new recipe (see the `new_recipes` section)
function risen.recipe.create(name, spec, opts)
    return balance.apply({ new_recipes = { [name] = spec } }, opts)
end
)LUA";

// The per-mod part: `learn = true` recipes are given to the player once per savegame when the world is ready.
inline const char* kRecipeModChunk = R"LUA(
local api = ...
local shared = api.recipe
local r = setmetatable({}, { __index = shared })
api.recipe = r
local watched, hooked = {}, false

-- risen.recipe.learn_on_start(name): this mod gives the player the recipe item once per savegame (a game variable remembers it) on every `world.ready`
-- (new game and load). Done by the `learn = true` setting of new recipes.
function r.learn_on_start(name)
    watched[name] = true
    if hooked then return true end
    hooked = true
    api.on("world.ready", function()
        local player = api.entity.player()
        if not player then return end
        for _, n in ipairs((function() local l = {} for k in pairs(watched) do l[#l + 1] = k end table.sort(l) return l end)()) do
            local given, why = shared.learn_once(player, n)
            if given == nil then api.log.warn("recipe: could not teach '" .. n .. "': " .. tostring(why)) end
        end
    end)
    return true
end

-- risen.recipe.create(name, spec [, opts]): `learn = true` also registers this mod's handler (the shared function cannot: it does not know the mod)
function r.create(name, spec, opts)
    local report, why, problems = shared.create(name, spec, opts)
    if report and type(spec) == "table" and spec.learn then r.learn_on_start(name) end
    return report, why, problems
end
)LUA";

}  // namespace spass::game
