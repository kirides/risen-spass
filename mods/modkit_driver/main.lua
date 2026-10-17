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
    "dialogue options exist",
    "turn-in option has engine conditions",
    "offered options before the quest",
    "the work option starts the quest",
    "the quest log has the entry",
    "the turn-in is offered with the ore",
    "the turn-in takes the ore and succeeds the quest",
    "the parent option opens its sub-options",
    "saved",
    "a quest's success starts the follow-up quest",
    "the follow-up's log entries",
    "the first kill does not finish it",
    "the second kill pays the reward and succeeds",
    "a load rolls the quest back to one kill",
    "after the load the kill pays once more",
    "the game-start quest runs",
    "carrying three potions finishes it and sets the variable",
    "an item is spawned where it was placed",
    "a chest of the world got the items",
    "an NPC stands at his world position",
    "after the load: nothing spawned or filled twice",
}

local spawned, filled = {}, {}
risen.on("modkit.world_spawned", function(id, key, e) if id == "modkit_showcase" then spawned[#spawned + 1] = e end end)
risen.on("modkit.container_filled", function(id, key, c) if id == "modkit_showcase" then filled[#filled + 1] = key end end)
local CHEST = "2c2c2522-cdad-4dba-aba1-12a5e2054790"
local KNIFE_AT = { -31950, 620, -4650 }
local function near(e, p, r)
    local x, y, z = e:position()
    return x ~= nil and math.abs(x - p[1]) < r and math.abs(z - p[3]) < r
end
local function knives_near()
    local t = risen.template.get("MK_Hob_Knife")
    local n = 0
    for _, e in ipairs(t and t:instances(1000) or {}) do
        if near(e, KNIFE_AT, 400) then n = n + 1 end
    end
    return n
end

local npc_ready = {}

risen.on("modkit.npc_ready", function(id, name, reason) if id == "modkit_showcase" then npc_ready[name] = reason end end)

local applied = {}   -- when -> report
risen.on("modkit.applied", function(id, when, report)
    if id == "modkit_showcase" then applied[when] = report end
end)

local function save_game()
    T.wait_until(function() return risen.world.is_save_allowed() end, 30, "saving allowed")
    local saved = false
    local h = risen.on("save.after", function(_, ok) saved = ok end)
    risen.world.quick_save()
    T.wait_until(function() return saved end, 60, "savegame written")
    risen.off(h)
    T.sleep(200)   -- the engine keeps writing after save.after
    return saved
end

local function qstatus(name)
    local q = risen.quest.get(name)
    return tostring(q and q:get_property("Status"))
end

local function prop(name, set, key)   -- get_property returns the value as text
    local t = risen.template.get(name)
    return t and tonumber(t:get_property(set, key))
end

T.run(function()
    T.wait_event("session.start", 240, function(mode) return mode == "new" end)
    T.wait_until(function() return applied["session.start (new)"] ~= nil end, 30, "modkit.applied on session.start")
    local rep = applied["session.start (new)"]
    T.expect("the export was applied without failures", rep.ok and #(rep.failed or {}) == 0, table.concat(rep.failed or {}, "; "))

    -- world edits (on world.ready of the new game)
    T.wait_until(function() return #spawned > 0 and #filled > 0 end, 30, "world edits applied")
    local knife = spawned[1]
    local kx, ky, kz = 0, 0, 0
    if knife then kx, ky, kz = knife:position() end
    T.expect("an item is spawned where it was placed", knife ~= nil and near(knife, KNIFE_AT, 300) and knives_near() == 1,
             ("%s at %.0f %.0f %.0f, %d near"):format(tostring(knife and knife:template() and knife:template():name()), kx or 0, ky or 0, kz or 0, knives_near()))
    local chest = risen.entity.by_id(CHEST)
    T.eq("a chest of the world got the items", chest and { risen.item.count(chest, "MK_Strong_Potion"), risen.item.count(chest, "It_Gold") >= 77 }, { 2, true })
    T.wait_until(function() return risen.entity.get("MK_Watcher") ~= nil end, 30, "MK_Watcher made")
    local watcher = risen.entity.get("MK_Watcher")
    T.expect("an NPC stands at his world position", watcher ~= nil and near(watcher, { -31900, 600, -4800 }, 300), watcher and table.concat({ watcher:position() }, " ") or "missing")

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

        -- dialogues: talk like a player (open the conversation, choose an option, wait until it played)
        local names = { "MK_Hob_Who", "MK_Hob_Work", "MK_Hob_Ore", "MK_Hob_Island", "MK_Hob_Pirates", "MK_Hob_Never" }
        local missing = {}
        for _, n in ipairs(names) do if not risen.dialog.get(n) then missing[#missing + 1] = n end end
        T.expect("dialogue options exist", #missing == 0, table.concat(missing, ", "))
        -- quest state and item amount are conditions the engine evaluates itself (no Lua condition behind them)
        local ore = risen.dialog.get("MK_Hob_Ore")
        T.eq("turn-in option has engine conditions", ore and ore:condition_count(), 2)
        local function offered()   -- this mod's options only (the game adds its generic ones, PANKRATZX2_...)
            local t = {}
            for _, o in ipairs(risen.dialog.available()) do
                local n = tostring(o:name())
                if n:find("^MK_") then t[#t + 1] = n end
            end
            table.sort(t)
            return table.concat(t, ",")
        end
        local function open_talk()
            risen.dialog.start(player, hob)
            T.sleep(90)
        end
        local function choose(info)
            assert(risen.dialog.get(info), info):run()
            T.wait_event("info.finished", 60, function(name) return name == info end)
            T.sleep(60)
        end
        risen.item.take(player, "It_IronOre", 99)   -- the crafting check above left none, but be sure
        open_talk()
        T.eq("offered options before the quest", offered(), "MK_Hob_Island,MK_Hob_Who,MK_Hob_Work")
        choose("MK_Hob_Work")
        T.expect("the work option starts the quest", risen.quest.is_running("Q_MK_Ore"))
        local q = risen.quest.get("Q_MK_Ore")
        T.expect("the quest log has the entry", q ~= nil and q:log_count() >= 1, tostring(q and q:log_count()))
        risen.item.give(player, "It_IronOre", 2)
        open_talk()
        local now = offered()
        T.expect("the turn-in is offered with the ore", now:find("MK_Hob_Ore", 1, true) ~= nil, now)
        local hob_ore = risen.item.count(hob, "It_IronOre")
        choose("MK_Hob_Ore")
        local status = q and q:get_property("Status") or "?"
        T.eq("the turn-in takes the ore and succeeds the quest",
             { risen.item.count(player, "It_IronOre"), risen.item.count(hob, "It_IronOre") - hob_ore, tostring(status):find("Success") ~= nil },
             { 0, 2, true })
        open_talk()
        assert(risen.dialog.get("MK_Hob_Island")):run()
        T.wait_event("info.finished", 60, function(name) return name == "MK_Hob_Island" end)
        T.sleep(60)
        T.eq("the parent option opens its sub-options", offered(), "MK_Hob_Never,MK_Hob_Pirates")
        choose("MK_Hob_Never")

        -- quest flows: Q_MK_Wolves starts when Q_MK_Ore succeeded (above)
        T.wait_until(function() return risen.quest.is_running("Q_MK_Wolves") end, 10, "Q_MK_Wolves running")
        T.expect("a quest's success starts the follow-up quest", risen.quest.is_running("Q_MK_Wolves"))
        local wq = risen.quest.get("Q_MK_Wolves")
        T.expect("the follow-up's log entries", wq ~= nil and wq:log_count() >= 2, tostring(wq and wq:log_count()))
        local function kill_wolf()
            local p = assert(risen.entity.player())
            local x, y, z = p:position()
            local w = assert(risen.npc.spawn("MK_Dire_Wolf", x + 600, y + 20, z + 600, { name = "MK_Test_Wolf_" .. tostring(math.floor(T.elapsed() * 1000)) }))
            T.sleep(5)
            risen.script.call("Kill", p, w, 0)   -- the game's own death (Entity:kill() removes without one); a script cannot make the player fight
            T.sleep(30)
        end
        local function wallet()
            local p = assert(risen.entity.player())
            local st = risen.npc.stats(p) or {}
            return risen.item.money(p), risen.item.count(p, "MK_Strong_Potion"), st.experience or 0
        end
        kill_wolf()
        T.expect("the first kill does not finish it", risen.quest.is_running("Q_MK_Wolves"))
        local gold0, pot0, xp0 = wallet()
        T.expect("saved", save_game())
        kill_wolf()
        local gold1, pot1, xp1 = wallet()
        T.eq("the second kill pays the reward and succeeds", { qstatus("Q_MK_Wolves"):find("Success") ~= nil, gold1 - gold0, pot1 - pot0, xp1 - xp0 >= 150 },
             { true, 50, 1, true })
        risen.world.quick_load()
        T.wait_event("session.start", 120, function(m) return m == "load" end)
        T.sleep(120)
        local g2 = wallet()
        T.eq("a load rolls the quest back to one kill", { risen.quest.is_running("Q_MK_Wolves"), g2 }, { true, gold0 })
        kill_wolf()
        local g3, p3 = wallet()
        T.eq("after the load the kill pays once more", { qstatus("Q_MK_Wolves"):find("Success") ~= nil, g3 - gold0, p3 - pot0 }, { true, 50, 1 })

        local chest2 = risen.entity.by_id(CHEST)
        T.eq("after the load: nothing spawned or filled twice", { knives_near(), chest2 and risen.item.count(chest2, "MK_Strong_Potion") }, { 1, 2 })

        -- Q_MK_Potions started with the new game: done while the player carries three
        T.expect("the game-start quest runs", risen.quest.is_running("Q_MK_Potions"))
        local p = assert(risen.entity.player())
        risen.item.give(p, "MK_Strong_Potion", 3 - risen.item.count(p, "MK_Strong_Potion"))
        T.sleep(30)
        local pq = risen.quest.get("Q_MK_Potions")
        T.eq("carrying three potions finishes it and sets the variable", { tostring(pq and pq:get_property("Status")):find("Success") ~= nil, risen.gamevar.get("MK_Potions_Done") },
             { true, 1 })
    end
    T.finish()
end, "modkit")
