#pragma once

// The Lua parts of "safe updates and removals" (M15, docs/LUA_API.md "Updating and removing content"):
//   risen.migrate(from_version, fn)   per mod: a hook that rewrites the mod's saved data when a savegame written by an older version is loaded
//   risen.retire{ npcs, quests, ... } per mod: content older versions of the mod made and this one does not any more
// Both are plain Lua on top of risen.save / risen.mods / the entity API, so tests/migrate_tests.inc runs them against mocks.
namespace spass::game {

inline const char* kMigrateChunk = R"LUA(
local api, modid, version = ...
local log = api.log

local migrations = {}     -- { spec = "<2.0.0", fn = function(ctx) }
local installed = false
local reported = {}

local function say(level, fmt, ...) log[level](("migrate: [" .. modid .. "] " .. fmt):format(...)) end

-- Runs every migration whose version spec matches the version that wrote the data. They run in the order they were registered, each one on the result of the one
-- before; an error in one is logged and the next still runs.
local function run(from, to)
    if from == nil or from == "" or from == to then return 0 end
    local ran = 0
    local ctx = { from = from, to = to, mod = modid, save = api.save, log = log }
    for _, m in ipairs(migrations) do
        if (api.mods.version_matches(m.spec, from)) then
            local ok, err = pcall(m.fn, ctx)
            ran = ran + 1
            if ok then say("info", "%s -> %s (%s)", from, to, m.spec)
            else say("error", "%s -> %s (%s): %s", from, to, m.spec, tostring(err)) end
        end
    end
    return ran
end

local function install()
    if installed then return end
    installed = true
    -- a savegame is read: its data is in the store already; run before the mod's own load.before handlers (priority 0)
    api.on("load.before", function()
        local from = api.mods.saved_version(modid)
        if from and from ~= version then run(from, version) end
    end, 1000)
    -- the mod was updated while the game runs (risen.mods.reload): the data in memory was written by the previous instance
    api.on("mods.migrate", function(from, to) run(from, to) end, 1000)
end

-- risen.migrate(from_version, fn): `from_version` is a version ("1.0.0"), a comparison ("<2.0", ">=1.0,<1.5", "!=1.3") or "*". fn(ctx) gets ctx.from, ctx.to, ctx.save (the
-- mod's risen.save) and ctx.log.
function api.migrate(spec, fn)
    if type(spec) ~= "string" or spec == "" then return nil, "risen.migrate: the first argument must be a version or a comparison such as \"<2.0.0\"" end
    if type(fn) ~= "function" then return nil, "risen.migrate: the second argument must be a function" end
    local valid, why = api.mods.version_matches(spec, "0")
    if valid == nil then return nil, "risen.migrate: " .. tostring(why) end
    migrations[#migrations + 1] = { spec = spec, fn = fn }
    install()
    return true
end

-- the pieces the unit tests drive without an engine
api._migrate_run = run
)LUA";

// risen.retire{ npcs, quests, game_vars, items, save_keys } (per mod; args: the per-mod risen view, the mod id, the `retire` table of mod.toml or nil, the registrar of the global chunk below)
inline const char* kRetireChunk = R"LUA(
local api, modid, from_toml, register = ...
local log = api.log

local KINDS = { "npcs", "quests", "game_vars", "items", "save_keys" }
local SETTLE = 90     -- frames after world.ready: the persistent NPCs (30 frames) were reconciled first, the restored NPCs are settled
local declared = { npcs = {}, quests = {}, game_vars = {}, items = {}, save_keys = {} }
local order = { npcs = {}, quests = {}, game_vars = {}, items = {}, save_keys = {} }
local installed, pending, tick_handle = false, nil, nil
local doomed, verify_handle, verify_in, verify_rounds = {}, nil, 0, 0     -- entities that were asked to go

local function say(level, fmt, ...) log[level](("retire: [" .. modid .. "] " .. fmt):format(...)) end

-- adds a list of names; returns nil, reason for a wrong shape (nothing is added then)
local function add(spec)
    if type(spec) ~= "table" then return nil, "risen.retire expects a table { npcs = {...}, quests = {...}, game_vars = {...}, items = {...}, save_keys = {...} }" end
    for kind, list in pairs(spec) do
        local known = false
        for _, k in ipairs(KINDS) do if k == kind then known = true end end
        if not known then return nil, ("risen.retire: unknown kind '%s' (valid: %s)"):format(tostring(kind), table.concat(KINDS, ", ")) end
        if type(list) ~= "table" then return nil, ("risen.retire: '%s' must be a list of names"):format(kind) end
        for _, name in ipairs(list) do
            if type(name) ~= "string" or name == "" or #name > 200 then return nil, ("risen.retire: '%s' holds a name that is not a non-empty string"):format(kind) end
        end
    end
    for kind, list in pairs(spec) do
        for _, name in ipairs(list) do
            if not declared[kind][name] then declared[kind][name] = true; order[kind][#order[kind] + 1] = name end
        end
    end
    return true
end

local function store_table(key) local t = api.save.get(key) return type(t) == "table" and t or nil end

-- takes one persistent NPC out of the savegame: the entity (by the ID in the record, and by name), the record, the spawn rule state, a saved AI state and routine record
local function retire_npc(name, dry)
    local r = { name = name, entities = 0 }
    if api.npc.persistent_get and api.npc.persistent_get(name) then r.skipped = "this mod still declares it" return r end
    local recs, rules = store_table("persistent_npcs"), store_table("spawn_rules")
    local rec = recs and recs[name]
    r.record = rec ~= nil
    r.rule = rules ~= nil and rules[name] ~= nil
    local found, seen = {}, {}
    local function consider(e)
        if e then
            local id = e:id()
            if not seen[id] then seen[id] = true found[#found + 1] = e end
        end
    end
    if type(rec) == "table" and type(rec.id) == "string" then consider(api.entity.by_id(rec.id)) end
    consider(api.entity.get(name))
    r.entities = #found
    if dry then return r end
    for _, e in ipairs(found) do
        local id = e:id()
        pcall(function() e:set_name(name .. "~retired") end)
        if api.npc.clear_routine then pcall(api.npc.clear_routine, e, { apply = false }) end
        pcall(function() e:kill() end)
        if id then doomed[#doomed + 1] = { id = id, name = name } end
    end
    if type(rec) == "table" and type(rec.id) == "string" then
        local ai, routines = store_table("ai_states"), store_table("npc_routines")
        if ai then ai[rec.id] = nil end
        if routines and type(routines.npcs) == "table" then routines.npcs[rec.id] = nil end
    end
    if recs then recs[name] = nil end
    if rules then rules[name] = nil end
    return r
end

-- Applies the declarations; dry = true only reports what exists. Returns { npcs = {...}, quests = {...}, ... } with a line per name.
local function apply(dry)
    local report = { npcs = {}, quests = {}, game_vars = {}, items = {}, save_keys = {}, removed = 0 }
    for _, name in ipairs(order.npcs) do
        local r = retire_npc(name, dry)
        report.npcs[#report.npcs + 1] = r
        if not dry and not r.skipped and (r.entities > 0 or r.record or r.rule) then report.removed = report.removed + 1 end
    end
    for _, name in ipairs(order.quests) do
        local exists = api.quest.exists(name)
        local r = { name = name, exists = exists }
        if exists and not dry then r.removed = api.quest.remove(name) and true or false if r.removed then report.removed = report.removed + 1 end end
        report.quests[#report.quests + 1] = r
    end
    for _, name in ipairs(order.game_vars) do
        local exists = api.gamevar.exists(name)
        local r = { name = name, exists = exists }
        if exists and not dry then r.removed = api.gamevar.remove(name) and true or false if r.removed then report.removed = report.removed + 1 end end
        report.game_vars[#report.game_vars + 1] = r
    end
    for _, name in ipairs(order.items) do
        local player = api.entity.player()
        local n = player and api.item.count(player, name) or 0
        local r = { name = name, count = n or 0 }
        if (n or 0) > 0 and not dry then
            api.item.take(player, name, n)
            r.removed = true
            report.removed = report.removed + 1
        end
        report.items[#report.items + 1] = r
    end
    for _, name in ipairs(order.save_keys) do
        local exists = api.save.get(name) ~= nil
        local r = { name = name, exists = exists }
        if exists and not dry then api.save.remove(name) r.removed = true report.removed = report.removed + 1 end
        report.save_keys[#report.save_keys + 1] = r
    end
    if not dry and report.removed > 0 then say("info", "%d thing(s) removed", report.removed) end
    return report
end

local function cancel()
    pending = nil
    if tick_handle then api.off(tick_handle); tick_handle = nil end
end

-- Makes sure the removed entities stay away: look for them again every 30 frames for about 12 seconds (the engine may restore a layer late) and ask once more for every one that turns up.
local function watch_doomed()
    if verify_handle or #doomed == 0 then return end
    verify_in, verify_rounds = 30, 0
    verify_handle = api.on("tick", function()
        verify_in = verify_in - 1
        if verify_in > 0 then return end
        verify_in = 30
        verify_rounds = verify_rounds + 1
        for _, d in ipairs(doomed) do
            local e = api.entity.by_id(d.id)
            if e then
                pcall(function() e:set_name(d.name .. "~retired") end)
                pcall(function() e:kill() end)
            end
        end
        if verify_rounds >= 30 then
            api.off(verify_handle)
            verify_handle = nil
            doomed = {}
        end
    end)
end

local function schedule()
    cancel()
    pending = SETTLE
    tick_handle = api.on("tick", function()
        if not pending then return end
        pending = pending - 1
        if pending > 0 then return end
        cancel()
        local ok, err = pcall(apply, false)
        if not ok then say("error", "%s", tostring(err)) end
        watch_doomed()
    end)
end

local function install()
    if installed then return end
    installed = true
    api.on("world.ready", schedule)
    api.on("session.stop", cancel)
    api.on("load.before", cancel)
    if api.entity.player() then schedule() end     -- declared late, inside a running game
end

-- risen.retire(spec) -> true | nil, reason: content that older versions of this mod made and this one does not. Once the world of a game is ready (a new game, a loaded savegame, a
-- reloaded mod) the named things are taken out: persistent NPCs (entity, record, spawn rule state), quests (risen.quest.remove), game variables, items in the player's inventory, keys of risen.save.
function api.retire(spec)
    local ok, why = add(spec)
    if not ok then say("error", "%s", tostring(why)) return nil, why end
    install()
    return true
end

if type(from_toml) == "table" then
    local ok, why = add(from_toml)
    if ok then install() else say("error", "mod.toml retire: %s", tostring(why)) end
end
if register then register(modid, {
    apply = apply,
    declared = function()
        local out = {}
        for _, kind in ipairs(KINDS) do out[kind] = { unpack(order[kind]) } end
        return out
    end,
    persistent = function()
        local out = {}
        if api.npc.persistent_list then
            for _, h in ipairs(api.npc.persistent_list()) do out[#out + 1] = { name = h.name, template = h.template } end
        end
        return out
    end,
}) end
)LUA";

// the global part: risen.mods.retire / orphans / remove_orphans (args: the shared risen table); returns the registrar the per-mod chunk above calls
inline const char* kModsChunk = R"LUA(
local risen = ...
local M = risen.mods
local handles = {}        -- mod id -> { apply, declared, persistent } of the running instance
local function register(modid, handle) handles[modid] = handle end

local function id_ok(id) return type(id) == "string" and id ~= "" and #id <= 80 and id:sub(1, 1) ~= "@" end

-- risen.mods.retire(id [, { dry_run = true }]) -> report | nil, reason: applies (or, with dry_run, only reports) what the running mod declared with risen.retire / the `retire` table of its mod.toml
function M.retire(id, opts)
    if not id_ok(id) then return nil, "the first argument must be a mod id (a string)" end
    if opts ~= nil and type(opts) ~= "table" then return nil, "the options must be a table or nil" end
    local dry = opts == nil or opts.dry_run ~= false
    local h = handles[id]
    if not h then return nil, "the mod is not running, so it cannot say what it retires (keep a stub mod with a `retire` list in mod.toml, or use risen.mods.orphans / forget_data)" end
    return h.apply(dry), nil
end

-- risen.mods.orphans(id) -> list of { kind, name, template, id, entity, reason }: what a mod left in the world that nothing owns any more. Read only.
--   "undeclared"  an NPC the mod's savegame record names, which the running mod does not declare (or the mod is not running at all), and whose entity still exists
--   "stray"       an NPC of a template this process created for the mod that no record of the mod owns (the savegame had no sidecar: the engine kept the old, nameless NPC and the mod made a second one)
function M.orphans(id)
    if not id_ok(id) then return nil, "the first argument must be a mod id (a string)" end
    local rec = M.record(id)
    local recs = rec and type(rec.persistent_npcs) == "table" and rec.persistent_npcs or {}
    local h = handles[id]
    local declared_names, templates = {}, {}
    if h then
        for _, d in ipairs(h.persistent()) do declared_names[d.name] = true templates[d.template] = true end
    end
    local owned, out = {}, {}
    for name, r in pairs(recs) do
        if type(r) == "table" then
            if type(r.id) == "string" then owned[r.id] = name end
            if type(r.template) == "string" then templates[r.template] = true end
        end
    end
    local names = {}
    for name in pairs(recs) do names[#names + 1] = name end
    table.sort(names)
    for _, name in ipairs(names) do
        local r = recs[name]
        if type(r) == "table" and not r.removed and not declared_names[name] then
            local e = type(r.id) == "string" and risen.entity.by_id(r.id) or nil
            if e then
                out[#out + 1] = { kind = "undeclared", name = name, template = r.template, id = r.id, entity = e, reason = h and "the running mod no longer declares it" or "the mod is not running" }
            end
        end
    end
    -- only templates this layer created (risen.template.create / the balance sections) are searched: a shipped template has thousands of unrelated NPCs
    local created = risen.template.created()
    local tnames = {}
    for t in pairs(templates) do if created[t] then tnames[#tnames + 1] = t end end
    table.sort(tnames)
    for _, tpl in ipairs(tnames) do
        local t = risen.template.get(tpl)
        local list = t and t:instances(1000) or {}
        for _, e in ipairs(list) do
            local eid = e:id()
            if eid and not owned[eid] and e:is_npc() then
                out[#out + 1] = { kind = "stray", name = e:name(), template = tpl, id = eid, entity = e, reason = "no record of the mod owns it" }
            end
        end
    end
    return out
end

-- risen.mods.remove_orphans(id, list) -> removed: removes the entities of a list that risen.mods.orphans returned (and the records of "undeclared" ones). Never runs by itself.
function M.remove_orphans(id, list)
    if not id_ok(id) then return nil, "the first argument must be a mod id (a string)" end
    if type(list) ~= "table" then return nil, "the second argument must be a list returned by risen.mods.orphans" end
    local removed = 0
    for _, o in ipairs(list) do
        if type(o) == "table" and type(o.id) == "string" and (o.kind == "undeclared" or o.kind == "stray") then
            local e = risen.entity.by_id(o.id)
            if e then
                pcall(function() e:set_name("orphan~removed") end)
                if risen.npc.clear_routine then pcall(risen.npc.clear_routine, e, { apply = false }) end
                pcall(function() e:kill() end)
                removed = removed + 1
            end
            if o.kind == "undeclared" and type(o.name) == "string" then
                M.forget_record(id, "persistent_npcs", o.name)
                M.forget_record(id, "spawn_rules", o.name)
                M.forget_record(id, "ai_states", o.id)
            end
        end
    end
    return removed
end

return register
)LUA";

}  // namespace spass::game
