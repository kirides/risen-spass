---@diagnostic disable: need-check-nil
-- Scenario `scene`: the scene controls of the layer for renderer comparisons (docs/RE.md section 59): the fixed camera (risen.camera), the forced weather
-- (risen.world.set_weather / weather / weather_states), the console runner (risen.debug.console), and how they behave together with the time freeze of
-- section 58. Takes the same spot twice (camera, weather, clock, HUD off, frozen) with a different view in between, plus a few pictures for the eye.
--
--   .\scripts\test-run.ps1 -Mods scene_test -WaitForResult -Seconds 400 -Scenario scene [-Param settle=30]
--
-- Files in <Risen>\bin\spass\screenshots: scene_a1.png / scene_a2.png (the same spot twice: compare them), scene_b.png (the other view),
-- scene_pinned1/2.png (clock set and frozen in the same frame, twice), scene_rain.png (forced Stormy_Rain, follow camera), scene_cam_hud.png (fixed camera with the HUD),
-- scene_fade_0010/0300/0900.png (frames after rain -> sun), scene_frozen_cam.png / scene_frozen_weather.png / scene_frozen_menu.png (camera, weather, inventory while frozen),
-- scene_released.png (the follow camera again), scene_scale0_a/b.png (3 s apart at time_scale(0)).
-- Every measurement is one "SCENE {...}" log line.
local T = risen.test
local settle = tonumber(T.param("settle", "30")) or 30 -- seconds after the new game (the intro camera flies ~25 s)

T.plan {
    "new game started",
    "console commands listed", "console unknown command", "console missing argument", "console weather", "console weather reset",
    "weather states", "weather zones", "weather forced", "weather weights", "weather auto", "weather unknown state", "weather thunder off",
    "camera refuses bad specs", "camera set", "camera settled", "camera pose", "camera fixed", "camera eye+target", "camera eye+target pose",
    "spot a1", "spot b", "spot a2", "same pose twice", "spot pinned 1", "spot pinned 2",
    "camera while frozen", "weather while frozen", "menu while frozen",
    "camera released", "camera follows again", "weather back to zones",
    "time scale 0", "fixed frame time", "game time refuses bad values", "game time set", "game time pins the sea",
}

local ticks, frames = 0, 0
risen.on("tick", function() ticks = ticks + 1 end)
risen.on("ui.frame", function() frames = frames + 1 end)

