-- EXAMPLE 12: NPCs that appear (and disappear) when something happens: a quest, a dialogue choice, ... -- and stay correct across saving and loading.
--
-- The elder gives an errand. While it runs a messenger stands next to him; the moment it is finished the messenger is gone and a smith appears who thanks you, who
-- has a conversation of his own and who leaves for good when you ask him to.
--
--   EX_Elder      always there (risen.npc.persistent): gives and takes the errand
--   EX_Messenger  risen.spawn.when, tracking (`once = false`): exists exactly while the quest RUNS; the rule follows the condition in both directions
--   EX_Smith      risen.spawn.when, one-shot: appears when the quest SUCCEEDED; the dialogue option "Time to go" sets a game event (SetGameEvent) that makes him leave: `despawn_when`
--
-- What you learn
--   * risen.spawn.when{ ...the options of risen.npc.persistent..., when = <condition>, despawn_when = <condition>, once = true | false } declares an NPC that does not exist
--     until its condition holds. Conditions are built with risen.cond: quest(name, status), item(template, count), var / event (game variables), dead / alive (an NPC),
--     fn(predicate), combined with all / any / negate. A plain function is a condition too.
--   * Nothing is polled: a condition names the events that can change it (quest.status_changed, inventory.changed, gamevar.changed, entity.died ...) and the rule is looked at on the next frame.
--   * The layer remembers per savegame (risen.save) whether a rule fired: a savegame made before the quest has the smith waiting, one made after has him standing there once
--     (never twice), one made after he left keeps him gone -- and a one-shot rule never fires again in that savegame. After every load each rule is looked at once, so
--     a condition that changed behind the layer's back is caught up.
--   * `define` of a rule's NPC runs on load.before and every session.start, also while the NPC does not exist: its dialogue options and the quest are there when the engine reads a savegame.
--   * Dialogue commands (`SetGameEvent`) and scripts (risen.quest.succeed) change the game like any other code: the rules react to the real thing.
--
-- Run the live check (about 2 minutes: it saves and loads in one process; see docs/EXAMPLES.md):
--   .\scripts\test-run.ps1 -Instance 2 -Mods example_spawn_when -WaitForResult -Seconds 300 -Scenario example_spawn_when -ShowChecks

local ELDER, MESSENGER, SMITH, QUEST = "EX_Elder", "EX_Messenger", "EX_Smith", "Q_EX_Spawn"
local GONE_EVENT = "EX_Smith_Leaves"

risen.balance.install({
    new_npcs = {
        Spass_EX_Elder = { base = "Peasant_1_01", name = "Elder Orin", level = 5, hp_max = 300, guild = "Cit" },
        Spass_EX_Messenger = { base = "Peasant_1_03", name = "Messenger", level = 2, hp_max = 100, guild = "Cit" },
        Spass_EX_Smith = { base = "Peasant_1_02", name = "Smith Bram", level = 5, hp_max = 300, guild = "Cit" },
    },
})

-- ---- the quest and the conversations ---------------------------------------------------------------------------------------------
risen.script.define("EX_Elder_Start", function()
    risen.quest.open(QUEST)
    risen.quest.run(QUEST)
    risen.quest.add_log(QUEST, "Elder Orin wants to hear that you are ready.")
    return 0
end)
risen.script.define("EX_Elder_Finish", function()
    risen.quest.succeed(QUEST)      -- everything else is the rules' business
    return 0
end)

local function define_content()
    risen.quest.define({ name = QUEST, title = "The Elder's Errand", type = "Report", xp = 40 })
    risen.dialog.create({
        name = "EX_Elder_Work", owner = ELDER, permanent = true,
        lines = {
            { speaker = "player", listener = "npc", text = "Do you have work for me?" },
            { speaker = "npc", listener = "player", text = "Tell me when you are ready. A messenger will wait for your word." },
            { script = "EX_Elder_Start" },
            { command = "End" },
        },
    })
    risen.dialog.create({
        name = "EX_Elder_Done", owner = ELDER, condition = "Running", quest = QUEST, permanent = true,
        lines = {
            { speaker = "player", listener = "npc", text = "I am ready." },
            { speaker = "npc", listener = "player", text = "Then the smith will come and thank you." },
            { script = "EX_Elder_Finish" },
            { command = "End" },
        },
    })
    -- the smith's own options: they belong to an NPC that may not exist right now, which is fine
    risen.dialog.create({
        name = "EX_Smith_Thanks", owner = SMITH, permanent = true,
        lines = {
            { speaker = "player", listener = "npc", text = "Who are you?" },
            { speaker = "npc", listener = "player", text = "Bram. The elder sent me to thank you." },
            { command = "End" },
        },
    })
    risen.dialog.create({
        name = "EX_Smith_Leave", owner = SMITH, permanent = true,
        lines = {
            { speaker = "player", listener = "npc", text = "Time to go." },
            { speaker = "npc", listener = "player", text = "Farewell." },
            { command = "SetGameEvent", GameEvent = GONE_EVENT },     -- the despawn condition below is this game event
            { command = "End" },
        },
    })
end

-- ---- the NPCs ---------------------------------------------------------------------------------------------------------------------
risen.npc.persistent({
    name = ELDER, template = "Spass_EX_Elder", offset = { 150, 20, 150 }, label = "Elder Orin", peaceful = true, define = define_content,
})

-- tracking rule: exists while the quest runs; made again if the quest is started again (e.g. after a load of an earlier savegame), removed when it is not running
risen.spawn.when({
    name = MESSENGER, template = "Spass_EX_Messenger", offset = { 150, 20, -150 }, label = "Messenger", peaceful = true,
    once = false, when = risen.cond.quest(QUEST, "Running"),
})

-- one-shot rule: fires once when the quest has succeeded; leaves for good when the dialogue option set the game event
local smith_log = {}
risen.spawn.when({
    name = SMITH, template = "Spass_EX_Smith", offset = { -150, 20, 150 }, label = "Smith Bram", peaceful = true,
    when = risen.cond.quest(QUEST, "Success"), despawn_when = risen.cond.event(GONE_EVENT),
    on_fire = function(rule, why) smith_log[#smith_log + 1] = "fire:" .. why end,        -- why: "condition" (it happened now) | "load" (found true when a game was loaded)
    on_despawn = function(rule, why) smith_log[#smith_log + 1] = "leave:" .. why end,
})

-- the live check belongs to the test run only
if risen.test.is_active() then
    require("test_spawn_when")(ELDER, MESSENGER, SMITH, QUEST, GONE_EVENT, smith_log)
end
