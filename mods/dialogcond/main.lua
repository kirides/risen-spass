-- Live test mod of M9 "dialogue conditions beyond quest state" (scenario `dialogcond`, scripts/smoke-test.ps1; the readable example is mods/example_dialog_when).
--
-- One NPC (DC_Npc) owns about thirty options, each with another condition (`when`, or `engine` for a raw engine condition). Every option says in `expect` how the layer must compile it:
-- `engine` = the engine conditions it becomes (class names, in order), `lua` = whether a Lua script condition is left over. The scenario (test_dialogcond.lua) changes the world, asks the
-- engine which options it offers (Info:available() and the real conversation list), compares that with what the same condition says when evaluated in Lua, saves and loads in a fresh process.
local phase = risen.test.param("phase", "save")

local C = risen.cond
local OWNER, VICTIM, CORPSE, QUEST = "DC_Npc", "DC_Victim", "DC_Corpse", "Q_DC"
local ORE = "It_IronOre"

risen.balance.install({
    new_npcs = {
        Spass_DC_Npc = { base = "Peasant_1_01", name = "Condition Keeper", level = 3, hp_max = 300, guild = "Cit" },
        Spass_DC_Victim = { base = "Peasant_1_01", name = "Victim", level = 3, hp_max = 100, guild = "Cit" },
    },
})

-- what the predicates answer and how often the engine asked (the scenario switches `flag` and reads `calls`)
local calls = { fn = 0, mixed = 0, err = 0, near = 0 }
local flag = { fn = false, mixed = true, npc = nil, player = nil }

