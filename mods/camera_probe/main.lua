---@diagnostic disable: need-check-nil
-- Scenario `camprobe`: the camera types (docs/RE.md section 61). Dumps the camera types and their flags, then measures what the hero and the camera do on
-- TurnRight / forward / strafe input, first with the stock cam_normal and then with the camera-space flags patched in (the "free orbit" experiment).
--
--   .\scripts\test-run.ps1 -Mods camera_probe -WaitForResult -Seconds 300 -Scenario camprobe
--
-- Every measurement is one "CAMPROBE {...}" log line; screenshots camprobe_*.png.
local T = risen.test
local settle = tonumber(T.param("settle", "30")) or 30
if T.param("phase", "camera") == "combat" then
    require("combat")()
    return
end

T.plan {
    "new game started", "types listed", "cam_normal read", "patch refuses bad names",
    "stock turn", "stock forward", "patched", "patched turn", "patched strafe", "patched forward",
}

local function log(tbl)
    local parts = {}
    for k, v in pairs(tbl) do
        local s
        if type(v) == "number" then s = string.format("%.3f", v)
        elseif type(v) == "boolean" then s = tostring(v)
        else s = string.format("%q", tostring(v)) end
        parts[#parts + 1] = string.format("%q:%s", k, s)
    end
    table.sort(parts)
    risen.log.info("CAMPROBE {" .. table.concat(parts, ",") .. "}")
end

local function wait_s(s)
    local t0 = T.elapsed()
    T.wait_until(function() return T.elapsed() - t0 >= s end, s + 30, "wait " .. s .. " s")
end

local function shot(name)
    if risen.debug.screenshot(name) then
        T.wait_until(function() return risen.debug.screenshot_status(name) ~= "pending" end, 30, "screenshot " .. name)
    end
end

local function angle(a, b) -- signed b - a in (-180, 180]
    local d = (b - a) % 360
    if d > 180 then d = d - 360 end
    return d
end

local function state()
    local p = risen.entity.player():pose()
    local c = risen.camera.get()
    return { px = p.x, pz = p.z, pyaw = p.yaw, cyaw = c.yaw, cx = c.x, cz = c.z, type = c.type }
end

-- holds an action for `s` seconds and logs what changed
local function hold(label, action, s)
    local a = state()
    risen.input.simulate(action, true)
    wait_s(s)
    risen.input.simulate(action, false)
    wait_s(0.6)
    local b = state()
    local dx, dz = b.px - a.px, b.pz - a.pz
    local moved = math.sqrt(dx * dx + dz * dz)
    local moveYaw = moved > 1 and (math.deg(math.atan2(dx, dz)) % 360) or -1
    local r = {
        what = label, action = action, type = b.type,
        hero_turn = angle(a.pyaw, b.pyaw), cam_turn = angle(a.cyaw, b.cyaw), moved = moved,
        move_vs_cam = moveYaw >= 0 and angle(a.cyaw, moveYaw) or 999, -- direction of travel relative to the camera's view before the input
        hero_vs_cam = angle(b.cyaw, b.pyaw), -- where the hero faces relative to the camera afterwards
    }
    log(r)
    return r
end

local FLAGS = {
    "CamControl_AxisX", "CamControl_AxisY", "CamControl_AxisZ", "CharacterControlMove", "CharacterControlRotation",
    "CharacterMoveInCameraSpace", "CharacterRotationInCameraSpace", "FixTargetRotation", "CamFOV", "CamTargetMinDistance", "CamTargetMaxDistance", "CamJumpOnStart",
}

T.run(function()
    local started = T.wait_event("session.start", 240, function(m) return m == "new" end)
    T.eq("new game started", started, "new")
    wait_s(settle)
    pcall(risen.gui.close, 26)

    local acts = risen.input.actions()
    local al = {}
    for i = 0, 16 do al[#al + 1] = i .. "=" .. tostring(acts[i]) end
    log { what = "actions", list = table.concat(al, " ") }

    local names, why = risen.camera.types()
    T.expect("types listed", names and #names > 10, names and (#names .. " types: " .. table.concat(names, ",")) or why)

    -- every type's flags on one line each
    for _, n in ipairs(names or {}) do
        local t = risen.camera.type(n)
        if t then
            local r = { what = "type", name = n }
            for _, f in ipairs(FLAGS) do r[f] = t[f] end
            log(r)
        end
    end
    local normal = risen.camera.type("cam_normal")
    T.expect("cam_normal read", normal and normal.CharacterMoveInCameraSpace ~= nil, normal and "ok" or "nil")
    if normal then
        local keys = {}
        for k in pairs(normal) do keys[#keys + 1] = k end
        table.sort(keys)
        for _, k in ipairs(keys) do log { what = "cam_normal", prop = k, value = normal[k] } end
    end

    local ok1, w1 = risen.camera.patch_type("cam_normal", { NoSuchProperty = 1 })
    local ok2, w2 = risen.camera.patch_type("no_such_cam", { CamFOV = 60 })
    T.expect("patch refuses bad names", ok1 == nil and ok2 == nil, tostring(w1) .. " / " .. tostring(w2))

    -- stock behaviour
    shot("camprobe_stock")
    local st = hold("stock turn", "TurnRight", 1.0)
    T.expect("stock turn", true, string.format("hero %.1f cam %.1f", st.hero_turn, st.cam_turn))
    local sf = hold("stock forward", "MoveForward", 1.0)
    hold("stock left", "MoveLeft", 1.0)
    T.expect("stock forward", true, string.format("moved %.0f dir vs cam %.1f", sf.moved, sf.move_vs_cam))

    -- the experiment: the camera turns, the hero moves in camera space and turns toward where he walks
    local ok, pw = risen.camera.patch_type("cam_normal", { CamControl_AxisX = true, CharacterMoveInCameraSpace = true, CharacterRotationInCameraSpace = false })
    T.expect("patched", ok, tostring(pw))
    log { what = "patched_values", AxisX = risen.camera.type("cam_normal").CamControl_AxisX, MoveCam = risen.camera.type("cam_normal").CharacterMoveInCameraSpace }
    wait_s(1)
    local pt = hold("patched turn", "TurnRight", 1.0)
    T.expect("patched turn", true, string.format("hero %.1f cam %.1f", pt.hero_turn, pt.cam_turn))
    shot("camprobe_patched_turn")
    local ps = hold("patched strafe", "MoveLeft", 1.2)
    T.expect("patched strafe", true, string.format("moved %.0f dir vs cam %.1f hero vs cam %.1f", ps.moved, ps.move_vs_cam, ps.hero_vs_cam))
    shot("camprobe_patched_strafe")
    local pf = hold("patched forward", "MoveForward", 1.0)
    T.expect("patched forward", true, string.format("moved %.0f dir vs cam %.1f hero vs cam %.1f", pf.moved, pf.move_vs_cam, pf.hero_vs_cam))
    hold("patched turn 2", "TurnRight", 1.5)
    hold("patched backward", "MoveBackward", 1.0)
    shot("camprobe_patched_back")
    hold("patched right", "MoveRight", 1.0)

    -- variant: the hero keeps facing the camera's direction and strafes (lock-on style)
    risen.camera.patch_type("cam_normal", { CharacterRotationInCameraSpace = true })
    hold("facing turn", "TurnLeft", 1.0)
    hold("facing left", "MoveLeft", 1.0)
    hold("facing forward", "MoveForward", 1.0)
    hold("facing back", "MoveBackward", 1.0)

    -- weapon drawn: the game switches to cam_melee, which keeps the stock flags
    risen.camera.patch_type("cam_normal", { CharacterRotationInCameraSpace = false })
    risen.input.simulate("WeaponMode", true); wait_s(0.3); risen.input.simulate("WeaponMode", false)
    wait_s(2)
    log { what = "weapon drawn", type = risen.camera.get().type }
    hold("melee stock turn", "TurnRight", 1.0)
    risen.camera.patch_type("cam_melee", { CamControl_AxisX = true, CharacterMoveInCameraSpace = true, CharacterRotationInCameraSpace = false })
    hold("melee patched turn", "TurnRight", 1.0)
    hold("melee patched left", "MoveLeft", 1.0)
    T.finish()
end)
