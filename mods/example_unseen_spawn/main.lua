-- EXAMPLE 16: an enemy appears where the player cannot see it -- a waypoint or freepoint around him that is behind his back or hidden by the terrain.
--
-- What you learn
--   * risen.world.points({ near = player, radius = ... }) lists the game's own freepoints and anchors around a position, nearest first (guard posts, patrol waypoints, ...).
--   * risen.world.is_seen_by(point, viewers, { camera = true }) asks the engine's physics scene whether a ray from the viewer to the point is blocked (terrain, rocks, buildings)
--     and whether the point lies inside the viewer's field of view. With `camera = true` the viewer looks along the render camera (what is on the player's screen) instead of
--     the way his body faces; the camera's opening is used unless you give `fov` / `vfov` (degrees). `range` limits the distance.
--   * risen.math: vectors are `{ x, y, z }` tables or entities (+y is up, a yaw of 0 looks along +z, 90 along +x, degrees). `M.is_behind(point, origin, facing, margin)` is true when the point
--     is more than 90 + margin degrees away from the way `facing` (an entity, a yaw or a direction vector) looks. `M.distance2d` is the distance on the ground.
--   * risen.random is this mod's own random stream (nobody else's math.random calls disturb it): `choice(list)`, `int`, `float`, `shuffle`, `weighted`, `point_in_ring`; `seed(n)` makes a
--     run repeatable (tests), `persist("name")` saves the stream's state with the savegame.
--   * Two policies: mode "behind" (only points behind the player's back, 110 degrees or more off the camera's direction) and mode "unseen" (any point he cannot see, also one in front of him
--     behind a rock). Both never use a point he can see.
--
-- Run the live check (about a minute; see docs/EXAMPLES.md):
--   .\scripts\test-run.ps1 -Instance 1 -Mods example_unseen_spawn -WaitForResult -Seconds 300 -Scenario ex_unseen_spawn -ShowChecks

local M, W, R = risen.math, risen.world, risen.random

local ENEMY = "Wolf"            -- what appears
local MIN_DISTANCE = 700        -- not closer than this to the player (on the ground) ...
local MAX_DISTANCE = 2200       -- ... and not farther than this
local SIGHT = 4000             -- how far the player can see: a point farther away than this counts as unseen anyway
local BEHIND_MARGIN = 20        -- "behind" means more than 90 + 20 degrees away from where the camera looks
local INTERVAL = 45             -- seconds between two ambushes (real time)
local MAX_ALIVE = 2             -- never more than this many of our wolves at once

local spawned = {}              -- the wolves this mod made

-- The spawn points that qualify, nearest first. `opts.points` replaces the game's own points (a list of { x, y, z }); `opts.mode` is "behind" (default) or "unseen".
-- Returns the list, or nil and the reason (no camera: it cannot be told what the player sees).
local function candidates(player, opts)
    opts = opts or {}
    local mode = opts.mode or "behind"
    local points = opts.points or W.points({ near = player, radius = MAX_DISTANCE, limit = 300 }) or {}
    -- "behind" is relative to where the camera looks: that is the direction the player sees. Without a camera: where his body faces.
    local cam = W.camera()
    local facing = cam and { cam.dx, cam.dy, cam.dz } or player
    local out = {}
    for _, p in ipairs(points) do
        if p.kind ~= "helper" then
            local d = M.distance2d(p, player)
            if d and d >= MIN_DISTANCE and d <= MAX_DISTANCE then
                -- the enemy would stand with its feet on the point: ask about a spot 100 units above it (body height)
                local seen, why = W.is_seen_by({ p.x, p.y + 100, p.z }, player, { camera = true, range = SIGHT })
                if seen == nil then return nil, why end
                if not seen and (mode == "unseen" or M.is_behind(p, player, facing, BEHIND_MARGIN)) then
                    out[#out + 1] = p
                end
            end
        end
    end
    return out
end

-- One random qualifying point, or nil and a reason.
local function pick(player, opts)
    local list, why = candidates(player, opts)
    if not list then return nil, why end
    if #list == 0 then return nil, "no point around the player is hidden from him" end
    return R.choice(list)
end

-- Spawns the enemy at a hidden point. Returns the enemy and the point, or nil and a reason.
---@return Entity|nil enemy
---@return any point_or_reason
local function ambush(player, opts)
    local point, why = pick(player, opts)
    if not point then return nil, why end
    local enemy = risen.npc.spawn(ENEMY, point.x, point.y + 5, point.z, { name = ("EX_Ambush_%d"):format(#spawned + 1) })
    if not enemy then return nil, "the enemy could not be spawned" end
    spawned[#spawned + 1] = enemy
    return enemy, point
end

local function alive()
    local n = 0
    for _, e in ipairs(spawned) do
        if e:valid() and not e:is_dead() then n = n + 1 end
    end
    return n
end

-- the periodic ambush (not in a test run: the scenario drives ambush() itself)
if not risen.test.is_active() then
    local last = os.clock()
    risen.on("tick", function()
        if os.clock() - last < INTERVAL then return end
        last = os.clock()
        local player = risen.entity.player()
        if not player then return end
        local state = player:state() or ""
        if not state:find("^PS_Normal") or alive() >= MAX_ALIVE then return end     -- only while he walks around, never a crowd
        local enemy, point = ambush(player)
        if enemy and type(point) == "table" then
            risen.log.info(("ambush: %s at (%.0f, %.0f, %.0f), %.0f units from the player"):format(enemy:name(), point.x, point.y, point.z, M.distance2d(point, player)))
        end
    end)
end

if risen.test.is_active() then
    require("test_example_unseen_spawn")({ candidates = candidates, pick = pick, ambush = ambush, min = MIN_DISTANCE, max = MAX_DISTANCE, margin = BEHIND_MARGIN, sight = SIGHT })
end
