#pragma once

// The Lua part of persistent NPCs (docs/RE.md section 32, docs/LUA_API.md "Persistent NPCs"): `risen.npc.persistent{...}` declares an NPC once; this layer
// keeps it alive across new games, quick saves/loads and fresh processes.
//
// How: the engine itself writes every entity made by Entity::Spawn into the savegame (the system dynamic layer; position, hit points, inventory, guild, state
// and the unique entity ID come back after a load) but NOT its unique name. The per-mod record in `risen.save` therefore holds the entity's ID (and the dead /
// version state); after a load the entity is found by that ID and given its name again. Only an NPC the savegame does not know (a new game, or a savegame made
// before the declaration existed) is spawned. Pure Lua on top of risen.entity.by_id/spawn, Entity:id/name/set_name/kill/is_dead/template, risen.npc.spawn,
// risen.save and the events, so the unit tests run it against mocks. Arguments: the per-mod `risen` table and the mod id.
namespace spass::game {

inline const char* kPersistentNpcChunk = R"LUA(
local api, modid = ...
local shared = api.npc
local npc = setmetatable({}, { __index = shared })
api.npc = npc
local log = api.log

local STORE = "persistent_npcs"      -- the mod's key in risen.save: { [npc name] = record }
local SETTLE = 30                    -- frames after world.ready before the NPCs are reconciled (the player entity exists a moment earlier)

-- name -> mod id of every declaration of this process (names are global: an entity name is unique in the world)
shared._persistent_owner = shared._persistent_owner or {}
local owner = shared._persistent_owner

local declared, order, by_name = {}, {}, {}    -- name -> declaration; names in declaration order
local ready = false                  -- the world of this session was reconciled
local pending = nil                  -- frames left before the reconciliation, or nil
local tick_handle = nil
local installed = false
local reported = {}                  -- warnings already logged for this session

local SPAWN_KEYS = { "hp", "hp_max", "level", "mana", "guild", "group", "peaceful", "state", "items", "equip" }
local KNOWN = { name = true, template = true, position = true, offset = true, version = true, respawn = true, label = true, define = true,
                on_ready = true, on_death = true, enabled = true }
for _, k in ipairs(SPAWN_KEYS) do KNOWN[k] = true end

local function tr(...) local t = api.trace if t and t.enabled("spawn") then t("spawn", ...) end end
local function say(level, fmt, ...) log[level](("persistent npc: " .. fmt):format(...)) end
local function once(key, level, fmt, ...)
    if reported[key] then return end
    reported[key] = true
    say(level, fmt, ...)
end

-- The mod's records: { [name] = { id, template, version, home = {x,y,z}, last = {x,y,z}, dead, deaths, removed } }. Fetched on every use: a load swaps the store.
local function records(create)
    local r = api.save.get(STORE)
    if type(r) ~= "table" then
        if not create then return nil end
        r = {}
        api.save.set(STORE, r)
    end
    return r
end

local function call(d, what, ...)
    local f = d[what]
    if type(f) ~= "function" then return end
    local ok, err = pcall(f, ...)
    if not ok then say("error", "'%s' %s: %s", d.name, what, tostring(err)) end
end

local function player_xyz()
    local p = api.entity.player()
    if not p then return nil end
    local x, y, z = p:position()
    if not x then return nil end
    return x, y, z
end

-- Where a new NPC goes: the remembered home on a respawn, else `position` ({x,y,z} or function(player) -> x,y,z) or `offset` from the player.
local function spawn_position(d, old)
    if old and type(old.home) == "table" and old.home[1] then return old.home[1], old.home[2], old.home[3] end
    local p = d.position
    if type(p) == "function" then
        local player = api.entity.player()
        if not player then return nil end
        local ok, x, y, z = pcall(p, player)
        if not ok then say("error", "'%s' position: %s", d.name, tostring(x)) return nil end
        if x == nil then return nil end    -- "not known yet": tried again at the next reconciliation
        if type(x) ~= "number" or type(y) ~= "number" or type(z) ~= "number" then say("error", "'%s' position() must return x, y, z", d.name) return nil end
        return x, y, z
    elseif type(p) == "table" then
        return p[1], p[2], p[3]
    end
    local px, py, pz = player_xyz()
    if not px then return nil end
    local o = d.offset
    return px + o[1], py + o[2], pz + o[3]
end

-- Removes an entity and frees its name at once (a killed entity may stay registered until the end of the frame).
local function retire(e, name)
    local ok = pcall(function() e:set_name(name .. "~retired") end)
    e:kill()
end

local function entity_of(rec)
    if type(rec) ~= "table" or type(rec.id) ~= "string" then return nil end
    return api.entity.by_id(rec.id)
end

local function xyz(e)
    local x, y, z = e:position()
    if x then return { x, y, z } end
end

-- Creates the NPC (a new one, or the replacement of a dead / vanished / outdated one). `old` is the previous record (its home and death count are kept).
local function do_spawn(d, old, reason)
    local recs = records(true)
    if api.entity.get(d.name) then
        once("taken:" .. d.name, "warn", "'%s': another entity already has this name; not spawning", d.name)
        return nil, "the name is taken"
    end
    if not api.template.get(d.template) then
        once("template:" .. d.name, "warn", "'%s': template '%s' does not exist (yet); will try again on the next session start", d.name, d.template)
        return nil, "unknown template '" .. d.template .. "'"
    end
    local x, y, z = spawn_position(d, old)
    if not x then return nil, "no position (the player is not available yet)" end
    local o = {}
    for _, k in ipairs(SPAWN_KEYS) do o[k] = d[k] end
    o.name = d.name
    local e = api.npc.spawn(d.template, x, y, z, o)
    if not e then
        once("spawn:" .. d.name, "warn", "'%s': spawning '%s' failed", d.name, d.template)
        return nil, "spawning failed"
    end
    e:set_save_relevant(true)    -- the engine's default for spawned entities; stated so the savegame keeps the NPC
    local rec = { id = e:id(), template = d.template, version = d.version, home = (old and old.home) or { x, y, z }, deaths = (old and old.deaths) or 0 }
    recs[d.name] = rec
    tr("persistent '%s' %s at (%.0f, %.0f, %.0f) id %s", d.name, reason, x, y, z, tostring(rec.id))
    say("info", "'%s' %s (%s)", d.name, reason, d.template)
    call(d, "on_ready", e, reason, d.handle)
    return e
end

local function wants_respawn(d, rec)
    local r = d.respawn
    if r == true then return true end
    if type(r) == "function" then
        local ok, res = pcall(r, { name = d.name, deaths = rec.deaths or 0, handle = d.handle })
        if not ok then say("error", "'%s' respawn: %s", d.name, tostring(res)) return false end
        return res and true or false
    end
    return false
end

-- `enabled = function() -> bool` of a declaration (risen.spawn.when uses it): while it answers false the NPC is neither created, restored nor replaced.
local function gate_open(d)
    if d.enabled == nil then return true end
    local ok, res = pcall(d.enabled)
    if not ok then say("error", "'%s' enabled: %s", d.name, tostring(res)) return false end
    return res and true or false
end

-- Brings one declared NPC into the state its record says: restore (find by ID, give the name back), replace (outdated), respawn (dead + policy,
-- or vanished) or create (no record).
local function reconcile(d)
    if not gate_open(d) then return end
    local recs = records(true)
    local rec = recs[d.name]
    if rec and rec.removed then return end
    local e = entity_of(rec)
    local reason = "spawned"
    if rec and (rec.template ~= d.template or (rec.version or 1) ~= d.version) then
        -- the declaration changed (template or `version`): the old entity is replaced, at the position of the new declaration
        say("info", "'%s': the declaration changed (%s v%s -> %s v%s): replacing", d.name, tostring(rec.template), tostring(rec.version or 1), d.template, tostring(d.version))
        if e then retire(e, d.name) end
        recs[d.name] = nil
        rec, e, reason = nil, nil, "replaced"
    end
    if rec then
        if e then
            if e:name() ~= d.name then
                local other = api.entity.get(d.name)
                if other and other:id() ~= rec.id then
                    local t = other:template()
                    if t and t:name() == d.template then
                        say("warn", "'%s': a second entity of '%s' carries this name; it was removed", d.name, d.template)
                        retire(other, d.name)
                    else
                        once("stray:" .. d.name, "warn", "'%s': another entity (%s) carries this name", d.name, tostring(other:name()))
                    end
                end
                if not e:set_name(d.name) then once("rename:" .. d.name, "warn", "'%s': the restored entity could not be given its name", d.name) end
            end
            if e:is_dead() and not rec.dead then rec.dead = true; rec.deaths = (rec.deaths or 0) + 1 end
        end
        if rec.dead then
            if wants_respawn(d, rec) then
                if e then retire(e, d.name) end      -- the corpse is removed (with its loot) before the new body is made
                rec.dead = false
                do_spawn(d, rec, "respawned")
            end
            return
        end
        if e then
            tr("persistent '%s' restored id %s", d.name, tostring(rec.id))
            call(d, "on_ready", e, "restored", d.handle)
            return
        end
        -- the record says alive, the entity is gone (removed by the engine, or its template was missing when the savegame was read)
        do_spawn(d, rec, "respawned")
        return
    end
    -- no record: adopt an entity that already carries the name (made by the mod itself), else create it
    local existing = api.entity.get(d.name)
    if existing then
        recs[d.name] = { id = existing:id(), template = d.template, version = d.version, home = xyz(existing) or { 0, 0, 0 }, deaths = 0 }
        say("info", "'%s' adopted", d.name)
        call(d, "on_ready", existing, "adopted", d.handle)
        return
    end
    do_spawn(d, nil, reason)
end

-- Records without a declaration (the mod no longer declares the NPC): the entity is removed and the record dropped.
local function prune_orphans()
    local recs = records(false)
    if not recs then return end
    local names = {}
    for name in pairs(recs) do if not declared[name] then names[#names + 1] = name end end
    table.sort(names)
    for _, name in ipairs(names) do
        local e = entity_of(recs[name])
        if e then retire(e, name) end
        recs[name] = nil
        say("info", "'%s' is no longer declared: its entity was removed", name)
    end
end

local function reconcile_all()
    ready = true
    for _, name in ipairs(order) do
        local ok, err = pcall(reconcile, declared[name])
        if not ok then say("error", "'%s': %s", name, tostring(err)) end
    end
    prune_orphans()
end

local function define_all()
    for _, name in ipairs(order) do call(declared[name], "define", declared[name].handle) end
end

local function cancel_pending()
    pending = nil
    if tick_handle then api.off(tick_handle); tick_handle = nil end
end

-- ---- events (installed once, with the first declaration) -----------------------------------------------------------------------------------
local function install_events()
    if installed then return end
    installed = true
    api.on("load.before", function() ready = false; cancel_pending(); reported = {}; define_all() end)
    api.on("session.stop", function() ready = false; cancel_pending() end)
    api.on("session.start", function() ready = false; cancel_pending(); reported = {}; define_all() end)
    api.on("world.ready", function()
        cancel_pending()
        pending = SETTLE
        tick_handle = api.on("tick", function()
            if not pending then return end
            pending = pending - 1
            if pending > 0 then return end
            cancel_pending()
            reconcile_all()
        end)
    end)
    api.on("entity.died", function(e, killer)
        if not e then return end
        local d = by_name[e:name()]
        local recs = d and records(false)
        local rec = recs and recs[d.name]
        if not rec or (rec.id and e:id() ~= rec.id) then return end
        if not rec.dead then rec.dead = true; rec.deaths = (rec.deaths or 0) + 1 end
        call(d, "on_death", e, killer, d.handle)
    end)
    -- the record is part of the savegame: bring position and dead state up to date just before it is written
    api.on("save.before", function()
        local recs = records(false)
        if not recs then return end
        for _, name in ipairs(order) do
            local rec = recs[name]
            local e = rec and not rec.removed and entity_of(rec)
            if e then
                rec.last = xyz(e) or rec.last
                if e:is_dead() and not rec.dead then rec.dead = true; rec.deaths = (rec.deaths or 0) + 1 end
            end
        end
    end)
end

-- ---- the handle -------------------------------------------------------------------------------------------------------------------------------
local Handle = {}
Handle.__index = Handle
Handle.__tostring = function(h) return "PersistentNpc(" .. h.name .. ")" end

-- handle:entity() -> Entity | nil: the NPC's entity right now (a corpse counts), found by its unique name
function Handle:entity()
    local e = api.entity.get(self.name)
    if e and e:valid() then return e end
    return nil
end

-- handle:state() -> "alive" | "dead" | "missing" | "removed" | "pending" | "disabled"
function Handle:state()
    local recs = records(false)
    local rec = recs and recs[self.name]
    if rec and rec.removed then return "removed" end
    local e = self:entity()
    if e then return e:is_dead() and "dead" or "alive" end
    if rec and rec.dead then return "dead" end
    if not rec and not gate_open(declared[self.name]) then return "disabled" end
    if rec and ready then return "missing" end
    return "pending"
end

function Handle:is_dead() return self:state() == "dead" end

-- handle:deaths() -> how often the NPC died (kept in the savegame)
function Handle:deaths()
    local recs = records(false)
    local rec = recs and recs[self.name]
    return rec and rec.deaths or 0
end

-- handle:respawn() -> Entity | nil, reason: removes the NPC (or its corpse) and makes it again at its home position; also brings back a removed one
function Handle:respawn()
    local recs = records(true)
    local rec = recs[self.name]
    local e = self:entity()
    if not e and rec then e = entity_of(rec) end
    if e then retire(e, self.name) end
    local old = rec
    if old then old.removed = nil; old.dead = false end
    recs[self.name] = old
    return do_spawn(declared[self.name], old, "respawned")
end

-- handle:ensure() -> Entity | nil, reason: makes the NPC exist now as its declaration says (what the reconciliation does after a load): creates it when it is new, restores or
-- re-creates it when the savegame lost it, brings a removed one back, leaves a living one alone. A declaration whose `enabled` says no is not touched.
function Handle:ensure()
    if not ready then return nil, "the world is not ready" end
    local d = declared[self.name]
    if not gate_open(d) then return nil, "disabled" end
    local recs = records(true)
    local rec = recs[self.name]
    if rec and rec.removed then return self:respawn() end
    local ok, err = pcall(reconcile, d)
    if not ok then say("error", "'%s': %s", self.name, tostring(err)) return nil, tostring(err) end
    return self:entity()
end

-- handle:remove(): takes the NPC out of the world for good (it stays gone in this savegame until handle:respawn())
function Handle:remove()
    local recs = records(true)
    local rec = recs[self.name] or { template = self.template, version = declared[self.name].version, deaths = 0 }
    local e = self:entity() or entity_of(rec)
    if e then retire(e, self.name) end
    rec.removed = true
    rec.dead = false
    rec.id = nil
    recs[self.name] = rec
end

-- ---- risen.npc.persistent -----------------------------------------------------------------------------------------------------------------------
local function is_xyz(t) return type(t) == "table" and type(t[1]) == "number" and type(t[2]) == "number" and type(t[3]) == "number" end

-- risen.npc.persistent(spec) -> handle | nil, reason
function npc.persistent(spec)
    local function fail(reason)
        say("error", "%s", reason)
        return nil, reason
    end
    if type(spec) ~= "table" then return fail("risen.npc.persistent expects a table { name = ..., template = ..., position = {x, y, z} }") end
    for k in pairs(spec) do
        if not KNOWN[k] then return fail(("unknown option '%s'"):format(tostring(k))) end
    end
    local name = spec.name
    if type(name) ~= "string" or not name:match("^[%w_%-]+$") then return fail("name must be a string of letters, digits, '_' and '-'") end
    if type(spec.template) ~= "string" or spec.template == "" then return fail(("'%s': template must be a template name"):format(name)) end
    local pos = spec.position
    if (pos == nil) == (spec.offset == nil) then return fail(("'%s': give exactly one of position (= {x, y, z} or function(player) -> x, y, z) and offset (= {dx, dy, dz} from the player)"):format(name)) end
    if pos ~= nil and type(pos) ~= "function" and not is_xyz(pos) then return fail(("'%s': position must be {x, y, z} or a function"):format(name)) end
    if spec.offset ~= nil and not is_xyz(spec.offset) then return fail(("'%s': offset must be {dx, dy, dz}"):format(name)) end
    if spec.version ~= nil and (type(spec.version) ~= "number" or spec.version ~= math.floor(spec.version)) then return fail(("'%s': version must be an integer"):format(name)) end
    if spec.respawn ~= nil and type(spec.respawn) ~= "boolean" and type(spec.respawn) ~= "function" then return fail(("'%s': respawn must be true, false or a function"):format(name)) end
    for _, f in ipairs { "define", "on_ready", "on_death", "enabled" } do
        if spec[f] ~= nil and type(spec[f]) ~= "function" then return fail(("'%s': %s must be a function"):format(name, f)) end
    end
    if owner[name] and owner[name] ~= modid then return fail(("'%s' is already declared by the mod '%s'"):format(name, owner[name])) end
    if declared[name] then return declared[name].handle end    -- idempotent, like define/create

    local d = {}
    for k, v in pairs(spec) do d[k] = v end
    d.version = spec.version or 1
    d.respawn = spec.respawn or false
    d.handle = setmetatable({ name = name, template = spec.template }, Handle)
    declared[name] = d
    owner[name] = modid
    order[#order + 1] = name
    by_name[name] = d
    if type(spec.label) == "string" then api.loc.set("FO_" .. name, spec.label) end   -- the name shown when the player looks at it
    install_events()
    if ready then
        -- declared late (inside a running session): it is created / found right away
        local ok, err = pcall(reconcile, d)
        if not ok then say("error", "'%s': %s", name, tostring(err)) end
    end
    return d.handle
end

-- risen.npc.persistent_get(name) -> handle | nil: a declaration of this mod
function npc.persistent_get(name)
    local d = declared[name]
    return d and d.handle or nil
end

-- risen.npc.persistent_list() -> { handle, ... } in declaration order
function npc.persistent_list()
    local out = {}
    for _, name in ipairs(order) do out[#out + 1] = declared[name].handle end
    return out
end
)LUA";

}  // namespace spass::game
