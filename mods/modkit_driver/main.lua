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
    "new item icon from the mod's atlas",
    "a picture replaces a game texture",
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
    "the voiced line has its sounds",
    "turn-in option has engine conditions",
    "offered options before the quest",
    "the voiced line is spoken with its sound",
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
    "pose() reads a world object as the editor does",
    "an object is placed with its rotation",
    "a world object is removed",
    "a world object is moved and turned",
    "after the load: nothing spawned or filled twice",
    "after the load: the removal and the move hold",
    "behaviour on a placed NPC with its params, on a template's NPC with the defaults",
    "the behaviour greets the player and hands out its gift",
    "after the load: the behaviour resumes with its saved state",
    "the active world's name",
    "a spawn and an NPC of another world stay out of this one",
}

local spawned, filled = {}, {}
-- behaviours (examples/modkit/behaviors/greeter.lua) report through a mod event
local bhv = {}
risen.on("modkit_showcase.behavior", function(kind, name, a, b, c) bhv[#bhv + 1] = { kind = kind, name = name, a = a, b = b, c = c } end)
local function bhv_find(kind, name, from, pred)
    for i = from or 1, #bhv do
        local e = bhv[i]
        if e.kind == kind and e.name == name and (not pred or pred(e)) then return e end
    end
end
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

-- E12: entities of the world the showcase removes / moves (ids from the editor's world view), the bench it places turned by 45 degrees
local TORCH = "01c4332e-efae-459f-9098-ae6c71dbd23d"
local STONE = "3dbe0473-6c27-4a13-a0c2-86fdafabff88"     -- moved to -31910 655 -3777, yaw 33 (was -32160, yaw 123)
local STONEHENGE = "789b1754-3d25-45e3-b6b8-6cd9006be2d1" -- untouched: -31956 639 -3757, yaw 217 in the level file
local BENCH_AT = { -32250, 600, -4450 }
local function angle_diff(a, b) return math.abs(((a - b + 180) % 360) - 180) end
local function benches_near()
    local t = risen.template.get("Object_Interact_Bench_Outdoor")
    local n = 0
    for _, e in ipairs(t and t:instances(1000) or {}) do
        if near(e, BENCH_AT, 150) then n = n + 1 end
    end
    return n
end
local function stone_moved()
    local s = risen.entity.by_id(STONE)
    local p = s and s:pose()
    return p ~= nil and math.abs(p.x + 31910) < 5 and math.abs(p.z + 3777) < 5 and angle_diff(p.yaw, 33) < 1, p
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
    -- worlds: the showcase has a knife and an NPC that belong to Testlevel_Physics; on the island neither exists
    T.eq("the active world's name", risen.world.name(), "World")
    T.expect("a spawn and an NPC of another world stay out of this one", knives_near() == 1 and risen.entity.get("MK_Elsewhere") == nil,
             ("%d knives near, MK_Elsewhere %s"):format(knives_near(), risen.entity.get("MK_Elsewhere") and "exists" or "absent"))
    -- E12: Entity:pose() of an untouched level object gives what the editor read from the level file
    local sh = risen.entity.by_id(STONEHENGE)
    local shp = sh and sh:pose()
    T.expect("pose() reads a world object as the editor does", shp ~= nil and math.abs(shp.x + 31956) < 2 and math.abs(shp.z + 3757) < 2 and angle_diff(shp.yaw, 217) < 1,
             shp and ("%.1f %.1f %.1f yaw %.1f pitch %.1f roll %.1f"):format(shp.x, shp.y, shp.z, shp.yaw, shp.pitch, shp.roll) or "no entity")
    local bench
    for _, e in ipairs(spawned) do
        if e:template() and e:template():name() == "Object_Interact_Bench_Outdoor" then bench = e end
    end
    local bp = bench and bench:pose()
    T.expect("an object is placed with its rotation", bp ~= nil and near(bench, BENCH_AT, 100) and angle_diff(bp.yaw, 45) < 1 and benches_near() == 1,
             bp and ("%.0f %.0f %.0f yaw %.1f, %d near"):format(bp.x, bp.y, bp.z, bp.yaw, benches_near()) or "not spawned")
    T.wait_until(function() return risen.entity.by_id(TORCH) == nil end, 5, "torch removed")
    T.expect("a world object is removed", risen.entity.by_id(TORCH) == nil)
    local moved_ok, sp = stone_moved()
    T.expect("a world object is moved and turned", moved_ok, sp and ("%.1f %.1f %.1f yaw %.1f"):format(sp.x, sp.y, sp.z, sp.yaw) or "no entity")

    T.expect("new item exists", risen.template.exists("MK_Hob_Knife"))
    T.eq("new item numbers", { prop("MK_Hob_Knife", "gCItem_PS", "GoldValue"), prop("MK_Hob_Knife", "gCDamage_PS", "DamageAmount") }, { 250, 20 })
    local req = risen.item.requirements("MK_Hob_Knife") or {}
    T.eq("new item requirement", req.Strength, 12)
    T.eq("new item label", risen.loc.get("FO_MK_Hob_Knife"), "Hob's Knife")
    -- pictures: hob_knife.png is a cell of the exported icon atlas (registered by the runtime before the items were made)
    local icon = "IC_modkit_showcase_hob_knife"
    local bm = risen.assets.bitmap(icon)
    local knife_t = risen.template.get("MK_Hob_Knife")
    local icon_prop = tostring(knife_t and knife_t:get_property("gCItem_PS", "IconImage"))
    T.expect("new item icon from the mod's atlas", bm ~= nil and bm.attached and icon_prop:lower() == icon:lower(),
             ("attached=%s IconImage=%s"):format(tostring(bm and bm.attached), icon_prop))
    -- ItWpn_SwordMisc_01_Diffuse_01.png became the compiled texture of that name: the handles of the knife Hob carries load it from the mod
    local tex = "ItWpn_SwordMisc_01_Diffuse_01._ximg"
    T.wait_until(function() local r = risen.assets.resource(tex) return r ~= nil and r.state == 3 end, 20, "texture loaded")
    local res, info = risen.assets.resource(tex), risen.assets.info(tex)
    T.expect("a picture replaces a game texture", res ~= nil and res.state == 3 and tostring(res.path):find("modkit_showcase", 1, true) ~= nil and
             info.source == "modkit_showcase:assets", ("state=%s path=%s source=%s"):format(tostring(res and res.state), tostring(res and res.path), tostring(info.source)))
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

        -- behaviours: greeter on MK_Watcher (his own text, a gift) and, through the template MK_Hob_T, on Hob (the default text)
        T.wait_until(function() return bhv_find("start", "MK_Watcher") and bhv_find("start", "MK_Hob") end, 20, "the behaviours started")
        local ws, hs = bhv_find("start", "MK_Watcher"), bhv_find("start", "MK_Hob")
        T.eq("behaviour on a placed NPC with its params, on a template's NPC with the defaults", { ws and ws.a, hs and hs.a }, { "Halt! Who goes there?", "Hello, stranger!" })
        local wat, pl = risen.entity.get("MK_Watcher"), risen.entity.player()
        if wat and pl and not bhv_find("near", "MK_Watcher") then
            local px, py, pz = pl:position()
            wat:set_pose({ x = px + 150, y = py + 20, z = pz })
        end
        T.wait_until(function() return bhv_find("near", "MK_Watcher") ~= nil end, 10, "the watcher greets")
        local wn = bhv_find("near", "MK_Watcher")
        T.expect("the behaviour greets the player and hands out its gift", wn ~= nil and wn.a == 1 and wn.b == true, wn and ("greeted %s, gift %s"):format(tostring(wn.a), tostring(wn.b)) or "no greeting")

        -- dialogues: talk like a player (open the conversation, choose an option, wait until it played)
        local names = { "MK_Hob_Who", "MK_Hob_Work", "MK_Hob_Ore", "MK_Hob_Island", "MK_Hob_Pirates", "MK_Hob_Never" }
        local missing = {}
        for _, n in ipairs(names) do if not risen.dialog.get(n) then missing[#missing + 1] = n end end
        T.expect("dialogue options exist", #missing == 0, table.concat(missing, ", "))
        -- the line with a sound (an Ogg for every language, an MP3 for German): the layer compiled both under the line's text id
        local voiced = risen.voice.get("MK_Hob_Who_2")
        local vdef, vde = voiced and voiced.variants.default, voiced and voiced.variants.German
        T.expect("the voiced line has its sounds", vdef ~= nil and vde ~= nil and vdef.duration_ms >= 290 and vdef.duration_ms <= 310 and vde.duration_ms > 0,
                 ("default=%s German=%s"):format(tostring(vdef and vdef.duration_ms), tostring(vde and vde.duration_ms)))
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
        choose("MK_Hob_Who")
        local last = risen.voice.last()
        T.expect("the voiced line is spoken with its sound", last ~= nil and last.key == "MK_Hob_Who_2",
                 last and (tostring(last.key) .. " " .. tostring(last.title)) or "the engine asked for no registered voice")
        open_talk()   -- the option has no sub-options: it ended the conversation
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
        T.eq("after the load: nothing spawned or filled twice", { knives_near(), chest2 and risen.item.count(chest2, "MK_Strong_Potion"), benches_near() }, { 1, 2, 1 })
        T.wait_until(function() return risen.entity.by_id(TORCH) == nil and stone_moved() end, 10, "world edits after the load")
        local w_after = bhv_find("start", "MK_Watcher", 1, function(e) return e.b == true end)
        T.expect("after the load: the behaviour resumes with its saved state", w_after ~= nil and (w_after.c or 0) >= 1,
                 w_after and ("resumed, greeted %s"):format(tostring(w_after.c)) or "not resumed")
        local moved2, sp2 = stone_moved()
        T.expect("after the load: the removal and the move hold", risen.entity.by_id(TORCH) == nil and moved2,
                 ("torch %s, stone %s"):format(risen.entity.by_id(TORCH) and "back" or "gone", sp2 and ("%.0f %.0f yaw %.0f"):format(sp2.x, sp2.z, sp2.yaw) or "?"))

        -- Q_MK_Potions started with the new game: done while the player carries three
        T.expect("the game-start quest runs", risen.quest.is_running("Q_MK_Potions"))
        local p = assert(risen.entity.player())
        risen.item.give(p, "MK_Strong_Potion", 3 - risen.item.count(p, "MK_Strong_Potion"))
        T.sleep(30)
        local pq = risen.quest.get("Q_MK_Potions")
        T.eq("carrying three potions finishes it and sets the variable", { tostring(pq and pq:get_property("Status")):find("Success") ~= nil, risen.gamevar.get("MK_Potions_Done") },
             { true, 1 })
    end
    if T.param("shot", "") ~= "" then
        -- a look at the new texture (-Param shot=1): a club and the knife on the ground between the camera and the player
        local cam = risen.world.camera()
        local p = risen.entity.player()
        if cam and p then
            local x, y, z = p:position()
            risen.entity.spawn("It_1H_Club", x - cam.dx * 120, y + 60, z - cam.dz * 120, true)
            risen.entity.spawn("MK_Hob_Knife", x - cam.dx * 150 + cam.dz * 40, y + 60, z - cam.dz * 150 - cam.dx * 40, true)
            T.sleep(60)
            risen.debug.screenshot("modkit_textures")
            T.sleep(20)
        end
    end
    T.finish()
end, "modkit")
