---@diagnostic disable: need-check-nil, param-type-mismatch, undefined-field
-- Live check of an edited material (docs/RE.md section 60): scripts/matprobe.ps1 runs this twice, without and with mods/matprobe (a loose copy of the club's
-- material with another diffuse image and the specular flag off). Prints the state of the material resource and takes pictures of the club.
-- PNGs: <Risen>\bin\spass\screenshots\<prefix>_<yaw>.png
local T = risen.test
local prefix = T.param("prefix", "matprobe_base")
T.plan { "the club exists", "the material resource is loaded", "pictures taken" }

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
    local tpl = T.param("template", "It_1H_Club")
    local club = risen.entity.spawn(tpl, px + dx * T.param("dist", 150), py + 10, pz + dz * T.param("dist", 150))
    T.expect("the club exists", club ~= nil)
    wait_s(2)
    local mname = T.param("material", "ItWpn_SwordMisc_01_Diffuse_01._xmat")
    risen.log.info("matprobe: lookup " .. tostring(risen.vfs.lookup(mname)) .. " / " .. tostring(risen.vfs.lookup_physical(mname)))
    local r = risen.assets.resource(T.param("material", "ItWpn_SwordMisc_01_Diffuse_01._xmat"))
    risen.log.info("matprobe: material resource " .. tostring(r and r.state) .. " " .. tostring(r and r.path))
    T.expect("the material resource is loaded", r ~= nil and r.state == 3)
    local done = 0
    for _, yaw in ipairs({ 0, 90 }) do
        if club then club:set_pose({ yaw = yaw }) end
        wait_s(1.5)
        if shot(prefix .. "_" .. yaw) then done = done + 1 end
    end
    T.expect("pictures taken", done == 2)
    T.finish()
end)
