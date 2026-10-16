-- A complete fetch quest, written the way a savegame-safe mod should be (docs/LUA_API.md, "Persistence"):
--
--   * Marta, an NPC, asks the player for 3 apples. Talking to her starts the quest; once the quest runs, a second option
--     lets the player hand the apples in (reward: a machete). The quest, its log entries and "already given" flags are
--     saved by the engine; the NPC is declared with risen.npc.persistent: spawned once, kept (and named again) across loads.
--   * Quests and dialogue options must exist *before* the engine reads a savegame (load.before) and again after a new game
--     wiped the quest manager (session.start). risen.quest.define / risen.dialog.create are idempotent, so one function
--     serves both.
--   * Marta stands where she stood in that savegame: the engine saves spawned entities (not their unique name); the layer finds her by
--     her entity ID after a load and names her again.
local NPC, QUEST = "SPASS_Marta", "Q_SPASS_Apples"
local ITEM, WANTED = "It_Apple", 3

-- ---- effects of the dialogue lines: plain scripts, referenced by name from the info commands ---------------------------

risen.script.define("SPASS_Marta_Start", function()
    risen.quest.open(QUEST)
    risen.quest.run(QUEST)
    risen.quest.add_log(QUEST, ("Marta needs %d apples. Bring them to her."):format(WANTED))
    return 0
end)

local turnins = 0
risen.script.define("SPASS_Marta_TurnIn", function()
    turnins = turnins + 1
    risen.log.debug("fetchquest: turn-in script call #" .. turnins)
    local player = risen.entity.player()
    if not player then return 0 end
    if risen.item.count(player, ITEM) < WANTED then
        risen.log.debug("fetchquest: log entries before add_log: " .. risen.quest.get(QUEST):log_count())
        risen.quest.add_log(QUEST, ("You only have %d of the %d apples Marta wants."):format(risen.item.count(player, ITEM), WANTED))
        risen.log.debug("fetchquest: log entries after add_log: " .. risen.quest.get(QUEST):log_count())
        return 0
    end
    local removed = risen.item.take(player, ITEM, WANTED)
    risen.log.debug("fetchquest: took " .. removed .. " apples")
    risen.item.give(player, "It_1H_Machete", 1)
    risen.quest.add_log(QUEST, "Marta thanks you and gives you a machete.")
    risen.quest.succeed(QUEST)
    risen.save.set("delivered", true)
    return 0
end)

-- ---- content: quest + dialogue options (idempotent) -----------------------------------------------------------------------

local function define_content()
    risen.quest.define({ name = QUEST, title = "Marta's Apples", type = "Report", xp = 100 })

    risen.dialog.create({
        name = "SPASS_Marta_Work", owner = NPC,
        lines = {
            { speaker = "player", listener = "npc", text = "Do you have any work for me?" },
            { speaker = "npc", listener = "player", text = ("My apples are gone and the pie won't bake itself. Bring me %d apples."):format(WANTED) },
            { script = "SPASS_Marta_Start" },
        },
    })
    risen.dialog.create({
        name = "SPASS_Marta_Apples", owner = NPC,
        condition = "Running", quest = QUEST,  -- only offered while the quest runs
        permanent = true,                      -- stays available, so the player can come back with more apples
        lines = {
            { speaker = "player", listener = "npc", text = "I brought the apples." },
            { speaker = "npc", listener = "player", text = "Let me count them..." },
            { script = "SPASS_Marta_TurnIn" },
        },
    })
end

-- ---- the NPC: declared once; the layer spawns it in a new game and finds it (and names it again) after every load ------------------
-- `define` runs on load.before (before the engine reads quests/infos of a savegame) and on every session.start.
risen.npc.persistent({
    name = NPC, template = "Peasant_1_01", offset = { 150, 20, 150 }, level = 3, hp_max = 300, peaceful = true, group = "SPASS", label = "Marta the Orchardkeeper", define = define_content,
})
