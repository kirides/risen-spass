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
--   * Only the follow camera is changed: aiming a bow or a spell (cam_ranged*), dialogues and interactions keep the game's own camera, and the hero is the orientation again there.
--
-- Run the live check (see docs/EXAMPLES.md):
--   .\scripts\test-run.ps1 -Instance 1 -Mods example_action_camera -WaitForResult -Seconds 300 -Scenario ex_action_camera -ShowChecks

local CAMERA = "cam_normal"                -- the type this mod changes (the game's follow camera; melee stays on it)
local FLAGS = { CamControl_AxisX = true, CharacterMoveInCameraSpace = true, CharacterRotationInCameraSpace = false }

local held = {}                            -- action name -> down (from the input.action events: also sees keys a test simulates)
local MOVES = { MoveForward = true, MoveBackward = true, MoveLeft = true, MoveRight = true }

risen.on("input.action", function(ev)
    if MOVES[ev.action] then
        if ev.phase == "pressed" or ev.phase == "held" then held[ev.action] = true
        elseif ev.phase == "released" then held[ev.action] = false end
    end
end)

local function v(b) return b and 1 or 0 end

-- The world direction the move keys ask for, relative to the camera's view, or nil when no move key is down.
local function wish_direction(cam)
    local f = v(held.MoveForward) - v(held.MoveBackward)
    local r = v(held.MoveRight) - v(held.MoveLeft)
    if f == 0 and r == 0 then return nil end
    local len = math.sqrt(cam.dx * cam.dx + cam.dz * cam.dz)
    if len < 1e-4 then return nil end
    local fx, fz = cam.dx / len, cam.dz / len          -- the view, flat
    local rx, rz = fz, -fx                              -- 90 degrees to the right of it
    return { f * fx + r * rx, 0, f * fz + r * rz }
end

local patched = false
local function patch()
    local ok, why = risen.camera.patch_type(CAMERA, FLAGS)
    if not ok then risen.log.warn("action camera: cannot patch " .. CAMERA .. ": " .. tostring(why)) end
    patched = ok == true
end

local state = { world_relative = false, aligned = 0 }

local function frame()
    local player = risen.entity.player()
    local cam = risen.camera.get()
    if not player or not cam then return end
    if not patched then patch() end
    local ours = cam.type == CAMERA and not cam.fixed
    -- the yaw is world-relative only in our camera; the game's other cameras (bow, spells) keep theirs behind the hero
    if ours and cam.orientation ~= nil then
        state.world_relative = risen.camera.set_targets(player, nil) == true
    elseif not ours and cam.orientation == nil and cam.target ~= nil then
        risen.camera.set_targets(player, player)
        state.world_relative = false
    end
    if ours then
        local dir = wish_direction(cam)
        if dir and player:align(dir) then state.aligned = state.aligned + 1 end
    end
end

risen.on("tick", frame)
-- the types are loaded again with every start: patch again
risen.on("session.start", function() patched = false end)

if risen.test.is_active() then
    require("test_example_action_camera")({ state = state, held = held, camera = CAMERA, flags = FLAGS })
end
