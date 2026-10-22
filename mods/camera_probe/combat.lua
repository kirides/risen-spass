---@diagnostic disable: need-check-nil, param-type-mismatch
-- phase=combat of scenario `camprobe`: the player's states and actions while fighting (weapon drawn, dodge, attack), stock camera.
return function()
    local T = risen.test
    local function log(s) risen.log.info("COMBATPROBE " .. s) end
    local function wait_s(s)
        local t0 = T.elapsed()
        T.wait_until(function() return T.elapsed() - t0 >= s end, s + 30, "wait")
    end

    T.plan { "new game started" }
    local acts = risen.input.actions()
    local list = {}
    for i = 0, #acts do list[#list + 1] = i .. "=" .. tostring(acts[i]) end
    log("actions " .. table.concat(list, " "))

    local last
    local function watch(tag)
        local p = risen.entity.player()
        if not p then return end
        local st = p:state() or "?"
        local pose = p:pose()
        local key = st
        if key ~= last then
            log(string.format("%s state %s yaw %.1f", tag, st, pose and pose.yaw or -1))
            last = key
        end
    end
    local tag = "idle"
    risen.on("tick", function() watch(tag) end)
    risen.on("input.action", function(ev)
        if ev.phase == "pressed" or ev.phase == "released" then log(string.format("action %s %s", ev.action, ev.phase)) end
    end)

    local function press(name, s)
        risen.input.simulate(name, true); wait_s(s or 0.2); risen.input.simulate(name, false)
    end

    T.run(function()
        local started = T.wait_event("session.start", 240, function(m) return m == "new" end)
        T.eq("new game started", started, "new")
        wait_s(30)
        pcall(risen.gui.close, 26)
        local p = risen.entity.player()
        risen.item.give(p, "It_1H_Club_01", 1)
        wait_s(1)
        tag = "draw"; press("WeaponMode", 0.3); wait_s(3)
        tag = "attack"; press("PrimaryAction", 0.2); wait_s(2)
        local y0 = p:pose().yaw
        tag = "dodge left (jump)"; risen.input.simulate("MoveLeft", true); press("Jump", 0.2); wait_s(0.5); risen.input.simulate("MoveLeft", false); wait_s(1.5)
        log(string.format("after dodge-left yaw change %.1f", p:pose().yaw - y0))
        tag = "dodge back (jump)"; risen.input.simulate("MoveBackward", true); press("Jump", 0.2); wait_s(0.5); risen.input.simulate("MoveBackward", false); wait_s(1.5)
        tag = "parry"; press("SecondaryAction", 0.8); wait_s(1)
        tag = "move left"; press("MoveLeft", 1.0); wait_s(1)
        tag = "holster"; press("WeaponMode", 0.3); wait_s(3)
        T.finish()
    end)
end
