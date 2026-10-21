---@diagnostic disable: need-check-nil, param-type-mismatch, undefined-field
-- Plays the exported chronicle example (mods/chronicle_example, exported from examples/modkit/chronicle.rmk.json, docs/editor/CHRONICLE.md) chapter by chapter
-- like a player would (dialogues, quests, kills, the chapter, movement), in one game process, with a quick save and a quick load in the middle:
--   .\scripts\test-run.ps1 -Instance 4 -Mods chronicle_example,chronicle_driver -WaitForResult -Seconds 700 -Scenario chronicle
local T = risen.test

local MOD = "chronicle_example"
local CIRCLE = { -31956, 639, -3757 }
local CAMP = { -35080, 420, -6646 }
local MAREN_AT = { -30050, 700, -6350 }
local CHEST = "2c2c2522-cdad-4dba-aba1-12a5e2054790"
local TORCH = "01c4332e-efae-459f-9098-ae6c71dbd23d"
local STONE = "3dbe0473-6c27-4a13-a0c2-86fdafabff88"
local EDGAR = "f9e70034-b8fa-40b2-b8b7-6dca9d95bfb9"   -- the harbour guard of Harbour Town (a placed NPC of the world)

T.plan {
    -- chapter 1
    "c1: the export was applied without failures",
    "c1: the new-game event set its variable",
    "c1: Maren stands on the beach (template, label, guild)",
    "c1: only the layer that starts on is made",
    "c1: new items: numbers, requirements, modifiers, texts",
    "c1: changed items and item groups",
    "c1: the Tide Draught's modifier list keeps its order and mode",
    "c1: the Lua file's before_install set the amulet's value",
    "c1: a template patch: the camp fire glows wider",
    "c1: the free string rows are served",
    "c1: Sara's dialogues: an XP reward added, one changed",
    "c1: Sara gives two potions (property edit of a shipped info)",
    "c1: Cormac's chapter 1 stock gets the Tide Draught (trader refill)",
    "c1: Edgar teaches Strength only up to 130",
    "c1: the mod's icons are in the atlas",
    "c1: the new spell: items, mana, numbers",
    "c1: changed shipped spells",
    "c1: the chest of the world holds the spell",
    "c1: new recipes: station, result, ingredients, skills",
    "c1: the alchemy plan is known from the start, the smithing plan is not",
    "c1: changed shipped recipe",
    "c1: new NPC templates: numbers",
    "c1: changed shipped templates and the NPC groups",
    "c1: world edits: spawns, removal, move",
    "c1: the imported 3D model is the tablet's mesh",
    "c1: the tablet is picked up: the event fires once",
    "c1: the behaviour of Maren starts with its params",
    "c1: dialogue options exist",
    "c1: Maren offers who / work / lore / farewell, nothing of the later chapters",
    "c1: the voiced line plays with its sound",
    "c1: the work option starts the pearl quest",
    "c1: the turn-in is offered with three pearls (engine conditions)",
    "c1: the turn-in takes the pearls, succeeds the quest, pays",
    "c1: the plan from the reward is known",
    "c1: the follow-up quest starts and the quest event gives a gift",
    "c1: the Run script node writes page 1 with the Lua file's script",
    "c1: the lore option opens its sub-options, the tide one is hidden",
    "c1: the behaviour saw the player and the conversations",
    "c1: the scout is not there yet",
    "c1: the first wolf starts the bounty quest, two more finish it",
    "c1: the pearl event is fed by the pearls",
    -- chapter 2
    "c2: the chapter event switches the camp layer on",
    "c2: the camp stands where it was placed",
    "c2: the camp quest and the variables",
    "c2: a stock story sector was switched on by the graph",
    "c2: the camp's objects were made, the chest was filled once",
    "c2: the harbour guard Edgar is armed with the camp (equipped item of a container fill)",
    "c2: the scout appears while the camp quest runs",
    "c2: the boss behaviour taunts when the player comes near",
    "c2: the tide option appears with the variable",
    "c2: the tithe needs 200 gold",
    "c2: saved",
    "c2: the tithe is offered with 200 gold",
    "c2: paying the tithe ends the quest on the second path",
    "c2: a load rolls everything back, nothing is made twice",
    "c2: killing the warlord ends the quest on the first path",
    "c2: the kill event ran once",
    "c2: the warlord's fall writes page 2",
    "c2: the warlord's loot",
    "c2: the beacon quest starts after the camp",
    "c2: the scout leaves when the camp quest is done",
    -- chapter 3
    "c3: the chapter event sets its variable and a stock sector",
    "c3: the smugglers layer switched itself on",
    "c3: changed stats of the chapter's monsters",
    "c3: the trader has his stock",
    "c3: the smuggler offers trade and the chronicle option",
    "c3: the smuggler's dialogue gives a scroll",
    "c3: the dialogue switches a stock sector",
    "c3: walking fires the movement event, the counter lets one through",
    "c3: the beacon ambush: the area trigger spawns raiders",
    "c3: the ambush does not start twice",
    "c3: killing the raiders finishes the beacon quest",
    "c3: the kill counter and the signal chain ran",
    "c3: the camp layer switched itself off",
    "c3: the dusk bell rings at 20:00",
    "c3: saved",
    "c3: after the load: layers and ambush are not made twice",
    -- chapter 4
    "c4: the key is given and starts the finale quest",
    "c4: the tide circle layer is made",
    "c4: the monastery sector is on and its lizards are there",
    "c4: the clock was set",
    "c4: killing the lord pays the finale quest",
    "c4: the kill event set the finale variable",
    "c4: the epilogue quest and the main quest end, the amulet is the reward",
    "c4: the Lua file writes the last line when the main quest ends (modkit.quest_done)",
    "c4: Maren answers the finale question with the script",
    "c4: the epilogue teleports, sets the clock and takes the key",
    "c4: the circle layer switched itself off",
    "c4: every graph ran and every quest ended",
    "c4: the after_install script counts the quests that ended well",
}

