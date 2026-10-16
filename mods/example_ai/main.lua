-- EXAMPLE 15: AI states -- a companion who follows the player, a sentry who warns him, and a coward who runs away from an enemy he notices or from whoever hit him.
--
-- What you learn
--   * `risen.ai.state(name, { begin, loop, finish, on, task, interval })` defines a custom AI state. The engine's AI runs it like one of its own `ZS_*` states: `begin` once,
--     `loop` every `interval` seconds while the NPC is in it, `finish` when it ended (for whatever reason: `ctx.reason`). `ctx.data` is the state's scratch table,
--     `ctx.args` what `risen.ai.start` was given, `ctx.self` the NPC. Walking is the ordinary `npc:goto_pos / goto_entity / stop_goto / turn_to`; the loop keeps running while he walks.
--   * `task = "OnRoutine"` keeps the engine's perception sweep going (about once a second it runs the `Assess*` scripts for everything around the NPC). The state's `on` table hooks
--     those scripts: `notice` is "I perceive somebody" (`ctx.other`), returning nil lets the game's own reaction run too.
--   * `risen.ai.perceive("damage", fn, { all = true })` is a perception handler that is not tied to a state: here it makes the coward run when he is hit. Returning true says "handled":
--     the game's own reaction (fighting back) does not run. Starting a state from inside a handler is deferred to the end of the frame (the engine code that called the handler goes on
--     and may set a state of its own).
--   * A Lua state is **not** part of a savegame: after a load the engine puts every NPC back at his routine. `{ persist = true }` makes the layer remember the state in the mod's
--     `risen.save` and start it again after each load (`ctx.resumed` is then true). The arguments of a persisted state have to be plain data.
--   * `risen.npc.persistent`'s `on_ready(npc, reason)` is the place to start a state for a new NPC (reason "spawned"; a restored NPC is started by `persist`).
--
-- Run the live check (about a minute and a half; see docs/EXAMPLES.md):
--   .\scripts\test-run.ps1 -Instance 2 -Mods example_ai -WaitForResult -Seconds 300 -Scenario example_ai -ShowChecks

local stats = { warnings = 0 }       -- what the scenario looks at

