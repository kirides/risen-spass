#pragma once

// The Lua part of conditional spawning (docs/RE.md section 33, docs/LUA_API.md "Conditional spawning"): `risen.cond.*` builds conditions (quest status, items the player holds, game
// variables, an NPC dead or alive, any Lua predicate, combined with all/any/negate); `risen.spawn.when{...}` declares a persistent NPC (risen.npc.persistent, M7) that exists only
// while / after a condition holds and is removed by another one. Everything is pure Lua on top of the events (quest.status_changed, inventory.changed, gamevar.changed,
// entity.died, ...), risen.save and the persistent NPC handle, so the unit tests run it against mocks.
//
//   * Event driven: every condition names the events that can change it; a matching event only marks the rules that use it, the rules are evaluated on the next `tick`
//     (a handler may run inside engine code, e.g. quest.status_changed fires inside gCQuest::SetStatus). Nothing is polled unless a predicate asks for it (`poll`).
//   * Remembered: each rule keeps `{ state, spawns }` in the mod's risen.save (key `spawn_rules`), written next to every savegame and rolled back by a load, so a savegame made before
//     the trigger has the rule waiting and one made after has it fired / gone. After a load (and in a new game) every rule is evaluated once, 40 frames after world.ready
//     (the persistent NPCs are reconciled 30 frames after it), so a savegame whose world already satisfies a condition, or no longer does, is made consistent.
//   * The NPC is a persistent NPC with an `enabled` gate: nothing of it exists while the rule is waiting, however often the world is loaded.
// The same conditions are the vocabulary of dialogues (dialog_cond_lua.h, M9: `risen.dialog.create{ when = ... }` compiles them to engine conditions where the engine has an equivalent and evaluates the rest
// in Lua): `cond.skill` and `cond.guild` exist for them, `cond.test(c, ctx)` takes the dialogue's context ({ npc = the owner's name }) and a predicate then receives (npc, player).
// Arguments: the per-mod `risen` table and the mod id (installed after the persistent NPC chunk).
namespace spass::game {

inline const char* kSpawnWhenChunk = R"LUA(
local api, modid = ...
local log = api.log

local STORE = "spawn_rules"             -- the mod's key in risen.save: { [rule name] = { state = "waiting" | "present" | "gone", spawns = n } }
local SETTLE = 40                       -- frames after world.ready before the rules are evaluated (persistent NPCs reconcile after 30)

local function tr(...) local t = api.trace if t and t.enabled("spawn") then t("spawn", ...) end end
local function say(level, fmt, ...) log[level](("spawn rule: " .. fmt):format(...)) end

local cond = {}
local spawn = {}
api.cond = cond
api.spawn = spawn

-- ---- conditions -------------------------------------------------------------------------------------------------------------------------------
-- A condition is a node { kind, ..., watch = { { event, match } }, poll = frames } with the metatable CondMT; a plain function is a predicate node.
local CondMT = {}
CondMT.__index = CondMT
CondMT.__tostring = function(n) return n.text end

local STATUSES = { Open = true, Running = true, Success = true, Failed = true, Obsolete = true, Cancelled = true, Lost = true, Won = true, None = true }
local STATUS_LIST = "Open, Running, Success, Failed, Obsolete, Cancelled, Lost, Won, None"
local OPS = { ["=="] = function(a, b) return a == b end, ["~="] = function(a, b) return a ~= b end, [">="] = function(a, b) return a >= b end,
              ["<="] = function(a, b) return a <= b end, [">"] = function(a, b) return a > b end, ["<"] = function(a, b) return a < b end }

local function node(kind, text, fields)
    fields.kind, fields.text = kind, text
    return setmetatable(fields, CondMT)
end

local function is_node(c) return type(c) == "table" and getmetatable(c) == CondMT end

local function bad(msg) error(msg, 3) end

local function name_of(x, what)
    if type(x) == "table" and type(x.name) == "string" then x = x.name end
    if type(x) ~= "string" or x == "" then bad(what .. ": expected an entity name (or a persistent / rule handle)") end
    return x
end

-- Turns a function into a predicate node and checks everything else: returns the node, or nil + reason.
local function normalize(c)
    if is_node(c) then return c end
    if type(c) == "function" then
        return node("fn", "predicate", { fn = c, watch = {} })
    end
    return nil, "expected a condition (risen.cond.*) or a function"
end

cond.is_node = is_node
cond.normalize = function(c) return normalize(c) end

local function must(c, what)
    local n, why = normalize(c)
    if not n then bad(what .. ": " .. why) end
    return n
end

-- cond.quest(name [, status]) -> condition: the quest has this status ("Success" by default; a list of statuses accepts any of them; "None" = the quest does not exist)
function cond.quest(name, status)
    if type(name) ~= "string" or name == "" then bad("cond.quest: the quest name must be a string") end
    local set, list = {}, {}
    local given = status == nil and { "Success" } or type(status) == "table" and status or { status }
    if #given == 0 then bad("cond.quest: the status list is empty") end
    for _, s in ipairs(given) do
        if type(s) ~= "string" then bad("cond.quest: a status is a string (" .. STATUS_LIST .. ")") end
        s = s:gsub("^gEQuestStatus_", "")
        if not STATUSES[s] then bad(("cond.quest: unknown status '%s' (valid: %s)"):format(s, STATUS_LIST)) end
        set[s] = true
        list[#list + 1] = s
    end
    return node("quest", ("quest %s is %s"):format(name, table.concat(list, "/")), {
        quest = name, set = set,
        watch = { { event = "quest.status_changed", match = function(q) return q == name end } },
    })
end

-- cond.item(template [, count [, holder]]) -> condition: the holder (the player by default; or an entity name) carries at least `count` (1) of the item
function cond.item(template, count, holder)
    if type(template) ~= "string" or template == "" then bad("cond.item: the item template must be a string") end
    count = count == nil and 1 or count
    if type(count) ~= "number" or count < 1 or count ~= math.floor(count) then bad("cond.item: the count must be an integer >= 1") end
    holder = holder == nil and "player" or name_of(holder, "cond.item")
    local is_player = holder == "player"
    return node("item", ("%s holds >= %d x %s"):format(holder, count, template), {
        template = template, count = count, holder = holder,
        watch = { { event = "inventory.changed", match = function(owner, tpl)
            if tpl ~= template or not owner then return false end
            if is_player then return owner:is_player() end
            return owner:name() == holder
        end } },
    })
end

-- cond.var(name [, value [, op]]) -> condition on the value of a game variable (risen.gamevar; a variable that does not exist counts as 0; for "was set by SetGameEvent" use cond.event):
-- cond.var("X") = it is non-zero, cond.var("X", 3) = it equals 3, cond.var("X", 3, ">=") with op "==", "~=", ">=", "<=", ">" or "<"
function cond.var(name, value, op)
    if type(name) ~= "string" or name == "" then bad("cond.var: the variable name must be a string") end
    if value ~= nil and type(value) ~= "number" then bad("cond.var: the value must be a number") end
    if op ~= nil and not OPS[op] then bad("cond.var: unknown operator '" .. tostring(op) .. "' (== ~= >= <= > <)") end
    local text = value == nil and ("var %s is set"):format(name) or ("var %s %s %s"):format(name, op or "==", tostring(value))
    local lower = name:lower()
    return node("var", text, {
        var = name, value = value, op = op or "==",
        watch = { { event = "gamevar.changed", match = function(n) return type(n) == "string" and n:lower() == lower end } },
    })
end

-- cond.event(name) -> condition: the game variable exists, whatever its value. This is what the dialogue command SetGameEvent makes true (it creates the variable with the value 0;
-- ClearGameEvent removes it again).
function cond.event(name)
    if type(name) ~= "string" or name == "" then bad("cond.event: the event / variable name must be a string") end
    local lower = name:lower()
    return node("event", ("game event %s is set"):format(name), {
        var = name,
        watch = { { event = "gamevar.changed", match = function(n) return type(n) == "string" and n:lower() == lower end } },
    })
end

-- cond.skill(skill, value [, op [, who]]) -> condition: a skill or attribute of the character compares as wanted (risen.balance.skills(): "Strength", "Sword", "Smith", ...; op ">=" by default; who "player" by default,
-- or an entity's unique name; in a dialogue "npc" is the owner of the option). The value is the total (worn items included), as Entity:skill_total. No event announces a change: a spawn rule needs a poll or risen.spawn.check().
function cond.skill(skill, value, op, who)
    if type(skill) ~= "string" or skill == "" then bad("cond.skill: the skill name must be a string (risen.balance.skills())") end
    local engine = api.balance.skill_enum and api.balance.skill_enum(skill)
    if not engine then bad(("cond.skill: unknown skill '%s' (valid: %s)"):format(skill, table.concat(api.balance.skills(), ", "))) end
    if type(value) ~= "number" or value ~= math.floor(value) then bad("cond.skill: the value must be an integer") end
    if op ~= nil and not OPS[op] then bad("cond.skill: unknown operator '" .. tostring(op) .. "' (== ~= >= <= > <)") end
    who = who == nil and "player" or name_of(who, "cond.skill")
    return node("skill", ("%s of %s %s %d"):format(skill, who, op or ">=", value), { skill = skill, engine = engine, value = value, op = op or ">=", who = who, watch = {} })
end

-- cond.guild(guild [, who]) -> condition: the NPC (or the player) belongs to the guild (a name of risen.npc.guilds: "Cit", "Pir", ...; a number; or a list of them: any of them)
function cond.guild(guild, who)
    local given = type(guild) == "table" and guild or { guild }
    if #given == 0 then bad("cond.guild: the guild list is empty") end
    local set, list = {}, {}
    for _, g in ipairs(given) do
        local number = type(g) == "string" and api.npc.guilds[g] or type(g) == "number" and g or nil
        if number == nil then bad(("cond.guild: unknown guild '%s' (valid: a number or a name of risen.npc.guilds)"):format(tostring(g))) end
        set[number] = true
        list[#list + 1] = tostring(g)
    end
    who = who == nil and "player" or name_of(who, "cond.guild")
    return node("guild", ("%s guild is %s"):format(who, table.concat(list, "/")), { set = set, who = who, watch = {} })
end

-- cond.dead(name) / cond.alive(name) -> conditions on an NPC by its unique name (or a persistent / rule handle): dead = a corpse exists (or the persistent NPC is recorded dead),
-- alive = the entity exists and is not dead
function cond.dead(who)
    local name = name_of(who, "cond.dead")
    return node("dead", name .. " is dead", { who = name, watch = {
        { event = "entity.died", match = function(e) return e ~= nil and e:name() == name end },
        { event = "spawn.state" } } })
end
function cond.alive(who)
    local name = name_of(who, "cond.alive")
    return node("alive", name .. " is alive", { who = name, watch = {
        { event = "entity.died", match = function(e) return e ~= nil and e:name() == name end },
        { event = "entity.destroyed", match = function(n) return n == name end },
        { event = "spawn.state" } } })
end

-- cond.fn(predicate [, { on = "event" | { "event", ... }, poll = frames }]) -> condition: any Lua function returning a boolean. `on` names the events after which it is looked at again
-- (also mod events: risen.emit("my_mod.changed")), `poll` re-evaluates it every n frames (no events needed, costs a call each time). Without either it is only looked at after a load
-- and when another condition of the same rule changes, or by risen.spawn.check().
function cond.fn(fn, opts)
    if type(fn) ~= "function" then bad("cond.fn: expected a function") end
    opts = opts or {}
    if type(opts) ~= "table" then bad("cond.fn: options must be a table { on = ..., poll = ... }") end
    local events = type(opts.on) == "string" and { opts.on } or opts.on or {}
    if type(events) ~= "table" then bad("cond.fn: `on` is an event name or a list of them") end
    local watch = {}
    for _, ev in ipairs(events) do
        if type(ev) ~= "string" then bad("cond.fn: `on` is an event name or a list of them") end
        watch[#watch + 1] = { event = ev }
    end
    if opts.poll ~= nil and (type(opts.poll) ~= "number" or opts.poll < 1) then bad("cond.fn: poll is a number of frames >= 1") end
    return node("fn", "predicate", { fn = fn, watch = watch, poll = opts.poll })
end

local function combine(kind, list, what)
    if type(list) ~= "table" or #list == 0 then bad(what .. ": expected a non-empty list of conditions") end
    local items, texts, watch, poll = {}, {}, {}, nil
    for i, c in ipairs(list) do
        local n = must(c, what .. " #" .. i)
        items[i] = n
        texts[i] = n.text
        for _, w in ipairs(n.watch) do watch[#watch + 1] = w end
        if n.poll and (not poll or n.poll < poll) then poll = n.poll end
    end
    return node(kind, kind .. "(" .. table.concat(texts, ", ") .. ")", { items = items, watch = watch, poll = poll })
end

-- cond.all{ c1, c2, ... } / cond.any{ c1, c2, ... } / cond.negate(c)
function cond.all(list) return combine("all", list, "cond.all") end
function cond.any(list) return combine("any", list, "cond.any") end
function cond.negate(c)
    local n = must(c, "cond.negate")
    return node("not", "not " .. n.text, { item = n, watch = n.watch, poll = n.poll })
end

-- The answer of one condition now: true, false, or nil = cannot tell (no player yet, a predicate that raised an error): a rule does nothing then.
-- `cx` is the context of the evaluation in progress (cond.test(c, ctx)): in a dialogue { npc = the owner's unique name, name = the option's name, level = "warn" } - "npc" holders mean that entity, predicates receive (npc, player),
-- and a predicate that raised an error is reported as a warning (the option is simply not offered).
local reported = {}
local cx = nil
local function leaf(n, fn)
    local ok, res = pcall(fn, n)
    if not ok then
        if not reported[n] then
            reported[n] = true
            if cx then log[cx.level or "warn"](("dialogue '%s': condition '%s' raised an error, so the option is not offered: %s"):format(tostring(cx.name or cx.npc), n.text, tostring(res)))
            else say("error", "condition '%s': %s", n.text, tostring(res)) end
        end
        return nil
    end
    return res
end

-- The entity a holder / who refers to: nil = cannot tell (no player, or "npc" outside a dialogue), false = it does not exist
local function resolve(who)
    if who == "player" then return api.entity.player() end
    if who == "npc" then
        local name = cx and cx.npc
        if not name then return nil end
        return api.entity.get(name) or false
    end
    return api.entity.get(who) or false
end

local function quest_status(name)
    local q = api.quest.get(name)
    if not q then return "None" end
    local s = q:get_property("Status")
    if type(s) ~= "string" then return "None" end
    return (s:gsub("^gEQuestStatus_", ""))
end

local EVAL = {}
function EVAL.quest(n) return n.set[quest_status(n.quest)] == true end
function EVAL.item(n)
    local e = resolve(n.holder)
    if e == nil then return nil end   -- no player yet: cannot tell
    if not e then return false end
    return (api.item.count(e, n.template) or 0) >= n.count
end
function EVAL.skill(n)
    local e = resolve(n.who)
    if e == nil then return nil end
    if not e then return false end
    local v = api.item.skill_total(e, n.skill)
    if type(v) ~= "number" then return nil end
    return OPS[n.op](v, n.value)
end
function EVAL.guild(n)
    local e = resolve(n.who)
    if e == nil then return nil end
    if not e then return false end
    local g = e:guild()
    if type(g) ~= "number" then return nil end
    return n.set[g] == true
end
function EVAL.event(n) return api.gamevar.exists(n.var) and true or false end
function EVAL.var(n)
    local v = api.gamevar.get(n.var) or 0
    if n.value == nil then return v ~= 0 end
    return OPS[n.op](v, n.value)
end
local function npc_dead(name)
    local e = api.entity.get(name)
    if e then return e:is_dead() end
    local h = api.npc.persistent_get and api.npc.persistent_get(name)
    return h ~= nil and h:state() == "dead"
end
function EVAL.dead(n) return npc_dead(n.who) end
function EVAL.alive(n)
    local e = api.entity.get(n.who)
    return e ~= nil and not e:is_dead()
end
function EVAL.fn(n)
    local r
    if cx and cx.npc then r = n.fn(api.entity.get(cx.npc) or nil, api.entity.player()) else r = n.fn() end
    if r == nil then return nil end
    return r and true or false
end

local eval
function EVAL.all(n)
    local unknown = false
    for _, c in ipairs(n.items) do
        local r = eval(c)
        if r == false then return false end
        if r == nil then unknown = true end
    end
    if unknown then return nil end
    return true
end
function EVAL.any(n)
    local unknown = false
    for _, c in ipairs(n.items) do
        local r = eval(c)
        if r == true then return true end
        if r == nil then unknown = true end
    end
    if unknown then return nil end
    return false
end
EVAL["not"] = function(n)
    local r = eval(n.item)
    if r == nil then return nil end
    return not r
end

eval = function(n)
    local f = EVAL[n.kind]
    if n.kind == "all" or n.kind == "any" or n.kind == "not" then return f(n) end
    return leaf(n, f)
end

-- cond.test(condition [, ctx]) -> boolean | nil: what the condition says right now (nil = cannot tell, e.g. outside a running game). `ctx` is the dialogue context { npc = name, name = option, level = "warn" } (see `cx`)
function cond.test(c, ctx)
    local n, why = normalize(c)
    if not n then return nil, why end
    local previous = cx
    cx = ctx
    local ok, res = pcall(eval, n)
    cx = previous
    if not ok then error(res, 0) end
    return res
end

-- ---- rules ------------------------------------------------------------------------------------------------------------------------------------
local rules, order = {}, {}              -- name -> rule; names in declaration order
local watchers = {}                      -- event -> list of { rule, match }
local hooked = {}                        -- real events this layer listens to
local ready = false                      -- the world of this session settled: rules are evaluated
local was_ready = false                  -- ready before a savegame load started (restored when the load fails)
local settle = nil                       -- frames left until ready
local frame = 0
local dirty_count = 0
local first = true                       -- the next evaluation of each rule is the first one after a load / new game ("load")
local installed = false

local function records(create)
    local r = api.save.get(STORE)
    if type(r) ~= "table" then
        if not create then return nil end
        r = {}
        api.save.set(STORE, r)
    end
    return r
end

local function state_of(rule)
    local recs = records(false)
    local rec = recs and recs[rule.name]
    return rec and rec.state or "waiting"
end

local function mark(rule)
    if not rule.dirty then rule.dirty = true dirty_count = dirty_count + 1 end
end

local function fire_watchers(event, ...)
    local list = watchers[event]
    if not list then return end
    for _, w in ipairs(list) do
        local rule = w.rule
        if not rule.dirty then
            local ok, hit = true, true
            if w.match then ok, hit = pcall(w.match, ...) end
            if ok and hit then mark(rule) end
        end
    end
end

local function watch_event(rule, event, match)
    local list = watchers[event]
    if not list then list = {} watchers[event] = list end
    list[#list + 1] = { rule = rule, match = match }
    if event ~= "spawn.state" and not hooked[event] then
        hooked[event] = true
        api.on(event, function(...) if ready then fire_watchers(event, ...) end end)
    end
end

local function call(rule, what, ...)
    local f = rule[what]
    if type(f) ~= "function" then return end
    local ok, err = pcall(f, ...)
    if not ok then say("error", "'%s' %s: %s", rule.name, what, tostring(err)) end
end

local function make_present(rule, why)
    local recs = records(true)
    local rec = recs[rule.name] or {}
    recs[rule.name] = rec
    rec.state = "present"
    rec.spawns = (rec.spawns or 0) + 1
    tr("rule '%s' fires (%s): %s", rule.name, why, rule.when.text)
    say("info", "'%s' fires (%s): %s", rule.name, why, rule.when.text)
    local e, reason = rule.npc:ensure()
    if not e and reason then say("warn", "'%s': the NPC could not be created now (%s); it is tried again at the next session start", rule.name, tostring(reason)) end
    call(rule, "on_fire", rule.handle, why, e)
    fire_watchers("spawn.state", rule.name)
end

local function make_gone(rule, why)
    local recs = records(true)
    local rec = recs[rule.name] or {}
    recs[rule.name] = rec
    rec.state = rule.once and "gone" or "waiting"
    tr("rule '%s' despawns (%s) -> %s", rule.name, why, rec.state)
    say("info", "'%s' despawns (%s)", rule.name, why)
    rule.npc:remove()
    call(rule, "on_despawn", rule.handle, why)
    fire_watchers("spawn.state", rule.name)
end

local function evaluate(rule)
    if rule.dirty then rule.dirty = false dirty_count = dirty_count - 1 end
    rule.last = frame
    local why = rule.first and "load" or "condition"
    rule.first = false
    local on = eval(rule.when)
    local off = false
    if rule.despawn_when then off = eval(rule.despawn_when) end
    if on == nil or off == nil then return end
    local state = state_of(rule)
    if rule.once then
        if state == "waiting" and on then
            if off then
                -- both hold at once: it would be created and removed in the same breath
                local recs = records(true)
                recs[rule.name] = recs[rule.name] or {}
                recs[rule.name].state = "gone"
                say("info", "'%s': the spawn and the despawn condition hold together: nothing is created", rule.name)
            else
                make_present(rule, why)
            end
        elseif state == "present" and off then
            make_gone(rule, why)
        end
    else
        local want = on and not off
        if want and state ~= "present" then make_present(rule, why)
        elseif not want and state == "present" then make_gone(rule, why) end
    end
end

local function evaluate_all()
    for _, name in ipairs(order) do
        local rule = rules[name]
        local ok, err = pcall(evaluate, rule)
        if not ok then say("error", "'%s': %s", name, tostring(err)) end
    end
end

local function install_events()
    if installed then return end
    installed = true
    local function stop() ready = false settle = nil end
    api.on("load.before", function() was_ready = was_ready or ready stop() end)
    api.on("session.stop", function() stop() was_ready = false end)
    api.on("session.start", function() stop() was_ready = false end)
    api.on("load.after", function(_, ok)
        if ok == false and was_ready then ready = true end   -- a failed load keeps the running session
        was_ready = false
    end)
    api.on("world.ready", function() settle = SETTLE end)
    api.on("tick", function()
        frame = frame + 1
        if settle then
            settle = settle - 1
            if settle > 0 then return end
            settle = nil
            ready = true
            for _, rule in ipairs(order) do rules[rule].first = true mark(rules[rule]) end
        end
        if not ready then return end
        local polled = false
        for _, name in ipairs(order) do
            local rule = rules[name]
            if rule.poll and frame - (rule.last or 0) >= rule.poll then mark(rule) polled = true end
        end
        if dirty_count <= 0 and not polled then return end
        for _, name in ipairs(order) do
            local rule = rules[name]
            if rule.dirty then
                local ok, err = pcall(evaluate, rule)
                if not ok then say("error", "'%s': %s", name, tostring(err)) end
            end
        end
    end)
end

-- ---- the handle ---------------------------------------------------------------------------------------------------------------------------------
local Handle = {}
Handle.__index = Handle
Handle.__tostring = function(h) return "SpawnRule(" .. h.name .. ")" end

-- handle:state() -> "waiting" | "present" | "gone" | "pending" (the world has not settled yet; after a load the rule is looked at 40 frames after world.ready)
function Handle:state()
    if not ready then return "pending" end
    return state_of(rules[self.name])
end

-- handle:entity() -> Entity | nil: the NPC right now; handle:fired() -> how often the rule created its NPC in this savegame
function Handle:entity() return self.npc:entity() end
function Handle:fired()
    local recs = records(false)
    local rec = recs and recs[self.name]
    return rec and rec.spawns or 0
end

-- handle:check() -> state: evaluate the rule now (use it after something changed that no event announces)
function Handle:check()
    if ready then
        local ok, err = pcall(evaluate, rules[self.name])
        if not ok then say("error", "'%s': %s", self.name, tostring(err)) end
    end
    return self:state()
end

-- handle:reset(): back to "waiting" and the NPC is removed; the rule may fire again (the next evaluation does so at once when the condition still holds)
function Handle:reset()
    local rule = rules[self.name]
    local recs = records(true)
    local rec = recs[self.name] or {}
    recs[self.name] = rec
    rec.state = "waiting"
    self.npc:remove()
    mark(rule)
    fire_watchers("spawn.state", self.name)
end

-- ---- risen.spawn.when ---------------------------------------------------------------------------------------------------------------------------
local RULE_KEYS = { when = true, despawn_when = true, once = true, on_fire = true, on_despawn = true }

-- risen.spawn.when(spec) -> handle | nil, reason: spec = the options of risen.npc.persistent plus when / despawn_when / once / on_fire / on_despawn
function spawn.when(spec)
    local function fail(reason)
        say("error", "%s", reason)
        return nil, reason
    end
    if type(spec) ~= "table" then return fail("risen.spawn.when expects a table { name = ..., template = ..., position = {x, y, z}, when = risen.cond.quest(...) }") end
    local name = spec.name
    if type(name) ~= "string" then return fail("name must be a string (the NPC's unique entity name)") end
    if rules[name] then return rules[name].handle end   -- idempotent, like persistent / define / create
    if spec.enabled ~= nil then return fail(("'%s': `enabled` is managed by risen.spawn.when"):format(name)) end
    local when, why = normalize(spec.when)
    if spec.when == nil then return fail(("'%s': `when` is required (risen.cond.* or a function)"):format(name)) end
    if not when then return fail(("'%s': when: %s"):format(name, why)) end
    local despawn
    if spec.despawn_when ~= nil then
        despawn, why = normalize(spec.despawn_when)
        if not despawn then return fail(("'%s': despawn_when: %s"):format(name, why)) end
    end
    if spec.once ~= nil and type(spec.once) ~= "boolean" then return fail(("'%s': once must be true or false"):format(name)) end
    for _, f in ipairs { "on_fire", "on_despawn" } do
        if spec[f] ~= nil and type(spec[f]) ~= "function" then return fail(("'%s': %s must be a function"):format(name, f)) end
    end

    local rule = { name = name, when = when, despawn_when = despawn, once = spec.once ~= false, dirty = false, first = true,
                   on_fire = spec.on_fire, on_despawn = spec.on_despawn }
    local pspec = {}
    for k, v in pairs(spec) do if not RULE_KEYS[k] then pspec[k] = v end end
    pspec.enabled = function() return state_of(rule) == "present" end
    local npc, reason = api.npc.persistent(pspec)
    if not npc then return nil, reason end

    rule.npc = npc
    rule.handle = setmetatable({ name = name, template = spec.template, npc = npc }, Handle)
    rules[name] = rule
    order[#order + 1] = name
    local poll
    for _, n in ipairs { when, despawn } do
        for _, w in ipairs(n.watch) do watch_event(rule, w.event, w.match) end
        if n.poll and (not poll or n.poll < poll) then poll = n.poll end
    end
    rule.poll = poll
    install_events()
    if ready then mark(rule) end   -- declared late: looked at on the next tick
    return rule.handle
end

-- risen.spawn.get(name) -> handle | nil;  risen.spawn.rules() -> { handle, ... } in declaration order;  risen.spawn.check(): evaluate every rule now
function spawn.get(name)
    local rule = rules[name]
    return rule and rule.handle or nil
end
function spawn.rules()
    local out = {}
    for _, name in ipairs(order) do out[#out + 1] = rules[name].handle end
    return out
end
function spawn.check()
    if ready then evaluate_all() end
end
)LUA";

}  // namespace spass::game