-- ---- bookkeeping ------------------------------------------------------------------------------------------------------------------------
local applied = {}
risen.on("modkit.applied", function(id, when, report) if id == MOD then applied[when] = report end end)
local npc_ready = {}
risen.on("modkit.npc_ready", function(id, name, reason) if id == MOD then npc_ready[name] = reason end end)
local spawned, filled = {}, {}
risen.on("modkit.world_spawned", function(id, key, e) if id == MOD then spawned[#spawned + 1] = e end end)
risen.on("modkit.container_filled", function(id, key, c) if id == MOD then filled[#filled + 1] = key end end)
local started, qdone = {}, {}
risen.on("modkit.quest_started", function(id, q) if id == MOD then started[q] = (started[q] or 0) + 1 end end)
risen.on("modkit.quest_done", function(id, q, result) if id == MOD then qdone[q] = result end end)
local ran = {}      -- "graph" -> trigger runs;  "graph:node type" -> executions
risen.on("modkit.event", function(id, graph, node, kind) if id == MOD then ran[graph] = (ran[graph] or 0) + 1 end end)
risen.on("modkit.event_node", function(id, graph, node, ntype) if id == MOD then ran[graph .. ":" .. ntype] = (ran[graph .. ":" .. ntype] or 0) + 1 end end)
local bhv = {}
risen.on("chronicle.behavior", function(kind, name, a, b) bhv[#bhv + 1] = { kind = kind, name = name, a = a, b = b } end)
local bells = {}
risen.on("chronicle.bell", function(arg) bells[#bells + 1] = arg end)
local equipped = {}   -- "<owner name>:<template>" -> slot
risen.on("item.equipped", function(owner, template, slot)
    local ok, n = pcall(function() return owner and owner:name() end)
    if ok and n then equipped[n .. ":" .. tostring(template)] = slot end
end)
local function bhv_find(kind, name, pred)
    for _, e in ipairs(bhv) do
        if e.kind == kind and e.name == name and (not pred or pred(e)) then return e end
    end
    return nil
end

risen.on("entity.died", function(e, killer, cause)
    local ok, line = pcall(function() return ("chronicle: died %s (%s) by %s cause %s"):format(tostring(e:name()), tostring(e:template() and e:template():name()), tostring(killer and killer:name()), tostring(cause)) end)
    risen.log.info(ok and line or "chronicle: died ?")
end)
risen.on("entity.damaged", function(e, attacker, dmg)
    local ok, line = pcall(function() return e:name() == "MKC_Maren" and ("chronicle: Maren hit by %s for %s"):format(tostring(attacker and attacker:name()), tostring(dmg)) or nil end)
    if ok and line then risen.log.info(line) end
end)
local function player() return assert(risen.entity.player(), "no player") end
local function named(n) local e = risen.entity.get(n) return e end
local function pos_str(e) local x, y, z = e:position() return ("%.0f %.0f %.0f"):format(x or 0, y or 0, z or 0) end
local function near(e, p, r)
    if not e then return false end
    local x, y, z = e:position()
    return x ~= nil and math.abs(x - p[1]) < r and math.abs(z - p[3]) < r
end
local function prop(name, set, key)
    local t = risen.template.get(name)
    return t and tonumber(t:get_property(set, key))
end
local function qstatus(name)
    local q = risen.quest.get(name)
    return tostring(q and q:get_property("Status"))
end
local function qdone_ok(name) return qstatus(name):find("Success", 1, true) ~= nil end
local function wait(pred, secs, what) return pcall(T.wait_until, pred, secs, what) end
local function boost()
    local p = player()
    p:set_hp_max(200000)
    p:set_hp(200000)
end
local function tp(x, y, z)
    local p = player()
    p:set_pose({ x = x, y = y, z = z })
    T.sleep(30)
end
local function wallet()
    local p = player()
    local st = risen.npc.stats(p) or {}
    return risen.item.money(p), st.experience or 0
end
local function save_game()
    T.wait_until(function() return risen.world.is_save_allowed() end, 30, "saving allowed")
    local saved = false
    local h = risen.on("save.after", function(_, ok) saved = ok end)
    risen.world.quick_save()
    T.wait_until(function() return saved end, 60, "savegame written")
    risen.off(h)
    T.sleep(200)
    return saved
end
local function offered()   -- this mod's options only
    local t = {}
    for _, o in ipairs(risen.dialog.available()) do
        local n = tostring(o:name())
        if n:find("^MKC_") then t[#t + 1] = n end
    end
    table.sort(t)
    return table.concat(t, ",")
end
local function talk(npc)
    risen.dialog.start(player(), npc)
    T.sleep(90)
end
local function choose(info)
    assert(risen.dialog.get(info), info):run()
    T.wait_event("info.finished", 60, function(name) return name == info end)
    T.sleep(60)
end
local function kill(e)
    -- the game's own death, the player is the killer; an NPC in the middle of a fight needs a few tries
    local id = e:id()
    for _ = 1, 12 do
        local v = id and risen.entity.by_id(id) or e
        if v:is_dead() then break end
        risen.script.call("Kill", player(), v, 0)
        T.sleep(25)
    end
    T.sleep(10)
end
local function count_template(tpl, center, radius)
    local c = 0
    for _, e in ipairs(center and risen.entity.npcs(center, radius) or risen.entity.npcs()) do
        local t = e:template()
        if t and t:name() == tpl and not e:is_dead() then c = c + 1 end
    end
    return c
end
local function spawn_near_player(tpl, name)
    local x, y, z = player():position()
    return assert(risen.npc.spawn(tpl, x + 500, y + 20, z + 500, { name = name }))
end

T.run(function()
    T.wait_event("session.start", 240, function(mode) return mode == "new" end)
    T.wait_until(function() return applied["session.start (new)"] ~= nil end, 60, "modkit.applied on session.start")
    T.sleep(60)
    boost()
    local rep = applied["session.start (new)"]

    -- ======================================================================================================================================
    T.section("chapter 1")
    T.expect("c1: the export was applied without failures", rep.ok and #(rep.failed or {}) == 0, table.concat(rep.failed or {}, "; "))
    T.wait_until(function() return risen.gamevar.get("mkc_ch1") == 1 end, 30, "mkc_ch1")
    T.eq("c1: the new-game event set its variable", { risen.gamevar.get("mkc_ch1"), ran["Chronicle begins"], ran["Chronicle begins:set_variable"] }, { 1, 1, 1 })
    T.wait_until(function() return named("MKC_Maren") ~= nil end, 40, "Maren made")
    local maren = named("MKC_Maren")
    local ms = risen.npc.stats(maren) or {}
    T.eq("c1: Maren stands on the beach (template, label, guild)", { ms.template, ms.guild, risen.loc.get("FO_MKC_Maren"), near(maren, MAREN_AT, 400) },
         { "MKC_Maren_T", "Cit", "Maren", true })
    T.expect("c1: only the layer that starts on is made",
             named("MKC_Ash_Warlord") == nil and named("MKC_Ash_Raider_1") == nil and named("MKC_Smuggler") == nil and named("MKC_Tide_Lord") == nil and named("MKC_Scout") == nil,
             ("warlord %s smuggler %s lord %s scout %s"):format(tostring(named("MKC_Ash_Warlord")), tostring(named("MKC_Smuggler")), tostring(named("MKC_Tide_Lord")), tostring(named("MKC_Scout"))))

    -- items
    local req = risen.item.requirements("MKC_Chronicle_Blade") or {}
    local function modif(item, skill)
        for _, m in ipairs(risen.item.modifiers(item) or {}) do
            if m.skill == skill then return m.amount end
        end
    end
    T.eq("c1: new items: numbers, requirements, modifiers, texts",
         { prop("MKC_Chronicle_Blade", "gCItem_PS", "GoldValue"), prop("MKC_Chronicle_Blade", "gCDamage_PS", "DamageAmount"), req.Strength, modif("MKC_Tide_Potion", "HitPoints"),
           modif("MKC_Tide_Amulet", "HitPoints"), prop("MKC_Warlord_Axe", "gCDamage_PS", "DamageAmount"), risen.loc.get("FO_MKC_Chronicle_Blade"), risen.template.exists("MKC_Tide_Key") },
         { 420, 36, 20, 160, 150, 52, "Chronicler's Blade", true })
    local labels = {}
    for _, label in ipairs(rep.applied or {}) do labels[#labels + 1] = tostring(label) end
    local all_labels = table.concat(labels, " | ")
    T.expect("c1: changed items and item groups",
             prop("It_1H_Sword_Rusty", "gCItem_PS", "GoldValue") == 9 and prop("It_1H_Sword_Rusty", "gCDamage_PS", "DamageAmount") == 11 and prop("It_Apple", "gCItem_PS", "GoldValue") == 2
                 and all_labels:find("Items/Potions", 1, true) ~= nil and all_labels:find("Items/Weapons_Axes", 1, true) ~= nil and risen.loc.get("FO_It_1H_Sword_Rusty") == "Salt-eaten sword",
             all_labels)
    -- the features of the editor since the first version of the example (EXT-1..5, LUA-1..3)
    local tide_mods = {}
    for _, m in ipairs(risen.item.modifiers("MKC_Tide_Potion") or {}) do tide_mods[#tide_mods + 1] = ("%s:%d:%s"):format(m.skill, m.amount, tostring(m.mode)) end
    T.eq("c1: the Tide Draught's modifier list keeps its order and mode", table.concat(tide_mods, ","), "HitPoints:160:add,ManaPoints:25:percent")
    -- the project says 1500; lua/pages.lua's before_install makes it 250 per quest of the Chronicle (8 quests)
    T.eq("c1: the Lua file's before_install set the amulet's value", prop("MKC_Tide_Amulet", "gCItem_PS", "GoldValue"), 2000)
    T.eq("c1: a template patch: the camp fire glows wider", prop("Def_CampFire", "eCStaticAmbientLight_PS", "Range"), 900)
    T.eq("c1: the free string rows are served", { risen.loc.get("MKC_PAGE_WRITTEN"), tostring(risen.loc.get("MKC_LAST_LINE")):find("^Maren writes the last line") ~= nil },
         { "Maren writes page {page} of the Chronicle.", true })
    local function last_command_with(inf, name, value)
        for i = inf:command_count(), 1, -1 do
            if inf:get_command_property(i, name) == value then return i end
        end
    end
    local sara_first, sara_storm = risen.dialog.get("HOGEX2_00006"), risen.dialog.get("PANKRATZX2_01756")
    local n_first = sara_first and sara_first:command_count() or 0
    T.eq("c1: Sara's dialogues: an XP reward added, one changed",
         { sara_first and sara_first:get_command_property(n_first, "Amount"), sara_storm and last_command_with(sara_storm, "Amount", "120") ~= nil,
           sara_storm and last_command_with(sara_storm, "Amount", "50") == nil },
         { "40", true, true })
    local sara_back = risen.dialog.get("PANKRATZX2_01755")
    T.eq("c1: Sara gives two potions (property edit of a shipped info)", sara_back and { sara_back:get_command_property(10, "Amount"), sara_back:get_command_property(10, "Entity1") },
         { "2", "Sara" })
    local stock = risen.dialog.get("FILLERX2_00976")
    local ri = stock and last_command_with(stock, "Other", "MKC_Tide_Potion")
    local cormac = named("Cormac")
    local traded = 0
    for i = 0, (cormac and cormac:stack_count() or 0) - 1 do
        local s = cormac:stack_item(i)
        if s and s:name() == "MKC_Tide_Potion" then traded = traded + 1 end   -- a trade stack (risen.item.count does not count those)
    end
    T.expect("c1: Cormac's chapter 1 stock gets the Tide Draught (trader refill)", ri ~= nil and stock:get_command_property(ri, "Self") == "Cormac" and stock:get_command_property(ri, "Param") == "3" and traded >= 1,
             ("refill command %s; Cormac %s has %d Tide Draught stack(s)"):format(tostring(ri), tostring(cormac), traded))
    local edgar = risen.entity.by_id(EDGAR)
    local limit
    for _, e in ipairs(edgar and risen.npc.teach_skills(edgar) or {}) do
        if e.skill == "gESkill_Stat_STR" then limit = e.max end
    end
    T.eq("c1: Edgar teaches Strength only up to 130", limit, 130)
    local icon = "IC_chronicle_example_chronicle_blade"
    local bm = risen.assets.bitmap(icon)
    local blade_t = risen.template.get("MKC_Chronicle_Blade")
    T.expect("c1: the mod's icons are in the atlas", bm ~= nil and bm.attached and tostring(blade_t:get_property("gCItem_PS", "IconImage")):lower() == icon:lower(),
             ("bitmap %s icon %s"):format(tostring(bm and bm.attached), tostring(blade_t and blade_t:get_property("gCItem_PS", "IconImage"))))

    -- spells
    local ember_req = risen.item.requirements("MKC_Ember_Whisper") or {}
    local fire = risen.spell.get("Fireball") or {}
    local crystal_cost, scroll_cost = prop("MKC_Ember_Whisper", "gCCastInfo_PS", "ManaCost"), prop("MKC_Ember_Whisper_Scroll", "gCCastInfo_PS", "ManaCost")
    T.expect("c1: the new spell: items, mana, numbers",
             risen.template.exists("MKC_Ember_Whisper") and risen.template.exists("MKC_Ember_Whisper_Scroll") and crystal_cost == 16 and ember_req.Fireball == 1 and scroll_cost ~= nil
                 and fire.projectiles ~= nil,
             ("crystal cost %s scroll cost %s requires Fireball %s"):format(tostring(crystal_cost), tostring(scroll_cost), tostring(ember_req.Fireball)))
    local light = risen.spell.get("Light") or {}
    local frost = risen.spell.get("Frost") or {}
    T.expect("c1: changed shipped spells", light.items and light.items.rune and light.items.rune.mana_cost == 7 and light.items.scroll and light.items.scroll.mana_cost == 3 and frost.projectiles ~= nil,
             ("light rune %s scroll %s"):format(tostring(light.items and light.items.rune and light.items.rune.mana_cost), tostring(light.items and light.items.scroll and light.items.scroll.mana_cost)))
    T.wait_until(function() return #filled >= 1 end, 30, "container filled")
    local chest = risen.entity.by_id(CHEST)
    T.eq("c1: the chest of the world holds the spell", chest and { risen.item.count(chest, "MKC_Ember_Whisper"), risen.item.count(chest, "MKC_Ember_Whisper_Scroll"), risen.item.count(chest, "It_Gold") >= 120 },
         { 1, 1, true })

    -- recipes
    local info = risen.recipe.info("MKC_Recipe_Chronicle_Blade") or {}
    local ingr = {}
    for _, i in ipairs(info.ingredients or {}) do ingr[#ingr + 1] = i.item .. "=" .. i.count end
    local pinfo = risen.recipe.info("MKC_Recipe_Tide_Potion") or {}
    T.eq("c1: new recipes: station, result, ingredients, skills",
         { info.station, info.result and info.result.item, table.concat(ingr, ","), (info.skills or {}).Smith, pinfo.station, pinfo.result and pinfo.result.item, (pinfo.skills or {}).Alchemy },
         { "anvil", "MKC_Chronicle_Blade", "It_1H_Steel_Sharp=1,It_IronOre=2,It_Pearl=1", 1, "alchemy", "MKC_Tide_Potion", 1 })
    local p1 = player()
    T.wait_until(function() return risen.recipe.known(p1, "MKC_Recipe_Tide_Potion") end, 30, "alchemy plan learned")
    T.eq("c1: the alchemy plan is known from the start, the smithing plan is not", { risen.recipe.known(p1, "MKC_Recipe_Tide_Potion"), risen.recipe.known(p1, "MKC_Recipe_Chronicle_Blade") },
         { true, false })
    T.eq("c1: changed shipped recipe", ((risen.recipe.info("It_1H_Steel_Hot") or {}).skills or {}).Smith, 1)

    -- NPC templates
    local sm, wd, rd, lord = risen.npc.stats("MKC_Smuggler_T") or {}, risen.npc.stats("MKC_Ash_Warlord_T") or {}, risen.npc.stats("MKC_Ash_Raider_T") or {}, risen.npc.stats("MKC_Tide_Lord_T") or {}
    T.eq("c1: new NPC templates: numbers", { ms.hp_max, ms.level, sm.hp_max, sm.level, wd.hp_max, wd.level, rd.hp_max, rd.level, lord.hp_max, lord.level, rd.species },
         { 220, 4, 260, 8, 1400, 20, 260, 11, 3200, 28, "Skeleton" })
    local wolf, wblack, lizard = risen.npc.stats("Wolf") or {}, risen.npc.stats("Wolf_Black") or {}, risen.npc.stats("LizardWarrior") or {}
    T.eq("c1: changed shipped templates and the NPC groups",
         { wolf.hp_max, wblack.hp_max, wblack.level, (risen.npc.stats("Wolf_Young_NoviceRanch_01") or {}).hp_max, lizard.hp_max, (lizard.skills or {}).Strength },
         { 216, 432, 14, 120, 780, 95 })

    -- world edits
    T.wait_until(function() return #spawned >= 2 end, 30, "world spawns")
    local tablet
    for _, e in ipairs(spawned) do
        if e:template() and e:template():name() == "MKC_Chronicle_Tablet" then tablet = e end
    end
    local sh = risen.entity.by_id(STONE)
    local sp_ = sh and sh:pose()
    T.wait_until(function() return risen.entity.by_id(TORCH) == nil end, 10, "torch removed")
    T.expect("c1: world edits: spawns, removal, move",
             tablet ~= nil and near(tablet, { -31880, 620, -4600 }, 300) and risen.entity.by_id(TORCH) == nil and sp_ ~= nil and math.abs(sp_.x + 31910) < 5 and math.abs(sp_.z + 3777) < 5,
             ("tablet %s torch %s stone %s"):format(tablet and pos_str(tablet) or "missing", tostring(risen.entity.by_id(TORCH)), sp_ and ("%.0f %.0f"):format(sp_.x, sp_.z) or "?"))
    local mt = risen.template.get("MKC_Chronicle_Tablet")
    local mesh_res = risen.assets.info("MKC_Tablet._xmsh")
    T.expect("c1: the imported 3D model is the tablet's mesh", tostring(mt and mt:get_property("eCMesh_PS", "MeshFileName")):lower() == "mkc_tablet.xmsh" and mesh_res ~= nil
        and tostring(mesh_res.source):find(MOD, 1, true) ~= nil, ("mesh %s source %s"):format(tostring(mt and mt:get_property("eCMesh_PS", "MeshFileName")), tostring(mesh_res and mesh_res.source)))
    -- pick the tablet up like the player; a second one must not pay again
    local xp0 = select(2, wallet())
    risen.item.pick_up(p1, tablet)
    T.sleep(60)
    local xp1 = select(2, wallet())
    local extra = risen.entity.spawn("MKC_Chronicle_Tablet", -31870, 640, -4580, true)
    if extra then risen.item.pick_up(p1, extra) end
    T.sleep(60)
    local xp2 = select(2, wallet())
    T.eq("c1: the tablet is picked up: the event fires once", { risen.item.count(p1, "MKC_Chronicle_Tablet"), xp1 - xp0 >= 60, xp2 - xp1, ran["Tablet found:give_xp"], ran["Tablet found:once"] },
         { 2, true, 0, 1, 2 })

    -- behaviour
    T.wait_until(function() return bhv_find("start", "MKC_Maren") ~= nil end, 30, "behaviour started")
    local bs = bhv_find("start", "MKC_Maren")
    T.expect("c1: the behaviour of Maren starts with its params", bs ~= nil and bs.a == false, "behaviour start " .. tostring(bs))

    -- Maren's dialogue
    local names = { "MKC_Maren_Who", "MKC_Maren_Work", "MKC_Maren_Ink_Turnin", "MKC_Maren_Lore", "MKC_Maren_Ash", "MKC_Maren_Tide", "MKC_Maren_Tithe", "MKC_Maren_Trail", "MKC_Maren_Done",
                    "MKC_Maren_Bye", "MKC_Scout_Hurt", "MKC_Scout_Gift", "MKC_Smuggler_Trade", "MKC_Smuggler_Chronicle" }
    local missing = {}
    for _, n in ipairs(names) do if not risen.dialog.get(n) then missing[#missing + 1] = n end end
    T.expect("c1: dialogue options exist", #missing == 0, table.concat(missing, ", "))
    -- she walks to her post: bring the player to her
    local mx, my, mz = maren:position()
    tp(mx - 150, my + 20, mz)
    maren = named("MKC_Maren")
    talk(maren)
    T.eq("c1: Maren offers who / work / lore / farewell, nothing of the later chapters", offered(), "MKC_Maren_Bye,MKC_Maren_Lore,MKC_Maren_Who,MKC_Maren_Work")
    choose("MKC_Maren_Who")
    local last = risen.voice.last()
    T.expect("c1: the voiced line plays with its sound", last ~= nil and tostring(last.key):find("MKC_Maren_Who", 1, true) ~= nil, last and tostring(last.key) or "no voice")
    T.wait_until(function() return offered() ~= "" end, 20, "the list after Who")
    T.eq("c1: Who returns to the start: the conversation goes on with the list", { risen.dialog.is_running(), offered() },
         { true, "MKC_Maren_Bye,MKC_Maren_Lore,MKC_Maren_Who,MKC_Maren_Work" })
    choose("MKC_Maren_Bye")
    T.eq("c1: Farewell ends the conversation", risen.dialog.is_running(), false)
    talk(maren)
    choose("MKC_Maren_Work")
    T.eq("c1: the work option starts the pearl quest", { risen.quest.is_running("Q_MKC_Ink"), (risen.quest.get("Q_MKC_Ink") or { log_count = function() return -1 end }):log_count() >= 1 }, { true, true })
    local pearls = risen.dialog.get("MKC_Maren_Ink_Turnin")
    risen.item.give(p1, "It_Pearl", 3)
    T.sleep(40)
    talk(maren)
    T.expect("c1: the turn-in is offered with three pearls (engine conditions)", offered():find("MKC_Maren_Ink_Turnin", 1, true) ~= nil and pearls ~= nil and pearls:condition_count() >= 2, offered())
    T.expect("c1: the pearl event is fed by the pearls", (ran["Pearls in the pocket"] or 0) >= 1 and (ran["Pearls in the pocket:cooldown"] or 0) >= 1,
             ("runs %s cooldown %s"):format(tostring(ran["Pearls in the pocket"]), tostring(ran["Pearls in the pocket:cooldown"])))
    local g0, x0 = wallet()
    local maren_pearls = risen.item.count(maren, "It_Pearl")
    choose("MKC_Maren_Ink_Turnin")
    T.wait_until(function() return qdone_ok("Q_MKC_Ink") end, 20, "pearl quest done")
    local g1, x1 = wallet()
    T.eq("c1: the turn-in takes the pearls, succeeds the quest, pays", { risen.item.count(p1, "It_Pearl"), risen.item.count(maren, "It_Pearl") - maren_pearls, qdone_ok("Q_MKC_Ink"), g1 - g0 >= 40, x1 - x0 >= 120 },
         { 0, 3, true, true, true })
    T.expect("c1: the plan from the reward is known", risen.recipe.known(p1, "MKC_Recipe_Chronicle_Blade"))
    T.wait_until(function() return risen.quest.is_running("Q_MKC_Scout") and (ran["Ink delivered:give_item"] or 0) >= 1 end, 30, "follow-up quest")
    T.expect("c1: the follow-up quest starts and the quest event gives a gift", risen.quest.is_running("Q_MKC_Scout") and risen.item.count(p1, "It_Plant_Health_01") >= 2,
             ("scout quest %s plants %d"):format(qstatus("Q_MKC_Scout"), risen.item.count(p1, "It_Plant_Health_01")))
    wait(function() return (ran["Ink delivered:run_script"] or 0) >= 1 end, 20, "page 1")
    T.eq("c1: the Run script node writes page 1 with the Lua file's script", { ran["Ink delivered:run_script"], risen.gamevar.get("mkc_pages") }, { 1, 1 })
    talk(maren)
    assert(risen.dialog.get("MKC_Maren_Lore")):run()
    T.wait_event("info.finished", 60, function(name) return name == "MKC_Maren_Lore" end)
    T.sleep(60)
    T.eq("c1: the lore option opens its sub-options, the tide one is hidden", offered(), "MKC_Maren_Ash,MKC_Maren_Lore_Back")
    choose("MKC_Maren_Ash")
    T.wait_until(function() return offered() ~= "" end, 20, "the sub-menu after Ash")
    T.eq("c1: Ash goes back to the choices: the lore sub-menu loops", { risen.dialog.is_running(), offered() }, { true, "MKC_Maren_Ash,MKC_Maren_Lore_Back" })
    choose("MKC_Maren_Lore_Back")
    T.wait_until(function() return offered():find("MKC_Maren_Bye", 1, true) ~= nil end, 20, "the list after Enough")
    local top = offered()
    T.expect("c1: Enough returns from the sub-menu to the start", risen.dialog.is_running() and top:find("MKC_Maren_Bye", 1, true) ~= nil and top:find("MKC_Maren_Ash", 1, true) == nil, top)
    choose("MKC_Maren_Bye")
    T.expect("c1: the behaviour saw the player and the conversations", bhv_find("near", "MKC_Maren") ~= nil and bhv_find("talk", "MKC_Maren") ~= nil,
             ("near %s talk %s"):format(tostring(bhv_find("near", "MKC_Maren")), tostring(bhv_find("talk", "MKC_Maren"))))
    T.expect("c1: the scout is not there yet", named("MKC_Scout") == nil)

    -- the wolf bounty: the first kill starts it, two more finish it
    local w1 = spawn_near_player("Wolf", "MKC_T_Wolf_1")
    T.sleep(5)
    kill(w1)
    T.wait_until(function() return risen.quest.is_running("Q_MKC_Wolves") end, 10, "bounty running")
    local w2, w3 = spawn_near_player("Wolf", "MKC_T_Wolf_2"), spawn_near_player("Wolf", "MKC_T_Wolf_3")
    T.sleep(5)
    kill(w2)
    local after_one = qstatus("Q_MKC_Wolves")
    kill(w3)
    T.wait_until(function() return qdone_ok("Q_MKC_Wolves") end, 10, "bounty done")
    T.eq("c1: the first wolf starts the bounty quest, two more finish it", { started.Q_MKC_Wolves, after_one:find("Running", 1, true) ~= nil, qdone_ok("Q_MKC_Wolves"), risen.item.count(p1, "MKC_Tide_Potion") },
         { 1, true, true, 1 })

    -- ======================================================================================================================================
    T.section("chapter 2")
    boost()
    risen.quest.set_chapter(2, true)
    T.wait_until(function() return named("MKC_Ash_Warlord") ~= nil and named("MKC_Ash_Raider_3") ~= nil end, 40, "camp made")
    T.sleep(60)
    T.expect("c2: the chapter event switches the camp layer on", (ran["Chapter 2: the hidden camp"] or 0) == 1 and named("MKC_Ash_Warlord") ~= nil and named("MKC_Ash_Raider_1") ~= nil
        and named("MKC_Ash_Raider_2") ~= nil, ("runs %s layer node %s"):format(tostring(ran["Chapter 2: the hidden camp"]), tostring(ran["Chapter 2: the hidden camp:layer"])))
    local warlord = named("MKC_Ash_Warlord")
    T.expect("c2: the camp stands where it was placed", near(warlord, CAMP, 600) and select(2, warlord:position()) > 100, warlord and pos_str(warlord) or "missing")
    T.eq("c2: the camp quest and the variables", { risen.quest.is_running("Q_MKC_Camp"), risen.gamevar.get("mkc_ch2"), (risen.quest.get("Q_MKC_Camp") or { log_count = function() return -1 end }):log_count() >= 2 }, { true, 1, true })
    T.expect("c2: a stock story sector was switched on by the graph", risen.world.sector_enabled("OutMain_Story_Chapter2_MonsterForFollowPatty_Off") == true)
    local fires = 0
    for _, e in ipairs(spawned) do
        if e:template() and e:template():name() == "Def_CampFire" and near(e, CAMP, 400) then fires = fires + 1 end
    end
    wait(function() return #filled >= 3 end, 20, "camp chest and Edgar filled")
    local chest2 = risen.entity.by_id("7bd3ae88-20f1-490c-8c86-f7e02d4f5787")
    -- three container fills by now: the beach chest (chapter 1), the camp chest and Edgar (both members of the AshCamp layer)
    T.eq("c2: the camp's objects were made, the chest was filled once", { fires, chest2 and risen.item.count(chest2, "MKC_Tide_Potion"), #filled }, { 1, 2, 3 })
    local edgar2 = risen.entity.by_id(EDGAR)
    T.eq("c2: the harbour guard Edgar is armed with the camp (equipped item of a container fill)",
         { edgar2 ~= nil, equipped["Edgar:MKC_Chronicle_Blade"] ~= nil, edgar2 and risen.item.count(edgar2, "MKC_Tide_Potion") },
         { true, true, 2 })
    T.wait_until(function() return named("MKC_Scout") ~= nil end, 30, "scout appears")
    T.expect("c2: the scout appears while the camp quest runs", named("MKC_Scout") ~= nil and (risen.npc.stats(named("MKC_Scout")) or {}).guild == "Cit", tostring(named("MKC_Scout")))

    tp(CAMP[1] + 900, CAMP[2] + 40, CAMP[3] + 300)
    T.wait_until(function() return bhv_find("taunt", "MKC_Ash_Warlord") ~= nil end, 20, "taunt")
    local taunt = bhv_find("taunt", "MKC_Ash_Warlord")
    T.expect("c2: the boss behaviour taunts when the player comes near", taunt ~= nil and taunt.a == "Who dares to walk into the Ash Camp?", taunt and tostring(taunt.a) or "no taunt")
    boost()
    tp(mx - 150, my + 20, mz)
    talk(named("MKC_Maren"))
    local off2 = offered()
    T.expect("c2: the tide option appears with the variable", off2:find("MKC_Maren_Lore", 1, true) ~= nil and off2:find("MKC_Maren_Tithe", 1, true) == nil, off2)
    assert(risen.dialog.get("MKC_Maren_Lore")):run()
    T.wait_event("info.finished", 60, function(name) return name == "MKC_Maren_Lore" end)
    T.sleep(60)
    local sub = offered()
    choose("MKC_Maren_Tide")
    T.eq("c2: the tithe needs 200 gold", { sub, offered():find("MKC_Maren_Tithe", 1, true) ~= nil }, { "MKC_Maren_Ash,MKC_Maren_Lore_Back,MKC_Maren_Tide", false })
    -- out of the sub-menu and the conversation (an open conversation blocks saving)
    choose("MKC_Maren_Lore_Back")
    choose("MKC_Maren_Bye")
    T.expect("c2: saved", save_game())
    local gold_before = risen.item.money(p1)
    risen.item.give(p1, "It_Gold", 200)
    talk(named("MKC_Maren"))
    T.expect("c2: the tithe is offered with 200 gold", offered():find("MKC_Maren_Tithe", 1, true) ~= nil, offered())
    local _, xpt0 = wallet()
    choose("MKC_Maren_Tithe")
    T.wait_until(function() return qdone_ok("Q_MKC_Camp") end, 20, "camp quest done by tithe")
    local _, xpt1 = wallet()
    T.eq("c2: paying the tithe ends the quest on the second path", { qdone_ok("Q_MKC_Camp"), xpt1 - xpt0 >= 100, xpt1 - xpt0 < 400, risen.item.money(p1) - gold_before }, { true, true, true, 0 })
    risen.world.quick_load()
    T.wait_event("session.start", 120, function(m) return m == "load" end)
    T.sleep(240)
    boost()
    tp(CAMP[1] + 900, CAMP[2] + 40, CAMP[3] + 300)   -- the camp is 40 m away: its NPCs are only there while the player is near
    wait(function() return named("MKC_Ash_Warlord") ~= nil end, 30, "camp after the load")
    T.sleep(60)
    local w_n = 0
    for _, e in ipairs(risen.entity.npcs()) do if e:name() == "MKC_Ash_Warlord" then w_n = w_n + 1 end end
    T.eq("c2: a load rolls everything back, nothing is made twice", { qstatus("Q_MKC_Camp"):find("Running", 1, true) ~= nil, w_n, count_template("MKC_Ash_Raider_T", nil, 100000) },
         { true, 1, 3 })
    warlord = named("MKC_Ash_Warlord")
    do
        local m = named("MKC_Maren")
        risen.log.info(("chronicle: before the kill: mkc_ch2=%s maren=%s dead=%s hp=%s state=%s pos=%s alive=%s"):format(tostring(risen.gamevar.get("mkc_ch2")), tostring(m), tostring(m and m:is_dead()),
            tostring(m and m:hp()), tostring(m and m:state()), m and pos_str(m) or "?", tostring(risen.cond.test(risen.cond.alive("MKC_Maren")))))
    end
    local g2, x2 = wallet()
    kill(warlord)
    T.wait_until(function() return qdone_ok("Q_MKC_Camp") end, 20, "camp quest done by kill")
    local g3, x3 = wallet()
    T.eq("c2: killing the warlord ends the quest on the first path", { qdone_ok("Q_MKC_Camp"), x3 - x2 >= 400, risen.item.count(p1, "MKC_Tide_Potion") >= 2 }, { true, true, true })
    T.wait_until(function() return risen.gamevar.get("mkc_camp_broken") == 1 end, 20, "camp broken")
    T.eq("c2: the kill event ran once", { risen.gamevar.get("mkc_camp_broken"), ran["Camp falls"], ran["Camp falls:send_signal"], (ran["Camp down:quest_log"] or 0) >= 1 }, { 1, 1, 1, true })
    wait(function() return (ran["Camp falls:run_script"] or 0) >= 1 end, 20, "page 2")
    T.eq("c2: the warlord's fall writes page 2", { ran["Camp falls:run_script"], risen.gamevar.get("mkc_pages") }, { 1, 2 })
    local loot = risen.item.count(warlord, "MKC_Warlord_Axe") + risen.item.count(warlord, "MKC_Tide_Potion") + risen.item.count(warlord, "It_Gold")
    T.expect("c2: the warlord's loot", risen.item.count(warlord, "MKC_Warlord_Axe") >= 1 and loot >= 3, ("axe %d potions %d gold %d"):format(risen.item.count(warlord, "MKC_Warlord_Axe"), risen.item.count(warlord, "MKC_Tide_Potion"), risen.item.count(warlord, "It_Gold")))
    T.expect("c2: the beacon quest starts after the camp", risen.quest.is_running("Q_MKC_Beacon"), qstatus("Q_MKC_Beacon"))
    T.wait_until(function() return named("MKC_Scout") == nil end, 30, "scout leaves")
    T.expect("c2: the scout leaves when the camp quest is done", named("MKC_Scout") == nil)

    -- ======================================================================================================================================
    T.section("chapter 3")
    boost()
    risen.quest.set_chapter(3, true)
    T.wait_until(function() return risen.gamevar.get("mkc_ch3") == 1 and named("MKC_Smuggler") ~= nil end, 40, "chapter 3 content")
    T.sleep(60)
    T.eq("c3: the chapter event sets its variable and a stock sector", { risen.gamevar.get("mkc_ch3"), risen.world.sector_enabled("Druid_Outdoor_Story_LizardHunter_Off"), ran["Chapter 3: smugglers and the beacon"] },
         { 1, true, 1 })
    local smug = named("MKC_Smuggler")
    T.expect("c3: the smugglers layer switched itself on", smug ~= nil and (ran["Chapter 3: smugglers and the beacon:layer"] or 0) == 0, smug and pos_str(smug) or "missing")
    local lz = risen.npc.stats("LizardWarrior") or {}
    local lp = risen.npc.stats("LizardWarrior_Crypt") or {}
    T.expect("c3: changed stats of the chapter's monsters", lz.hp_max == 780 and lz.level == 26 and lp.level ~= nil, ("hp %s level %s crypt level %s"):format(tostring(lz.hp_max), tostring(lz.level), tostring(lp.level)))
    T.eq("c3: the trader has his stock", { risen.item.count(smug, "MKC_Tide_Potion"), risen.item.count(smug, "MKC_Recipe_Tide_Potion"), risen.item.count(smug, "MKC_Ember_Whisper_Scroll") >= 2,
                                          risen.item.count(smug, "It_Pearl") }, { 6, 1, true, 4 })
    local sx, sy, sz = smug:position()
    tp(sx - 150, sy + 20, sz)
    talk(named("MKC_Smuggler"))
    local so = offered()
    T.expect("c3: the smuggler offers trade and the chronicle option", so:find("MKC_Smuggler_Trade", 1, true) ~= nil and so:find("MKC_Smuggler_Chronicle", 1, true) ~= nil, so)
    local scroll0 = risen.item.count(p1, "MKC_Ember_Whisper_Scroll")
    choose("MKC_Smuggler_Chronicle")
    T.eq("c3: the smuggler's dialogue gives a scroll", risen.item.count(p1, "MKC_Ember_Whisper_Scroll") - scroll0, 1)
    tp(mx - 150, my + 20, mz)
    talk(named("MKC_Maren"))
    local o3 = offered()
    choose("MKC_Maren_Trail")
    T.eq("c3: the dialogue switches a stock sector", { o3:find("MKC_Maren_Trail", 1, true) ~= nil, risen.world.sector_enabled("OutMain_Story_Chapter3_Lizards_Off"), risen.gamevar.get("MKC_Trail_Shown") ~= nil },
         { true, true, true })

    -- the movement event: 20 steps of 60 m
    local steps0 = ran["Walking the shore:set_variable"] or 0
    local px, py, pz = player():position()
    for i = 1, 24 do
        player():set_pose({ x = px + 6000 * (i % 2 == 0 and 0 or 1), y = py, z = pz })
        T.sleep(8)
    end
    T.wait_until(function() return (ran["Walking the shore:count"] or 0) >= 24 end, 10, "walking events")
    T.eq("c3: walking fires the movement event, the counter lets one through", { (ran["Walking the shore:set_variable"] or 0) - steps0 >= 20, (ran["Walking the shore:give_item"] or 0) + (ran["Walking the shore:hud_message"] or 0) },
         { true, 1 })

    -- the beacon: enter the stone circle
    local raiders_before = count_template("MKC_Ash_Raider_T", nil, 100000)
    tp(CIRCLE[1] + 3000, CIRCLE[2] + 40, CIRCLE[3])
    T.sleep(40)
    tp(CIRCLE[1] + 200, CIRCLE[2] + 40, CIRCLE[3] + 200)
    T.wait_until(function() return named("MKC_Beacon_Wraith") ~= nil and count_template("MKC_Ash_Raider_T", nil, 100000) >= raiders_before + 4 end, 60, "ambush raiders")
    T.eq("c3: the beacon ambush: the area trigger spawns raiders", { ran["The beacon"], ran["The beacon:spawn_npc"], count_template("MKC_Ash_Raider_T", nil, 100000) - raiders_before >= 4 }, { 1, 4, true })
    tp(CIRCLE[1] + 3000, CIRCLE[2] + 40, CIRCLE[3])
    T.sleep(40)
    tp(CIRCLE[1] + 200, CIRCLE[2] + 40, CIRCLE[3] + 200)
    T.sleep(120)
    T.eq("c3: the ambush does not start twice", { ran["The beacon"], ran["The beacon:spawn_npc"] }, { 2, 4 })
    -- kill them: the three nearest first
    local g_b, x_b = wallet()
    local killed = 0
    -- the three named raiders of the camp: NPCs spawned without a name share their template's name, so a script cannot tell them apart
    tp(CAMP[1] + 900, CAMP[2] + 40, CAMP[3] + 300)
    wait(function() return named("MKC_Ash_Raider_1") ~= nil and named("MKC_Ash_Raider_2") ~= nil and named("MKC_Ash_Raider_3") ~= nil end, 30, "camp raiders")
    boost()
    local killed = 0
    for _, n in ipairs({ "MKC_Ash_Raider_1", "MKC_Ash_Raider_2", "MKC_Ash_Raider_3" }) do
        local e = named(n)
        if e then
            kill(e)
            killed = killed + 1
        end
    end
    T.wait_until(function() return qdone_ok("Q_MKC_Beacon") end, 30, "beacon quest done")
    local _, x_c = wallet()
    T.expect("c3: killing the raiders finishes the beacon quest", killed == 3 and qdone_ok("Q_MKC_Beacon") and x_c - x_b >= 450, ("killed %d status %s xp +%d"):format(killed, qstatus("Q_MKC_Beacon"), x_c - x_b))
    T.wait_until(function() return (ran["Beacon cleared:quest_log"] or 0) >= 1 end, 20, "signal chain")
    T.eq("c3: the kill counter and the signal chain ran", { risen.gamevar.get("mkc_ambush_done"), ran["Raider hunt:count"], ran["Raider hunt:send_signal"], ran["Beacon cleared"] }, { 1, 3, 1, 1 })
    T.wait_until(function() return named("MKC_Ash_Raider_1") == nil and named("MKC_Ash_Warlord") == nil end, 30, "camp removed")
    T.expect("c3: the camp layer switched itself off", named("MKC_Ash_Raider_1") == nil and named("MKC_Ash_Raider_2") == nil and named("MKC_Ash_Raider_3") == nil,
             ("raider 1 %s"):format(tostring(named("MKC_Ash_Raider_1"))))
    -- the dusk bell: the clock runs through 20:00
    risen.world.set_time(19, 58)
    T.sleep(30)
    local bells0 = #bells
    local clock_ok = wait(function() return #bells > bells0 end, 150, "the bell")
    T.expect("c3: the dusk bell rings at 20:00", clock_ok and (ran["Dusk bell"] or 0) >= 1 and bells[#bells] == 1, ("runs %s bells %d time %s"):format(tostring(ran["Dusk bell"]), #bells, tostring(risen.world.time())))
    T.expect("c3: saved", save_game())
    local smug_n = 0
    risen.world.quick_load()
    T.wait_event("session.start", 120, function(m) return m == "load" end)
    T.sleep(240)
    boost()
    tp(MAREN_AT[1] + 100, MAREN_AT[2] + 40, MAREN_AT[3])   -- the smuggler is 30 m from the circle: he is only there while the player is near
    wait(function() return named("MKC_Smuggler") ~= nil end, 30, "smuggler after the load")
    T.sleep(60)
    for _, e in ipairs(risen.entity.npcs()) do if e:name() == "MKC_Smuggler" then smug_n = smug_n + 1 end end
    local spawns_before = ran["The beacon:spawn_npc"]
    tp(CIRCLE[1] + 3000, CIRCLE[2] + 40, CIRCLE[3])
    T.sleep(40)
    tp(CIRCLE[1] + 200, CIRCLE[2] + 40, CIRCLE[3] + 200)
    T.sleep(300)
    T.eq("c3: after the load: layers and ambush are not made twice", { smug_n, named("MKC_Ash_Raider_1") == nil, ran["The beacon:spawn_npc"] - spawns_before, qdone_ok("Q_MKC_Beacon") },
         { 1, true, 0, true })

    -- ======================================================================================================================================
    T.section("chapter 4")
    boost()
    risen.quest.set_chapter(4, true)
    T.wait_until(function() return risen.gamevar.get("mkc_ch4") == 1 and named("MKC_Tide_Lord") ~= nil end, 60, "chapter 4 content")
    T.sleep(60)
    T.expect("c4: the key is given and starts the finale quest", risen.item.count(p1, "MKC_Tide_Key") == 1 and risen.quest.is_running("Q_MKC_Finale"),
             ("key %d quest %s"):format(risen.item.count(p1, "MKC_Tide_Key"), qstatus("Q_MKC_Finale")))
    local lordn = named("MKC_Tide_Lord")
    T.expect("c4: the tide circle layer is made", lordn ~= nil and named("MKC_Tide_Guard_1") ~= nil and named("MKC_Tide_Guard_2") ~= nil and near(lordn, CIRCLE, 800), lordn and pos_str(lordn) or "missing")
    local hour = risen.world.time()
    T.expect("c4: the clock was set", hour ~= nil and hour >= 20.9 and hour < 22, "hour " .. tostring(hour))
    -- the monastery sector: its lizard scouts appear around (13637, 26207)
    local flag = risen.world.sector_enabled("Monastery_Story_LizardAttack_Off")
    tp(13637, 13400, 26207)
    local function scouts()
        local c = 0
        for _, e in ipairs(risen.entity.npcs(player(), 8000)) do
            local t = e:template()
            if t and t:name() == "LizardScout_Monastery" then c = c + 1 end
        end
        return c
    end
    boost()
    wait(function() return scouts() > 0 end, 40, "monastery scouts")
    T.expect("c4: the monastery sector is on and its lizards are there", flag == true and scouts() > 0, ("flag %s scouts %d"):format(tostring(flag), scouts()))
    tp(CIRCLE[1] + 600, CIRCLE[2] + 60, CIRCLE[3] + 900)
    boost()
    local g4, x4 = wallet()
    kill(named("MKC_Tide_Lord"))
    T.wait_until(function() return qdone_ok("Q_MKC_Finale") end, 20, "finale quest")
    local _, x5 = wallet()
    T.expect("c4: killing the lord pays the finale quest", qdone_ok("Q_MKC_Finale") and x5 - x4 >= 800 and risen.item.money(p1) - g4 >= 500, ("xp +%d"):format(x5 - x4))
    T.wait_until(function() return risen.gamevar.get("mkc_finale_done") == 1 end, 20, "finale variable")
    T.eq("c4: the kill event set the finale variable", { risen.gamevar.get("mkc_finale_done"), ran["The Lord falls"], ran["The Lord falls:send_signal"] }, { 1, 1, 1 })
    T.wait_until(function() return qdone_ok("Q_MKC_Chronicle") and qdone_ok("Q_MKC_Epilogue") end, 30, "main quest")
    T.eq("c4: the epilogue quest and the main quest end, the amulet is the reward", { qdone_ok("Q_MKC_Epilogue"), qdone_ok("Q_MKC_Chronicle"), risen.item.count(p1, "MKC_Tide_Amulet"), risen.gamevar.get("mkc_chronicle_done") },
         { true, true, 1, 1 })
    wait(function() return risen.gamevar.get("mkc_last_line") == 1 end, 20, "the last line")
    T.eq("c4: the Lua file writes the last line when the main quest ends (modkit.quest_done)", { qdone.Q_MKC_Chronicle, risen.gamevar.get("mkc_last_line") }, { "success", 1 })
    -- the epilogue: 6 s after the signal
    T.wait_until(function() return risen.item.count(p1, "MKC_Tide_Key") == 0 and (ran["Epilogue:teleport"] or 0) >= 1 end, 90, "epilogue")
    T.sleep(60)
    local ex, ey, ez = player():position()
    T.expect("c4: the epilogue teleports, sets the clock and takes the key", math.abs(ex + 31900) < 300 and math.abs(ez + 4800) < 300 and risen.item.count(p1, "MKC_Tide_Key") == 0 and (risen.world.time() or 0) < 10,
             ("at %.0f %.0f %.0f key %d time %s"):format(ex, ey, ez, risen.item.count(p1, "MKC_Tide_Key"), tostring(risen.world.time())))
    -- Maren: the question that is only offered when the finale is done, and the script behind it
    maren = named("MKC_Maren")
    local mx2, my2, mz2 = maren:position()
    tp(mx2 - 150, my2 + 20, mz2)
    talk(maren)
    local o4 = offered()
    local bells_before = #bells
    choose("MKC_Maren_Done")
    T.expect("c4: Maren answers the finale question with the script", o4:find("MKC_Maren_Done", 1, true) ~= nil and #bells == bells_before + 1, o4 .. " bells " .. tostring(#bells - bells_before))
    T.wait_until(function() return named("MKC_Tide_Lord") == nil and named("MKC_Tide_Guard_1") == nil end, 40, "circle layer off")
    T.expect("c4: the circle layer switched itself off", named("MKC_Tide_Lord") == nil and named("MKC_Tide_Guard_1") == nil and named("MKC_Tide_Guard_2") == nil)
    local unused = {}
    for _, g in ipairs({ "Chronicle begins", "Tablet found", "Pearls in the pocket", "Ink delivered", "After the ink", "Lore heard", "Walking the shore", "Chapter 2: the hidden camp", "Camp falls",
                         "Raider hunt", "Chapter 3: smugglers and the beacon", "The beacon", "Dusk bell", "Chapter 4: the key", "The tide rises", "The Lord falls", "Epilogue", "Camp open", "Camp down",
                         "Beacon cleared", "Smugglers hint" }) do
        if (ran[g] or 0) == 0 then unused[#unused + 1] = g end
    end
    local open = {}
    for _, q in ipairs({ "Q_MKC_Chronicle", "Q_MKC_Ink", "Q_MKC_Scout", "Q_MKC_Wolves", "Q_MKC_Camp", "Q_MKC_Beacon", "Q_MKC_Finale", "Q_MKC_Epilogue" }) do
        if q ~= "Q_MKC_Scout" and not qdone_ok(q) then open[#open + 1] = q .. "=" .. qstatus(q) end
    end
    T.expect("c4: every graph ran and every quest ended", #unused == 0 and #open == 0, "not run: " .. table.concat(unused, ", ") .. "; open: " .. table.concat(open, ", "))
    -- MKC_PagesWritten (defined by after_install of lua/pages.lua) counts the mod's quests that ended with success
    local won = 0
    for _, q in ipairs({ "Q_MKC_Chronicle", "Q_MKC_Ink", "Q_MKC_Scout", "Q_MKC_Wolves", "Q_MKC_Camp", "Q_MKC_Beacon", "Q_MKC_Finale", "Q_MKC_Epilogue" }) do
        if qdone_ok(q) then won = won + 1 end
    end
    local pages = risen.script.call("MKC_PagesWritten", player(), player(), 0)
    T.expect("c4: the after_install script counts the quests that ended well", pages == won and won >= 7, ("script %s, quests won %d"):format(tostring(pages), won))
    T.finish("chronicle done")
end, "chronicle")
