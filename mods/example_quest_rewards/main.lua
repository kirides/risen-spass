-- EXAMPLE 7: things that happen when a quest completes -- and stay correct across saving and loading.
--
-- Mara gives a small errand. The moment the quest succeeds (however it was completed: this example uses a dialogue, but the trigger does not
-- care) a `quest.status_changed` handler
--   * pays 200 gold,
--   * spawns a guard next to Mara,
--   * sets a game variable (saved by the engine, readable by other mods and dialogue conditions),
-- and a dialogue option that is only available after the quest succeeded appears (`condition = "Success"`: the engine unlocks it).
--
-- What you learn
--   * `quest.status_changed(quest, status, previous)` fires for EVERY quest of the game; compare the name. Statuses: "Open", "Running",
--     "Success", "Failed", ...
--   * The trigger must be idempotent: the status is restored when a savegame is loaded and a mod must not pay twice. Remember "already
--     done" in risen.save (written next to every savegame, restored with it) -- check the flag, set it, then act.
--   * What persists: quest status and log (engine), game variables (engine), `risen.save` data (this layer), dialogue "used" flags (engine).
--     Entities spawned from Lua are saved by the engine too, but without their unique name: Mara and the guard are risen.npc.persistent
--     NPCs (found by entity ID and named again after a load). The guard is declared when the trigger runs and again on every session.start
--     of a savegame whose `rewarded` flag is set.
--   * Items already given are part of the savegame's inventory (do not give them again), so only the trigger needs the flag.
--
-- Run the live check (about 1.5 minutes: it saves and loads in one process; see docs/EXAMPLES.md):
--   .\scripts\test-run.ps1 -Instance 2 -Mods example_quest_rewards -WaitForResult -Seconds 300 -Scenario example_quest_rewards -ShowChecks

local NPC, GUARD, QUEST = "EX_Mara", "EX_Guard", "Q_EX_Rewards"
local GAMEVAR = "EX_Mara_Done"

local stats = { triggered = 0, handler_calls = 0 }   -- for the test: how often did the trigger really act?

-- ---- the guard: a persistent NPC that only exists once the quest paid out ---------------------------------------------------------------
-- Declared while the game runs, it is created at once (next to the player at that moment); declared again on every session.start of a
-- savegame that paid already, it is found and named again.
local function declare_guard()
    return risen.npc.persistent({
        name = GUARD, template = "Peasant_1_02", offset = { 300, 20, 150 }, label = "Mara's Guard", peaceful = true, level = 3, hp_max = 300,
    })
end

-- ---- the trigger -------------------------------------------------------------------------------------------------------------------
local function on_quest_completed()
    stats.handler_calls = stats.handler_calls + 1
    if risen.save.get("rewarded") then return end         -- already paid in this savegame (also true right after a load)
    risen.save.set("rewarded", true)
    stats.triggered = stats.triggered + 1

    local player = risen.entity.player()
    if player then risen.item.give(player, "It_Gold", 200) end
    risen.gamevar.set(GAMEVAR, 1)
    declare_guard()
    risen.quest.add_log(QUEST, "Mara paid you and sent a guard to watch the beach.")
end

risen.on("quest.status_changed", function(quest, status)
    if quest == QUEST and status == "Success" then on_quest_completed() end
end)

-- ---- dialogue ---------------------------------------------------------------------------------------------------------------------
risen.script.define("EX_Mara_Start", function()
    risen.quest.open(QUEST)
    risen.quest.run(QUEST)
    risen.quest.add_log(QUEST, "Mara wants you to tell her when you have seen the sea.")
    return 0
end)
risen.script.define("EX_Mara_Finish", function()
    risen.quest.succeed(QUEST)                            -- everything else is the trigger's business
    return 0
end)

local function define_content()
    risen.quest.define({ name = QUEST, title = "Mara's Errand", type = "Report", xp = 60 })
    risen.dialog.create({
        name = "EX_Mara_Work", owner = NPC,
        lines = {
            { speaker = "player", listener = "npc", text = "Anything I can do for you?" },
            { speaker = "npc", listener = "player", text = "Look at the sea and come back." },
            { script = "EX_Mara_Start" },
            { command = "End" },
        },
    })
    risen.dialog.create({
        name = "EX_Mara_Finish", owner = NPC, condition = "Running", quest = QUEST,
        lines = {
            { speaker = "player", listener = "npc", text = "I have seen the sea." },
            { speaker = "npc", listener = "player", text = "Wonderful. Thank you." },
            { script = "EX_Mara_Finish" },
            { command = "End" },
        },
    })
    -- the unlocked option: offered by the engine once the quest succeeded, before and after a load
    risen.dialog.create({
        name = "EX_Mara_Thanks", owner = NPC, condition = "Success", quest = QUEST, permanent = true,
        lines = {
            { speaker = "player", listener = "npc", text = "Is the guard behaving?" },
            { speaker = "npc", listener = "player", text = "He is. Thanks again." },
            { command = "End" },
        },
    })
end

-- ---- Mara: declared once; `define` runs on load.before (before the engine reads quests and dialogue flags) and on every session.start ----
risen.npc.persistent({
    name = NPC, template = "Peasant_1_01", offset = { 150, 20, 150 }, label = "Mara the Fisher",
    peaceful = true, level = 3, hp_max = 300, define = define_content,
})
risen.on("session.start", function()
    if risen.save.get("rewarded") then declare_guard() end   -- risen.save is empty in a new game, restored with a savegame
end)

-- the live check belongs to the test run only
if risen.test.is_active() then
    require("test_quest_rewards")(NPC, GUARD, QUEST, GAMEVAR, stats)
end
