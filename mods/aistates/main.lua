---@diagnostic disable: need-check-nil, param-type-mismatch, cast-local-type, assign-type-mismatch
-- Live test mod of M11 "AI script bridging" (scenario `aistates`, scripts/smoke-test.ps1; the readable example is mods/example_ai).
--
-- It defines Lua AI states, callbacks and perception handlers and records what the engine does with them in `rec`; test_aistates.lua drives the NPCs and checks.
-- The phase (risen.test param "phase") says what this process defines:
--   save, load       everything; `save` plays the whole scenario and saves, `load` loads that save in a fresh process
--   load_missing     the states are NOT defined (a mod that was removed): the saved follower must not hang the game
local T = risen.test
local phase = T.param("phase", "save")

local rec = { events = {}, notices = {}, damage = {}, task = {}, fn_task = {}, bad_task = 0, nuisance = {}, follow = {}, finished = {} }
risen.on("ai.finished", function(npc, state, reason, loops, run)
    rec.finished[#rec.finished + 1] = { npc = npc and npc:name(), state = state, reason = reason, loops = loops, run = run }
end)
local function ev(kind, fields)
    fields = fields or {}
    fields.kind = kind
    fields.t = T.elapsed()
    rec.events[#rec.events + 1] = fields
end

if phase ~= "load_missing" then
    -- ---- a state that records everything it is given; `args` steer it (fail = "begin"|"loop"|"finish"|"notice", target, stopAfter, next, finish_call) -------------------------
    risen.ai.state("ZS_AIT_Probe", {
        interval = 0.5,
        task = "OnRoutine",                              -- the stock task callback: the sweep that calls AssessActive for every entity around
        begin = function(ctx)
            local a = ctx.args or {}
            if a.fail == "begin" then error("boom in begin") end
            ev("begin", { npc = ctx.self:name(), resumed = ctx.resumed, tag = a.tag, time = ctx.time, kind2 = ctx.kind, name = ctx.name })
        end,
        loop = function(ctx)
            local a = ctx.args or {}
            ev("loop", { npc = ctx.self:name(), n = ctx.count, dt = ctx.dt, time = ctx.time })
            if a.fail == "loop" and ctx.count == 2 then error("boom in loop") end
            if a.target and ctx.count == 1 then
                ev("goto", { npc = ctx.self:name(), ok = ctx.self:goto_pos(a.target[1], a.target[2], a.target[3], 0) })
            end
            if a.finish_call and ctx.count == 2 then
                if a.finish_call == "routine" then ctx:finish() else ctx:finish(a.finish_call) end
            end
            if a.stopAfter and ctx.count >= a.stopAfter then return a.next or "done" end
        end,
        finish = function(ctx)
            ev("finish", { npc = ctx.self:name(), reason = ctx.reason, loops = ctx.count, tag = ctx.args and ctx.args.tag, state = ctx.self:state() })
            if ctx.args and ctx.args.fail == "finish" then error("boom in finish") end
        end,
        on = {
            notice = function(ctx)
                if ctx.args and ctx.args.fail == "notice" then error("boom in notice") end
                if ctx.other and ctx.other:is_player() then
                    rec.notices[#rec.notices + 1] = { npc = ctx.self:name(), dist = ctx.self:distance_to(ctx.other), t = T.elapsed(), time = ctx.time, name = ctx.name, kind = ctx.kind }
                end
            end,
            nuisance = function(ctx)
                local d = ctx.data
                rec.nuisance[#rec.nuisance + 1] = { npc = ctx.self:name(), other = ctx.other and ctx.other:name(), want = d and d.nuisance }
                if d and d.nuisance ~= nil then return d.nuisance end
                if d and d.original then return ctx:call_original() + 100 end
            end,
        },
    })

    -- ---- the follower: keeps near the player ---------------------------------------------------------------------------------------------------------------------
    risen.ai.state("ZS_AIT_Follow", {
        interval = 0.5,
        task = "OnRoutine",
        begin = function(ctx) ev("follow_begin", { npc = ctx.self:name(), resumed = ctx.resumed, tag = ctx.args and ctx.args.tag }) end,
        loop = function(ctx)
            local player = risen.entity.player()
            if not player then return end
            local d = ctx.self:distance_to(player)
            rec.follow.last = d
            rec.follow.loops = (rec.follow.loops or 0) + 1
            if d > 420 then
                if (ctx.data.last_goto or -9) + 2 <= ctx.time then
                    ctx.data.last_goto = ctx.time
                    ctx.self:goto_entity(player, 0)
                end
            elseif d < 280 and not ctx.data.stopped then
                ctx.data.stopped = true
                ctx.self:stop_goto()
                ctx.self:turn_to(player)
            elseif d >= 280 then
                ctx.data.stopped = nil
            end
        end,
        finish = function(ctx) ev("follow_finish", { npc = ctx.self:name(), reason = ctx.reason }) end,
    })

    -- ---- callbacks: a named one used as the task callback, an inline one, and one that fails -----------------------------------------------------------------------
    risen.ai.callback("AIT_Task", function(ctx)
        rec.task[#rec.task + 1] = { npc = ctx.self and ctx.self:name(), t = T.elapsed(), kind = ctx.kind, name = ctx.name }
    end)
    risen.ai.state("ZS_AIT_TaskDemo", { interval = 1, task = "AIT_Task", loop = function() end })
    risen.ai.state("ZS_AIT_TaskFn", {
        interval = 1,
        task = function(ctx) rec.fn_task[#rec.fn_task + 1] = { npc = ctx.self and ctx.self:name(), t = T.elapsed() } end,
        loop = function() end,
    })
    risen.ai.callback("AIT_BadTask", function(ctx) rec.bad_task = rec.bad_task + 1 error("boom in a callback") end)
    risen.ai.state("ZS_AIT_BadTask", { interval = 1, task = "AIT_BadTask", loop = function() end })

    -- ---- flees from whoever hit it (started by a perception handler of the scenario) -------------------------------------------------------------------------------
    risen.ai.state("ZS_AIT_Flee", {
        interval = 0.5,
        begin = function(ctx)
            local from = ctx.args and ctx.args.from
            local x, y, z = ctx.self:position()
            local fx, fy, fz = x - 400, y, z
            if from and from:valid() then
                local ax, ay, az = from:position()
                local dx, dz = x - ax, z - az
                local len = math.sqrt(dx * dx + dz * dz)
                if len > 1 then fx, fz = x + dx / len * 450, z + dz / len * 450 end
            end
            ctx.data.from = { x, z }
            ev("flee_begin", { npc = ctx.self:name(), ok = ctx.self:goto_pos(fx, y, fz, 0), from = from and from:name() })
        end,
        loop = function(ctx)
            if ctx.time > 12 then return "done" end
        end,
        finish = function(ctx) ev("flee_finish", { npc = ctx.self:name(), reason = ctx.reason }) end,
    })
end

risen.npc.persistent({
    name = "AIT_Follower", template = "Peasant_1_02", offset = { 700, 20, 350 }, label = "Follower", peaceful = true,
    -- the NPC starts in the Lua state when he is made. A savegame does not keep the state: persist = true makes the layer start it again after every load
    on_ready = function(entity, reason)
        if reason == "spawned" and phase ~= "load_missing" then risen.ai.start(entity, "ZS_AIT_Follow", { tag = "follow" }, { persist = true }) end
    end,
})

if T.is_active() then
    require("test_aistates")(phase, rec)
end
