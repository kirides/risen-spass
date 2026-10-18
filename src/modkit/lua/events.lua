-- modkit/events.lua: the run-time part of the event graphs of a mod exported by risen-modkit (docs/editor/REQUIREMENTS.md sections 3.11, 7.5).
-- Written only for projects that have event graphs; the graphs are plain data in data/events.lua. Do not edit: the next export replaces it
-- (hand-written additions go into custom.lua; `require("modkit.events")` gives this module: set_enabled, fire_signal, var_get, var_set).
--
-- A graph is { name, key, enabled, fallback, nodes = { { id, type, <fields>, outs = { pin = { id, ... } } } } }. A trigger node ("on_*") starts a run when its
-- engine event happens; the run walks the outs of every node it executes (several targets: all of them, in the listed order) and ends where a node has no
-- out or lets nothing pass. State that must survive a savegame lives in risen.save:
--   modkit.var.<name>            a "save" variable (a number)
--   modkit.ev.<graph>.<node>     Once / count / cooldown state of a node
--   modkit.ev.timer.<graph>.<node>   { due = clock } or { done = true } of an On timer node
--   modkit.ev.pending            delays that have not run out: { { at, g, from, ctx } }
--   modkit.ev.clock              seconds of play (advances only while the world runs)
--   modkit.ev.chapter            the chapter last seen
-- so a quick load rolls all of it back together with the world: nothing fires twice and nothing is lost.
--
-- Mod events: "modkit.event" (mod id, graph name, trigger node id, kind) when a trigger starts a run; "modkit.event_node" (mod id, graph name, node id, type)
-- for every node a run executes.
local E = { VERSION = 1 }

local unpack = table.unpack or unpack
local STEP_BUDGET = 400        -- nodes one run may execute (a loop with a Delay is the only way to go on)
local RUN_BUDGET = 300         -- runs per frame (variables that set each other would never stop otherwise)
local MOVE_SAMPLE_UNITS = 100  -- 1 m: the smallest movement of the player that is looked at (units are centimetres)

-- ---- pure helpers (tested without the game) -------------------------------------------------------------------------------------------------------

-- The movement filter. `state` = {} to begin with; true when (x, y, z) is at least `min_units` away from the anchor (the position of the last sample),
-- which becomes the new anchor. The first position only sets the anchor. Turning the player never changes a position, so rotation alone never gives a sample.
function E.movement_step(state, x, y, z, min_units)
    if type(x) ~= "number" or type(y) ~= "number" or type(z) ~= "number" or x ~= x or y ~= y or z ~= z then return false end
    min_units = math.max(min_units or MOVE_SAMPLE_UNITS, MOVE_SAMPLE_UNITS)
    if not state.ax then
        state.ax, state.ay, state.az = x, y, z
        return false
    end
    local dx, dy, dz = x - state.ax, y - state.ay, z - state.az
    if dx * dx + dy * dy + dz * dz < min_units * min_units then return false end
    state.ax, state.ay, state.az = x, y, z
    return true
end

function E.in_sphere(x, y, z, cx, cy, cz, radius_units)
    local dx, dy, dz = x - cx, y - cy, z - cz
    return dx * dx + dy * dy + dz * dz <= radius_units * radius_units
end

-- the hour is in [from, to): 22..6 spans midnight, from == to is always
function E.in_hours(hour, from, to)
    if from == to then return true end
    if from < to then return hour >= from and hour < to end
    return hour >= from or hour < to
end

function E.compare(a, op, b)
    a, b = a or 0, b or 0
    if op == "==" then return a == b
    elseif op == "~=" then return a ~= b
    elseif op == ">=" then return a >= b
    elseif op == "<=" then return a <= b
    elseif op == ">" then return a > b
    elseif op == "<" then return a < b end
    return false
end

