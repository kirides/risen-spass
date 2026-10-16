---@diagnostic disable: need-check-nil, param-type-mismatch, cast-local-type, assign-type-mismatch
-- Live scenario `routines` of mods/routines (only loaded in a test run). One game process per phase (scripts/smoke-test.ps1):
--   save          new game: API validation, the world's stock points, a guard that is displaced and returns, a day/night cycle (work, relax, sleep points), a
--                 patrol that walks its waypoints, the persistent RT_Gate with a `routine`, idempotence / replacement / clearing; quick save; quick load in the
--                 same process: everything is still there and working
--   load          a fresh process loads that quick save: the same checks (the patrol anchors get their names and numbers back from the mod's record)
--   load_changed  a fresh process whose mod declares a changed routine for RT_Gate: the old post is removed, the new one is used
-- What the engine does with the points (walk there, stand guard, face the post's yaw, sleep on the ground) is the engine's own routine system; the scenario
-- proves that the layer feeds it correctly and that it survives savegames.
local T = risen.test

return function(phase)
    local P0 = {}            -- the player's start position of this process
    local function spot(dx, dz) return P0.x + dx, P0.y, P0.z + dz end

    local function dist(a, b) return (a and b) and a:distance_to(b) or 1e9 end
    -- waits (in seconds of game time ticks, ~80 per second) for pred() without failing the run; true when it held
    local function await(pred, seconds)
        for _ = 1, math.floor(seconds * 8) do
            if pred() then return true end
            T.sleep(10)
        end
        return pred() and true or false
    end
    local function at_post(npc, point, radius) return npc and point and dist(npc, point) < (radius or 90) end
    local function prop(e, set, name) return e and e:get_property(set, name) end
    -- standing at a post and busy with it: the engine's interaction state "ZS_Interact_Interact" (before that it walks in "ZS_Interact_PreInteract")
    local function busy_at(npc, point, radius) return at_post(npc, point, radius or 90) and npc:state() == "ZS_Interact_Interact" end
    local function by_id(id) return id and risen.entity.by_id(id) or nil end
    local function ids_of(list) local out = {} for i, e in ipairs(list) do out[i] = e:id() end return out end
    local function guards_near() return #risen.world.points({ kind = "guard", near = risen.entity.player(), radius = 3500, limit = 5000 }) end
    local function sorted_patrol(name)
        local list = risen.world.points({ template = "Anchor_Patrol", name = name, limit = 20 }) or {}
        table.sort(list, function(a, b) return (a.index or 0) < (b.index or 0) end)
        return list
    end
    -- the waypoint (1..n) of a patrol group an NPC stands at: anchor index + 1
    local function waypoint_near(anchors, npc, radius)
        for _, a in ipairs(anchors) do
            if dist(npc, a.entity) < (radius or 110) then return (a.index or 0) + 1 end
        end
        return nil
    end
    local function wait_gate()
        T.wait_until(function() return risen.entity.get("RT_Gate") ~= nil end, 60, "RT_Gate exists")
        T.sleep(60)
    end
    local function save_game()
        T.wait_until(function() return risen.world.is_save_allowed() end, 30, "saving allowed")
        local saved = false
        risen.on("save.after", function(_, ok) saved = ok end)
        risen.world.quick_save()
        T.wait_until(function() return saved end, 60, "savegame written")
        return saved
    end

    -- ---- what must hold in a loaded world: in-process load and fresh process -------------------------------------------------------------------------
    local function check_loaded(exp, tag)
        local player = assert(risen.entity.player())
        local G, K, P = by_id(exp.G.id), by_id(exp.K.id), by_id(exp.P.id)
        T.expect(tag .. ": the NPCs are back (same entity IDs)", G and K and P, tostring(G) .. " " .. tostring(K) .. " " .. tostring(P))
        if not (G and K and P) then return end
        -- the engine saved the points and the references the NPCs hold
        local g = risen.npc.get_routine(G)
        T.eq(tag .. ": the guard still has its post (same freepoint)", { g.work and g.work:id(), g.relax and g.relax:id(), g.sleep and g.sleep:id() }, { exp.G.work, exp.G.work, exp.G.work })
        local k = risen.npc.get_routine(K)
        T.eq(tag .. ": the day/night NPC keeps its three points", { k.work and k.work:id(), k.relax and k.relax:id(), k.sleep and k.sleep:id() }, { exp.K.work, exp.K.relax, exp.K.sleep })
        T.eq(tag .. ": the points are saved as freepoints of their kind", { by_id(exp.G.work) and by_id(exp.G.work):get_property("gCInteraction_PS", "UseType"),
            by_id(exp.K.relax) and by_id(exp.K.relax):get_property("gCInteraction_PS", "UseType"), by_id(exp.K.sleep) and by_id(exp.K.sleep):get_property("gCInteraction_PS", "UseType") },
            { "gEInteractionUseType_Guard", "gEInteractionUseType_LookAt", "gEInteractionUseType_SleepGround" })
        T.expect(tag .. ": the guard is at its post and faces its yaw", await(function()
            local g1 = by_id(exp.G.id)
            return busy_at(g1, by_id(exp.G.work)) and select(1, g1:direction()) > 0.9
        end, 45), tostring(dist(by_id(exp.G.id), by_id(exp.G.work))))
        T.expect(tag .. ": the day/night NPC works at its work point", await(function() return busy_at(by_id(exp.K.id), by_id(exp.K.work)) end, 45), tostring(dist(K, by_id(exp.K.work))))
        -- the patrol: the savegame has the anchors but not their name and numbers; the mod's record gives them back
        local wp = sorted_patrol("routines_Patrol_Ring")
        T.eq(tag .. ": the patrol anchors are found by their group name again", #wp, 3)
        T.eq(tag .. ": the patrol anchors have their numbers again", { wp[1] and wp[1].index, wp[2] and wp[2].index, wp[3] and wp[3].index }, { 0, 1, 2 })
        T.eq(tag .. ": the patrol anchors are the saved entities", { wp[1] and wp[1].entity:id(), wp[2] and wp[2].entity:id(), wp[3] and wp[3].entity:id() }, exp.P.anchors)
        T.eq(tag .. ": every waypoint keeps its freepoint as a child", { wp[1] and #wp[1].entity:children(), wp[2] and #wp[2].entity:children(), wp[3] and #wp[3].entity:children() }, { 1, 1, 1 })
        local pr = risen.npc.get_routine(P)
        T.eq(tag .. ": the patroller still works at its first waypoint's freepoint", pr.work and pr.work:id(), exp.P.work)
        -- the engine's patrol logic uses those names and numbers: sending the patroller on must lead to another waypoint of the group
        local function nearest_wp() return waypoint_near(sorted_patrol("routines_Patrol_Ring"), by_id(exp.P.id)) end
        local first = await(function() return nearest_wp() ~= nil and by_id(exp.P.id):state() == "ZS_Interact_Interact" end, 60) and nearest_wp()
        T.expect(tag .. ": the patroller stands at a waypoint", first ~= nil)
        if first then
            T.sleep(120)
            risen.npc.apply_routine(by_id(exp.P.id))
            local moved = await(function() local n = nearest_wp() return n ~= nil and n ~= first end, 60)
            T.expect(tag .. ": the restored patrol sends the patroller to another waypoint", moved, "from " .. tostring(first) .. " to " .. tostring(nearest_wp()))
        end
        -- the persistent NPC with a routine
        local gate = risen.entity.get("RT_Gate")
        T.expect(tag .. ": the gate keeper is the saved NPC", gate and gate:id() == exp.GATE.id, tostring(gate and gate:id()))
        if gate then
            local gr = risen.npc.get_routine(gate)
            T.eq(tag .. ": the gate keeper keeps the post made before the save (not made again)", gr.work and gr.work:id(), exp.GATE.work)
            T.expect(tag .. ": the gate keeper is at his post", await(function() return busy_at(risen.entity.get("RT_Gate") or gate, by_id(exp.GATE.work)) end, 45))
        end
        T.eq(tag .. ": no freepoint was made twice", guards_near(), exp.guards)
    end

    -- ===================================================================================================================================================
    if phase == "save" then
        local checks = {
            "bad arguments are refused", "bad point and patrol arguments are refused", "stock points are listed", "points can be filtered", "the clock can be read and set",
            "a point is made on the mesh", "an unreachable position is refused", "the guard reaches its post", "the guard faces the post's yaw", "the guard displaced far is sent back by apply_routine",
            "gate keeper: the declared routine is applied", "gate keeper: reaches its post", "day: at the work point", "evening: at the relax point", "night: asleep at the sleep point", "next day: back at work",
            "patrol group: three numbered waypoints", "patrol: starts at a waypoint", "patrol: goes on to the next waypoint", "patrol: and to the one after", "patrol: wraps around", "patrol: and round again",
            "the same routine again changes nothing", "another routine replaces the old points", "clearing removes the points", "saved",
            "loaded: the NPCs are back (same entity IDs)", "loaded: the guard still has its post (same freepoint)", "loaded: the day/night NPC keeps its three points",
            "loaded: the points are saved as freepoints of their kind", "loaded: the guard is at its post and faces its yaw", "loaded: the day/night NPC works at its work point",
            "loaded: the patrol anchors are found by their group name again", "loaded: the patrol anchors have their numbers again", "loaded: the patrol anchors are the saved entities",
            "loaded: every waypoint keeps its freepoint as a child", "loaded: the patroller still works at its first waypoint's freepoint", "loaded: the patroller stands at a waypoint",
            "loaded: the restored patrol sends the patroller to another waypoint", "loaded: the gate keeper is the saved NPC", "loaded: the gate keeper keeps the post made before the save (not made again)",
            "loaded: the gate keeper is at his post", "loaded: no freepoint was made twice",
        }
        T.plan(checks)
        T.run(function()
            T.wait_event("session.start", 180, function(m) return m == "new" end)
            wait_gate()
            local player = assert(risen.entity.player())
            P0.x, P0.y, P0.z = player:position()

            -- ---- API validation: nothing may crash, every refusal says why ----------------------------------------------------------------------------------
            T.section("validation")
            local probe = assert(risen.npc.spawn("Peasant_1_01", P0.x + 100, P0.y + 20, P0.z + 100, { name = "RT_Probe", peaceful = true }))
            local x1, y1, z1 = spot(400, 100)
            local bad = {
                { "a number", 5 }, { "an empty table", {} }, { "unknown key", { colour = 1 } }, { "point is a number", { at = 5 } }, { "point too short", { at = { 1, 2 } } },
                { "at and work", { at = { x1, y1, z1 }, work = { x1, y1, z1 } } }, { "unknown kind", { at = { x1, y1, z1, kind = "nonsense" } } }, { "kind is a number", { at = { x1, y1, z1, kind = 5 } } },
                { "yaw is text", { at = { x1, y1, z1, yaw = "left" } } }, { "no such entity", { at = "NoSuchEntity_RT" } }, { "patrol of one", { patrol = { { x1, y1, z1 } } } },
                { "patrol and at", { patrol = { { x1, y1, z1 }, { x1 + 100, y1, z1 } }, at = { x1, y1, z1 } } }, { "unknown patrol group", { patrol = "NoSuchGroup" } },
                { "routine name is a number", { at = { x1, y1, z1 }, routine = 5 } }, { "unknown routine name", { at = { x1, y1, z1 }, routine = "NoSuchRoutine" } },
                { "off the mesh", { at = { x1 + 6000, y1, z1 + 9000 } } }, { "apply is text", { at = { x1, y1, z1 }, apply = "yes" } },
                { "relative with a bad kind", { at = { dx = 1, dy = 0, dz = 1, kind = "x" } } },
            }
            local refused, reasons = 0, {}
            for _, b in ipairs(bad) do
                local ok, why = risen.npc.set_routine(probe, b[2])
                if ok == nil and type(why) == "string" and why ~= "" then refused = refused + 1 else reasons[#reasons + 1] = b[1] end
            end
            local arg_ok = true
            for _, victim in ipairs({ { nil, { at = { x1, y1, z1 } } }, { player, { at = { x1, y1, z1 } } }, { "text", { at = { x1, y1, z1 } } } }) do
                local ok, why = pcall(risen.npc.set_routine, victim[1], victim[2])
                local r, w = risen.npc.set_routine(victim[1], victim[2])
                if not ok or r ~= nil or type(w) ~= "string" then arg_ok = false end
            end
            T.expect("bad arguments are refused", refused == #bad and arg_ok, ("%d of %d refused; accepted: %s"):format(refused, #bad, table.concat(reasons, ", ")))
            local p_ok = true
            -- the Lua functions answer nil and a reason; a native may also raise an argument error: neither crashes the game
            local function refused_call(f, ...)
                local ok, a, b = pcall(f, ...)
                return ok and a == nil and type(b) == "string"
            end
            local function refused_native(f, ...)
                local ok, a, b = pcall(f, ...)
                return not ok or (a == nil and type(b) == "string")
            end
            p_ok = p_ok and refused_call(risen.world.add_point, "nonsense", x1, y1, z1)
            p_ok = p_ok and refused_call(risen.world.add_point, "guard", "x", y1, z1)
            p_ok = p_ok and refused_call(risen.world.add_point, "guard", x1 + 6000, y1, z1 + 9000)
            p_ok = p_ok and refused_call(risen.world.add_point, 5, x1, y1, z1)
            p_ok = p_ok and refused_call(risen.world.add_point, "guard", x1, y1, z1, 5)
            p_ok = p_ok and refused_call(risen.world.points, 5)
            p_ok = p_ok and refused_call(risen.world.points, { kind = "nonsense" })
            p_ok = p_ok and refused_call(risen.world.add_patrol, "bad name!", { { x1, y1, z1 }, { x1, y1, z1 } })
            p_ok = p_ok and refused_call(risen.world.add_patrol, "OnePoint", { { x1, y1, z1 } })
            p_ok = p_ok and refused_call(risen.world.add_patrol, "Far", { { x1 + 6000, y1, z1 + 9000 }, { x1, y1, z1 } })
            p_ok = p_ok and refused_native(risen.npc.routine_points, 5)
            p_ok = p_ok and refused_native(risen.npc.set_routine_points, probe, { work = 5 })
            p_ok = p_ok and refused_native(risen.npc.select_routine, probe, "NoSuchRoutine")
            p_ok = p_ok and refused_native(risen.npc.apply_routine, 5)
            p_ok = p_ok and pcall(risen.world.scan, 5)
            p_ok = p_ok and refused_native(risen.world.spawn_point, 5)
            p_ok = p_ok and refused_native(risen.world.nav_point, "x")
            local no_junk = risen.world.remove_patrol("Far") == false and risen.world.remove_point(5) == false and risen.npc.clear_routine(5) == nil
            T.expect("bad point and patrol arguments are refused", p_ok and no_junk, "a refused call must return nil and a reason")
            T.expect("the clock can be read and set", (function()
                local h0, m0 = risen.world.time()
                local ok = risen.world.set_time(12, 0)
                local h, m = risen.world.time()
                return h0 ~= nil and m0 ~= nil and ok and h == 12 and m <= 1 and not risen.world.set_time(24, 0) and not risen.world.set_time(10, 60) and not risen.world.set_time(-1, 0)
            end)(), table.concat({ risen.world.time() }, ":"))

            -- ---- the world's own points ---------------------------------------------------------------------------------------------------------------------
            T.section("world points")
            local all = risen.world.points({ limit = 5000 })
            local guards = risen.world.points({ kind = "guard", limit = 5000 })
            local patrol = risen.world.points({ template = "Anchor_Patrol", limit = 500 })
            T.expect("stock points are listed", #all > 300 and #guards > 100 and #patrol >= 8 and all[1].kind ~= nil and all[1].template ~= nil and all[1].x ~= nil,
                ("%d points, %d guard points, %d patrol anchors"):format(#all, #guards, #patrol))
            local near = risen.world.points({ near = { 10364, 12899, 24625 }, radius = 1500, kind = "stand" })
            local lib = risen.world.points({ template = "Anchor_Patrol", name = "Anchor_Patrol_Monastery_Library" })
            local idx = {}
            for _, a in ipairs(lib) do idx[#idx + 1] = a.index end
            table.sort(idx)
            T.expect("points can be filtered", #near == 1 and near[1].distance < 5 and near[1].use == "Stand" and near[1].task == "ZS_HangAround" and #lib >= 3 and idx[1] == 0,
                ("%d stand points near the monastery spot, library patrol indices %s"):format(#near, table.concat(idx, ",")))

            -- ---- a point on the mesh ------------------------------------------------------------------------------------------------------------------------
            T.section("points")
            local sx, sy, sz = spot(400, 300)
            local fp = risen.world.add_point("guard", sx, sy, sz, { yaw = 90 })
            local px2, py2, pz2 = 0, 0, 0
            if fp then px2, py2, pz2 = fp:position() end
            T.expect("a point is made on the mesh", fp ~= nil and fp:save_relevant() and math.abs(px2 - sx) < 300 and fp:get_property("gCInteraction_PS", "UseType") == "gEInteractionUseType_Guard"
                and math.abs((select(1, fp:direction())) - 1) < 0.01, tostring(fp))
            local off, why = risen.world.add_point("guard", sx + 6000, sy, sz + 9000)
            T.expect("an unreachable position is refused", off == nil and tostring(why):find("walkable", 1, true) ~= nil, tostring(why))
            if fp then risen.world.remove_point(fp) end
            risen.entity.get("RT_Probe"):kill()

            -- ---- the actors ---------------------------------------------------------------------------------------------------------------------------------
            local function npc_at(name, dx, dz)
                local x, y, z = spot(dx, dz)
                return assert(risen.npc.spawn("Peasant_1_01", x, y + 20, z, { name = name, peaceful = true }))
            end
            local G = npc_at("RT_G", 300, 0)
            local K = npc_at("RT_K", 100, 400)
            local Pn = npc_at("RT_P", 650, 250)
            local gx, gy, gz = spot(800, -100)
            local ginfo = assert(risen.npc.set_routine(G, { at = { gx, gy, gz, kind = "guard", yaw = 90 } }))
            local kinfo = assert(risen.npc.set_routine(K, {
                work = { dx = 300, dy = 0, dz = 300, kind = "guard" }, relax = { dx = 800, dy = 0, dz = 300, kind = "lookat" }, sleep = { dx = 300, dy = 0, dz = 650, kind = "sleep" },
            }, { origin = { P0.x, P0.y, P0.z } }))
            local pinfo = assert(risen.npc.set_routine(Pn, { patrol = { { dx = 500, dy = 0, dz = 100 }, { dx = 200, dy = 0, dz = 550 }, { dx = 800, dy = 0, dz = 450 }, name = "Ring", kind = "guard" } },
                { origin = { P0.x, P0.y, P0.z } }))

            -- ---- the guard -----------------------------------------------------------------------------------------------------------------------------------
            T.section("guard")
            T.expect("the guard reaches its post", await(function() return busy_at(G, ginfo.work, 80) end, 45), ("%s %.0f"):format(G:state(), dist(G, ginfo.work)))
            local dx, _, dz = G:direction()
            T.expect("the guard faces the post's yaw", await(function() local a = G:direction() return a and a > 0.9 end, 15), tostring(dx) .. "," .. tostring(dz))

            -- the gate keeper (persistent NPC with a routine) works in parallel
            local gate = assert(risen.entity.get("RT_Gate"))
            local gr = risen.npc.get_routine(gate)
            T.expect("gate keeper: the declared routine is applied", gr.work ~= nil and gr.work:get_property("gCInteraction_PS", "UseType") == "gEInteractionUseType_Guard" and gr.created == 1
                and gr.work:save_relevant(), tostring(gr.work) .. " created " .. tostring(gr.created))
            T.expect("gate keeper: reaches its post", await(function() return busy_at(gate, gr.work, 90) end, 45), tostring(dist(gate, gr.work)))

            -- Displaced far the guard stays where he was put until his routine is looked at again (his dwell is over, or apply_routine; a small displacement, up to
            -- 300 units, may be walked back by himself, but that is not reliable).
            local far_away = assert(risen.world.add_point("stand", spot(0, 700)))
            T.expect("the guard displaced far is sent back by apply_routine", (function()
                G:teleport(far_away)
                T.sleep(20)
                if dist(G, ginfo.work) < 700 then return false, "teleport did not move him" end
                risen.npc.apply_routine(G)
                return await(function() return busy_at(G, ginfo.work, 80) end, 60)
            end)(), ("%.0f %s"):format(dist(G, ginfo.work), G:state()))

            -- ---- the day: work, evening: relax, night: sleep ---------------------------------------------------------------------------------------------------
            T.section("day and night")
            risen.world.set_time(12, 0)
            risen.npc.apply_routine(K)
            T.expect("day: at the work point", await(function() return busy_at(K, kinfo.work, 80) and prop(K, "gCScriptRoutine_PS", "ActionString") == "HandsOnHips" end, 45),
                ("%.0f %s %s"):format(dist(K, kinfo.work), K:state(), tostring(prop(K, "gCScriptRoutine_PS", "ActionString"))))
            risen.world.set_time(21, 0)
            risen.npc.apply_routine(K)
            T.expect("evening: at the relax point", await(function() return busy_at(K, kinfo.relax, 80) and prop(K, "gCScriptRoutine_PS", "ActionString") == "Think" end, 60),
                ("%.0f %s %s"):format(dist(K, kinfo.relax), K:state(), tostring(prop(K, "gCScriptRoutine_PS", "ActionString"))))
            risen.world.set_time(3, 0)
            risen.npc.apply_routine(K)
            T.expect("night: asleep at the sleep point", await(function() return busy_at(K, kinfo.sleep, 80) and prop(K, "gCScriptRoutine_PS", "AniState") == "gEAniState_SleepGround" end, 60),
                ("%.0f %s %s"):format(dist(K, kinfo.sleep), K:state(), tostring(prop(K, "gCScriptRoutine_PS", "AniState"))))
            risen.world.set_time(12, 0)
            risen.npc.apply_routine(K)
            T.expect("next day: back at work", await(function() return busy_at(K, kinfo.work, 80) end, 90), ("%.0f %s"):format(dist(K, kinfo.work), K:state()))

            -- ---- the patrol ------------------------------------------------------------------------------------------------------------------------------------
            T.section("patrol")
            local wp = sorted_patrol("routines_Patrol_Ring")
            T.expect("patrol group: three numbered waypoints", #wp == 3 and wp[1].index == 0 and wp[2].index == 1 and wp[3].index == 2 and wp[1].anchor == "Patrol" and wp[1].max_users == 1
                and pinfo.patrol == "Ring" and #wp[1].entity:children() == 1, tostring(#wp))
            -- The engine's patrol ring: a patroller whose routine point is a waypoint's freepoint walks to the next numbered waypoint of the group each time it is sent on, and from the
            -- last back to the first. (By himself a guard waits a minute or two at a waypoint; apply_routine sends him on at once.)
            local function at_wp() local n = waypoint_near(wp, Pn) return n ~= nil and Pn:state() == "ZS_Interact_Interact" and n or nil end
            local order = {}
            local ok_first = await(function() return at_wp() ~= nil end, 60)
            T.expect("patrol: starts at a waypoint", ok_first, tostring(waypoint_near(wp, Pn)) .. " " .. Pn:state())
            local cur = ok_first and at_wp() or nil
            order[1] = cur
            for hop = 1, 4 do
                if not cur then break end
                T.sleep(120)    -- (an NPC that has just arrived is still "travelling" for a moment and ignores the call)
                risen.npc.apply_routine(Pn)
                local want = cur % 3 + 1
                local arrived = await(function() return at_wp() == want end, 60)
                cur = arrived and want or nil
                order[#order + 1] = cur or ("missed " .. want)
                local where = {}
                for _, a in ipairs(wp) do where[#where + 1] = ("%d:%.0f"):format(a.index, dist(Pn, a.entity)) end
                T.expect(({ "patrol: goes on to the next waypoint", "patrol: and to the one after", "patrol: wraps around", "patrol: and round again" })[hop], arrived,
                    table.concat({ tostring(order[1]), tostring(order[2]), tostring(order[3]), tostring(order[4]), tostring(order[5]) }, ">") .. " " .. Pn:state() .. " " .. table.concat(where, ","))
            end

            -- ---- idempotence, replacement, clearing -----------------------------------------------------------------------------------------------------------
            T.section("idempotence")
            local R = npc_at("RT_R", 300, 700)
            local spec = { at = { dx = 0, dy = 0, dz = -300, kind = "spectator" } }
            local first = assert(risen.npc.set_routine(R, spec, { origin = { P0.x, P0.y, P0.z + 700 } }))
            local n1 = guards_near() + #risen.world.points({ kind = "spectator", near = player, radius = 3500 })
            local again = assert(risen.npc.set_routine(R, spec, { origin = { P0.x, P0.y, P0.z + 700 } }))
            local n2 = guards_near() + #risen.world.points({ kind = "spectator", near = player, radius = 3500 })
            T.expect("the same routine again changes nothing", again.reused == true and n1 == n2 and again.work ~= nil and first.work ~= nil and again.work:id() == first.work:id(),
                ("reused %s, %d -> %d points"):format(tostring(again.reused), n1, n2))
            local oldid = first.work:id()
            local second = assert(risen.npc.set_routine(R, { at = { dx = 0, dy = 0, dz = -300, kind = "dance" } }, { origin = { P0.x, P0.y, P0.z + 700 } }))
            T.sleep(80)    -- (a point an NPC used is removed 40 frames after the NPC went on)
            T.expect("another routine replaces the old points", second.reused == false and second.work:id() ~= oldid and by_id(oldid) == nil
                and second.work:get_property("gCInteraction_PS", "UseType") == "gEInteractionUseType_Dance", tostring(by_id(oldid)))
            local third_id = second.work:id()
            T.expect("clearing removes the points", (function()
                local ok = risen.npc.clear_routine(R)
                T.sleep(80)
                local r = risen.npc.get_routine(R)
                return ok and r.work == nil and r.created == 0 and by_id(third_id) == nil
            end)())
            R:kill()

            -- ---- save, and load in the same process --------------------------------------------------------------------------------------------------------------
            T.section("save and load")
            local gate_work = risen.npc.get_routine(risen.entity.get("RT_Gate")).work
            local exp = {
                G = { id = G:id(), work = ginfo.work:id() },
                K = { id = K:id(), work = kinfo.work:id(), relax = kinfo.relax:id(), sleep = kinfo.sleep:id() },
                P = { id = Pn:id(), work = pinfo.work:id(), anchors = { wp[1].entity:id(), wp[2].entity:id(), wp[3].entity:id() } },
                GATE = { id = risen.entity.get("RT_Gate"):id(), work = gate_work:id() },
                guards = guards_near(),
            }
            risen.save.set("exp", exp)
            T.expect("saved", save_game())
            T.sleep(100)
            risen.world.quick_load()
            T.wait_event("session.start", 120, function(m) return m == "load" end)
            wait_gate()
            P0.x, P0.y, P0.z = assert(risen.entity.player()):position()
            check_loaded(exp, "loaded")
        end, "routines")
        return
    end

    -- ===================================================================================================================================================
    if phase == "load" then
        T.plan {
            "loaded (fresh process)", "fresh: the NPCs are back (same entity IDs)", "fresh: the guard still has its post (same freepoint)", "fresh: the day/night NPC keeps its three points",
            "fresh: the points are saved as freepoints of their kind", "fresh: the guard is at its post and faces its yaw", "fresh: the day/night NPC works at its work point",
            "fresh: the patrol anchors are found by their group name again", "fresh: the patrol anchors have their numbers again", "fresh: the patrol anchors are the saved entities",
            "fresh: every waypoint keeps its freepoint as a child", "fresh: the patroller still works at its first waypoint's freepoint", "fresh: the patroller stands at a waypoint",
            "fresh: the restored patrol sends the patroller to another waypoint", "fresh: the gate keeper is the saved NPC", "fresh: the gate keeper keeps the post made before the save (not made again)",
            "fresh: the gate keeper is at his post", "fresh: no freepoint was made twice",
        }
        T.run(function()
            local mode = T.wait_event("session.start", 180, function(m) return m == "load" end)
            T.eq("loaded (fresh process)", mode, "load")
            wait_gate()
            P0.x, P0.y, P0.z = assert(risen.entity.player()):position()
            check_loaded(risen.save.get("exp"), "fresh")
        end, "routines")
        return
    end

    -- ===================================================================================================================================================
    if phase == "load_changed" then
        T.plan {
            "changed: the gate keeper is the saved NPC", "changed: the old post is gone", "changed: a new post of the new kind was made", "changed: the gate keeper looks at the new point",
            "changed: other routines are untouched", "changed: exactly one freepoint less (the old guard post)",
        }
        T.run(function()
            T.wait_event("session.start", 180, function(m) return m == "load" end)
            wait_gate()
            P0.x, P0.y, P0.z = assert(risen.entity.player()):position()
            local exp = risen.save.get("exp")
            local gate = risen.entity.get("RT_Gate")
            T.expect("changed: the gate keeper is the saved NPC", gate and gate:id() == exp.GATE.id, tostring(gate and gate:id()))
            local gr = gate and risen.npc.get_routine(gate) or {}
            T.expect("changed: the old post is gone", await(function() return by_id(exp.GATE.work) == nil end, 10) and gr.work ~= nil and gr.work:id() ~= exp.GATE.work, tostring(by_id(exp.GATE.work)))
            T.expect("changed: a new post of the new kind was made", gr.work ~= nil and gr.work:get_property("gCInteraction_PS", "UseType") == "gEInteractionUseType_LookAt" and gr.created == 1)
            T.expect("changed: the gate keeper looks at the new point", gate and gr.work and await(function()
                return busy_at(gate, gr.work, 90) and prop(gate, "gCScriptRoutine_PS", "ActionString") == "Think"
            end, 45), gate and tostring(dist(gate, gr.work)))
            local k = by_id(exp.K.id) and risen.npc.get_routine(by_id(exp.K.id))
            local g = by_id(exp.G.id) and risen.npc.get_routine(by_id(exp.G.id))
            T.expect("changed: other routines are untouched", k and g and k.work and k.work:id() == exp.K.work and g.work and g.work:id() == exp.G.work and #sorted_patrol("routines_Patrol_Ring") == 3)
            T.eq("changed: exactly one freepoint less (the old guard post)", guards_near(), exp.guards - 1)
        end, "routines")
        return
    end

    error("unknown phase " .. tostring(phase))
end
