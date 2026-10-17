---@diagnostic disable: need-check-nil, param-type-mismatch, undefined-field
-- .\scripts\test-run.ps1 -Mods meshprobe,meshprobe_shots -WaitForResult -Seconds 200 -Scenario meshprobe
-- PNGs: <Risen>\bin\spass\screenshots\meshprobe_<yaw>.png  (the probe model and the shipped club side by side, the probe turned by yaw)
local T = risen.test
T.plan { "the probe item exists", "pictures taken" }

local function wait_s(s)
    local t0 = T.elapsed()
    T.wait_until(function() return T.elapsed() - t0 >= s end, s + 15, "wait " .. s .. " s")
end
local function shot(name)
    risen.debug.screenshot(name, 8)
    T.wait_until(function() return risen.debug.screenshot_status(name) ~= "pending" end, 20, "screenshot " .. name)
    return risen.debug.screenshot_status(name) == "done"
end

T.run(function()
    T.wait_event("session.start", 240, function(m) return m == "new" end)
    wait_s(2)
    pcall(risen.gui.close, 26)
    wait_s(25)
    pcall(risen.world.set_time, 12, 0)
    wait_s(3)
    local p = risen.entity.player()
    local cam = risen.world.camera()
    local px, py, pz = p:position()
    local dx, dz = 0, 1
    if cam then
        local l = math.sqrt(cam.dx * cam.dx + cam.dz * cam.dz)
        if l > 0.01 then dx, dz = cam.dx / l, cam.dz / l end
    end
    local function at(fwd, right) return px + dx * fwd + dz * right, py + 10, pz + dz * fwd - dx * right end
    local x, y, z = at(190, -60)
    local probe = risen.entity.spawn("Spass_ProbeModel", x, y, z)
    T.expect("the probe item exists", probe ~= nil)
    local x2, y2, z2 = at(190, 90)
    risen.entity.spawn("It_1H_Club", x2, y2, z2)
    wait_s(2)
    local done = 0
    for _, yaw in ipairs({ 0, 90, 180 }) do
        if probe then probe:set_pose({ yaw = yaw }) end
        wait_s(1.5)
        if shot("meshprobe_" .. yaw) then done = done + 1 end
    end
    T.expect("pictures taken", done == 3)
    T.finish()
end)
