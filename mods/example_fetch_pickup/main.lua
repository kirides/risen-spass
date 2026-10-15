-- EXAMPLE 5b: a fetch quest that completes by itself when the player has collected the items -- no hand-in dialogue.
--
-- The scout asks for 3 berries and scatters them around himself. From then on the mod watches the player's inventory; every new berry
-- adds a quest log line ("Berries: 1/3"), the third one completes the quest and pays the reward. The berries stay in the inventory.
--
-- What you learn
--   * `inventory.changed(owner, template, delta, total, cause, other)` fires for EVERY change of an inventory, whatever its source: picked up
--     from the ground (`cause` "pickup"), looted from a corpse or chest ("loot"), bought ("trade"), given by a script or a dialogue ("script",
--     "dialogue"). No polling: the handler below just asks the engine for the count whenever the player's berries changed.
--   * Count relative to a baseline taken when the quest starts (the player may already carry berries) and keep the baseline in risen.save
--     so it survives saving and loading (the Lua state itself is not saved).
--   * Entities spawned from Lua are not saved: the scattered berries are spawned again when a saved game with this quest running is loaded.
--
-- Run the live check (about a minute; see docs/EXAMPLES.md):
--   .\scripts\test-run.ps1 -Instance 2 -Mods example_fetch_pickup -WaitForResult -Seconds 240 -Scenario example_fetch_pickup -ShowChecks

local NPC, QUEST = "EX_Scout", "Q_EX_Collect"
local ITEM, WANTED = "It_Plant_Berry", 3

risen.loc.set("FO_" .. NPC, "Finn the Scout")

-- ---- progress: how many berries did the player collect since the quest started? ------------------------------------------------
local reported = -1   -- last progress that was written to the log (Lua state: not saved, only used to avoid repeating lines)

local function collected()
    local player = risen.entity.player()
    if not player then return nil end
    return risen.item.count(player, ITEM) - risen.save.get("collect_base", 0)
end

local function on_item_count_changed()
    if not risen.quest.is_running(QUEST) then return end
    local n = collected()
    if not n or n == reported then return end
    reported = n
    if n >= WANTED then
        local player = assert(risen.entity.player())
        risen.item.give(player, "It_Po_Health_01", 2)                    -- the reward; the berries are kept
        risen.quest.add_log(QUEST, "That is enough berries. Finn gives you two potions.")
        risen.quest.succeed(QUEST)
    elseif n > 0 then
        risen.quest.add_log(QUEST, ("Berries: %d/%d"):format(n, WANTED))
    end
end

-- Every source of berries: ground pickup, loot, trade, scripts, dialogues (the event is queued by the engine hook and dispatched at the end of
-- the frame, so calling the quest API from here is safe).
risen.on("inventory.changed", function(owner, template)
    if template == ITEM and owner and owner:is_player() then on_item_count_changed() end
end)

-- ---- scatter the berries around the scout (runtime entities: respawned after a load) -----------------------------------------------
local function scatter(count)
    local scout = risen.entity.get(NPC)
    if not scout then return end
    local x, y, z = scout:position()
    if not x then return end
    for i = 1, count do
        risen.entity.spawn(ITEM, x + 80 * i, y + 10, z - 120)
    end
end

-- ---- dialogue ---------------------------------------------------------------------------------------------------------------------
risen.script.define("EX_Scout_Start", function()
    local player = risen.entity.player()
    risen.save.set("collect_base", player and risen.item.count(player, ITEM) or 0)   -- the baseline
    reported = -1
    risen.quest.open(QUEST)
    risen.quest.run(QUEST)
    risen.quest.add_log(QUEST, ("Finn needs %d berries. They lie around his camp."):format(WANTED))
    scatter(WANTED)
    return 0
end)

local function define_content()
    risen.quest.define({ name = QUEST, title = "Finn's Berries", type = "Report", xp = 80 })
    risen.dialog.create({
        name = "EX_Scout_Work", owner = NPC,
        lines = {
            { speaker = "player", listener = "npc", text = "What are you doing here?" },
            { speaker = "npc", listener = "player", text = ("I am out of berries. Pick up %d of them around here, I will notice."):format(WANTED) },
            { script = "EX_Scout_Start" },
            { command = "End" },
        },
    })
end

-- ---- the NPC (and, after a load, the berries that are still missing) ---------------------------------------------------------------
local function ensure_world()
    if not risen.entity.get(NPC) then
        local pos = risen.save.get("scout_pos")
        if not pos then
            local player = risen.entity.player()
            if not player then return end
            local x, y, z = player:position()
            if not x then return end
            pos = { x + 150, y + 20, z + 150 }
            risen.save.set("scout_pos", pos)
        end
        risen.npc.spawn("Peasant_1_01", pos[1], pos[2], pos[3], { name = NPC, peaceful = true, level = 3, hp_max = 300 })
        if risen.quest.is_running(QUEST) then                -- a savegame with the quest running: put back what is still missing
            scatter(math.max(0, WANTED - (collected() or 0)))
        end
    end
end

risen.on("load.before", define_content)
risen.on("session.start", function()
    define_content()
    reported = -1
    local waited, handle = 0, nil
    handle = risen.on("tick", function()
        waited = waited + 1
        if waited == 60 then
            ensure_world()
            if handle then risen.off(handle) end
        end
    end)
end)

-- the live check belongs to the test run only
if risen.test.is_active() then
    require("test_fetch_pickup")(NPC, QUEST, ITEM, WANTED, function() return collected() end)
end
