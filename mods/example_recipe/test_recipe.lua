-- Live check of example_recipe (only loaded in a test run): scripts/smoke-test.ps1 -Scenario ex_recipe, several game processes (-Param phase=...):
--   save          new game: the recipe and the item exist, the player got the plan (learn = true); the REAL anvil interaction crafts (twice), a partial stack and
--                 a missing skill are refused; the sword is equipped, quick save
--   load          fresh process, quick load: the crafted swords, the plan and the "given once" flag are there; one more real craft
--   load_missing  fresh process without the mod's content: the saved stacks stay dormant, the game runs, a save keeps them
--   load_again    fresh process with the mod again: everything resolves again, a craft through risen.recipe.craft
-- The real path: risen.interact.begin(player, anvil) is what the use key does; the engine's PreInteract script opens the anvil's inventory page and the
-- Interact script waits for the player's choice. The click on the recipe in that page is the engine's own script InvEvent_OnClick_CraftSelect; then the
-- engine plays the smith animation and calls PSInventory::Synthesize (ingredients out, result in).
local T = risen.test

local RECIPE, SWORD = "Spass_Recipe_Improved_Sword", "Spass_Sword_Improved"
local VAR = "SPASS_RECIPE_" .. RECIPE

local function player_now() return assert(risen.entity.player(), "no player") end
local function tpl(name) return assert(risen.template.get(name)) end
local function count(name) return risen.item.count(player_now(), name) end

