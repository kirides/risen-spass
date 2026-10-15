-- Live check of example_fetch_turnin (only loaded in a test run).
local T = risen.test

return function(NPC, QUEST, ITEM, WANTED, REWARD_GOLD)
    T.plan { "NPC exists", "quest started by the dialogue", "too few items: quest keeps running", "too few items: nothing taken", "too few items: log entry",
             "enough items: quest succeeded", "enough items: items taken", "enough items: reward given" }

    local function status() return risen.quest.get(QUEST) and risen.quest.get(QUEST):get_property("Status") or "missing" end
    local function converse(player, npc, info_name)
        risen.dialog.start(player, npc)
        T.sleep(90)
        assert(risen.dialog.get(info_name), info_name):run()
        T.wait_event("info.finished", 60, function(name) return name == info_name end)
        T.sleep(60)
    end

    T.run(function()
        T.wait_event("session.start", 180, function(m) return m == "new" end)
        T.wait_until(function() return risen.entity.get(NPC) ~= nil end, 30, "Hanna was spawned")
        T.sleep(60)
        local player = assert(risen.entity.player())
        local hanna = assert(risen.entity.get(NPC))
        T.expect("NPC exists", hanna:valid())

        converse(player, hanna, "EX_Hanna_Work")
        T.expect("quest started by the dialogue", risen.quest.is_running(QUEST), status())

        -- one berry is not enough
        local gold_before = risen.item.money(player)
        risen.item.give(player, ITEM, 1)
        local berries = risen.item.count(player, ITEM)           -- the new game may start with berries in the pocket: work relative to that
        local logs_before = risen.quest.get(QUEST):log_count()
        converse(player, hanna, "EX_Hanna_TurnIn")
        T.expect("too few items: quest keeps running", risen.quest.is_running(QUEST), status())
        T.eq("too few items: nothing taken", risen.item.count(player, ITEM), berries)
        -- a quest-bound option also makes the engine append the speakers' labels, so "more entries than before" is the check
        T.expect("too few items: log entry", risen.quest.get(QUEST):log_count() > logs_before,
            ("log entries %d -> %d"):format(logs_before, risen.quest.get(QUEST):log_count()))

        -- now enough
        risen.item.give(player, ITEM, WANTED - 1)
        local before = risen.item.count(player, ITEM)
        converse(player, hanna, "EX_Hanna_TurnIn")
        T.expect("enough items: quest succeeded", status():find("Success") ~= nil, status())
        T.eq("enough items: items taken", risen.item.count(player, ITEM), before - WANTED)
        T.eq("enough items: reward given", risen.item.money(player), gold_before + REWARD_GOLD)
    end, "example_fetch_turnin")
end