local function log(tbl)
    local parts = {}
    for k, v in pairs(tbl) do
        local s
        if type(v) == "number" then s = string.format("%.4f", v)
        elseif type(v) == "boolean" then s = tostring(v)
        else s = string.format("%q", tostring(v)) end
        parts[#parts + 1] = string.format("%q:%s", k, s)
    end
    table.sort(parts)
    risen.log.info("SCENE {" .. table.concat(parts, ",") .. "}")
end

local function wait_s(s, name)
    local t0 = T.elapsed()
    T.wait_until(function() return T.elapsed() - t0 >= s end, s + 30, name or ("wait " .. s .. " s"))
end

local function shot(name)
    local ok, why = risen.debug.screenshot(name)
    if not ok then return "refused: " .. tostring(why) end
    T.wait_until(function() return risen.debug.screenshot_status(name) ~= "pending" end, 30, "screenshot " .. name)
    return risen.debug.screenshot_status(name)
end

local function dist(a, b) return math.sqrt((a[1] - b[1]) ^ 2 + (a[2] - b[2]) ^ 2 + (a[3] - b[3]) ^ 2) end

local function cam_pos() local c = risen.camera.get() return c and { c.x, c.y, c.z } or { 0 / 0, 0 / 0, 0 / 0 }, c end

-- sets the camera and waits until the camera admin reports the destination pose (or `frames_max` rendered frames)
local function set_camera(spec, frames_max)
    local ok, why = risen.camera.set(spec)
    if not ok then return false, why end
    local f0 = frames
    T.wait_until(function() return risen.camera.settled() or frames - f0 >= (frames_max or 300) end, 60, "camera settles")
    return risen.camera.settled(), frames - f0
end

T.run(function()
    local started = T.wait_event("session.start", 240, function(m) return m == "new" end)
    T.eq("new game started", started, "new")
    wait_s(settle, "settle")
    pcall(risen.gui.close, 26)
    T.sleep_frames(10)

    local hs0 = risen.hud.state()
    log { what = "hud_normal", mode = hs0 and hs0.mode, page_mode = hs0 and hs0.page_mode, pages = hs0 and table.concat(hs0.pages or {}, " ") }
    local player = risen.entity.player()
    local pp = player and player:pose()
    local P = pp and { pp.x, pp.y, pp.z } or { 0, 0, 0 }
    log { what = "player", x = P[1], y = P[2], z = P[3] }

    T.section("console")
    local cmds = risen.debug.console_commands()
    local have = {}
    for _, c in ipairs(cmds) do have[c] = true end
    T.expect("console commands listed", #cmds >= 40 and have.Weather and have.time and have.timescale, #cmds .. " commands: " .. table.concat(cmds, ","))
    local ok, msg = risen.debug.console("NoSuchCommand")
    T.expect("console unknown command", ok == nil and tostring(msg):find("unknown console command", 1, true), tostring(msg))
    ok, msg = risen.debug.console("Weather")
    T.expect("console missing argument", ok == false and tostring(msg):find("not enough parameters", 1, true), tostring(ok) .. " " .. tostring(msg))
    ok, msg = risen.debug.console("weather", "Sunny_Full")   -- case-insensitive name
    local w = risen.world.weather()
    T.expect("console weather", ok == true and w and w.mode == 2 and w.dominant == "Sunny_Full", ("%s %q mode %s dominant %s"):format(tostring(ok), tostring(msg), tostring(w and w.mode), tostring(w and w.dominant)))
    ok, msg = risen.debug.console("Weather", "NoSuchState")
    w = risen.world.weather()
    T.expect("console weather reset", ok == true and w and w.mode == 0 and tostring(msg):find("PlayerPos", 1, true), ("%s %q mode %s"):format(tostring(ok), tostring(msg), tostring(w and w.mode)))

    T.section("weather")
    local states = risen.world.weather_states() or {}
    local known = {}
    for _, n in ipairs(states) do known[n] = true end
    T.expect("weather states", known.Stormy and known.Stormy_Rain and known.Foggy_Rain and known.Sunny_Full, #states .. ": " .. table.concat(states, ","))
    T.sleep_frames(5)
    w = risen.world.weather()
    local zw = {}
    for k, v in pairs(w and w.weights or {}) do zw[#zw + 1] = ("%s=%.3f"):format(k, v) end
    T.expect("weather zones", w and w.mode == 0 and w.mode_name == "zones" and next(w.weights) ~= nil, "mode " .. tostring(w and w.mode) .. ", weights " .. table.concat(zw, ","))
    ok, why = risen.world.set_weather("Sunny_Full", { thunder = false })
    w = risen.world.weather()
    T.expect("weather forced", ok and w.mode == 2 and w.dominant == "Sunny_Full" and w.weights.Sunny_Full == 1 and w.thunder == false, tostring(why) .. " mode " .. tostring(w.mode))
    ok, why = risen.world.set_weather({ Stormy_Rain = 0.75, Foggy = 0.25 })
    w = risen.world.weather()
    T.expect("weather weights", ok and w.mode == 2 and w.weights.Stormy_Rain == 0.75 and w.weights.Foggy == 0.25 and w.weights.Sunny_Full == nil, tostring(why))
    ok, why = risen.world.set_weather("auto")
    T.sleep_frames(3)
    w = risen.world.weather()
    T.expect("weather auto", ok and w.mode == 0 and w.thunder == true, tostring(why) .. " mode " .. tostring(w.mode) .. " thunder " .. tostring(w.thunder))
    ok, why = risen.world.set_weather("NoSuchState")
    T.expect("weather unknown state", ok == nil and tostring(why):find("unknown weather state", 1, true) and risen.world.weather().mode == 0, tostring(why))
    ok = risen.world.set_weather("Stormy_Rain", { thunder = false })
    T.sleep_frames(60)
    T.eq("weather thunder off", ok and risen.world.weather().thunder, false)
    shot("scene_rain")

    T.section("camera")
    local bad = {
        {}, { pos = { 1, 2 } }, { target = P }, { pos = P, target = P }, { target = P, yaw = 0, pitch = 95 }, { pos = { 0 / 0, 0, 0 }, target = P },
        { target = P, yaw = 10, distance = 0 }, { target = P, yaw = 0, fov = 500 }, { pos = P, target = { P[1], P[2] + 500, P[3] } },
    }
    local refused = 0
    for _, spec in ipairs(bad) do if risen.camera.set(spec) == nil then refused = refused + 1 end end
    T.eq("camera refuses bad specs", refused, #bad)

    risen.world.set_weather("Sunny_Full", { thunder = false })
    risen.world.set_time(12, 0)
    risen.hud.set_visible(false)
    local A = { target = { P[1], P[2] + 150, P[3] }, yaw = 200, pitch = -12, distance = 600 }
    local settled, nf = set_camera(A)
    T.expect("camera set", settled ~= nil, "settled " .. tostring(settled) .. " after " .. tostring(nf) .. " frames")
    T.expect("camera settled", settled == true, "after " .. tostring(nf) .. " frames")
    T.sleep_frames(30)
    local cp, c = cam_pos()
    local want = c and c.request and c.request.pos
    local d = want and dist(cp, want) or 1e9
    log { what = "camera_a", x = cp[1], y = cp[2], z = cp[3], yaw = c and c.yaw, pitch = c and c.pitch, fov = c and c.fov, type = c and c.type, err = d,
          req_fov = c and c.request and c.request.fov }
    T.expect("camera pose", d < 1 and math.abs(((c.yaw - A.yaw + 180) % 360) - 180) < 0.2 and math.abs(c.pitch - A.pitch) < 0.2,
        ("eye off by %.3f, yaw %.3f (want %d), pitch %.3f (want %d), fov %.2f"):format(d, c and c.yaw or -1, A.yaw, c and c.pitch or -1, A.pitch, c and c.fov or -1))
    T.expect("camera fixed", c and c.fixed == true and c.type == "cam_cinema", "fixed " .. tostring(c and c.fixed) .. " type " .. tostring(c and c.type) .. " aspect " .. tostring(c and c.aspect))
    risen.hud.set_visible(true)
    T.sleep_frames(10)
    local hs = risen.hud.state()
    log { what = "hud_fixed", mode = hs and hs.mode, page_mode = hs and hs.page_mode, pages = hs and table.concat(hs.pages or {}, " ") }
    shot("scene_cam_hud")
    risen.hud.set_visible(false)
    -- how fast the picture follows a weather change (rain -> sun): pictures 10, 300 and 900 frames after it
    risen.world.set_weather("Stormy_Rain", { thunder = false })
    T.sleep_frames(300)
    risen.world.set_weather("Sunny_Full", { thunder = false })
    for _, n in ipairs { 10, 300, 900 } do
        local f1 = frames
        T.wait_until(function() return frames - f1 >= (n == 10 and 10 or (n == 300 and 290 or 600)) end, 120, "fade " .. n)
        shot(("scene_fade_%04d"):format(n))
    end

    local E = { pos = { P[1] + 300, P[2] + 250, P[3] - 400 }, target = { P[1], P[2] + 100, P[3] } }
    settled, nf = set_camera(E)
    T.sleep_frames(10)
    cp, c = cam_pos()
    T.expect("camera eye+target", settled == true, "after " .. tostring(nf) .. " frames")
    local dir = { E.target[1] - E.pos[1], E.target[2] - E.pos[2], E.target[3] - E.pos[3] }
    local len = math.sqrt(dir[1] ^ 2 + dir[2] ^ 2 + dir[3] ^ 2)
    local dot = c and (c.dx * dir[1] + c.dy * dir[2] + c.dz * dir[3]) / len or 0
    T.expect("camera eye+target pose", dist(cp, E.pos) < 1 and dot > 0.99999, ("eye off by %.3f, direction cos %.6f"):format(dist(cp, E.pos), dot))

    T.section("same spot twice")
    -- spot A, frozen
    set_camera(A)
    risen.world.set_time(12, 0)
    T.sleep_frames(120)
    risen.debug.time_freeze(true)
    T.sleep_frames(30)
    local a1, c1 = cam_pos()
    T.eq("spot a1", shot("scene_a1"), "done")
    risen.debug.time_freeze(false)
    -- somewhere else
    set_camera({ target = { P[1], P[2] + 150, P[3] }, yaw = 20, pitch = -25, distance = 900 })
    T.sleep_frames(60)
    T.eq("spot b", shot("scene_b"), "done")
    -- spot A again
    set_camera(A)
    risen.world.set_time(12, 0)
    T.sleep_frames(120)
    risen.debug.time_freeze(true)
    T.sleep_frames(30)
    local a2, c2 = cam_pos()
    T.eq("spot a2", shot("scene_a2"), "done")
    local same = c1 and c2 and dist(a1, a2) < 0.01 and math.abs(c1.dx - c2.dx) < 1e-5 and math.abs(c1.dy - c2.dy) < 1e-5 and math.abs(c1.dz - c2.dz) < 1e-5
    T.expect("same pose twice", same, ("eye moved %.5f, dir (%.6f %.6f %.6f) vs (%.6f %.6f %.6f)"):format(dist(a1, a2), c1 and c1.dx or 0, c1 and c1.dy or 0, c1 and c1.dz or 0, c2 and c2.dx or 0, c2 and c2.dy or 0, c2 and c2.dz or 0))
    -- the clock set and the freeze in the same frame, twice: does the animation phase (sea, clouds) follow the world clock?
    for i = 1, 2 do
        risen.debug.time_freeze(false)
        T.sleep_frames(90)
        risen.world.set_time(12, 0)
        risen.debug.time_freeze(true)
        local tm = risen.debug.timer()
        T.sleep_frames(5)
        log { what = "pinned", i = i, playing_s = tm and tm.playing_s, game_ms = tm and tm.game_ms }
        T.eq("spot pinned " .. i, shot("scene_pinned" .. i), "done")
    end

    T.section("frozen")
    -- what a camera set while frozen does (the camera admin runs in the session's process step?)
    local f0, t0 = frames, ticks
    local okc = risen.camera.set({ target = { P[1], P[2] + 150, P[3] }, yaw = 20, pitch = -25, distance = 900 })
    T.sleep_frames(30)
    local fp = cam_pos()
    local moved = dist(fp, a2)
    log { what = "camera_while_frozen", moved = moved, settled = tostring(risen.camera.settled()), frames = frames - f0, ticks = ticks - t0, app_state = risen.debug.app_state() }
    shot("scene_frozen_cam")
    T.expect("camera while frozen", okc == true, ("set %s; camera moved %.2f units in 30 frozen frames (settled %s)"):format(tostring(okc), moved, tostring(risen.camera.settled())))
    -- the weather while frozen
    local okw = risen.world.set_weather("Stormy_Rain", { thunder = false })
    T.sleep_frames(30)
    shot("scene_frozen_weather")
    T.expect("weather while frozen", okw == true, "set " .. tostring(okw) .. " (compare scene_frozen_weather.png with scene_frozen_cam.png)")
    -- a menu page opened while frozen
    local s0 = risen.debug.app_state()
    local opened, owhy = risen.gui.open(2)
    T.sleep_frames(30)
    local s1 = risen.debug.app_state()
    local hsm = risen.hud.state()
    shot("scene_frozen_menu")
    risen.gui.close(2)
    T.sleep_frames(30)
    local s2 = risen.debug.app_state()
    log { what = "menu_while_frozen", before = s0, open = s1, closed = s2, opened = tostring(opened), why = tostring(owhy), pages = hsm and table.concat(hsm.pages or {}, " ") }
    T.expect("menu while frozen", opened, ("open %s (%s), app state %s, inventory open %s, closed %s"):format(tostring(opened), tostring(owhy), tostring(s0), tostring(s1), tostring(s2)))
    risen.debug.time_freeze(false)
    T.sleep_frames(30)

    T.section("release")
    ok = risen.camera.release()
    T.sleep_frames(60)
    cp, c = cam_pos()
    T.expect("camera released", ok and c and c.fixed == false, "fixed " .. tostring(c and c.fixed) .. " type " .. tostring(c and c.type))
    local pd = dist(cp, P)
    T.expect("camera follows again", c and c.type == "cam_normal" and pd < 1000, ("type %s, %.0f units from the player"):format(tostring(c and c.type), pd))
    risen.hud.set_visible(true)
    shot("scene_released")
    risen.world.set_weather("auto")
    w = risen.world.weather()
    T.expect("weather back to zones", w and w.mode == 0 and w.thunder == true, "mode " .. tostring(w and w.mode))

    T.section("timer")
    -- time_scale(0): the measured frame time becomes 0 and eCTimer::Update clamps it to 1e-5 ms (docs/RE.md section 59)
    local function sample(seconds)
        local a, t0_, e0_ = risen.debug.timer(), ticks, T.elapsed()
        wait_s(seconds, "timer sample")
        local b = risen.debug.timer()
        return { ticks = ticks - t0_, wall = T.elapsed() - e0_, game_ms = b.game_ms - a.game_ms, playing_s = b.playing_s - a.playing_s, frame_ms = b.frame_ms }
    end
    set_camera(A)
    risen.hud.set_visible(false)
    T.sleep_frames(30)
    risen.debug.time_scale(0)
    shot("scene_scale0_a")
    local z = sample(3)
    shot("scene_scale0_b")
    risen.debug.time_scale(1)
    risen.camera.release()
    risen.hud.set_visible(true)
    local one = sample(3)
    log { what = "time_scale_0", ticks0 = z.ticks, game_ms0 = z.game_ms, playing_s0 = z.playing_s, frame_ms0 = z.frame_ms,
          ticks1 = one.ticks, game_ms1 = one.game_ms, playing_s1 = one.playing_s, frame_ms1 = one.frame_ms }
    T.expect("time scale 0", z.ticks > 0 and z.game_ms == 0 and z.frame_ms < 5,   -- ticks go on, the timer's game time stands, the frame time is tiny, the world clock runs on
        ("scale 0: %d ticks, game time +%d ms, playing time +%.5f s, frame %.6f ms; scale 1: %d ticks, +%d ms, +%.3f s, frame %.2f ms"):format(
            z.ticks, z.game_ms, z.playing_s, z.frame_ms, one.ticks, one.game_ms, one.playing_s, one.frame_ms))
    -- fixed frame time: every frame counts 50 ms (the engine sleeps the rest of it); the per-frame readings of the timer show whether the simulation step is exact
    local fmin, fmax, n, pmin, pmax, lastp = 1e9, -1e9, 0, 1e9, -1e9, nil
    local id = risen.on("ui.frame", function()
        local tm = risen.debug.timer()
        if not tm or n < 0 then return end
        n = n + 1
        if n > 5 then   -- the first frames still carry the switch
            fmin, fmax = math.min(fmin, tm.frame_ms), math.max(fmax, tm.frame_ms)
            if lastp then pmin, pmax = math.min(pmin, tm.playing_s - lastp), math.max(pmax, tm.playing_s - lastp) end
        end
        lastp = tm.playing_s
    end)
    risen.debug.fixed_frame_time(50)
    local fx = sample(4)
    n = -1
    risen.off(id)
    risen.debug.fixed_frame_time(0)
    log { what = "fixed_frame_time", fps = fx.ticks / fx.wall, game_ms = fx.game_ms, wall = fx.wall, playing_s = fx.playing_s, frame_ms_min = fmin, frame_ms_max = fmax,
          playing_step_min = pmin, playing_step_max = pmax }
    T.expect("fixed frame time", math.abs(fmin - 50) < 0.01 and math.abs(fmax - 50) < 0.01,
        ("%.1f ticks/s, frame time %.3f..%.3f ms, playing time per frame %.5f..%.5f s, game time +%d ms in %.2f s"):format(
            fx.ticks / fx.wall, fmin, fmax, pmin, pmax, fx.game_ms, fx.wall))

    T.section("game time")
    -- set_game_time: eCTimer +0x10, the time of the timed shader elements (sea surface, clouds) (docs/RE.md section 60)
    local bad = 0
    for _, v in ipairs { -1, 0.5, 5e9, 0 / 0 } do if risen.debug.set_game_time(v) == nil then bad = bad + 1 end end
    T.eq("game time refuses bad values", bad, 4)
    local g0 = risen.debug.timer().game_ms
    local target = math.floor(g0 / 1000) * 1000 + 600000
    local okg, whyg = risen.debug.set_game_time(target)
    local g1 = risen.debug.timer().game_ms
    T.sleep_frames(5)
    local g2 = risen.debug.timer().game_ms
    T.expect("game time set", okg == true and g1 == target and g2 >= target and g2 < target + 2000,
        ("%s %s: before %d, after the call %d, 5 frames later %d"):format(tostring(okg), tostring(whyg), g0, g1, g2))
    -- the same game time a frame before two freezes of the same view: the sea surface and the clouds stand at the same phase (compare scene_gt1.png / scene_gt2.png)
    set_camera(A)
    risen.hud.set_visible(false)
    risen.world.set_weather("Sunny_Full", { thunder = false })
    local res = {}
    for i = 1, 2 do
        T.sleep_frames(120)
        risen.world.set_time(12, 0)
        risen.debug.set_game_time(target + 3600000)
        T.sleep_frames(2)
        risen.debug.time_freeze(true)
        T.sleep_frames(30)
        res[i] = shot("scene_gt" .. i)
        log { what = "game_time_pin", i = i, game_ms = risen.debug.timer().game_ms }
        risen.debug.time_freeze(false)
    end
    risen.hud.set_visible(true)
    risen.camera.release()
    T.expect("game time pins the sea", res[1] == "done" and res[2] == "done", "pictures " .. tostring(res[1]) .. ", " .. tostring(res[2]) .. " (compare scene_gt1.png and scene_gt2.png)")
end, "scene")
