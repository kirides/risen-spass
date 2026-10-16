---@diagnostic disable: need-check-nil, param-type-mismatch, cast-local-type, assign-type-mismatch, undefined-field
-- Live scenario `ex_unseen_spawn` (only loaded in a test run, see main.lua): five hand-made points around the player (one in plain view, one hidden behind a hill or rock in front of him,
-- two behind his back, one at his side) and the example's two policies, the random pick, and the ambush itself.
local T = risen.test
local M, W, R = risen.math, risen.world, risen.random

return function(ex)
    local function f(...)
        local t = {}
        for i = 1, select("#", ...) do
            local v = select(i, ...)
            t[#t + 1] = type(v) == "number" and string.format("%.2f", v) or tostring(v)
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
        "the camera has settled behind the player", "five test points are placed", "the point in plain view is seen, the others are not", "behind the player: only the two points at his back",
        "unseen: every point he cannot see, never the visible one", "random picks: only qualifying points, both of the two behind him show up", "unseen picks cover all four hidden points and never the visible one",
        "a seeded stream makes the picks repeatable", "ambush spawns the enemy at a hidden point behind him", "the enemy is neither seen nor in front", "nothing qualifies: no spawn, a reason", "the game's own points are used when no list is given", "ambush with the world's freepoints",
    }

    T.run(function()
        T.wait_event("session.start", 180, function(m) return m == "new" end)
        T.sleep(200)
        local player = risen.entity.player()
        local px, py, pz = player:position()

        -- the third-person camera flies in during the first seconds of a new game: wait until it looks at the player
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
            return p
        end
        -- B: inside the camera's cone, but hidden: look along directions up to 35 degrees to either side for an obstacle that stops a ray at eye level 500-1500 units away, and take
        -- a point behind it that the player cannot see (the ground behind a hill may rise again, so several distances are tried)
        local opts = { camera = true, range = ex.sight }
        local function seen(p) return W.is_seen_by({ p.x, p.y + 100, p.z }, player, opts) end
        local hidden
        for off = 0, 35, 5 do
            for _, sign in ipairs { 1, -1 } do
                local deg = off * sign
                local r = W.raycast({ px, py + 165, pz }, M.polar({ px, py + 165, pz }, cy + deg, 2000), { ignore = player })
                if not hidden and r.blocked and r.distance > 500 and r.distance < 1500 then
                    for _, extra in ipairs { 150, 300, 450, 600, 800 } do
                        local p = at(deg, r.distance + extra)
                        if not hidden and M.distance2d(p, player) < ex.max - 100 and seen(p) == false then hidden = p end
                    end
                end
            end
        end
        local A, B, C, D, E = at(0, 900), hidden, at(180, 900), at(160, 1100), at(90, 900)
        T.expect("five test points are placed", B ~= nil, "no obstacle in view" .. (B and (" B at " .. f(B.x, B.y, B.z)) or ""))
        if not B then
            T.finish("no hidden point could be made")
            return
        end

        local sA, sB, sC, sD, sE = seen(A), seen(B), seen(C), seen(D), seen(E)
        T.expect("the point in plain view is seen, the others are not", sA == true and sB == false and sC == false and sD == false and sE == false, f(sA, sB, sC, sD, sE))

        local function names(list)
            local out = {}
            for _, p in ipairs(list) do
                for label, q in pairs { A = A, B = B, C = C, D = D, E = E } do if q == p then out[#out + 1] = label end end
            end
            table.sort(out)
            return table.concat(out)
        end
        local all = { A, B, C, D, E }
        local behind = ex.candidates(player, { points = all })
        T.expect("behind the player: only the two points at his back", behind and names(behind) == "CD", behind and names(behind) or "nil")
        local unseen = ex.candidates(player, { points = all, mode = "unseen" })
        T.expect("unseen: every point he cannot see, never the visible one", unseen and names(unseen) == "BCDE", unseen and names(unseen) or "nil")

        -- random picks
        R.seed(77)
        local counts = { C = 0, D = 0, other = 0 }
        for _ = 1, 60 do
            local p = ex.pick(player, { points = all })
            local label = p == C and "C" or p == D and "D" or "other"
            counts[label] = counts[label] + 1
        end
        T.expect("random picks: only qualifying points, both of the two behind him show up", counts.other == 0 and counts.C > 10 and counts.D > 10, f(counts.C, counts.D, counts.other))
        local covered = {}
        local never_a = true
        for _ = 1, 120 do
            local p = ex.pick(player, { points = all, mode = "unseen" })
            if p == A then never_a = false end
            covered[names({ p })] = true
        end
        T.expect("unseen picks cover all four hidden points and never the visible one", never_a and covered.B and covered.C and covered.D and covered.E, f(never_a, covered.B, covered.C, covered.D, covered.E))
        R.seed(5)
        local run1 = {}
        for i = 1, 20 do run1[i] = names({ ex.pick(player, { points = all, mode = "unseen" }) }) end
        R.seed(5)
        local run2 = {}
        for i = 1, 20 do run2[i] = names({ ex.pick(player, { points = all, mode = "unseen" }) }) end
        T.expect("a seeded stream makes the picks repeatable", table.concat(run1) == table.concat(run2) and #table.concat(run1) == 20, table.concat(run1) .. " / " .. table.concat(run2))

        -- the ambush
        local enemy, point = ex.ambush(player, { points = all })
        T.expect("ambush spawns the enemy at a hidden point behind him", enemy and (point == C or point == D) and M.distance(enemy, { point.x, point.y + 5, point.z }) < 60,
            enemy and f(M.distance(enemy, { point.x, point.y, point.z })) or tostring(point))
        if enemy then
            T.sleep(10)
            local cam2 = W.camera()
            T.expect("the enemy is neither seen nor in front", W.is_seen_by(enemy, player, { camera = true, range = ex.sight }) == false and M.is_behind(enemy, player, { cam2.dx, cam2.dy, cam2.dz }, ex.margin),
                f(M.relative_angle(enemy, player, { cam2.dx, cam2.dy, cam2.dz })))
            risen.script.call("Kill", player, enemy, 0)
        else
            T.expect("the enemy is neither seen nor in front", false, "no enemy")
        end
        -- nothing hidden: only the visible point to choose from
        local before = risen.template.census().Wolf or 0
        local none, why = ex.ambush(player, { points = { A } })
        T.expect("nothing qualifies: no spawn, a reason", none == nil and type(why) == "string" and (risen.template.census().Wolf or 0) == before, tostring(why))
        -- the same five places as real freepoints of the world: no list given, the example asks risen.world.points itself
        local made = {}
        for _, p in ipairs(all) do made[#made + 1] = W.spawn_point("FP_Guard", p.x, p.y + 2, p.z, 0) end
        T.sleep(10)
        local listed = ex.candidates(player)
        local function which(p)
            for label, q in pairs { A = A, B = B, C = C, D = D, E = E } do if M.distance2d(p, q) < 10 then return label end end
            return "?"
        end
        local got = {}
        for _, p in ipairs(listed or {}) do got[#got + 1] = which(p) end
        table.sort(got)
        T.expect("the game's own points are used when no list is given", listed and table.concat(got) == "CD", table.concat(got))
        local enemy2, point2 = ex.ambush(player)
        T.expect("ambush with the world's freepoints", enemy2 and (which(point2) == "C" or which(point2) == "D") and M.distance2d(enemy2, point2) < 60, enemy2 and which(point2) or tostring(point2))
        if enemy2 then risen.script.call("Kill", player, enemy2, 0) end
        for _, e in ipairs(made) do if e then e:kill() end end
    end, "ex_unseen_spawn")
end