return function(spec, reports)
    local phase = T.param("phase", "save")
    local PLAN = {
        save = {
            "install reported success", "the new item has its own numbers and label", "the recipe is a recipe item with the declared parts", "learn = true gives the plan once",
            "a missing skill refuses (engine check and the anvil)", "partial ingredients: the anvil page does not list the recipe, nothing is consumed",
            "real anvil: ingredients consumed, result created", "real anvil: events", "real anvil: crafting twice", "the improved sword is equipped", "the temporary hold copies are not counted",
        },
        load = { "load: the template resolves", "load: numbers and label", "load: items and plan are back", "load: the recipe is known and was not given again", "load: the sword is still equipped", "load: one more real craft" },
        load_missing = { "missing: the mod's templates are gone", "missing: the game runs and the stacks are dormant", "missing: a save in that state works" },
        load_again = { "again: everything resolves", "again: risen.recipe.craft works", "again: the counts are exact" },
    }
    T.plan(PLAN[phase] or {})

    -- ---- the events of the crafting, counted exactly ---------------------------------------------------------------------------------------
    local ev = { crafting = {}, added = {}, removed = {}, equipped = {} }
    risen.on("crafting", function(who, recipe, result, n, ingredients)
        ev.crafting[#ev.crafting + 1] = { who = who, recipe = recipe, result = result, count = n, ingredients = ingredients }
    end)
    risen.on("inventory.added", function(owner, template, n, total, cause) ev.added[#ev.added + 1] = ("%s x%d total %d (%s)"):format(template, n, total, cause) end)
    risen.on("inventory.removed", function(owner, template, n, total, cause) ev.removed[#ev.removed + 1] = ("%s x%d total %d (%s)"):format(template, n, total, cause) end)
    risen.on("item.equipped", function(owner, template, slot) ev.equipped[#ev.equipped + 1] = { template = template, slot = slot } end)

    -- the real anvil interaction for the recipe item; returns once the player stands in PS_Normal again
    local function use_anvil(p)
        local x, y, z = p:position()
        local anvil = assert(risen.entity.spawn("Object_Interact_Anvil", (x or 0) + 300, y or 0, z or 0))
        T.sleep(10)
        assert(risen.interact.begin(p, anvil))                                  -- the use key: AI state ZS_Interact_PreInteract
        T.wait_until(function() return p:state() == "ZS_Interact_Interact" end, 60, "the player reached the anvil")
        T.sleep(60)                                                             -- the anvil's inventory page is open
        local stack
        for i = 0, p:stack_count() - 1 do
            local it = p:stack_item(i)
            if it and it:name() == RECIPE and not stack then stack = i end
        end
        assert(stack, "the plan is not in the inventory")
        risen.script.call("InvEvent_OnClick_CraftSelect", p, nil, stack)       -- the click on the recipe in that page (amount 1)
        T.wait_until(function() return p:state() == "PS_Normal" end, 90, "the crafting animation is over")
        T.sleep(30)
        anvil:kill()
    end
    local function ready_to_smith(p, ore, swords)
        assert(risen.npc.set_stats(p, { skills = { Smith = 1, Strength = 20 } }))
        if count("It_SmithHammer") < 1 then risen.item.give(p, "It_SmithHammer", 1) end
        risen.item.give(p, "It_IronOre", ore)
        risen.item.give(p, "It_1H_Sword", swords)
    end
    local function reset_events() for _, l in pairs(ev) do for i = #l, 1, -1 do l[i] = nil end end end

    T.run(function()
        if phase == "save" then
            T.wait_event("session.start", 180, function(m) return m == "new" end)
        else
            T.wait_event("session.start", 180, function(m) return m == "load" end)
        end
        T.sleep(150)
        local p = player_now()

        if phase == "save" then
            local bad = {}
            for _, r in ipairs(reports) do if not (r.report.ok and #r.report.failed == 0 and #r.report.applied == 2) then bad[#bad + 1] = r.when end end
            T.expect("install reported success", #reports >= 2 and #bad == 0, ("%d applications, failed: %s"):format(#reports, table.concat(bad, ",")))

            local sword = tpl(SWORD)
            T.eq("the new item has its own numbers and label", { sword:get_property("gCItem_PS", "GoldValue"), sword:get_property("gCDamage_PS", "DamageAmount"), sword:focus_name() },
                { "65", "18", "Slightly Improved Sword" })
            local info = assert(risen.recipe.info(RECIPE))
            T.eq("the recipe is a recipe item with the declared parts", { info.station, info.ingredients, info.result, info.skills, info.shipped }, {
                "anvil", { { item = "It_1H_Sword", count = 1 }, { item = "It_IronOre", count = 2 } }, { item = SWORD, count = 1 }, { Smith = 1 }, false })
            T.eq("learn = true gives the plan once", { risen.recipe.known(p, RECIPE), count(RECIPE), risen.gamevar.get(VAR) }, { true, 1, 1 })

            -- the skill: the player starts with Smith 0
            assert(risen.npc.set_stats(p, { skills = { Strength = 20, Smith = 0 } }))
            risen.item.give(p, "It_SmithHammer", 1); risen.item.give(p, "It_IronOre", 1); risen.item.give(p, "It_1H_Sword", 2)
            local x, y, z = p:position()
            local anvil = assert(risen.entity.spawn("Object_Interact_Anvil", (x or 0) + 300, y or 0, z or 0))
            local n0, why0 = risen.recipe.can_craft(p, RECIPE)
            local engine_can = risen.interact.can(p, anvil)   -- CanInteract_Player_Anvil: NoSkillSmithing
            T.eq("a missing skill refuses (engine check and the anvil)", { n0, tostring(why0):find("skill Smith 1 (has 0)", 1, true) ~= nil, engine_can }, { 0, true, false })

            -- one ore only: the recipe needs two. The anvil page lists a recipe with the select limit of InvFilter_Craft_Anvil (0 = not craftable)
            assert(risen.npc.set_stats(p, { skills = { Smith = 1 } }))
            local stack
            for i = 0, p:stack_count() - 1 do if p:stack_item(i):name() == RECIPE and not stack then stack = i end end
            local limit1 = risen.script.call("InvFilter_Craft_Anvil", p, nil, stack)
            local ok1, why1 = risen.recipe.craft(p, RECIPE)
            T.eq("partial ingredients: the anvil page does not list the recipe, nothing is consumed", { limit1, ok1, why1 ~= nil, count("It_IronOre"), count("It_1H_Sword"), count(SWORD) }, { 0, nil, true, 1, 2, 0 })
            anvil:kill()

            -- the real thing: ingredients for two crafts (ore 1 -> 5, swords 2 -> 2: one sword is the one the player will fight with later)
            risen.item.give(p, "It_IronOre", 4)
            T.sleep(10)
            reset_events()
            use_anvil(p)
            T.eq("real anvil: ingredients consumed, result created", { count("It_1H_Sword"), count("It_IronOre"), count(SWORD), count(RECIPE), count("It_SmithHammer") }, { 1, 3, 1, 1, 1 })
            -- exactly: two removals and one addition, all with the cause crafting, and one `crafting` event with the engine's recipe data
            table.sort(ev.removed)
            local c = ev.crafting[1]
            T.eq("real anvil: events", { ev.removed, ev.added, #ev.crafting, c and c.recipe, c and c.result, c and c.count, c and c.ingredients, c and c.who:name() }, {
                { "It_1H_Sword x1 total 1 (crafting)", "It_IronOre x2 total 3 (crafting)" }, { SWORD .. " x1 total 1 (crafting)" }, 1, RECIPE, SWORD, 1,
                { { item = "It_1H_Sword", count = 1 }, { item = "It_IronOre", count = 2 } }, "PC_Hero" })
            -- twice
            reset_events()
            use_anvil(p)
            T.eq("real anvil: crafting twice", { count("It_1H_Sword"), count("It_IronOre"), count(SWORD), #ev.crafting, #ev.added, #ev.removed }, { 0, 1, 2, 1, 1, 2 })

            -- the improved sword is a weapon like any other: equip it (Strength 10 required, like the regular sword)
            local equipped = risen.item.equip(p, SWORD)
            T.sleep(30)
            local slot
            for _, e in ipairs(ev.equipped) do if e.template == SWORD then slot = e.slot end end
            T.eq("the improved sword is equipped", { equipped and true or false, slot ~= nil and risen.spell.equipped(p, slot) or nil }, { true, SWORD })
            -- the animation holds temporary copies (stack type 2) of the plan, the hammer and the result: they are not counted as possessions
            local names = {}
            for i = 0, p:stack_count() - 1 do local it = p:stack_item(i) names[#names + 1] = it and it:name() or "?" end
            T.expect("the temporary hold copies are not counted", count(RECIPE) == 1 and count("It_SmithHammer") == 1 and count(SWORD) == 2, "stacks: " .. table.concat(names, ","))
            risen.log.info("example_recipe: slot of the sword " .. tostring(slot) .. " stacks " .. table.concat(names, ","))
            risen.gamevar.set("SPASS_EXRECIPE_SLOT", slot or -1)

            risen.world.quick_save()
            T.wait_event("save.after", 60)
            T.sleep(200)   -- the engine keeps writing after save.after: the run must not end right after a save

        elseif phase == "load" then
            local sword = risen.template.get(SWORD)
            T.eq("load: the template resolves", { sword ~= nil, risen.template.id_for(SWORD) == (sword and sword:id()), risen.recipe.info(RECIPE) ~= nil }, { true, true, true })
            sword = tpl(SWORD)
            T.eq("load: numbers and label", { sword:get_property("gCItem_PS", "GoldValue"), sword:get_property("gCDamage_PS", "DamageAmount"), sword:focus_name() }, { "65", "18", "Slightly Improved Sword" })
            T.eq("load: items and plan are back", { count(SWORD), count(RECIPE), count("It_SmithHammer"), count("It_IronOre"), count("It_1H_Sword") }, { 2, 1, 1, 1, 0 })
            T.eq("load: the recipe is known and was not given again", { risen.recipe.known(p, RECIPE), count(RECIPE), risen.gamevar.get(VAR) }, { true, 1, 1 })
            local slot = risen.gamevar.get("SPASS_EXRECIPE_SLOT") or -1
            T.eq("load: the sword is still equipped", risen.spell.equipped(p, slot), SWORD)
            ready_to_smith(p, 1, 1)   -- ore 1 + 1 = 2, a sword
            reset_events()
            use_anvil(p)
            T.eq("load: one more real craft", { count(SWORD), count("It_IronOre"), count("It_1H_Sword"), #ev.crafting }, { 3, 0, 0, 1 })
            risen.world.quick_save()
            T.wait_event("save.after", 60)
            T.sleep(200)

        elseif phase == "load_missing" then
            local gone = not risen.template.exists(SWORD) and not risen.template.exists(RECIPE) and risen.recipe.info(RECIPE) == nil
            T.expect("missing: the mod's templates are gone", gone, "the spec was not installed in this phase")
            T.sleep(400)
            T.eq("missing: the game runs and the stacks are dormant", { risen.entity.player() ~= nil, count(SWORD), count(RECIPE), p:stack_count() > 0 }, { true, 0, 0, true })
            risen.world.quick_save()
            T.wait_event("save.after", 60)
            T.sleep(200)
            T.expect("missing: a save in that state works", true)

        elseif phase == "load_again" then
            T.eq("again: everything resolves", { count(SWORD), count(RECIPE), risen.recipe.known(p, RECIPE), risen.gamevar.get(VAR) }, { 3, 1, true, 1 })
            ready_to_smith(p, 2, 1)
            local ok, res = risen.recipe.craft(p, RECIPE)
            T.eq("again: risen.recipe.craft works", { ok, res }, { true, SWORD })
            T.eq("again: the counts are exact", { count(SWORD), count("It_IronOre"), count("It_1H_Sword") }, { 4, 0, 0 })
        end
    end, "example_recipe")
end
