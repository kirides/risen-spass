-- Live test mod of M7 "persistent NPCs" (scenario `persistnpc`, scripts/smoke-test.ps1; the readable example is mods/example_custom_npc).
--
-- Four NPCs, each of its own template so the scenario can count the live entities of one NPC:
--   PN_Hob      a quest giver (dialogue + quest), items, peaceful guild
--   PN_Victim   killed by the scenario: stays dead (respawn = false)
--   PN_Phoenix  killed by the scenario: replaced at the next load (respawn = true)
--   PN_Wanderer moved by the scenario: its position must survive a save
-- The phase (risen.test.param "phase") changes what this process declares:
--   save / load      the four NPCs
--   load_changed     PN_Hob with version = 2 (replaced), PN_Wanderer no longer declared (removed)
--   load_missing     the NPC templates do not exist (the mod "lost" its content): nothing may break
local phase = risen.test.param("phase", "save")

local QUEST = "Q_PN_Hob"

if phase ~= "load_missing" then
    risen.balance.install({
        new_npcs = {
            Spass_PN_Hob = { base = "Peasant_1_01", name = "Hob the Persistent", level = 4, hp_max = 321, guild = "Cit" },
            Spass_PN_Victim = { base = "Peasant_1_02", name = "Victim", level = 2, hp_max = 100, guild = "Cit" },
            Spass_PN_Phoenix = { base = "Peasant_1_03", name = "Phoenix", level = 2, hp_max = 100, guild = "Cit" },
            Spass_PN_Wanderer = { base = "Peasant_1_01", name = "Wanderer", level = 2, hp_max = 100, guild = "Cit" },
        },
    })
end

risen.script.define("PN_Hob_StartQuest", function()
    risen.quest.open(QUEST)
    risen.quest.run(QUEST)
    risen.quest.add_log(QUEST, "Hob asked you to remember him.")
    return 0
end)
risen.script.define("PN_Hob_Finish", function()
    risen.quest.succeed(QUEST)
    return 0
end)

local function define_content()
    risen.quest.define({ name = QUEST, title = "Hob's Request", type = "Report", xp = 10 })
    risen.dialog.create({
        name = "PN_Hob_Work", owner = "PN_Hob",
        lines = {
            { speaker = "player", listener = "npc", text = "Do you have work for me?" },
            { speaker = "npc", listener = "player", text = "Remember my name." },
            { script = "PN_Hob_StartQuest" },
            { command = "End" },
        },
    })
    risen.dialog.create({
        name = "PN_Hob_Done", owner = "PN_Hob", condition = "Running", quest = QUEST,
        lines = {
            { speaker = "player", listener = "npc", text = "You are Hob." },
            { speaker = "npc", listener = "player", text = "Good." },
            { script = "PN_Hob_Finish" },
            { command = "End" },
        },
    })
end

ready_reasons = ready_reasons or {}
local function note(name) return function(_, reason) ready_reasons[#ready_reasons + 1] = name .. ":" .. reason end end

local hob_version = phase == "load_changed" and 2 or 1
risen.npc.persistent({
    name = "PN_Hob", template = "Spass_PN_Hob", offset = { 200, 20, 0 }, version = hob_version, label = "Hob the Persistent",
    peaceful = true, items = { { "It_Apple", 3 } }, define = define_content, on_ready = note("PN_Hob"),
})
risen.npc.persistent({ name = "PN_Victim", template = "Spass_PN_Victim", offset = { -200, 20, 0 }, peaceful = true, on_ready = note("PN_Victim") })
risen.npc.persistent({ name = "PN_Phoenix", template = "Spass_PN_Phoenix", offset = { 0, 20, 200 }, peaceful = true, respawn = true, on_ready = note("PN_Phoenix") })
if phase ~= "load_changed" then
    risen.npc.persistent({ name = "PN_Wanderer", template = "Spass_PN_Wanderer", offset = { 0, 20, -200 }, peaceful = true, on_ready = note("PN_Wanderer") })
end

if risen.test.is_active() then
    require("test_persistnpc")(phase, QUEST)
end
