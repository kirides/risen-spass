-- SHOWCASE: one mod that uses the whole stack. Copy this folder, rename the ids, change the numbers and the story.
--
--   balance.lua   the NUMBERS in one declarative table (risen.balance.install): shipped items re-balanced (sword, health potion, all potions), shipped monsters
--                 (Wolf absolute, Boar relative), the Fireball spell; NEW things made from existing ones: a sword (the recipe's result), a club with its own mesh and
--                 texture, a Strong Fireball (crystal and scroll), a smithing recipe (regular sword + 2 iron ore -> Slightly Improved Sword at the anvil), three NPC templates.
--   assets.lua    the files of that club (mesh, material, texture), derived from shipped ones at run time because this repository ships no artwork. A real mod puts its
--                 files into assets/ and needs no code.
--   story.lua     Brogar the Smith (persistent NPC, voiced reply, iron-ore quest, dialogue options the ENGINE hides unless the world says so), Tilda the Apprentice
--                 (appears when the quest succeeded, has a daily routine) and Watchman Rolf (behaviour written in Lua).
--   README.md     what it does, how to play it, how to test it.
--
-- The order below matters and is the same in every mod of this kind:
--   1. everything that must exist before a savegame is read is declared while the mod loads (balance.install, risen.script.define, risen.npc.persistent, risen.spawn.when,
--      risen.ai.state): the layer applies/creates it on the first `tick` and on every `session.start`;
--   2. files a template refers to are written BEFORE the first tick's balance application (a tick handler with a higher priority).
--
-- Live check (about 3 minutes, two game processes: a new game, then a savegame loaded in a fresh process):
--   .\scripts\smoke-test.ps1 -Scenario showcase          or by hand:  .\scripts\test-run.ps1 -Mods balance_example -WaitForResult -Seconds 400 -Scenario showcase -Param phase=play
local assets = require("assets")
local balance = require("balance")
local story = require("story")

-- 1. the club's files, before the templates are applied (the first tick; later games find them already mounted)
local assets_state = { ok = nil, why = nil }
local built = false
risen.on("tick", function()
    if built then return end
    built = true
    assets_state.ok, assets_state.why = assets.ensure()
    if not assets_state.ok then risen.log.warn("balance_example: the club's files could not be made: " .. tostring(assets_state.why)) end
end, 100)

-- 2. the numbers and the new content
local reports = {}      -- what install() reported, for the test
balance.install(function(when, report) reports[#reports + 1] = { when = when, report = report } end)

-- the live check belongs to the test run only
if risen.test.is_active() then
    require("test_showcase")(story, balance, assets_state, reports)
end
