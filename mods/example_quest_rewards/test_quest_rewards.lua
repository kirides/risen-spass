-- Live check of example_quest_rewards (only loaded in a test run): complete the quest, then save and load in the same process.
local T = risen.test

return function(NPC, GUARD, QUEST, GAMEVAR, stats)
    T.plan { "Mara exists", "quest running", "trigger ran once", "gold paid", "guard spawned", "game variable set", "unlocked option offered",
             "saved", "loaded", "quest still succeeded after load", "trigger did not run again", "gold not paid twice", "guard respawned at the same spot",
             "game variable survived", "unlocked option offered after load", "risen.save flag survived" }

    local function status() return risen.quest.get(QUEST) and risen.quest.get(QUEST):get_property("Status") or "missing" end
    local function offered()
        local names = {}
        for _, info in ipairs(risen.dialog.available()) do names[#names + 1] = info:name() end
        return table.concat(names, ",")
    end
    -- open the conversation and choose an option; the option ends with an `End` command (an open conversation blocks saving)
    local function converse(player, npc, info_name)
        risen.dialog.start(player, npc)
        T.sleep(90)
        assert(risen.dialog.get(info_name), info_name):run()
        T.wait_event("info.finished", 60, function(name) return name == info_name end)
        T.sleep(60)
    end

    T.run(function()
        T.wait_event("session.start", 180, function(m) return m == "new" end)
        T.wait_until(function() return risen.entity.get(NPC) ~= nil end, 30, "Mara was spawned")
        T.sleep(60)
        local player = assert(risen.entity.player())
        local mara = assert(risen.entity.get(NPC))
        T.expect("Mara exists", mara:valid())

        converse(player, mara, "EX_Mara_Work")
        T.expect("quest running", risen.quest.is_running(QUEST), status())
        local gold = risen.item.money(player)
        converse(player, mara, "EX_Mara_Finish")
        T.wait_until(function() return status():find("Success") ~= nil end, 30, "quest succeeded")
        T.sleep(30)

        T.eq("trigger ran once", stats.triggered, 1)
        T.eq("gold paid", risen.item.money(player), gold + 200)
        local guard = risen.entity.get(GUARD)
        T.expect("guard spawned", guard ~= nil, "EX_Guard")
        local gx, gz
        if guard then local px, _, pz = guard:position(); gx, gz = px, pz end
        T.eq("game variable set", risen.gamevar.get(GAMEVAR), 1)
        risen.dialog.start(player, mara)
        T.sleep(90)
        T.expect("unlocked option offered", offered():find("EX_Mara_Thanks", 1, true) ~= nil, offered())
        assert(risen.dialog.get("EX_Mara_Thanks")):run()          -- ends the conversation (its last line is `End`)
        T.wait_event("info.finished", 60, function(name) return name == "EX_Mara_Thanks" end)
        T.sleep(120)

        -- save, then load: runtime entities are gone, quest state and variables come back from the savegame
        local gold_saved = risen.item.money(player)
        T.wait_until(function() return risen.world.is_save_allowed() end, 20, "saving allowed")
        local saved = false
        risen.on("save.after", function(_, ok) saved = ok end)
        risen.world.quick_save()
        T.wait_until(function() return saved end, 60, "savegame written")
        T.expect("saved", saved)
        T.sleep(100)
        risen.world.quick_load()
        local mode = T.wait_event("session.start", 120, function(m) return m == "load" end)
        T.expect("loaded", mode == "load", tostring(mode))
        T.wait_until(function() return risen.entity.get(NPC) ~= nil and risen.entity.get(GUARD) ~= nil end, 30, "Mara and the guard were respawned")
        T.sleep(100)

        player = assert(risen.entity.player())
        T.expect("quest still succeeded after load", status():find("Success") ~= nil, status())
        T.eq("trigger did not run again", stats.triggered, 1)
        T.eq("gold not paid twice", risen.item.money(player), gold_saved)
        local guard2 = risen.entity.get(GUARD)
        local x2, z2
        if guard2 then local px, _, pz = guard2:position(); x2, z2 = px, pz end
        T.expect("guard respawned at the same spot", guard2 ~= nil and gx ~= nil and x2 ~= nil and math.abs((x2 or 0) - (gx or 0)) < 5 and math.abs((z2 or 0) - (gz or 0)) < 5,
            ("%s,%s -> %s,%s"):format(tostring(gx), tostring(gz), tostring(x2), tostring(z2)))
        T.eq("game variable survived", risen.gamevar.get(GAMEVAR), 1)
        T.eq("risen.save flag survived", risen.save.get("rewarded"), true)
        local mara2 = assert(risen.entity.get(NPC))
        risen.dialog.start(player, mara2)
        T.sleep(90)
        T.expect("unlocked option offered after load", offered():find("EX_Mara_Thanks", 1, true) ~= nil, offered())
        assert(risen.dialog.get("EX_Mara_Thanks")):run()
        T.wait_event("info.finished", 60, function(name) return name == "EX_Mara_Thanks" end)
        T.sleep(120)
    end, "example_quest_rewards")
end
