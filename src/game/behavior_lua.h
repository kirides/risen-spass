#pragma once

// The Lua part of behaviours (docs/LUA_API.md "Behaviours"): scripts attached to NPCs like components in Unity / Unreal. `risen.behavior.define(name, def)` describes one
// (parameters with defaults, `start` / `update` / `stop`, handlers of the NPC's events), `risen.behavior.attach(target, name, params)` puts it on one NPC (a unique name, an
// Entity or a persistent NPC handle), `risen.behavior.attach_template(template, name, params)` on every NPC of a template. Pure Lua on top of the events (tick, world.ready,
// entity.spawned / died / damaged, info.started, inventory.changed, quest.status_changed, and any event a definition names in `events`), risen.entity and risen.save, so the
// unit tests run it against mocks.
//
//   * An attachment is a declaration: it holds for every session, the behaviour starts whenever its NPC is in the world (SETTLE frames after world.ready, when the persistent
//     NPCs have their names back; an NPC that appears later is found by a look every RESCAN frames, a template's NPCs by entity.spawned) and stops when the NPC dies or is gone.
//     Attachments made while the mod loads are made again by every run of the mod; one made during a session (a quest handler) lasts for that session, or for that savegame
//     with `{ persist = true }` (kept in risen.save, key `behaviors_attached`).
//   * Each running attachment has a `state` table that is saved with the game (risen.save key `behaviors`: { [target key] = { [behaviour] = state } }; the key is the unique
//     name, or "@" .. Entity:id() for template NPCs and nameless entities), so a behaviour continues where it was after a load (`self.resumed`).
//   * Errors never reach the engine: a callback that raises is logged once and switches that one attachment off until the next session.
//   * Not called on teardown: when the world goes away (load, new game, quit) or the mod is hot-reloaded, `stop` does not run (the engine is tearing the NPCs down).
// Arguments: the per-mod `risen` table and the mod id.
namespace spass::game {

inline const char* kBehaviorChunk = R"LUA(
local api, modid = ...
local log = api.log

local STORE = "behaviors"               -- risen.save: { [target key] = { [behaviour] = state } }
local ATTACHED = "behaviors_attached"    -- risen.save: { { kind, target, behavior, params } } attachments made during a session with persist = true
local SETTLE = 40                       -- frames after world.ready before the behaviours start (persistent NPCs are named again after 30)
local RESCAN = 15                       -- frames between looks for declared NPCs that are not in the world yet
local MAX_DT = 0.25                     -- a longer frame (a menu, a loading pause) counts as this much

local behavior = {}
api.behavior = behavior

local function tr(...) local t = api.trace if t and t.enabled("behavior") then t("behavior", ...) end end

local defs = {}                         -- name -> definition
local decls = {}                        -- list of { kind = "name" | "id" | "template", target, behavior, params, runtime, persist }
local running = {}                      -- target key -> { [behaviour] = instance }
local broken = {}                       -- target key .. "|" .. behaviour -> true: switched off by an error until the next session
local queued = {}                       -- entities of templates waiting a few frames after entity.spawned: { entity, frames }
local ready, settle, rescan, last_clock = false, nil, 0, nil
local failures, last_failure = 0, nil

local HANDLERS = { "start", "update", "stop", "on_death", "on_damage", "on_talk", "on_items", "on_quest", "on_player_near", "on_player_far" }
local KNOWN = { params = true, interval = true, near = true, description = true, events = true }
for _, k in ipairs(HANDLERS) do KNOWN[k] = true end

local clock = api.clock or os.clock
local unpack = table.unpack or unpack
local listen

-- ---- helpers --------------------------------------------------------------------------------------------------------------------------------

-- plain data that risen.save can keep: booleans, finite numbers, strings, acyclic tables of those with string / number keys
local function plain(v, seen)
    local t = type(v)
    if t == "boolean" or t == "string" then return true end
    if t == "number" then return v == v and v ~= math.huge and v ~= -math.huge end
    if t ~= "table" then return false end
    seen = seen or {}
    if seen[v] then return false end
    seen[v] = true
    for k, x in pairs(v) do
        if (type(k) ~= "string" and type(k) ~= "number") or not plain(x, seen) then return false end
    end
    seen[v] = nil
    return true
end

local function copy(v)
    if type(v) ~= "table" then return v end
    local c = {}
    for k, x in pairs(v) do c[k] = copy(x) end
    return c
end

local function merged(defaults, params)
    local p = copy(defaults or {})
    for k, v in pairs(params or {}) do p[k] = copy(v) end
    return p
end

local function valid_name(n) return type(n) == "string" and #n > 0 and #n <= 64 and n:match("^[%a_][%w_%.%-]*$") ~= nil end

local function is_entity(t)
    return (type(t) == "userdata" or type(t) == "table") and type(t.id) == "function" and type(t.name) == "function"
end

-- a target: a unique name, a persistent NPC / spawn rule handle ({ name = "..." }) or an Entity -> kind, target key text
local function target_of(t)
    if type(t) == "string" then
        if t == "" then return nil, "the target name is empty" end
        return "name", t
    end
    if type(t) == "table" and type(t.name) == "string" and t.name ~= "" then return "name", t.name end
    if is_entity(t) then
        local ok, name = pcall(t.name, t)
        local okid, id = pcall(t.id, t)
        if not ok or not okid or type(id) ~= "string" or id == "" then return nil, "the entity is not valid" end
        -- an entity named after its template (a spawned wolf, an item) has no unique name: it is found by its ID
        local tok, tpl = pcall(t.template, t)
        local tname = tok and tpl and tpl.name and tpl:name() or nil
        if type(name) == "string" and name ~= "" and name ~= tname then return "name", name end
        return "id", id
    end
    return nil, "expected an NPC: a unique name, an Entity or a persistent NPC handle"
end

local function key_of(kind, target) return kind == "id" and ("@" .. target) or target end

local function states(create)
    local s = api.save.get(STORE)
    if type(s) ~= "table" then
        if not create then return nil end
        s = {}
        api.save.set(STORE, s)
    end
    return s
end

local function attached(create)
    local a = api.save.get(ATTACHED)
    if type(a) ~= "table" then
        if not create then return nil end
        a = {}
        api.save.set(ATTACHED, a)
    end
    return a
end

-- ---- instances --------------------------------------------------------------------------------------------------------------------------------

local Instance = {}
Instance.__index = Instance

local stop_instance

-- self:detach(): this attachment ends ("detached") with its saved state; a single NPC's attachment is no longer declared, a template's NPC loses it for this session
function Instance:detach()
    local d = self.decl
    if d.kind ~= "template" then
        for i, x in ipairs(decls) do if x == d then table.remove(decls, i) break end end
        local a = attached(false)
        if a then
            for j = #a, 1, -1 do if a[j].kind == d.kind and a[j].target == d.target and a[j].behavior == d.behavior then table.remove(a, j) end end
        end
    end
    if not self.off then stop_instance(self, "detached", true) end
end

local function call_fn(inst, what, f, ...)
    if type(f) ~= "function" or inst.off then return end
    local ok, err = pcall(f, inst, ...)
    if ok then return err end
    failures = failures + 1
    last_failure = ("%s on %s: %s failed: %s"):format(inst.name, inst.key, what, tostring(err))
    log.warn("behavior " .. last_failure .. " (switched off until the next session)")
    inst.off = true
    broken[inst.key .. "|" .. inst.name] = true
    local r = running[inst.key]
    if r and r[inst.name] == inst then r[inst.name] = nil end
end

local function call(inst, what, ...)
    local def = defs[inst.name]
    return call_fn(inst, what, def and def[what], ...)
end

local function start_instance(d, e, key)
    local def = defs[d.behavior]
    if not def or broken[key .. "|" .. d.behavior] then return end
    running[key] = running[key] or {}
    if running[key][d.behavior] then return end
    local st = states(true)
    st[key] = st[key] or {}
    local s = st[key][d.behavior]
    local resumed = type(s) == "table"
    if not resumed then s = {} st[key][d.behavior] = s end
    local inst = setmetatable({ entity = e, name = d.behavior, key = key, target = key, params = merged(def.params, d.params), state = s, resumed = resumed,
                                time = 0, acc = 0, near_in = false, decl = d }, Instance)
    running[key][d.behavior] = inst
    tr("start", d.behavior, "on", key, resumed and "(resumed)" or "")
    call(inst, "start")
end

function stop_instance(inst, reason, forget)
    local r = running[inst.key]
    if r and r[inst.name] == inst then r[inst.name] = nil end
    if r and next(r) == nil then running[inst.key] = nil end
    tr("stop", inst.name, "on", inst.key, reason)
    call(inst, "stop", reason)
    inst.off = true
    if forget then
        local st = states(false)
        if st and st[inst.key] then
            st[inst.key][inst.name] = nil
            if next(st[inst.key]) == nil then st[inst.key] = nil end
        end
    end
end

local function each_running(fn)
    local list = {}
    for _, r in pairs(running) do for _, inst in pairs(r) do list[#list + 1] = inst end end
    for _, inst in ipairs(list) do if not inst.off then fn(inst) end end
end

-- the running instances of an entity (by unique name and by ID)
local function instances_of(e)
    if not e then return {} end
    local out = {}
    local ok, name = pcall(e.name, e)
    if ok and running[name] then for _, inst in pairs(running[name]) do out[#out + 1] = inst end end
    local okid, id = pcall(e.id, e)
    if okid and id and running["@" .. id] then for _, inst in pairs(running["@" .. id]) do out[#out + 1] = inst end end
    return out
end

-- the entity is still there: Entity:id() answers nil once the engine removed it. (Entity:valid() would look the NAME up again, which rebinds the handle of an NPC
-- named after its template to the first NPC of that name.)
local function present(e)
    if not e then return false end
    local ok, id = pcall(e.id, e)
    return ok and type(id) == "string" and id ~= ""
end

local function alive(e)
    if not present(e) then return false end
    local okd, dead = pcall(e.is_dead, e)
    return okd and not dead
end

local function template_name(e)
    local ok, t = pcall(e.template, e)
    if not ok or not t then return nil end
    local okn, n = pcall(t.name, t)
    return okn and n or nil
end

-- start what is declared and whose NPC is there; `scan`: also every NPC of the world for the template attachments
local function start_all(scan)
    local by_template
    for _, d in ipairs(decls) do
        if d.kind == "template" then
            by_template = by_template or {}
            by_template[d.target] = by_template[d.target] or {}
            table.insert(by_template[d.target], d)
        else
            local key = key_of(d.kind, d.target)
            if not (running[key] and running[key][d.behavior]) and not broken[key .. "|" .. d.behavior] then
                local e = d.kind == "id" and api.entity.by_id(d.target) or api.entity.get(d.target)
                if alive(e) then start_instance(d, e, key) end
            end
        end
    end
    if scan and by_template and api.entity.npcs then
        for _, e in ipairs(api.entity.npcs() or {}) do
            local list = by_template[template_name(e) or ""]
            if list and alive(e) then
                local key = "@" .. e:id()
                for _, d in ipairs(list) do start_instance(d, e, key) end
            end
        end
    end
end

local function reset(why)
    ready, settle, rescan, queued, broken = false, nil, 0, {}, {}
    if next(running) then tr("world torn down (" .. why .. "): behaviours dropped without stop") end
    for _, r in pairs(running) do for _, inst in pairs(r) do inst.off = true end end
    running = {}
    -- attachments made during the last session go; the persisted ones come back from the savegame at world.ready
    local keep = {}
    for _, d in ipairs(decls) do if not d.runtime then keep[#keep + 1] = d end end
    decls = keep
end

local function find_decl(kind, target, name)
    for i, d in ipairs(decls) do
        if d.kind == kind and d.target == target and d.behavior == name then return d, i end
    end
end

local function add_decl(kind, target, name, params, runtime, persist)
    local d = find_decl(kind, target, name)
    if d then
        d.params = params
        d.persist = d.persist or persist
        return d, false
    end
    d = { kind = kind, target = target, behavior = name, params = params, runtime = runtime, persist = persist }
    decls[#decls + 1] = d
    return d, true
end

local function restore_attached()
    for _, a in ipairs(attached(false) or {}) do
        if type(a) == "table" and defs[a.behavior] and (a.kind == "name" or a.kind == "id" or a.kind == "template") and type(a.target) == "string" then
            add_decl(a.kind, a.target, a.behavior, a.params, true, true)
        end
    end
end

-- any event a definition's `events` names: forwarded to every running attachment of a behaviour that handles it (one handler per event name)
local listening = {}
listen = function(ev)
    if listening[ev] then return end
    listening[ev] = true
    api.on(ev, function(...)
        if not ready then return end
        local args = { n = select("#", ...), ... }
        each_running(function(inst)
            local def = defs[inst.name]
            local f = def and def.events and def.events[ev]
            if f then call_fn(inst, ev, f, unpack(args, 1, args.n)) end
        end)
    end)
end

-- ---- the API ----------------------------------------------------------------------------------------------------------------------------------

-- behavior.define(name, def) -> true | nil, reason
function behavior.define(name, def)
    if not valid_name(name) then return nil, "behaviour name: letters, digits, _ . -, up to 64 characters, not starting with a digit" end
    if type(def) ~= "table" then return nil, "the definition must be a table" end
    for k, v in pairs(def) do
        if not KNOWN[k] then return nil, ("unknown key '%s' (params, interval, near, description, %s)"):format(tostring(k), table.concat(HANDLERS, ", ")) end
    end
    for _, k in ipairs(HANDLERS) do
        if def[k] ~= nil and type(def[k]) ~= "function" then return nil, k .. " must be a function" end
    end
    if def.params ~= nil and (type(def.params) ~= "table" or not plain(def.params)) then return nil, "params must be a table of plain values (numbers, strings, booleans, tables of those)" end
    if def.interval ~= nil and (type(def.interval) ~= "number" or def.interval ~= def.interval or def.interval < 0 or def.interval > 3600) then
        return nil, "interval must be a number of seconds from 0 (every frame) to 3600"
    end
    if def.near ~= nil and (type(def.near) ~= "number" or def.near ~= def.near or def.near <= 0 or def.near > 100000) then
        return nil, "near must be a distance > 0 (units, 100 = 1 m)"
    end
    if def.description ~= nil and type(def.description) ~= "string" then return nil, "description must be a string" end
    if def.events ~= nil then
        if type(def.events) ~= "table" then return nil, "events must be a table { [event name] = function(self, ...) }" end
        for ev, f in pairs(def.events) do
            if type(ev) ~= "string" or not ev:match("^[%w_][%w_%.%-]*$") then return nil, "events: event names are strings like \"quest.status_changed\"" end
            if type(f) ~= "function" then return nil, ("events[\"%s\"] must be a function"):format(ev) end
        end
        for ev in pairs(def.events) do listen(ev) end
    end
    local redefined = defs[name] ~= nil
    defs[name] = def
    tr(redefined and "redefined" or "defined", name)
    return true
end

-- behavior.attach(target, name [, params [, { persist = bool }]]) -> true | nil, reason
function behavior.attach(target, name, params, opts)
    local kind, t = target_of(target)
    if not kind then return nil, t end
    if not defs[name] then return nil, ("no behaviour '%s' is defined (risen.behavior.define)"):format(tostring(name)) end
    if params ~= nil and (type(params) ~= "table" or not plain(params)) then return nil, "params must be a table of plain values" end
    if opts ~= nil and type(opts) ~= "table" then return nil, "options must be a table" end
    local persist = opts and opts.persist == true
    local runtime = ready or settle ~= nil
    local d, new = add_decl(kind, t, name, copy(params), runtime, persist)
    if persist then
        local a = attached(true)
        local found = false
        for _, x in ipairs(a) do if x.kind == kind and x.target == t and x.behavior == name then x.params = copy(params) found = true end end
        if not found then a[#a + 1] = { kind = kind, target = t, behavior = name, params = copy(params) } end
    end
    -- new parameters for a running attachment: it starts again with them
    local key = key_of(kind, t)
    local inst = running[key] and running[key][name]
    if inst and not new then stop_instance(inst, "restarted", false) end
    if ready then start_all(false) end
    return true
end

-- behavior.attach_template(template, name [, params]) -> true | nil, reason
function behavior.attach_template(template, name, params)
    if type(template) == "table" and type(template.name) == "function" then template = template:name() end
    if type(template) ~= "string" or template == "" then return nil, "expected a template name" end
    if not defs[name] then return nil, ("no behaviour '%s' is defined (risen.behavior.define)"):format(tostring(name)) end
    if params ~= nil and (type(params) ~= "table" or not plain(params)) then return nil, "params must be a table of plain values" end
    add_decl("template", template, name, copy(params), ready or settle ~= nil, false)
    if ready then start_all(true) end
    return true
end

-- behavior.detach(target, name) -> true when something was attached
function behavior.detach(target, name)
    local kind, t = target_of(target)
    if not kind then return false end
    local found = false
    local d, i = find_decl(kind, t, name)
    if d then table.remove(decls, i) found = true end
    local a = attached(false)
    if a then
        for j = #a, 1, -1 do if a[j].kind == kind and a[j].target == t and a[j].behavior == name then table.remove(a, j) end end
    end
    -- a template NPC: its own instance only (the template keeps the behaviour for the others)
    local key = key_of(kind, t)
    local inst = running[key] and running[key][name]
    if inst then stop_instance(inst, "detached", true) found = true end
    return found
end

-- behavior.detach_template(template, name) -> true when it was attached; stops it on every NPC of the template
function behavior.detach_template(template, name)
    if type(template) == "table" and type(template.name) == "function" then template = template:name() end
    local d, i = find_decl("template", template, name)
    if not d then return false end
    table.remove(decls, i)
    each_running(function(inst) if inst.decl == d then stop_instance(inst, "detached", true) end end)
    return true
end

-- behavior.get(target, name) -> the running instance (self of the callbacks) or nil
function behavior.get(target, name)
    if is_entity(target) then
        for _, inst in ipairs(instances_of(target)) do if inst.name == name then return inst end end
        return nil
    end
    local kind, t = target_of(target)
    if not kind then return nil end
    local r = running[key_of(kind, t)]
    return r and r[name] or nil
end

-- behavior.list() -> { { kind, target, behavior, running } } of this mod's attachments; template attachments list their running NPCs as `count`
function behavior.list()
    local out = {}
    for _, d in ipairs(decls) do
        local n = 0
        if d.kind == "template" then
            each_running(function(inst) if inst.decl == d then n = n + 1 end end)
        else
            local r = running[key_of(d.kind, d.target)]
            n = (r and r[d.behavior]) and 1 or 0
        end
        out[#out + 1] = { kind = d.kind, target = d.target, behavior = d.behavior, running = n > 0, count = n, persist = d.persist or false }
    end
    return out
end

-- behavior.defined() -> sorted names
function behavior.defined()
    local out = {}
    for n in pairs(defs) do out[#out + 1] = n end
    table.sort(out)
    return out
end

-- behavior.failures() -> count, last message
function behavior.failures() return failures, last_failure end

-- ---- events -----------------------------------------------------------------------------------------------------------------------------------

api.on("session.start", function() reset("session.start") end)
api.on("load.before", function() reset("load") end)
api.on("session.stop", function() reset("session.stop") end)
api.on("world.ready", function()
    restore_attached()
    settle = SETTLE
end)

api.on("tick", function()
    local now = clock()
    local dt = last_clock and math.min(math.max(now - last_clock, 0), MAX_DT) or 0
    last_clock = now
    if settle then
        settle = settle - 1
        if settle > 0 then return end
        settle = nil
        ready = true
        start_all(true)
    end
    if not ready then return end
    rescan = rescan + 1
    if rescan >= RESCAN then rescan = 0 start_all(false) end
    if #queued > 0 then
        local later = {}
        for _, q in ipairs(queued) do
            q.frames = q.frames - 1
            if q.frames > 0 then later[#later + 1] = q
            elseif alive(q.entity) then
                local tname = template_name(q.entity)
                for _, d in ipairs(decls) do
                    if d.kind == "template" and d.target == tname then start_instance(d, q.entity, "@" .. q.entity:id()) end
                end
            end
        end
        queued = later
    end
    local player = api.entity.player and api.entity.player()
    each_running(function(inst)
        local e = inst.entity
        if not present(e) then return stop_instance(inst, "removed", false) end
        local okd, dead = pcall(e.is_dead, e)
        if okd and dead then
            call(inst, "on_death", nil)
            if not inst.off then stop_instance(inst, "died", false) end
            return
        end
        inst.time = inst.time + dt
        inst.acc = inst.acc + dt
        local def = defs[inst.name]
        if not def then return end
        if def.near and player and (def.on_player_near or def.on_player_far) then
            local okp, d = pcall(e.distance_to, e, player)
            if okp and type(d) == "number" then
                local inside = d <= def.near
                if inside ~= inst.near_in then
                    inst.near_in = inside
                    call(inst, inside and "on_player_near" or "on_player_far", player, d)
                end
            end
        end
        if not inst.off and inst.acc >= (def.interval or 0.5) then
            local step = inst.acc
            inst.acc = 0
            call(inst, "update", step)
        end
    end)
end)

api.on("entity.spawned", function(e)
    if not e or not ready then return end
    for _, d in ipairs(decls) do
        if d.kind == "template" then
            queued[#queued + 1] = { entity = e, frames = 3 }   -- after the spawn options (name, inventory) are applied
            return
        end
    end
end)

api.on("entity.died", function(e, killer)
    for _, inst in ipairs(instances_of(e)) do
        call(inst, "on_death", killer)
        if not inst.off then stop_instance(inst, "died", false) end
    end
end)

api.on("entity.damaged", function(e, attacker, damage)
    for _, inst in ipairs(instances_of(e)) do call(inst, "on_damage", attacker, damage) end
end)

api.on("info.started", function(info, owner)
    if type(owner) ~= "string" then return end
    local list, seen = {}, {}
    for _, inst in pairs(running[owner] or {}) do list[#list + 1] = inst seen[inst] = true end
    -- an NPC with a unique name whose behaviour came from his template runs under "@<id>": find him by the name and look there too
    local ok, e = pcall(api.entity.get, owner)
    if ok and e then
        for _, inst in ipairs(instances_of(e)) do
            if not seen[inst] then list[#list + 1] = inst seen[inst] = true end
        end
    end
    for _, inst in ipairs(list) do call(inst, "on_talk", info) end
end)

-- every quest, the game's own included (quest.status_changed): any attachment of a behaviour with on_quest
api.on("quest.status_changed", function(quest, status, previous)
    if not ready then return end
    each_running(function(inst) call(inst, "on_quest", quest, status, previous) end)
end)

api.on("inventory.changed", function(owner, template, delta, total, cause)
    for _, inst in ipairs(instances_of(owner)) do call(inst, "on_items", template, delta, total, cause) end
end)

-- states that cannot be saved (a function, a cycle) would make the whole save of the mod fail: they are dropped with a warning
api.on("save.before", function()
    local st = states(false)
    if not st then return end
    for key, per in pairs(st) do
        for name, s in pairs(per) do
            if not plain(s) then
                log.warn(("behavior %s on %s: its state holds something that cannot be saved (functions, entities, cycles); the state was reset"):format(name, key))
                local fresh = {}
                per[name] = fresh
                local inst = running[key] and running[key][name]
                if inst then inst.state = fresh end
            end
        end
    end
end)
)LUA";

}  // namespace spass::game
