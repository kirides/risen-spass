#pragma once

// The Lua part of the NPC routine API (M10, docs/RE.md section 35, docs/LUA_API.md "NPC routines"), on top of the natives of lua_routine.cpp
// (risen.npc.routine_points / set_routine_points / select_routine / apply_routine, risen.world.scan / spawn_point / nav_point / time / set_time,
// Entity:children / attach / direction). Kept in a header so the unit tests can load the chunks against mocks.
//
// kRoutineChunk (shared, runs once): point kinds, risen.world.points / add_point / remove_point / point_template.
// kRoutineModChunk (per mod): risen.npc.set_routine / get_routine / clear_routine and risen.world.add_patrol / remove_patrol. A routine's points are ordinary
// entities (freepoints, patrol anchors): the engine saves the NPC's references to them, and the points themselves are saved once they are marked save-relevant,
// but the properties of a patrol anchor (name, PatrolIndex, MaxUsers) are not part of a savegame -- the mod's risen.save record restores them after a load.
namespace spass::game {

inline constexpr const char* kRoutineChunk = R"LUA(
local risen = ...
local world = risen.world

-- point kinds -> the freepoint template of the game (data/common/templates/Navigation/Freepoints). Every other FP_* template is accepted by name.
local KINDS = {
    guard = "FP_Guard", stand = "FP_Stand", pace = "FP_Pace", roam = "FP_Roam", sit = "FP_Sit_Ground", sleep = "FP_Sleep_Ground",
    lookat = "FP_LookAt", smalltalk = "FP_Smalltalk", field = "FP_Field", dance = "FP_Dance", spectator = "FP_Spectator", trader = "FP_Trader",
    boss = "FP_Boss", practice_sword = "FP_PracticeSword", practice_staff = "FP_PracticeStaff", practice_magic = "FP_PracticeMagic",
    clean_floor = "FP_Clean_Floor", dig = "FP_Dig_Ground", pick = "FP_Pick_Ground", bard = "FP_Bard", flute = "FP_Flute", plunder = "FP_Plunder",
}
world.point_kinds = KINDS

local function known_kinds()
    local names = {}
    for k in pairs(KINDS) do names[#names + 1] = k end
    table.sort(names)
    return table.concat(names, ", ")
end

-- risen.world.point_template(kind) -> template name | nil, reason: "guard" -> "FP_Guard"; an FP_* template name is returned as it is
function world.point_template(kind)
    if type(kind) ~= "string" or kind == "" then return nil, "a point kind must be a string" end
    local t = KINDS[kind:lower()]
    if t then return t end
    if kind:match("^FP_[%w_]+$") then return kind end
    return nil, ("unknown point kind '%s' (known: %s; or the name of a freepoint template FP_*)"):format(kind, known_kinds())
end

local NAV_GROUPS = { "Navigation/Freepoints", "Navigation/Freepoints/Story_FPs", "Navigation/Anchors", "Navigation/AI_Helpers", "Navigation/Startpoints" }
local nav_templates

local function navigation_templates()
    if not nav_templates then
        nav_templates = {}
        for _, group in ipairs(NAV_GROUPS) do
            for _, name in ipairs(risen.template.list(group) or {}) do nav_templates[#nav_templates + 1] = name end
        end
    end
    return nav_templates
end

-- risen.world.points([opts]) -> { { entity, name, template, x, y, z, distance, kind = "freepoint"|"anchor"|"helper", use, task, anchor, index, max_users }, ... } | nil, reason
-- Every freepoint, anchor and AI helper of the world (or only those of `kind` = "guard" ... / `template` = "Anchor_Patrol" or a list of templates), optionally near an
-- entity or {x, y, z} within `radius`, with the unique `name` (or `name_prefix`); nearest first when `near` is given; at most `limit` (500).
function world.points(opts)
    if opts ~= nil and type(opts) ~= "table" then return nil, "opts must be a table" end
    opts = opts or {}
    local templates
    if opts.template ~= nil then
        templates = type(opts.template) == "table" and opts.template or { opts.template }
        for _, t in ipairs(templates) do
            if type(t) ~= "string" then return nil, "template must be a template name or a list of them" end
        end
    elseif opts.kind ~= nil then
        local t, why = world.point_template(opts.kind)
        if not t then return nil, why end
        templates = { t }
    else
        templates = navigation_templates()
    end
    if opts.near ~= nil and type(opts.near) ~= "table" and type(opts.near) ~= "userdata" then return nil, "near must be an entity or {x, y, z}" end
    return world.scan(templates, opts)
end

-- risen.world.add_point(kind, x, y, z [, opts]) -> Entity | nil, reason
-- Makes a freepoint of the given kind (see world.point_kinds) in the world. NPCs whose routine points to it go there and do what the kind says (guard: stand
-- guard, spectator: arms crossed, lookat: think, sleep: sleep on the ground, ...). The position is moved to the nearest walkable point within `snap_radius` (300)
-- unless `snap = false`: a point outside the navigation mesh is never reached. `yaw` (degrees, 0 = looking along +z) is the way whoever stands there faces.
-- The point is saved with the savegame. opts: yaw, snap, snap_radius, name (a unique entity name; it does not survive a load).
function world.add_point(kind, x, y, z, opts)
    if opts ~= nil and type(opts) ~= "table" then return nil, "opts must be a table" end
    opts = opts or {}
    if type(x) ~= "number" or type(y) ~= "number" or type(z) ~= "number" then return nil, "the position must be three numbers" end
    local tpl, why = world.point_template(kind)
    if not tpl then return nil, why end
    if opts.yaw ~= nil and type(opts.yaw) ~= "number" then return nil, "yaw must be a number (degrees)" end
    if opts.snap ~= false then
        local radius = opts.snap_radius or 300
        local r, reason = world.nav_point(x, y, z, radius)
        if not r then return nil, ("no walkable point within %d units of (%.0f, %.0f, %.0f): %s"):format(radius, x, y, z, tostring(reason)) end
        x, y, z = r[1], r[2], r[3]
    end
    local e, err = world.spawn_point(tpl, x, y, z, opts.yaw)
    if not e then return nil, err end
    if opts.name then e:set_name(opts.name) end
    return e
end

-- risen.world.remove_point(entity) -> bool: takes a point out of the world
function world.remove_point(point)
    if type(point) ~= "userdata" or type(point.kill) ~= "function" then return false end
    return point:kill() and true or false
end
)LUA";

inline constexpr const char* kRoutineModChunk = R"LUA(
local api, modid = ...
local shared_npc, shared_world = api.npc, api.world
local npc = setmetatable({}, { __index = shared_npc })
local world = setmetatable({}, { __index = shared_world })
api.npc, api.world = npc, world
local log = api.log

local STORE = "npc_routines"     -- the mod's key in risen.save: { npcs = { [npc id] = record }, patrols = { [group] = record } }
local SETTLE = 30                -- frames after world.ready before the patrol anchors are named again

local function say(level, fmt, ...) log[level](("routine: " .. fmt):format(...)) end
local function tr(...) local t = api.trace if t and t.enabled("script") then t("script", ...) end end

-- Fetched on every use: a load swaps the store.
local function records(create)
    local r = api.save.get(STORE)
    if type(r) ~= "table" then
        if not create then return nil end
        r = {}
        api.save.set(STORE, r)
    end
    if type(r.npcs) ~= "table" then r.npcs = {} end
    if type(r.patrols) ~= "table" then r.patrols = {} end
    return r
end

-- the unique entity name the anchors of a group carry: the engine joins anchors by name, so two mods' groups must never share one
local function ename(group) return ("%s_Patrol_%s"):format(modid, group) end

local function is_entity(v) return type(v) == "userdata" and type(v.valid) == "function" end
local function is_xyz(t) return type(t) == "table" and type(t[1]) == "number" and type(t[2]) == "number" and type(t[3]) == "number" end
local function is_rel(t) return type(t) == "table" and type(t.dx) == "number" and type(t.dy) == "number" and type(t.dz) == "number" end

local function alive(id) local e = type(id) == "string" and api.entity.by_id(id) or nil return e end

-- A point an NPC was using is not killed at once: the game crashed when a freepoint died in the very frame the NPC's routine was continued (the AI still held it).
-- It is removed REAP_DELAY frames later, when the NPC has moved on.
local REAP_DELAY = 40
local reap, reap_handle = {}, nil
local function reap_tick()
    for i = #reap, 1, -1 do
        local r = reap[i]
        r.due = r.due - 1
        if r.due <= 0 then
            local e = alive(r.id)
            if e then e:kill() end
            table.remove(reap, i)
        end
    end
    if #reap == 0 and reap_handle then api.off(reap_handle) reap_handle = nil end
end
local function kill_ids(ids)
    for _, id in ipairs(ids or {}) do
        if alive(id) then reap[#reap + 1] = { id = id, due = REAP_DELAY } end
    end
    if #reap > 0 and not reap_handle then reap_handle = api.on("tick", reap_tick) end
end
api.on("session.stop", function() reap = {} if reap_handle then api.off(reap_handle) reap_handle = nil end end)

-- A canonical text of a spec made of plain data (nil when it holds entities or functions): equal specs have equal signatures.
local function signature(v)
    local t = type(v)
    if t == "number" then return ("%.2f"):format(v) end
    if t == "string" then return ("%q"):format(v) end
    if t == "boolean" then return tostring(v) end
    if t ~= "table" then return nil end
    local keys = {}
    for k in pairs(v) do keys[#keys + 1] = k end
    table.sort(keys, function(a, b)
        if type(a) ~= type(b) then return type(a) < type(b) end
        return a < b
    end)
    local parts = {}
    for _, k in ipairs(keys) do
        local ks, vs = signature(k), signature(v[k])
        if not ks or not vs then return nil end
        parts[#parts + 1] = ks .. "=" .. vs
    end
    return "{" .. table.concat(parts, ",") .. "}"
end

-- ---- checking a spec (no side effects) ------------------------------------------------------------------------------------------------------------

-- one point: an entity, the name of an entity, {x, y, z [, kind, yaw]} or {dx, dy, dz [, kind, yaw]} (relative to the origin)
local function check_point(p, what, plan)
    if is_entity(p) then return nil end
    if type(p) == "string" then
        if p == "" then return what .. ": the entity name is empty" end
        return nil
    end
    if type(p) ~= "table" then return what .. " must be an entity, an entity name, {x, y, z} or {dx = , dy = , dz = }" end
    if is_rel(p) then plan.relative = true
    elseif not is_xyz(p) then return what .. " must be an entity, an entity name, {x, y, z} or {dx = , dy = , dz = }" end
    if p.kind ~= nil then
        local _, why = world.point_template(p.kind)
        if why then return what .. ": " .. why end
    end
    if p.yaw ~= nil and type(p.yaw) ~= "number" then return what .. ": yaw must be a number (degrees)" end
    return nil
end

local SPEC_KEYS = { at = true, work = true, relax = true, sleep = true, guard = true, patrol = true, routine = true, apply = true, kind = true, snap = true }
local PATROL_KEYS = { name = true, kind = true, max_users = true, yaw = true }

local function check_patrol(def, plan)
    if type(def) == "string" then
        if def == "" then return "patrol: the group name is empty" end
        plan.patrol_name = def
        return nil
    end
    if type(def) ~= "table" then return "patrol must be a list of waypoints { {x, y, z}, ... } or the name of a patrol group" end
    for k in pairs(def) do
        if type(k) ~= "number" and not PATROL_KEYS[k] then return ("patrol: unknown option '%s'"):format(tostring(k)) end
    end
    if #def < 2 then return "patrol needs at least two waypoints (use `at` for a single post)" end
    for i, wp in ipairs(def) do
        if type(wp) ~= "table" then return ("patrol waypoint %d must be {x, y, z}"):format(i) end
        local why = check_point(wp, ("patrol waypoint %d"):format(i), plan)
        if why then return why end
    end
    if def.name ~= nil and (type(def.name) ~= "string" or not def.name:match("^[%w_%-]+$")) then return "patrol name must be a string of letters, digits, '_' and '-'" end
    if def.kind ~= nil then
        local _, why = world.point_template(def.kind)
        if why then return "patrol: " .. why end
    end
    if def.max_users ~= nil and (type(def.max_users) ~= "number" or def.max_users < 1) then return "patrol max_users must be a number >= 1" end
    plan.patrol_def = def
    return nil
end

-- -> plan | nil, reason
local function check_spec(e, spec)
    if not is_entity(e) then return nil, "the first argument must be an NPC entity" end
    if not e:valid() then return nil, "the NPC entity is gone" end
    if not e:is_npc() or e:is_player() then return nil, "the entity is not an NPC" end
    if e:is_dead() then return nil, "the NPC is dead" end
    if type(spec) ~= "table" then return nil, "the routine must be a table { at = ..., work = ..., relax = ..., sleep = ..., patrol = ... }" end
    for k in pairs(spec) do
        if not SPEC_KEYS[k] then return nil, ("unknown routine option '%s'"):format(tostring(k)) end
    end
    local plan = {}
    for _, key in ipairs { "at", "work", "relax", "sleep", "guard" } do
        if spec[key] ~= nil then
            local why = check_point(spec[key], key, plan)
            if why then return nil, why end
        end
    end
    if spec.at ~= nil and (spec.work ~= nil or spec.relax ~= nil or spec.sleep ~= nil) then
        return nil, "`at` stands for work, relax and sleep at once: give either `at` or the three"
    end
    if spec.patrol ~= nil then
        local why = check_patrol(spec.patrol, plan)
        if why then return nil, why end
        if spec.at ~= nil or spec.work ~= nil then return nil, "`patrol` gives the working point: it cannot be combined with `at` or `work` (add relax / sleep if they differ)" end
    end
    if spec.work == nil and spec.at == nil and spec.patrol == nil and spec.relax == nil and spec.sleep == nil and spec.guard == nil then
        return nil, "the routine has no points (give at, work, relax, sleep, patrol or guard)"
    end
    if spec.routine ~= nil and (type(spec.routine) ~= "string" or spec.routine == "") then return nil, "routine must be the name of one of the NPC's routines" end
    if spec.apply ~= nil and type(spec.apply) ~= "boolean" then return nil, "apply must be true or false" end
    if spec.snap ~= nil and type(spec.snap) ~= "boolean" then return nil, "snap must be true or false" end
    if spec.kind ~= nil then
        local _, why = world.point_template(spec.kind)
        if why then return nil, why end
    end
    return plan
end

-- ---- making the points ----------------------------------------------------------------------------------------------------------------------------

local function position_of(p, ctx)
    if is_rel(p) then
        local o = ctx.origin
        return o[1] + p.dx, o[2] + p.dy, o[3] + p.dz
    end
    return p[1], p[2], p[3]
end

-- one point spec -> entity | nil, reason. Positions make a new freepoint (remembered in ctx.created).
local function resolve_point(p, what, ctx)
    if is_entity(p) then
        if not p:valid() then return nil, what .. " is a stale entity handle" end
        return p
    end
    if type(p) == "string" then
        local e = api.entity.get(p)
        if not e then return nil, ("%s: there is no entity named '%s'"):format(what, p) end
        return e
    end
    local x, y, z = position_of(p, ctx)
    local e, why = world.add_point(p.kind or ctx.kind or "guard", x, y, z, { yaw = p.yaw, snap = ctx.snap })
    if not e then return nil, what .. ": " .. tostring(why) end
    ctx.created[#ctx.created + 1] = e
    return e
end

local function group_intact(g)
    if type(g) ~= "table" or type(g.anchors) ~= "table" or #g.anchors == 0 then return false end
    for _, a in ipairs(g.anchors) do
        if not alive(a.id) then return false end
    end
    for _, id in ipairs(g.points or {}) do
        if not alive(id) then return false end
    end
    return true
end

local function remove_group(name)
    local recs = records(false)
    local g = recs and recs.patrols[name]
    if not g then return end
    for _, a in ipairs(g.anchors or {}) do kill_ids({ a.id }) end
    kill_ids(g.points)
    recs.patrols[name] = nil
end

-- Builds a patrol: one anchor (type patrol, the same unique name, PatrolIndex 0..n-1) per waypoint, each with a freepoint of `kind` as its interact point. The engine's own
-- routine logic (SelectRoutineDestination / Entity::GetNextPatrolPoint) sends an NPC whose routine point is one of those freepoints to the next numbered anchor of the group
-- when it is done there, and from the last back to index 0. A freepoint somebody stands on (the player included) is not free: keep the waypoints off the places where
-- people stand. -> group { name, points = {Entity...}, anchors = {Entity...} } (index 0 first) | nil, reason
local function build_patrol(name, def, ctx)
    local kind = def.kind or ctx.kind or "guard"
    local max = def.max_users or 1
    local rec = { kind = kind, max = max, anchors = {}, points = {} }
    local made = {}
    local function undo()
        for _, e in ipairs(made) do e:kill() end
    end
    local pos = {}
    for i, wp in ipairs(def) do
        local x, y, z = position_of(wp, ctx)
        if ctx.snap ~= false then
            local r, why = world.nav_point(x, y, z, 300)
            if not r then return nil, ("patrol waypoint %d: no walkable point within 300 units of (%.0f, %.0f, %.0f): %s"):format(i, x, y, z, tostring(why)) end
            x, y, z = r[1], r[2], r[3]
        end
        pos[i] = { x, y, z, wp.yaw or def.yaw, wp.kind or kind }
    end
    local group = { name = name, points = {}, anchors = {} }
    for i = 1, #def do
        local p = pos[i]
        local anchor, why = world.spawn_point("Anchor_Patrol", p[1], p[2], p[3], p[4])
        if not anchor then undo() return nil, ("patrol waypoint %d: %s"):format(i, tostring(why)) end
        made[#made + 1] = anchor
        local point, why2 = world.add_point(p[5], p[1], p[2], p[3], { yaw = p[4], snap = false })
        if not point then undo() return nil, ("patrol waypoint %d: %s"):format(i, tostring(why2)) end
        made[#made + 1] = point
        anchor:set_name(ename(name))
        anchor:set_property("gCAnchor_PS", "PatrolIndex", tostring(i - 1))
        anchor:set_property("gCAnchor_PS", "MaxUsers", tostring(max))
        if not anchor:attach(point) then undo() return nil, ("patrol waypoint %d: the freepoint could not be attached to its anchor"):format(i) end
        group.anchors[i], group.points[i] = anchor, point
        rec.anchors[i] = { id = anchor:id(), index = i - 1 }
        rec.points[i] = point:id()
    end
    records(true).patrols[name] = rec
    tr("patrol '%s': %d waypoints of kind %s", name, #def, kind)
    return group
end

-- the group of a patrol spec: an existing one by name (joined), or a new one
local function patrol_group(plan, npc_id, ctx)
    local recs = records(true)
    local def = plan.patrol_def
    local name = plan.patrol_name or (def and def.name) or ("npc_" .. tostring(npc_id))
    local g = recs.patrols[name]
    if g and group_intact(g) then
        local group = { name = name, points = {}, anchors = {} }
        for i, a in ipairs(g.anchors) do group.anchors[i] = alive(a.id) group.points[i] = alive(g.points[i]) end
        return group, false
    end
    if not def then return nil, ("there is no patrol group named '%s' (create it with risen.world.add_patrol or give the waypoints)"):format(name) end
    if g then remove_group(name) end      -- a broken group (an old savegame lost its anchors) is made again
    local group, why = build_patrol(name, def, ctx)
    if not group then return nil, why end
    return group, true
end

-- -- the points of the NPC's record that exist right now
local function slots_intact(e, rec)
    local p = api.npc.routine_points(e)
    if not p then return false end
    for _, slot in ipairs { "work", "relax", "sleep" } do
        if rec.slots and rec.slots[slot] and not (p[slot] and p[slot]:valid()) then return false end
    end
    if rec.patrol then
        local recs = records(false)
        if not (recs and group_intact(recs.patrols[rec.patrol])) then return false end
    end
    for _, id in ipairs(rec.created or {}) do
        if not alive(id) then return false end
    end
    return true
end

local function patrol_used_by_others(name, except_id)
    local recs = records(false)
    if not recs then return false end
    for id, r in pairs(recs.npcs) do
        if id ~= except_id and r.patrol == name then return true end
    end
    return false
end

-- Drops what a record created (freepoints, a patrol nobody else uses).
local function release(npc_id, rec)
    kill_ids(rec.created)
    if rec.patrol and not patrol_used_by_others(rec.patrol, npc_id) then remove_group(rec.patrol) end
end

-- ---- risen.npc.set_routine ------------------------------------------------------------------------------------------------------------------------

-- risen.npc.set_routine(npc, spec [, opts]) -> info | nil, reason
-- spec: at (all three times of day), or work / relax / sleep; patrol; guard; routine; apply; kind; snap. A point is an entity, an entity name, {x, y, z [, kind, yaw]} or
-- {dx, dy, dz [, kind, yaw]} (relative to opts.origin, by default the NPC's position). opts: origin = {x, y, z}.
-- Calling it again with an equal spec while the points still exist changes nothing (info.reused).
function npc.set_routine(e, spec, opts)
    if opts ~= nil and type(opts) ~= "table" then return nil, "opts must be a table" end
    opts = opts or {}
    local plan, why = check_spec(e, spec)
    if not plan then return nil, why end
    if opts.origin ~= nil and not is_xyz(opts.origin) then return nil, "origin must be {x, y, z}" end
    local origin = opts.origin
    if plan.relative and not origin then
        local x, y, z = e:position()
        if not x then return nil, "the NPC has no position" end
        origin = { x, y, z }
    end
    local npc_id = e:id()
    if not npc_id then return nil, "the NPC has no entity id" end
    local recs = records(false)
    local old = recs and recs.npcs[npc_id]

    local sig = signature(spec)
    if sig and plan.relative then sig = sig .. signature({ origin[1], origin[2], origin[3] }) end
    if sig and old and old.sig == sig and slots_intact(e, old) then
        local p = api.npc.routine_points(e)
        p.created, p.patrol, p.reused = {}, old.patrol, true
        return p
    end

    local ctx = { origin = origin, kind = spec.kind, snap = spec.snap, created = {} }
    local function abort(reason, group_new)
        for _, c in ipairs(ctx.created) do c:kill() end
        if group_new then remove_group(group_new) end
        return nil, reason
    end

    local group, group_new
    if plan.patrol_def or plan.patrol_name then
        local g, created_or_why = patrol_group(plan, npc_id, ctx)
        if not g then return abort(created_or_why) end
        group = g
        if created_or_why then group_new = g.name end
    end

    -- the engine falls back to the working point for a missing relax / sleep point, so unnamed slots are simply cleared
    local at = spec.at
    local slot_spec = { work = spec.work or at, relax = spec.relax or at, sleep = spec.sleep or at }
    local resolved, by_spec = {}, {}
    for _, slot in ipairs { "work", "relax", "sleep" } do
        local s = slot_spec[slot]
        if s ~= nil then
            local ent = by_spec[s]
            if not ent then
                local err
                ent, err = resolve_point(s, spec.at ~= nil and "at" or slot, ctx)
                if not ent then return abort(err, group_new) end
                by_spec[s] = ent
            end
            resolved[slot] = ent
        end
    end
    if group then    -- a patrol gives the working point; relax / sleep follow it unless named
        resolved.work = group.points[1]
        resolved.relax = resolved.relax or resolved.work
        resolved.sleep = resolved.sleep or resolved.work
    end
    local guard
    if spec.guard ~= nil then
        local g, err = resolve_point(spec.guard, "guard", ctx)
        if not g then return abort(err, group_new) end
        guard = g
    end

    if spec.routine then
        local ok, err = api.npc.select_routine(e, spec.routine)
        if not ok then return abort(err, group_new) end
    end
    local ok, err = api.npc.set_routine_points(e, { work = resolved.work or false, relax = resolved.relax or false, sleep = resolved.sleep or false, guard = guard or nil })
    if not ok then return abort(err or "the engine refused the routine points", group_new) end

    -- success: what the previous routine created is released (its patrol only when nobody else walks it), a moment after the NPC went on
    if old then
        local keep_group = group and old.patrol == group.name
        local drop = { created = old.created, patrol = (not keep_group) and old.patrol or nil }
        release(npc_id, drop)
    end
    local created_ids = {}
    for i, c in ipairs(ctx.created) do created_ids[i] = c:id() end
    recs = records(true)
    recs.npcs[npc_id] = {
        sig = sig, created = created_ids, patrol = group and group.name or nil,
        slots = { work = resolved.work ~= nil, relax = resolved.relax ~= nil, sleep = resolved.sleep ~= nil },
    }
    if spec.apply ~= false then api.npc.apply_routine(e) end
    tr("set_routine '%s': work %s relax %s sleep %s (%d created)", tostring(e:name()), tostring(resolved.work), tostring(resolved.relax), tostring(resolved.sleep), #created_ids)
    return { work = resolved.work, relax = resolved.relax, sleep = resolved.sleep, guard = guard, created = ctx.created, patrol = group and group.name or nil, reused = false }
end

-- risen.npc.get_routine(npc) -> { routine, index, count, work, relax, sleep, guard, created, patrol } | nil, reason: what the engine has for the NPC, plus how many points this mod made
function npc.get_routine(e)
    if not is_entity(e) then return nil, "the argument must be an NPC entity" end
    local p, why = api.npc.routine_points(e)
    if not p then return nil, why end
    local recs = records(false)
    local rec = recs and recs.npcs[e:id()]
    p.created = rec and #rec.created or 0
    p.patrol = rec and rec.patrol or nil
    return p
end

-- risen.npc.clear_routine(npc [, opts]) -> bool: empties the NPC's working / relaxing / sleeping points; opts.remove (default true) removes the points this mod made for it (and
-- its patrol, unless another NPC uses it), opts.apply (default true) lets the NPC look at its routine again
function npc.clear_routine(e, opts)
    if opts ~= nil and type(opts) ~= "table" then return nil, "opts must be a table" end
    opts = opts or {}
    if not is_entity(e) then return nil, "the first argument must be an NPC entity" end
    if not e:valid() then return nil, "the NPC entity is gone" end
    local ok, why = api.npc.set_routine_points(e, { work = false, relax = false, sleep = false, guard = false })
    if not ok then return nil, why end
    local recs = records(false)
    local id = e:id()
    local rec = recs and recs.npcs[id]
    if rec then
        recs.npcs[id] = nil
        if opts.remove ~= false then release(id, rec) end
    end
    if opts.apply ~= false then api.npc.apply_routine(e) end
    return true
end

-- ---- patrol groups --------------------------------------------------------------------------------------------------------------------------------

-- risen.world.add_patrol(name, waypoints [, opts]) -> group { name, points, anchors } | nil, reason
-- Makes a patrol group (see build_patrol); an NPC joins it with set_routine(npc, { patrol = name }). opts: kind (default "guard"), max_users (1), origin, snap.
function world.add_patrol(name, waypoints, opts)
    if type(name) ~= "string" or not name:match("^[%w_%-]+$") then return nil, "the patrol name must be a string of letters, digits, '_' and '-'" end
    if type(waypoints) ~= "table" then return nil, "the waypoints must be a list { {x, y, z}, ... }" end
    if opts ~= nil and type(opts) ~= "table" then return nil, "opts must be a table" end
    opts = opts or {}
    local def = {}
    for i, wp in ipairs(waypoints) do def[i] = wp end
    def.kind, def.max_users, def.yaw = opts.kind, opts.max_users, opts.yaw
    local plan = {}
    local why = check_patrol(def, plan)
    if why then return nil, why end
    if opts.origin ~= nil and not is_xyz(opts.origin) then return nil, "origin must be {x, y, z}" end
    if plan.relative and not opts.origin then return nil, "relative waypoints need opts.origin" end
    local recs = records(true)
    local g = recs.patrols[name]
    if g and group_intact(g) then
        local group = { name = name, points = {}, anchors = {} }
        for i, a in ipairs(g.anchors) do group.anchors[i] = alive(a.id) group.points[i] = alive(g.points[i]) end
        return group
    end
    if g then remove_group(name) end
    return build_patrol(name, def, { origin = opts.origin, kind = opts.kind, snap = opts.snap })
end

-- risen.world.remove_patrol(name) -> bool: removes the group's anchors and freepoints (NPCs that used it fall back to their other points)
function world.remove_patrol(name)
    local recs = records(false)
    if type(name) ~= "string" or not (recs and recs.patrols[name]) then return false end
    remove_group(name)
    return true
end

-- Gives the anchors of the mod's patrol groups their name and numbers again: a savegame keeps the anchors, not these properties.
local function restore_patrols()
    local recs = records(false)
    if not recs then return end
    local names = {}
    for name in pairs(recs.patrols) do names[#names + 1] = name end
    table.sort(names)
    for _, name in ipairs(names) do
        local g = recs.patrols[name]
        local missing = 0
        for _, a in ipairs(g.anchors or {}) do
            local e = alive(a.id)
            if e then
                if e:name() ~= ename(name) then e:set_name(ename(name)) end
                e:set_property("gCAnchor_PS", "PatrolIndex", tostring(a.index))
                e:set_property("gCAnchor_PS", "MaxUsers", tostring(g.max or 1))
            else
                missing = missing + 1
            end
        end
        if missing > 0 then say("warn", "patrol '%s': %d of %d waypoints are missing from this savegame; set the routine again to rebuild it", name, missing, #(g.anchors or {})) end
    end
end

local pending, tick_handle
local function cancel_pending()
    pending = nil
    if tick_handle then api.off(tick_handle) tick_handle = nil end
end
api.on("session.stop", cancel_pending)
api.on("world.ready", function()
    cancel_pending()
    pending = SETTLE
    tick_handle = api.on("tick", function()
        if not pending then return end
        pending = pending - 1
        if pending > 0 then return end
        cancel_pending()
        local ok, err = pcall(restore_patrols)
        if not ok then say("error", "restoring the patrols: %s", tostring(err)) end
    end)
end)
)LUA";

}  // namespace spass::game
