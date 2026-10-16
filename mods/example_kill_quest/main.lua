-- EXAMPLE 6: kill quests -- "kill N entities of a template" and "kill one special, named entity".
--
-- The hunter Garrick gives two quests in a chain:
--   1. Q_EX_Wolves: kill 3 wolves. Every kill is counted (risen.save keeps the counter), each one adds a quest log line ("Wolves: 2/3"),
--      the third completes the quest and pays gold.
--   2. Q_EX_Boss: offered only after quest 1 succeeded (`condition = "Success"` on the dialogue option -- no code needed for the unlock).
--      The boss is a NEW template (Spass_Dire_Wolf, a clone of Wolf with 600 hit points) spawned under the unique name EX_DireWolf; killing
--      exactly that entity completes the quest.
--
-- What you learn
--   * `entity.died(entity, killer, cause)` fires ONCE for every entity that really died: in a fight (`cause` "combat"), through the game's
--     `Kill` script ("script"), the kill cheat ("cheat"). `entity:name()` is its unique name, `entity:template():name()` the template it was
--     made from ("Wolf", "Spass_Dire_Wolf"); count by template ("any wolf") or by unique entity name ("this boss"). `killer` is the Entity that did
--     it or nil: only kills by the player count here (`killer:is_player()`).
--   * Humans are usually knocked out instead of killed: that is `entity.defeated(entity, attacker)`. Listen to both for "beat" objectives.
--   * Entity:kill() removes an entity without a death (`entity.destroyed` fires, not `entity.died`). Setting hit points to 0 fires nothing either.
--     The game's own script `risen.script.call("Kill", killer, victim)` is what the scenario of this example uses as a stand-in for a fight.
--   * Both NPCs are risen.npc.persistent: Garrick is declared once, the boss is declared when its quest starts (and again, before the load,
--     whenever a savegame has that quest running or finished) so it keeps its name and its dead state across save/load.
--
-- Run the live check (about a minute and a half; see docs/EXAMPLES.md):
--   .\scripts\test-run.ps1 -Instance 2 -Mods example_kill_quest -WaitForResult -Seconds 300 -Scenario example_kill_quest -ShowChecks

local NPC = "EX_Garrick"
local WOLVES, BOSS_QUEST = "Q_EX_Wolves", "Q_EX_Boss"
local BOSS = "EX_DireWolf"
local WANTED = 3


-- ---- the boss template (a clone of Wolf, applied on the first tick and on every session.start) -------------------------------
risen.balance.install({
    new_npcs = {
        Spass_Dire_Wolf = { base = "Wolf", name = "Dire Wolf", hp_max = 600, level = 15, skills = { Strength = 80 }, loot = { It_Apple = 2 } },
    },
})

-- ---- objective 1: count the wolves ---------------------------------------------------------------------------------------------
local function wolf_killed(entity)
    if not risen.quest.is_running(WOLVES) then return end
    local n = risen.save.get("wolf_kills", 0) + 1
    risen.save.set("wolf_kills", n)
    risen.quest.add_log(WOLVES, ("Wolves: %d/%d"):format(math.min(n, WANTED), WANTED))
    if n >= WANTED then
        local player = risen.entity.player()
        if player then risen.item.give(player, "It_Gold", 150) end
        risen.quest.succeed(WOLVES)
    end
end

-- ---- objective 2: the one boss ---------------------------------------------------------------------------------------------------
local function boss_killed()
    if not risen.quest.is_running(BOSS_QUEST) then return end
    local player = risen.entity.player()
    if player then risen.item.give(player, "It_Gold", 500) end
    risen.quest.add_log(BOSS_QUEST, "The Dire Wolf is dead.")
    risen.quest.succeed(BOSS_QUEST)
end

-- Everything the kill objectives need. entity.died fires once per entity (no duplicate bookkeeping) and names the killer.
risen.on("entity.died", function(entity, killer)
    if not entity or not (killer and killer:is_player()) then return end
    if entity:name() == BOSS then return boss_killed() end          -- one specific entity (by unique name)
    local template = entity:template()
    if template and template:name() == "Wolf" then wolf_killed(entity) end   -- any entity of a template
end)

-- ---- the boss: a persistent NPC that exists only once its quest has started -----------------------------------------------------------
-- `position` may be a function of the player: here the other end of the beach, 1200 units from Garrick. Declaring it while the game runs
-- creates it at once; declaring it again on every session.start (below) is what brings it back after a load.
local function spawn_boss()
    return risen.npc.persistent({
        name = BOSS, template = "Spass_Dire_Wolf", label = "The Dire Wolf",
        position = function()
            local garrick = risen.entity.get(NPC)
            local x, y, z = nil, nil, nil
            if garrick then x, y, z = garrick:position() end
            if not (x and y and z) then return nil, nil, nil end   -- not placed yet: tried again at the next session start
            return x + 1200, y + 20, z
        end,
    })
end

-- ---- dialogue -------------------------------------------------------------------------------------------------------------------------
risen.script.define("EX_Garrick_StartWolves", function()
    risen.save.set("wolf_kills", 0)
    risen.quest.open(WOLVES)
    risen.quest.run(WOLVES)
    risen.quest.add_log(WOLVES, ("Garrick wants %d wolves dead."):format(WANTED))
    return 0
end)
risen.script.define("EX_Garrick_StartBoss", function()
    risen.quest.open(BOSS_QUEST)
    risen.quest.run(BOSS_QUEST)
    risen.quest.add_log(BOSS_QUEST, "A Dire Wolf haunts the other end of the beach. Kill it.")
    spawn_boss()
    return 0
end)

local function define_content()
    risen.quest.define({ name = WOLVES, title = "Wolf Hunt", type = "Kill", xp = 100 })
    risen.quest.define({ name = BOSS_QUEST, title = "The Dire Wolf", type = "Kill", xp = 300 })

    risen.dialog.create({
        name = "EX_Garrick_Wolves", owner = NPC,
        lines = {
            { speaker = "player", listener = "npc", text = "Anything to hunt around here?" },
            { speaker = "npc", listener = "player", text = ("Wolves. Kill %d of them."):format(WANTED) },
            { script = "EX_Garrick_StartWolves" },
            { command = "End" },
        },
    })
    -- unlocked by the first quest: the engine only offers it once Q_EX_Wolves succeeded
    risen.dialog.create({
        name = "EX_Garrick_Boss", owner = NPC, condition = "Success", quest = WOLVES,
        lines = {
            { speaker = "player", listener = "npc", text = "The wolves are dead. What else?" },
            { speaker = "npc", listener = "player", text = "Their leader. A Dire Wolf, at the other end of the beach." },
            { script = "EX_Garrick_StartBoss" },
            { command = "End" },
        },
    })
end

-- ---- the hunter ---------------------------------------------------------------------------------------------------------------------
risen.npc.persistent({
    name = NPC, template = "Peasant_1_01", offset = { 150, 20, 150 }, label = "Garrick the Hunter",
    peaceful = true, level = 3, hp_max = 300, define = define_content,
})

-- A savegame whose boss quest has started (running, or finished with the corpse lying there) declares the boss again before the world is
-- reconciled: its record in risen.save and its entity (saved by the engine) are found again. A new game has no such quest.
risen.on("session.start", function()
    local quest = risen.quest.get(BOSS_QUEST)
    local status = quest and quest:get_property("Status") or ""
    if status:find("Running") or status:find("Success") then spawn_boss() end
end)

-- the live check belongs to the test run only
if risen.test.is_active() then
    require("test_kill_quest")(NPC, WOLVES, BOSS_QUEST, BOSS, WANTED)
end
