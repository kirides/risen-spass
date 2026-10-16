---@diagnostic disable: need-check-nil, param-type-mismatch, cast-local-type, assign-type-mismatch, undefined-field
-- Live scenario `geometry` (only loaded in a test run, see main.lua): vectors and the engine's yaw convention against real entities, the engine's own ray test
-- (open air, terrain, objects, characters), the field of view compared with the AI's `CanSee`, the player's camera, random streams and a quick save + load.
--
--   .\scripts\test-run.ps1 -Instance 1 -Mods geometry -WaitForResult -Seconds 400 -Scenario geometry -ShowChecks
local T = risen.test
local M, W = risen.math, risen.world

return function()
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
    local function name_of(e) return e and e:name() or "-" end

    T.plan {
        -- conventions
        "player exists with a box taller than 150 (y is up)", "a ray straight down lands on the ground at the player's feet", "freepoints: direction() = from_yaw(yaw) for 0 45 90 180 270",
        "freepoints: yaw_of(entity) = the yaw it was made with", "a guard NPC faces the yaw of his post (yaw_of)",
        -- line of sight
        "open air is clear", "the sky above is clear", "terrain blocks (a ray into the ground)", "an object blocks, the engine names it", "the surface of an object: just before is clear, just behind is blocked",
        "a hill or object between two points blocks (2 of 12 directions at least)", "line_of_sight = raycast.blocked", "a character does not block by default", "a character blocks with bodies = true", "ignore removes the blocker",
        "entity to entity agrees with the engine's IsFreeLOS in the open", "entity to entity behind an object agrees with the engine's IsFreeLOS", "behind an object the engine's CanSee says no, and so does can_see (the object is the reason)", "beside the object both say yes",
        -- field of view
        "two guards at their posts", "can_see agrees with the engine's CanSee in all six directions", "in front is seen, behind is not", "a narrow field of view", "range", "fov 360", "a viewer table with a yaw",
        "a wall hides a target in front of the viewer", "is_seen_by: the viewer who looks at the point is returned", "is_seen_by: nobody looks at it", "is_seen_by skips dead viewers",
        -- camera
        "the camera is on the player's side", "the camera direction is a unit vector", "the camera direction looks at the player", "can_see with camera = true follows the camera", "the engine's camera fov",
        -- random
        "a seeded stream repeats", "the stream of the mod is seedable", "unseeded streams differ", "math.random starts from a fixed state",
        -- save and load
        "saved", "loaded: the stream continues the saved sequence", "loaded: the ray test works in the new world", "loaded: the camera works", "loaded: clear stays clear and the wall stays a wall",
    }

    T.run(function()
        T.wait_event("session.start", 180, function(m) return m == "new" end)
        T.sleep(200)
        local player = risen.entity.player()
        local px, py, pz = player:position()
        local function ground(x, z)
            local blocked, _, _, hy = W.trace(x, py + 500, z, x, py - 1500, z, player)
            return blocked and hy or nil
        end
        local eye = py + 165

        -- ---------------------------------------------------------------------------------------------------------------------------------------------
        T.section("conventions")
        local x0, y0, z0, x1, y1, z1 = player:bounds()
        T.expect("player exists with a box taller than 150 (y is up)", player and y1 and (y1 - y0) > 150 and (y1 - y0) > 0.4 * math.max(x1 - x0, z1 - z0), f(x0, y0, z0, x1, y1, z1))
        local down, ddist, _, dy = W.trace(px, py + 150, pz, px, py - 1000, pz, player)
        T.expect("a ray straight down lands on the ground at the player's feet", down and math.abs(ddist - 150) < 5 and math.abs(dy - py) < 5, f(down, ddist, dy, py))

        local ok45 = true
        local detail = {}
        for _, yaw in ipairs { 0, 45, 90, 180, 270 } do
            local fp = W.spawn_point("FP_Guard", px + 100, py + 5, pz + 100, yaw)
            T.sleep(2)
            local dx, ddy, dz = fp:direction()
            local v = M.from_yaw(yaw)
            local good = dx and math.abs(dx - v.x) < 1e-3 and math.abs(ddy - v.y) < 1e-3 and math.abs(dz - v.z) < 1e-3
            local yv = M.yaw_of(fp)
            local good2 = yv and math.abs(M.angle_diff(yv, yaw)) < 0.1
            detail[#detail + 1] = ("%d: dir (%s) from_yaw (%s) yaw_of %s"):format(yaw, f(dx, ddy, dz), f(v.x, v.y, v.z), f(yv))
            ok45 = ok45 and good and good2
            fp:kill()
        end
        T.expect("freepoints: direction() = from_yaw(yaw) for 0 45 90 180 270", ok45, table.concat(detail, " | "))
        T.expect("freepoints: yaw_of(entity) = the yaw it was made with", ok45)

        -- ---------------------------------------------------------------------------------------------------------------------------------------------
        T.section("line of sight")
        -- open air: toward the sea in the south-west (the beach is open there, 2500 units)
        local out = M.polar({ px, eye, pz }, 210, 900)
        local clear, info = W.line_of_sight({ px, eye, pz }, out)
        T.expect("open air is clear", clear == true and not info.blocked and math.abs(info.distance - 900) < 1, f(clear, info and info.distance))
        local up = W.line_of_sight({ px, eye, pz }, { px, eye + 3000, pz })
        T.expect("the sky above is clear", up == true)
        local into = W.raycast({ px, eye, pz }, { px, py - 300, pz }, {})
        T.expect("terrain blocks (a ray into the ground)", into and into.blocked and math.abs(into.distance - (eye - py)) < 6 and into.entity and name_of(into.entity):find("^Levelmesh") ~= nil,
            into and (f(into.distance) .. " by " .. name_of(into.entity)))

        -- fan of rays at eye height: which directions are blocked, and by what
        local fan, first_obj, first_obj_deg = {}, nil, nil
        for deg = 0, 345, 15 do
            local r = W.raycast({ px, eye, pz }, M.polar({ px, eye, pz }, deg, 1800), { ignore = player })
            fan[#fan + 1] = { deg = deg, hit = r }
            if r.blocked and r.entity and name_of(r.entity):find("^Obj_") and r.distance > 300 and not first_obj then first_obj = r first_obj_deg = deg end
        end
        local blocked_dirs, names = 0, {}
        for _, e in ipairs(fan) do if e.hit.blocked then blocked_dirs = blocked_dirs + 1 names[#names + 1] = e.deg .. ":" .. name_of(e.hit.entity):sub(1, 22) .. "@" .. math.floor(e.hit.distance) end end
        risen.log.info("geometry: fan blocked " .. blocked_dirs .. " of " .. #fan .. ": " .. table.concat(names, " "))
        T.expect("a hill or object between two points blocks (2 of 12 directions at least)", blocked_dirs >= 2 and blocked_dirs <= #fan - 2, blocked_dirs .. " of " .. #fan)
        T.expect("an object blocks, the engine names it", first_obj ~= nil, first_obj and (first_obj_deg .. " " .. name_of(first_obj.entity)))
        local wall_a, wall_b, wall_dir
        if first_obj then
            local d = M.direction(first_obj.from, first_obj.to)
            local hit = M.vec(first_obj.x, first_obj.y, first_obj.z)
            local before = M.sub(hit, M.scale(d, 3))
            local after = M.add(hit, M.scale(d, 3))
            local c1 = W.line_of_sight(first_obj.from, before, { ignore = player })
            local c2, h2 = W.line_of_sight(first_obj.from, after, { ignore = player })
            T.expect("the surface of an object: just before is clear, just behind is blocked", c1 == true and c2 == false and h2 and h2.entity == first_obj.entity, f(c1, c2, name_of(h2 and h2.entity)))
            wall_dir = d
            wall_a = M.sub(hit, M.scale(d, 250))
            wall_b = M.add(hit, M.scale(d, 250))
        else
            T.expect("the surface of an object: just before is clear, just behind is blocked", false, "no object found in the fan")
        end
        local same, hitinfo = W.line_of_sight({ px, eye, pz }, M.polar({ px, eye, pz }, 90, 900), { ignore = player })
        local raw = W.raycast({ px, eye, pz }, M.polar({ px, eye, pz }, 90, 900), { ignore = player })
        T.expect("line_of_sight = raycast.blocked", same == not raw.blocked and hitinfo.blocked == raw.blocked)

        -- a character in the way
        local bx, bz = px + 700, pz + 250
        local by = ground(bx, bz) or py
        local body = risen.npc.spawn("Peasant_1_01", bx, by + 5, bz, { peaceful = true, name = "GEO_Body" })
        T.sleep(60)
        local bpx, bpy, bpz = body:position()
        local a_pt, b_pt = { bpx - 300, bpy + 100, bpz }, { bpx + 300, bpy + 100, bpz }
        local c_default = W.line_of_sight(a_pt, b_pt)
        local c_bodies, hb = W.line_of_sight(a_pt, b_pt, { bodies = true })
        local c_ign = W.line_of_sight(a_pt, b_pt, { bodies = true, ignore = body })
        T.expect("a character does not block by default", c_default == true, tostring(c_default))
        T.expect("a character blocks with bodies = true", c_bodies == false and hb and hb.entity == body, f(c_bodies, name_of(hb and hb.entity)))
        T.expect("ignore removes the blocker", c_ign == true)
        body:kill()

        -- ---------------------------------------------------------------------------------------------------------------------------------------------
        T.section("entity pairs against the engine's IsFreeLOS")
        -- two NPCs in the open, two on the two sides of the object
        local function engine_can_see(a, b) return risen.script.call("CanSee", a, b) ~= 0 end
        local function spawn_npc(name, x, z, tpl)
            local y = ground(x, z) or py
            return risen.npc.spawn(tpl or "Peasant_1_01", x, y + 5, z, { peaceful = true, name = name })
        end
        local o1 = spawn_npc("GEO_O1", px + 400, pz + 300)
        local o2 = spawn_npc("GEO_O2", px + 700, pz + 500, "Peasant_1_02")
        local w1, w2, w3
        if wall_a then
            w1 = spawn_npc("GEO_W1", wall_a.x, wall_a.z)
            w2 = spawn_npc("GEO_W2", wall_b.x, wall_b.z, "Peasant_1_02")
            local yaw_wall = M.yaw_to(wall_a, wall_b)
            for _, spot in ipairs { { 300, 450 }, { 300, -450 }, { 500, 800 }, { 500, -800 } } do
                if not w3 then
                    local p = M.offset(wall_a, yaw_wall, spot[1], spot[2])
                    if W.line_of_sight(wall_a, { p.x, wall_a.y + 150, p.z }) == true then w3 = spawn_npc("GEO_W3", p.x, p.z, "Peasant_1_03") end
                end
            end
        end
        -- freshly made NPCs drop onto the ground and shuffle about: wait until every one of them stands still
        local function settle(list)
            local last
            for _ = 1, 30 do
                T.sleep(10)
                local now = {}
                for i, e in ipairs(list) do local x, y, z = e:position() now[i] = { x, y, z } end
                local still = last ~= nil
                for i, p in ipairs(now) do
                    if not last or not last[i] or M.distance(p, last[i]) > 2 then still = false end
                end
                if still then return true end
                last = now
            end
            return false
        end
        local group = { o1, o2, w1, w2, w3 }
        local settled_all = settle(group)
        risen.log.info("geometry: NPCs settled: " .. tostring(settled_all))
        local free_open = W.line_of_sight(o1, o2)
        local engine_open = W.engine_free_los(o1, o2)
        T.expect("entity to entity agrees with the engine's IsFreeLOS in the open", free_open == true and engine_open == true, f(free_open, engine_open))
        if w1 and w2 then
            local mine = W.line_of_sight(w1, w2)
            local engine = W.engine_free_los(w1, w2)
            local engine_rev = W.engine_free_los(w2, w1)
            local p1, p2 = { w1:position() }, { w2:position() }
            risen.log.info(("geometry: wall pair %s (%s) -> %s (%s): mine %s engine %s/%s"):format(name_of(w1), f(unpack(p1)), name_of(w2), f(unpack(p2)), tostring(mine), tostring(engine), tostring(engine_rev)))
            T.expect("entity to entity behind an object agrees with the engine's IsFreeLOS", mine == engine, f(mine, engine, engine_rev))
        else
            T.expect("entity to entity behind an object agrees with the engine's IsFreeLOS", false, "no object")
        end
        -- the AI's own verdict, at the same moment (idle NPCs wander): a viewer who looks straight at a target behind the object (inside his field of view, the object in between)
        -- does not see it; beside the object he does
        if w1 and w2 then
            local facing_wall = M.in_cone(w2, w1, w1, 120) == true
            local engine_wall, mine_wall, why_wall = engine_can_see(w1, w2), W.can_see(w1, w2)
            local _, why = W.can_see(w1, w2)
            T.expect("behind an object the engine's CanSee says no, and so does can_see (the object is the reason)", facing_wall and engine_wall == false and mine_wall == false and why == "blocked",
                f(facing_wall, engine_wall, mine_wall, why))
            if w3 then
                local in_view = M.in_cone(w3, w1, w1, 150) == true
                local engine_side, mine_side = engine_can_see(w1, w3), W.can_see(w1, w3)
                T.expect("beside the object both say yes", in_view and engine_side and mine_side == true, f(in_view, engine_side, mine_side))
            else
                T.expect("beside the object both say yes", false, "no free spot beside the object")
            end
        else
            T.expect("behind an object the engine's CanSee says no, and so does can_see (the object is the reason)", false, "no object")
            T.expect("beside the object both say yes", false, "no object")
        end
        for _, e in ipairs { o1, o2, w1, w2, w3 } do if e then e:kill() end end

        -- ---------------------------------------------------------------------------------------------------------------------------------------------
        T.section("field of view against the engine's CanSee")
        -- two guards: V looks along +z (yaw 0), W along -x (yaw 270) from 600 units to the east of him
        local function guard(name, x, z, yaw, tpl)
            -- the post is the walkable point nearest to (x, z); the NPC starts 220 units away from it and walks there (he turns to the point's yaw on arrival, not when he
            -- is made at the post)
            local y = ground(x, z) or py
            local pt = risen.world.nav_point(x, y, z, 600) or { x, y, z }
            local sx, sz = pt[1] + 220, pt[3] + 120
            local npc = risen.npc.spawn(tpl or "Peasant_1_01", sx, (ground(sx, sz) or pt[2]) + 5, sz, { peaceful = true, name = name })
            local r, why = risen.npc.set_routine(npc, { at = { pt[1], pt[2], pt[3], kind = "guard", yaw = yaw } })
            if not r then risen.log.warn("geometry: guard routine failed: " .. tostring(why)) end
            return npc
        end
        local gx, gz = px + 250, pz + 150
        local V = guard("GEO_V", gx, gz, 0)
        local Wk = guard("GEO_W", gx + 600, gz - 100, 270, "Peasant_1_02")
        local function at_post(npc, yaw)
            return npc:state() == "ZS_Interact_Interact" and math.abs(M.angle_diff(M.yaw_of(npc) or 999, yaw)) < 5
        end
        local ready = await(function() return at_post(V, 0) and at_post(Wk, 270) end, 90)
        local function post_info(npc)
            local rp = risen.npc.get_routine(npc)
            local post = rp and rp.work
            return post and ("post (%s) dir (%s) dist %s"):format(f(post:position()), f(post:direction()), f(npc:distance_to(post))) or "no post"
        end
        T.expect("two guards at their posts", ready, f(V:state(), M.yaw_of(V), Wk:state(), M.yaw_of(Wk)) .. " | " .. post_info(V) .. " | " .. post_info(Wk))
        T.expect("a guard NPC faces the yaw of his post (yaw_of)", ready)
        local vx, vy, vz = V:position()

        -- six targets around V (the angles are 80 / 100 degrees off: clear of the 90 degree edge)
        local spots = {
            { "front", 0, 350 }, { "back", 0, -350 }, { "right-front", 350, 60 }, { "left-front", -350, 60 }, { "right-back", 350, -60 }, { "left-back", -350, -60 },
        }
        local targets = {}
        for i, s in ipairs(spots) do
            local p = M.offset({ vx, vy, vz }, 0, s[3], s[2]) -- forward, right: yaw 0 looks along +z, its right is +x
            targets[i] = spawn_npc("GEO_T" .. i, p.x, p.z, "Peasant_1_0" .. (i % 3 + 1))
        end
        settle(targets)
        local agree, pattern, lines = true, true, {}
        local want = { true, false, true, true, false, false }
        for i, s in ipairs(spots) do
            local mine = W.can_see(V, targets[i]) == true
            local theirs = engine_can_see(V, targets[i])
            lines[#lines + 1] = ("%s mine %s engine %s"):format(s[1], tostring(mine), tostring(theirs))
            agree = agree and mine == theirs
            pattern = pattern and mine == want[i]
        end
        T.expect("can_see agrees with the engine's CanSee in all six directions", agree, table.concat(lines, "; "))
        T.expect("in front is seen, behind is not", pattern, table.concat(lines, "; "))
        local rf = targets[3]
        local narrow = W.can_see(V, rf, { fov = 100 })    -- 80 degrees off: outside +-50
        local wide = W.can_see(V, rf, { fov = 170 })      -- inside +-85
        local fr = W.can_see(V, targets[1], { fov = 20 })
        T.expect("a narrow field of view", narrow == false and wide == true and fr == true, f(narrow, wide, fr))
        local near_ok = W.can_see(V, targets[1], { range = 400 })
        local far, why_far = W.can_see(V, targets[1], { range = 300 })
        T.expect("range", near_ok == true and far == false and why_far == "range", f(near_ok, far, why_far))
        T.expect("fov 360", W.can_see(V, targets[2], { fov = 360 }) == true)
        local tp = M.offset({ vx, vy + 120, vz }, 0, 500)
        T.expect("a viewer table with a yaw", W.can_see({ vx, vy + 150, vz, yaw = 0 }, tp) == true and W.can_see({ vx, vy + 150, vz, yaw = 180 }, tp) == false)

        -- a wall hides a target in front of the viewer (pseudo viewer, the object found in the fan)
        if wall_a then
            local yawd = M.yaw_to(wall_a, wall_b)
            local seen_wall, why_wall = W.can_see({ wall_a.x, wall_a.y, wall_a.z, yaw = yawd }, wall_b)
            local seen_side = W.can_see({ wall_a.x, wall_a.y, wall_a.z, yaw = yawd }, M.offset(wall_a, yawd, 250, 0))
            T.expect("a wall hides a target in front of the viewer", seen_wall == false and why_wall == "blocked" and seen_side == true, f(seen_wall, why_wall, seen_side))
        else
            T.expect("a wall hides a target in front of the viewer", false, "no object")
        end

        -- is_seen_by: a point 350 behind V is in front of W (and behind V)
        local behind_v = M.offset({ vx, vy, vz }, 0, -350)
        behind_v.y = (ground(behind_v.x, behind_v.z) or vy) + 60
        local seen, who = W.is_seen_by(behind_v, { V, Wk })
        T.expect("is_seen_by: the viewer who looks at the point is returned", seen == true and who == Wk, f(seen, name_of(who)))
        T.expect("is_seen_by: nobody looks at it", W.is_seen_by(behind_v, { V }) == false)
        risen.script.call("Kill", player, Wk, 0)
        T.sleep(60)
        T.expect("is_seen_by skips dead viewers", Wk:is_dead() and W.is_seen_by(behind_v, { V, Wk }) == false and W.is_seen_by(behind_v, { V, Wk }, { include_dead = true }) ~= nil, tostring(Wk:is_dead()))
        for _, e in ipairs(targets) do e:kill() end
        V:kill()
        Wk:kill()

        -- ---------------------------------------------------------------------------------------------------------------------------------------------
        T.section("camera")
        -- the third-person camera looks at the hero: wait until it has settled (the first seconds of a new game it flies in)
        local cam, cwhy, to_player_angle
        local function look()
            cam, cwhy = W.camera()
            if not cam then return false end
            local ppx, ppy, ppz = player:position()
            to_player_angle = M.angle_between({ cam.dx, cam.dy, cam.dz }, M.direction({ cam.x, cam.y, cam.z }, { ppx, ppy + 120, ppz }) or { 0, 0, 1 })
            return to_player_angle < 20
        end
        local settled = await(look, 25)
        local cd = cam and M.distance({ cam.x, cam.y, cam.z }, player)
        T.expect("the camera is on the player's side", cam and cd < 1200, cam and f(cam.x, cam.y, cam.z, cd) or cwhy)
        local cl = cam and math.sqrt(cam.dx ^ 2 + cam.dy ^ 2 + cam.dz ^ 2)
        T.expect("the camera direction is a unit vector", cl and math.abs(cl - 1) < 1e-3, f(cl))
        T.expect("the camera direction looks at the player", settled, f(to_player_angle) .. " degrees off")
        T.expect("the engine's camera fov", cam and cam.fov > 20 and cam.fov < 140 and cam.aspect > 0.3 and cam.aspect < 1.5, cam and f(cam.fov, cam.aspect))
        if cam then
            -- a target on the camera axis 800 away, and one behind the camera: the body may look elsewhere, the camera decides
            local axis = { cam.x + cam.dx * 800, cam.y + cam.dy * 800, cam.z + cam.dz * 800 }
            local back = { cam.x - cam.dx * 800, cam.y - cam.dy * 800, cam.z - cam.dz * 800 }
            local ahead, why_ahead = W.can_see(player, axis, { camera = true, fov = 20, vfov = 20 })
            local behind = W.can_see(player, back, { camera = true, fov = 20, vfov = 20 })
            local hx, hy, hz = player:direction()
            T.expect("can_see with camera = true follows the camera", behind == false and (ahead == true or why_ahead == "blocked"),
                f(ahead, why_ahead, behind) .. " body yaw " .. f(M.yaw_of(player)) .. " camera yaw " .. f(M.yaw_of({ cam.dx, cam.dy, cam.dz })))
        else
            T.expect("can_see with camera = true follows the camera", false, cwhy)
        end

        -- ---------------------------------------------------------------------------------------------------------------------------------------------
        T.section("random")
        local R = risen.random
        local function draw(rng, n) local t = {} for i = 1, n do t[i] = rng:int(1, 1000000) end return table.concat(t, ",") end
        T.expect("a seeded stream repeats", draw(R.new(42), 10) == draw(R.new(42), 10) and draw(R.new(42), 10) ~= draw(R.new(43), 10))
        R.seed(7)
        local m1 = { R.int(1, 100), R.int(1, 100), R.int(1, 100) }
        R.seed(7)
        local m2 = { R.int(1, 100), R.int(1, 100), R.int(1, 100) }
        T.expect("the stream of the mod is seedable", table.concat(m1, ",") == table.concat(m2, ","))
        T.expect("unseeded streams differ", draw(R.new(), 6) ~= draw(R.new(), 6))
        math.randomseed(1234)
        local ra = math.random()
        math.randomseed(1234)
        T.expect("math.random starts from a fixed state", ra == math.random())
        R.seed(2024)
        R.persist("main")

        -- ---------------------------------------------------------------------------------------------------------------------------------------------
        T.section("save and load")
        local probe_clear = W.line_of_sight({ px, eye, pz }, out)
        local probe_wall = wall_a and W.line_of_sight(wall_a, wall_b)
        R.int(1, 10)
        R.int(1, 10)
        T.wait_until(function() return W.is_save_allowed() end, 30, "saving allowed")
        local saved = false
        risen.on("save.after", function(_, ok) saved = ok end)
        W.quick_save()
        T.wait_until(function() return saved end, 60, "savegame written")
        T.expect("saved", saved)
        local expected = { R.int(1, 1000), R.int(1, 1000), R.int(1, 1000), R.int(1, 1000), R.int(1, 1000) }
        T.sleep(100)
        W.quick_load()
        T.wait_event("session.start", 120, function(m) return m == "load" end)
        T.sleep(150)
        local got = { R.int(1, 1000), R.int(1, 1000), R.int(1, 1000), R.int(1, 1000), R.int(1, 1000) }
        T.expect("loaded: the stream continues the saved sequence", table.concat(got, ",") == table.concat(expected, ","), table.concat(got, ",") .. " / " .. table.concat(expected, ","))
        local player2 = risen.entity.player()
        local qx, qy, qz = player2:position()
        local down2, d2 = W.trace(qx, qy + 150, qz, qx, qy - 500, qz, player2)
        T.expect("loaded: the ray test works in the new world", down2 == true and math.abs(d2 - 150) < 6, f(down2, d2))
        local cam2 = W.camera()
        T.expect("loaded: the camera works", cam2 and cam2.fov > 20, cam2 and f(cam2.x, cam2.y, cam2.z))
        local c2 = W.line_of_sight({ px, eye, pz }, out)
        local w2 = wall_a and W.line_of_sight(wall_a, wall_b)
        T.expect("loaded: clear stays clear and the wall stays a wall", c2 == probe_clear and w2 == probe_wall and c2 == true and w2 == false, f(c2, w2, probe_clear, probe_wall))
    end, "geometry")
end
