-- Live check of example_fetch_pickup (only loaded in a test run).
-- The player's pick-up itself cannot be performed by a script, so the items arrive through risen.item.give: the poll in the mod counts the
-- inventory whatever the source (the `item.picked_up` handler is the instant path for a real pick-up by a human player).
local T = risen.test

return function(NPC, QUEST, ITEM, WANTED, collected)
    T.plan { "NPC exists", "quest started by the dialogue", "berries were scattered", "no progress before collecting", "progress is logged",
             "quest still running after one berry", "quest completes by collecting", "reward given", "berries are kept" }

    local function status() return risen.quest.get(QUEST) and risen.quest.get(QUEST):get_property("Status") or "missing" end

    T.run(function()
        T.wait_event("session.start", 180, function(m) return m == "new" end)
        T.wait_until(function() return risen.entity.get(NPC) ~= nil end, 30, "the scout was spawned")
        T.sleep(60)
        local player = assert(risen.entity.player())
        local scout = assert(risen.entity.get(NPC))
        T.expect("NPC exists", scout:valid())

        risen.dialog.start(player, scout)
        T.sleep(90)
        assert(risen.dialog.get("EX_Scout_Work")):run()
        T.wait_event("info.finished", 60, function(name) return name == "EX_Scout_Work" end)
        T.sleep(60)
        T.expect("quest started by the dialogue", risen.quest.is_running(QUEST), status())

        -- the berries lying around the scout
        local near = 0
        for _, e in ipairs(risen.template.get(ITEM):instances(5000) or {}) do
            if (e:distance_to(scout) or 1e9) < 600 then near = near + 1 end
        end
        T.expect("berries were scattered", near >= WANTED, near .. " berries within 6 m of the scout")

        T.sleep(60)
        T.eq("no progress before collecting", collected(), 0)
        local logs = risen.quest.get(QUEST):log_count()
        local potions = risen.item.count(player, "It_Po_Health_01")

        risen.item.give(player, ITEM, 1)                       -- stand-in for picking up / looting one berry
        T.sleep(60)
        T.expect("progress is logged", risen.quest.get(QUEST):log_count() == logs + 1, ("log entries %d -> %d"):format(logs, risen.quest.get(QUEST):log_count()))
        T.expect("quest still running after one berry", risen.quest.is_running(QUEST), status())

        risen.item.give(player, ITEM, WANTED - 1)
        T.wait_until(function() return status():find("Success") ~= nil end, 20, "quest completed by collecting")
        T.expect("quest completes by collecting", status():find("Success") ~= nil, status())
        T.eq("reward given", risen.item.count(player, "It_Po_Health_01"), potions + 2)
        T.eq("berries are kept", collected(), WANTED)
    end, "example_fetch_pickup")
end
