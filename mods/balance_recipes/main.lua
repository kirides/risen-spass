-- Recipe balancing with risen.balance (docs/LUA_API.md "Recipes", docs/RE.md section 30), played as a risen.test scenario: scripts/smoke-test.ps1 -Scenario recipes.
--
-- A shipped recipe is an item with a gCRecipe_PS property set; the `recipes` section edits its ingredients, required skills, result and station. Everything is
-- verified in the engine's own crafting: PSInventory::CanSynthesize (the check) and ::Synthesize (the craft) through risen.recipe.can_craft/craft, and the real
-- smithing anvil (risen.interact.begin -> the engine's PreInteract/Interact scripts, the click on the recipe is the engine's script InvEvent_OnClick_CraftSelect).
local T = risen.test
local log = risen.log

local SPEC = {
    recipes = {
        -- the hot steel blank at the anvil (shipped: the blank alone, Smith 1) now also needs two iron ore and Smith 2
        It_1H_Steel_Hot = { ingredients = { It_1H_Steel_Hot = 1, It_IronOre = 2 }, skills = { Smith = 2 } },
        -- the health potion recipe (shipped: flask + 3 plants + wine, Alchemy 1): one plant and two wine, no skill, and a better potion comes out
        It_Recipe_Health_01 = { counts = { It_Plant_Health_01 = 1, It_Wine = 2 }, skills = {}, result = "It_Po_Health_02" },
    },
}

