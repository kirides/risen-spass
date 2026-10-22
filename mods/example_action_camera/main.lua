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
--   * Fights: with a weapon drawn (the hero's state `PS_Melee*`, Entity:state()) an attack, a parry or a dodge (Jump + a direction) turns the hero to the camera first - the
--     input.action handler runs before the game handles the key - and for COMBAT_HOLD seconds he keeps facing it: the move keys strafe and the next dodge goes where you press
--     relative to the view, instead of the hero turning toward every dodge direction. The dodge reads its direction from the hero's facing in the frame the key
--     arrives, so a Jump press while he does not face the camera yet is held back (the handler returns true) and replayed with risen.input.simulate once he does.
--   * Only the follow camera is changed: aiming a bow or a spell (cam_ranged*), dialogues and interactions keep the game's own camera, and the hero is the orientation again there.
--
-- Run the live check (see docs/EXAMPLES.md):
--   .\scripts\test-run.ps1 -Instance 1 -Mods example_action_camera -WaitForResult -Seconds 300 -Scenario ex_action_camera -ShowChecks

local CAMERA = "cam_normal"                -- the type this mod changes (the game's follow camera; melee stays on it)
local FLAGS = { CamControl_AxisX = true, CharacterMoveInCameraSpace = true, CharacterRotationInCameraSpace = false }
local COMBAT_HOLD = 1.5                    -- seconds the hero keeps facing the camera after an attack, parry or dodge (weapon drawn)
local COMBAT_SNAP = true                   -- turn to the camera at once when the attack / dodge key goes down (false: the normal turn speed)

local held = {}                            -- action name -> down (from the input.action events: also sees keys a test simulates)
local MOVES = { MoveForward = true, MoveBackward = true, MoveLeft = true, MoveRight = true }
-- with a weapon drawn these turn the hero to the camera: attacks, parry, and Jump (Jump + a direction is the dodge)
local COMBAT = { PrimaryAction = true, SecondaryAction = true, Jump = true, Attack = true, Parade = true, CounterAttack = true }

local state = { world_relative = false, aligned = 0, weapon = false, combat_until = -1, now = 0, last_dir = nil }

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

local function angle_between(a, b) -- |b - a| in degrees, 0..180
    local d = (b - a) % 360
    return d > 180 and 360 - d or d
end

-- The dodge (Jump + a move key) takes its direction from where the hero faces in the frame the key arrives, before a turn can happen: when he does not face the camera
-- yet, the press is held back, the hero snaps to the camera, and the press is replayed (risen.input.simulate) once he faces it - so D + Space is a dodge to the right of
-- the VIEW, not to the right of a hero who was running to the right.
local FACING_TOLERANCE = 8                 -- degrees: closer than this to the view counts as facing it
local MAX_WAIT_FRAMES = 6                  -- replay the jump after this many frames even when the turn did not finish
local jump = { pending = false, frames = 0, replaying = false, release_next = false }

risen.on("input.action", function(ev)
    if MOVES[ev.action] then
        if ev.phase == "pressed" or ev.phase == "held" then held[ev.action] = true
        elseif ev.phase == "released" then held[ev.action] = false end
        return
    end
    if ev.action == "Jump" then
        if jump.replaying and ev.phase == "pressed" then jump.replaying = false; return end -- our own replay: let the game have it
        if jump.pending then return true end                                              -- held / released while we wait: swallowed, the replay presses and releases
    end
    if not (COMBAT[ev.action] and ev.phase == "pressed" and state.weapon) then return end
    -- this runs before the game handles the key: the attack / dodge starts already facing the camera
    local player, cam = risen.entity.player(), risen.camera.get()
    if not (player and cam and cam.type == CAMERA and not cam.fixed) then return end
    local view = view_direction(cam)
    if not view then return end
    if player:align(view, COMBAT_SNAP) then state.last_dir = view end
    state.combat_until = risen.clock() + COMBAT_HOLD
    if ev.action == "Jump" then
        local pose = player:pose()
        if pose and angle_between(pose.yaw, cam.yaw) > FACING_TOLERANCE then
            jump.pending, jump.frames = true, 0
            return true -- the game does not see this press; frame() replays it when the hero faces the camera
        end
    end
end)

-- per frame: replay a held-back jump once the hero faces the camera, release it one frame later
local function replay_jump(player, cam)
    if jump.release_next then
        jump.release_next = false
        risen.input.simulate("Jump", false)
    end
    if not jump.pending then return end
    jump.frames = jump.frames + 1
    local pose = player:pose()
    if (pose and angle_between(pose.yaw, cam.yaw) <= FACING_TOLERANCE) or jump.frames >= MAX_WAIT_FRAMES then
        jump.pending = false
        jump.replaying = true
        jump.release_next = true
        state.combat_until = risen.clock() + COMBAT_HOLD
        risen.input.simulate("Jump", true)
    end
end

local function v(b) return b and 1 or 0 end

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
    if ours then
        -- fighting (an attack, parry or dodge in the last COMBAT_HOLD seconds): face the camera, the move keys strafe and dodges go relative to the view;
        -- otherwise the hero turns toward where he runs
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
    replay_jump(player, cam)
end

risen.on("tick", frame)
-- the types are loaded again with every start: patch again
risen.on("session.start", function()
    patched = false
    state.last_dir = nil
    jump.pending, jump.replaying, jump.release_next = false, false, false
end)

if risen.test.is_active() then
    require("test_example_action_camera")({ state = state, held = held, camera = CAMERA, flags = FLAGS })
end
