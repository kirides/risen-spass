-- A complete fetch quest, written the way a savegame-safe mod should be (docs/LUA_API.md, "Persistence"):
--
--   * Marta, an NPC, asks the player for 3 apples. Talking to her starts the quest; once the quest runs, a second option
--     lets the player hand the apples in (reward: a machete). The quest, its log entries and "already given" flags are
--     saved by the engine; the NPC itself is not, so she is spawned again whenever a game starts or is loaded.
--   * Quests and dialogue options must exist *before* the engine reads a savegame (load.before) and again after a new game
--     wiped the quest manager (session.start). risen.quest.define / risen.dialog.create are idempotent, so one function
--     serves both.
--   * Where Marta stands is remembered in risen.save, so she reappears at the same spot in that savegame.
local NPC, QUEST = "SPASS_Marta", "Q_SPASS_Apples"
local ITEM, WANTED = "It_Apple", 3

risen.loc.set("FO_" .. NPC, "Marta the Orchardkeeper")

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

-- ---- the NPC: not part of savegames, so spawn it whenever it is missing ---------------------------------------------------

local function ensure_npc()
    if risen.entity.get(NPC) then return end
    local pos = risen.save.get("marta_pos")
    if not pos then
        local player = risen.entity.player()
        if not player then return end
        local x, y, z = player:position()
        if not x then return end
        pos = { x + 150, y + 20, z + 150 }
        risen.save.set("marta_pos", pos)
    end
    risen.npc.spawn("Peasant_1_01", pos[1], pos[2], pos[3], { name = NPC, level = 3, hp_max = 300, guild = 3, group = "SPASS" })
end

risen.on("load.before", define_content)             -- before the engine reads quests/infos of a savegame
risen.on("session.start", function()                -- a new game, or the end of a load
    define_content()
    local frames = 0
    local h
    h = risen.on("tick", function()                 -- the player entity exists a moment after the session started
        frames = frames + 1
        if frames == 60 then
            ensure_npc()
            risen.off(h)
        end
    end)
end)
