-- Measures where the time of a test run goes and how fast the engine ticks (docs/DEVELOPMENT.md "Startup time"):
--   .\scripts\test-run.ps1 -Mods startupprobe -WaitForResult -Seconds 120 -Scenario startupprobe [-ExtraArgs '-noaudio']
-- Logs one "[probe] ..." line: first_tick / session_start (seconds since the layer started), the frame rate in the menu world and in the
-- game (ticks per second over 600 frames after the new game settled), and how long the new game took.
local T = risen.test
local log = risen.log

T.plan { "probe finished" }

T.run(function()
    local t_first = risen.test.elapsed()
    local menu_ticks = 0
    local h = risen.on("tick", function() menu_ticks = menu_ticks + 1 end)
    T.wait_event("session.start", 180, function(m) return m == "new" end)
    local t_start = T.elapsed()
    local menu_fps = menu_ticks / math.max(t_start - t_first, 0.001)
    T.sleep(150)
    local t0 = T.elapsed()
    T.sleep(600)
    local game_fps = 600 / math.max(T.elapsed() - t0, 0.001)
    log.info(("[probe] layer_first_step=%.1f session_start=%.1f menu_ticks=%d menu_fps=%.1f game_fps=%.1f"):format(t_first, t_start, menu_ticks, menu_fps, game_fps))
    T.expect("probe finished", true, ("session.start at %.1f s, %.1f fps in the game"):format(t_start, game_fps))
end, "startupprobe")
