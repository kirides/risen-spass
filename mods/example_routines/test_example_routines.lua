---@diagnostic disable: need-check-nil, param-type-mismatch, cast-local-type, assign-type-mismatch
-- Live scenario `example_routines` (only loaded in a test run, see main.lua): the gatekeeper's day, evening and night, the patrol's ring, a quick save and load.
local T = risen.test

return function()
    local function dist(a, b) return (a and b) and a:distance_to(b) or 1e9 end
    local function await(pred, seconds)
        for _ = 1, math.floor(seconds * 8) do
            if pred() then return true end
            T.sleep(10)
        end
        return pred() and true or false
    end
    local function action(e) return e and e:get_property("gCScriptRoutine_PS", "ActionString") end
    local function ani(e) return e and e:get_property("gCScriptRoutine_PS", "AniState") end
    -- standing at a point and busy with it (the engine's interaction state; before that the NPC walks in "ZS_Interact_PreInteract")
    local function busy_at(npc, point, r) return npc and point and dist(npc, point) < (r or 90) and npc:state() == "ZS_Interact_Interact" end
    local function ring()
        local list = risen.world.points({ template = "Anchor_Patrol", name = "example_routines_Patrol_Camp", limit = 20 }) or {}
        table.sort(list, function(a, b) return (a.index or 0) < (b.index or 0) end)
        return list
    end
    local function waypoint_near(npc)
        for _, a in ipairs(ring()) do if dist(npc, a.entity) < 110 then return a.index + 1 end end
        return nil
    end
    local function freepoints() return #risen.world.points({ near = risen.entity.player(), radius = 3500, kind = "guard", limit = 5000 }) end

    T.plan {
        "both NPCs exist", "the routines were applied (freepoints, a patrol of three)", "day: the gatekeeper stands guard at his post", "day: facing east", "evening: he looks at the fire",
        "night: he sleeps on the ground", "morning: back at the post", "the patrol walks to a waypoint", "the patrol goes on to the next waypoint", "and to the one after", "and round again",
        "saved", "loaded: the same NPCs", "loaded: the same posts (nothing was made twice)", "loaded: the gatekeeper is at his post again", "loaded: the patrol anchors are named again",
        "loaded: the patrol goes on to another waypoint",
    }
    T.run(function()
        T.wait_event("session.start", 180, function(m) return m == "new" end)
        T.wait_until(function() return risen.entity.get("EX_Gatekeeper") ~= nil and risen.entity.get("EX_Patrol") ~= nil end, 60, "the NPCs exist")
        T.sleep(60)
        local guard, patrol = risen.entity.get("EX_Gatekeeper"), risen.entity.get("EX_Patrol")
        T.expect("both NPCs exist", guard and patrol)
        local g, p = risen.npc.get_routine(guard), risen.npc.get_routine(patrol)
        local group = ring()
        T.expect("the routines were applied (freepoints, a patrol of three)", g.work and g.relax and g.sleep and g.created == 3 and #group == 3 and p.patrol == "Camp" and p.work ~= nil,
            ("%s %s %s created %d, patrol anchors %d"):format(tostring(g.work), tostring(g.relax), tostring(g.sleep), g.created, #group))

        -- ---- the gatekeeper's day ----
        T.section("day and night")
        risen.world.set_time(12, 0)
        risen.npc.apply_routine(guard)
        T.expect("day: the gatekeeper stands guard at his post", await(function() return busy_at(guard, g.work) and action(guard) == "HandsOnHips" end, 45),
            tostring(dist(guard, g.work)) .. " " .. tostring(action(guard)))
        T.expect("day: facing east", await(function() return select(1, guard:direction()) > 0.9 end, 15), tostring(select(1, guard:direction())))
        risen.world.set_time(21, 0)
        risen.npc.apply_routine(guard)
        T.expect("evening: he looks at the fire", await(function() return busy_at(guard, g.relax) and action(guard) == "Think" end, 60), tostring(dist(guard, g.relax)) .. " " .. tostring(action(guard)))
        risen.world.set_time(3, 0)
        risen.npc.apply_routine(guard)
        T.expect("night: he sleeps on the ground", await(function() return busy_at(guard, g.sleep) and ani(guard) == "gEAniState_SleepGround" end, 60),
            tostring(dist(guard, g.sleep)) .. " " .. tostring(ani(guard)))
        risen.world.set_time(12, 0)
        risen.npc.apply_routine(guard)
        T.expect("morning: back at the post", await(function() return busy_at(guard, g.work) end, 90), tostring(dist(guard, g.work)))

        -- ---- the patrol ----
        T.section("patrol")
        local function at_waypoint() local n = waypoint_near(patrol) return n ~= nil and patrol:state() == "ZS_Interact_Interact" and n or nil end
        T.expect("the patrol walks to a waypoint", await(function() return at_waypoint() ~= nil end, 60), tostring(waypoint_near(patrol)))
        local cur, order = at_waypoint(), {}
        order[1] = cur
        for _, name in ipairs { "the patrol goes on to the next waypoint", "and to the one after", "and round again" } do
            if not cur then break end
            T.sleep(120)    -- (an NPC that has just arrived is still travelling for a moment and ignores apply_routine)
            risen.npc.apply_routine(patrol)
            local want = cur % 3 + 1
            local arrived = await(function() return at_waypoint() == want end, 60)
            cur = arrived and want or nil
            order[#order + 1] = cur or "missed"
            T.expect(name, arrived, table.concat({ tostring(order[1]), tostring(order[2]), tostring(order[3]), tostring(order[4]) }, ">"))
        end

        -- ---- save, load ----
        T.section("save and load")
        local exp = { guard = guard:id(), patrol = patrol:id(), work = g.work:id(), relax = g.relax:id(), sleep = g.sleep:id(), points = freepoints() }
        T.wait_until(function() return risen.world.is_save_allowed() end, 30, "saving allowed")
        local saved = false
        risen.on("save.after", function(_, ok) saved = ok end)
        risen.world.quick_save()
        T.wait_until(function() return saved end, 60, "savegame written")
        T.expect("saved", saved)
        T.sleep(100)
        risen.world.quick_load()
        T.wait_event("session.start", 120, function(m) return m == "load" end)
        T.wait_until(function() return risen.entity.get("EX_Gatekeeper") ~= nil and risen.entity.get("EX_Patrol") ~= nil end, 60, "the NPCs exist again")
        T.sleep(100)
        guard, patrol = risen.entity.get("EX_Gatekeeper"), risen.entity.get("EX_Patrol")
        T.expect("loaded: the same NPCs", guard and patrol and guard:id() == exp.guard and patrol:id() == exp.patrol)
        local g2 = risen.npc.get_routine(guard)
        T.eq("loaded: the same posts (nothing was made twice)", { g2.work:id(), g2.relax:id(), g2.sleep:id(), freepoints() }, { exp.work, exp.relax, exp.sleep, exp.points })
        T.expect("loaded: the gatekeeper is at his post again", await(function() return busy_at(guard, g2.work) end, 45), tostring(dist(guard, g2.work)))
        local anchors = ring()
        T.expect("loaded: the patrol anchors are named again", #anchors == 3 and anchors[1].index == 0 and anchors[3].index == 2, tostring(#anchors))
        local first = await(function() return waypoint_near(patrol) ~= nil and patrol:state() == "ZS_Interact_Interact" end, 60) and waypoint_near(patrol)
        T.sleep(120)
        risen.npc.apply_routine(patrol)
        T.expect("loaded: the patrol goes on to another waypoint", first ~= nil and await(function() local n = waypoint_near(patrol) return n ~= nil and n ~= first end, 60), tostring(first))
    end, "example_routines")
end
