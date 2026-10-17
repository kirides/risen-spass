-- modkit/runtime.lua: the generic run-time part of a mod exported by risen-modkit (docs/editor/REQUIREMENTS.md section 7).
-- The same file for every exported mod; the mod's content is plain data in data/*.lua. Do not edit: the next export replaces it
-- (hand-written additions go into custom.lua).
--
--   balance   risen.balance.install: items, item groups, new items, recipes, NPC and monster templates; the layer applies it on the first
--             tick and on every session.start
--   npcs      risen.npc.persistent (or risen.spawn.when with appear_when / leave_when): made in a new game, restored after a load, never twice;
--             the items marked "equip" are equipped when the NPC is made
--   quests    risen.quest.define, and dialogs: risen.dialog.create (parents first), both on load.before (the engine restores their saved state only
--             into objects that exist) and on every session.start (a new game removes runtime quests); the quest actions of the dialogue lines
--             are scripts (risen.script.define) defined once at load time
--
-- Events for other mods and tests:
--   "modkit.applied"   (mod id, when, report) after every application of the balance data (when = "first tick", "session.start (new)", ...)
--   "modkit.npc_ready" (mod id, npc name, reason) whenever a placed NPC is in the world (reason as risen.npc.persistent's on_ready)
--   "modkit.quest_action" (mod id, quest, action) after a dialogue line started / succeeded / failed / cancelled a quest
local M = { VERSION = 3 }

local unpack = table.unpack or unpack
local function nonempty(t) return type(t) == "table" and next(t) ~= nil end

-- Conditions are plain data in the exported files: { "quest", "Q_X", "Running" }, { "item", "It_Apple", 3 }, { "all", { ... } }, { "not", c }, ...
-- (the names of risen.cond). Returns a risen.cond condition or nil for nil.
function M.cond(d)
    if d == nil then return nil end
    local kind = d[1]
    if kind == "all" or kind == "any" then
        local list = {}
        for i, c in ipairs(d[2] or {}) do list[i] = M.cond(c) end
        return risen.cond[kind](list)
    elseif kind == "not" then
        local inner = M.cond(d[2])
        if not inner then error("modkit: 'not' without a condition") end
        return risen.cond.negate(inner)
    elseif type(risen.cond[kind]) == "function" then
        return risen.cond[kind](unpack(d, 2))
    end
    error(("modkit: unknown condition '%s'"):format(tostring(kind)))
end

-- a patrol is stored as { points = { ... }, name = , kind = } (JSON has no mixed tables); the layer wants the points as the array part
local function routine(r)
    if type(r) ~= "table" or type(r.patrol) ~= "table" or not r.patrol.points then return r end
    local out, patrol = {}, {}
    for k, v in pairs(r) do out[k] = v end
    for i, pt in ipairs(r.patrol.points) do patrol[i] = pt end
    for k, v in pairs(r.patrol) do
        if k ~= "points" then patrol[k] = v end
    end
    out.patrol = patrol
    return out
end

local function npc_spec(id, n, define)
    local s = {
        name = n.name, template = n.template, peaceful = n.peaceful, guild = n.guild, level = n.level, hp_max = n.hp_max,
        items = n.items, routine = routine(n.routine), respawn = n.respawn, version = n.version, define = define,
    }
    if n.offset then s.offset = n.offset else s.position = n.position end
    local equip = n.equip
    s.on_ready = function(entity, reason)
        if equip and (reason == "spawned" or reason == "respawned" or reason == "replaced") then
            for _, item in ipairs(equip) do
                local ok, why = risen.item.equip(entity, item)
                if not ok then risen.log.warn(("%s: %s cannot equip %s: %s"):format(id, n.name, item, tostring(why))) end
            end
        end
        risen.emit("modkit.npc_ready", id, n.name, reason)
    end
    return s
end

-- what a Quest node of a dialogue does
function M.quest_action(quest, action)
    if action == "start" then
        risen.quest.open(quest)
        risen.quest.run(quest)
    elseif action == "succeed" then
        risen.quest.succeed(quest)
    elseif action == "fail" then
        risen.quest.fail(quest)
    elseif action == "cancel" then
        risen.quest.close(quest)
    end
    risen.emit("modkit.quest_action", mod and mod.id or "?", quest, action)
end

local function define_content(id, data)
    for _, q in ipairs(data.quests or {}) do
        local ok, why = risen.quest.define({ name = q.name, title = q.title, type = q.type, xp = q.xp, silent = q.silent })
        if not ok then risen.log.error(("%s: quest %s: %s"):format(id, q.name, tostring(why))) end
    end
    for _, d in ipairs((data.dialogs or {}).infos or {}) do
        local spec = {}
        for k, v in pairs(d) do spec[k] = v end
        spec.when = M.cond(d.when)
        local info, why = risen.dialog.create(spec)
        if not info then risen.log.error(("%s: dialogue option %s: %s"):format(id, d.name, tostring(why))) end
    end
end

function M.install(data)
    local id = mod and mod.id or "?"
    if nonempty(data.balance) then
        risen.balance.install(data.balance, {
            on_applied = function(when, report)
                if report and not report.ok then
                    for _, f in ipairs(report.failed or {}) do risen.log.warn(id .. ": " .. tostring(f)) end
                end
                risen.emit("modkit.applied", id, when, report)
            end,
        })
    end
    for _, a in ipairs((data.dialogs or {}).quest_actions or {}) do
        risen.script.define(a.script, function() M.quest_action(a.quest, a.action) return 0 end)
    end
    if nonempty(data.quests) or nonempty((data.dialogs or {}).infos) then
        risen.on("load.before", function() define_content(id, data) end)
        risen.on("session.start", function() define_content(id, data) end)
    end
    for _, n in ipairs(data.npcs or {}) do
        local spec = npc_spec(id, n, nil)
        local ok, why
        if n.appear_when or n.leave_when then
            spec.when = M.cond(n.appear_when) or risen.cond.fn(function() return true end)
            spec.despawn_when = M.cond(n.leave_when)
            ok, why = risen.spawn.when(spec)
        else
            ok, why = risen.npc.persistent(spec)
        end
        if not ok then risen.log.error(("%s: NPC %s: %s"):format(id, n.name, tostring(why))) end
    end
end

return M
