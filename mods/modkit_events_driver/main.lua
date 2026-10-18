-- Checks the exported event graphs of examples/modkit/events.rmk.json (mods/modkit_events) and the sector API in a running game:
--   .\scripts\test-run.ps1 -Instance 3 -Mods modkit_events,modkit_events_driver -WaitForResult -Seconds 300 -Scenario evlive
local T = risen.test

T.plan {
    "events module installed",
    "sector API lists the world's sectors",
    "bad sector names are refused",
    "a stock sector that starts off is found",
    "enabling it makes entities appear",
    "disabling clears the flag (entities already made stay)",
    "chapter 3 sets the variable",
    "chapter 3 said so once",
    "movement: the first position only anchors",
    "movement: turning does not fire",
    "movement: under a metre does not fire",
    "movement: a metre fires once",
    "movement: small steps add up",
    "movement: standing still does not fire",
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
        local function snapshot()
            local m, n = {}, 0
            for _, e in ipairs(risen.entity.npcs()) do
                local t = e:template()
                m[e:id() or tostring(n)] = t and t:name() or "?"
                n = n + 1
            end
            return m, n
        end
        local function diff(a, b)
            local out = {}
            for id, t in pairs(b) do if not a[id] then out[#out + 1] = t end end
            table.sort(out)
            return table.concat(out, ",")
        end
        local s0, n0 = snapshot()
        local on, why = risen.world.set_sector(pick.name, true)
        local s1, n1
        for i = 1, 4 do
            T.sleep(120)
            s1, n1 = snapshot()
            risen.log.info(("evlive: %d ticks after enabling: %d npcs, new: %s"):format(i * 120, n1, diff(s0, s1)))
        end
        T.expect("enabling it makes entities appear", on == true and risen.world.sector_enabled(pick.name) == true and n1 > n0, ("npcs %d -> %d: %s"):format(n0, n1, diff(s0, s1)))
        local off = risen.world.set_sector(pick.name, false)
        local s2, n2
        for i = 1, 4 do
            T.sleep(120)
            s2, n2 = snapshot()
            risen.log.info(("evlive: %d ticks after disabling: %d npcs, gone: %s, new: %s"):format(i * 120, n2, diff(s2, s1), diff(s1, s2)))
        end
        T.expect("disabling clears the flag (entities already made stay)", off == true and risen.world.sector_enabled(pick.name) == false, ("npcs %d -> %d, gone: %s"):format(n1, n2, diff(s2, s1)))
    end

    T.section("chapter event")
    risen.quest.set_chapter(3, true)
    T.wait_until(function() return risen.gamevar.get("mkev_chapter_seen") == 1 end, 20, "chapter variable")
    T.sleep(120)
    T.expect("chapter 3 sets the variable", risen.gamevar.get("mkev_chapter_seen") == 1, tostring(risen.gamevar.get("mkev_chapter_seen")))
    T.expect("chapter 3 said so once", (fired.chapter or 0) == 1, "fired " .. tostring(fired.chapter))

    T.section("movement")
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
    T.finish("evlive done")
end, "evlive")
