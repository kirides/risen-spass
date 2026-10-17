-- Checks the exported risen-modkit showcase (mods/modkit_showcase, exported from examples/modkit/showcase.rmk.json) in a running game:
--   .\scripts\test-run.ps1 -Mods modkit_showcase,modkit_driver -WaitForResult -Seconds 240 -Scenario modkit
-- What the editor promised is what the engine holds: new items with their numbers and texts, changes of shipped items, the item group.
local T = risen.test

T.plan {
    "the export was applied without failures",
    "new item exists",
    "new item numbers",
    "new item requirement",
    "new item label",
    "new item can be given",
    "new potion modifier",
    "changed item numbers",
    "changed item label",
    "free property written",
    "item group applied",
    "new recipe: station, result, ingredients, skills",
    "the player got the plan",
    "the skill requirement holds",
    "the recipe crafts the new item",
    "changed recipe skill",
    "new NPC template numbers",
    "new monster numbers",
    "changed monster numbers",
    "placed NPC made",
    "placed NPC template, guild, label",
    "placed NPC inventory",
    "placed NPC equipped his knife",
    "placed NPC routine post",
}

local npc_ready = {}
risen.on("modkit.npc_ready", function(id, name, reason) if id == "modkit_showcase" then npc_ready[name] = reason end end)

local applied = {}   -- when -> report
risen.on("modkit.applied", function(id, when, report)
    if id == "modkit_showcase" then applied[when] = report end
end)

local function prop(name, set, key)   -- get_property returns the value as text
    local t = risen.template.get(name)
    return t and tonumber(t:get_property(set, key))
end

T.run(function()
    T.wait_event("session.start", 240, function(mode) return mode == "new" end)
    T.wait_until(function() return applied["session.start (new)"] ~= nil end, 30, "modkit.applied on session.start")
    local rep = applied["session.start (new)"]
    T.expect("the export was applied without failures", rep.ok and #(rep.failed or {}) == 0, table.concat(rep.failed or {}, "; "))

    T.expect("new item exists", risen.template.exists("MK_Hob_Knife"))
    T.eq("new item numbers", { prop("MK_Hob_Knife", "gCItem_PS", "GoldValue"), prop("MK_Hob_Knife", "gCDamage_PS", "DamageAmount") }, { 250, 20 })
    local req = risen.item.requirements("MK_Hob_Knife") or {}
    T.eq("new item requirement", req.Strength, 12)
    T.eq("new item label", risen.loc.get("FO_MK_Hob_Knife"), "Hob's Knife")
    local player = assert(risen.entity.player(), "no player")
    risen.item.give(player, "MK_Hob_Knife", 1)
    T.eq("new item can be given", risen.item.count(player, "MK_Hob_Knife"), 1)
    local hp
    for _, m in ipairs(risen.item.modifiers("MK_Strong_Potion") or {}) do
        if m.skill == "HitPoints" then hp = m.amount end
    end
    T.eq("new potion modifier", hp, 120)

    T.eq("changed item numbers", { prop("It_1H_Sword_Rusty", "gCItem_PS", "GoldValue"), prop("It_1H_Sword_Rusty", "gCDamage_PS", "DamageAmount") }, { 7, 9 })
    T.eq("changed item label", risen.loc.get("FO_It_1H_Sword_Rusty"), "Very rusty sword")
    T.eq("free property written", { prop("It_Apple", "gCItem_PS", "SortValue"), prop("It_Apple", "gCItem_PS", "GoldValue") }, { 77, 3 })

    local group
    for _, label in ipairs(rep.applied or {}) do
        if tostring(label):find("Items/Plants", 1, true) then group = label end
    end
    T.expect("item group applied", group ~= nil, table.concat(rep.applied or {}, " | "))

    -- recipes
    local info = risen.recipe.info("MK_Recipe_Hob_Knife") or {}
    local ingr = {}
    for _, i in ipairs(info.ingredients or {}) do ingr[#ingr + 1] = i.item .. "=" .. i.count end
    T.eq("new recipe: station, result, ingredients, skills", { info.station, info.result and info.result.item, table.concat(ingr, ","), (info.skills or {}).Smith },
         { "anvil", "MK_Hob_Knife", "It_1H_Machete=1,It_IronOre=2", 1 })
    T.wait_until(function() return risen.recipe.known(player, "MK_Recipe_Hob_Knife") end, 20, "the plan in the inventory")
    T.expect("the player got the plan", risen.recipe.known(player, "MK_Recipe_Hob_Knife"))
    risen.item.give(player, "It_1H_Machete", 1)
    risen.item.give(player, "It_IronOre", 2)
    local times, missing = risen.recipe.can_craft(player, "MK_Recipe_Hob_Knife")
    T.expect("the skill requirement holds", times == 0 and tostring(missing):find("Smith") ~= nil, tostring(times) .. " " .. tostring(missing))
    local before = risen.item.count(player, "MK_Hob_Knife")
    risen.recipe.craft_raw(player, "MK_Recipe_Hob_Knife", 1)   -- what a station does once the check passed
    T.eq("the recipe crafts the new item", { risen.item.count(player, "MK_Hob_Knife") - before, risen.item.count(player, "It_IronOre") }, { 1, 0 })
    T.eq("changed recipe skill", ((risen.recipe.info("It_1H_Steel_Hot") or {}).skills or {}).Smith, 1)

    -- NPC templates and a placed NPC
    local hob_t = risen.npc.stats("MK_Hob_T") or {}
    T.eq("new NPC template numbers", { hob_t.hp_max, hob_t.level, hob_t.guild }, { 300, 5, "Cit" })
    local dire = risen.npc.stats("MK_Dire_Wolf") or {}
    T.eq("new monster numbers", { dire.hp_max, dire.level, dire.species }, { 500, 10, "Wolf" })
    local boar = risen.npc.stats("Boar") or {}
    T.eq("changed monster numbers", { boar.hp_max, (boar.skills or {}).Strength }, { 222, 33 })
    T.wait_until(function() return npc_ready.MK_Hob ~= nil end, 30, "MK_Hob ready")
    local hob = risen.entity.get("MK_Hob")
    T.expect("placed NPC made", hob ~= nil and npc_ready.MK_Hob == "spawned", tostring(npc_ready.MK_Hob))
    if hob then
        local st = risen.npc.stats(hob) or {}
        T.eq("placed NPC template, guild, label", { st.template, st.guild, risen.loc.get("FO_MK_Hob") }, { "MK_Hob_T", "Cit", "Hob" })
        T.eq("placed NPC inventory", { risen.item.count(hob, "MK_Hob_Knife"), risen.item.count(hob, "It_Apple") >= 3 }, { 1, true })
        T.sleep(30)
        -- item events of NPCs are dropped until the world is ready (the NPC is made before), so look into his equip slots
        local slot
        for s = 0, 20 do
            if risen.spell.equipped(hob, s) == "MK_Hob_Knife" then slot = s end
        end
        T.expect("placed NPC equipped his knife", slot ~= nil, "slot " .. tostring(slot))
        -- the post: a guard freepoint about 100 units from his home (`created` counts the points of the calling mod only)
        local r = risen.npc.get_routine(hob) or {}
        local d = r.work and r.work:distance_to(hob)
        T.expect("placed NPC routine post", r.work ~= nil and d ~= nil and d < 400, ("work=%s distance=%s"):format(tostring(r.work), tostring(d)))
    end
    T.finish()
end, "modkit")
