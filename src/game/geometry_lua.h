#pragma once

// The Lua part of the geometry API (M13, docs/RE.md section 38, docs/LUA_API.md "Geometry, line of sight and randomness"), on top of the natives of
// lua_geometry.cpp (risen.world.trace / engine_free_los / camera, Entity:bounds, Entity:position / direction). Kept in a header so the unit tests can load the
// chunks against mocks.
//
// kMathChunk (shared, runs once): risen.math -- vectors on {x, y, z} tables and entity handles, yaw / pitch, cones, offsets.
// kLineOfSightChunk (shared): risen.world.raycast / line_of_sight / can_see / is_seen_by.
// kRandomChunk (per mod): risen.random -- seedable streams.
//
// Conventions (verified in the game, docs/RE.md section 38): +y is up, the ground is the x/z plane; a yaw of 0 looks along +z and 90 along +x (yaw grows towards the
// player's right); angles of the API are degrees; a pitch above the horizon is positive.
namespace spass::game {

inline constexpr const char* kMathChunk = R"LUA(
local risen = ...
local M = {}
risen.math = M

local sqrt, sin, cos, atan2, asin, acos, abs, floor, huge, pi = math.sqrt, math.sin, math.cos, math.atan2, math.asin, math.acos, math.abs, math.floor, math.huge, math.pi
local type, pcall, rawget = type, pcall, rawget
local RAD, DEG = pi / 180, 180 / pi

M.pi = pi

local function finite(n) return type(n) == "number" and n == n and n ~= huge and n ~= -huge end

-- a new vector: the named fields and the array part hold the same numbers, so it can be passed wherever a position is expected ({ x, y, z } or { x = , y = , z = })
local function vec(x, y, z) return { x, y, z, x = x, y = y, z = z } end

-- any point-like value -> x, y, z  |  nil, reason.  An entity is read at its position (its feet); tables are read raw (no metamethods run).
local function xyz(v)
    local t = type(v)
    if t == "table" then
        local x, y, z = rawget(v, "x"), rawget(v, "y"), rawget(v, "z")
        if x == nil and y == nil and z == nil then x, y, z = rawget(v, 1), rawget(v, 2), rawget(v, 3) end
        if finite(x) and finite(y) and finite(z) then return x, y, z end
        return nil, "a point needs three finite numbers x, y, z (or [1], [2], [3])"
    elseif t == "userdata" then
        local ok, x, y, z = pcall(function() return v:position() end)
        if ok and finite(x) and finite(y) and finite(z) then return x, y, z end
        return nil, "the entity is gone or has no position"
    end
    return nil, "expected a point {x, y, z} or an entity"
end
M.xyz = xyz

local function pair(a, b)
    local ax, ay, az = xyz(a)
    if not ax then return nil, ay end
    local bx, by, bz = xyz(b)
    if not bx then return nil, by end
    return ax, ay, az, bx, by, bz
end

local function wrap180(a)   -- (-180, 180]
    a = a % 360
    if a > 180 then a = a - 360 end
    return a
end

local function wrap360(a) return a % 360 end

-- ---- construction and arithmetic -------------------------------------------------------------------------------------------------------------

function M.vec(x, y, z)
    if finite(x) and finite(y) and finite(z) then return vec(x, y, z) end
    return nil, "vec needs three finite numbers"
end

function M.point(v)
    local x, y, z = xyz(v)
    if not x then return nil, y end
    return vec(x, y, z)
end

function M.add(a, b)
    local ax, ay, az, bx, by, bz = pair(a, b)
    if not ax then return nil, ay end
    return vec(ax + bx, ay + by, az + bz)
end

function M.sub(a, b)
    local ax, ay, az, bx, by, bz = pair(a, b)
    if not ax then return nil, ay end
    return vec(ax - bx, ay - by, az - bz)
end

function M.scale(a, k)
    if not finite(k) then return nil, "the factor must be a finite number" end
    local x, y, z = xyz(a)
    if not x then return nil, y end
    return vec(x * k, y * k, z * k)
end

function M.dot(a, b)
    local ax, ay, az, bx, by, bz = pair(a, b)
    if not ax then return nil, ay end
    return ax * bx + ay * by + az * bz
end

function M.cross(a, b)
    local ax, ay, az, bx, by, bz = pair(a, b)
    if not ax then return nil, ay end
    return vec(ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx)
end

function M.length(a)
    local x, y, z = xyz(a)
    if not x then return nil, y end
    return sqrt(x * x + y * y + z * z)
end

function M.normalize(a)
    local x, y, z = xyz(a)
    if not x then return nil, y end
    local l = sqrt(x * x + y * y + z * z)
    if l < 1e-9 then return nil, "cannot normalize a zero-length vector" end
    return vec(x / l, y / l, z / l)
end

function M.clamp(x, lo, hi)
    if not (finite(x) and finite(lo) and finite(hi)) then return nil, "clamp needs three finite numbers" end
    if lo > hi then lo, hi = hi, lo end
    if x < lo then return lo elseif x > hi then return hi end
    return x
end

function M.sign(x) if not finite(x) then return nil, "not a finite number" end return x > 0 and 1 or x < 0 and -1 or 0 end

function M.round(x, digits)
    if not finite(x) then return nil, "not a finite number" end
    if digits ~= nil and not finite(digits) then return nil, "digits must be a number" end
    local m = 10 ^ floor(digits or 0)
    return floor(x * m + 0.5) / m
end

function M.deg(r) if not finite(r) then return nil, "not a finite number" end return r * DEG end
function M.rad(d) if not finite(d) then return nil, "not a finite number" end return d * RAD end

-- lerp(a, b, t): numbers or points; t is not clamped (0 = a, 1 = b)
function M.lerp(a, b, t)
    if not finite(t) then return nil, "t must be a finite number" end
    if type(a) == "number" and type(b) == "number" then
        if not (finite(a) and finite(b)) then return nil, "not a finite number" end
        return a + (b - a) * t
    end
    local ax, ay, az, bx, by, bz = pair(a, b)
    if not ax then return nil, ay end
    return vec(ax + (bx - ax) * t, ay + (by - ay) * t, az + (bz - az) * t)
end

-- ---- distances -----------------------------------------------------------------------------------------------------------------------------

function M.distance(a, b)
    local ax, ay, az, bx, by, bz = pair(a, b)
    if not ax then return nil, ay end
    local dx, dy, dz = bx - ax, by - ay, bz - az
    return sqrt(dx * dx + dy * dy + dz * dz)
end

function M.distance_sq(a, b)
    local ax, ay, az, bx, by, bz = pair(a, b)
    if not ax then return nil, ay end
    local dx, dy, dz = bx - ax, by - ay, bz - az
    return dx * dx + dy * dy + dz * dz
end

-- the distance on the ground (x/z), ignoring the height difference
function M.distance2d(a, b)
    local ax, ay, az, bx, _, bz = pair(a, b)
    if not ax then return nil, ay end
    local dx, dz = bx - ax, bz - az
    return sqrt(dx * dx + dz * dz)
end

-- ---- directions and angles (degrees; yaw 0 = +z, 90 = +x; pitch > 0 looks up) -------------------------------------------------------------------

-- the unit vector from a to b
function M.direction(a, b)
    local ax, ay, az, bx, by, bz = pair(a, b)
    if not ax then return nil, ay end
    local dx, dy, dz = bx - ax, by - ay, bz - az
    local l = sqrt(dx * dx + dy * dy + dz * dz)
    if l < 1e-9 then return nil, "the two points are the same" end
    return vec(dx / l, dy / l, dz / l)
end

-- a facing: a yaw (number, degrees), an entity (the way it looks, Entity:direction()) or a direction vector -> dx, dy, dz (unit), flat (true for a yaw number)
local function facing_vec(f)
    local t = type(f)
    if t == "number" then
        if not finite(f) then return nil, "the yaw must be a finite number" end
        local r = f * RAD
        return sin(r), 0, cos(r), true
    elseif t == "userdata" then
        local ok, x, y, z = pcall(function() return f:direction() end)
        if ok and finite(x) and finite(y) and finite(z) then
            local l = sqrt(x * x + y * y + z * z)
            if l > 1e-9 then return x / l, y / l, z / l, false end
        end
        return nil, "the entity is gone or has no direction"
    elseif t == "table" then
        local x, y, z = xyz(f)
        if not x then return nil, "a facing is a yaw in degrees, an entity or a direction vector" end
        local l = sqrt(x * x + y * y + z * z)
        if l < 1e-9 then return nil, "a facing vector must not be zero" end
        return x / l, y / l, z / l, false
    end
    return nil, "a facing is a yaw in degrees, an entity or a direction vector"
end

local function yaw_xz(x, z) if abs(x) < 1e-12 and abs(z) < 1e-12 then return 0 end return wrap360(atan2(x, z) * DEG) end

-- yaw of a direction vector or of the way an entity looks, in [0, 360)
function M.yaw_of(f)
    local dx, _, dz = facing_vec(f)
    if not dx then return nil, _ end
    return yaw_xz(dx, dz)
end

-- pitch of a direction vector or of the way an entity looks: -90 (down) .. 90 (up)
function M.pitch_of(f)
    local dx, dy = facing_vec(f)
    if not dx then return nil, dy end
    return asin(dy > 1 and 1 or dy < -1 and -1 or dy) * DEG
end

function M.yaw_to(a, b)
    local ax, ay, az, bx, _, bz = pair(a, b)
    if not ax then return nil, ay end
    return yaw_xz(bx - ax, bz - az)
end

function M.pitch_to(a, b)
    local ax, ay, az, bx, by, bz = pair(a, b)
    if not ax then return nil, ay end
    local dx, dy, dz = bx - ax, by - ay, bz - az
    local l = sqrt(dx * dx + dy * dy + dz * dz)
    if l < 1e-9 then return 0 end
    return asin(dy / l) * DEG
end

-- the unit vector of a yaw (and an optional pitch)
function M.from_yaw(yaw, pitch)
    if not finite(yaw) or (pitch ~= nil and not finite(pitch)) then return nil, "yaw and pitch must be finite numbers" end
    local y, p = yaw * RAD, (pitch or 0) * RAD
    return vec(sin(y) * cos(p), sin(p), cos(y) * cos(p))
end

-- the way an entity looks, as a unit vector (Entity:direction())
function M.facing(e)
    local dx, dy, dz = facing_vec(e)
    if not dx then return nil, dy end
    return vec(dx, dy, dz)
end

M.wrap_angle = function(a) if not finite(a) then return nil, "not a finite number" end return wrap180(a) end   -- (-180, 180]
M.wrap_yaw = function(a) if not finite(a) then return nil, "not a finite number" end return wrap360(a) end    -- [0, 360)

-- the signed shortest turn from angle a to angle b, in (-180, 180]: positive = clockwise seen from above = to the right
function M.angle_diff(a, b)
    if not (finite(a) and finite(b)) then return nil, "angles must be finite numbers" end
    return wrap180(b - a)
end

-- the angle between two direction vectors, 0 .. 180
function M.angle_between(u, v)
    local ax, ay, az, bx, by, bz = pair(u, v)
    if not ax then return nil, ay end
    local la, lb = sqrt(ax * ax + ay * ay + az * az), sqrt(bx * bx + by * by + bz * bz)
    if la < 1e-9 or lb < 1e-9 then return nil, "a zero-length vector has no angle" end
    local c = (ax * bx + ay * by + az * bz) / (la * lb)
    return acos(c > 1 and 1 or c < -1 and -1 or c) * DEG
end

-- rotates a vector around the vertical axis by `degrees` to the right (0,0,1) -> (1,0,0) for 90
function M.rotate_yaw(v, degrees)
    if not finite(degrees) then return nil, "the angle must be a finite number" end
    local x, y, z = xyz(v)
    if not x then return nil, y end
    local r = degrees * RAD
    local s, c = sin(r), cos(r)
    return vec(x * c + z * s, y, -x * s + z * c)
end

-- the point `forward` ahead, `right` to the right and `up` above `origin`, in the frame of a yaw (a facing: number, entity or vector); negative forward = behind
function M.offset(origin, facing, forward, right, up)
    forward, right, up = forward or 0, right or 0, up or 0
    if not (finite(forward) and finite(right) and finite(up)) then return nil, "forward, right and up must be finite numbers" end
    local x, y, z = xyz(origin)
    if not x then return nil, y end
    local dx, _, dz = facing_vec(facing)
    if not dx then return nil, _ end
    local l = sqrt(dx * dx + dz * dz)
    if l < 1e-9 then return nil, "the facing has no horizontal direction" end
    dx, dz = dx / l, dz / l
    return vec(x + dx * forward + dz * right, y + up, z + dz * forward - dx * right)
end

-- the point at `distance` from origin in the direction of `yaw` (degrees, 0 = +z), `dy` higher
function M.polar(origin, yaw, distance, dy)
    if not (finite(yaw) and finite(distance)) or (dy ~= nil and not finite(dy)) then return nil, "yaw and distance must be finite numbers" end
    local x, y, z = xyz(origin)
    if not x then return nil, y end
    local r = yaw * RAD
    return vec(x + sin(r) * distance, y + (dy or 0), z + cos(r) * distance)
end

-- ---- areas --------------------------------------------------------------------------------------------------------------------------------

-- inside the circle on the ground (x/z) / inside the sphere
function M.in_circle(point, center, radius)
    if not finite(radius) then return nil, "the radius must be a finite number" end
    local d, why = M.distance2d(point, center)
    if not d then return nil, why end
    return d <= radius
end

function M.in_sphere(point, center, radius)
    if not finite(radius) then return nil, "the radius must be a finite number" end
    local d, why = M.distance(point, center)
    if not d then return nil, why end
    return d <= radius
end

-- the signed horizontal angle from the way `facing` looks to the direction of `point` seen from `origin`: positive = to the right, negative = to the left,
-- +-180 = straight behind
function M.relative_angle(point, origin, facing)
    local px, py, pz, ox, _, oz = pair(point, origin)
    if not px then return nil, py end
    local fx, fy, fz = facing_vec(facing)
    if not fx then return nil, fy end
    return wrap180(yaw_xz(px - ox, pz - oz) - yaw_xz(fx, fz))
end

-- the vertical angle between the way `facing` looks and the direction of the point (positive = the point is higher); 0 for a yaw number (flat facing)
local function pitch_delta(px, py, pz, ox, oy, oz, fdy, flat)
    local dx, dy, dz = px - ox, py - oy, pz - oz
    local l = sqrt(dx * dx + dy * dy + dz * dz)
    local pp = l < 1e-9 and 0 or asin(dy / l) * DEG
    if flat then return pp end
    return pp - asin(fdy > 1 and 1 or fdy < -1 and -1 or fdy) * DEG
end

-- in_cone(point, origin, facing, fov [, range [, vfov]]): is the point inside the field of view `fov` (degrees, the whole opening: 180 = everything in front)
-- of an observer at `origin` looking along `facing` (yaw / entity / vector)? The horizontal angle may be at most fov / 2 to either side and the vertical angle
-- (only when the facing is an entity or a vector) at most vfov / 2 (default fov / 2: the engine's own NPC test, Entity::HasInFOV, uses 90 degrees for both).
-- `range`: the distance limit (3D). Returns true / false, and a reason (false, "range" | "fov") when outside.
function M.in_cone(point, origin, facing, fov, range, vfov)
    if not (finite(fov) and fov > 0 and fov <= 360) then return nil, "fov must be a number in (0, 360]" end
    if range ~= nil and not (finite(range) and range >= 0) then return nil, "range must be a number >= 0" end
    if vfov ~= nil and not (finite(vfov) and vfov > 0 and vfov <= 360) then return nil, "vfov must be a number in (0, 360]" end
    local px, py, pz, ox, oy, oz = pair(point, origin)
    if not px then return nil, py end
    local fx, fy, fz, flat = facing_vec(facing)
    if not fx then return nil, fy end
    local dx, dy, dz = px - ox, py - oy, pz - oz
    if range and (dx * dx + dy * dy + dz * dz) > range * range then return false, "range" end
    if abs(dx) < 1e-9 and abs(dz) < 1e-9 then
        -- exactly above or below: no horizontal direction, the vertical test decides
        if flat then return true end
    else
        local rel = wrap180(yaw_xz(dx, dz) - yaw_xz(fx, fz))
        if abs(rel) > fov / 2 + 1e-9 then return false, "fov" end
    end
    if not flat then
        local pd = pitch_delta(px, py, pz, ox, oy, oz, fy, false)
        if abs(pd) > (vfov or fov) / 2 + 1e-9 then return false, "fov" end
    end
    return true
end

-- is_behind(point, origin, facing [, margin]): the point lies behind the observer: more than 90 + margin degrees (default 0) away from the way he looks
function M.is_behind(point, origin, facing, margin)
    if margin ~= nil and not finite(margin) then return nil, "margin must be a number" end
    local rel, why = M.relative_angle(point, origin, facing)
    if not rel then return nil, why end
    return abs(rel) > 90 + (margin or 0)
end
)LUA";

inline constexpr const char* kLineOfSightChunk = R"LUA(
local risen = ...
local world = risen.world
local M = risen.math
local type, pcall, rawget, abs = type, pcall, rawget, math.abs

local MAX_VIEWERS = 200

local function is_entity(v)
    if type(v) ~= "userdata" then return false end
    local ok, f = pcall(function() return v.bounds end)
    return ok and type(f) == "function"
end

-- the point of an entity a ray starts or ends at: `height` above its position when given, else where the engine's own test aims (eCGeometryEntity::IsFreeLOS:
-- the centre of the bounding box, raised to three quarters of the height for anything below 350 units, i.e. about eye level); an entity without a box: its position
local function body_point(e, height)
    local ok, x, y, z = pcall(function() return e:position() end)
    if not ok or type(x) ~= "number" then return nil, "the entity is gone" end
    if height ~= nil then return x, y + height, z end
    local ok2, x0, y0, z0, x1, y1, z1 = pcall(function() return e:bounds() end)
    if ok2 and x0 then
        local h = y1 - y0
        return (x0 + x1) / 2, h < 350 and y0 + h * 0.75 or (y0 + y1) / 2, (z0 + z1) / 2
    end
    return x, y, z