-- key -> { when = , engine = , condition = , quest = , expect = { engine = { classes }, lua = bool }, oracle = bool (compare with the Lua evaluation) }
local OPT = {
    Item3     = { when = C.item(ORE, 3), expect = { engine = { "ItemAmount" }, lua = false } },
    NoOre     = { when = C.negate(C.item(ORE)), expect = { engine = { "ItemAmount" }, lua = false } },
    NpcApple  = { when = C.item("It_Apple", 1, "npc"), expect = { engine = { "ItemAmount" }, lua = false } },
    Flag      = { when = C.event("DC_Flag"), expect = { engine = { "PlayerKnows" }, lua = false } },
    NoFlag    = { when = C.negate(C.event("DC_Flag")), expect = { engine = { "PlayerNotKnows" }, lua = false } },
    VarGE2    = { when = C.var("DC_Count", 2, ">="), expect = { engine = {}, lua = true } },
    Str       = { when = C.skill("Strength", 25), expect = { engine = { "SkillValue" }, lua = false } },
    StrLow    = { when = C.negate(C.skill("Strength", 25)), expect = { engine = { "SkillValue" }, lua = false } },
    Guild     = { when = C.guild("Cit", "npc"), expect = { engine = {}, lua = true } },
    GuildPir  = { when = C.guild("Pir", "npc"), expect = { engine = {}, lua = true } },
    Dead      = { when = C.dead(CORPSE), expect = { engine = { "NPCStatus" }, lua = false } },
    Alive     = { when = C.alive(CORPSE), expect = { engine = { "NPCStatus" }, lua = false } },
    DeadP     = { when = C.dead(VICTIM), expect = { engine = {}, lua = true } },     -- a persistent NPC: its record knows, the engine does not
    Q_Running   = { when = C.quest(QUEST, "Running"), expect = { engine = { "QuestStatus" }, lua = false } },
    Q_Success   = { when = C.quest(QUEST, "Success"), expect = { engine = { "QuestStatus" }, lua = false } },
    Q_Failed    = { when = C.quest(QUEST, "Failed"), expect = { engine = { "QuestStatus" }, lua = false } },
    Q_Cancelled = { when = C.quest(QUEST, "Cancelled"), expect = { engine = { "QuestStatus" }, lua = false } },
    Q_Lost      = { when = C.quest(QUEST, "Lost"), expect = { engine = { "QuestStatus" }, lua = false } },
    Q_Won       = { when = C.quest(QUEST, "Won"), expect = { engine = { "QuestStatus" }, lua = false } },
    Q_Open      = { when = C.quest(QUEST, "Open"), expect = { engine = {}, lua = true } },
    Q_Obsolete  = { when = C.quest(QUEST, "Obsolete"), expect = { engine = {}, lua = true } },
    Q_Any       = { when = C.quest(QUEST, { "Failed", "Lost" }), expect = { engine = {}, lua = true } },
    Q_Missing   = { when = C.quest("Q_DC_Missing", "Running"), expect = { engine = { "QuestStatus" }, lua = false } },
    Q_None      = { when = C.quest("Q_DC_Missing", "None"), expect = { engine = {}, lua = true } },
    -- combinations: "has >= 3 iron ore AND the quest runs" is two engine conditions; "variable set OR guild == Pir" one Lua condition; the mixed one both
    Combo1    = { when = C.all { C.item(ORE, 3), C.quest(QUEST, "Running") }, expect = { engine = { "ItemAmount", "QuestStatus" }, lua = false } },
    Combo2    = { when = C.any { C.var("DC_Count"), C.guild("Pir", "npc") }, expect = { engine = {}, lua = true } },
    Mixed     = { when = C.all { C.item(ORE, 3), C.event("DC_Flag"), C.fn(function() calls.mixed = calls.mixed + 1 return flag.mixed end), },
                  expect = { engine = { "ItemAmount", "PlayerKnows" }, lua = true }, oracle = false },
    Nested    = { when = C.all { C.any { C.event("DC_Flag"), C.var("DC_Count", 5) }, C.negate(C.all { C.item(ORE, 3), C.guild("Pir", "npc") }) }, expect = { engine = {}, lua = true } },
    -- a plain function: the whole condition is Lua; it receives the NPC and the player
    FnFlag    = { when = function(npc, player) calls.fn = calls.fn + 1 flag.npc = npc and npc:name() flag.player = player and player:is_player() return flag.fn end,
                  expect = { engine = {}, lua = true }, oracle = false },
    FnError   = { when = function() calls.err = calls.err + 1 error("boom in a dialogue condition") end, expect = { engine = {}, lua = true }, oracle = false },
    -- raw engine condition by class name; and the quest-state condition type of the info itself combined with a `when`
    Near      = { engine = { { class = "OwnerNearEntity", Entity = "PC_Hero" } }, expect = { engine = { "OwnerNearEntity" }, lua = false }, oracle = false },
    AndBase   = { condition = "Running", quest = QUEST, when = C.item(ORE, 3), expect = { engine = { "ItemAmount" }, lua = false }, oracle = false },
}

local function define_content()
    risen.quest.define({ name = QUEST, title = "Condition quest", type = "Report", xp = 1 })
    for key, o in pairs(OPT) do
        local info = risen.dialog.create({
            name = "DC_" .. key, owner = OWNER, permanent = true, when = o.when, engine = o.engine, condition = o.condition, quest = o.quest,
            lines = { { speaker = "player", listener = "npc", text = "Option " .. key } },
        })
        o.info = info
    end
    risen.dialog.create({
        name = "DC_Bye", owner = OWNER, permanent = true,
        lines = { { speaker = "player", listener = "npc", text = "Goodbye." }, { command = "End" } },
    })
end

risen.npc.persistent({ name = OWNER, template = "Spass_DC_Npc", offset = { 200, 20, 0 }, peaceful = true, label = "Condition Keeper", define = define_content })
risen.npc.persistent({ name = VICTIM, template = "Spass_DC_Victim", offset = { -200, 20, 0 }, peaceful = true })

if risen.test.is_active() then
    require("test_dialogcond")(phase, { OPT = OPT, calls = calls, flag = flag, OWNER = OWNER, VICTIM = VICTIM, CORPSE = CORPSE, QUEST = QUEST, ORE = ORE })
end
