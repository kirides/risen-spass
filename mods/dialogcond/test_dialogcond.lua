-- Live scenario `dialogcond` of mods/dialogcond (only loaded in a test run). One game process per phase (scripts/smoke-test.ps1):
--   save   new game: the options are compiled as expected, the world is changed step by step (items, quest, variables, skill, guild, deaths) and after every step the engine's answer (Info:available()) is
--          compared with the same condition evaluated in Lua; real conversations show the offered options; the Lua script condition is counted; bad specs are refused; leaves a quick save
--   load   a fresh process loads that save: the options (defined in load.before) are there with their conditions, the engine offers what the saved world says, the state is switched again
local T = risen.test
local C = risen.cond

return function(phase, X)
    local OPT, calls, flag = X.OPT, X.calls, X.flag
    local OWNER, VICTIM, CORPSE, QUEST, ORE = X.OWNER, X.VICTIM, X.CORPSE, X.QUEST, X.ORE
    local CTX = { npc = OWNER, level = "warn" }

    local function keys()
        local t = {}
        for k in pairs(OPT) do t[#t + 1] = k end
        table.sort(t)
        return t
    end
    local function join(list) return table.concat(list, ",") end
    local function set_of(list) local s = {} for _, v in ipairs(list) do s[v] = true end return s end

    -- the options the engine offers right now, by Info:available() (every option, or the keys given)
    local function offered(which)
        local out = {}
        for _, k in ipairs(which or keys()) do
            local o = OPT[k]
            if o.info and o.info:available() then out[#out + 1] = k end
        end
        return out
    end
    -- the options the running conversation lists
    local function conversation_list()
        local out = {}
        for _, o in ipairs(risen.dialog.available()) do
            local n = o:name()
            if n and n:find("^DC_") and n ~= "DC_Bye" then out[#out + 1] = n:sub(4) end
        end
        table.sort(out)
        return out
    end
    local function converse(player, npc)
        assert(risen.dialog.start(player, npc), "dialog.start")
        T.sleep(90)
        local list = conversation_list()
        assert(risen.dialog.get("DC_Bye")):run()
        T.wait_event("info.finished", 60, function(name) return name == "DC_Bye" end)
        T.sleep(60)
        return list
    end
    -- every option whose condition is comparable: the engine's answer must be what Lua says
    local function mismatches()
        local bad = {}
        for _, k in ipairs(keys()) do
            local o = OPT[k]
            if o.oracle ~= false and o.when ~= nil and o.engine == nil then
                local engine = o.info and o.info:available() or false
                local lua = C.test(o.when, CTX) == true
                if engine ~= lua then bad[#bad + 1] = ("%s(engine=%s,lua=%s)"):format(k, tostring(engine), tostring(lua)) end
            end
        end
        return bad
    end
    -- `in_` must be offered, `out` must not
    local function check_state(label, in_, out, player, npc, list_too)
        local got = set_of(offered())
        local wrong = {}
        for _, k in ipairs(in_ or {}) do if not got[k] then wrong[#wrong + 1] = "missing " .. k end end
        for _, k in ipairs(out or {}) do if got[k] then wrong[#wrong + 1] = "unexpected " .. k end end
        T.eq(label .. ": offered as expected", wrong, {})
        T.eq(label .. ": the engine agrees with the Lua evaluation", mismatches(), {})
        if list_too then
            local want = offered()
            T.eq(label .. ": the conversation lists the same options", converse(player, npc), want)
        end
    end

    local function q() return risen.quest.get(QUEST) end
    local function quest_status() local qq = q() return qq and qq:get_property("Status") or "missing" end
    local function settled()
        T.wait_until(function() return risen.entity.get(OWNER) ~= nil and risen.entity.get(VICTIM) ~= nil and risen.entity.player() ~= nil end, 90, "the NPCs exist")
        T.sleep(60)
    end
    local function plan_as_expected()
        local bad = {}
        for _, k in ipairs(keys()) do
            local o = OPT[k]
            local want = #o.expect.engine + (o.expect.lua and 1 or 0)
            local n = o.info and o.info:condition_count() or -1
            if n ~= want then bad[#bad + 1] = ("%s: %d conditions, expected %d"):format(k, n, want) end
            if o.when ~= nil and o.engine == nil then
                local d = risen.dialog.describe("DC_" .. k)
                local classes = {}
                for _, text in ipairs(d and d.engine or {}) do classes[#classes + 1] = text:match("^(%w+)") end
                if join(classes) ~= join(o.expect.engine) then bad[#bad + 1] = ("%s: engine part %s, expected %s"):format(k, join(classes), join(o.expect.engine)) end
                if (d and d.lua ~= nil) ~= o.expect.lua then bad[#bad + 1] = ("%s: lua part %s, expected %s"):format(k, tostring(d and d.lua), tostring(o.expect.lua)) end
            end
        end
        return bad
    end

    -- ---- phase save ------------------------------------------------------------------------------------------------------------------------------
    if phase == "save" then
        T.plan { "options: all created", "plan: every option compiled as expected", "plan: risen.dialog.compile describes the engine and Lua parts",
                 "start: quest status", "start: offered as expected", "start: the engine agrees with the Lua evaluation", "start: the conversation lists the same options",
                 "ore 3: offered as expected", "ore 3: the engine agrees with the Lua evaluation", "quest running: offered as expected", "quest running: the engine agrees with the Lua evaluation",
                 "variables: offered as expected", "variables: the engine agrees with the Lua evaluation", "variables: the conversation lists the same options",
                 "ore taken: offered as expected", "ore taken: the engine agrees with the Lua evaluation", "skill up: offered as expected", "skill up: the engine agrees with the Lua evaluation",
                 "guild Pir: offered as expected", "guild Pir: the engine agrees with the Lua evaluation", "npc apple: offered as expected", "npc apple: the engine agrees with the Lua evaluation",
                 "deaths: offered as expected", "deaths: the engine agrees with the Lua evaluation",
                 "quest Open: engine == Lua", "quest Running: engine == Lua", "quest Success: engine == Lua", "quest Failed: engine == Lua", "quest Cancelled: engine == Lua", "quest Lost: engine == Lua",
                 "quest Won: engine == Lua", "quest Obsolete: engine == Lua",
                 "quest statuses: each native status selects exactly its option",
                 "script condition: not polled", "script condition: one call per look", "script condition: switches", "script condition: gets the NPC and the player",
                 "mixed: engine conditions first, the script is not called while they are false", "mixed: called when they hold", "mixed: the answer follows the script",
                 "conversation: the script was asked when the list was built", "conversation: lists what the script says",
                 "error: the condition counts as false", "error: asked again, no crash", "error: the other options still work",
                 "bad specs: when = 5", "bad specs: unknown engine class", "bad specs: unknown engine property", "bad specs: unknown skill", "bad specs: refused ones leave no info",
                 "bad specs: an engine property that is not taken hides the option", "bad specs: that option exists but is never offered",
                 "final: offered as expected", "final: the engine agrees with the Lua evaluation", "final: the conversation lists the same options", "saved" }
        T.run(function()
            T.wait_event("session.start", 180, function(m) return m == "new" end)
            settled()
            local player = assert(risen.entity.player())
            local npc = assert(risen.entity.get(OWNER))
            local victim = assert(risen.entity.get(VICTIM))
            local x, y, z = player:position()
            local corpse = assert(risen.npc.spawn("Peasant_1_01", x + 150, y + 20, z + 150, { name = CORPSE, peaceful = true }))
            T.sleep(30)
            local STR = assert(risen.item.skill_id("gESkill_Stat_STR"))

            local made = 0
            for _, k in ipairs(keys()) do if OPT[k].info then made = made + 1 end end
            T.eq("options: all created", made, #keys())
            T.eq("plan: every option compiled as expected", plan_as_expected(), {})
            local plan = assert(risen.dialog.compile(C.all { C.item(ORE, 3), C.quest(QUEST, "Running"), C.any { C.event("A"), C.var("B") } }))
            T.eq("plan: risen.dialog.compile describes the engine and Lua parts",
                { #plan.engine, plan.engine[1]:match("^%w+"), plan.engine[2]:match("^%w+"), plan.lua }, { 2, "ItemAmount", "QuestStatus", "any(game event A is set, var B is set)" })

            -- ---- state: nothing ----
            risen.item.take(player, ORE, 99)
            risen.npc.set_skill_base(player, STR, 10)
            T.sleep(10)
            T.expect("start: quest status", quest_status():find("^gEQuestStatus_") ~= nil, quest_status())
            check_state("start", { "NoOre", "NoFlag", "Guild", "Alive", "StrLow", "Q_None", "Near" },
                { "Item3", "NpcApple", "Flag", "VarGE2", "Str", "GuildPir", "Dead", "DeadP", "Q_Running", "Q_Success", "Q_Missing", "Combo1", "Combo2", "Mixed", "Nested", "AndBase" }, player, npc, true)

            -- ---- 3 ore ----
            risen.item.give(player, ORE, 3)
            T.sleep(10)
            check_state("ore 3", { "Item3", "Guild", "Alive" }, { "NoOre", "Combo1", "AndBase", "Mixed" })

            -- ---- the quest runs ----
            risen.quest.open(QUEST)
            risen.quest.run(QUEST)
            T.sleep(10)
            check_state("quest running", { "Item3", "Q_Running", "Combo1", "AndBase" }, { "NoOre", "Q_Open", "Q_Success", "Mixed", "Flag" })

            -- ---- a game event (what SetGameEvent makes) and a variable ----
            risen.gamevar.set("DC_Flag", 0)
            risen.gamevar.set("DC_Count", 2)
            T.sleep(10)
            check_state("variables", { "Item3", "Flag", "VarGE2", "Combo2", "Mixed", "Nested" }, { "NoFlag", "GuildPir" }, player, npc, true)

            -- ---- the ore is taken away: the item options switch, Mixed (engine conditions first) is no longer offered ----
            risen.item.take(player, ORE, 3)
            T.sleep(10)
            check_state("ore taken", { "NoOre", "Flag", "VarGE2", "Combo2", "Nested" }, { "Item3", "Combo1", "AndBase", "Mixed" })
            risen.item.give(player, ORE, 3)

            -- ---- skill ----
            risen.npc.set_skill_base(player, STR, 30)
            T.sleep(10)
            check_state("skill up", { "Str", "Item3", "Q_Running" }, { "StrLow" })
            risen.npc.set_skill_base(player, STR, 10)
            T.sleep(10)
            T.expect("skill up: restored", set_of(offered())["StrLow"], join(offered()))
            risen.npc.set_skill_base(player, STR, 30)

            -- ---- the owner's guild changes (Lua condition) ----
            npc:set_guild(risen.npc.guilds.Pir)
            T.sleep(10)
            check_state("guild Pir", { "GuildPir", "Combo2" }, { "Guild" })
            npc:set_guild(risen.npc.guilds.Cit)

            -- ---- an item of the NPC ----
            risen.item.give(npc, "It_Apple", 1)
            T.sleep(10)
            check_state("npc apple", { "NpcApple", "Guild" }, { "GuildPir" })

            -- ---- deaths: the corpse (engine NPCStatus), the persistent victim (Lua, record) ----
            risen.script.call("Kill", player, corpse, 0)
            risen.script.call("Kill", player, victim, 0)
            T.sleep(60)
            check_state("deaths", { "Dead", "DeadP" }, { "Alive" })

            -- ---- every quest status: the six native ones and the Lua ones answer the same as Lua ----
            local statuses = { "Open", "Running", "Success", "Failed", "Cancelled", "Lost", "Won", "Obsolete" }
            local picked = {}
            for _, s in ipairs(statuses) do
                q():set_property("Status", "gEQuestStatus_" .. s)
                T.sleep(3)
                local got = offered({ "Q_Open", "Q_Running", "Q_Success", "Q_Failed", "Q_Cancelled", "Q_Lost", "Q_Won", "Q_Obsolete", "Q_Any" })
                local own = {}
                for _, k in ipairs(got) do if k ~= "Q_Any" then own[#own + 1] = k end end
                picked[s] = own
                T.eq("quest " .. s .. ": engine == Lua", mismatches(), {})
            end
            T.eq("quest statuses: each native status selects exactly its option",
                { picked.Open, picked.Running, picked.Success, picked.Failed, picked.Cancelled, picked.Lost, picked.Won, picked.Obsolete },
                { { "Q_Open" }, { "Q_Running" }, { "Q_Success" }, { "Q_Failed" }, { "Q_Cancelled" }, { "Q_Lost" }, { "Q_Won" }, { "Q_Obsolete" } })
            q():set_property("Status", "gEQuestStatus_Running")
            T.sleep(5)

            -- ---- the Lua script condition is counted ----
            local c0 = calls.fn
            T.sleep(150)
            T.eq("script condition: not polled", calls.fn, c0)
            local f = OPT.FnFlag.info
            c0 = calls.fn
            local a1 = f:available()
            local a2 = f:conditions_fulfilled()
            T.eq("script condition: one call per look", calls.fn - c0, 2)
            flag.fn = true
            T.eq("script condition: switches", { a1, a2, f:available() }, { false, false, true })
            T.eq("script condition: gets the NPC and the player", { flag.npc, flag.player }, { OWNER, true })
            flag.fn = false

            -- ---- engine conditions first: the script is not asked while they are false ----
            local m = OPT.Mixed.info
            risen.item.take(player, ORE, 3)
            c0 = calls.mixed
            local m1 = m:available()
            T.eq("mixed: engine conditions first, the script is not called while they are false", { calls.mixed - c0, m1 }, { 0, false })
            risen.item.give(player, ORE, 3)
            c0 = calls.mixed
            local m2 = m:available()
            T.eq("mixed: called when they hold", { calls.mixed - c0, m2 }, { 1, true })
            flag.mixed = false
            T.eq("mixed: the answer follows the script", m:available(), false)
            flag.mixed = true

            -- ---- a conversation asks once when it builds its list ----
            flag.fn = true
            c0 = calls.fn
            local list = converse(player, npc)
            local asked = calls.fn - c0
            risen.log.info("dialogcond: the conversation asked the predicate " .. asked .. " time(s)")
            T.expect("conversation: the script was asked when the list was built", asked >= 1, asked)
            T.expect("conversation: lists what the script says", set_of(list)["FnFlag"], join(list))
            flag.fn = false

            -- ---- a predicate that raises an error: a warning once, false, no crash ----
            c0 = calls.err
            local e1 = OPT.FnError.info:available()
            T.eq("error: the condition counts as false", { e1, calls.err - c0 }, { false, 1 })
            local e2 = OPT.FnError.info:available()
            T.eq("error: asked again, no crash", { e2, calls.err - c0 }, { false, 2 })
            T.expect("error: the other options still work", set_of(offered())["Item3"], join(offered()))

            -- ---- bad specs ----
            local lines = { { speaker = "player", listener = "npc", text = "x" } }
            ---@diagnostic disable-next-line: assign-type-mismatch
            local r1, why1 = risen.dialog.create({ name = "DC_BadWhen", owner = OWNER, when = 5, lines = lines })
            T.expect("bad specs: when = 5", r1 == nil and tostring(why1):find("expected a condition"), why1)
            ---@diagnostic disable-next-line: assign-type-mismatch
            local r2, why2 = risen.dialog.create({ name = "DC_BadClass", owner = OWNER, engine = { { class = "Nope" } }, lines = lines })
            T.expect("bad specs: unknown engine class", r2 == nil and tostring(why2):find("unknown class 'Nope'"), why2)
            local r3, why3 = risen.dialog.create({ name = "DC_BadProp", owner = OWNER, engine = { { class = "ItemAmount", Bogus = "1" } }, lines = lines })
            T.expect("bad specs: unknown engine property", r3 == nil and tostring(why3):find("no property 'Bogus'"), why3)
            local ok4, err4 = pcall(C.skill, "NoSuchSkill", 1)
            T.expect("bad specs: unknown skill", not ok4 and tostring(err4):find("unknown skill 'NoSuchSkill'"), err4)
            T.eq("bad specs: refused ones leave no info", { risen.dialog.get("DC_BadWhen"), risen.dialog.get("DC_BadClass"), risen.dialog.get("DC_BadProp") }, { nil, nil, nil })
            local r5, why5 = risen.dialog.create({ name = "DC_BadAttach", owner = OWNER, engine = { { class = "ItemAmount", ItemAmount = "abc" } }, lines = lines })
            T.expect("bad specs: an engine property that is not taken hides the option", r5 == nil and tostring(why5):find("did not take"), why5)
            local bad = risen.dialog.get("DC_BadAttach")
            T.expect("bad specs: that option exists but is never offered", bad ~= nil and bad:available() == false, bad and tostring(bad:available()))

            -- ---- the final state, saved ----
            -- ore 3, quest running, event + variable, skill, apple on the NPC, corpse and victim dead, guild Cit
            T.sleep(10)
            local final_in = { "Item3", "Flag", "VarGE2", "Str", "Guild", "NpcApple", "Dead", "DeadP", "Q_Running", "Combo1", "Combo2", "Mixed", "Nested", "AndBase", "Near" }
            check_state("final", final_in, { "NoOre", "NoFlag", "StrLow", "GuildPir", "Alive", "Q_Open", "Q_Success", "Q_Missing", "FnFlag", "FnError" }, player, npc, true)
            T.wait_until(function() return risen.world.is_save_allowed() end, 20, "saving allowed")
            local saved = false
            risen.on("save.after", function(_, ok) saved = ok end)
            risen.world.quick_save()
            T.wait_until(function() return saved end, 60, "savegame written")
            T.expect("saved", saved)
            T.sleep(60)
        end, "dialogcond")
        return
    end

    -- ---- phase load ------------------------------------------------------------------------------------------------------------------------------
    T.plan { "load: fresh process loaded the savegame", "load: every option exists with its conditions", "load: the quest is running", "load: offered as expected", "load: the engine agrees with the Lua evaluation",
             "load: the conversation lists the same options", "load: the persistent victim is dead (Lua condition)", "load: the corpse is gone (engine condition false)",
             "switch: ore taken", "switch: ore taken, the engine agrees", "switch: ore back", "switch: ore back, the engine agrees", "switch: the conversation follows",
             "switch: variable removed", "switch: variable removed, the engine agrees", "switch: skill down", "switch: skill down, the engine agrees",
             "script: not asked while the engine conditions fail", "script: asked when they hold" }
    T.run(function()
        local mode = T.wait_event("session.start", 180, function(m) return m == "load" end)
        T.expect("load: fresh process loaded the savegame", mode == "load", tostring(mode))
        settled()
        local player = assert(risen.entity.player())
        local npc = assert(risen.entity.get(OWNER))
        local STR = assert(risen.item.skill_id("gESkill_Stat_STR"))

        T.eq("load: every option exists with its conditions", plan_as_expected(), {})
        T.expect("load: the quest is running", quest_status():find("Running") ~= nil, quest_status())
        local final_in = { "Item3", "Flag", "VarGE2", "Str", "Guild", "NpcApple", "DeadP", "Q_Running", "Combo1", "Combo2", "Mixed", "Nested", "AndBase", "Near" }
        local final_out = { "NoOre", "NoFlag", "StrLow", "GuildPir", "Q_Open", "Q_Success", "Q_Missing", "FnFlag", "FnError", "Alive", "Dead" }
        local got = set_of(offered())
        local wrong = {}
        for _, k in ipairs(final_in) do if not got[k] then wrong[#wrong + 1] = "missing " .. k end end
        for _, k in ipairs(final_out) do if got[k] then wrong[#wrong + 1] = "unexpected " .. k end end
        T.eq("load: offered as expected", wrong, {})
        T.eq("load: the engine agrees with the Lua evaluation", mismatches(), {})
        T.eq("load: the conversation lists the same options", converse(player, npc), offered())
        T.expect("load: the persistent victim is dead (Lua condition)", C.test(C.dead(VICTIM)) == true and got.DeadP, join(offered()))
        T.expect("load: the corpse is gone (engine condition false)", not got.Dead and not got.Alive, join(offered()))

        -- the state is switched again in the loaded game
        risen.item.take(player, ORE, 3)
        T.sleep(10)
        local g = set_of(offered())
        T.expect("switch: ore taken", not g.Item3 and not g.Combo1 and not g.Mixed and not g.AndBase and g.NoOre, join(offered()))
        T.eq("switch: ore taken, the engine agrees", mismatches(), {})
        risen.item.give(player, ORE, 3)
        T.sleep(10)
        g = set_of(offered())
        T.expect("switch: ore back", g.Item3 and g.Combo1 and g.Mixed and g.AndBase and not g.NoOre, join(offered()))
        T.eq("switch: ore back, the engine agrees", mismatches(), {})
        T.eq("switch: the conversation follows", converse(player, npc), offered())

        risen.gamevar.remove("DC_Flag")
        risen.gamevar.set("DC_Count", 0)
        T.sleep(10)
        g = set_of(offered())
        T.expect("switch: variable removed", not g.Flag and g.NoFlag and not g.VarGE2 and not g.Combo2 and not g.Mixed, join(offered()))
        T.eq("switch: variable removed, the engine agrees", mismatches(), {})
        risen.npc.set_skill_base(player, STR, 10)
        T.sleep(10)
        g = set_of(offered())
        T.expect("switch: skill down", not g.Str and g.StrLow, join(offered()))
        T.eq("switch: skill down, the engine agrees", mismatches(), {})

        -- the script condition of Mixed: not asked while the engine conditions (ore, event) fail, asked when they hold
        local c0 = calls.mixed
        OPT.Mixed.info:available()
        T.eq("script: not asked while the engine conditions fail", calls.mixed - c0, 0)
        risen.gamevar.set("DC_Flag", 0)
        c0 = calls.mixed
        T.expect("script: asked when they hold", OPT.Mixed.info:available() and calls.mixed - c0 == 1, calls.mixed - c0)
    end, "dialogcond")
end