end

local NUMBER_KEYS = { "from_height", "to_height", "eye_height", "target_height", "fov", "vfov", "range" }
local OTHER_KEYS = { "camera", "ignore", "ignore_endpoints", "include_dead", "bodies" }

-- a clean copy of the options (read raw: no metamethod of a caller's table runs) with every number checked | nil, reason
local function check_opts(opts)
    local o = {}
    if opts == nil then return o end
    if type(opts) ~= "table" then return nil, "opts must be a table" end
    for _, k in ipairs(NUMBER_KEYS) do
        local v = rawget(opts, k)
        if v ~= nil then
            if type(v) ~= "number" or v ~= v or v == math.huge or v == -math.huge then return nil, ("opts.%s must be a finite number"):format(k) end
            o[k] = v
        end
    end
    for _, k in ipairs(OTHER_KEYS) do o[k] = rawget(opts, k) end
    return o
end

-- endpoint(value, height) -> x, y, z, entity | nil, reason
local function endpoint(v, height)
    if is_entity(v) then
        local x, y, z = body_point(v, height)
        if not x then return nil, y end
        return x, y, z, v
    end
    local x, y, z = M.xyz(v)
    if not x then return nil, y end
    return x, y, z, nil
end

local function ignore_list(opts, a, b)
    local list = {}
    local function add(e) if e ~= nil then list[#list + 1] = e end end
    if opts.ignore_endpoints ~= false then add(a) add(b) end
    local extra = opts.ignore
    if extra ~= nil then
        if is_entity(extra) then
            add(extra)
        elseif type(extra) == "table" then
            for i = 1, 64 do
                local e = rawget(extra, i)
                if e == nil then break end
                add(e)
            end
        else
            return nil, "opts.ignore must be an entity or a list of entities"
        end
    end
    return list
end

-- raycast(from, to [, opts]) -> { blocked, distance, length, x, y, z, entity, from, to } | nil, reason
-- from / to: a point ({x, y, z}) or an entity. opts: from_height / to_height (above an entity's position; default: eye level / the engine's aim point), ignore (entity
-- or list), ignore_endpoints (default true: the two entities themselves never block), bodies (true: characters and other dynamic bodies block too; the engine's own test
-- only sees level geometry and objects).
function world.raycast(from, to, opts)
    local o, why = check_opts(opts)
    if not o then return nil, why end
    local fx, fy, fz, fe = endpoint(from, o.from_height or o.eye_height)
    if not fx then return nil, "from: " .. tostring(fy) end
    local tx, ty, tz, te = endpoint(to, o.to_height or o.target_height)
    if not tx then return nil, "to: " .. tostring(ty) end
    local ignore, why2 = ignore_list(o, fe, te)
    if not ignore then return nil, why2 end
    local blocked, dist, hx, hy, hz, ent = world.trace(fx, fy, fz, tx, ty, tz, ignore, o.bodies == true)
    if blocked == nil then return nil, dist end
    local dx, dy, dz = tx - fx, ty - fy, tz - fz
    return {
        blocked = blocked, distance = dist, length = math.sqrt(dx * dx + dy * dy + dz * dz), x = hx, y = hy, z = hz, entity = ent,
        from = M.vec(fx, fy, fz), to = M.vec(tx, ty, tz),
    }
end

-- line_of_sight(from, to [, opts]) -> true (nothing in between) | false, hit | nil, reason: the engine's physics-scene ray test between two points / entities
function world.line_of_sight(from, to, opts)
    local r, why = world.raycast(from, to, opts)
    if not r then return nil, why end
    return not r.blocked, r
end

-- ---- the field of view ----------------------------------------------------------------------------------------------------------------------

local DEFAULT_FOV = 180      -- the engine's NPC perception (Entity::HasInFOV): at most 90 degrees to either side, horizontally and vertically

-- can_see(viewer, target [, opts]) -> true | false, "fov" | "range" | "blocked", hit | nil, reason
-- viewer: an entity, or a table { x, y, z, yaw [, pitch] } (an observer that is not in the world). target: an entity or a point.
-- opts: fov (degrees, whole opening, default 180), vfov (default fov), range (3D), eye_height / target_height (above the positions; default: eye level, see raycast),
-- camera (true: look from the render camera along its view direction instead of the entity's; fov / vfov then default to the camera's opening), ignore,
-- ignore_endpoints, bodies.
function world.can_see(viewer, target, opts)
    local o, why = check_opts(opts)
    if not o then return nil, why end
    local fov, vfov, range = o.fov or DEFAULT_FOV, o.vfov, o.range
    if fov <= 0 or fov > 360 then return nil, "opts.fov must be in (0, 360]" end
    if vfov ~= nil and (vfov <= 0 or vfov > 360) then return nil, "opts.vfov must be in (0, 360]" end
    if range ~= nil and range < 0 then return nil, "opts.range must be >= 0" end
    local vx, vy, vz, ve, facing
    if o.camera then
        local cam, cwhy = world.camera()
        if not cam then return nil, cwhy end
        vx, vy, vz, facing, ve = cam.x, cam.y, cam.z, M.vec(cam.dx, cam.dy, cam.dz), (is_entity(viewer) and viewer or nil)
        -- the camera's opening: the engine stores one angle (60 degrees); taken as the VERTICAL one (inferred, not proven), the horizontal opening follows from the aspect ratio
        if o.vfov == nil then vfov = cam.fov end
        if o.fov == nil and cam.aspect and cam.aspect > 0 then
            fov = math.min(360, 2 * math.deg(math.atan(math.tan(math.rad(cam.fov) / 2) / cam.aspect)))
        end
    elseif is_entity(viewer) then
        local x, y, z = body_point(viewer, o.eye_height or o.from_height)
        if not x then return nil, y end
        vx, vy, vz, ve = x, y, z, viewer
        local f, fwhy = M.facing(viewer)
        if not f then return nil, fwhy end
        facing = f
    elseif type(viewer) == "table" then
        local x, y, z = M.xyz(viewer)
        local yaw, pitch = rawget(viewer, "yaw"), rawget(viewer, "pitch")
        if not x then return nil, y end
        if type(yaw) ~= "number" or (pitch ~= nil and type(pitch) ~= "number") then return nil, "a viewer table needs a numeric yaw (and an optional pitch)" end
        local f, fwhy = M.from_yaw(yaw, pitch)
        if not f then return nil, fwhy end
        vx, vy, vz, facing = x, y + (o.eye_height or o.from_height or 0), z, f
    else
        return nil, "the viewer must be an entity or a table { x, y, z, yaw }"
    end
    local tx, ty, tz, te = endpoint(target, o.target_height or o.to_height)
    if not tx then return nil, "target: " .. tostring(ty) end
    local inside, outside = M.in_cone({ tx, ty, tz }, { vx, vy, vz }, facing, fov, range, vfov)
    if inside == nil then return nil, outside end
    if not inside then return false, outside end
    local ignore, why2 = ignore_list(o, ve, te)
    if not ignore then return nil, why2 end
    local blocked, dist, hx, hy, hz, ent = world.trace(vx, vy, vz, tx, ty, tz, ignore, o.bodies == true)
    if blocked == nil then return nil, dist end
    if blocked then return false, "blocked", { blocked = true, distance = dist, x = hx, y = hy, z = hz, entity = ent } end
    return true
end

-- is_seen_by(point, viewers [, opts]) -> true, viewer | false | nil, reason
-- viewers: an entity, a list of entities, or nil for the player. The first viewer who can_see the point (same options) is returned. Dead viewers are skipped
-- (opts.include_dead = true keeps them), a viewer that is the point itself is skipped.
function world.is_seen_by(point, viewers, opts)
    local o, why = check_opts(opts)
    if not o then return nil, why end
    local list
    if viewers == nil then
        local p = risen.entity.player()
        if not p then return nil, "there is no player" end
        list = { p }
    elseif is_entity(viewers) then
        list = { viewers }
    elseif type(viewers) == "table" then
        list = {}
        for i = 1, MAX_VIEWERS + 1 do
            local v = rawget(viewers, i)
            if v == nil then break end
            if i > MAX_VIEWERS then return nil, "at most " .. MAX_VIEWERS .. " viewers" end
            list[i] = v
        end
    else
        return nil, "viewers must be an entity, a list of entities or nil (the player)"
    end
    -- the point is resolved once (an entity stays the same entity for every viewer)
    local px, py, pz = endpoint(point, o.target_height or o.to_height)
    if not px then return nil, "point: " .. tostring(py) end
    for _, v in ipairs(list) do
        if v ~= point and is_entity(v) then
            local dead = false
            if not o.include_dead then local ok, d = pcall(function() return v:is_dead() end) dead = ok and d == true end
            if not dead then
                local seen, reason = world.can_see(v, point, o)
                if seen then return true, v end
                -- a stale viewer is skipped; anything else (a wrong option) is the caller's mistake
                if seen == nil and reason ~= "the entity is gone" and reason ~= "the entity is gone or has no direction" then return nil, reason end
            end
        end
    end
    return false
end
)LUA";

inline constexpr const char* kRandomChunk = R"LUA(
local api, modid = ...
local bit = bit
local tobit, bxor, bor, lshift, rshift = bit.tobit, bit.bxor, bit.bor, bit.lshift, bit.rshift
local floor, sqrt, sin, cos, huge, pi = math.floor, math.sqrt, math.sin, math.cos, math.huge, math.pi
local type, rawget, setmetatable, getmetatable = type, rawget, setmetatable, getmetatable

local Stream = {}
Stream.__index = Stream

local TWO32, TWO53 = 4294967296, 9007199254740992
local MAX_LIST = 100000

local function finite(n) return type(n) == "number" and n == n and n ~= huge and n ~= -huge end

-- sfc32 (Chris Doty-Humphrey's "small fast chaotic" generator, 128 bits of state, 32-bit output): the whole state is four integers, so a stream can be
-- saved, restored and compared; it uses only the 32-bit bit operations, so the sequence of a seed is the same on every machine and in every run.
local function step(s)
    local a, b, c, d = s[1], s[2], s[3], s[4]
    local t = tobit(a + b)
    a = bxor(b, rshift(b, 9))
    b = tobit(c + lshift(c, 3))
    c = bor(lshift(c, 21), rshift(c, 11))
    d = tobit(d + 1)
    t = tobit(t + d)
    c = tobit(c + t)
    s[1], s[2], s[3], s[4] = a, b, c, d
    if t < 0 then return t + TWO32 end
    return t
end

-- a string or number -> a 32-bit hash pair
local function hash_seed(seed)
    if type(seed) == "string" then
        local h = 2166136261
        for i = 1, #seed > 4096 and 4096 or #seed do
            h = tobit(bxor(h, seed:byte(i)) * 16777619 % TWO32)
        end
        return h, tobit(h * 2654435761 % TWO32)
    end
    local n = seed % TWO53
    local lo = n % TWO32
    local hi = floor(n / TWO32)
    return tobit(lo), tobit(hi)
end

local function seeded_state(seed)
    local lo, hi = hash_seed(seed)
    local s = { tobit(0x9E3779B9), tobit(0x243F6A88), tobit(0xB7E15162), lo }
    s[2] = bxor(s[2], hi)
    for _ = 1, 15 do step(s) end      -- the first outputs of a plain counter seed are correlated: discard some
    return s
end

local entropy_counter = 0
local function entropy_seed()
    entropy_counter = entropy_counter + 1
    local s = tostring({}) .. tostring(os.time()) .. tostring(os.clock()) .. tostring(modid) .. tostring(entropy_counter) .. tostring(os.date and os.date("%c") or "")
    return s
end

local function valid_state(t)
    if type(t) ~= "table" then return false end
    for i = 1, 4 do
        local v = rawget(t, i)
        if not finite(v) or v ~= floor(v) or v < -2147483648 or v >= TWO32 then return false end
    end
    return true
end

local function new_stream(seed)
    local s
    if seed == nil then
        s = seeded_state(entropy_seed())
    elseif type(seed) == "table" then
        if not valid_state(seed) then return nil, "a state is a table of four 32-bit integers (what rng:state() returns)" end
        s = { tobit(seed[1]), tobit(seed[2]), tobit(seed[3]), tobit(seed[4]) }
    elseif type(seed) == "number" then
        if not finite(seed) then return nil, "the seed must be a finite number" end
        s = seeded_state(seed)
    elseif type(seed) == "string" then
        s = seeded_state(seed)
    else
        return nil, "the seed must be a number, a string, a state table or nil"
    end
    return setmetatable({ s = s }, Stream)
end

local function is_stream(self) return type(self) == "table" and getmetatable(self) == Stream and type(rawget(self, "s")) == "table" end
local NOT_STREAM = "call it on a stream: rng:method(...)"

-- a number in [0, 1) with 53 random bits
local function unit(self)
    local a = step(self.s) % TWO32
    local b = step(self.s) % TWO32
    return ((a * 2097152) + floor(b / 2048)) / TWO53
end

function Stream:float(lo, hi)
    if not is_stream(self) then return nil, NOT_STREAM end
    if lo == nil and hi == nil then return unit(self) end
    if hi == nil then lo, hi = 0, lo end
    if not (finite(lo) and finite(hi)) then return nil, "float(lo, hi) needs finite numbers" end
    if hi < lo then lo, hi = hi, lo end
    return lo + (hi - lo) * unit(self)
end

-- int(n) -> 1..n;  int(lo, hi) -> lo..hi (both included)
function Stream:int(lo, hi)
    if not is_stream(self) then return nil, NOT_STREAM end
    if hi == nil then lo, hi = 1, lo end
    if not (finite(lo) and finite(hi)) then return nil, "int(lo, hi) needs finite numbers" end
    lo, hi = floor(lo), floor(hi)
    if hi < lo then return nil, "int(lo, hi): hi must not be below lo" end
    local span = hi - lo + 1
    if span > TWO53 / 2 then return nil, "int: the range is too large" end
    return lo + floor(unit(self) * span)
end

function Stream:chance(p)
    if not is_stream(self) then return nil, NOT_STREAM end
    if not finite(p) then return nil, "chance(p) needs a probability between 0 and 1" end
    return unit(self) < p
end

local function list_length(list)
    local n = #list
    if n > MAX_LIST then return nil end
    return n
end

function Stream:choice(list)
    if not is_stream(self) then return nil, NOT_STREAM end
    if type(list) ~= "table" then return nil, "choice(list) needs a list" end
    local n = list_length(list)
    if not n then return nil, "the list is too long" end
    if n == 0 then return nil, "the list is empty" end
    local i = 1 + floor(unit(self) * n)
    return rawget(list, i), i
end

-- shuffle(list) -> list: shuffles IN PLACE (Fisher-Yates) and returns it
function Stream:shuffle(list)
    if not is_stream(self) then return nil, NOT_STREAM end
    if type(list) ~= "table" then return nil, "shuffle(list) needs a list" end
    local n = list_length(list)
    if not n then return nil, "the list is too long" end
    for i = n, 2, -1 do
        local j = 1 + floor(unit(self) * i)
        list[i], list[j] = list[j], list[i]
    end
    return list
end

-- shuffled(list) -> a shuffled copy
function Stream:shuffled(list)
    if not is_stream(self) then return nil, NOT_STREAM end
    if type(list) ~= "table" then return nil, "shuffled(list) needs a list" end
    local n = list_length(list)
    if not n then return nil, "the list is too long" end
    local copy = {}
    for i = 1, n do copy[i] = rawget(list, i) end
    return self:shuffle(copy)
end

-- sample(list, k) -> k different elements, in random order (fewer if the list is shorter)
function Stream:sample(list, k)
    if not is_stream(self) then return nil, NOT_STREAM end
    if not finite(k) or k < 0 then return nil, "sample(list, k) needs k >= 0" end
    local copy, why = self:shuffled(list)
    if not copy then return nil, why end
    k = floor(k)
    for i = #copy, k + 1, -1 do copy[i] = nil end
    return copy
end

-- weighted(items, weights) -> item, index:  weights is a list of numbers (>= 0) of the same length, or a function(item, index) -> number
function Stream:weighted(items, weights)
    if not is_stream(self) then return nil, NOT_STREAM end
    if type(items) ~= "table" then return nil, "weighted(items, weights) needs a list of items" end
    local n = list_length(items)
    if not n then return nil, "the list is too long" end
    if n == 0 then return nil, "the list is empty" end
    local w, total = {}, 0
    for i = 1, n do
        local v
        if type(weights) == "function" then
            local ok, r = pcall(weights, rawget(items, i), i)
            if not ok then return nil, "the weight function raised an error: " .. tostring(r) end
            v = r
        elseif type(weights) == "table" then
            v = rawget(weights, i)
        else
            return nil, "weights must be a list of numbers or a function"
        end
        if not finite(v) or v < 0 then return nil, ("the weight of item %d is not a number >= 0"):format(i) end
        w[i] = v
        total = total + v
    end
    if total <= 0 then return nil, "all weights are zero" end
    local r = unit(self) * total
    local acc = 0
    for i = 1, n do
        acc = acc + w[i]
        if r < acc then return rawget(items, i), i end
    end
    for i = n, 1, -1 do if w[i] > 0 then return rawget(items, i), i end end
end

-- point_in_ring(center, rmin, rmax [, opts]) -> { x, y, z, ... }: a point on the ground around `center`, evenly spread over the ring between the two radii.
-- opts.yaw + opts.arc (degrees) restrict it to a sector: the directions yaw - arc / 2 .. yaw + arc / 2; opts.dy is added to the height.
function Stream:point_in_ring(center, rmin, rmax, opts)
    if not is_stream(self) then return nil, NOT_STREAM end
    local M = api.math
    local x, y, z = M.xyz(center)
    if not x then return nil, y end
    if not (finite(rmin) and finite(rmax) and rmin >= 0 and rmax >= rmin) then return nil, "point_in_ring needs 0 <= rmin <= rmax" end
    local yaw, arc, dy = 0, 360, 0
    if opts ~= nil then
        if type(opts) ~= "table" then return nil, "opts must be a table" end
        if opts.yaw ~= nil or opts.arc ~= nil then
            yaw, arc = opts.yaw, opts.arc
            if not (finite(yaw) and finite(arc) and arc > 0 and arc <= 360) then return nil, "opts.yaw and opts.arc must be numbers (0 < arc <= 360)" end
        end
        if opts.dy ~= nil then
            if not finite(opts.dy) then return nil, "opts.dy must be a number" end
            dy = opts.dy
        end
    end
    local r = sqrt(rmin * rmin + unit(self) * (rmax * rmax - rmin * rmin))
    local a = (yaw + (unit(self) - 0.5) * arc) * pi / 180
    return M.vec(x + sin(a) * r, y + dy, z + cos(a) * r)
end

function Stream:state()
    if not is_stream(self) then return nil, NOT_STREAM end
    local s = self.s
    return { s[1], s[2], s[3], s[4] }
end

function Stream:set_state(t)
    if not is_stream(self) then return nil, NOT_STREAM end
    if not valid_state(t) then return nil, "a state is a table of four 32-bit integers (what rng:state() returns)" end
    self.s = { tobit(t[1]), tobit(t[2]), tobit(t[3]), tobit(t[4]) }
    return true
end

function Stream:seed(seed)
    if not is_stream(self) then return nil, NOT_STREAM end
    local n, why = new_stream(seed == nil and entropy_seed() or seed)
    if not n then return nil, why end
    self.s = n.s
    return true
end

-- persist(key): the state of this stream is written to the mod's risen.save before every savegame (key "random:<key>") and read back when a savegame is
-- loaded, so a loaded game continues the sequence the saved game would have drawn (without it a load just goes on with the sequence of the running process)
local persisted = {}
local hooked = false
local function restore_all()
    for key, st in pairs(persisted) do
        local saved = api.save.get("random:" .. key)
        if valid_state(saved) then st.s = { tobit(saved[1]), tobit(saved[2]), tobit(saved[3]), tobit(saved[4]) } end
    end
end
function Stream:persist(key)
    if not is_stream(self) then return nil, NOT_STREAM end
    if type(key) ~= "string" or key == "" or #key > 64 then return nil, "persist(key) needs a non-empty name (at most 64 characters)" end
    for k, st in pairs(persisted) do if st == self then persisted[k] = nil end end
    persisted[key] = self
    if not hooked then
        hooked = true
        api.on("save.before", function()
            for k, st in pairs(persisted) do api.save.set("random:" .. k, { st.s[1], st.s[2], st.s[3], st.s[4] }) end
        end)
        api.on("load.before", restore_all)
    end
    local saved = api.save.get("random:" .. key)
    if valid_state(saved) then self.s = { tobit(saved[1]), tobit(saved[2]), tobit(saved[3]), tobit(saved[4]) } end
    return true
end

-- ---- risen.random: the mod's own stream plus risen.random.new for more ----------------------------------------------------------------------------

local default = new_stream(nil)
local R = {}
api.random = R

function R.new(seed) return new_stream(seed) end

for _, name in ipairs { "float", "int", "chance", "choice", "shuffle", "shuffled", "sample", "weighted", "point_in_ring", "state", "set_state", "seed", "persist" } do
    local method = Stream[name]
    R[name] = function(...) return method(default, ...) end
end
)LUA";

}  // namespace spass::game
