-- EXAMPLE 11: spells -- change the shipped ones and build a new one from the pieces of an existing spell.
--
-- What you learn
--   * A spell is a set of items. A projectile spell (Fireball, Frost, Missile) is cast with a CRYSTAL and shoots a projectile template; every other spell
--     (Light, Inferno, Skeleton, the heals, ...) comes as RUNE, SCROLL and ORB items. The numbers: mana cost, requirement and "used up on cast" live in the item
--     (gCCastInfo_PS), damage, speed and radius in the projectile (gCDamage_PS, gCProjectile2_PS). risen.spell.names() lists the spells, risen.spell.get(name)
--     shows the numbers the templates hold now; docs/SPELL_PROPERTIES.md says where everything lives (and which numbers are fixed in the game's scripts).
--   * `spells` edits a shipped spell: `mana_cost`, `damage`, `requires`, `consume`, `scroll_mana_cost`, relative `mana_mult` / `damage_mult` / `speed_mult` ...
--     (relative values are computed from the shipped ones, so applying twice never compounds).
--   * `new_spells` builds a new spell from a shipped one: every form (crystal, scroll; for rune spells rune, scroll, orb) is a new item template with a name that
--     is derived into a fixed ID, so savegames that contain it load again. A new projectile spell keeps the shipped projectile template; its own numbers
--     (`damage_mult = 2`) are written into the projectile of every shot of its crystal -- the engine chooses the projectile per spell TYPE, not per crystal.
--   * risen.balance.install applies the spec on the first `tick` (templates) and on every `session.start` (live entities), like for items and NPCs.
--
-- Run the live check (about a minute; see docs/EXAMPLES.md):
--   .\scripts\test-run.ps1 -Instance 2 -Mods example_spell -WaitForResult -Seconds 240 -Scenario example_spell -ShowChecks

local SPEC = {
    spells = {
        -- the shipped crystal costs 8 mana and its projectile does 100 damage (Fireball skill adds 25 per level on a hit)
        Fireball = { mana_cost = 10, damage = 140 },
        -- a rune spell: the rune costs 10 mana, the scroll 5; the orb is free
        Light = { mana_cost = 5, scroll_mana_cost = 2 },
    },
    new_spells = {
        -- a Fireball with double damage for double the mana: a crystal and a scroll (the scroll is the crystal that is used up by the cast, half the cost)
        Spass_Fireball_Strong = {
            base = "Fireball",
            name = { English = "Strong Fireball", German = "Starker Feuerball" },
            description = "A fireball that hits twice as hard.",
            mana_cost = 20,                  -- the crystal; the scroll costs half
            requires = { Fireball = 2 },     -- needs the second fireball circle
            damage_mult = 2,                 -- of the (edited) Fireball projectile: 140 -> 280
            forms = { "crystal", "scroll" },
        },
    },
}

local reports = {}   -- what install() reported, for the test
risen.balance.install(SPEC, {
    on_applied = function(when, report) reports[#reports + 1] = { when = when, report = report } end,
})

-- the live check belongs to the test run only
if risen.test.is_active() then
    require("test_spell")(SPEC, reports)
end