-- ---- 1. the companion: stays near the player -----------------------------------------------------------------------------------------------------------------
risen.ai.state("ZS_EX_Follow", {
    interval = 0.5,                  -- look twice a second
    task = "OnRoutine",              -- keep the stock perception (he still sees danger and the game's own reactions may end the state)
    loop = function(ctx)
        local player = risen.entity.player()
        if not player then return end
        local d = ctx.self:distance_to(player)
        if d > 420 then
            ctx.data.waiting = nil
            if ctx.time - (ctx.data.walked or -10) >= 2 then      -- the target moves: send him again every two seconds
                ctx.data.walked = ctx.time
                ctx.self:goto_entity(player, 0)
            end
        elseif d < 280 and not ctx.data.waiting then
            ctx.data.waiting = true                               -- close enough: stand, face the player
            ctx.self:stop_goto()
            ctx.self:turn_to(player)
        elseif d >= 280 then
            ctx.data.waiting = nil
        end
    end,
})

-- ---- 2. the sentry: warns the player once when he comes close ---------------------------------------------------------------------------------------------------
risen.ai.state("ZS_EX_Sentry", {
    interval = 0.5,
    task = "OnRoutine",
    begin = function(ctx)
        ctx.data.home = { ctx.self:position() }
    end,
    loop = function(ctx)
        local player = risen.entity.player()
        if not player then return end
        local d = ctx.self:distance_to(player)
        if d < 450 then ctx.self:turn_to(player) end              -- keeps an eye on him
        if d > 700 then ctx.data.warned = false end               -- he may warn again when the player comes back
        local home = ctx.data.home
        if home and ctx.data.walking == nil then
            local x, _, z = ctx.self:position()
            if math.sqrt((x - home[1]) ^ 2 + (z - home[3]) ^ 2) > 200 then   -- pushed off his post (or the player took him along): walk back
                ctx.data.walking = true
                ctx.self:goto_pos(home[1], home[2], home[3], 0)
            end
        elseif ctx.data.walking and ctx.time - (ctx.data.walk_checked or 0) > 3 then
            ctx.data.walking, ctx.data.walk_checked = nil, ctx.time
        end
    end,
    on = {
        -- called about once a second for every entity the sentry perceives, the player included
        notice = function(ctx)
            if ctx.other and ctx.other:is_player() and not ctx.data.warned and ctx.self:distance_to(ctx.other) < 450 then
                ctx.data.warned = true
                stats.warnings = stats.warnings + 1
                risen.hud.log("Sentry: Halt! This is as far as you go.")
            end
            -- no return value: the game's reaction to the player (a guard may comment, an NPC may get nervous) goes on
        end,
    },
})

-- ---- 3. the coward: runs away from whoever hit him ----------------------------------------------------------------------------------------------------------------
risen.ai.state("ZS_EX_Flee", {
    interval = 0.5,
    begin = function(ctx)
        local x, y, z = ctx.self:position()
        x, y, z = x or 0, y or 0, z or 0
        local dx, dz = -1, 0                                      -- no attacker known: run west
        local from = ctx.args and ctx.args.from
        if from and from:valid() then
            local fx, _, fz = from:position()
            local len = math.sqrt((x - fx) ^ 2 + (z - fz) ^ 2)
            if len > 1 then dx, dz = (x - fx) / len, (z - fz) / len end
        end
        stats.fled_from = from and from:name()
        ctx.self:goto_pos(x + dx * 450, y, z + dz * 450, 0)
    end,
    loop = function(ctx)
        if ctx.time > 10 then return "done" end                   -- calms down; "done" gives him back to his routine
    end,
})

-- The coward's reactions. A hit: `all = true` asks the handler for every NPC that is hit (a rare event) and it picks its man by name. Seeing an enemy: AssessEnemy runs for every pair of
-- entities every NPC perceives, so that handler is registered for the coward alone (below, in on_ready). The game's own AssessEnemy runs first (`ctx:call_original()`): when it found
-- an enemy it has put the coward into a fight, and the flee state, started at the end of the frame, replaces that. Which of the two comes first (the wolf is noticed or bites) is a race.
local function flee_from(ctx)
    local current = risen.ai.current(ctx.self)
    if current and current.state == "ZS_EX_Flee" then return end         -- already running
    risen.ai.start(ctx.self, "ZS_EX_Flee", { from = ctx.other })         -- deferred to the end of the frame (the game sets a state of its own right after the handler)
end

risen.ai.perceive("damage", function(ctx)
    if ctx.self:name() ~= "EX_Coward" then return nil end         -- everybody else reacts as the game wants
    flee_from(ctx)
    return true                                                   -- handled: the game's own reaction (fighting back) does not run
end, { all = true })

local watching_enemies = false
local function watch_enemies(coward)
    if watching_enemies then return end
    watching_enemies = true                                       -- (the handle finds the NPC by name again after a load)
    risen.ai.perceive("enemy", function(ctx)
        local enemy = ctx:call_original()
        if enemy ~= 0 and ctx.other and not ctx.other:is_player() then flee_from(ctx) end
        return enemy
    end, { npc = coward })
end

-- ---- the NPCs: started in their state when they are made, restarted after every load by `persist` -------------------------------------------------------------------
local function start_when_new(state)
    return function(npc, reason)
        if reason == "spawned" then risen.ai.start(npc, state, nil, { persist = true }) end
    end
end

risen.npc.persistent({ name = "EX_Companion", template = "Peasant_1_02", offset = { 700, 20, 350 }, label = "Companion", peaceful = true, on_ready = start_when_new("ZS_EX_Follow") })
risen.npc.persistent({ name = "EX_Sentry", template = "Peasant_1_01", offset = { 300, 20, -150 }, label = "Sentry", peaceful = true, on_ready = start_when_new("ZS_EX_Sentry") })
risen.npc.persistent({ name = "EX_Coward", template = "Peasant_1_03", offset = { -450, 20, 350 }, label = "Coward", peaceful = true,
    on_ready = function(npc) watch_enemies(npc) end })

if risen.test.is_active() then
    require("test_example_ai")(stats)
end
