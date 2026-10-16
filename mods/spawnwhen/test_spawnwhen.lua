-- Live scenario `spawnwhen` of mods/spawnwhen (only loaded in a test run). One game process per phase (scripts/smoke-test.ps1):
--   save_before   new game: nothing of the rules exists, a real quest completion (the dialogue) creates the reward NPC, a new game removes it again, quick save "A" (nothing fired)
--   main          a fresh process loads A: still nothing (earlier save, fresh process); then every kind of trigger in turn, quick save A2 -> triggers -> quick load A2 (earlier save,
--                 same process) -> triggers again -> quick save B -> quick load B (later save, same process); leaves B on disk
--   load_after    a fresh process loads B: all four NPCs exist exactly once and the rules remember; then the despawn conditions
--   load_changed  a fresh process loads B with edited rules: a condition that is false now despawns, a new rule whose condition already holds fires at the load
local T = risen.test

return function(phase, QUEST, rules)
    local TOKEN = "Spass_SW_Token"
    local TEMPLATE = { Reward = "Spass_SW_Reward", Guard = "Spass_SW_Guard", Visitor = "Spass_SW_Visitor", Avenger = "Spass_SW_Avenger", Late = "Spass_SW_Late",
                       Giver = "Spass_SW_Giver", Victim = "Spass_SW_Victim" }
    local ORDER = { "Reward", "Guard", "Visitor", "Avenger" }

    local spawned, varevents = {}, {}
    risen.on("entity.spawned", function(_, tpl) spawned[tpl] = (spawned[tpl] or 0) + 1 end)
    risen.on("gamevar.changed", function(name, value, previous) varevents[#varevents + 1] = ("%s:%s:%s"):format(name, tostring(value), tostring(previous)) end)

    local function count(key)
        local t = risen.template.get(TEMPLATE[key])
        local list = t and t:instances(200)
        return list and #list or -1
    end
    local function counts(keys)
        local out = {}
        for _, k in ipairs(keys or ORDER) do out[#out + 1] = count(k) end
        return table.concat(out, ",")
    end
    local function states(keys)
        local out = {}
        for _, k in ipairs(keys or ORDER) do out[#out + 1] = rules[k]:state() end
        return table.concat(out, ",")
    end
    local function status() local q = risen.quest.get(QUEST) return q and q:get_property("Status") or "missing" end
    local function converse(player, npc, info_name)
        risen.dialog.start(player, npc)
        T.sleep(90)
        assert(risen.dialog.get(info_name), info_name):run()
        T.wait_event("info.finished", 60, function(name) return name == info_name end)
        T.sleep(60)
    end
    local function settled(keys)
        T.wait_until(function()
            for _, k in ipairs(keys or ORDER) do if rules[k]:state() == "pending" then return false end end
            return risen.entity.get("SW_Giver") ~= nil and risen.entity.get("SW_Victim") ~= nil
        end, 60, "the rules are evaluated after the load")
        T.sleep(60)
    end
    local function wait_state(key, want, label)
        T.wait_until(function() return rules[key]:state() == want end, 30, label or (key .. " becomes " .. want))
        T.sleep(45)
    end
    local function save_game()
        T.wait_until(function() return risen.world.is_save_allowed() end, 20, "saving allowed")
        local saved = false
        risen.on("save.after", function(_, ok) saved = ok end)
        risen.world.quick_save()
        T.wait_until(function() return saved end, 60, "savegame written")
        return saved
    end
    local function ids()
        local t = {}
        for _, k in ipairs(ORDER) do local e = risen.entity.get("SW_" .. k) t[k] = e and e:id() or false end
        return t
    end
    local function records() return risen.save.get("spawn_rules") or {} end
    local function trigger_all(player)
        -- every kind of trigger without the dialogues (they were driven once): the quest through its API, items, a variable, a death
        risen.quest.open(QUEST) risen.quest.run(QUEST) risen.quest.succeed(QUEST)
        risen.item.give(player, TOKEN, 3)
        risen.gamevar.set("SW_Open", 1)
        risen.script.call("Kill", player, assert(risen.entity.get("SW_Victim")), 0)
    end

    -- ---- phase save_before ---------------------------------------------------------------------------------------------------------------
    if phase == "save_before" then
        T.plan { "new game: the giver exists", "new game: no rule NPC exists", "new game: every rule is waiting", "new game: nothing is remembered yet", "new game: quest not started",
                 "dialogue: the quest runs", "dialogue: the quest succeeded", "quest completion: the reward NPC appears", "quest completion: exactly one", "quest completion: its label",
                 "quest completion: the others did not fire", "quest completion: on_fire says condition", "quest completion: the rule is remembered",
                 "a new game removes the reward NPC again", "a new game: every rule waits again", "a new game: the quest is gone", "a new game: nothing is remembered", "saved (A)" }
        T.run(function()
            T.wait_event("session.start", 180, function(m) return m == "new" end)
            settled()
            local player = assert(risen.entity.player())
            local giver = assert(risen.entity.get("SW_Giver"))
            T.expect("new game: the giver exists", giver:valid() and count("Giver") == 1)
            T.eq("new game: no rule NPC exists", counts(), "0,0,0,0")
            T.eq("new game: every rule is waiting", states(), "waiting,waiting,waiting,waiting")
            T.eq("new game: nothing is remembered yet", next(records()), nil)
            T.eq("new game: quest not started", risen.quest.is_running(QUEST), false)

            converse(player, giver, "SW_Work")
            T.expect("dialogue: the quest runs", risen.quest.is_running(QUEST), status())
            converse(player, giver, "SW_Done")
            T.expect("dialogue: the quest succeeded", status():find("Success") ~= nil, status())
            wait_state("Reward", "present", "the reward NPC appears")
            T.eq("quest completion: the reward NPC appears", states(), "present,waiting,waiting,waiting")
            T.eq("quest completion: exactly one", counts(), "1,0,0,0")
            local reward = risen.entity.get("SW_Reward")
            T.eq("quest completion: its label", reward and reward:focus_name(), "The Reward")
            T.eq("quest completion: the others did not fire", { rules.Guard:fired(), rules.Visitor:fired(), rules.Avenger:fired() }, { 0, 0, 0 })
            T.eq("quest completion: on_fire says condition", fires, { "SW_Reward:condition" })
            T.eq("quest completion: the rule is remembered", { records().SW_Reward and records().SW_Reward.state, records().SW_Reward and records().SW_Reward.spawns }, { "present", 1 })

            risen.world.new_game()
            T.wait_event("session.start", 180, function(m) return m == "new" end)
            settled()
            T.eq("a new game removes the reward NPC again", counts(), "0,0,0,0")
            T.eq("a new game: every rule waits again", states(), "waiting,waiting,waiting,waiting")
            T.eq("a new game: the quest is gone", risen.quest.is_running(QUEST), false)
            T.eq("a new game: nothing is remembered", next(records()), nil)
            T.expect("saved (A)", save_game())
            T.sleep(60)
        end, "spawnwhen")
        return
    end

    -- ---- phase main ----------------------------------------------------------------------------------------------------------------------
    if phase == "main" then
        T.plan { "A (fresh process): loaded", "A: no rule NPC exists", "A: every rule is waiting", "A: nothing is remembered", "A: the giver is back, once",
                 "dialogues: quest completion creates the reward NPC", "tokens: 2 are not enough", "tokens: 3 create the guard", "tokens: more change nothing", "tokens: fewer remove the guard",
                 "tokens: 3 again create the guard (a new body)", "tokens: still one", "variable: the dialogue command SetGameEvent is seen as gamevar.changed", "variable: the visitor appears",
                 "variable: exactly one", "variable: SW_Gone removes the visitor", "variable: removing a variable is reported", "variable: a one-shot rule stays gone", "variable: reset lets it fire again", "death: the avenger appears",
                 "death: not while SW_Peace is set", "all four exist exactly once", "the records say present", "A2: the earlier save removes every rule NPC", "A2: every rule waits again",
                 "A2: nothing is remembered", "A2: the victim lives again", "again: every trigger fires again", "again: one NPC each", "again: spawned once each since the load", "saved (B)",
                 "B (same process): exactly one each, no new spawns", "B: the rules remember", "B: the NPCs are the saved entities", "B: the victim is dead", "B: nothing fired again" }
        T.run(function()
            local mode = T.wait_event("session.start", 180, function(m) return m == "load" end)
            T.expect("A (fresh process): loaded", mode == "load", tostring(mode))
            settled()
            local player = assert(risen.entity.player())
            T.eq("A: no rule NPC exists", counts(), "0,0,0,0")
            T.eq("A: every rule is waiting", states(), "waiting,waiting,waiting,waiting")
            T.eq("A: nothing is remembered", next(records()), nil)
            T.eq("A: the giver is back, once", { count("Giver"), count("Victim") }, { 1, 1 })
            local giver = assert(risen.entity.get("SW_Giver"))

            -- ---- quest -> reward NPC (through the dialogues) ----
            local A2 = save_game()          -- A2: the same state as A (an earlier save inside this process)
            T.sleep(60)
            converse(player, giver, "SW_Work")
            converse(player, giver, "SW_Done")
            wait_state("Reward", "present", "the reward NPC appears")
            T.eq("dialogues: quest completion creates the reward NPC", { counts({ "Reward" }), fires[1] }, { "1", "SW_Reward:condition" })

            -- ---- tracking by items ----
            risen.item.give(player, TOKEN, 2)
            T.sleep(60)
            T.eq("tokens: 2 are not enough", { counts({ "Guard" }), rules.Guard:state() }, { "0", "waiting" })
            risen.item.give(player, TOKEN, 1)
            wait_state("Guard", "present")
            T.eq("tokens: 3 create the guard", { counts({ "Guard" }), rules.Guard:fired() }, { "1", 1 })
            local first_guard = risen.entity.get("SW_Guard"):id()
            risen.item.give(player, TOKEN, 4)
            T.sleep(90)
            T.eq("tokens: more change nothing", { counts({ "Guard" }), risen.entity.get("SW_Guard"):id() == first_guard, rules.Guard:fired() }, { "1", true, 1 })
            risen.item.take(player, TOKEN, 5)       -- 2 left
            wait_state("Guard", "waiting")
            T.eq("tokens: fewer remove the guard", { counts({ "Guard" }), risen.entity.get("SW_Guard") }, { "0", nil })
            risen.item.give(player, TOKEN, 1)
            wait_state("Guard", "present")
            local second = risen.entity.get("SW_Guard")
            T.expect("tokens: 3 again create the guard (a new body)", second and second:id() ~= first_guard and rules.Guard:fired() == 2, tostring(second and second:id()))
            T.eq("tokens: still one", counts({ "Guard" }), "1")

            -- ---- a game variable, set by a real dialogue command ----
            converse(player, giver, "SW_Open")
            T.expect("variable: the dialogue command SetGameEvent is seen as gamevar.changed", table.concat(varevents, ","):find("SW_Open:0:nil", 1, true) ~= nil, ("events [%s] exists=%s value=%s"):format(table.concat(varevents, ","), tostring(risen.gamevar.exists("SW_Open")), tostring(risen.gamevar.get("SW_Open"))))
            wait_state("Visitor", "present", "the visitor appears")
            T.eq("variable: the visitor appears", states({ "Visitor" }), "present")
            T.eq("variable: exactly one", counts({ "Visitor" }), "1")
            risen.gamevar.set("SW_Gone", 1)
            wait_state("Visitor", "gone")
            T.eq("variable: SW_Gone removes the visitor", { counts({ "Visitor" }), despawns[#despawns] }, { "0", "SW_Visitor:condition" })
            risen.gamevar.set("SW_Gone", 0)
            risen.gamevar.remove("SW_Open")
            T.sleep(60)
            T.expect("variable: removing a variable is reported", table.concat(varevents, ","):find("SW_Open:nil:0", 1, true) ~= nil, table.concat(varevents, ","))
            risen.gamevar.set("SW_Open", 1)
            T.sleep(90)
            T.eq("variable: a one-shot rule stays gone", { states({ "Visitor" }), counts({ "Visitor" }), rules.Visitor:fired() }, { "gone", "0", 1 })
            rules.Visitor:reset()
            wait_state("Visitor", "present", "the visitor fires again after reset")
            T.eq("variable: reset lets it fire again", { counts({ "Visitor" }), rules.Visitor:fired() }, { "1", 2 })

            -- ---- a death combined with a variable (all / negate) ----
            risen.gamevar.set("SW_Peace", 1)
            risen.script.call("Kill", player, assert(risen.entity.get("SW_Victim")), 0)
            T.sleep(120)
            T.eq("death: not while SW_Peace is set", { states({ "Avenger" }), counts({ "Avenger" }) }, { "waiting", "0" })
            risen.gamevar.set("SW_Peace", 0)
            wait_state("Avenger", "present", "the avenger appears")
            T.eq("death: the avenger appears", { states({ "Avenger" }), counts({ "Avenger" }) }, { "present", "1" })
            T.eq("all four exist exactly once", counts(), "1,1,1,1")
            T.eq("the records say present", { records().SW_Reward.state, records().SW_Guard.state, records().SW_Visitor.state, records().SW_Avenger.state },
                { "present", "present", "present", "present" })

            -- ---- the earlier save (A2), in this process ----
            risen.world.quick_load()
            T.wait_event("session.start", 120, function(m) return m == "load" end)
            settled()
            T.eq("A2: the earlier save removes every rule NPC", counts(), "0,0,0,0")
            T.eq("A2: every rule waits again", states(), "waiting,waiting,waiting,waiting")
            T.eq("A2: nothing is remembered", next(records()), nil)
            local victim = risen.entity.get("SW_Victim")
            T.expect("A2: the victim lives again", victim and not victim:is_dead())

            -- ---- everything again: the rules fire again, one NPC each ----
            for i = #fires, 1, -1 do fires[i] = nil end
            local before = {}
            for _, k in ipairs(ORDER) do before[k] = spawned[TEMPLATE[k]] or 0 end
            player = assert(risen.entity.player())
            trigger_all(player)
            for _, k in ipairs(ORDER) do wait_state(k, "present", k .. " fires again") end
            T.eq("again: every trigger fires again", states(), "present,present,present,present")
            T.eq("again: one NPC each", counts(), "1,1,1,1")
            local delta = {}
            for _, k in ipairs(ORDER) do delta[#delta + 1] = (spawned[TEMPLATE[k]] or 0) - before[k] end
            T.eq("again: spawned once each since the load", delta, { 1, 1, 1, 1 })

            -- ---- the later save (B), loaded in this process and (by the next phases) in fresh ones ----
            local saved = { ids = ids() }
            risen.save.set("test", saved)
            T.expect("saved (B)", save_game())
            T.sleep(60)
            for _, k in ipairs(ORDER) do before[k] = spawned[TEMPLATE[k]] or 0 end
            risen.world.quick_load()
            T.wait_event("session.start", 120, function(m) return m == "load" end)
            settled()
            delta = {}
            for _, k in ipairs(ORDER) do delta[#delta + 1] = (spawned[TEMPLATE[k]] or 0) - before[k] end
            T.eq("B (same process): exactly one each, no new spawns", { counts(), delta }, { "1,1,1,1", { 0, 0, 0, 0 } })
            T.eq("B: the rules remember", { states(), rules.Reward:fired(), rules.Guard:fired(), rules.Visitor:fired(), rules.Avenger:fired() }, { "present,present,present,present", 1, 1, 1, 1 })
            T.eq("B: the NPCs are the saved entities", ids(), saved.ids)
            T.expect("B: the victim is dead", risen.entity.get("SW_Victim"):is_dead())
            T.eq("B: nothing fired again", #fires, 4)
        end, "spawnwhen")
        return
    end

    -- ---- phase load_after ----------------------------------------------------------------------------------------------------------------
    if phase == "load_after" then
        T.plan { "B (fresh process): loaded", "B: exactly one each", "B: the rules remember", "B: the NPCs are the saved entities", "B: the victim is dead, the avenger stands",
                 "B: nothing fired", "B: fewer tokens remove the guard", "B: SW_Gone removes the visitor", "B: the one-shot reward stays" }
        T.run(function()
            local mode = T.wait_event("session.start", 180, function(m) return m == "load" end)
            T.expect("B (fresh process): loaded", mode == "load", tostring(mode))
            settled()
            local player = assert(risen.entity.player())
            local saved = assert(risen.save.get("test"), "the mod data of the savegame")
            T.eq("B: exactly one each", { counts(), count("Giver"), count("Victim") }, { "1,1,1,1", 1, 1 })
            T.eq("B: the rules remember", { states(), rules.Reward:fired(), rules.Guard:fired(), rules.Visitor:fired(), rules.Avenger:fired() }, { "present,present,present,present", 1, 1, 1, 1 })
            T.eq("B: the NPCs are the saved entities", ids(), saved.ids)
            T.expect("B: the victim is dead, the avenger stands", risen.entity.get("SW_Victim"):is_dead() and not risen.entity.get("SW_Avenger"):is_dead())
            T.eq("B: nothing fired", { #fires, #despawns }, { 0, 0 })
            risen.item.take(player, TOKEN, 1)
            wait_state("Guard", "waiting")
            T.eq("B: fewer tokens remove the guard", { counts({ "Guard" }), despawns[1] }, { "0", "SW_Guard:condition" })
            risen.gamevar.set("SW_Gone", 1)
            wait_state("Visitor", "gone")
            T.eq("B: SW_Gone removes the visitor", { counts({ "Visitor" }), rules.Visitor:fired() }, { "0", 1 })
            T.eq("B: the one-shot reward stays", { counts({ "Reward" }), states({ "Reward" }) }, { "1", "present" })
        end, "spawnwhen")
        return
    end

    -- ---- phase load_changed --------------------------------------------------------------------------------------------------------------
    T.plan { "changed: loaded", "changed: the unchanged rule keeps its NPC", "changed: the guard needs 4 tokens now: removed at the load", "changed: the visitor's new despawn condition holds: removed at the load",
             "changed: the new rule fires at the load", "changed: the avenger is unchanged", "changed: the records follow", "changed: 4 tokens bring the guard back" }
    T.run(function()
        local mode = T.wait_event("session.start", 180, function(m) return m == "load" end)
        T.expect("changed: loaded", mode == "load", tostring(mode))
        local ALL = { "Reward", "Guard", "Visitor", "Avenger", "Late" }
        settled(ALL)
        local player = assert(risen.entity.player())
        local saved = assert(risen.save.get("test"), "the mod data of the savegame")
        T.eq("changed: the unchanged rule keeps its NPC", { counts({ "Reward" }), risen.entity.get("SW_Reward"):id() }, { "1", saved.ids.Reward })
        T.eq("changed: the guard needs 4 tokens now: removed at the load", { states({ "Guard" }), counts({ "Guard" }), despawns[1] }, { "waiting", "0", "SW_Guard:load" })
        T.eq("changed: the visitor's new despawn condition holds: removed at the load", { states({ "Visitor" }), counts({ "Visitor" }) }, { "gone", "0" })
        T.eq("changed: the new rule fires at the load", { states({ "Late" }), counts({ "Late" }), fires }, { "present", "1", { "SW_Late:load" } })
        T.eq("changed: the avenger is unchanged", { counts({ "Avenger" }), risen.entity.get("SW_Avenger"):id() }, { "1", saved.ids.Avenger })
        local r = records()
        T.eq("changed: the records follow", { r.SW_Guard.state, r.SW_Visitor.state, r.SW_Late.state, r.SW_Reward.state }, { "waiting", "gone", "present", "present" })
        risen.item.give(player, TOKEN, 1)
        wait_state("Guard", "present")
        T.eq("changed: 4 tokens bring the guard back", counts({ "Guard" }), "1")
    end, "spawnwhen")
end