-- text per language: the active one, else the mod's first, else any
function E.pick_text(t, lang, fallback)
    if type(t) == "string" then return t end
    if type(t) ~= "table" then return "" end
    local s = t[lang] or t[fallback]
    if s and s ~= "" then return s end
    local keys = {}
    for k, v in pairs(t) do if type(v) == "string" and v ~= "" then keys[#keys + 1] = k end end
    table.sort(keys)
    return keys[1] and t[keys[1]] or ""
end

-- {key} from the run's context, {var:name} a variable (a save variable first, else a game variable)
function E.format(text, ctx, getvar)
    return (text:gsub("{([%w_:%.%-]+)}", function(key)
        local name = key:match("^var:(.+)$")
        if name then return tostring(getvar and getvar(name) or 0) end
        local v = ctx and ctx[key]
        if v == nil then return "{" .. key .. "}" end
        return tostring(v)
    end))
end

-- ---- state -----------------------------------------------------------------------------------------------------------------------------------------

local MOD_ID = "?"
local rt                 -- modkit.runtime (conditions as risen.cond objects, quest actions)
local glist = {}         -- the graphs in order
local gkey = {}          -- key -> graph
local used = {}          -- trigger kind -> true (any enabled or disabled graph uses it)
local clock, clock_saved = 0, 0
local last_t
local pending = {}
local queue = {}         -- { kind, ctx } raised by actions (signals, save variables) and run after the current run
local runs_frame = 0
local warned = {}
local frame = 0
local chapter_seen
local sampler = {}       -- the area triggers' own movement filter

local function warn_once(key, msg)
    if warned[key] then return end
    warned[key] = true
    risen.log.warn(("%s: events: %s"):format(MOD_ID, msg))
end

local function player() return risen.entity.player() end

local function save_get(key, default)
    local v = risen.save.get(key)
    if v == nil then return default end
    return v
end

-- ---- variables ---------------------------------------------------------------------------------------------------------------------------------------

local function var_get(scope, name)
    if scope == "save" then return save_get("modkit.var." .. name, 0) end
    return risen.gamevar.get(name) or 0
end

local function any_var(name)
    local v = risen.save.get("modkit.var." .. name)
    if v ~= nil then return v end
    return risen.gamevar.get(name) or 0
end
E.var_get = var_get

local raise  -- defined with the dispatcher

local function var_set(scope, name, value)
    if scope == "save" then
        local previous = risen.save.get("modkit.var." .. name)
        if value == nil then risen.save.remove("modkit.var." .. name) else risen.save.set("modkit.var." .. name, value) end
        raise("variable", { name = name, value = value, previous = previous, scope = "save" })
    else
        if value == nil then risen.gamevar.remove(name) else risen.gamevar.set(name, math.floor(value)) end  -- gamevar.changed follows
    end
end
E.var_set = var_set

-- ---- conditions ------------------------------------------------------------------------------------------------------------------------------------

local function cond_test(d)
    local c = rt.cond(d)
    local ok = risen.cond.test(c)
    return ok == true
end

local function position_of(t)
    if t.at == "entity" then
        local e = risen.entity.get(t.entity or "")
        if not e then return nil end
        return e:position()
    end
    return t.x or 0, t.y or 0, t.z or 0
end

local function check(t)
    local k = t.kind
    if k == "var" then return E.compare(var_get(t.scope or "game", t.name), t.op or "==", t.value)
    elseif k == "quest" then return cond_test({ "quest", t.quest, t.status or "Running" })
    elseif k == "item" then return cond_test({ "item", t.item, t.count or 1 })
    elseif k == "chapter" then return E.compare(risen.quest.chapter() or 0, t.op or "==", t.value)
    elseif k == "level" then
        local p = player()
        return p ~= nil and E.compare(p:level() or 0, t.op or ">=", t.value)
    elseif k == "skill" then return cond_test({ "skill", t.skill, t.value or 1, t.op or ">=" })
    elseif k == "alive" or k == "dead" then return cond_test({ k, t.who })
    elseif k == "guild" then return cond_test({ "guild", t.guild })
    elseif k == "event" then return cond_test({ "event", t.name })
    elseif k == "time" then
        local h = risen.world.time()
        return h ~= nil and E.in_hours(h, t.from or 0, t.to or 0)
    elseif k == "area" then
        local p = player()
        if not p then return false end
        local px, py, pz = p:position()
        local cx, cy, cz = position_of(t)
        if not (px and cx) then return false end
        return E.in_sphere(px, py, pz, cx, cy, cz, (t.radius or 5) * 100)
    elseif k == "layer" then
        local on = rt.layers and rt.layers.enabled(t.layer) or false
        return on == ((t.state or "enabled") == "enabled")
    elseif k == "sector" then
        local on = risen.world.sector_enabled(t.sector)
        if on == nil then return false end
        return on == ((t.state or "enabled") == "enabled")
    elseif k == "chance" then return math.random(100) <= (t.percent or 50)
    end
    error("unknown check " .. tostring(k))
end

local function condition_holds(node)
    local any = node.mode == "any"
    local result = not any
    for _, t in ipairs(node.tests or {}) do
        local ok, v = pcall(check, t)
        if not ok then
            warn_once("check." .. tostring(t.kind) .. tostring(node.id), ("a '%s' check failed: %s"):format(tostring(t.kind), tostring(v)))
            v = false
        end
        if any then
            if v then result = true break end
        elseif not v then
            result = false
            break
        end
    end
    if node.negate then result = not result end
    return result
end
E.condition_holds = condition_holds

-- ---- the interpreter -------------------------------------------------------------------------------------------------------------------------------

local step  -- (g, id, ctx, budget)

local function follow(g, node, pin, ctx, budget)
    for _, id in ipairs((node.outs or {})[pin] or {}) do step(g, id, ctx, budget) end
end

local function flat(ctx)
    local out = {}
    for k, v in pairs(ctx or {}) do
        local t = type(v)
        if t == "string" or t == "number" or t == "boolean" then out[k] = v end
    end
    return out
end

local function save_pending() risen.save.set("modkit.ev.pending", pending) end

local function text_of(g, t, ctx)
    return E.format(E.pick_text(t, risen.loc.language(), g.fallback), ctx, any_var)
end

local function pos_of(node, ctx)
    if node.at == "entity" then
        local e = risen.entity.get(node.entity or "")
        if e then return e:position() end
        return nil
    end
    return node.x or 0, node.y or 0, node.z or 0
end

local actions = {}

function actions.set_variable(g, n)
    local scope, name = n.scope or "game", n.name
    if n.op == "clear" then var_set(scope, name, nil)
    elseif n.op == "add" then var_set(scope, name, var_get(scope, name) + (n.value or 0))
    else var_set(scope, name, n.value or 0) end
end

function actions.spawn_npc(g, n)
    local p = player()
    if not p then return end
    local name = n.name ~= "" and n.name or nil
    if name and risen.entity.get(name) then return end   -- the named NPC is already there: no second one
    local x, y, z
    if n.at == "near_player" then
        local px, py, pz = p:position()
        if not px then return end
        x, y, z = px + (n.x or 0), py + (n.y or 0), pz + (n.z or 0)
    else
        x, y, z = pos_of(n)
    end
    if not x then return end
    ---@cast x number
    ---@cast y number
    ---@cast z number
    local e = risen.npc.spawn(n.template, x, y, z, { name = name, peaceful = n.peaceful or nil })
    if not e then warn_once("spawn." .. n.template, "cannot spawn " .. tostring(n.template)) end
end

function actions.remove_npc(g, n)
    local e = risen.entity.get(n.target)
    if e then
        e:kill()
        return
    end
    local p = player()
    if not p then return end
    for _, npc in ipairs(risen.entity.npcs(p, (n.radius or 50) * 100)) do
        local tpl = npc:template()
        if not npc:is_player() and tpl and tpl:name() == n.target then npc:kill() end
    end
end

function actions.give_item(g, n)
    local p = player()
    if not p then return end
    risen.item.give(p, n.item, n.count or 1)
    if n.show ~= false then risen.hud.item_log(n.item, n.count or 1) end
end

function actions.take_item(g, n)
    local p = player()
    if p then risen.item.take(p, n.item, n.count or 1) end
end

function actions.give_xp(g, n)
    local p = player()
    if p then risen.script.call("GiveXP", p, p, n.amount or 0) end
end

function actions.quest_action(g, n) rt.quest_action(n.quest, n.action) end
function actions.quest_log(g, n, ctx) risen.quest.add_log(n.quest, text_of(g, n.text, ctx)) end

function actions.hud_message(g, n, ctx)
    local style = ({ log = 0, note = 1, experience = 2 })[n.style or "note"] or 1
    risen.hud.log(text_of(g, n.text, ctx), style)
end

function actions.play_sound(g, n)
    local p = player()
    if p then risen.voice.say(p, n.sound) end
end

function actions.teleport(g, n)
    local p = player()
    if not p then return end
    local x, y, z = pos_of(n)
    if x then p:set_pose({ x = x, y = y, z = z }) end
end

function actions.layer(g, n)
    local layers = rt.layers
    if not layers then warn_once("layers", "a Layer node but no layers in this mod") return end
    if n.action == "toggle" then layers.toggle(n.layer) else layers.set(n.layer, n.action ~= "disable") end
end

function actions.sector(g, n)
    local ok, why = risen.world.set_sector(n.sector, n.action ~= "disable")
    if not ok then warn_once("sector." .. tostring(n.sector), ("cannot switch the sector %s: %s"):format(tostring(n.sector), tostring(why))) end
end

function actions.set_chapter(g, n) risen.quest.set_chapter(n.chapter, true) end
function actions.set_time(g, n) risen.world.set_time(n.hour or 12, n.minute or 0) end

function actions.heal(g, n)
    local p = player()
    if not p then return end
    if (n.hp or 0) ~= 0 then p:add_hp(n.hp) end
    if (n.mana or 0) ~= 0 then p:add_mana(n.mana) end
end

function actions.run_script(g, n)
    local p = player()
    if p then risen.script.call(n.script, p, p, n.arg or 0) end
end

function actions.send_signal(g, n) raise("signal", { name = n.name }) end
function actions.log(g, n, ctx) risen.log.info(("%s: %s"):format(MOD_ID, E.format(n.text or "", ctx, any_var))) end

local function nkey(g, n) return "modkit.ev." .. g.key .. "." .. n.id end

step = function(g, id, ctx, budget)
    local n = g.index[id]
    if not n then return end
    budget.n = budget.n + 1
    if budget.n > STEP_BUDGET then
        warn_once("budget." .. g.key, ("graph '%s' ran more than %d nodes in one go (a loop?): stopped"):format(g.name, STEP_BUDGET))
        return
    end
    risen.emit("modkit.event_node", MOD_ID, g.name, id, n.type)
    local t = n.type
    if t == "condition" then
        follow(g, n, condition_holds(n) and "then" or "else", ctx, budget)
    elseif t == "once" then
        local key = nkey(g, n)
        if risen.save.get(key) then follow(g, n, "again", ctx, budget)
        else
            risen.save.set(key, true)   -- first the state, then the effects
            follow(g, n, "next", ctx, budget)
        end
    elseif t == "count" then
        local key = nkey(g, n)
        local c = save_get(key, 0) + 1
        risen.save.set(key, c)
        local pass = c == n.n
        if n["repeat"] then pass = c % n.n == 0 end
        if pass then follow(g, n, "next", ctx, budget) end
    elseif t == "cooldown" then
        local key = nkey(g, n)
        local last = risen.save.get(key)
        if last and clock - last < (n.seconds or 0) then return end
        risen.save.set(key, clock)
        follow(g, n, "next", ctx, budget)
    elseif t == "delay" then
        if (n.seconds or 0) <= 0 then follow(g, n, "next", ctx, budget) return end
        pending[#pending + 1] = { at = clock + n.seconds, g = g.key, from = id, ctx = flat(ctx) }
        save_pending()
    elseif t == "repeat" then
        for _ = 1, math.min(n.count or 1, 50) do follow(g, n, "body", ctx, budget) end
        follow(g, n, "done", ctx, budget)
    elseif t == "sequence" then
        for _, pin in ipairs({ "1", "2", "3", "4" }) do follow(g, n, pin, ctx, budget) end
    elseif t == "end" or t == "comment" then
        return
    else
        local fn = actions[t]
        if not fn then warn_once("type." .. tostring(t), "unknown node type " .. tostring(t)) return end
        local ok, why = pcall(fn, g, n, ctx)
        if not ok then
            warn_once("node." .. g.key .. id, ("graph '%s': %s failed: %s"):format(g.name, t, tostring(why)))
        end
        follow(g, n, "next", ctx, budget)
    end
end

local function start(g, node, ctx, kind)
    runs_frame = runs_frame + 1
    if runs_frame > RUN_BUDGET then
        warn_once("runs", ("more than %d event runs in one frame (events that trigger each other?): the rest is dropped"):format(RUN_BUDGET))
        return
    end
    risen.emit("modkit.event", MOD_ID, g.name, node.id, kind)
    follow(g, node, "next", ctx, { n = 0 })
end

-- starts every trigger of this kind whose own test passes
local function fire(kind, ctx, match)
    for _, g in ipairs(glist) do
        if g.enabled then
            for _, id in ipairs(g.starts[kind] or {}) do
                local node = g.index[id]
                local ok, hit = true, true
                if match then ok, hit = pcall(match, node, ctx) end
                if ok and hit then start(g, node, ctx, kind) end
            end
        end
    end
end

-- raised from inside a run: after the current event (one frame, in order), never inside it
raise = function(kind, ctx)
    if used[kind] then queue[#queue + 1] = { kind, ctx } end
end

local function flush_queue()
    local guard = 0
    while #queue > 0 and guard < RUN_BUDGET do
        guard = guard + 1
        local item = table.remove(queue, 1)
        local kind, ctx = item[1], item[2]
        if kind == "signal" then
            fire("signal", ctx, function(n, c) return n.name == c.name end)
        elseif kind == "variable" then
            fire("variable", ctx, function(n, c)
                if (n.scope or "game") ~= c.scope or n.name ~= c.name then return false end
                if (n.op or "changed") == "changed" then return true end
                return c.value ~= nil and E.compare(c.value, n.op, n.value)
            end)
        end
    end
end

-- ---- triggers --------------------------------------------------------------------------------------------------------------------------------------

local function reset_motion()
    sampler = {}
    for _, g in ipairs(glist) do g.motion, g.inside = {}, {} end
end

local function resync()
    clock = save_get("modkit.ev.clock", 0)
    clock_saved = clock
    pending = save_get("modkit.ev.pending", {})
    last_t = nil
end

local function tick_pending()
    local i = 1
    while i <= #pending do
        local p = pending[i]
        if clock >= p.at then
            table.remove(pending, i)
            save_pending()   -- first the state, then the effects
            local g = gkey[p.g]
            local node = g and g.index[p.from]
            if g and node and g.enabled then follow(g, node, "next", p.ctx or {}, { n = 0 }) end
        else
            i = i + 1
        end
    end
end

local function tick_timers()
    for _, g in ipairs(glist) do
        for _, id in ipairs(g.starts.timer or {}) do
            local node = g.index[id]
            local key = "modkit.ev.timer." .. g.key .. "." .. id
            local st = risen.save.get(key)
            if not st then
                risen.save.set(key, { due = clock + (node.seconds or 1) })
            elseif not st.done and clock >= st.due then
                if node["repeat"] then st.due = clock + (node.seconds or 1) else st.done = true end
                risen.save.set(key, st)
                if g.enabled then start(g, node, { seconds = node.seconds }, "timer") end
            end
        end
    end
end

local function tick_motion()
    local p = player()
    if not p then return end
    local x, y, z = p:position()
    if not x then return end
    for _, g in ipairs(glist) do
        if g.enabled then
            for _, id in ipairs(g.starts.move or {}) do
                local node = g.index[id]
                local st = g.motion[id]
                if not st then st = {} g.motion[id] = st end
                if E.movement_step(st, x, y, z, (node.min_m or 1) * 100) then
                    start(g, node, { x = x, y = y, z = z }, "move")
                end
            end
        end
    end
    local areas = false
    for _, g in ipairs(glist) do if #(g.starts.area or {}) > 0 then areas = true end end
    if not areas then return end
    local first = sampler.ax == nil
    if not (E.movement_step(sampler, x, y, z, MOVE_SAMPLE_UNITS) or first) then return end
    for _, g in ipairs(glist) do
        for _, id in ipairs(g.starts.area or {}) do
            local node = g.index[id]
            local cx, cy, cz = position_of(node)
            if cx then
                local inside = E.in_sphere(x, y, z, cx, cy, cz, (node.radius or 5) * 100)
                local was = g.inside[id]
                g.inside[id] = inside
                if was ~= nil and was ~= inside and g.enabled then
                    local want = node.on or "enters"
                    local dir = inside and "enters" or "leaves"
                    if want == dir or want == "enters or leaves" then start(g, node, { x = x, y = y, z = z, direction = dir }, "area") end
                end
            end
        end
    end
end

local function tick_slow()
    if used.chapter then
        local c = risen.quest.chapter()
        if c then
            local seen = risen.save.get("modkit.ev.chapter")
            if seen == nil then
                risen.save.set("modkit.ev.chapter", c)
            elseif seen ~= c then
                risen.save.set("modkit.ev.chapter", c)
                fire("chapter", { chapter = c, previous = seen }, function(n) return (n.chapter or 0) == 0 or n.chapter == c end)
            end
        end
    end
    if used.time then
        local h, m = risen.world.time()
        m = tonumber(m)
        if h and m then
            local key = h * 60 + m
            if key ~= E._last_minute then
                E._last_minute = key
                fire("time", { hour = h, minute = m }, function(n) return n.hour == h and (n.minute or 0) == m end)
            end
        end
    end
end

local function on_tick()
    frame = frame + 1
    runs_frame = 0
    local t = risen.clock()
    if last_t then clock = clock + math.min(math.max(t - last_t, 0), 0.25) end
    last_t = t
    if clock - clock_saved >= 0.5 then
        clock_saved = clock
        risen.save.set("modkit.ev.clock", clock)
    end
    if #pending > 0 then tick_pending() end
    if used.timer then tick_timers() end
    if used.move or used.area then tick_motion() end
    if frame % 30 == 0 then tick_slow() end
    flush_queue()
end

local function install_events()
    risen.on("session.start", function() resync() reset_motion() end)
    risen.on("load.after", function() resync() reset_motion() end)
    risen.on("world.ready", function(mode)
        resync()
        reset_motion()
        local c = risen.quest.chapter()
        if c and risen.save.get("modkit.ev.chapter") == nil then risen.save.set("modkit.ev.chapter", c) end
        if used.session then
            fire("session", { mode = mode }, function(n) return n.mode == "any" or n.mode == mode end)
        end
        flush_queue()
    end)
    risen.on("tick", on_tick)
    if used.kill then
        risen.on("entity.died", function(entity, killer)
            if not entity then return end
            local name = entity:name()
            local tpl = entity:template()
            local tname = tpl and tpl:name()
            fire("kill", { target = name ~= "" and name or tname, template = tname, name = name }, function(n)
                if n.target ~= name and n.target ~= tname then return false end
                if n.by_player and not (killer and killer:is_player()) then return false end
                return true
            end)
            flush_queue()
        end)
    end
    if used.quest then
        risen.on("quest.status_changed", function(quest, status)
            fire("quest", { quest = quest, status = status }, function(n)
                if (n.quest or "") ~= "" and n.quest ~= quest then return false end
                local want = n.status or "any"
                return want == "any" or tostring(status):find(want, 1, true) ~= nil
            end)
            flush_queue()
        end)
    end
    if used.item then
        local function item_handler(what, ev, amount_of)
            risen.on(ev, function(owner, template, a, b, c)
                local who = owner --[[@as Entity?]]
                if not (who and who:is_player()) then return end
                if what == "dropped" and c ~= "drop" then return end
                fire("item", { item = template, amount = amount_of(a), what = what }, function(n)
                    return n.what == what and ((n.item or "") == "" or n.item == template)
                end)
                flush_queue()
            end)
        end
        local seen = {}
        for _, g in ipairs(glist) do
            for _, id in ipairs(g.starts.item or {}) do
                local w = g.index[id].what or "picked_up"
                if not seen[w] then
                    seen[w] = true
                    if w == "picked_up" then item_handler(w, "item.picked_up", function(a) return a end)
                    elseif w == "dropped" then item_handler(w, "inventory.removed", function(a) return a end)
                    elseif w == "equipped" then item_handler(w, "item.equipped", function() return 1 end)
                    elseif w == "unequipped" then item_handler(w, "item.unequipped", function() return 1 end)
                    elseif w == "added" then item_handler(w, "inventory.added", function(a) return a end)
                    elseif w == "removed" then item_handler(w, "inventory.removed", function(a) return a end)
                    elseif w == "consumed" then item_handler(w, "item.consumed", function(a) return a end)
                    end
                end
            end
        end
    end
    if used.dialogue then
        risen.on("info.finished", function(info)
            fire("dialogue", { info = info }, function(n) return n.info == info end)
            flush_queue()
        end)
    end
    if used.variable then
        risen.on("gamevar.changed", function(name, value, previous)
            fire("variable", { name = name, value = value, previous = previous, scope = "game" }, function(n)
                if (n.scope or "game") ~= "game" or n.name ~= name then return false end
                if (n.op or "changed") == "changed" then return true end
                return value ~= nil and E.compare(value, n.op, n.value)
            end)
            flush_queue()
        end)
    end
end

function E.set_enabled(name, on)
    for _, g in ipairs(glist) do
        if g.name == name then g.enabled = on and true or false end
    end
end

function E.fire_signal(name)
    raise("signal", { name = name })
end

function E.set_runtime(runtime) rt = runtime end

function E.install(data, runtime)
    rt = runtime
    MOD_ID = mod and mod.id or "?"
    for _, g in ipairs(data or {}) do
        g.index, g.starts, g.motion, g.inside = {}, {}, {}, {}
        for _, n in ipairs(g.nodes or {}) do
            g.index[n.id] = n
            local kind = n.type:match("^on_(.+)$")
            if kind then
                g.starts[kind] = g.starts[kind] or {}
                table.insert(g.starts[kind], n.id)
                used[kind] = true
            end
        end
        glist[#glist + 1] = g
        gkey[g.key] = g
    end
    if #glist > 0 then install_events() end
end

return E
