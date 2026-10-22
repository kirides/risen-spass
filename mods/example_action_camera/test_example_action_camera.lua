---@diagnostic disable: need-check-nil, param-type-mismatch
-- Scenario `ex_action_camera`: the action camera of example_action_camera, measured with simulated keys (risen.input.simulate).
--   .\scripts\test-run.ps1 -Instance 1 -Mods example_action_camera -WaitForResult -Seconds 300 -Scenario ex_action_camera -ShowChecks
return function(mod)
    local T = risen.test
    local settle = tonumber(T.param("settle", "30")) or 30
    local TOL = tonumber(T.param("tol", "20")) or 20 -- degrees

    T.plan {
        "new game started", "camera type patched", "yaw is world-relative",
        "turn orbits the camera", "turn leaves the hero",
        "right: hero faces right of the view", "right: walks right of the view", "right: camera not dragged",
        "back: hero faces the camera", "back: walks toward the camera",
        "forward: hero faces the view", "forward: walks along the view", "forward: camera not dragged",
        "weapon drawn", "weapon, no fight yet: runs turned", "attack: hero faces the camera",
        "dodge after the attack: side dodge left of the view", "weapon, running right: dodges right, keeps facing right", "weapon, the next dodge: right again",
        "no weapon, running right: jumps right, keeps facing right", "jumps were translated",
        "ranged: hero is the orientation", "ranged: turn turns the hero (game's own camera)", "back to ours: world-relative again",
        "loaded: still patched and world-relative", "loaded: hero turns where he runs",
    }

    local function log(tbl)
        local parts = {}
        for k, val in pairs(tbl) do
            parts[#parts + 1] = string.format("%q:%s", k, type(val) == "number" and string.format("%.2f", val) or string.format("%q", tostring(val)))
        end
        table.sort(parts)
        risen.log.info("ACTIONCAM {" .. table.concat(parts, ",") .. "}")
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
        return { px = p.x, pz = p.z, pyaw = p.yaw, cyaw = c.yaw }
    end

    -- holds an action, lets the hero finish turning, and reports hero / camera / travel relative to the camera's yaw before the input
    local function hold(label, action, s)
        local a = state()
        risen.input.simulate(action, true)
        wait_s(s)
        risen.input.simulate(action, false)
        wait_s(0.5)
        local b = state()
        local dx, dz = b.px - a.px, b.pz - a.pz
        local moved = math.sqrt(dx * dx + dz * dz)
        local r = {
            what = label, moved = moved,
            hero_vs_view = angle(a.cyaw, b.pyaw), -- where the hero faces now, relative to the view before
            travel_vs_view = moved > 1 and angle(a.cyaw, math.deg(math.atan2(dx, dz))) or 999,
            cam_turn = angle(a.cyaw, b.cyaw), hero_turn = angle(a.pyaw, b.pyaw),
        }
        log(r)
        return r
    end

    local function near(x, want) return math.abs(angle(want, x)) <= TOL end
    local function fmt(r) return string.format("hero %.1f travel %.1f cam %.1f moved %.0f", r.hero_vs_view, r.travel_vs_view, r.cam_turn, r.moved) end

    T.run(function()
        local started = T.wait_event("session.start", 240, function(m) return m == "new" end)
        T.eq("new game started", started, "new")
        wait_s(settle)
        pcall(risen.gui.close, 26)

        local t = risen.camera.type(mod.camera)
        T.expect("camera type patched", t and t.CamControl_AxisX == "True" and t.CharacterMoveInCameraSpace == "True" and t.CharacterRotationInCameraSpace == "False",
            t and (t.CamControl_AxisX .. " " .. t.CharacterMoveInCameraSpace .. " " .. t.CharacterRotationInCameraSpace) or "nil")
        local c = risen.camera.get()
        T.expect("yaw is world-relative", c.orientation == nil and c.target ~= nil and mod.state.world_relative, "orientation " .. tostring(c.orientation) .. " type " .. tostring(c.type))
        shot("actioncam_start")

        local turn = hold("turn", "TurnRight", 1.0)
        T.expect("turn orbits the camera", turn.cam_turn > 15, fmt(turn))
        T.expect("turn leaves the hero", math.abs(turn.hero_turn) < 2, fmt(turn))

        local right = hold("right", "MoveRight", 1.5)
        shot("actioncam_right")
        T.expect("right: hero faces right of the view", near(right.hero_vs_view, 90), fmt(right))
        T.expect("right: walks right of the view", right.moved > 100 and near(right.travel_vs_view, 90), fmt(right))
        T.expect("right: camera not dragged", math.abs(right.cam_turn) < TOL, fmt(right))

        local back = hold("back", "MoveBackward", 1.5)
        shot("actioncam_back")
        T.expect("back: hero faces the camera", near(back.hero_vs_view, 180), fmt(back))
        T.expect("back: walks toward the camera", back.moved > 100 and near(back.travel_vs_view, 180), fmt(back))

        local fwd = hold("forward", "MoveForward", 1.5)
        shot("actioncam_forward")
        T.expect("forward: hero faces the view", near(fwd.hero_vs_view, 0), fmt(fwd))
        T.expect("forward: walks along the view", fwd.moved > 100 and near(fwd.travel_vs_view, 0), fmt(fwd))
        T.expect("forward: camera not dragged", math.abs(fwd.cam_turn) < TOL, fmt(fwd))
        log { what = "aligned", n = mod.state.aligned }

        -- fights: an attack faces the camera; a dodge goes where the held key points FROM THE HERO'S VIEW (D while he runs to the right = forward for him)
        T.section("combat")
        local player = risen.entity.player()
        risen.item.give(player, "It_1H_Club_01", 1)
        wait_s(0.5)
        risen.input.simulate("WeaponMode", true); wait_s(0.3); risen.input.simulate("WeaponMode", false)
        T.wait_until(function() return mod.state.weapon end, 10, "weapon drawn")
        wait_s(1.5)
        T.expect("weapon drawn", mod.state.weapon, tostring(player:state()))
        local cright = hold("weapon right", "MoveRight", 1.2)
        T.expect("weapon, no fight yet: runs turned", near(cright.hero_vs_view, 90), fmt(cright))

        local a = state()
        risen.input.simulate("PrimaryAction", true); T.sleep_frames(3); risen.input.simulate("PrimaryAction", false)
        wait_s(0.4)
        local b = state()
        local att = { hero_vs_view = angle(a.cyaw, b.pyaw), travel_vs_view = 999, cam_turn = angle(a.cyaw, b.cyaw), moved = 0 }
        log { what = "attack", hero_vs_view = att.hero_vs_view, state = tostring(player:state()) }
        T.expect("attack: hero faces the camera", near(att.hero_vs_view, 0), fmt(att))

        -- holds `key` (running `run_s` seconds first), presses Jump, keeps the key down until he lands; travel is measured from the press
        local function jump_with(label, key, run_s, ready)
            if ready then T.wait_until(function() return player:state() == ready end, 10, "ready: " .. ready) end
            risen.input.simulate(key, true)
            wait_s(run_s)
            local s0 = state()
            local jumped = false
            local off = risen.on("tick", function() if player:state() == "PS_Jump" then jumped = true end end)
            risen.input.simulate("Jump", true); T.sleep_frames(3); risen.input.simulate("Jump", false)
            T.wait_until(function() return jumped end, 3, "jump started")
            T.sleep_frames(8)
            shot("actioncam_" .. label:gsub("[^%w]+", "_"))
            T.wait_until(function() local st = player:state() return st ~= "PS_Jump" and st ~= "ZS_Jump_End" end, 5, "landed")
            local s1 = state()
            risen.input.simulate(key, false)
            risen.off(off)
            wait_s(0.5)
            local dx, dz = s1.px - s0.px, s1.pz - s0.pz
            local moved = math.sqrt(dx * dx + dz * dz)
            local r = { what = label, moved = moved, jumped = tostring(jumped), hero_vs_view = angle(s0.cyaw, s1.pyaw),
                        travel_vs_view = moved > 1 and angle(s0.cyaw, math.deg(math.atan2(dx, dz))) or 999,
                        cam_turn = angle(s0.cyaw, s1.cyaw), hero_turn = angle(s0.pyaw, s1.pyaw), translated = mod.state.translated }
            log(r)
            return r
        end
        -- right after the attack he faces the camera: MoveLeft is "left" for him too, a side dodge
        local d1 = jump_with("dodge left after the attack", "MoveLeft", 0.05, "PS_Melee_Loop")
        T.expect("dodge after the attack: side dodge left of the view", d1.moved > 50 and near(d1.travel_vs_view, -90) and near(d1.hero_vs_view, 0), fmt(d1))
        wait_s(2.0) -- the fight window is over
        -- the user's case: he runs to the right of the view (faces right), Space: a dodge to the right, still facing right
        local d2 = jump_with("weapon: running right, dodge", "MoveRight", 1.0, "PS_Melee_Loop")
        T.expect("weapon, running right: dodges right, keeps facing right", d2.moved > 50 and near(d2.travel_vs_view, 90) and near(d2.hero_vs_view, 90), fmt(d2))
        local d3 = jump_with("weapon: running right, dodge again", "MoveRight", 0.3, "PS_Melee_Loop")
        T.expect("weapon, the next dodge: right again", d3.moved > 50 and near(d3.travel_vs_view, 90) and near(d3.hero_vs_view, 90), fmt(d3))
        risen.input.simulate("WeaponMode", true); wait_s(0.3); risen.input.simulate("WeaponMode", false)
        T.wait_until(function() return not mod.state.weapon end, 10, "weapon holstered")
        wait_s(1.5)
        -- no weapon: running to the right and Space is a jump forward for him, to the right of the view
        local j1 = jump_with("no weapon: running right, jump", "MoveRight", 1.0, nil)
        T.expect("no weapon, running right: jumps right, keeps facing right", j1.moved > 50 and near(j1.travel_vs_view, 90) and near(j1.hero_vs_view, 90), fmt(j1))
        T.expect("jumps were translated", mod.state.translated >= 3, "translated " .. mod.state.translated .. " of " .. mod.state.jumps)
        wait_s(1)

        -- another camera type (the game switches to cam_ranged when the hero aims a bow or a spell): the hero is the orientation again, and released after
        T.section("other camera")
        risen.camera.use_type("cam_ranged")
        T.sleep_frames(5)
        c = risen.camera.get()
        T.expect("ranged: hero is the orientation", c.type == "cam_ranged" and c.orientation ~= nil and c.orientation == risen.entity.player(), tostring(c.type) .. " " .. tostring(c.orientation))
        local rturn = hold("ranged turn", "TurnRight", 1.0)
        T.expect("ranged: turn turns the hero (game's own camera)", math.abs(rturn.hero_turn) > 15, fmt(rturn))
        risen.camera.use_type(mod.camera)
        T.sleep_frames(5)
        c = risen.camera.get()
        T.expect("back to ours: world-relative again", c.type == mod.camera and c.orientation == nil, tostring(c.type) .. " " .. tostring(c.orientation))

        -- a quick save and load: the game sets the camera targets again when it hands the hero to the player
        T.section("load")
        T.wait_until(function() return risen.world.is_save_allowed() end, 30, "saving allowed")
        local saved = false
        risen.on("save.after", function(_, ok) saved = ok end)
        risen.world.quick_save()
        T.wait_until(function() return saved end, 60, "savegame written")
        T.sleep(100)
        risen.world.quick_load()
        T.wait_event("session.start", 120, function(m) return m == "load" end)
        wait_s(5)
        c = risen.camera.get()
        t = risen.camera.type(mod.camera)
        T.expect("loaded: still patched and world-relative", t.CamControl_AxisX == "True" and c.type == mod.camera and c.orientation == nil, tostring(c.type) .. " " .. tostring(c.orientation) .. " " .. t.CamControl_AxisX)
        local lright = hold("loaded right", "MoveRight", 1.5)
        T.expect("loaded: hero turns where he runs", near(lright.hero_vs_view, 90) and math.abs(lright.cam_turn) < TOL, fmt(lright))
        T.finish()
    end)
end
