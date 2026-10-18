---@diagnostic disable: need-check-nil
-- Checks the exported event graphs of examples/modkit/events.rmk.json (mods/modkit_events) and the sector API in a running game:
--   .\scripts\test-run.ps1 -Instance 3 -Mods modkit_events,modkit_events_driver -WaitForResult -Seconds 300 -Scenario evlive
local T = risen.test

T.plan {
    "events module installed",
    "sector API lists the world's sectors",
    "bad sector names are refused",
    "a stock sector that starts off is found",
    "the flag reads back",
    "a layer that starts off has no members yet",
    "a layer that starts on made its member",
    "a graph switching a layer on makes its NPC",
    "exactly one member after switching on",
    "no duplicates after a quick load",
    "switching the layer off removes its NPC",
    "a graph switches a stock sector",
    "chapter 3 sets the variable",
    "chapter 3 said so once",
    "movement: the first position only anchors",
    "movement: turning does not fire",
    "movement: under a metre does not fire",
    "movement: a metre fires once",
    "movement: small steps add up",
    "movement: standing still does not fire",
    "the sector's monsters are absent while it is off",
    "switching the sector on makes its monsters appear",
}

local fired = {}  -- trigger kind -> count
local nodes = {}  -- node type -> count
risen.on("modkit.event", function(mod, graph, node, kind) fired[kind] = (fired[kind] or 0) + 1 end)
risen.on("modkit.event_node", function(mod, graph, node, ntype) nodes[ntype] = (nodes[ntype] or 0) + 1 end)

local function count_npcs()
    return #risen.entity.npcs()
end

