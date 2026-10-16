-- Live check of example_spawn_when (only loaded in a test run).
local T = risen.test

return function(ELDER, MESSENGER, SMITH, QUEST, GONE_EVENT, smith_log)
    T.plan { "new game: only the elder", "new game: both rules wait", "quest started: the messenger appears", "quest started: only one", "quest started: the smith still waits",
             "quest finished: the messenger is gone", "quest finished: the smith appears", "quest finished: exactly one smith", "quest finished: on_fire says condition", "the smith talks",
             "saved after the quest", "load: the smith is restored (same entity), not made twice", "load: the messenger stays away", "load: nothing fired again",
             "the smith leaves after the dialogue choice", "the smith is gone", "the smith's rule remembers", "saved after he left", "load: he stays gone", "load: still no messenger",
             "a new game: elder only again" }

    local function count(tpl)
        local t = risen.template.get(tpl)
        local list = t and t:instances(100)
        return list and #list or -1
    end
    local function counts() return table.concat({ count("Spass_EX_Elder"), count("Spass_EX_Messenger"), count("Spass_EX_Smith") }, ",") end
    local function converse(player, npc, info_name)
        risen.dialog.start(player, npc)
        T.sleep(90)
        assert(risen.dialog.get(info_name), info_name):run()
        T.wait_event("info.finished", 60, function(name) return name == info_name end)
        T.sleep(60)
    end
    local function rule(name) return risen.spawn.get(name) end
    local function settled()
        T.wait_until(function() return rule(MESSENGER):state() ~= "pending" and rule(SMITH):state() ~= "pending" and risen.entity.get(ELDER) ~= nil end, 60, "the rules are evaluated")
        T.sleep(60)
    end
    local function save_game()
        T.wait_until(function() return risen.world.is_save_allowed() end, 20, "saving allowed")
        local saved = false
        risen.on("save.after", function(_, ok) saved = ok end)
        risen.world.quick_save()
        T.wait_until(function() return saved end, 60, "savegame written")
        return saved
    end
    local function load_game()
        risen.world.quick_load()
        T.wait_event("session.start", 120, function(m) return m == "load" end)
        settled()
    end

    T.run(function()
        T.wait_event("session.start", 180, function(m) return m == "new" end)
        settled()
        local player = assert(risen.entity.player())
        local elder = assert(risen.entity.get(ELDER))
        T.eq("new game: only the elder", counts(), "1,0,0")
        T.eq("new game: both rules wait", { rule(MESSENGER):state(), rule(SMITH):state() }, { "waiting", "waiting" })

        converse(player, elder, "EX_Elder_Work")
        T.wait_until(function() return rule(MESSENGER):state() == "present" end, 20, "the messenger appears")
        T.sleep(45)
        T.eq("quest started: the messenger appears", { rule(MESSENGER):state(), risen.entity.get(MESSENGER) ~= nil }, { "present", true })
        T.eq("quest started: only one", counts(), "1,1,0")
        T.eq("quest started: the smith still waits", rule(SMITH):state(), "waiting")

        converse(player, elder, "EX_Elder_Done")
        T.wait_until(function() return rule(SMITH):state() == "present" and rule(MESSENGER):state() == "waiting" end, 20, "the smith appears, the messenger leaves")
        T.sleep(45)
        T.eq("quest finished: the messenger is gone", { rule(MESSENGER):state(), risen.entity.get(MESSENGER) }, { "waiting", nil })
        T.eq("quest finished: the smith appears", risen.entity.get(SMITH) ~= nil, true)
        T.eq("quest finished: exactly one smith", counts(), "1,0,1")
        T.eq("quest finished: on_fire says condition", smith_log, { "fire:condition" })
        local smith = assert(risen.entity.get(SMITH))
        local smith_id = smith:id()
        converse(player, smith, "EX_Smith_Thanks")
        T.expect("the smith talks", true)

        T.expect("saved after the quest", save_game())
        T.sleep(60)
        load_game()
        T.eq("load: the smith is restored (same entity), not made twice", { counts(), (risen.entity.get(SMITH) or { id = function() end }):id() == smith_id }, { "1,0,1", true })
        T.eq("load: the messenger stays away", rule(MESSENGER):state(), "waiting")
        T.eq("load: nothing fired again", smith_log, { "fire:condition" })

        player = assert(risen.entity.player())
        converse(player, assert(risen.entity.get(SMITH)), "EX_Smith_Leave")
        T.wait_until(function() return rule(SMITH):state() == "gone" end, 20, "the smith leaves")
        T.sleep(45)
        T.eq("the smith leaves after the dialogue choice", smith_log, { "fire:condition", "leave:condition" })
        T.eq("the smith is gone", { counts(), risen.entity.get(SMITH) }, { "1,0,0", nil })
        T.eq("the smith's rule remembers", { rule(SMITH):fired(), rule(SMITH):state() }, { 1, "gone" })

        T.expect("saved after he left", save_game())
        T.sleep(60)
        load_game()
        T.eq("load: he stays gone", { counts(), rule(SMITH):state() }, { "1,0,0", "gone" })
        T.eq("load: still no messenger", rule(MESSENGER):state(), "waiting")

        risen.world.new_game()
        T.wait_event("session.start", 180, function(m) return m == "new" end)
        settled()
        T.eq("a new game: elder only again", { counts(), rule(SMITH):state(), rule(SMITH):fired() }, { "1,0,0", "waiting", 0 })
    end, "example_spawn_when")
end
