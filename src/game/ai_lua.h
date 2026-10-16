#pragma once

// The per-mod part of the AI bridge (lua_ai.cpp): risen.ai.start learns `persist`, risen.ai.stop / forget / persisted. A Lua AI state does not survive a savegame (the engine
// rebuilds every NPC's script stack on a load and returns him to his routine), so a state started with `persist = true` is remembered in the mod's risen.save (key
// `ai_states`: { [npc id] = { state, args } }) and started again 30 frames after world.ready, with ctx.resumed = true. Records are dropped when the state ends by itself
// ("done", "error") or is stopped; an engine replacement ("replaced": combat, a dialogue, a hit) keeps it. Unit-tested in tests/ai_tests.inc with a mocked native layer.

namespace spass::game {

inline constexpr const char* kAiModChunk = R"LUA(
local api, modid = ...
local shared = api.ai
if type(shared) ~= "table" then return end
local ai = setmetatable({}, { __index = shared })
api.ai = ai
local log = api.log

local STORE = "ai_states"       -- the mod's key in risen.save: { [npc id] = { state = "ZS_X", args = <plain data>, after = <run generation> } }
local SETTLE = 30               -- frames after world.ready before the saved states are started again

local function say(level, fmt, ...) log[level](("ai: " .. fmt):format(...)) end

-- Fetched on every use: a load swaps the store.
local function records(create)
    local r = api.save.get(STORE)
    if type(r) ~= "table" then
        if not create then return nil end
        r = {}
        api.save.set(STORE, r)
    end
    return r
end

-- nil when `v` can be stored in a savegame (nil, booleans, numbers, strings, acyclic tables of those), else the reason
local function check_plain(v, depth, seen)
    local t = type(v)
    if t == "nil" or t == "boolean" or t == "number" or t == "string" then return nil end
    if t ~= "table" then return t .. " values cannot be saved" end
    if depth > 8 then return "nested deeper than 8 levels" end
    seen = seen or {}
    if seen[v] then return "a table contains itself" end
    seen[v] = true
    for k, x in pairs(v) do
        local kt = type(k)
        if kt ~= "string" and kt ~= "number" then return "a table key of type " .. kt end
        local why = check_plain(x, depth + 1, seen)
        if why then return why end
    end
    seen[v] = nil
    return nil
end

local function id_of(npc)
    if type(npc) ~= "userdata" or type(npc.id) ~= "function" then return nil end
    return npc:id()
end

local function forget_id(id)
    local r = records(false)
    if r and r[id] ~= nil then r[id] = nil end
end

-- risen.ai.start(npc, state [, args [, { interrupt, resumed, persist }]]) -> true | nil, reason
function ai.start(npc, state, args, opts)
    local persist = false
    local native_opts = nil
    if opts ~= nil then
        if type(opts) ~= "table" then return nil, "the fourth argument must be a table { interrupt, resumed, persist } or nil" end
        native_opts = {}
        for k, v in pairs(opts) do
            if k == "persist" then
                if type(v) ~= "boolean" then return nil, "persist must be true or false" end
                persist = v
            else
                native_opts[k] = v
            end
        end
    end
    if persist then
        local why = check_plain(args, 0)
        if why then return nil, "persist: the arguments must be plain data (" .. why .. ")" end
    end
    local ok, reason = shared.start(npc, state, args, native_opts)
    if not ok then return nil, reason end
    local id = id_of(npc)
    if id then
        if persist then
            records(true)[id] = { state = state, args = args, after = shared.generation() }
        else
            forget_id(id)         -- a state that is not persisted replaces whatever was remembered for the NPC
        end
    end
    return true
end

-- risen.ai.stop(npc [, next_state]) -> true | nil, reason
function ai.stop(npc, next_state)
    local ok, reason = shared.stop(npc, next_state)
    if not ok then return nil, reason end
    local id = id_of(npc)
    if id then forget_id(id) end
    return true
end

-- risen.ai.forget(npc) -> bool: drops what persist = true remembered for the NPC (the running state is not touched)
function ai.forget(npc)
    local id = id_of(npc)
    local r = records(false)
    if not (id and r and r[id] ~= nil) then return false end
    r[id] = nil
    return true
end

-- risen.ai.persisted() -> { { id = , state = , args = }, ... }: what this mod has remembered
function ai.persisted()
    local out = {}
    local r = records(false)
    if r then
        for id, rec in pairs(r) do out[#out + 1] = { id = id, state = rec.state, args = rec.args } end
        table.sort(out, function(a, b) return a.id < b.id end)
    end
    return out
end

-- a state that ended by itself (or was stopped) is not remembered any more; a run that began before the record was made is not the record's
api.on("ai.finished", function(npc, state, reason, _, run)
    if reason == "replaced" then return end
    local id = id_of(npc)
    local r = records(false)
    local rec = id and r and r[id]
    if rec and rec.state == state and run >= (rec.after or 0) then r[id] = nil end
end)

local function restore()
    local r = records(false)
    if not r then return end
    local ids = {}
    for id in pairs(r) do ids[#ids + 1] = id end
    table.sort(ids)
    for _, id in ipairs(ids) do
        local rec = r[id]
        local npc = type(id) == "string" and api.entity.by_id(id) or nil
        if not npc then
            r[id] = nil           -- the NPC is not in this savegame any more
        elseif type(rec) ~= "table" or not shared.exists("state", tostring(rec.state)) then
            say("warn", "the saved state '%s' of an NPC is not defined (any more): dropped", tostring(rec and rec.state))
            r[id] = nil
        else
            local ok, reason = shared.start(npc, rec.state, rec.args, { resumed = true })
            if not ok then say("warn", "the saved state '%s' could not be started again: %s", rec.state, tostring(reason)) end
            rec.after = shared.generation()
        end
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
        local ok, err = pcall(restore)
        if not ok then say("error", "restoring the saved states: %s", tostring(err)) end
    end)
end)
)LUA";

}  // namespace spass::game
