-- EXAMPLE 13: dialogue options that appear only when the world says so: items AND a game variable AND a guild, "variable set OR guild", ... (not just a quest state).
--
-- Rurik the smuggler stands near the start. What he offers depends on the player:
--
--   "What do you smuggle?"        always; every time it is chosen the smuggler trusts the player a little more (a game variable) and the game event EXD_Heard is set (SetGameEvent)
--   "I know where your hideout is"  appears once EXD_Heard is set OR the player is a bandit        -- any{ event, guild }       (a Lua condition)
--   "I brought the ore"           appears when the player carries >= 3 iron ore AND trust >= 2 AND is a bandit (guild Pir)
--                                    -- all{ item, var, guild }: the item is an ENGINE condition, the other two one Lua condition behind it
--
-- What you learn
--   * risen.dialog.create{ ..., when = <condition> }: the conditions of risen.cond (the ones risen.spawn.when uses: quest, item, var, event, skill, guild, dead, alive, fn, all / any / negate)
--     decide whether the engine offers the option. `condition = "Running", quest = ...` (the info's own condition type) still works and is combined with `when` (both must hold).
--   * What the layer does with it: leaves that the engine knows (item amount, a game variable existing, skill value, NPC dead/alive, one quest status) become the engine's own gCInfoCondition
--     objects, evaluated natively; `all{}` is just several of them. Everything else (any, var with a value, guild, a quest status without a twin, your own predicate) is ONE script condition,
--     evaluated by a Lua function when the engine looks at the option. The engine's conditions come first, so the Lua part only runs when they hold.
--     risen.dialog.describe("EXD_Rurik_Secret") tells you what was made.
--   * The conditions are re-read whenever the engine builds the option list: talk to the NPC again after the world changed and the options follow. (The list does not change while the
--     conversation is open.)
--   * A predicate (`when = function(npc, player) ... end`) receives the owner of the option and the player; an error in it is a warning in the log and the option is simply not offered.
--
-- Run the live check (about 1 minute; see docs/EXAMPLES.md):
--   .\scripts\test-run.ps1 -Instance 2 -Mods example_dialog_when -WaitForResult -Seconds 300 -Scenario example_dialog_when -ShowChecks

local RURIK = "EXD_Rurik"
local C = risen.cond

risen.balance.install({
    new_npcs = {
        Spass_EXD_Rurik = { base = "Peasant_1_01", name = "Rurik the Smuggler", level = 5, hp_max = 300, guild = "Cit" },
    },
})

-- chatting builds trust: a game variable the options below look at (risen.gamevar is saved with the game)
risen.script.define("EXD_Chat", function()
    risen.gamevar.set("EXD_Trust", (risen.gamevar.get("EXD_Trust") or 0) + 1)
    return 0
end)

local function define_content()
    risen.dialog.create({
        name = "EXD_Rurik_Chat", owner = RURIK, permanent = true,
        lines = {
            { speaker = "player", listener = "npc", text = "What do you smuggle?" },
            { speaker = "npc", listener = "player", text = "Whatever pays. Ask me again some time." },
            { script = "EXD_Chat" },
            { command = "SetGameEvent", GameEvent = "EXD_Heard" },       -- a game event is a variable with the value 0: it EXISTS from now on
            { command = "End" },
        },
    })

    -- "variable set OR guild == Pir": one Lua condition (the engine has no OR)
    risen.dialog.create({
        name = "EXD_Rurik_Hideout", owner = RURIK, permanent = true,
        when = C.any { C.event("EXD_Heard"), C.guild("Pir") },
        lines = {
            { speaker = "player", listener = "npc", text = "I know where your hideout is." },
            { speaker = "npc", listener = "player", text = "Then you know too much, or you are one of us. Keep it quiet." },
            { command = "End" },
        },
    })

    -- "has >= 3 iron ore AND trust >= 2 AND is a bandit": the ore is an engine condition (ItemAmount), trust and guild are one Lua condition behind it
    risen.dialog.create({
        name = "EXD_Rurik_Secret", owner = RURIK,                         -- not permanent: it is gone once it was used
        when = C.all { C.item("It_IronOre", 3), C.var("EXD_Trust", 2, ">="), C.guild("Pir") },
        lines = {
            { speaker = "player", listener = "npc", text = "I brought the ore." },
            { speaker = "npc", listener = "player", text = "Three pieces. Good. Here is what the Inquisition does not want you to have." },
            { command = "Give", Entity1 = "player", Entity2 = "npc", Item = "It_IronOre", Amount = 3 },    -- the ore changes hands
            { command = "Give", Entity1 = "npc", Entity2 = "player", Item = "It_Apple", Amount = 5 },
            { command = "GiveXP", Amount = 25 },
            { command = "End" },
        },
    })
end

risen.npc.persistent({
    name = RURIK, template = "Spass_EXD_Rurik", offset = { 150, 20, 150 }, label = "Rurik the Smuggler", peaceful = true, define = define_content,
})

-- the live check belongs to the test run only
if risen.test.is_active() then
    require("test_dialog_when")(RURIK)
end
