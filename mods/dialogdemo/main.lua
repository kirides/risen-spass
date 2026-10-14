-- "Gus's errand": a complete story built only from Lua.
--   * SPASS_Gus is spawned next to the player (~5 s into a game session)
--   * option 1 ("Do you have work?") is always offered until it was used; it starts the quest and writes a log entry
--   * option 2 ("I'm done") is only offered while the quest is *running*; it completes the quest and pays the reward
-- The demo also drives the conversation itself (talk, choose, talk again) so it can run unattended.
local log = risen.log

local NAME, QUEST = "SPASS_Gus", "Q_SPASS_Gus"
risen.loc.set("FO_" .. NAME, "Gus the Talkative")

-- Effects of the dialogue lines: plain Lua scripts referenced by name from the info commands
local quest_started_at, quest_finished_at = nil, nil
local now = 0
risen.script.define("SPASS_Gus_StartQuest", function()
    quest_started_at = now
    log.info("dialogdemo: [effect] starting the quest: open=" .. tostring(risen.quest.open(QUEST)) .. " run=" .. tostring(risen.quest.run(QUEST)))
    log.info("dialogdemo: [effect] log entry added = " .. tostring(risen.quest.add_log(QUEST, "Gus wants you to come back and tell him you are done.")))
    return 0
end)
risen.script.define("SPASS_Gus_FinishQuest", function()
    quest_finished_at = now
    log.info("dialogdemo: [effect] finishing the quest: succeed=" .. tostring(risen.quest.succeed(QUEST)))
    local player = risen.entity.player()
    if player then log.info("dialogdemo: [effect] reward -> " .. tostring(risen.item.give(player, "It_1H_Machete", 1))) end
    return 0
end)

local frames, gus, intro, finish = 0, nil, nil, nil
local second_started, second_chosen, final_logged = false, false, false

local function describe_options(label)
    local opts, names = risen.dialog.available(), {}
    for _, o in ipairs(opts) do names[#names + 1] = tostring(o:name()) end
    log.info(("dialogdemo [%s]: running=%s options(%d): %s"):format(label, tostring(risen.dialog.is_running()), #opts, table.concat(names, ", ")))
end

risen.on("tick", function()
    frames = frames + 1
    now = frames

    if frames == 300 and not risen.entity.get(NAME) then
        local player = risen.entity.player()
        if not player then return end
        local x, y, z = player:position()
        gus = risen.npc.spawn("Peasant_1_01", x + 150, y + 20, z + 150, { name = NAME, level = 3, hp_max = 300 })
        if not gus then return end

        risen.quest.define({ name = QUEST, title = "Gus's Errand", type = "Report", xp = 100 })

        intro = risen.dialog.create({
            name = "SPASS_Gus_Work", owner = NAME,
            lines = {
                { speaker = "player", listener = "npc", text = "Do you have any work for me?" },
                { speaker = "npc", listener = "player", text = "Funny you ask. Walk around a bit, then come back and tell me you are done." },
                { script = "SPASS_Gus_StartQuest" },
            },
        })
        finish = risen.dialog.create({
            name = "SPASS_Gus_Done", owner = NAME,
            condition = "Running", quest = QUEST,            -- only offered while the quest is running
            lines = {
                { speaker = "player", listener = "npc", text = "I walked around. I am done." },
                { speaker = "npc", listener = "player", text = "Excellent. Here is your reward." },
                { script = "SPASS_Gus_FinishQuest" },
            },
        })
        log.info(("dialogdemo: created intro=%s finish=%s (finish Type=%s ConditionType=%s Quest=%s)"):format(tostring(intro), tostring(finish),
            tostring(finish and finish:get_property("Type")), tostring(finish and finish:get_property("ConditionType")), tostring(finish and finish:get_property("Quest"))))

    elseif frames == 600 and gus then
        log.info("dialogdemo: dialog.start -> " .. tostring(risen.dialog.start(risen.entity.player(), gus)))
    elseif frames == 660 and gus then
        describe_options("before the quest")
    elseif frames == 720 and intro then
        log.info("dialogdemo: choosing option 1 -> " .. tostring(intro:run()))

    -- second conversation: 90 frames after the first one's effect script ran (its lines are finished by then)
    elseif quest_started_at and not second_started and frames >= quest_started_at + 90 and gus then
        second_started = true
        local q = risen.quest.get(QUEST)
        log.info(("dialogdemo: quest running=%s log_entries=%s"):format(tostring(risen.quest.is_running(QUEST)), tostring(q and q:log_count())))
        log.info("dialogdemo: dialog.start (2nd) -> " .. tostring(risen.dialog.start(risen.entity.player(), gus)))
    elseif second_started and not second_chosen and frames >= quest_started_at + 150 and finish then
        second_chosen = true
        describe_options("while the quest runs")
        log.info("dialogdemo: choosing option 2 -> " .. tostring(finish:run()))
    elseif quest_finished_at and not final_logged and frames >= quest_finished_at + 60 then
        final_logged = true
        local q = risen.quest.get(QUEST)
        log.info(("dialogdemo: final: quest Status=%s, machetes=%s"):format(tostring(q and q:get_property("Status")),
            tostring(risen.item.count(risen.entity.player(), "It_1H_Machete"))))    end
end)
