-- Live test mod of M8 "conditional spawning" (scenario `spawnwhen`, scripts/smoke-test.ps1; the readable example is mods/example_spawn_when).
--
-- A quest giver (always there) and four NPCs that only exist once a condition holds, one per kind of condition, each of its own template so the scenario can count the live entities:
--   SW_Reward   one-shot: appears when the quest Q_SW_Main succeeds (the player finishes it in the giver's dialogue)
--   SW_Guard    tracking: exists while the player carries 3 SW tokens (a new item), goes away when he carries fewer, comes back
--   SW_Visitor  one-shot: appears when the game event SW_Open is set (the dialogue command SetGameEvent) and leaves for good when the variable SW_Gone is set
--   SW_Avenger  one-shot: appears when SW_Victim is dead (the player kills him) and the variable SW_Peace is not set (all / negate)
-- The phase (risen.test.param "phase") changes what this process declares:
--   save_before / main / load_after   the five NPCs
--   load_changed                      the rules were edited after the savegame was made: the guard needs 4 tokens, the visitor also leaves when SW_Open is set,
--                                     and a new rule SW_Late (quest already succeeded in the savegame) is added
local phase = risen.test.param("phase", "main")

local QUEST = "Q_SW_Main"
local C = risen.cond

risen.balance.install({
    new_npcs = {
        Spass_SW_Giver = { base = "Peasant_1_01", name = "Gus the Giver", level = 3, hp_max = 300, guild = "Cit" },
        Spass_SW_Reward = { base = "Peasant_1_01", name = "Rewarded Smith", level = 3, hp_max = 200, guild = "Cit" },
        Spass_SW_Guard = { base = "Peasant_1_01", name = "Token Guard", level = 3, hp_max = 200, guild = "Cit" },
        Spass_SW_Visitor = { base = "Peasant_1_01", name = "Visitor", level = 3, hp_max = 200, guild = "Cit" },
        Spass_SW_Avenger = { base = "Peasant_1_01", name = "Avenger", level = 3, hp_max = 200, guild = "Cit" },
        Spass_SW_Victim = { base = "Peasant_1_01", name = "Victim", level = 3, hp_max = 100, guild = "Cit" },
        Spass_SW_Late = { base = "Peasant_1_01", name = "Latecomer", level = 3, hp_max = 200, guild = "Cit" },
    },
    new_items = { Spass_SW_Token = { base = "It_Apple", name = "SW Token" } },
})

risen.script.define("SW_StartQuest", function()
    risen.quest.open(QUEST)
    risen.quest.run(QUEST)
    risen.quest.add_log(QUEST, "Gus wants to see you again.")
    return 0
end)
risen.script.define("SW_FinishQuest", function()
    risen.quest.succeed(QUEST)
    return 0
end)

local function define_content()
    risen.quest.define({ name = QUEST, title = "Gus' Errand", type = "Report", xp = 10 })
    risen.dialog.create({
        name = "SW_Work", owner = "SW_Giver", permanent = true,
        lines = {
            { speaker = "player", listener = "npc", text = "Do you have work for me?" },
            { speaker = "npc", listener = "player", text = "Come back and tell me you are done." },
            { script = "SW_StartQuest" },
            { command = "End" },
        },
    })
    risen.dialog.create({
        name = "SW_Done", owner = "SW_Giver", condition = "Running", quest = QUEST, permanent = true,
        lines = {
            { speaker = "player", listener = "npc", text = "I am done." },
            { speaker = "npc", listener = "player", text = "Good." },
            { script = "SW_FinishQuest" },
            { command = "End" },
        },
    })
    -- sets a game variable the way the game's own dialogues do
    risen.dialog.create({
        name = "SW_Open", owner = "SW_Giver", permanent = true,
        lines = {
            { speaker = "player", listener = "npc", text = "Open the gate." },
            { command = "SetGameEvent", GameEvent = "SW_Open" },
            { command = "End" },
        },
    })
end

-- what happened (the scenario asserts on it): "<npc>:<why>" per on_fire / on_despawn call
fires, despawns = {}, {}
local function fired(h, why) fires[#fires + 1] = h.name .. ":" .. why end
local function despawned(h, why) despawns[#despawns + 1] = h.name .. ":" .. why end

risen.npc.persistent({ name = "SW_Giver", template = "Spass_SW_Giver", offset = { 200, 20, 0 }, peaceful = true, label = "Gus the Giver", define = define_content })
-- the victim is a plain persistent NPC (always there): the avenger's condition watches its death
risen.npc.persistent({ name = "SW_Victim", template = "Spass_SW_Victim", offset = { -200, 20, 0 }, peaceful = true })

local rules = {}
rules.Reward = risen.spawn.when({
    name = "SW_Reward", template = "Spass_SW_Reward", offset = { 0, 20, 200 }, peaceful = true, label = "The Reward",
    when = C.quest(QUEST, "Success"), on_fire = fired, on_despawn = despawned,
})
rules.Guard = risen.spawn.when({
    name = "SW_Guard", template = "Spass_SW_Guard", offset = { 0, 20, -200 }, peaceful = true, once = false,
    when = C.item("Spass_SW_Token", phase == "load_changed" and 4 or 3), on_fire = fired, on_despawn = despawned,
})
rules.Visitor = risen.spawn.when({
    name = "SW_Visitor", template = "Spass_SW_Visitor", offset = { 300, 20, 300 }, peaceful = true,
    when = C.event("SW_Open"),
    despawn_when = phase == "load_changed" and C.any { C.var("SW_Gone"), C.event("SW_Open") } or C.var("SW_Gone"),
    on_fire = fired, on_despawn = despawned,
})
rules.Avenger = risen.spawn.when({
    name = "SW_Avenger", template = "Spass_SW_Avenger", offset = { -300, 20, 300 }, peaceful = true,
    when = C.all { C.dead("SW_Victim"), C.negate(C.var("SW_Peace")) }, on_fire = fired, on_despawn = despawned,
})
if phase == "load_changed" then
    -- a rule the mod did not have when the savegame was made: its condition already holds in the savegame
    rules.Late = risen.spawn.when({
        name = "SW_Late", template = "Spass_SW_Late", offset = { 300, 20, -300 }, peaceful = true,
        when = C.quest(QUEST, "Success"), on_fire = fired, on_despawn = despawned,
    })
end

if risen.test.is_active() then
    require("test_spawnwhen")(phase, QUEST, rules)
end
