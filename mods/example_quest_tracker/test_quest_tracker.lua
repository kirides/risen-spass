-- Live check of example_quest_tracker (only loaded in a test run). Kills use the game's `Kill` script (it fires entity.died like a death in combat
-- does, see example_kill_quest); items arrive through risen.item.give (inventory.changed counts them).
local T = risen.test

return function(NPC, tracker)
    T.plan { "NPC exists", "talk: started", "talk: not complete before the option", "talk: completed by finishing the option", "items: started",
             "items: progress after one apple", "items: completed by carrying them", "items: apples are kept", "kills: started", "kills: other templates do not count",
             "kills: progress after one boar", "kills: a repeated death counts once", "kills: completed by the second boar", "rewards paid for all three" }

    local function status(q) return risen.quest.get(q) and risen.quest.get(q):get_property("Status") or "missing" end
    local function done(q) return status(q):find("Success") ~= nil end
    local function converse(player, npc, info_name)
        risen.dialog.start(player, npc)
        T.sleep(90)
        assert(risen.dialog.get(info_name), info_name):run()
        T.wait_event("info.finished", 60, function(name) return name == info_name end)
        T.sleep(60)
    end
    local function kill(victim)
        risen.script.call("Kill", risen.entity.player(), victim, 0)
        T.sleep(30)
    end

    T.run(function()
        T.wait_event("session.start", 180, function(m) return m == "new" end)
        T.wait_until(function() return risen.entity.get(NPC) ~= nil end, 30, "Bram was spawned")
        T.sleep(60)
        local player = assert(risen.entity.player())
        local bram = assert(risen.entity.get(NPC))
        T.expect("NPC exists", bram:valid())
        local gold = risen.item.money(player)
        local x, y, z = bram:position()
        x, y, z = x or 0, y or 0, z or 0

        -- 1. completed by a dialogue
        converse(player, bram, "EX_Bram_AskReport")
        T.expect("talk: started", risen.quest.is_running("Q_EX_Report"), status("Q_EX_Report"))
        T.expect("talk: not complete before the option", not done("Q_EX_Report"), status("Q_EX_Report"))
        converse(player, bram, "EX_Bram_Report")
        T.wait_until(function() return done("Q_EX_Report") end, 10, "talk quest completed")
        T.expect("talk: completed by finishing the option", done("Q_EX_Report") and tracker.progress("Q_EX_Report") == 1, status("Q_EX_Report"))

        -- 2. completed by carrying items
        converse(player, bram, "EX_Bram_AskApples")
        T.expect("items: started", risen.quest.is_running("Q_EX_Apples"), status("Q_EX_Apples"))
        risen.item.give(player, "It_Apple", 1)
        T.sleep(60)
        T.expect("items: progress after one apple", tracker.progress("Q_EX_Apples") == 1 and not done("Q_EX_Apples"), "progress " .. tracker.progress("Q_EX_Apples"))
        local apples = risen.item.count(player, "It_Apple")
        risen.item.give(player, "It_Apple", 1)
        T.wait_until(function() return done("Q_EX_Apples") end, 10, "items quest completed")
        T.expect("items: completed by carrying them", done("Q_EX_Apples"), status("Q_EX_Apples"))
        T.eq("items: apples are kept", risen.item.count(player, "It_Apple"), apples + 1)

        -- 3. completed by kills
        converse(player, bram, "EX_Bram_AskBoars")
        T.expect("kills: started", risen.quest.is_running("Q_EX_Boars"), status("Q_EX_Boars"))
        kill(assert(risen.npc.spawn("Wolf", x + 1500, y + 20, z, { name = "T_Wolf", hp_max = 20 })))
        T.eq("kills: other templates do not count", tracker.progress("Q_EX_Boars"), 0)
        local boar1 = assert(risen.npc.spawn("Boar", x + 1560, y + 20, z, { name = "T_Boar1", hp_max = 20 }))
        kill(boar1)
        T.eq("kills: progress after one boar", tracker.progress("Q_EX_Boars"), 1)
        kill(boar1)
        T.eq("kills: a repeated death counts once", tracker.progress("Q_EX_Boars"), 1)
        kill(assert(risen.npc.spawn("Boar", x + 1620, y + 20, z, { name = "T_Boar2", hp_max = 20 })))
        T.wait_until(function() return done("Q_EX_Boars") end, 10, "kill quest completed")
        T.expect("kills: completed by the second boar", done("Q_EX_Boars"), status("Q_EX_Boars"))

        T.eq("rewards paid for all three", risen.item.money(player), gold + 150)
    end, "example_quest_tracker")
end
