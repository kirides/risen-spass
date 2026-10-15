-- Live check of example_custom_npc (only loaded in a test run).
local T = risen.test

return function(NPC, QUEST)
    T.plan { "NPC exists", "NPC uses the new template", "NPC label", "options registered", "quest not started yet", "work option starts the quest",
             "quest log entry", "done option offered while running", "done option finishes the quest", "reward item is the new template", "reward numbers" }

    -- open the conversation and choose one option (the way a player would), wait until its lines finished
    local function converse(player, npc, info_name)
        risen.dialog.start(player, npc)
        T.sleep(90)
        local info = assert(risen.dialog.get(info_name), info_name)
        info:run()
        T.wait_event("info.finished", 60, function(name) return name == info_name end)
        T.sleep(60)
    end

    T.run(function()
        T.wait_event("session.start", 180, function(m) return m == "new" end)
        T.wait_until(function() return risen.entity.get(NPC) ~= nil end, 30, "Hob was spawned")
        T.sleep(60)
        local player = assert(risen.entity.player())
        local hob = assert(risen.entity.get(NPC))
        T.expect("NPC exists", hob:valid())
        local tpl = hob:template()
        T.eq("NPC uses the new template", tpl and tpl:name(), "Spass_Hob")
        T.eq("NPC label", hob:focus_name(), "Hob the Tinkerer")
        T.expect("options registered", risen.dialog.get("EX_Hob_Who") ~= nil and risen.dialog.get("EX_Hob_Work") ~= nil and risen.dialog.get("EX_Hob_Done") ~= nil)
        T.eq("quest not started yet", risen.quest.is_running(QUEST), false)

        converse(player, hob, "EX_Hob_Work")
        T.expect("work option starts the quest", risen.quest.is_running(QUEST), tostring(risen.quest.get(QUEST):get_property("Status")))
        T.expect("quest log entry", risen.quest.get(QUEST):log_count() >= 1, "log entries: " .. risen.quest.get(QUEST):log_count())

        risen.dialog.start(player, hob)
        T.sleep(90)
        local offered = {}
        for _, info in ipairs(risen.dialog.available()) do offered[#offered + 1] = info:name() end
        T.expect("done option offered while running", table.concat(offered, ","):find("EX_Hob_Done", 1, true) ~= nil, table.concat(offered, ","))
        risen.dialog.get("EX_Hob_Done"):run()
        T.wait_event("info.finished", 60, function(name) return name == "EX_Hob_Done" end)
        T.sleep(60)
        T.expect("done option finishes the quest", (risen.quest.get(QUEST):get_property("Status") or ""):find("Success") ~= nil,
            tostring(risen.quest.get(QUEST):get_property("Status")))
        T.eq("reward item is the new template", risen.item.count(player, "Spass_Hob_Knife"), 1)
        local knife = risen.template.get("Spass_Hob_Knife")
        T.eq("reward numbers", { knife and knife:get_property("gCItem_PS", "GoldValue"), knife and knife:focus_name() }, { "250", "Hob's Knife" })
    end, "example_custom_npc")
end
