-- EXAMPLE 5a: a fetch quest that completes through a dialogue turn-in.
--
-- Hanna wants 3 berries. Talking to her starts the quest; while it runs, a second option ("I brought the berries") checks the
-- inventory: too few -> she says so (quest log entry, quest keeps running), enough -> the berries are taken, the reward is given and
-- the quest succeeds.
--
-- What you learn
--   * The turn-in is a risen.script.define'd Lua function referenced from the option's lines ({ script = "..." }): it counts with
--     risen.item.count, takes with risen.item.take, rewards with risen.item.give / risen.quest.succeed.
--   * `permanent = true` on the hand-in option lets the player come back with more berries after a failed attempt.
--   * The quest, its log entries and the "option was used" flags are saved by the engine; the NPC is not (spawned again on every
--     session.start, at the position remembered in risen.save so she stays where she was in that savegame).
--   * See example_fetch_pickup for the variant that completes without any dialogue.
--
-- Run the live check (about a minute; see docs/EXAMPLES.md):
--   .\scripts\test-run.ps1 -Instance 2 -Mods example_fetch_turnin -WaitForResult -Seconds 240 -Scenario example_fetch_turnin -ShowChecks

local NPC, QUEST = "EX_Hanna", "Q_EX_Berries"
local ITEM, WANTED, REWARD_GOLD = "It_Plant_Berry", 3, 100

risen.loc.set("FO_" .. NPC, "Hanna the Gatherer")

-- ---- effects of the dialogue lines ------------------------------------------------------------------------------------------
risen.script.define("EX_Hanna_Start", function()
    risen.quest.open(QUEST)
    risen.quest.run(QUEST)
    risen.quest.add_log(QUEST, ("Hanna needs %d berries. Bring them to her."):format(WANTED))
    return 0
end)

risen.script.define("EX_Hanna_TurnIn", function()
    local player = risen.entity.player()
    if not player then return 0 end
    local have = risen.item.count(player, ITEM)
    if have < WANTED then
        risen.quest.add_log(QUEST, ("You only have %d of the %d berries Hanna wants."):format(have, WANTED))
        return 0                                   -- the quest keeps running; the option is permanent, so try again later
    end
    risen.item.take(player, ITEM, WANTED)          -- the hand-in takes the berries ...
    risen.item.give(player, "It_Gold", REWARD_GOLD) -- ... and pays
    risen.quest.add_log(QUEST, "Hanna thanks you and pays you " .. REWARD_GOLD .. " gold.")
    risen.quest.succeed(QUEST)
    return 0
end)

-- ---- quest + options (idempotent: called on load.before and session.start) ------------------------------------------------------
local function define_content()
    risen.quest.define({ name = QUEST, title = "Hanna's Berries", type = "Report", xp = 100 })

    risen.dialog.create({
        name = "EX_Hanna_Work", owner = NPC,
        lines = {
            { speaker = "player", listener = "npc", text = "Can I help you with something?" },
            { speaker = "npc", listener = "player", text = ("My jam needs berries. Bring me %d."):format(WANTED) },
            { script = "EX_Hanna_Start" },
            { command = "End" },
        },
    })
    risen.dialog.create({
        name = "EX_Hanna_TurnIn", owner = NPC, condition = "Running", quest = QUEST, permanent = true,
        lines = {
            { speaker = "player", listener = "npc", text = "I brought the berries." },
            { speaker = "npc", listener = "player", text = "Let me count them ..." },
            { script = "EX_Hanna_TurnIn" },
            { command = "End" },
        },
    })
end

-- ---- the NPC ------------------------------------------------------------------------------------------------------------------
local function ensure_npc()
    if risen.entity.get(NPC) then return end
    local pos = risen.save.get("hanna_pos")               -- the same spot after every load of this savegame
    if not pos then
        local player = risen.entity.player()
        if not player then return end
        local x, y, z = player:position()
        if not x then return end
        pos = { x + 150, y + 20, z + 150 }
        risen.save.set("hanna_pos", pos)
    end
    risen.npc.spawn("Peasant_1_01", pos[1], pos[2], pos[3], { name = NPC, peaceful = true, level = 3, hp_max = 300 })
end

risen.on("load.before", define_content)
risen.on("session.start", function()
    define_content()
    local frames, handle = 0, nil
    handle = risen.on("tick", function()
        frames = frames + 1
        if frames == 60 then
            ensure_npc()
            if handle then risen.off(handle) end
        end
    end)
end)

-- the live check belongs to the test run only
if risen.test.is_active() then
    require("test_fetch_turnin")(NPC, QUEST, ITEM, WANTED, REWARD_GOLD)
end
