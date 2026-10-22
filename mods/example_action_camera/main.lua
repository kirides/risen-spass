-- EXAMPLE: an action camera. The mouse (TurnLeft/TurnRight) orbits the camera around the hero instead of turning him, the move keys walk relative to the view,
-- and the hero turns toward where he runs - the third-person camera of Elden Ring / Dark Souls instead of Risen's camera that is glued behind the hero's back.
--
-- What you learn
--   * The engine's camera types are data (docs/RE.md section 61): risen.camera.patch_type(name, props) changes them while the game runs. Three flags of `cam_normal` do most of the work:
--     CamControl_AxisX (the turn input goes to the camera's yaw), CharacterMoveInCameraSpace (the move keys are relative to the view) and CharacterRotationInCameraSpace = false
--     (the hero does not keep facing the camera's direction).
--   * The camera's yaw is measured relative to an "orientation" entity, the hero by default: when he turns, the camera turns with him. risen.camera.set_targets(player, nil)
--     makes it world-relative; the game sets (hero, hero) again on its own at times, so the mod checks risen.camera.get().orientation every frame.
--   * Entity:align(direction) is the direction the character's body turns toward, with his own turn speed (the engine keeps it until it is replaced).
--   * Fights: with a weapon drawn (the hero's state `PS_Melee*`, Entity:state()) an attack or a parry turns the hero to the camera first - the input.action handler runs
--     before the game handles the key - and for COMBAT_HOLD seconds he keeps facing it, so the move keys strafe around what you look at.
--   * Space is the action MoveUp, and the game reads the held move key on the press from the HERO's point of view: forward = jump, left / right / back = a side or
--     back step. The mod holds the press back for a few frames and replays it (risen.input.simulate) so that it means what the player means: without a weapon, running
--     to the right (D, the hero faces right) is a jump where he runs (the game is shown MoveForward for the take-off); with a weapon he snaps to the camera first and the
--     step goes to the right of the view. See the comment at JUMP_KEY.
--   * Only the follow camera is changed: aiming a bow or a spell (cam_ranged*), dialogues and interactions keep the game's own camera, and the hero is the orientation again there.
--
-- Run the live check (see docs/EXAMPLES.md):
--   .\scripts\test-run.ps1 -Instance 1 -Mods example_action_camera -WaitForResult -Seconds 300 -Scenario ex_action_camera -ShowChecks

local CAMERA = "cam_normal"                -- the type this mod changes (the game's follow camera; melee stays on it)
local FLAGS = { CamControl_AxisX = true, CharacterMoveInCameraSpace = true, CharacterRotationInCameraSpace = false }
local COMBAT_HOLD = 1.5                    -- seconds the hero keeps facing the camera after an attack or a parry (weapon drawn)
local COMBAT_SNAP = true                   -- turn to the camera at once when the attack key goes down (false: the normal turn speed)
local JUMP_TIMEOUT = 3                     -- seconds: give the keys back even when the jump state was not seen starting
local TRANSLATE_JUMPS = not (risen.test.is_active() and risen.test.param("translate", "1") == "0")   -- a test run can compare with the game's own jumps

local held = {}                            -- move key -> physically down (from the input.action events; a test's risen.input.simulate counts as physical)
local MOVES = { MoveForward = true, MoveBackward = true, MoveLeft = true, MoveRight = true }
-- with a weapon drawn these turn the hero to the camera
local COMBAT = { PrimaryAction = true, SecondaryAction = true, Attack = true, Parade = true, CounterAttack = true }

local state = { world_relative = false, aligned = 0, weapon = false, combat_until = -1, now = 0, last_dir = nil, jumps = 0, translated = 0 }

-- The camera's view on the ground, as a unit vector { x, 0, z }, or nil.
local function view_direction(cam)
    local len = math.sqrt(cam.dx * cam.dx + cam.dz * cam.dz)
    if len < 1e-4 then return nil end
    return { cam.dx / len, 0, cam.dz / len }
end

-- two flat directions closer than about half a degree (the second may be nil)
local function same_direction(a, b)
    if not b then return false end
    local la, lb = math.sqrt(a[1] * a[1] + a[3] * a[3]), math.sqrt(b[1] * b[1] + b[3] * b[3])
    if la < 1e-6 or lb < 1e-6 then return false end
    return (a[1] * b[1] + a[3] * b[3]) / (la * lb) > 0.99996
end

-- the hero's state tells whether a weapon is drawn: PS_Melee* (drawn), PS_Normal* (holstered); a dodge passes PS_Jump / ZS_Jump_End / PS_Routine, which keep the last answer
local function update_weapon(player)
    local st = player:state() or ""
    if st:find("^PS_Melee") then state.weapon = true
    elseif st:find("^PS_Normal") then state.weapon = false end
end

local function v(b) return b and 1 or 0 end

local function moving() return held.MoveForward or held.MoveBackward or held.MoveLeft or held.MoveRight end

-- The world direction the move keys ask for, relative to the camera's view, or nil when no move key is down.
local function wish_direction(cam)
    local f = v(held.MoveForward) - v(held.MoveBackward)
    local r = v(held.MoveRight) - v(held.MoveLeft)
    if f == 0 and r == 0 then return nil end
    local view = view_direction(cam)
    if not view then return nil end
    local fx, fz = view[1], view[3]
    local rx, rz = fz, -fx                              -- 90 degrees to the right of the view
    return { f * fx + r * rx, 0, f * fz + r * rz }
end

-- The move keys that point `dir` from the hero's point of view (yaw: where he faces), in 45 degree steps.
local function hero_keys(dir, yaw)
    local rel = (math.deg(math.atan2(dir[1], dir[3])) - yaw) % 360   -- 0 ahead of him, 90 to his right
    if rel > 180 then rel = rel - 360 end
    local keys = {}
    if math.abs(rel) <= 67.5 then keys.MoveForward = true end
    if math.abs(rel) >= 112.5 then keys.MoveBackward = true end
    if rel >= 22.5 and rel <= 157.5 then keys.MoveRight = true end
    if rel <= -22.5 and rel >= -157.5 then keys.MoveLeft = true end
    return keys
end

local function same_keys(a, b)
    for k in pairs(MOVES) do
        if (a[k] or false) ~= (b[k] or false) then return false end
    end
    return true
end

-- The jump key. On the keyboard Space is the action MoveUp (the action `Jump` is only on a gamepad button, and always a plain jump): the game decides on the
-- press (Script_Game OnPlayerGameKeyPressed, RE.md section 61): MoveForward held -> a jump; otherwise MoveLeft / MoveRight / MoveBackward held -> a side step /
-- back step (PS_StrafeLeft / PS_StrafeRight / PS_JumpBack), all relative to the HERO. With our camera the hero faces where he runs, so D (running to the right of
-- the view) is "his right" and Space would side-step away from his path. So the press is held back for a few frames (the handler returns true) and replayed with
-- risen.input.simulate once the game will read it the way the player means it:
--   * no weapon ("translate"): the game is shown the keys as the hero sees them (D while he faces right -> MoveForward) for the take-off, so it is a jump where he
--     runs; the real keys come back as soon as the jump has started (the flight follows the keys relative to the view, like walking)
--   * weapon drawn ("snap"): the hero snaps to the camera first, so D is "right" for him too and the game's side step goes to the right of the view
local JUMP_KEY = "MoveUp"
-- the states of a jump or step: PS_Jump, the steps without a weapon (PS_StrafeLeft / PS_StrafeRight / PS_JumpBack) and with one (PS_Melee_Move, also the attacks; then ZS_Slide_Loop)
local JUMP_STATES = { PS_Jump = true, ZS_Jump_End = true, PS_StrafeLeft = true, PS_StrafeRight = true, PS_JumpBack = true, PS_Melee_Move = true, ZS_Slide_Loop = true }
local FACING_TOLERANCE = 8                 -- degrees: closer than this to the view counts as facing it
local MAX_WAIT_FRAMES = 6                  -- replay after this many frames even when the snap did not finish
local jump = { active = false, mode = nil, stage = nil, keys = nil, expect = {}, replaying = false, seen = false, t0 = 0, frames = 0 }

local function simulate(key, down)
    jump.expect[key] = down and "pressed" or "released"
    risen.input.simulate(key, down)
end

local function angle_between(a, b) -- |b - a| in degrees, 0..180
    local d = (b - a) % 360
    return d > 180 and 360 - d or d
end

local function keys_text(t)
    local s = {}
    for _, k in ipairs({ "MoveForward", "MoveBackward", "MoveLeft", "MoveRight" }) do if t[k] then s[#s + 1] = k end end
    return table.concat(s, "+")
end

-- A trace of every jump in the log (spass.log, "action camera trace"): the engine's own key states, the hero's state and facing, every key event, per frame from
-- the jump key press until a few seconds later. It is how a jump that looks wrong in play is explained afterwards.
local TRACE = true
local trace = { frames = 0, until_t = -1 }
local TRACE_KEYS = { "MoveForward", "MoveBackward", "MoveLeft", "MoveRight", "MoveUp", "Jump" }
local function tlog(fmt, ...) if TRACE and risen.clock() < trace.until_t then risen.log.info("action camera trace: " .. string.format(fmt, ...)) end end
local function engine_keys()
    local s = {}
    for _, k in ipairs(TRACE_KEYS) do
        if risen.input.pressed("action:" .. k) then s[#s + 1] = k end
    end
    return table.concat(s, "+")
end

risen.on("input.action", function(ev)
    local a = ev.action
    if (a == JUMP_KEY or a == "Jump") and ev.phase == "pressed" and not jump.replaying and TRACE then
        trace.frames, trace.until_t = 0, risen.clock() + 4
        local p = risen.entity.player()
        local pose, cam = p and p:pose(), risen.camera.get()
        tlog("%s pressed: real keys %s, engine keys %s, hero yaw %.1f, view yaw %.1f, state %s, weapon %s, busy %s", a, keys_text(held), engine_keys(),
            pose and pose.yaw or -1, cam and cam.yaw or -1, p and p:state() or "?", tostring(state.weapon), tostring(jump.active))
    end
    if (MOVES[a] or a == JUMP_KEY or a == "Jump") and ev.phase ~= "held" then tlog("event %s %s", a, ev.phase) end
    if MOVES[a] then
        if jump.active and jump.mode == "translate" then
            if jump.expect[a] == ev.phase then jump.expect[a] = nil; return end   -- our own swap: let the game have it
            if ev.phase == "held" and jump.keys[a] then return end                 -- the shown key stays down for the game
            -- a real key change during the take-off: remembered for afterwards, the game keeps the shown keys
            if ev.phase == "pressed" or ev.phase == "held" then held[a] = true elseif ev.phase == "released" then held[a] = false end
            return true
        end
        if ev.phase == "pressed" or ev.phase == "held" then held[a] = true
        elseif ev.phase == "released" then held[a] = false end
        return
    end
    if a == JUMP_KEY then
        if jump.replaying and ev.phase == "pressed" then jump.replaying = false; return end   -- our replay
        if jump.active then return true end                                                 -- the real press / release while we wait
        if ev.phase ~= "pressed" or not moving() or not TRANSLATE_JUMPS then return end
        local player, cam = risen.entity.player(), risen.camera.get()
        if not (player and cam and cam.type == CAMERA and not cam.fixed) then return end
        local dir, pose, view = wish_direction(cam), player:pose(), view_direction(cam)
        if not (dir and pose and view) then return end
        state.jumps = state.jumps + 1
        if state.weapon then
            if angle_between(pose.yaw, cam.yaw) <= FACING_TOLERANCE then return end   -- already facing the camera: the game's side step is right as it is
            if player:align(view, true) then state.last_dir = view end
            state.translated = state.translated + 1
            jump.active, jump.mode, jump.stage, jump.frames = true, "snap", "face", 0
            tlog("snap: weapon drawn, turning to the camera before the step")
            return true
        end
        local keys = hero_keys(dir, pose.yaw)
        if same_keys(keys, held) then return end   -- he sees it the same way (facing the camera)
        state.translated = state.translated + 1
        jump.active, jump.mode, jump.stage, jump.keys, jump.expect, jump.seen = true, "translate", "swap", keys, {}, false
        tlog("translate: hero sees %s, held back the press", keys_text(keys))
        return true
    end
    if not (COMBAT[a] and ev.phase == "pressed" and state.weapon) then return end
    -- an attack or parry: this runs before the game handles the key, so the swing starts already facing the camera
    local player, cam = risen.entity.player(), risen.camera.get()
    if not (player and cam and cam.type == CAMERA and not cam.fixed) then return end
    local view = view_direction(cam)
    if not view then return end
    if player:align(view, COMBAT_SNAP) then state.last_dir = view end
    state.combat_until = risen.clock() + COMBAT_HOLD
end)

-- per frame: the stages of a held-back jump key
local function run_jump(player, cam)
    if not jump.active then return end
    if jump.stage == "face" then   -- snap: wait until he faces the camera
        jump.frames = jump.frames + 1
        local pose = player:pose()
        if (pose and angle_between(pose.yaw, cam.yaw) <= FACING_TOLERANCE) or jump.frames >= MAX_WAIT_FRAMES then jump.stage = "press" end
        return
    end
    if jump.stage == "swap" then   -- translate: show the game the hero's keys
        for k in pairs(MOVES) do
            if held[k] and not jump.keys[k] then simulate(k, false) end
            if jump.keys[k] and not held[k] then simulate(k, true) end
        end
        jump.stage = "press"
    elseif jump.stage == "press" then
        jump.replaying = true
        risen.input.simulate(JUMP_KEY, true)
        jump.stage, jump.t0 = "release", risen.clock()
    elseif jump.stage == "release" then
        risen.input.simulate(JUMP_KEY, false)
        if jump.mode == "snap" then
            state.combat_until = risen.clock() + COMBAT_HOLD   -- he keeps facing the camera after the step, like after an attack
            jump.active, jump.stage = false, nil
        else
            jump.stage = "air"
        end
    elseif jump.stage == "air" then
        -- translate: the real keys go back as soon as the jump has started
        if JUMP_STATES[player:state() or ""] then jump.seen = true end
        if jump.seen or risen.clock() - jump.t0 > JUMP_TIMEOUT then
            for k in pairs(MOVES) do
                if jump.keys[k] and not held[k] then simulate(k, false) end
                if held[k] and not jump.keys[k] then simulate(k, true) end
            end
            jump.active, jump.stage = false, nil
        end
    end
end

local patched = false
local function patch()
    local ok, why = risen.camera.patch_type(CAMERA, FLAGS)
    if not ok then risen.log.warn("action camera: cannot patch " .. CAMERA .. ": " .. tostring(why)) end
    patched = ok == true
end

local function frame()
    state.now = risen.clock()
    local player = risen.entity.player()
    local cam = risen.camera.get()
    if not player or not cam then return end
    if not patched then patch() end
    update_weapon(player)
    local ours = cam.type == CAMERA and not cam.fixed
    -- the yaw is world-relative only in our camera; the game's other cameras (bow, spells) keep theirs behind the hero
    if ours and cam.orientation ~= nil then
        state.world_relative = risen.camera.set_targets(player, nil) == true
    elseif not ours and cam.orientation == nil and cam.target ~= nil then
        risen.camera.set_targets(player, player)
        state.world_relative = false
    end
    if not ours then state.last_dir = nil end -- the game's own turning runs there: hand the direction over again afterwards
    local st = player:state() or ""
    local airborne = jump.active or JUMP_STATES[st] ~= nil   -- no turning during a jump or step: it goes where it was started
    if ours and not airborne then
        -- fighting (an attack or parry in the last COMBAT_HOLD seconds): face the camera, the move keys strafe; otherwise the hero turns toward where he runs
        local dir
        if state.weapon and state.now < state.combat_until then dir = view_direction(cam)
        else dir = wish_direction(cam) end
        -- only a new direction is handed over: the engine keeps the last one, and setting the same one again would cancel the snap of an attack key
        -- (`immediate` is a flag the engine clears after the turn, a repeated align(dir, false) clears it before)
        if dir and not same_direction(dir, state.last_dir) and player:align(dir) then
            state.last_dir = dir
            state.aligned = state.aligned + 1
        end
    end
    run_jump(player, cam)
    if TRACE and state.now < trace.until_t then
        trace.frames = trace.frames + 1
        local pose = player:pose()
        local yaw = pose and pose.yaw or -1
        local sig = st .. "|" .. engine_keys() .. "|" .. tostring(jump.stage) .. "|" .. math.floor(yaw / 5)
        if sig ~= trace.last then -- only frames where something changed
            trace.last = sig
            tlog("frame %d: state %s, engine keys %s, hero yaw %.1f, view yaw %.1f, stage %s, pos %.0f %.0f", trace.frames, st, engine_keys(), yaw, cam.yaw,
                tostring(jump.stage), pose and pose.x or 0, pose and pose.z or 0)
        end
    end
end

risen.on("tick", frame)
-- the types are loaded again with every start: patch again
risen.on("session.start", function()
    patched = false
    state.last_dir = nil
    jump.active, jump.stage, jump.replaying, jump.expect = false, nil, false, {}
end)

if risen.test.is_active() then
    require("test_example_action_camera")({ state = state, held = held, camera = CAMERA, flags = FLAGS })
end
