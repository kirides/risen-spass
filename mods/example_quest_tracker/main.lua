-- EXAMPLE 8: one quest-tracker pattern for the three ways a quest completes -- by dialogue, by item pickup, by kills.
--
-- The quartermaster Bram hands out three tasks. Each task is ONE line in the objective table below; tracker.lua (read it, it is short) feeds the
-- objectives from the engine's events and completes the quests:
--
--   Q_EX_Report  "talk"   completes when the dialogue option `EX_Bram_Report` finished playing             (event info.finished)
--   Q_EX_Apples  "items"  completes when the player carries 2 more apples than when the task started       (event inventory.changed)
--   Q_EX_Boars   "kills"  completes after 2 boars died                                                       (event entity.died)
--
-- What you learn
--   * The tracker is the answer to "which events do I need?": dialogue -> `info.finished(info_name, owner)`, kills -> `entity.died(entity, killer, cause)` /
--     `entity.defeated(entity, attacker)` (knocked out humans), items -> `inventory.changed(owner, template, delta, total, cause, other)` for every source
--     (pickup, loot, trade, gifts); nothing is polled. All three feed the same `advance(quest, n)`, which writes progress to risen.save and succeeds the quest.
--   * A reward for ANY of them hangs on `on_complete` (or on `quest.status_changed`, see example_quest_rewards): the completion path does not matter.
--   * Progress persists with the savegame (risen.save), the quest status with the engine, runtime entities (Bram) are respawned.
--
-- Run the live check (about 1.5 minutes; see docs/EXAMPLES.md):
--   .\scripts\test-run.ps1 -Instance 3 -Mods example_quest_tracker -WaitForResult -Seconds 300 -Scenario example_quest_tracker -ShowChecks

local tracker = require("tracker")

local NPC = "EX_Bram"
risen.loc.set("FO_" .. NPC, "Bram the Quartermaster")

-- ---- the objectives: HOW each quest completes -----------------------------------------------------------------------------------------
tracker.add { quest = "Q_EX_Report", kind = "talk",  info = "EX_Bram_Report" }
tracker.add { quest = "Q_EX_Apples", kind = "items", item = "It_Apple", count = 2 }
tracker.add { quest = "Q_EX_Boars",  kind = "kills", template = "Boar", count = 2 }

-- ---- reactions: log lines for progress, a reward for completion ---------------------------------------------------------------------------
tracker.on_progress(function(quest, n, wanted)
    if n < wanted then risen.quest.add_log(quest, ("Progress: %d/%d"):format(n, wanted)) end
end)
tracker.on_complete(function(quest)
    local player = risen.entity.player()
    if player then risen.item.give(player, "It_Gold", 50) end
    risen.quest.add_log(quest, "Done. Bram pays 50 gold.")
end)

-- ---- dialogue: three options start the tasks, one is the "talk" objective ------------------------------------------------------------------
for quest, script in pairs({ Q_EX_Report = "EX_Bram_StartReport", Q_EX_Apples = "EX_Bram_StartApples", Q_EX_Boars = "EX_Bram_StartBoars" }) do
    risen.script.define(script, function()
        tracker.start(quest)
        risen.quest.add_log(quest, "Bram gave you a task.")
        return 0
    end)
end

local function define_content()
    risen.quest.define({ name = "Q_EX_Report", title = "Bram: Report", type = "Report", xp = 20 })
    risen.quest.define({ name = "Q_EX_Apples", title = "Bram: Two Apples", type = "Report", xp = 20 })
    risen.quest.define({ name = "Q_EX_Boars", title = "Bram: Two Boars", type = "Kill", xp = 20 })

    local function task(info, quest, script, question, answer)
        risen.dialog.create({
            name = info, owner = NPC,
            lines = {
                { speaker = "player", listener = "npc", text = question },
                { speaker = "npc", listener = "player", text = answer },
                { script = script },
                { command = "End" },
            },
        })
    end
    task("EX_Bram_AskReport", "Q_EX_Report", "EX_Bram_StartReport", "Do you have a task that needs only talking?", "Tell me you were here. Then come back.")
    task("EX_Bram_AskApples", "Q_EX_Apples", "EX_Bram_StartApples", "Do you need supplies?", "Two apples, from wherever you get them.")
    task("EX_Bram_AskBoars", "Q_EX_Boars", "EX_Bram_StartBoars", "Anything to hunt?", "Two boars are eating the camp's cabbages.")
    -- the "talk" objective: nothing in this option completes the quest, the tracker does when it sees info.finished for it
    risen.dialog.create({
        name = "EX_Bram_Report", owner = NPC, condition = "Running", quest = "Q_EX_Report",
        lines = {
            { speaker = "player", listener = "npc", text = "I am here, as promised." },
            { speaker = "npc", listener = "player", text = "Good enough." },
            { command = "End" },
        },
    })
end

-- ---- Bram ------------------------------------------------------------------------------------------------------------------------------------
local function ensure_npc()
    if risen.entity.get(NPC) then return end
    local pos = risen.save.get("bram_pos")
    if not pos then
        local player = risen.entity.player()
        if not player then return end
        local x, y, z = player:position()
        if not x then return end
        pos = { x + 150, y + 20, z + 150 }
        risen.save.set("bram_pos", pos)
    end
    risen.npc.spawn("Peasant_1_01", pos[1], pos[2], pos[3], { name = NPC, peaceful = true, level = 3, hp_max = 300 })
end

risen.on("load.before", define_content)
risen.on("session.start", function()
    define_content()
    local waited, handle = 0, nil
    handle = risen.on("tick", function()
        waited = waited + 1
        if waited == 60 then
            ensure_npc()
            if handle then risen.off(handle) end
        end
    end)
end)

-- the live check belongs to the test run only
if risen.test.is_active() then
    require("test_quest_tracker")(NPC, tracker)
end
