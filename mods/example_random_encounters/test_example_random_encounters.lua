---@diagnostic disable: need-check-nil, param-type-mismatch, cast-local-type, assign-type-mismatch, undefined-field
-- Live scenario `ex_random_encounters` (only loaded in a test run, see main.lua): hand-made spots around the player (two behind his back, one in plain view) feed a test zone that is
-- rolled by the mod's own functions: the spawn, the cooldown, the spot filter, the monsters-near limit, chapter pools, groups, the surprise and the clean-up of leftovers.
local T = risen.test
local M, W, R = risen.math, risen.world, risen.random

return function(Enc)
    local function f(...)
        local t = {}
        for i = 1, select("#", ...) do
            local v = select(i, ...)
            t[#t + 1] = type(v) == "number" and string.format("%.1f", v) or tostring(v)
        end
        return table.concat(t, ", ")
    end
    local function await(pred, seconds)
        for _ = 1, math.floor(seconds * 8) do
            if pred() then return true end
            T.sleep(10)
        end
        return pred() and true or false
    end

    T.plan {
        "the data tables are valid (zones, pools, categories)", "the camera has settled behind the player", "a spot in plain view is never used", "a hidden spot behind him: the monster appears there",
        "the monster is one of the pool's templates for the chapter", "the category sleeps for its cooldown", "a spot with enough monsters around it is not used", "chance 0 never spawns",
        "chapter 1 never yields a chapter 2 monster, chapter 4 does", "a group entry spawns 2 monsters around the spot", "a surprise comes from the nearest hidden spot", "far-away unseen leftovers are cleaned up",
        "a zone bound to an area is silent outside it",
    }

    T.run(function()
        T.wait_event("session.start", 180, function(m) return m == "new" end)
        T.sleep(200)
        local player = risen.entity.player()
        local px, py, pz = player:position()

        -- the shipped tables
        local problems = {}
        for _, z in ipairs(Enc.zones) do
            if not Enc.pools[z.pool] then problems[#problems + 1] = "zone " .. tostring(z.name) .. ": no pool " .. tostring(z.pool) end
            if not Enc.categories[z.category or "small"] then problems[#problems + 1] = "zone " .. tostring(z.name) .. ": no category" end
        end
        for name, pool in pairs(Enc.pools) do
            for _, e in ipairs(pool) do
                if not risen.template.get(e.template) then problems[#problems + 1] = name .. ": unknown template " .. e.template end
            end
        end
        T.expect("the data tables are valid (zones, pools, categories)", #problems == 0 and #Enc.zones >= 3, table.concat(problems, "; "))

        local cam
        local function settled()
            cam = W.camera()
            if not cam then return false end
            local x, y, z = player:position()
            local to = M.direction({ cam.x, cam.y, cam.z }, { x, y + 120, z })
            return to and M.angle_between({ cam.dx, cam.dy, cam.dz }, to) < 20
        end
        T.expect("the camera has settled behind the player", await(settled, 25), cam and f(cam.dx, cam.dy, cam.dz) or "no camera")
        local cy = M.yaw_of({ cam.dx, cam.dy, cam.dz })
        local function ground(x, z)
            local blocked, _, _, hy = W.trace(x, py + 500, z, x, py - 1500, z, player)
            return blocked and hy or py
        end
        local function at(deg, dist)
            local p = M.polar({ px, py, pz }, cy + deg, dist)
            p.y = ground(p.x, p.z)
            return { p.x, p.y, p.z }
        end
        local front, back1, back2 = at(0, 1000), at(180, 900), at(165, 1500)

        local function zone(extra)
            local z = { name = "test", category = "small", pool = "wildlife", chance = 1, min_distance = 500, max_distance = 3000, max_near = 1, near_radius = 600, spots = { back1, back2 } }
            for k, v in pairs(extra or {}) do z[k] = v end
            return z
        end
        local function try(z, surprise)
            local npcs = risen.entity.npcs()
            return Enc.try_zone(z, player, 1, npcs, surprise or false)
        end
        local function kill_all(list) for _, e in ipairs(list or {}) do if e:valid() then e:kill() end end end
        Enc.settings.max_alive = 20

        -- a spot in plain view
        Enc.reset()
        local made, why = try(zone({ spots = { front } }))
        T.expect("a spot in plain view is never used", made == nil and why == "no hidden spot", tostring(why))

        -- a hidden spot
        Enc.reset()
        local m1, spot = try(zone({ spots = { back1 } }))
        T.expect("a hidden spot behind him: the monster appears there", m1 and #m1 == 1 and M.distance2d(m1[1], spot) < 60 and M.distance2d(spot, back1) < 5,
            m1 and f(M.distance2d(m1[1], spot)) or tostring(spot))
        local t = m1 and m1[1]:template()
        local ok = false
        for _, e in ipairs(Enc.pools.wildlife) do if t and e.template == t:name() and (not e.chapters or e.chapters[1] <= 1) then ok = true end end
        T.expect("the monster is one of the pool's templates for the chapter", ok, t and t:name() or "nil")
        if m1 then m1[1]:set_frozen(true) end   -- stays where it is: the monster limit around the spot is tested below
        T.sleep(20)

        -- cooldown
        local again, why2 = try(zone())
        T.expect("the category sleeps for its cooldown", again == nil and why2 == "cooldown" and Enc.cooldown_left("small") > 200, tostring(why2) .. " " .. f(Enc.cooldown_left("small")))

        -- monsters near the only spot
        Enc.reset()
        local one = zone({ spots = { back1 } })
        local crowded, why3 = try(one)
        T.expect("a spot with enough monsters around it is not used", crowded == nil and why3 == "no hidden spot", tostring(why3))
        kill_all(m1)
        T.sleep(30)

        -- chance 0
        Enc.reset()
        Enc.cfg.chance_scale = 0
        local none, why4 = try(zone())
        Enc.cfg.chance_scale = 1
        T.expect("chance 0 never spawns", none == nil and why4 == "chance", tostring(why4))

        -- chapter pools
        R.seed(11)
        local seen1, seen4 = {}, {}
        for _ = 1, 300 do
            seen1[Enc.pick_entry("wildlife", 1).template] = true
            seen4[Enc.pick_entry("wildlife", 4).template] = true
        end
        T.expect("chapter 1 never yields a chapter 2 monster, chapter 4 does", not seen1.GiantBug and not seen1.Rotworm and seen1.Wolf and seen4.GiantBug and seen4.Rotworm and not seen4.Stingrat,
            f(seen1.GiantBug, seen4.GiantBug, seen4.Stingrat))

        -- a group
        Enc.reset()
        Enc.pools.test_group = { { template = "Wolf_Black", count = 2 } }
        local grp = try(zone({ pool = "test_group", max_near = 5 }))
        T.expect("a group entry spawns 2 monsters around the spot", grp and #grp == 2 and grp[1]:id() ~= grp[2]:id(), grp and #grp or "nil")
        local leftovers = grp

        -- surprise: the nearest hidden spot
        Enc.reset()
        local far_first = zone({ spots = { back2, back1 }, max_near = 9 })
        local sm, sspot = try(far_first, true)
        T.expect("a surprise comes from the nearest hidden spot", sm and M.distance2d(sspot, back1) < 5, sspot and f(M.distance2d(sspot, back1), M.distance2d(sspot, back2)) or tostring(sspot))
        local all = {}
        for _, e in ipairs(m1 or {}) do all[#all + 1] = e end
        for _, e in ipairs(leftovers or {}) do all[#all + 1] = e end
        for _, e in ipairs(sm or {}) do all[#all + 1] = e end

        -- clean-up: the player is far from them now (distance limit lowered), they are behind him
        local keep = Enc.settings.despawn_distance
        Enc.settings.despawn_distance = 300
        local n = Enc.cleanup()
        Enc.settings.despawn_distance = keep
        T.sleep(30)   -- the engine destroys them at the end of the frame
        local gone = 0
        for _, e in ipairs(all) do if not e:valid() then gone = gone + 1 end end
        T.expect("far-away unseen leftovers are cleaned up", n == 3 and gone == #all and Enc.alive() == 0, f(n, gone, Enc.alive(), #all))
        kill_all(all)

        -- area-bound zone
        Enc.reset()
        local outside, why5 = try(zone({ area = { x = px + 20000, z = pz, radius = 1000 } }))
        T.expect("a zone bound to an area is silent outside it", outside == nil and why5 == "outside area", tostring(why5))

        T.finish("random encounters")
    end, "ex_random_encounters")
end
