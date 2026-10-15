-- EXAMPLE 12: a new crafting recipe -- a Slightly Improved Sword from a regular sword and two iron ore, made at the smithing anvil.
--
-- What you learn
--   * A recipe is an ITEM with a gCRecipe_PS property set (station, ingredients, required skills, result). A character "knows" a recipe by carrying that recipe
--     item (the engine has no list of learned recipes): the anvil lists the recipe items of its category from the inventory. risen.recipe.create makes one
--     (a clone of a shipped recipe item, with a deterministic ID like every new template); `learn = true` gives it to the player once per savegame.
--   * The station is chosen with `station` (anvil, forge, grindstone, coolweapon, alchemy, goldsmith, scroll, cooking, frying). The shipped smithing recipes are
--     the intermediate blanks themselves (raw blade at the forge, hot blank at the anvil, ...): see docs/RECIPE_PROPERTIES.md.
--   * The result is an ordinary item: here a NEW one (`new_items`, see example 7/new templates), a clone of the regular sword with slightly higher numbers and
--     its own localized label. Both sections go into the same risen.balance spec; a recipe may name an item that the same spec creates.
--   * risen.balance.install applies the spec on the first `tick` (before any savegame is read) and on every `session.start`, so saved swords and the saved
--     recipe item resolve again after a restart. Without the mod the stacks stay dormant in the savegame and come back when the mod is back.
--   * Crafting is the engine's own: the anvil checks the ingredients and the skill, deletes them, creates the result and a `crafting` event fires once.
--
-- Run the live check (about two minutes; real anvil interaction, see docs/EXAMPLES.md):
--   .\scripts\test-run.ps1 -Instance 2 -Mods example_recipe -WaitForResult -Seconds 300 -Scenario ex_recipe -Param phase=save -ShowChecks

local SPEC = {
    new_items = {
        -- the regular sword is It_1H_Sword (value 50, damage 15, Strength 10); the improved one is a little better
        Spass_Sword_Improved = {
            base = "It_1H_Sword",
            name = { English = "Slightly Improved Sword", German = "Leicht verbessertes Schwert" },
            description = "A regular sword, reforged with a little iron ore.",
            value = 65,
            damage = 18,
        },
    },
    new_recipes = {
        Spass_Recipe_Improved_Sword = {
            name = { English = "Smithing plan: Slightly Improved Sword", German = "Schmiedeplan: Leicht verbessertes Schwert" },
            station = "anvil",
            ingredients = { It_1H_Sword = 1, It_IronOre = 2 },   -- items are consumed in this amount; they are the template names of docs/TEMPLATES.md
            result = "Spass_Sword_Improved",
            skills = { Smith = 1 },                              -- the crafter needs the Smith skill (the anvil itself already asks for 1 and a hammer)
            learn = true,                                        -- the player gets the plan once per savegame (the plan item is what makes the recipe "known")
        },
    },
}

local reports = {}   -- what install() reported, for the test
-- (test run only) the phase `load_missing` starts the game without the mod's content to see what a savegame does when the mod is gone
if not (risen.test.is_active() and risen.test.param("phase") == "load_missing") then
    risen.balance.install(SPEC, {
        on_applied = function(when, report) reports[#reports + 1] = { when = when, report = report } end,
    })
end

-- the live check belongs to the test run only
if risen.test.is_active() then
    require("test_recipe")(SPEC, reports)
end
