-- EXAMPLE 4: a custom NPC with its own dialogue that gives a quest.
--
-- What you learn
--   * risen.balance.install{ new_npcs = ..., new_items = ... } creates NEW templates (clones of existing ones with other numbers, texts,
--     meshes; the ID derives from the name, so savegames resolve them again). The NPC template carries the label, level and hit points.
--   * risen.npc.persistent{ name, template, offset = {dx, dy, dz} from the player, ... } declares the NPC once. The layer spawns him in a new
--     game and keeps him across save/load (the engine saves spawned entities but not their unique name: the layer finds him again by his
--     entity ID and names him again), no "spawn it again on every session.start" code, no duplicates.
--   * risen.dialog.create adds dialogue options to an NPC (owner = the entity's unique name). A line is either spoken text or an engine
--     command ({ command = "End" }) or a script (risen.script.define'd Lua function that starts/finishes the quest and hands out the reward).
--   * Quests and dialogue options must exist BEFORE the engine reads a savegame (load.before) and again after a new game (session.start);
--     define/create are idempotent, so one function serves both.
--   * `condition = "Running", quest = ...` offers an option only while the quest runs (the engine itself hides it before and after).
--
-- Run the live check (about a minute; see docs/EXAMPLES.md):
--   .\scripts\test-run.ps1 -Instance 2 -Mods example_custom_npc -WaitForResult -Seconds 240 -Scenario example_custom_npc -ShowChecks

local NPC, QUEST = "EX_Hob", "Q_EX_Hob"

-- ---- new templates (applied on the first tick and on every session.start) --------------------------------------------------
risen.balance.install({
    new_items = {
        -- the reward: a cheap machete clone with its own name and numbers
        Spass_Hob_Knife = { base = "It_1H_Machete", name = "Hob's Knife", description = "Hob made it himself.", value = 250, damage = 20 },
    },
    new_npcs = {
        -- the quest giver: a peasant clone with his own label, level and hit points
        Spass_Hob = { base = "Peasant_1_01", name = "Hob the Tinkerer", level = 5, hp_max = 300, guild = "Cit" },
    },
})

-- ---- effects of the dialogue: plain scripts, referenced by name from the lines -----------------------------------------------
risen.script.define("EX_Hob_StartQuest", function()
    risen.quest.open(QUEST)
    risen.quest.run(QUEST)
    risen.quest.add_log(QUEST, "Hob asked you to look around the beach. Report back to him.")
    return 0
end)
risen.script.define("EX_Hob_Reward", function()
    local player = risen.entity.player()
    if player then risen.item.give(player, "Spass_Hob_Knife", 1) end
    risen.quest.add_log(QUEST, "Hob gave you a knife of his own making.")
    risen.quest.succeed(QUEST)
    return 0
end)

-- ---- quest and dialogue options -----------------------------------------------------------------------------------------------
local function define_content()
    risen.quest.define({ name = QUEST, title = "Hob's Errand", type = "Report", xp = 50 })

    -- plain conversation: permanent = true keeps the option after it was used
    risen.dialog.create({
        name = "EX_Hob_Who", owner = NPC, permanent = true,
        lines = {
            { speaker = "player", listener = "npc", text = "Who are you?" },
            { speaker = "npc", listener = "player", text = "Hob. I fix what the sea breaks." },
            { command = "End" },
        },
    })
    -- the quest giver: not permanent, so it disappears once the quest started
    risen.dialog.create({
        name = "EX_Hob_Work", owner = NPC,
        lines = {
            { speaker = "player", listener = "npc", text = "Do you have work for me?" },
            { speaker = "npc", listener = "player", text = "Take a look around the beach and tell me what you saw." },
            { script = "EX_Hob_StartQuest" },
            { command = "End" },
        },
    })
    -- offered only while the quest runs
    risen.dialog.create({
        name = "EX_Hob_Done", owner = NPC, condition = "Running", quest = QUEST,
        lines = {
            { speaker = "player", listener = "npc", text = "I looked around the beach." },
            { speaker = "npc", listener = "player", text = "Good. Here, a knife for your trouble." },
            { script = "EX_Hob_Reward" },
            { command = "End" },
        },
    })
end

-- ---- the NPC itself: declared once; the layer spawns and restores him -----------------------------------------------------------
-- `label` is the name shown when the player looks at him (FO_<name>). `define` runs on load.before and on every session.start: the quest and
-- the dialogue options must exist before the engine restores their saved state, and a new game removes runtime quests.
risen.npc.persistent({
    name = NPC, template = "Spass_Hob", offset = { 150, 20, 150 }, peaceful = true,
    label = "Hob the Tinkerer", define = define_content,
})

-- the live check belongs to the test run only
if risen.test.is_active() then
    require("test_custom_npc")(NPC, QUEST)
end
