-- balance.lua -- the numbers: shipped items, NPCs and spells re-balanced, new items, a new spell and a smithing recipe, all in ONE declarative table.
--
-- risen.balance.install(SPEC) validates the whole table first (template names, setting names, skill names, ranges: a typo is an error in the log, not a silent no-op), then
-- applies it on the first `tick` (into the templates: spawns, inventories, loot) and again on every `session.start` (now also into the entities that already live in the
-- world, which keep their own copies of every value). Applying twice is harmless: relative settings (`*_mult`, `*_add`) are computed from the shipped values.
-- Where the numbers live: docs/ITEM_PROPERTIES.md, NPC_PROPERTIES.md, SPELL_PROPERTIES.md, RECIPE_PROPERTIES.md; the setting names: docs/LUA_API.md "Balancing".
local assets = require("assets")

local M = {}

M.SWORD = "Spass_BX_Sword"                  -- the recipe's result
M.CLUB = "Spass_BX_Ember_Club"              -- the quest reward, the item with its own mesh and texture
M.RECIPE = "Spass_BX_Recipe_Sword"          -- the smithing plan (a recipe IS an item the character carries)
M.SPELL = "Spass_BX_Fireball_Strong"        -- crystal + scroll (the scroll is "Spass_BX_Fireball_Strong_Scroll")

M.SPEC = {
    -- ---- 1. shipped items ---------------------------------------------------------------------------------------------------------------------------------
    items = {
        -- the regular sword (shipped: value 50, damage 15, Strength 10): dearer, sharper, and wieldable by a weaker character
        It_1H_Sword = { value = 90, damage = 20, requires = { Strength = 8 } },
        -- a potion heals through a modifier of the current hit points
        It_Po_Health_01 = { modifies = { HitPoints = 40 } },
    },
    -- every template of a group path (docs/TEMPLATES.md) or Lua pattern: halve the gold value of all potions (health potion: shipped 30 -> 15)
    item_groups = {
        { group = "Items/Potions", value_mult = 0.5 },
    },

    -- ---- 2. shipped NPCs and monsters -----------------------------------------------------------------------------------------------------------------------
    npcs = {
        -- absolute numbers: tougher wolves that pay more experience (via the level) and carry an apple
        Wolf = { hp_max = 200, xp = 120, loot = { It_Apple = 1 } },
        -- relative numbers (relative to the SHIPPED values: Boar has 180 hit points and level 6): 360 hit points, level 8
        Boar = { hp_mult = 2, level_add = 2 },
    },

    -- ---- 3. new items ---------------------------------------------------------------------------------------------------------------------------------------
    -- A new template is a clone of an existing one with other numbers, texts and (optionally) files. Its ID derives from its name, so savegames that contain it load again.
    new_items = {
        -- the recipe's result: a regular sword, slightly better than the (already improved) common one. Numbers are given explicitly: the clone is taken from the base at creation time
        [M.SWORD] = {
            base = "It_1H_Sword",
            name = { English = "Slightly Improved Sword", German = "Leicht verbessertes Schwert" },
            description = "A regular sword, reforged with a little iron ore.",
            value = 130, damage = 24, requires = { Strength = 8 },
        },
        -- the quest reward: a club with its own mesh and texture files (assets.lua makes them); `mesh` is checked against everything mounted
        [M.CLUB] = {
            base = "It_1H_Club",
            name = { English = "Ember Club", German = "Glutkeule" },
            description = "Brogar's own work. The wood glows a little at the edges.",
            mesh = assets.MESH, value = 150, damage = 14,
        },
    },

    -- ---- 4. spells ------------------------------------------------------------------------------------------------------------------------------------------
    -- A spell is a group of items (a crystal for Fireball/Frost/Missile; rune, scroll and orb for the others); the damage lives in the projectile. docs/SPELL_PROPERTIES.md
    spells = {
        Fireball = { mana_cost = 10, damage = 140 },        -- shipped: 8 mana, 100 damage
    },
    new_spells = {
        -- a fireball that hits twice as hard for twice the mana, as a crystal and a scroll (used up by the cast, half the cost); needs the second Fireball circle
        [M.SPELL] = {
            base = "Fireball",
            name = { English = "Strong Fireball", German = "Starker Feuerball" },
            description = "A fireball that hits twice as hard.",
            mana_cost = 20, requires = { Fireball = 2 }, damage_mult = 2,
            forms = { "crystal", "scroll" },
        },
    },

    -- ---- 5. a smithing recipe ---------------------------------------------------------------------------------------------------------------------------------
    -- docs/RECIPE_PROPERTIES.md: stations anvil, forge, grindstone, coolweapon, alchemy, goldsmith, scroll, cooking, frying
    new_recipes = {
        [M.RECIPE] = {
            name = { English = "Smithing plan: Slightly Improved Sword", German = "Schmiedeplan: Leicht verbessertes Schwert" },
            station = "anvil",
            ingredients = { It_1H_Sword = 1, It_IronOre = 2 },    -- consumed in this amount
            result = M.SWORD,
            skills = { Smith = 1 },
            learn = true,                                          -- the player gets the plan once per savegame
        },
    },

    -- ---- 6. the NPCs of the story (their dialogue and quest are in story.lua) ----------------------------------------------------------------------------------
    new_npcs = {
        Spass_BX_Brogar = { base = "Peasant_1_02", name = "Brogar the Smith", level = 6, hp_max = 320, guild = "Cit" },
        Spass_BX_Tilda = { base = "Peasant_1_03", name = "Tilda the Apprentice", level = 3, hp_max = 150, guild = "Cit" },
        Spass_BX_Watchman = { base = "Peasant_1_01", name = "Watchman Rolf", level = 4, hp_max = 250, guild = "Cit" },
    },
}

-- Installs the spec. `on_applied(when, report)` is called after every application (the test uses it). Call from main.lua, AFTER the asset files were made:
-- the `mesh` name of the club is checked when the spec is applied on the first tick.
function M.install(on_applied)
    return risen.balance.install(M.SPEC, { on_applied = on_applied })
end

return M
