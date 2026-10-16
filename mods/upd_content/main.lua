-- Test mod of M15 "safe updates and removals" (scenario `updsafe`, scripts/smoke-test.ps1; recipes: docs/MODDING_GUIDE.md "Updating and removing content").
-- The same mod in several versions, chosen by the test parameter `variant`:
--   v1          the old version: everything below, played and saved by the scenario
--   same        the same content again (the control: a normal load)
--   empty       an update that declares nothing any more (no NPCs, quests, dialogue options, items, recipes, spells)
--   renamed     an update that renamed the quest line and the items, keeps the giver NPC with other dialogue options, drops the extra and the late NPC
--   retire      like `empty`, but it states what it removed with risen.retire
-- The mod's version in mod.toml stays 1.0.0 in every variant (the scenario changes behaviour, not metadata).
local variant = risen.test.param("variant", "v1")
local same = variant == "v1" or variant == "same"

local Q_MAIN, Q_DONE = "Q_UP_Main", "Q_UP_Done"
if variant == "renamed" then Q_MAIN, Q_DONE = "Q_UP_Main2", "Q_UP_Done2" end

if same or variant == "renamed" then
    local P = variant == "renamed" and "Spass_UP2_" or "Spass_UP_"
    local spec = {
        new_items = {
            [P .. "Token"] = { base = "It_Apple", name = "Quest Token", description = "Proof of an errand." },
            [P .. "Blade"] = { base = "It_1H_Sword", name = "Errand Blade", value = 77, damage = 21 },
        },
        new_npcs = {
            Spass_UP_Giver = { base = "Peasant_1_01", name = "Giver Gil", level = 3, hp_max = 200, guild = "Cit" },
        },
    }
    if same then
        spec.new_npcs.Spass_UP_Extra = { base = "Peasant_1_02", name = "Extra Egon", level = 3, hp_max = 200, guild = "Cit" }
        spec.new_npcs.Spass_UP_Late = { base = "Peasant_1_03", name = "Late Lena", level = 3, hp_max = 200, guild = "Cit" }
        spec.new_spells = {
            Spass_UP_Bolt = { base = "Fireball", name = "Errand Bolt", description = "A bolt for errands.", mana_cost = 12, damage_mult = 1.5, forms = { "scroll" } },
        }
        spec.new_recipes = {
            Spass_UP_Plan = { name = "Plan: Errand Blade", station = "anvil", ingredients = { It_1H_Sword = 1, It_IronOre = 1 }, result = P .. "Blade", skills = { Smith = 1 }, learn = true },
        }
    end
    risen.balance.install(spec)
end

risen.script.define("UP_StartMain", function()
    risen.quest.open(Q_MAIN)
    risen.quest.run(Q_MAIN)
    risen.quest.add_log(Q_MAIN, "Gil asked you to run an errand.")
    return 0
end)
risen.script.define("UP_FinishMain", function()
    risen.quest.succeed(Q_MAIN)
    return 0
end)

local function define_content()
    risen.quest.define({ name = Q_MAIN, title = "The errand", type = "Report", xp = 5 })
    risen.quest.define({ name = Q_DONE, title = "The finished errand", type = "Report", xp = 5 })
    local P = variant == "renamed" and "UP2_" or "UP_"
    risen.dialog.create({
        name = P .. "Info_Start", owner = "UP_Giver",
        lines = { { speaker = "player", listener = "npc", text = "Any work?" }, { speaker = "npc", listener = "player", text = "Run an errand." }, { script = "UP_StartMain" }, { command = "End" } },
    })
    risen.dialog.create({
        name = P .. "Info_Running", owner = "UP_Giver", condition = "Running", quest = Q_MAIN,
        lines = { { speaker = "player", listener = "npc", text = "It is done." }, { speaker = "npc", listener = "player", text = "Good." }, { script = "UP_FinishMain" }, { command = "End" } },
    })
    risen.dialog.create({
        name = P .. "Info_Thanks", owner = "UP_Giver", condition = "Success", quest = Q_DONE,
        lines = { { speaker = "npc", listener = "player", text = "Thanks again." }, { command = "End" } },
    })
end

if same or variant == "renamed" then
    risen.npc.persistent({ name = "UP_Giver", template = "Spass_UP_Giver", offset = { 200, 20, 0 }, peaceful = true, define = define_content })
end
if same then
    risen.npc.persistent({ name = "UP_Extra", template = "Spass_UP_Extra", offset = { -200, 20, 0 }, peaceful = true })
    risen.npc.persistent({ name = "UP_Plain", template = "Peasant_1_02", offset = { 0, 20, -220 }, peaceful = true })     -- a SHIPPED template: his entity survives the removal of the mod (nameless)
    risen.spawn.when({ name = "UP_Late", template = "Spass_UP_Late", offset = { 0, 20, 220 }, peaceful = true, when = risen.cond.quest(Q_DONE, "Success") })
    risen.save.set("counter", risen.save.get("counter") or 0)
end

if variant == "retire" and risen.retire then
    risen.retire({ npcs = { "UP_Giver", "UP_Extra", "UP_Late", "UP_Plain" }, quests = { "Q_UP_Main", "Q_UP_Done" }, game_vars = { "UP_Flag" },
        items = { "Spass_UP_Token", "Spass_UP_Blade", "Spass_UP_Bolt_Scroll", "Spass_UP_Plan" }, save_keys = { "counter" } })
end
