-- Live check of example_kill_quest (only loaded in a test run).
-- A script cannot make the player fight. The engine's own `Kill` script (killer = self, victim = other) calls the quest manager exactly like a
-- death in combat does (`npc.killed` fires with the victim) and takes the victim's hit points to 0, so it stands in for the fight here. A real
-- fight (a pirate against a wolf) fires the same event -- seen live while writing this example, but its AI is too erratic for a regression test
-- (the wolves "threaten" for a long time).
local T = risen.test

return function(NPC, WOLVES, BOSS_QUEST, BOSS, WANTED)
    T.plan { "NPC exists", "a clone of Wolf does not count as Wolf", "wolf quest started", "wolf kills counted", "wolf quest succeeded", "wolf reward",
             "boss option unlocked by the first quest", "boss is a Spass_Dire_Wolf", "boss quest running", "boss quest succeeded", "boss reward" }

    local function status(q) return risen.quest.get(q) and risen.quest.get(q):get_property("Status") or "missing" end
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
        T.wait_until(function() return risen.entity.get(NPC) ~= nil end, 30, "Garrick was spawned")
        T.sleep(60)
        local player = assert(risen.entity.player())
        local garrick = assert(risen.entity.get(NPC))
        T.expect("NPC exists", garrick:valid())
        local x, y, z = garrick:position()
        x, y, z = x or 0, y or 0, z or 0

        converse(player, garrick, "EX_Garrick_Wolves")
        T.expect("wolf quest started", risen.quest.is_running(WOLVES), status(WOLVES))

        -- a clone of Wolf is a different template: its death must not count as a wolf
        local px, py, pz = x + 1500, y + 20, z
        kill(assert(risen.npc.spawn("Spass_Dire_Wolf", px, py, pz, { name = "T_Dire", hp_max = 20 })))
        T.eq("a clone of Wolf does not count as Wolf", risen.save.get("wolf_kills", 0), 0)

        -- three wolves
        for i = 1, WANTED do
            local wolf = assert(risen.npc.spawn("Wolf", px + 60 * i, py, pz, { name = "T_Wolf" .. i, hp_max = 20 }))
            kill(wolf)
            kill(wolf)   -- the engine may report a death twice: it must count once
        end
        T.wait_until(function() return status(WOLVES):find("Success") ~= nil end, 20, "wolf quest succeeded")
        T.eq("wolf kills counted", risen.save.get("wolf_kills", 0), WANTED)
        T.expect("wolf quest succeeded", status(WOLVES):find("Success") ~= nil, status(WOLVES))
        local gold = risen.item.money(player)
        T.expect("wolf reward", gold >= 150, "gold " .. gold)

        -- the second quest is offered only now
        T.sleep(60)
        risen.dialog.start(player, garrick)
        T.sleep(90)
        local offered = {}
        for _, info in ipairs(risen.dialog.available()) do offered[#offered + 1] = info:name() end
        T.expect("boss option unlocked by the first quest", table.concat(offered, ","):find("EX_Garrick_Boss", 1, true) ~= nil, table.concat(offered, ","))
        assert(risen.dialog.get("EX_Garrick_Boss")):run()
        T.wait_event("info.finished", 60, function(name) return name == "EX_Garrick_Boss" end)
        T.sleep(60)
        T.expect("boss quest running", risen.quest.is_running(BOSS_QUEST), status(BOSS_QUEST))

        local boss = assert(risen.entity.get(BOSS))
        local tpl = boss:template()
        T.eq("boss is a Spass_Dire_Wolf", { tpl and tpl:name(), boss:hp_max() }, { "Spass_Dire_Wolf", 600 })
        kill(boss)
        T.wait_until(function() return status(BOSS_QUEST):find("Success") ~= nil end, 150, "boss quest succeeded")
        T.expect("boss quest succeeded", status(BOSS_QUEST):find("Success") ~= nil, status(BOSS_QUEST))
        T.expect("boss reward", risen.item.money(player) >= gold + 500, "gold " .. risen.item.money(player))
    end, "example_kill_quest")
end
