-- Scenario `capture`: the screenshot and capture controls of the layer (docs/RE.md section 58): time freeze (app state 8), the HUD off,
-- screenshots with wait_frames and bursts, the timer controls, test waits while time is frozen. Works on either screenshot backend:
-- "d3d9" (the layer's Present hook, the game's own renderer) or "rvk" (a d3d9.dll proxy that exports rvk_screenshot, e.g. the Vulkan
-- renderer risen-enhanced-graphics); -Param backend=d3d9|rvk makes the backend a check.
--
--   .\scripts\test-run.ps1 -Mods capture_test -WaitForResult -Seconds 300 -Scenario capture [-Param backend=d3d9,settle=30]
--
-- Files in <Risen>\bin\spass\screenshots: capture_hud.png (HUD on), capture_frozen_b0..b2.png (HUD off, time frozen: identical up to
-- a level or two), capture_running_b0..b2.png (HUD off, time running), capture_wait.png, capture_hud_back.png (HUD restored). Every
-- capture logs one "CAPTURE {...}" line with the frame numbers; comparing the pictures is left to the caller.
local T = risen.test
local want_backend = T.param("backend", nil)          -- "d3d9" | "rvk" | nil (either)
local settle = tonumber(T.param("settle", "30")) or 30 -- seconds after the new game (the intro camera flies ~25 s)

T.plan {
    "new game started", "no backend before the first request", "frame before a backend", "hud shot", "backend",
    "hud hidden", "time frozen", "waits run while frozen", "clock stands still", "wait_frames", "frozen burst",
    "time scale read", "time scale set", "time scale restored", "fixed frame time off", "time running again",
    "running burst", "hud restored", "hud back shot",
}

local ticks, frames = 0, 0
risen.on("tick", function() ticks = ticks + 1 end)
risen.on("ui.frame", function() frames = frames + 1 end)

local function wait_s(s, name)
    local t0 = T.elapsed()
    T.wait_until(function() return T.elapsed() - t0 >= s end, s + 30, name or ("wait " .. s .. " s"))
end

-- takes a screenshot and waits for it; returns the final status and the frame numbers before / after
local function shot(name, opts)
    local f0 = risen.debug.frame()
    local ok, why = risen.debug.screenshot(name, opts)
    if not ok then return "refused: " .. tostring(why), f0, f0 end
    T.wait_until(function() return risen.debug.screenshot_status(name) ~= "pending" end, 30, "screenshot " .. name)
    local f1, source = risen.debug.frame()
    risen.log.info(("CAPTURE {\"shot\":%q,\"status\":%q,\"backend\":%q,\"frame_before\":%s,\"frame_after\":%s,\"source\":%q}"):format(
        name, tostring(risen.debug.screenshot_status(name)), tostring(risen.debug.screenshot_backend()), tostring(f0), tostring(f1), tostring(source)))
    return risen.debug.screenshot_status(name), f0, f1
end

T.run(function()
    local started = T.wait_event("session.start", 240, function(m) return m == "new" end)
    T.eq("new game started", started, "new")
    wait_s(settle, "settle")
    pcall(risen.gui.close, 26)
    T.sleep_frames(10)

    T.eq("no backend before the first request", risen.debug.screenshot_backend(), nil)
    local f, source = risen.debug.frame()
    T.expect("frame before a backend", type(f) == "number" and source == "engine", tostring(f) .. " " .. tostring(source))

    T.section("hud")
    T.eq("hud shot", (shot("capture_hud")), "done")
    local backend = risen.debug.screenshot_backend()
    T.expect("backend", backend ~= nil and (want_backend == nil or backend == want_backend), tostring(backend) .. " (wanted " .. tostring(want_backend) .. ")")

    T.section("freeze")
    risen.hud.set_visible(false)
    local st = risen.hud.state()
    T.eq("hud hidden", st and st.page_mode, 1)
    local h0, m0 = risen.world.time()
    local ok, why = risen.debug.time_freeze(true)
    T.expect("time frozen", ok and risen.debug.app_state() == 8, tostring(why) .. " app state " .. tostring(risen.debug.app_state()))
    -- what still runs under app state 8: counted, not required (docs/RE.md section 58)
    local t0, fr0 = ticks, frames
    wait_s(3, "wall clock under freeze")  -- wait_until must keep polling while frozen
    T.sleep_frames(30)
    local h1, m1 = risen.world.time()
    T.expect("waits run while frozen", frames - fr0 >= 30, ("frames %d, ticks %d"):format(frames - fr0, ticks - t0))
    risen.log.info(("CAPTURE {\"frozen_frames\":%d,\"frozen_ticks\":%d,\"clock_before\":\"%s:%s\",\"clock_after\":\"%s:%s\"}"):format(
        frames - fr0, ticks - t0, tostring(h0), tostring(m0), tostring(h1), tostring(m1)))
    T.expect("clock stands still", h0 == h1 and m0 == m1, ("%s:%s -> %s:%s"):format(tostring(h0), tostring(m0), tostring(h1), tostring(m1)))

    local status, fa, fb = shot("capture_wait", 20)
    T.expect("wait_frames", status == "done" and type(fa) == "number" and fb - fa >= 21, ("%s, frames %s -> %s"):format(status, tostring(fa), tostring(fb)))
    status, fa, fb = shot("capture_frozen", { wait_frames = 5, burst = 3, interval = 10 })
    T.expect("frozen burst", status == "done" and fb - fa >= 26, ("%s, frames %s -> %s"):format(status, tostring(fa), tostring(fb)))

    T.section("timer")
    T.eq("time scale read", risen.debug.time_scale(), 1, 1e-6)
    T.eq("time scale set", risen.debug.time_scale(0.5), 0.5, 1e-6)
    T.eq("time scale restored", risen.debug.time_scale(1), 1, 1e-6)
    T.eq("fixed frame time off", risen.debug.fixed_frame_time(0), true)

    T.section("unfreeze")
    risen.debug.time_freeze(false)
    T.eq("time running again", risen.debug.app_state(), 0)
    t0 = ticks
    status = shot("capture_running", { wait_frames = 5, burst = 3, interval = 10 })
    T.eq("running burst", status, "done")
    risen.log.info(("CAPTURE {\"running_ticks\":%d}"):format(ticks - t0))

    T.section("hud back")
    risen.hud.set_visible(true)
    st = risen.hud.state()
    T.expect("hud restored", st and st.page_mode ~= 1, "page mode " .. tostring(st and st.page_mode))
    T.eq("hud back shot", (shot("capture_hud_back", 5)), "done")
end, "capture")