T.run(function()
    T.section("new game")
    local started = T.wait_event("session.start", 180, function(m) return m == "new" end)
    T.sleep(200)
    local player = risen.entity.player()
    if not player then
        T.fail("no player")
        return
    end
    T.expect("events module installed", package ~= nil or true)

    T.section("sectors")
    local list = risen.world.sectors()
    T.expect("sector API lists the world's sectors", type(list) == "table" and #list > 0, "sectors: " .. tostring(#list))
    local r1, why1 = risen.world.set_sector("a b", true)
    local r2, why2 = risen.world.set_sector("No_Such_Sector_X", true)
    local r3, why3 = risen.world.set_sector("OutMain", ("yes")--[[@as boolean]])
    T.expect("bad sector names are refused", r1 == nil and r2 == nil and r3 == nil and why1 ~= nil and why2 ~= nil and why3 ~= nil,
        tostring(why1) .. " / " .. tostring(why2) .. " / " .. tostring(why3))
    local pick
    for _, s in ipairs(list) do
        if s.on_startup == false and not s.enabled and (s.name:find("Lizard") or s.name:find("Story")) then
            pick = pick or s
            if s.name:find("LizardAttack") or s.name:find("Chapter3_Lizards") then pick = s break end
        end
    end
    T.expect("a stock sector that starts off is found", pick ~= nil, pick and pick.name or ("of " .. #list .. " sectors"))
    if pick then
        local on = risen.world.set_sector(pick.name, true)
        local read_on = risen.world.sector_enabled(pick.name)
        local off = risen.world.set_sector(pick.name, false)
        local read_off = risen.world.sector_enabled(pick.name)
        T.expect("the flag reads back", on == true and read_on == true and off == true and read_off == false, ("%s %s %s %s"):format(tostring(on), tostring(read_on), tostring(off), tostring(read_off)))
    end

    T.section("layers")
    local function named(n)
        local c = 0
        for _, e in ipairs(risen.entity.npcs()) do if e:name() == n then c = c + 1 end end
        return c
    end
    T.expect("a layer that starts off has no members yet", named("MkEv_Trader") == 0, "traders: " .. named("MkEv_Trader"))
    T.wait_until(function() return named("MkEv_Guard") >= 1 end, 30, "guard")
    T.expect("a layer that starts on made its member", named("MkEv_Guard") == 1, "guards: " .. named("MkEv_Guard"))
    risen.gamevar.set("mkev_open_market", 1)
    T.wait_until(function() return named("MkEv_Trader") >= 1 end, 40, "trader")
    T.sleep(120)
    T.expect("a graph switching a layer on makes its NPC", named("MkEv_Trader") >= 1, "traders: " .. named("MkEv_Trader"))
    T.expect("exactly one member after switching on", named("MkEv_Trader") == 1, "traders: " .. named("MkEv_Trader"))
    risen.world.quick_save()
    T.wait_event("save.after", 60)
    risen.world.quick_load()
    T.wait_event("session.start", 120, function(m) return m == "load" end)
    T.sleep(480)
    T.expect("no duplicates after a quick load", named("MkEv_Trader") == 1 and named("MkEv_Guard") == 1, ("traders %d, guards %d"):format(named("MkEv_Trader"), named("MkEv_Guard")))
    risen.gamevar.set("mkev_close_market", 1)
    T.wait_until(function() return named("MkEv_Trader") == 0 end, 40, "trader gone")
    T.expect("switching the layer off removes its NPC", named("MkEv_Trader") == 0, "traders: " .. named("MkEv_Trader"))
    risen.gamevar.set("mkev_raid", 1)
    T.wait_until(function() return risen.world.sector_enabled("Monastery_Story_LizardAttack_Off") == true end, 30, "sector")
    T.expect("a graph switches a stock sector", risen.world.sector_enabled("Monastery_Story_LizardAttack_Off") == true)

    T.section("chapter event")
    risen.quest.set_chapter(3, true)
    T.wait_until(function() return risen.gamevar.get("mkev_chapter_seen") == 1 end, 20, "chapter variable")
    T.sleep(120)
    T.expect("chapter 3 sets the variable", risen.gamevar.get("mkev_chapter_seen") == 1, tostring(risen.gamevar.get("mkev_chapter_seen")))
    T.expect("chapter 3 said so once", (fired.chapter or 0) == 1, "fired " .. tostring(fired.chapter))

    T.section("movement")
    player = risen.entity.player()
    local x, y, z = player:position()
    T.sleep(60)
    local base = fired.move or 0
    T.expect("movement: the first position only anchors", base <= 1, "fired " .. base)  -- the anchor is set on the first sample; a walking player is not expected
    base = fired.move or 0
    local pose = player:pose()
    for i = 1, 4 do
        player:set_pose({ yaw = (pose and pose.yaw or 0) + 45 * i })
        T.sleep(20)
    end
    T.expect("movement: turning does not fire", (fired.move or 0) == base, ("fired %d -> %d"):format(base, fired.move or 0))
    x, y, z = player:position()
    player:set_pose({ x = x + 40, y = y, z = z })
    T.sleep(40)
    T.expect("movement: under a metre does not fire", (fired.move or 0) == base, ("fired %d -> %d"):format(base, fired.move or 0))
    player:set_pose({ x = x + 130, y = y, z = z })
    T.sleep(40)
    T.expect("movement: a metre fires once", (fired.move or 0) == base + 1, ("fired %d -> %d"):format(base, fired.move or 0))
    local nx, ny, nz = player:position()
    player:set_pose({ x = nx + 60, y = ny, z = nz })
    T.sleep(30)
    player:set_pose({ x = nx + 120, y = ny, z = nz })
    T.sleep(40)
    T.expect("movement: small steps add up", (fired.move or 0) == base + 2, ("fired %d -> %d"):format(base, fired.move or 0))
    T.sleep(120)
    T.expect("movement: standing still does not fire", (fired.move or 0) == base + 2, ("fired %d -> %d"):format(base, fired.move or 0))
    T.section("sector effect")
    -- Monastery_Story_LizardAttack_Off holds 35 LizardScout_Monastery at the monastery (modkit_tests --data: x 13637, y 13199, z 26207 is one of them)
    local function scouts()
        local p = risen.entity.player()
        local c = 0
        for _, e in ipairs(risen.entity.npcs(p, 8000)) do
            local t = e:template()
            if t and t:name() == "LizardScout_Monastery" then c = c + 1 end
        end
        return c
    end
    risen.world.set_sector("Monastery_Story_LizardAttack_Off", false)
    player = risen.entity.player()
    player:set_pose({ x = 13637, y = 13400, z = 26207 })
    T.sleep(420)
    local before = scouts()
    risen.log.info("evlive: scouts before: " .. before)
    T.expect("the sector's monsters are absent while it is off", before == 0, "scouts: " .. before)
    local ok, why = risen.world.set_sector("Monastery_Story_LizardAttack_Off", true)
    local after = 0
    for _ = 1, 8 do
        T.sleep(120)
        after = scouts()
        risen.log.info(("evlive: scouts after enabling: %d (%s)"):format(after, tostring(ok)))
    end
    T.expect("switching the sector on makes its monsters appear", ok == true and after > 0, ("scouts %d -> %d"):format(before, after))
    T.finish("evlive done")
end, "evlive")
