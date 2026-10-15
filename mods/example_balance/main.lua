-- EXAMPLE 2: balancing with risen.balance -- one declarative table for items, NPCs and monsters.
--
-- What you learn
--   * risen.balance.install(SPEC) validates the whole table (templates, setting names, skill names, ranges), then applies it
--     on the first `tick` (templates: new spawns, items handed out, inventory stacks, the world of a new game's *future* spawns) and again
--     on every `session.start` (now also into the live entities: the world's items and monsters keep their own copies).
--     Applying twice is harmless (relative settings are computed from the shipped values and never compound).
--   * `items`: value, damage, requires (equip condition), modifies (effect of wearing/using: armor protection, potion healing, ring
--     bonuses), mana_cost, use_type, category, properties = { Set = { Property = value } } for anything else.
--   * `npcs`: hp_max, hp, level, xp (reward of a kill, via the level), skills (Strength, ProtEdge, Sword, ...), guild, bravery,
--     damage_mult, loot = { Template = amount } (what spawned NPCs carry) and properties = { ... }.
--   * `npc_groups` / `item_groups`: the same settings (also relative ones: hp_mult, level_add, value_mult, ...) for every template
--     that a selector matches (a Lua pattern on the name, a template group path of docs/TEMPLATES.md, a list of names).
--   * Skill names: risen.balance.skills(); Strength/Dexterity/Intelligence, ProtEdge/ProtBlunt/ProtPoint/ProtFire/ProtIce/ProtMagic,
--     Sword/Axe/Staff/Bow/CrossBow, HitPoints, ManaPoints, ...
--
-- Run the live check (about a minute; see docs/EXAMPLES.md):
--   .\scripts\test-run.ps1 -Instance 2 -Mods example_balance -WaitForResult -Seconds 240 -Scenario example_balance -ShowChecks

local SPEC = {
    items = {
        -- a cheaper, stronger sword that a weaker character can wield (the default requirement is Strength 10)
        It_1H_Sword = { value = 120, damage = 25, requires = { Strength = 5 } },
        -- nobody can use this one now
        It_2H_TwoHandedSword = { requires = { Strength = 999 } },
        -- armor: protection is a list of skill modifiers applied while the armor is worn
        It_Armor_Guard = { value = 1500, modifies = { ProtEdge = 35, ProtBlunt = 25, ProtPoint = 20 } },
        -- a potion heals through a modifier of the current hit points
        It_Po_Health_01 = { modifies = { HitPoints = 40 } },
    },
    npcs = {
        -- absolute numbers: a wolf with 500 hit points that hits like Strength 90 and pays 200 experience (= level 20)
        Wolf = { hp_max = 500, xp = 200, skills = { Strength = 90, ProtEdge = 60 }, bravery = "Brave", loot = { It_Apple = 3 } },
        -- relative numbers (relative to the SHIPPED values): twice the hit points, two levels more
        Boar = { hp_mult = 2, level_add = 2 },
    },
    npc_groups = {
        -- every template whose name matches a Lua pattern; also { group = "Monster/Kapitel 2", ... } for a template group
        { match = "^Stingrat", hp_mult = 1.5, damage_mult = 2 },
    },
    item_groups = {
        -- halve the gold value of every potion (the health potion's shipped 30 becomes 15)
        { group = "Items/Potions", value_mult = 0.5 },
    },
}

local reports = {}   -- what install() reported, for the test
risen.balance.install(SPEC, {
    on_applied = function(when, report) reports[#reports + 1] = { when = when, report = report } end,
})

-- the live check belongs to the test run only
if risen.test.is_active() then
    require("test_balance")(SPEC, reports)
end