risen.balance.install(SPEC, {
    on_applied = function(when, report)
        if when == "first tick" then
            T.expect("the spec is applied on the first tick", report.ok and #report.applied == 2 and #report.failed == 0,
                ("applied %d, failed %d: %s"):format(#report.applied, #report.failed, table.concat(report.failed, "; ")))
        elseif when:find("^session.start") then
            T.expect("session.start applies it again (idempotent)", report.ok and #report.applied == 2 and #report.failed == 0,
                ("applied %d, failed %d: %s"):format(#report.applied, #report.failed, table.concat(report.failed, "; ")))
        end
    end,
})

local HOT, SHAPED, ORE = "It_1H_Steel_Hot", "It_1H_Steel_Shaped", "It_IronOre"
local ALCH, POTION1, POTION2 = "It_Recipe_Health_01", "It_Po_Health_01", "It_Po_Health_02"

local PLAN = {
    "the spec is applied on the first tick", "session.start applies it again (idempotent)", "the edited recipes read back",
    "applying the spec again changes nothing", "invalid specs are refused as a whole and write nothing",
    "skill below the edited requirement: refused by the engine, nothing consumed", "missing ingredient: the anvil page does not list the recipe",
    "real anvil: the edited recipe consumes exactly its ingredients", "real anvil: events", "alchemy table (real): the edited counts and result", "alchemy: the removed skill requirement (Alchemy 0)", "alchemy: the edited result item", "ResultAmount is not read by the engine's craft", "an edited recipe stays editable (modify)",
}
T.plan(PLAN)

local function player_now() return assert(risen.entity.player(), "no player") end
local function count(name) return risen.item.count(player_now(), name) end

local ev = { crafting = {}, added = {}, removed = {} }
risen.on("crafting", function(who, recipe, result, n, ingredients) ev.crafting[#ev.crafting + 1] = { recipe = recipe, result = result, count = n, ingredients = ingredients } end)
risen.on("inventory.added", function(owner, template, n, total, cause) ev.added[#ev.added + 1] = ("%s x%d (%s)"):format(template, n, cause) end)
risen.on("inventory.removed", function(owner, template, n, total, cause) ev.removed[#ev.removed + 1] = ("%s x%d (%s)"):format(template, n, cause) end)
local function reset_events() for _, l in pairs(ev) do for i = #l, 1, -1 do l[i] = nil end end end

local function stack_of(p, name)
    for i = 0, p:stack_count() - 1 do
        local it = p:stack_item(i)
        if it and it:name() == name then return i end
    end
end

-- the real station interaction for the recipe item `recipe` (see mods/example_recipe/test_recipe.lua)
local function use_station(p, mob_template, recipe)
    local x, y, z = p:position()
    local mob = assert(risen.entity.spawn(mob_template, (x or 0) + 300, y or 0, z or 0))
    T.sleep(10)
    -- (a smith recipe item in the left hand makes the engine refuse every other station: CanInteract_Player "CarryToAnvil", so alchemy goes first)
    assert(risen.interact.begin(p, mob))
    T.wait_until(function() return p:state() == "ZS_Interact_Interact" end, 60, "the player reached the station")
    T.sleep(60)
    -- (with ingredients for more than one craft the page asks how many first: a modal amount dialog the scenario would have to answer; one craft's worth avoids it)
    risen.script.call("InvEvent_OnClick_CraftSelect", p, nil, assert(stack_of(p, recipe), "the recipe item is not in the inventory"))
    T.wait_until(function() return p:state() == "PS_Normal" end, 90, "the crafting animation is over")
    T.sleep(30)
    mob:kill()
end

T.run(function()
    T.wait_event("session.start", 180, function(m) return m == "new" end)
    T.sleep(150)
    local p = player_now()
    assert(risen.npc.set_stats(p, { skills = { Smith = 1, Alchemy = 0, Strength = 20 } }))

    -- ---- the numbers -----------------------------------------------------------------------------------------------------------------------
    local hot, alch = assert(risen.recipe.info(HOT)), assert(risen.recipe.info(ALCH))
    local function summary(i) local t = {} for _, e in ipairs(i.ingredients) do t[#t + 1] = e.item .. "x" .. e.count end table.sort(t) return table.concat(t, "+") end
    T.eq("the edited recipes read back", { hot.station, summary(hot), hot.result.item, hot.skills, alch.station, summary(alch), alch.result.item, alch.skills }, {
        "anvil", HOT .. "x1+" .. ORE .. "x2", SHAPED, { Smith = 2 }, "alchemy", "It_EmptyFlaskx1+It_Plant_Health_01x1+It_Winex2", POTION2, {} })
    assert(risen.balance.apply(SPEC)); assert(risen.balance.apply(SPEC))
    local again = assert(risen.recipe.info(HOT))
    T.eq("applying the spec again changes nothing", { summary(again), again.skills, summary(assert(risen.recipe.info(ALCH))) }, { summary(hot), { Smith = 2 }, summary(alch) })
    local refusals = {}
    for _, spec in ipairs({ { recipes = { [HOT] = { skills = { Smith = 3 } }, It_Nope = { skills = {} } } }, { recipes = { [HOT] = { counts = { It_Apple = 1 } } } },
                           { recipes = { [HOT] = { result = { item = SHAPED, count = 2 } } } }, { new_recipes = { Bad = { station = "smithy", ingredients = { [ORE] = 1 }, result = SHAPED } } } }) do
        local rep, why = risen.balance.apply(spec)
        refusals[#refusals + 1] = rep == nil and why ~= nil
    end
    T.eq("invalid specs are refused as a whole and write nothing", { refusals, risen.recipe.info(HOT).skills, risen.template.exists("Bad") }, { { true, true, true, true }, { Smith = 2 }, false })

    -- ---- alchemy (the real alchemy table, then the engine's check and craft without a station) ------------------------------------------------------------------------------------
    risen.item.give(p, ALCH, 1)
    for _, e in ipairs({ { "It_EmptyFlask", 1 }, { "It_Plant_Health_01", 1 }, { "It_Wine", 2 } }) do risen.item.give(p, e[1], e[2]) end
    -- shipped: Alchemy 1 needed, 3 plants per potion; edited: no skill, 1 plant, 2 wine
    local canx = risen.recipe.can_craft(p, ALCH)
    assert(risen.npc.set_stats(p, { skills = { Alchemy = 1 } }))   -- the alchemy table itself asks for the skill
    reset_events()
    use_station(p, "Object_Interact_Alchemy_Table", ALCH)
    T.eq("alchemy table (real): the edited counts and result", { canx, count("It_EmptyFlask"), count("It_Plant_Health_01"), count("It_Wine"), count(POTION2), count(POTION1), #ev.crafting,
        ev.crafting[1] and ev.crafting[1].result }, { 1, 0, 0, 0, 1, 0, 1, POTION2 })
    assert(risen.npc.set_stats(p, { skills = { Alchemy = 0 } }))
    for _, e in ipairs({ { "It_EmptyFlask", 1 }, { "It_Plant_Health_01", 1 }, { "It_Wine", 2 } }) do risen.item.give(p, e[1], e[2]) end
    local ok3 = risen.recipe.craft(p, ALCH, 1)
    T.eq("alchemy: the removed skill requirement (Alchemy 0)", { ok3, count("It_EmptyFlask"), count("It_Plant_Health_01"), count("It_Wine"), count(POTION2) }, { true, 0, 0, 0, 2 })
    T.eq("alchemy: the edited result item", { risen.recipe.info(ALCH).result.item, count(POTION1) }, { POTION2, 0 })

    -- ---- the smithing anvil -----------------------------------------------------------------------------------------------------------------
    risen.item.give(p, "It_SmithHammer", 1)
    risen.item.give(p, HOT, 1); risen.item.give(p, ORE, 2)
    local n, why = risen.recipe.can_craft(p, HOT)
    local ok, cwhy = risen.recipe.craft(p, HOT)
    T.eq("skill below the edited requirement: refused by the engine, nothing consumed", { n, tostring(why):find("skill Smith 2 (has 1)", 1, true) ~= nil, ok, cwhy ~= nil, count(HOT), count(ORE), count(SHAPED) },
        { 0, true, nil, true, 1, 2, 0 })
    assert(risen.npc.set_stats(p, { skills = { Smith = 2 } }))
    assert(risen.item.take(p, ORE, 1) == 1)   -- one ore left, two needed
    local limit = risen.script.call("InvFilter_Craft_Anvil", p, nil, assert(stack_of(p, HOT)))
    T.eq("missing ingredient: the anvil page does not list the recipe", { limit, risen.recipe.can_craft(p, HOT), count(HOT), count(ORE) }, { 0, 0, 1, 1 })
    risen.item.give(p, ORE, 1)
    T.sleep(10)
    reset_events()
    use_station(p, "Object_Interact_Anvil", HOT)
    T.eq("real anvil: the edited recipe consumes exactly its ingredients", { count(HOT), count(ORE), count(SHAPED), count("It_SmithHammer") }, { 0, 0, 1, 1 })
    table.sort(ev.removed)
    local c = ev.crafting[1]
    T.eq("real anvil: events", { ev.removed, ev.added, #ev.crafting, c and c.recipe, c and c.result, c and c.ingredients }, {
        { HOT .. " x1 (crafting)", ORE .. " x2 (crafting)" }, { SHAPED .. " x1 (crafting)" }, 1, HOT, SHAPED, { { item = HOT, count = 1 }, { item = ORE, count = 2 } } })

    -- ResultAmount is data the engine's Synthesize never reads (it creates `count` items per craft)
    risen.item.give(p, "It_EmptyFlask", 1); risen.item.give(p, "It_Plant_Health_01", 1); risen.item.give(p, "It_Wine", 2)
    assert(risen.template.patch(ALCH, { gCRecipe_PS = { ResultAmount = "3" } }))
    local before = count(POTION2)
    assert(risen.recipe.craft(p, ALCH))
    local made = count(POTION2) - before
    assert(risen.template.patch(ALCH, { gCRecipe_PS = { ResultAmount = "1" } }))
    T.eq("ResultAmount is not read by the engine's craft", { made }, { 1 })

    -- modify() is the one-recipe form: put the shipped skill back
    assert(risen.recipe.modify(HOT, { skills = { Smith = 1 }, counts = { [ORE] = 5 } }))
    T.eq("an edited recipe stays editable (modify)", { risen.recipe.info(HOT).skills, summary(assert(risen.recipe.info(HOT))) }, { { Smith = 1 }, HOT .. "x1+" .. ORE .. "x5" })
    log.info("balance_recipes: done")
end, "recipes")
