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
--             are scripts (risen.script.define) defined once at load time. A quest with a flow is started by a dialogue or a trigger (an item, a kill,
--             a game variable, another quest, the start of a game) and walks its steps from the engine's events
--   world     entities spawned at a position and items put into containers of the world (by entity ID), once per savegame on world.ready
--   assets    the icon image lists (risen.assets.load_icons) registered on the first tick, before the balance data makes the items that use them
--
--   layers    groups of the NPCs, world spawns and container fills above that are switched on and off as a whole (modkit/layers.lua, optional)
--   events    the event graphs (modkit/events.lua, optional; installed by main.lua after this module)
--
-- Events for other mods and tests:
--   "modkit.applied"   (mod id, when, report) after every application of the balance data (when = "first tick", "session.start (new)", ...)
--   "modkit.npc_ready" (mod id, npc name, reason) whenever a placed NPC is in the world (reason as risen.npc.persistent's on_ready)
--   "modkit.quest_action" (mod id, quest, action) after a dialogue line started / succeeded / failed / cancelled a quest
--   "modkit.quest_started" (mod id, quest), "modkit.objective" (mod id, quest, step), "modkit.quest_done" (mod id, quest, result) for quest flows
--   "modkit.world_spawned" (mod id, key, entity), "modkit.container_filled" (mod id, key, container), "modkit.world_removed" (mod id, key, entity id),
--   "modkit.world_moved" (mod id, key, entity) for the world edits
local M = { VERSION = 9 }

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

-- ---- quest flows: a quest's steps (objectives wait for events; log entries, rewards and the outcome happen at once) ------------------------
-- The state of a running flow is in risen.save ("modkit.quest.<name>" = { step, entered, n, talked, done, result }): part of every savegame,
-- rolled back by a load, empty in a new game. A step is advanced before its effects run, so a reward is never paid twice.
local MOD_ID = "?"
local flows = {}   -- quest name -> { trigger = {...}, steps = {...} }

local function qkey(q) return "modkit.quest." .. q end
local function qstate(q) return risen.save.get(qkey(q)) end
local function set_qstate(q, s) risen.save.set(qkey(q), s) end
local function status_is(q, status)
    local quest = risen.quest.get(q)
    return quest ~= nil and tostring(quest:get_property("Status")):find(status, 1, true) ~= nil
end

local function give_reward(st)
    local p = risen.entity.player()
    if not p then return end
    if st.xp then risen.script.call("GiveXP", p, p, st.xp) end   -- the game's own script (what a kill pays through)
    if st.gold then risen.item.give(p, "It_Gold", st.gold) end
    for _, it in ipairs(st.items or {}) do risen.item.give(p, it[1], it[2]) end
    if st.var then risen.gamevar.set(st.var, st.var_value or 1) end
end

local function objective_done(st, s)
    if st.objective == "items" then
        local p = risen.entity.player()
        return p ~= nil and risen.item.count(p, st.item) >= st.count
    elseif st.objective == "kill" then
        return (s.n or 0) >= st.count
    elseif st.objective == "talk" then
        return s.talked == true
    elseif st.objective == "var" then
        local v = risen.gamevar.get(st.name)
        return v ~= nil and v >= st.value
    elseif st.objective == "quest" then
        return status_is(st.quest, st.status)
    end
    return false
end

local function advance(q)
    local f, s = flows[q], qstate(q)
    if not f or not s or s.done then return end
    while true do
        local st = f.steps[s.step]
        if not st or st.kind == "stop" then   -- the end of a path without an Outcome
            s.done = true
            set_qstate(q, s)
            return
        end
        if st.kind == "race" then
            -- an Either / or: every path's first objective is waited for at once; the first one done decides the path (the others are dropped)
            if not s.entered then
                s.entered, s.gates = true, {}
                for _, idx in ipairs(st.paths) do s.gates[tostring(idx)] = { n = 0, talked = false } end
                set_qstate(q, s)
                for _, idx in ipairs(st.paths) do
                    local gate = f.steps[idx]
                    if gate and gate.log then risen.quest.add_log(q, gate.log) end
                end
            end
            local taken
            for _, idx in ipairs(st.paths) do
                if objective_done(f.steps[idx], s.gates[tostring(idx)] or {}) then
                    taken = idx
                    break
                end
            end
            if not taken then return end
            risen.emit("modkit.objective", MOD_ID, q, taken)
            s.step, s.entered, s.gates = taken + 1, false, nil   -- on behind the deciding objective
            set_qstate(q, s)
        else
            if st.kind == "objective" then
                if not s.entered then
                    s.entered, s.n, s.talked = true, 0, false
                    set_qstate(q, s)
                    if st.log then risen.quest.add_log(q, st.log) end
                end
                if not objective_done(st, s) then return end
                risen.emit("modkit.objective", MOD_ID, q, s.step)
            end
            s.step, s.entered = s.step + 1, false
            if st.kind == "outcome" then
                s.done, s.result = true, st.result
            end
            set_qstate(q, s)   -- first the state, then the effects
            if st.kind == "log" then
                risen.quest.add_log(q, st.text)
            elseif st.kind == "reward" then
                give_reward(st)
            elseif st.kind == "outcome" then
                if st.log then risen.quest.add_log(q, st.log) end
                if st.result == "success" then risen.quest.succeed(q)
                elseif st.result == "fail" then risen.quest.fail(q)
                else risen.quest.close(q) end
                risen.emit("modkit.quest_done", MOD_ID, q, st.result)
                if st.next_quest then M.start_quest(st.next_quest) end
                return
            end
        end
    end
end

-- starts a quest of the mod (once per savegame when it has a flow)
function M.start_quest(q)
    if flows[q] then
        if qstate(q) then return end
        set_qstate(q, { step = 1 })
    end
    risen.quest.open(q)
    risen.quest.run(q)
    risen.emit("modkit.quest_started", MOD_ID, q)
    advance(q)
end

-- every running flow (what changed while the world was loading is looked at again)
local function each_running(fn)
    for q, f in pairs(flows) do
        local s = qstate(q)
        if s and not s.done and f.steps[s.step] then fn(q, f, s) end
    end
end

-- every objective the running flows wait for: fn(quest, objective step, its state, the flow's state). An objective step keeps its progress in the
-- flow's state, the paths of an Either / or each in their own table (s.gates), all saved with it
local function each_objective(fn)
    each_running(function(q, f, s)
        local st = f.steps[s.step]
        if st.kind == "objective" then
            fn(q, st, s, s)
        elseif st.kind == "race" and s.gates then
            for _, idx in ipairs(st.paths) do
                local gates = s.gates
                if not gates then break end   -- an earlier path just decided: the flow went on and dropped the gates
                local g = gates[tostring(idx)]
                if g then fn(q, f.steps[idx], g, s) end
            end
        end
    end)
end

local function triggers(kind, match)
    for q, f in pairs(flows) do
        if f.trigger and f.trigger.kind == kind and not qstate(q) and match(f.trigger) then M.start_quest(q) end
    end
end

local function install_flow_events()
    risen.on("inventory.changed", function(owner, template, delta)
        if not (owner and owner:is_player()) then return end
        if delta > 0 then triggers("item", function(t) return t.item == template end) end
        each_objective(function(q, st)
            if st.objective == "items" and st.item == template then advance(q) end
        end)
    end)
    risen.on("entity.died", function(entity)
        local name = entity and entity:name()
        local tpl = entity and entity:template()
        local tname = tpl and tpl:name()
        local function hit(target) return target == name or target == tname end
        triggers("kill", function(t) return hit(t.target) end)
        each_objective(function(q, st, state, s)
            if st.objective == "kill" and s.entered and hit(st.target) then
                state.n = (state.n or 0) + 1
                set_qstate(q, s)
                advance(q)
            end
        end)
    end)
    risen.on("info.finished", function(info)
        each_objective(function(q, st, state, s)
            if st.objective == "talk" and s.entered and st.info == info then
                state.talked = true
                set_qstate(q, s)
                advance(q)
            end
        end)
    end)
    risen.on("gamevar.changed", function(name, value)
        triggers("var", function(t) return t.name == name and value ~= nil and value >= (t.value or 1) end)
        each_objective(function(q, st)
            if st.objective == "var" and st.name == name then advance(q) end
        end)
    end)
    risen.on("quest.status_changed", function(quest, status)
        triggers("quest", function(t) return t.quest == quest and tostring(status):find(t.status or "Success", 1, true) ~= nil end)
        each_objective(function(q, st)
            if st.objective == "quest" and st.quest == quest then advance(q) end
        end)
    end)
    risen.on("world.ready", function(mode)
        if mode == "new" then triggers("game_start", function() return true end) end
        each_running(function(q) advance(q) end)   -- what changed while the world was loading
    end)
end

-- what a Quest node of a dialogue does
function M.quest_action(quest, action)
    if action == "start" then
        M.start_quest(quest)
    else
        local s = flows[quest] and qstate(quest)
        if s and not s.done then
            s.done, s.result = true, action
            set_qstate(quest, s)
        end
        if action == "succeed" then
            risen.quest.succeed(quest)
        elseif action == "fail" then
            risen.quest.fail(quest)
        elseif action == "cancel" then
            risen.quest.close(quest)
        end
    end
    risen.emit("modkit.quest_action", MOD_ID, quest, action)
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

-- ---- world edits: once per savegame -----------------------------------------------------------------------------------------------------------
local warned = {}
-- the world an entry belongs to is the active one (risen.world.name; a layer without it, or before the first world: every entry counts)
local function in_world(world)
    local here = risen.world.name and risen.world.name()
    return here == nil or world == nil or world == here
end
M.in_world = in_world

local function apply_world(id, w)
    for _, s in ipairs(w.spawns or {}) do
        if not in_world(s.world) then goto next_spawn end
        local key = "modkit.world.spawn." .. s.key
        local done = risen.save.get(key)
        if s.layer and not (M.layers and M.layers.enabled(s.layer)) then
            -- the layer is off: what it made is taken away again (made anew when it comes back), nothing new is made
            if done then
                if type(done) == "string" then
                    local e = risen.entity.by_id(done)
                    if e then e:kill() end
                end
                risen.save.remove(key)
                risen.save.remove(key .. ".object")
            end
            goto next_spawn
        end
        -- an object (not an item: the player may have picked that up) that is not in the world any more is placed again
        if done and risen.save.get(key .. ".object") and type(done) == "string" and not risen.entity.by_id(done) then
            risen.log.info(("%s: %s (%s) is missing after the load: placed again"):format(id, s.template, done))
            done = nil
        end
        if not done then
            local e = risen.entity.spawn(s.template, s.position[1], s.position[2], s.position[3])
            if e then
                if s.rotation then e:set_pose({ yaw = s.rotation[1], pitch = s.rotation[2], roll = s.rotation[3] }) end
                if s.amount and s.amount > 1 then e:set_item_amount(s.amount) end
                -- spawned objects are not save-relevant by themselves (items and NPCs are): the engine is asked to keep it in the savegame
                if not e:is_item() then
                    e:set_save_relevant(true)
                    risen.save.set(key .. ".object", true)
                end
                risen.save.set(key, e:id() or true)
                risen.emit("modkit.world_spawned", id, s.key, e)
            elseif not warned[key] then
                warned[key] = true
                risen.log.warn(("%s: cannot spawn %s"):format(id, s.template))
            end
        end
        ::next_spawn::
    end
    for _, c in ipairs(w.containers or {}) do
        local key = "modkit.world.container." .. c.key
        if in_world(c.world) and not risen.save.get(key) and not (c.layer and not (M.layers and M.layers.enabled(c.layer))) then
            local ent = risen.entity.by_id(c.id)
            if ent then
                for _, it in ipairs(c.items or {}) do risen.item.give(ent, it[1], it[2]) end
                risen.save.set(key, true)
                risen.emit("modkit.container_filled", id, c.key, ent)
            elseif not warned[key] then
                warned[key] = true
                risen.log.warn(("%s: no entity %s (%s) in this world"):format(id, c.id, tostring(c.name)))
            end
        end
    end
    -- removals and moves of the world's own entities: on every world.ready (idempotent; the engine keeps a removal in the savegame, a move only for
    -- save-relevant entities, so both are simply applied again after a load)
    for _, r in ipairs(w.removals or {}) do
        if not in_world(r.world) then goto next_removals end
        local ent = risen.entity.by_id(r.id)
        if ent then
            ent:kill()
            risen.emit("modkit.world_removed", id, r.key, r.id)
        end
        ::next_removals::
    end
    for _, m in ipairs(w.moves or {}) do
        if not in_world(m.world) then goto next_moves end
        local ent = risen.entity.by_id(m.id)
        if ent then
            local spec = {}
            if m.position then spec.x, spec.y, spec.z = m.position[1], m.position[2], m.position[3] end
            if m.rotation then spec.yaw, spec.pitch, spec.roll = m.rotation[1], m.rotation[2], m.rotation[3] end
            local ok, why = ent:set_pose(spec)
            if ok then
                risen.emit("modkit.world_moved", id, m.key, ent)
            elseif not warned["move." .. m.key] then
                warned["move." .. m.key] = true
                risen.log.warn(("%s: cannot move %s (%s): %s"):format(id, m.id, tostring(m.name), tostring(why)))
            end
        elseif not warned["move." .. m.key] then
            warned["move." .. m.key] = true
            risen.log.warn(("%s: no entity %s (%s) in this world"):format(id, m.id, tostring(m.name)))
        end
        ::next_moves::
    end
end

function M.install(data)
    local id = mod and mod.id or "?"
    MOD_ID = id
    -- layers first: NPC rules and world edits ask them
    local layers
    if nonempty(data.layers) then
        layers = require("modkit.layers")
        local events = require("modkit.events")
        events.set_runtime(M)
        layers.install(data.layers, events)
        M.layers = layers
    end
    local any_flow = false
    for _, q in ipairs(data.quests or {}) do
        if q.flow then
            flows[q.name] = q.flow
            any_flow = true
        end
    end
    if any_flow then install_flow_events() end
    -- icons first: a tick handler registered before risen.balance.install's own runs before it, so new items find their icons
    local icons = (data.assets or {}).icons
    if nonempty(icons) then
        local done = false
        risen.on("tick", function()
            if done then return end
            done = true
            for _, list in ipairs(icons) do
                local ok, why = risen.assets.load_icons(list)
                if not ok then risen.log.error(("%s: icon list %s: %s"):format(id, list, tostring(why))) end
            end
        end)
    end
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
    local w = data.world or {}
    if nonempty(w.spawns) or nonempty(w.containers) or nonempty(w.removals) or nonempty(w.moves) then
        risen.on("world.ready", function() apply_world(id, w) end)
        if layers then
            risen.on("modkit.layer_changed", function()
                if risen.entity.player() then apply_world(id, w) end
            end)
        end
    end
    -- behaviours: the scripts first (risen.behavior.define), then the attachments; an NPC that is not in the world yet gets his when he appears
    local b = data.behaviors or {}
    for _, s in ipairs(b.scripts or {}) do
        local ok, def = pcall(require, s.module)
        if not ok or type(def) ~= "table" then
            risen.log.error(("%s: behaviour %s: %s"):format(id, s.name, ok and "the script returns no table" or tostring(def)))
        else
            local done, why = risen.behavior.define(s.name, def)
            if not done then risen.log.error(("%s: behaviour %s: %s"):format(id, s.name, tostring(why))) end
        end
    end
    for _, a in ipairs(b.npcs or {}) do
        local ok, why = risen.behavior.attach(a.target, a.behavior, a.params)
        if not ok then risen.log.error(("%s: behaviour %s on %s: %s"):format(id, a.behavior, a.target, tostring(why))) end
    end
    for _, a in ipairs(b.templates or {}) do
        local ok, why = risen.behavior.attach_template(a.template, a.behavior, a.params)
        if not ok then risen.log.error(("%s: behaviour %s on template %s: %s"):format(id, a.behavior, a.template, tostring(why))) end
    end
    for _, n in ipairs(data.npcs or {}) do
        local spec = npc_spec(id, n, nil)
        local ok, why
        -- he belongs to one world: nothing of him is made, restored or replaced while another one is active
        spec.world = n.world
        if n.layer and layers then
            -- a member of a layer: made while the layer is on, removed when it goes off (a new body each time it comes back)
            local own = M.cond(n.appear_when)
            local on = layers.cond(n.layer)
            spec.when = own and risen.cond.all({ on, own }) or on
            spec.despawn_when = M.cond(n.leave_when)
            spec.once = false
            ok, why = risen.spawn.when(spec)
        elseif n.appear_when or n.leave_when then
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
