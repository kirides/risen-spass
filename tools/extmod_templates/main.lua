-- risen_extended: the data half of the Risen Extended Mod re-implementation (generated mod, tools/extmod_gen.py). Generic code; the numbers are in data/*.lua.
--
--   stage 1  new item templates (risen.template.create, clone of the closest shipped item) and their extra properties
--   stage 2  changes of shipped items (risen.balance items section)
--   stage 3  recipes: edits of the new recipe items, new recipe items for pearls / gold / ore (shipped items cannot become recipes at run time)
--   stage 4  other template edits (monster actors, light range)
-- Applied on the first `tick` (templates) and on every `session.start` (again, now with the live entities, because a world keeps its own copies).
-- Every entry is applied on its own: one bad entry is logged and counted, the rest still applies.
local cfg = require("config")
local ITEMS = require("data.items")
local NEW = require("data.new_items")
local REC = require("data.recipes")
local PATCH = require("data.patches")
local log = risen.log

local M = { passes = {}, failed = {} }   -- M.passes[when] = { new_items = n, ... }; M.failed = { "stage name: reason" }

local function sorted(t)
    local keys = {}
    for k in pairs(t) do keys[#keys + 1] = k end
    table.sort(keys)
    return keys
end

local function fail(first, stage, name, why)
    local msg = ("%s %s: %s"):format(stage, name, tostring(why))
    if first then
        M.failed[#M.failed + 1] = msg
        log.warn("risen_extended: " .. msg)
    end
end

local function apply_properties(name, props, instances, first, stage)
    local ok_n = 0
    for _, set in ipairs(sorted(props)) do
        for _, p in ipairs(sorted(props[set])) do
            local ok, err = risen.template.patch(name, { [set] = { [p] = props[set][p] } }, { instances = instances })
            if ok then ok_n = ok_n + 1 else fail(first, stage .. " property", name .. "." .. set .. "." .. p, err) end
        end
    end
    return ok_n
end

local CREATE_KEYS = { "value", "damage", "mana_cost", "requires", "modifies", "use_type", "category", "mesh", "icon", "material" }

local function stage_new_items(instances, first, counts)
    for _, name in ipairs(sorted(NEW)) do
        local e = NEW[name]
        local over = {}
        for _, k in ipairs(CREATE_KEYS) do over[k] = e[k] end
        local t, why = risen.template.create(name, e.base, over, { instances = instances })
        if not t and (e.mesh or e.icon) and (tostring(why):find("mesh") or tostring(why):find("icon")) then
            -- a mesh or icon name the layer cannot resolve (yet): create it anyway, it only fails to show
            over.check_assets = false
            t, why = risen.template.create(name, e.base, over, { instances = instances })
            if t and first then log.warn(("risen_extended: %s: created with unchecked asset names (%s)"):format(name, tostring(why))) end
        end
        if t then
            counts.new_items = (counts.new_items or 0) + 1
            if e.properties then counts.new_item_props = (counts.new_item_props or 0) + apply_properties(name, e.properties, instances, first, "new item") end
        else
            fail(first, "new item", name, why)
        end
    end
end

local function stage_items(instances, first, counts)
    for _, name in ipairs(sorted(ITEMS)) do
        local e = ITEMS[name]
        local entry = {}
        for k, v in pairs(e) do if k ~= "properties" then entry[k] = v end end
        entry.properties = e.properties
        local report, why = risen.balance.apply({ items = { [name] = entry } }, { instances = instances })
        if not report or not report.ok then
            -- retry without the free-form properties, then one by one, so a single bad property costs only itself
            entry.properties = nil
            local r2, why2 = true, nil
            if next(entry) ~= nil then r2, why2 = risen.balance.apply({ items = { [name] = entry } }, { instances = instances }) end
            if not r2 then fail(first, "item", name, why2 or why)
            else
                counts.items = (counts.items or 0) + 1
                counts.item_props = (counts.item_props or 0) + apply_properties(name, e.properties or {}, instances, first, "item")
            end
        else
            counts.items = (counts.items or 0) + 1
            if e.properties then counts.item_props = (counts.item_props or 0) + 1 end
        end
    end
end

local function stage_recipes(instances, first, counts)
    for _, name in ipairs(sorted(REC.edit)) do
        local report, why = risen.balance.apply({ recipes = { [name] = REC.edit[name] } }, { instances = instances })
        if report and report.ok then counts.recipes = (counts.recipes or 0) + 1 else fail(first, "recipe", name, why) end
    end
    for _, name in ipairs(sorted(REC.new)) do
        local e = {}
        for k, v in pairs(REC.new[name]) do if k ~= "from" then e[k] = v end end
        e.learn = nil   -- given by this mod's own start handler below (cfg.learn_recipes)
        local report, why = risen.balance.apply({ new_recipes = { [name] = e } }, { instances = instances })
        if report and report.ok then counts.new_recipes = (counts.new_recipes or 0) + 1 else fail(first, "new recipe", name, why) end
    end
end

local function stage_patches(instances, first, counts)
    for _, name in ipairs(sorted(PATCH)) do
        local ok, err = risen.template.patch(name, PATCH[name], { instances = instances })
        if ok then counts.patches = (counts.patches or 0) + 1 else fail(first, "patch", name, err) end
    end
end

local function apply(when, instances, first)
    local counts = {}
    stage_new_items(instances, first, counts)
    stage_items(instances, first, counts)
    stage_recipes(instances, first, counts)
    stage_patches(instances, first, counts)
    M.passes[#M.passes + 1] = { when = when, counts = counts }
    local parts = {}
    for _, k in ipairs(sorted(counts)) do parts[#parts + 1] = k .. "=" .. counts[k] end
    log.info(("risen_extended: %s: %s; %d problem(s) so far"):format(when, table.concat(parts, " "), #M.failed))
end

local first = true
risen.on("tick", function()
    if not first then return end
    first = false
    apply("first tick", false, true)
end)
risen.on("session.start", function(mode)
    apply("session.start (" .. tostring(mode) .. ")", true, false)
end)

-- ---- the player's start: plans of the new recipe items, and (config.lua) a kit of the new items until the traders carry them ----
local function give_once(player, name, n)
    local var = "RISEN_EXTENDED_GIVEN_" .. name
    if (risen.gamevar.get(var) or 0) ~= 0 then return end
    risen.gamevar.set(var, 1)
    risen.item.give(player, name, n or 1)
end

risen.on("world.ready", function(mode)
    local player = risen.entity.player()
    if not player then return end
    for _, name in ipairs(sorted(REC.new)) do
        if cfg.learn_recipes then
            local ok, err = pcall(function() risen.recipe.learn_once(player, name) end)
            if not ok then log.warn("risen_extended: learn " .. name .. ": " .. tostring(err)) end
        end
    end
    local kit = cfg.starter_kit
    if mode == "new" and kit and kit ~= "none" then
        for _, name in ipairs(sorted(NEW)) do
            local skip = false
            if kit == "plans" and not name:find("_Recipe$") then skip = true end
            if cfg.skip_intermediate and (name:find("_Hot$") or name:find("_Shaped$") or name:find("_Dull$")) then skip = true end
            if not skip then give_once(player, name, 1) end
        end
        log.info("risen_extended: starter kit '" .. tostring(kit) .. "' handed out")
    end
end)

M.cfg = cfg
M.items, M.new, M.rec, M.patch = ITEMS, NEW, REC, PATCH

-- the scenario (scripts/test-run.ps1 -WaitForResult -Param check=extmod) is part of the mod, loaded only in a test run
if risen.test and risen.test.is_active() and risen.test.param("check") == "extmod" then
    require("check")(M)
end
