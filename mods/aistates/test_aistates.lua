---@diagnostic disable: need-check-nil, param-type-mismatch, cast-local-type, assign-type-mismatch
-- Live scenario `aistates` of mods/aistates (only loaded in a test run). One game process per phase (scripts/smoke-test.ps1):
--   save          new game: the registry, refusals, a Lua state on a real NPC (begin / loop cadence / walking / perception / stop / done / replaced), errors in every kind of callback,
--                 task callbacks, a damage handler that sends a bitten NPC running, the follower (a persistent NPC whose initial state is a Lua state); quick save, quick load in the same process
--   load          a fresh process loads that save: the follower's saved state is resumed by the Lua state of the same name
--   load_missing  a fresh process without the mod's states (a mod that was removed): the engine drops the unknown state, the game goes on
local T = risen.test

return function(phase, rec)
    local P0 = {}

    local function dist(a, b) return (a and b) and a:distance_to(b) or 1e9 end
    local function await(pred, seconds)
        for _ = 1, math.floor(seconds * 8) do
            if pred() then return true end
            T.sleep(10)
        end
        return pred() and true or false
    end
    local function contains(list, value) for _, v in ipairs(list or {}) do if v == value then return true end end return false end
    local function mark() return #rec.events end
    local function lua_state(e) local c = e and risen.ai.current(e) return c and c.state end
    local function since(m, kind, npc)
        local out = {}
        for i = m + 1, #rec.events do
            local e = rec.events[i]
            if e.kind == kind and (npc == nil or e.npc == npc) then out[#out + 1] = e end
        end
        return out
    end
    local function npc_at(name, dx, dz, template)
        return assert(risen.npc.spawn(template or "Peasant_1_01", P0.x + dx, P0.y + 20, P0.z + dz, { name = name, peaceful = true }))
    end
    local function d2(npc, x, z)
        local nx, _, nz = npc:position()
        return math.sqrt((nx - x) ^ 2 + (nz - z) ^ 2)
    end
    local function failures() return (risen.ai.failures()) end
    local function save_game()
        T.wait_until(function() return risen.world.is_save_allowed() end, 30, "saving allowed")
        local saved = false
        risen.on("save.after", function(_, ok) saved = ok end)
        risen.world.quick_save()
        T.wait_until(function() return saved end, 60, "savegame written")
        return saved
    end
    local function wait_follower()
        T.wait_until(function() return risen.entity.get("AIT_Follower") ~= nil end, 60, "AIT_Follower exists")
        T.sleep(60)
    end
    local function by_id(id) return id and risen.entity.by_id(id) or nil end

    -- ===================================================================================================================================================
    if phase == "save" then
        T.run(function()
            T.wait_event("session.start", 180, function(m) return m == "new" end)
            wait_follower()
            local player = assert(risen.entity.player())
            P0.x, P0.y, P0.z = player:position()
            local noop = function() end

            -- ---- the registry -------------------------------------------------------------------------------------------------------------------------------
            T.section("registry")
            local states, callbacks, functions = risen.ai.list("state"), risen.ai.list("callback"), risen.ai.list("function")
            T.expect("registry: the game's AI states are listed, sorted", #states > 200 and states[1] <= states[2] and contains(states, "ZS_HangAround") and contains(states, "ZS_FollowPlayer"), #states)
            T.expect("registry: callbacks and functions are listed", contains(callbacks, "OnRoutine") and contains(callbacks, "OnFollowPlayer") and #functions > 100, #callbacks .. "/" .. #functions)
            T.expect("registry: the Lua states and callbacks are in it", contains(states, "ZS_AIT_Probe") and contains(states, "ZS_AIT_Follow") and contains(callbacks, "AIT_Task") and contains(callbacks, "ZS_AIT_TaskFn_Task"))
            local stock, mine = risen.ai.info("state", "ZS_HangAround"), risen.ai.info("state", "ZS_AIT_Probe")
            T.expect("registry: info tells the game's states from Lua's", stock and mine and stock.module == "Script_Game.dll" and stock.lua == false and stock.rva > 0 and mine.lua == true and mine.module ~= stock.module,
                stock and mine and (stock.module .. " " .. tostring(stock.lua) .. " / " .. mine.module .. " " .. tostring(mine.lua)))
            T.expect("registry: exists", risen.ai.exists("state", "ZS_HangAround") and risen.ai.exists("callback", "OnRoutine") and not risen.ai.exists("state", "ZS_NoSuchState") and not risen.ai.exists("bogus", "x"))
            local nilkind, why = risen.ai.list("bogus")
            T.expect("registry: an unknown kind is refused with a reason", nilkind == nil and type(why) == "string" and why:find("kind"), tostring(why))
            local defined = risen.ai.defined()
            T.expect("registry: defined() lists what Lua made", contains(defined.states, "ZS_AIT_Flee") and contains(defined.callbacks, "AIT_BadTask"))
            T.expect("registry: the perception table", risen.ai.perceptions.damage == "AssessDamage" and risen.ai.perceptions.notice == "AssessActive")

            -- ---- refusals: nothing may crash, every refusal says why ---------------------------------------------------------------------------------------------
            T.section("refusals")
            local cases = {
                { "name is a number", 5, { loop = noop } }, { "empty name", "", { loop = noop } }, { "name starts with a digit", "1abc", { loop = noop } },
                { "name has a space", "ZS_a b", { loop = noop } }, { "name too long", string.rep("x", 101), { loop = noop } }, { "a game state", "ZS_HangAround", { loop = noop } },
                { "definition is a number", "ZS_AIT_X1", 5 }, { "definition is nil", "ZS_AIT_X2", nil }, { "empty definition", "ZS_AIT_X3", {} }, { "unknown key", "ZS_AIT_X4", { loop = noop, colour = 1 } },
                { "begin is a number", "ZS_AIT_X5", { begin = 5 } }, { "loop is a string", "ZS_AIT_X6", { loop = "x" } }, { "on is a number", "ZS_AIT_X7", { on = 5 } },
                { "unknown perception", "ZS_AIT_X8", { on = { sniff = noop } } }, { "perception handler is a number", "ZS_AIT_X9", { on = { notice = 5 } } },
                { "unknown task callback", "ZS_AIT_X10", { loop = noop, task = "NoSuchCallback" } }, { "task is a number", "ZS_AIT_X11", { loop = noop, task = 5 } },
                { "interval is text", "ZS_AIT_X12", { loop = noop, interval = "fast" } }, { "a script that is not an Assess one", "ZS_AIT_X13", { on = { Kill = noop } } },
            }
            local refused, reasons = 0, {}
            for _, c in ipairs(cases) do
                local ok, reason = risen.ai.state(c[2], c[3])
                if ok == nil and type(reason) == "string" and reason ~= "" then refused = refused + 1 else reasons[#reasons + 1] = c[1] .. "=" .. tostring(ok) end
            end
            T.expect("refused: bad state definitions say why", refused == #cases, table.concat(reasons, "; "))
            local _, r1 = risen.ai.state("ZS_HangAround", { loop = noop })
            local _, r2 = risen.ai.state("ZS_AIT_X14", { on = { sniff = noop } })
            T.expect("refused: the reasons are specific", r1:find("already exists") and r2:find("unknown perception") and r2:find("damage"), r1 .. " | " .. r2)
            local c1 = risen.ai.callback(5, noop)
            local c2 = risen.ai.callback("AIT_X", 5)
            local c3, rc3 = risen.ai.callback("OnRoutine", noop)
            T.expect("refused: bad callbacks (also one that is the game's)", c1 == nil and c2 == nil and c3 == nil and rc3:find("already exists"), tostring(rc3))
            local V0 = npc_at("AIT_Refuse", 300, -300)
            local pcases = {
                { "kind is a number", 5, noop, { all = true } }, { "unknown kind", "sniff", noop, { all = true } }, { "handler is a number", "damage", 5, { all = true } },
                { "no filter", "damage", noop, nil }, { "empty filter", "damage", noop, {} }, { "all is text", "damage", noop, { all = "yes" } }, { "unknown filter key", "damage", noop, { color = 1 } },
                { "npc is a number", "damage", noop, { npc = 5 } }, { "npc list with a number", "damage", noop, { npc = { V0, 5 } } },
            }
            local prefused = 0
            for _, c in ipairs(pcases) do
                local h, reason = risen.ai.perceive(c[2], c[3], c[4])
                if h == nil and type(reason) == "string" and reason ~= "" then prefused = prefused + 1 else reasons[#reasons + 1] = "perceive " .. c[1] end
            end
            T.expect("refused: bad perception handlers", prefused == #pcases, table.concat(reasons, "; "))
            local dead = npc_at("AIT_Dead", -300, 300)
            T.sleep(60)
            dead:kill()
            await(function() return dead:is_dead() end, 5)
            local sref = {
                { "npc is a number", 5, "ZS_AIT_Probe" }, { "state is a number", V0, 7 }, { "unknown state", V0, "ZS_NoSuchState" }, { "the player", player, "ZS_HangAround" },
                { "options is a number", V0, "ZS_AIT_Probe", nil, 5 }, { "unknown option", V0, "ZS_AIT_Probe", nil, { speed = 1 } }, { "interrupt is text", V0, "ZS_AIT_Probe", nil, { interrupt = "yes" } },
                { "a dead NPC", dead, "ZS_AIT_Probe" }, { "an item", assert(risen.entity.spawn("It_Apple", P0.x + 100, P0.y + 20, P0.z + 100)), "ZS_AIT_Probe" },
            }
            local srefused = 0
            for _, c in ipairs(sref) do
                local ok, reason = risen.ai.start(c[2], c[3], c[4], c[5])
                if ok == nil and type(reason) == "string" and reason ~= "" then srefused = srefused + 1 else reasons[#reasons + 1] = "start " .. c[1] .. "=" .. tostring(ok) end
            end
            T.expect("refused: bad risen.ai.start calls", srefused == #sref, table.concat(reasons, "; "))
            local s1, sr1 = risen.ai.stop(V0)
            local s2 = risen.ai.stop(5)
            T.expect("refused: stop without a running Lua state", s1 == nil and sr1:find("not running") and s2 == nil)
            T.expect("refused: nothing broke", risen.ai.current(V0) == nil and risen.ai.current(5) == nil and V0:valid(), tostring(V0:state()))
            V0:kill()

            -- ---- begin / loop / walking / perception / stop -------------------------------------------------------------------------------------------------
            T.section("a Lua state on an NPC")
            local A = npc_at("AIT_A", 450, 250)
            local ax, ay, az = A:position()
            local m = mark()
            assert(risen.ai.start(A, "ZS_AIT_Probe", { tag = "alpha", target = { ax - 250, ay, az } }))
            T.expect("start: begin ran with the start arguments", await(function() return #since(m, "begin", "AIT_A") >= 1 end, 10))
            local b = since(m, "begin", "AIT_A")[1] or {}
            T.eq("begin: arguments, flags and the context", { b.tag, b.resumed, b.kind2, b.name, #since(m, "begin", "AIT_A") }, { "alpha", false, "begin", "ZS_AIT_Probe", 1 })
            T.eq("begin: ctx.time starts at 0", b.time or -1, 0, 0.3)
            local cur = risen.ai.current(A)
            T.expect("the NPC is in the Lua state (the engine reports it)", A:state() == "ZS_AIT_Probe" and cur and cur.state == "ZS_AIT_Probe" and cur.resumed == false and cur.args.tag == "alpha", tostring(A:state()))
            T.expect("loop: runs on", await(function() return #since(m, "loop", "AIT_A") >= 14 end, 15), #since(m, "loop", "AIT_A"))
            local loops = since(m, "loop", "AIT_A")
            local steady, numbered = 0, true
            for i, l in ipairs(loops) do
                if l.n ~= i - 1 then numbered = false end
                if i > 1 and l.dt >= 0.45 and l.dt <= 0.75 then steady = steady + 1 end
            end
            T.expect("loop: cadence is the interval (0.5 s) and ctx.count counts up", numbered and steady >= (#loops - 1) * 0.9, ("%d of %d steady, numbered %s"):format(steady, #loops - 1, tostring(numbered)))
            local gt = since(m, "goto", "AIT_A")[1]
            T.expect("behaviour: the NPC walked to the point the loop sent it to", gt and gt.ok and await(function() return d2(A, ax - 250, az) < 100 end, 25), ("%s, %.0f from the target"):format(tostring(gt and gt.ok), d2(A, ax - 250, az)))
            T.expect("the state stayed while he walked", A:state() == "ZS_AIT_Probe" and #since(m, "finish", "AIT_A") == 0)
            -- perception: the stock sweep (task callback OnRoutine) calls AssessActive for every entity around; the state's `notice` handler sees the player
            local mine_n = {}
            for _, n in ipairs(rec.notices) do if n.npc == "AIT_A" then mine_n[#mine_n + 1] = n end end
            local spaced = 0
            for i = 2, #mine_n do local gap = mine_n[i].t - mine_n[i - 1].t if gap >= 0.7 and gap <= 1.6 then spaced = spaced + 1 end end
            T.expect("perception: the player is noticed about once a second", #mine_n >= 4 and spaced >= (#mine_n - 1) * 0.8, ("%d notices, %d well spaced"):format(#mine_n, spaced))
            local n1 = mine_n[#mine_n] or {}
            T.expect("perception: the handler's context", n1.name == "AssessActive" and n1.kind == "perceive" and math.abs((n1.dist or 0) - dist(A, player)) < 400 and n1.time ~= nil, tostring(n1.name) .. " " .. tostring(n1.kind))

            -- a handler that returns a value replaces the stock script; nil lets it run; ctx:call_original() runs it from inside the handler
            local data = risen.ai.current(A).data
            local base = risen.script.call("AssessNuisance", A, player, 0)
            data.nuisance = 7
            local forced = risen.script.call("AssessNuisance", A, player, 0)
            data.nuisance = nil
            data.original = true
            local wrapped = risen.script.call("AssessNuisance", A, player, 0)
            data.original = nil
            local B0 = npc_at("AIT_B0", -300, -300)
            local plain = risen.script.call("AssessNuisance", B0, player, 0)
            T.expect("perception: a returned value is the script's result, nil is the stock one, call_original wraps it", forced == 7 and wrapped == base + 100 and plain == base, ("base %d forced %d wrapped %d plain %d"):format(base, forced, wrapped, plain))
            B0:kill()

            m = mark()
            assert(risen.ai.stop(A))
            T.expect("stop: finish ran with reason 'stopped'", await(function() return #since(m, "finish", "AIT_A") >= 1 end, 8) and since(m, "finish", "AIT_A")[1].reason == "stopped", tostring(since(m, "finish", "AIT_A")[1] and since(m, "finish", "AIT_A")[1].reason))
            T.expect("stop: the NPC left the state (back to his routine)", await(function() return A:state() ~= "ZS_AIT_Probe" end, 8) and risen.ai.current(A) == nil, tostring(A:state()))
            local loops_after = #since(m, "loop", "AIT_A")
            T.sleep(120)
            T.eq("stop: no loop runs afterwards", #since(m, "loop", "AIT_A"), loops_after)

            T.section("ending a state from Lua")
            m = mark()
            assert(risen.ai.start(A, "ZS_AIT_Probe", { tag = "beta", stopAfter = 3 }))
            T.expect("loop returning 'done': finish reason done, back to the routine", await(function() return #since(m, "finish", "AIT_A") >= 1 end, 10) and since(m, "finish", "AIT_A")[1].reason == "done"
                and since(m, "finish", "AIT_A")[1].loops == 4 and await(function() return A:state() ~= "ZS_AIT_Probe" end, 6), tostring(A:state()))
            m = mark()
            assert(risen.ai.start(A, "ZS_AIT_Probe", { tag = "gamma", stopAfter = 3, next = "ZS_AIT_TaskDemo" }))
            T.expect("loop returning a state name: that state follows", await(function() return A:state() == "ZS_AIT_TaskDemo" end, 12) and since(m, "finish", "AIT_A")[1].reason == "done", tostring(A:state()))
            T.expect("current() names the new Lua state", (risen.ai.current(A) or {}).state == "ZS_AIT_TaskDemo")
            m = mark()
            assert(risen.ai.start(A, "ZS_AIT_Probe", { tag = "delta", finish_call = "ZS_AIT_TaskFn" }))
            T.expect("ctx:finish(next): ends after the callback, the named state follows", await(function() return A:state() == "ZS_AIT_TaskFn" end, 12) and since(m, "finish", "AIT_A")[1].reason == "done", tostring(A:state()))
            m = mark()
            assert(risen.ai.start(A, "ZS_AIT_Probe", { tag = "eps", finish_call = "routine" }))
            T.expect("ctx:finish(): back to the routine", await(function() return #since(m, "finish", "AIT_A") >= 1 end, 10) and await(function() return A:state() ~= "ZS_AIT_Probe" end, 6), tostring(A:state()))
            m = mark()
            assert(risen.ai.start(A, "ZS_AIT_Probe", { tag = "zeta" }))
            await(function() return #since(m, "loop", "AIT_A") >= 2 end, 8)
            A:set_state("ZS_HangAround")
            T.expect("the engine replacing the state: finish reason 'replaced'", await(function() return #since(m, "finish", "AIT_A") >= 1 end, 8) and since(m, "finish", "AIT_A")[1].reason == "replaced", tostring(since(m, "finish", "AIT_A")[1] and since(m, "finish", "AIT_A")[1].reason))
            m = mark()
            assert(risen.ai.start(A, "ZS_AIT_Probe", { tag = "eta" }))
            await(function() return #since(m, "loop", "AIT_A") >= 2 end, 8)
            assert(risen.ai.start(A, "ZS_AIT_Probe", { tag = "theta" }))
            T.expect("starting the state again restarts it: replaced, then a new begin", await(function() return #since(m, "begin", "AIT_A") >= 2 end, 8)
                and since(m, "finish", "AIT_A")[1].reason == "replaced" and since(m, "begin", "AIT_A")[2].tag == "theta", tostring(#since(m, "begin", "AIT_A")))
            risen.ai.stop(A)
            await(function() return risen.ai.current(A) == nil end, 8)
            -- a state the engine itself defines, started with risen.ai.start
            assert(risen.ai.start(A, "ZS_HangAround"))
            T.expect("risen.ai.start also starts the game's own states", await(function() return (A:state() or ""):find("^ZS_HangAround") ~= nil end, 6), tostring(A:state()))

            -- ---- errors in Lua callbacks ----------------------------------------------------------------------------------------------------------------------
            T.section("errors")
            local B = npc_at("AIT_B", 600, -250)
            local f0 = failures()
            m = mark()
            assert(risen.ai.start(B, "ZS_AIT_Probe", { tag = "e1", fail = "begin" }))
            T.expect("error in begin: warning, the state ends (reason 'error'), the NPC is fine", await(function() return #since(m, "finish", "AIT_B") >= 1 end, 8) and since(m, "finish", "AIT_B")[1].reason == "error"
                and #since(m, "begin", "AIT_B") == 0 and failures() == f0 + 1 and await(function() return B:state() ~= "ZS_AIT_Probe" end, 6), tostring(failures() - f0))
            T.expect("the failure is reported", select(2, risen.ai.failures()):find("begin of state 'ZS_AIT_Probe' failed") ~= nil, select(2, risen.ai.failures()))
            f0 = failures()
            m = mark()
            assert(risen.ai.start(B, "ZS_AIT_Probe", { tag = "e2", fail = "loop" }))
            T.expect("error in loop: warning, the state ends with reason 'error'", await(function() return #since(m, "finish", "AIT_B") >= 1 end, 10) and since(m, "finish", "AIT_B")[1].reason == "error"
                and failures() == f0 + 1 and await(function() return B:state() ~= "ZS_AIT_Probe" end, 6), tostring(failures() - f0))
            f0 = failures()
            m = mark()
            assert(risen.ai.start(B, "ZS_AIT_Probe", { tag = "e3", fail = "finish", stopAfter = 2 }))
            T.expect("error in finish: warning, the NPC still leaves the state", await(function() return #since(m, "finish", "AIT_B") >= 1 end, 10) and failures() == f0 + 1
                and await(function() return B:state() ~= "ZS_AIT_Probe" end, 6), tostring(failures() - f0))
            f0 = failures()
            m = mark()
            local n0 = #rec.notices
            assert(risen.ai.start(B, "ZS_AIT_Probe", { tag = "e4", fail = "notice" }))
            T.expect("error in a perception handler: warning once, the handler is switched off, the state goes on", await(function() return failures() == f0 + 1 end, 8), tostring(failures() - f0))
            T.sleep(400)
            T.expect("... no second error, no notices, the state is still running", failures() == f0 + 1 and #rec.notices == n0 and risen.ai.current(B) ~= nil and B:state() == "ZS_AIT_Probe", ("%d %d"):format(failures() - f0, #rec.notices - n0))
            risen.ai.stop(B)
            local C = npc_at("AIT_C", -500, 200)
            f0 = failures()
            assert(risen.ai.start(C, "ZS_AIT_BadTask"))
            T.expect("error in a callback: warning once, the callback is switched off, the state goes on", await(function() return rec.bad_task >= 1 end, 8) and failures() == f0 + 1, tostring(rec.bad_task))
            T.sleep(400)
            T.expect("... the callback is not called again", rec.bad_task == 1 and failures() == f0 + 1 and C:state() == "ZS_AIT_BadTask", tostring(rec.bad_task))
            T.expect("the game is alive after all the errors", risen.entity.player() ~= nil and select(1, player:position()) ~= nil and A:valid())

            -- ---- task callbacks ---------------------------------------------------------------------------------------------------------------------------------
            T.section("task callbacks")
            local D = npc_at("AIT_D", -600, -100)
            local E = npc_at("AIT_E", -450, -450)
            local tm, fm = #rec.task, #rec.fn_task
            assert(risen.ai.start(D, "ZS_AIT_TaskDemo"))
            assert(risen.ai.start(E, "ZS_AIT_TaskFn"))
            T.expect("task callback: a named Lua callback runs about once a second, with the NPC as self", await(function() return #rec.task - tm >= 4 end, 15), tostring(#rec.task - tm))
            local gaps, okgaps = {}, 0
            for i = tm + 2, #rec.task do local g = rec.task[i].t - rec.task[i - 1].t gaps[#gaps + 1] = g if g > 0.7 and g < 1.4 then okgaps = okgaps + 1 end end
            T.expect("... evenly spaced", #gaps >= 3 and okgaps >= #gaps * 0.8 and rec.task[tm + 1].npc == "AIT_D" and rec.task[tm + 1].kind == "callback" and rec.task[tm + 1].name == "AIT_Task", table.concat(gaps, ","))
            T.expect("task callback: a function in the definition works the same way", await(function() return #rec.fn_task - fm >= 4 end, 15) and rec.fn_task[fm + 1].npc == "AIT_E", tostring(#rec.fn_task - fm))
            risen.ai.stop(D)
            risen.ai.stop(E)

            -- ---- the state option of risen.npc.spawn ------------------------------------------------------------------------
            T.section("spawn state")
            local S = assert(risen.npc.spawn("Peasant_1_01", P0.x - 200, P0.y + 20, P0.z + 300, { name = "AIT_S", peaceful = true, state = "ZS_AIT_TaskDemo" }))
            T.expect("the state option of risen.npc.spawn starts a Lua state in the NPC's first frame", await(function() return lua_state(S) == "ZS_AIT_TaskDemo" end, 10), tostring(S:state()))
            risen.ai.stop(S)

            -- ---- persist = true: a state is not part of a savegame, the layer starts it again after a load -------------------------------------------------------------------
            T.section("persist")
            local PA = npc_at("AIT_PA", -350, 450)
            local PB = npc_at("AIT_PB", -250, 500)
            local PC = npc_at("AIT_PC", -150, 550)
            local f_before = #rec.finished
            local bad, whybad = risen.ai.start(PA, "ZS_AIT_Probe", { fn = noop }, { persist = true })
            T.expect("persist: arguments that cannot be saved are refused", bad == nil and whybad:find("plain data"), tostring(whybad))
            local bad2 = risen.ai.start(PA, "ZS_AIT_Probe", nil, { persist = "yes" })
            T.expect("persist: must be a boolean", bad2 == nil)
            assert(risen.ai.start(PA, "ZS_AIT_Probe", { tag = "pa", stopAfter = 2 }, { persist = true }))
            assert(risen.ai.start(PB, "ZS_AIT_Probe", { tag = "pb" }, { persist = true }))
            assert(risen.ai.start(PC, "ZS_AIT_Probe", { tag = "pc" }, { persist = true }))
            local function persisted_tags()
                local out = {}
                for _, r in ipairs(risen.ai.persisted()) do out[#out + 1] = r.args and r.args.tag or "?" end
                table.sort(out)
                return table.concat(out, ",")
            end
            T.expect("persist: the states are remembered (the follower's too)", persisted_tags() == "follow,pa,pb,pc", persisted_tags())
            T.expect("persist: a state that ended by itself is forgotten", await(function() return persisted_tags() == "follow,pb,pc" end, 12), persisted_tags())
            T.expect("the ai.finished event reports it (npc, state, reason, loops, run)", await(function()
                for i = f_before + 1, #rec.finished do
                    local f = rec.finished[i]
                    if f.npc == "AIT_PA" and f.state == "ZS_AIT_Probe" and f.reason == "done" and f.loops == 3 and f.run > 0 then return true end
                end
                return false
            end, 5), #rec.finished - f_before)
            assert(risen.ai.stop(PB))
            T.expect("persist: risen.ai.stop forgets the NPC", persisted_tags() == "follow,pc", persisted_tags())
            await(function() return (risen.ai.current(PC) or {}).loops and risen.ai.current(PC).loops >= 2 end, 6)
            PC:set_state("ZS_HangAround")
            T.sleep(60)
            T.expect("persist: a state the engine replaced stays remembered", persisted_tags() == "follow,pc", persisted_tags())
            T.expect("persist: forget() drops it, once", risen.ai.forget(PC) == true and risen.ai.forget(PC) == false and persisted_tags() == "follow", persisted_tags())
            assert(risen.ai.start(PA, "ZS_AIT_Probe", { tag = "pa2" }, { persist = true }))
            assert(risen.ai.start(PA, "ZS_AIT_Probe", { tag = "pa3" }))
            T.expect("persist: a plain start replaces what was remembered for the NPC", persisted_tags() == "follow", persisted_tags())
            risen.ai.stop(PA)

            -- ---- the follower: a persistent NPC that is started in a Lua state when he is made ----------------------------------------------------------------------------
            T.section("follower")
            local F = assert(risen.entity.get("AIT_Follower"))
            T.expect("follower: started in the Lua state when he was made (on_ready -> risen.ai.start waits for the NPC's first frame)", await(function() return #since(0, "follow_begin", "AIT_Follower") >= 1 end, 15)
                and since(0, "follow_begin", "AIT_Follower")[1].resumed == false and since(0, "follow_begin", "AIT_Follower")[1].tag == "follow" and F:state() == "ZS_AIT_Follow", tostring(F:state()))
            T.expect("behaviour: the follower walks up to the player", await(function() return dist(F, player) < 450 end, 80), tostring(dist(F, player)))
            T.expect("... and keeps a distance", await(function() return dist(F, player) > 100 end, 5), tostring(dist(F, player)))

            -- ---- save, and load in the same process ---------------------------------------------------------------------------------------------------------------------
            T.section("save and load")
            risen.save.set("exp", { fid = F:id() })
            T.expect("saved", save_game())
            local fm0 = #since(0, "follow_begin", "AIT_Follower")
            local loops0 = rec.follow.loops or 0
            T.sleep(100)
            risen.world.quick_load()
            T.wait_event("session.start", 120, function(mode) return mode == "load" end)
            wait_follower()
            local F2 = assert(risen.entity.get("AIT_Follower"))
            T.expect("loaded: the layer started the remembered state again (resumed, with the saved arguments)", await(function() return #since(0, "follow_begin", "AIT_Follower") > fm0 end, 20)
                and since(0, "follow_begin", "AIT_Follower")[fm0 + 1].resumed == true and since(0, "follow_begin", "AIT_Follower")[fm0 + 1].tag == "follow" and F2:state() == "ZS_AIT_Follow", tostring(F2:state()))
            local cur2 = risen.ai.current(F2)
            T.sleep(240)
            T.eq("loaded: exactly one begin after the load (the savegame itself keeps no Lua state: only the layer's restart)", #since(0, "follow_begin", "AIT_Follower"), fm0 + 1)
            T.expect("loaded: current() sees the new run", cur2 and cur2.state == "ZS_AIT_Follow" and cur2.resumed == true and cur2.args and cur2.args.tag == "follow", cur2 and tostring(cur2.resumed) or "no run")
            T.expect("loaded: the loop runs again", await(function() return (rec.follow.loops or 0) > loops0 + 6 end, 15), tostring((rec.follow.loops or 0) - loops0))
            T.expect("loaded: the follower is still near the player", await(function() return dist(F2, risen.entity.player()) < 600 end, 40), tostring(dist(F2, risen.entity.player())))
            T.expect("loaded: only the follower was remembered", persisted_tags() == "follow", persisted_tags())

            -- ---- a damage handler that starts a state: "flees when hit" --------------------------------------------------------------------------------------------
            T.section("damage")
            player = assert(risen.entity.player())
            local V = npc_at("AIT_Victim", 420, 120)
            T.sleep(80)
            local handle = assert(risen.ai.perceive("damage", function(ctx)
                local vx, _, vz = ctx.self:position()
                rec.damage[#rec.damage + 1] = { victim = ctx.self:name(), attacker = ctx.other and ctx.other:name(), t = T.elapsed(), kind = ctx.kind, name = ctx.name, x = vx, z = vz }
                if not rec.fled then
                    rec.fled = true
                    risen.ai.start(ctx.self, "ZS_AIT_Flee", { from = ctx.other })
                    return true     -- handled: the stock reaction does not run
                end
            end, { npc = V }))
            local vx, vy, vz = V:position()
            local wolf = assert(risen.npc.spawn("Wolf", vx + 70, vy, vz, { name = "AIT_Wolf" }))
            T.expect("damage: the handler runs for a real bite, with victim and attacker", await(function() return #rec.damage >= 1 end, 40), "no AssessDamage seen")
            local dmg = rec.damage[1] or {}
            wolf:kill()
            player:set_hp(player:hp_max())
            T.eq("damage: ctx has the victim, the attacker and the script", { dmg.victim, dmg.attacker, dmg.kind, dmg.name }, { "AIT_Victim", "AIT_Wolf", "perceive", "AssessDamage" })
            T.expect("damage: the handler started the flee state", await(function() return V:valid() and (risen.ai.current(V) or {}).state == "ZS_AIT_Flee" end, 5) and #since(0, "flee_begin", "AIT_Victim") == 1, tostring(V:state()))
            T.expect("flee: begin saw the attacker and sent the NPC away", since(0, "flee_begin", "AIT_Victim")[1].from == "AIT_Wolf" and since(0, "flee_begin", "AIT_Victim")[1].ok == true)
            T.expect("behaviour: the NPC ran away from where he was bitten", await(function() return V:valid() and d2(V, dmg.x, dmg.z) > 250 end, 20), V:valid() and tostring(d2(V, dmg.x, dmg.z)) or "gone")
            T.expect("damage: unregistering the handler", risen.ai.unperceive(handle) == true and risen.ai.unperceive(handle) == false)
            risen.ai.stop(V)

        end, "aistates")
        return
    end

    -- ===================================================================================================================================================
    if phase == "load" then
        T.run(function()
            local mode = T.wait_event("session.start", 180, function(m) return m == "load" end)
            T.eq("loaded (fresh process)", mode, "load")
            wait_follower()
            T.sleep(80)
            local exp = risen.save.get("exp")
            local F = risen.entity.get("AIT_Follower")
            T.expect("fresh: the follower is the saved NPC", F and F:id() == exp.fid, tostring(F and F:id()))
            T.expect("fresh: the remembered state was started again (begin with resumed = true and the saved arguments)", await(function() return #since(0, "follow_begin", "AIT_Follower") >= 1 end, 20)
                and since(0, "follow_begin", "AIT_Follower")[1].resumed == true and since(0, "follow_begin", "AIT_Follower")[1].tag == "follow" and F:state() == "ZS_AIT_Follow", tostring(F and F:state()))
            T.sleep(240)
            T.eq("fresh: only one begin", #since(0, "follow_begin", "AIT_Follower"), 1)
            local cur = risen.ai.current(F)
            T.expect("fresh: current() sees the run", cur and cur.state == "ZS_AIT_Follow" and cur.resumed == true and cur.args and cur.args.tag == "follow")
            T.expect("fresh: the record is still there", #risen.ai.persisted() == 1 and risen.ai.persisted()[1].id == exp.fid)
            T.expect("fresh: the loop runs", await(function() return (rec.follow.loops or 0) >= 6 end, 15), tostring(rec.follow.loops))
            T.expect("fresh: the follower keeps near the player", await(function() return dist(F, risen.entity.player()) < 600 end, 40), tostring(dist(F, risen.entity.player())))
            T.expect("fresh: the game's registry has the Lua states", risen.ai.exists("state", "ZS_AIT_Follow") and risen.ai.exists("callback", "AIT_Task"))
            -- an NPC that was saved in a Lua state can be sent elsewhere
            T.expect("fresh: risen.ai.stop ends a resumed run", risen.ai.stop(F) == true and await(function() return F:state() ~= "ZS_AIT_Follow" and risen.ai.current(F) == nil end, 8), tostring(F:state()))
            T.expect("fresh: stop forgot the record", #risen.ai.persisted() == 0)
            T.expect("fresh: and it can be started again with arguments", risen.ai.start(F, "ZS_AIT_Follow", { tag = "again" }) == true and await(function() return risen.ai.current(F) ~= nil and risen.ai.current(F).resumed == false end, 8))
        end, "aistates")
        return
    end

    -- ===================================================================================================================================================
    if phase == "load_missing" then
        T.run(function()
            local mode = T.wait_event("session.start", 180, function(m) return m == "load" end)
            T.eq("loaded (fresh process, the mod defines no states)", mode, "load")
            wait_follower()
            local exp = risen.save.get("exp")
            local F = risen.entity.get("AIT_Follower")
            T.expect("missing: the follower is the saved NPC", F and F:id() == exp.fid, tostring(F and F:id()))
            T.expect("missing: the registry does not have the states", not risen.ai.exists("state", "ZS_AIT_Follow") and #(risen.ai.defined().states) == 0)
            T.expect("missing: the NPC is not in the state (the engine does not run an unknown one)", F:state() ~= "ZS_AIT_Follow" or await(function() return F:state() ~= "ZS_AIT_Follow" end, 30), tostring(F:state()))
            T.expect("missing: the layer drops the remembered state of a state that is not defined any more", await(function() return #risen.ai.persisted() == 0 end, 10), tostring(#risen.ai.persisted()))
            local ok, why = risen.ai.start(F, "ZS_AIT_Follow")
            T.expect("missing: starting the absent state is refused", ok == nil and why:find("unknown AI state"), tostring(why))
            T.sleep(240)
            T.expect("missing: the game goes on", risen.entity.player() ~= nil and F:valid())
        end, "aistates")
        return
    end

    error("unknown phase " .. tostring(phase))
end
