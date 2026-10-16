-- EXAMPLE 18: NPCs that look the other way -- while a game variable is set nobody reacts to the player's thefts.
--
-- What you learn
--   * How the game reacts to a theft. Taking an item or opening a chest/cupboard inside a watched area (a house, a camp: an area with a security level such as SL_Public) is theft. The game's
--     scripts ask *every* NPC of the world with the script `AssessTheft(npc, item_or_chest)`; the answer is 1 when that NPC takes note (the script has recorded what he will say and do:
--     comment, watch, threaten) and 0 when he does not. Whether he really notices is decided afterwards: he must be within 700 units of the player and see him (the AI's CanSee).
--   * `risen.ai.perceive("theft", fn, { all = true })` hooks that script for all NPCs (`{ npc = entity | { entities } }` for chosen ones). The handler gets `ctx.self` (the NPC) and
--     `ctx.other` (the item or chest). Returning **0** means "this NPC does not react": the game's own script does not run, nothing is recorded, nobody comments or attacks.
--     Returning nil lets the game's own reaction run (and the next handler); `ctx:call_original()` runs it from inside the handler, so you can look at its answer and still change it.
--   * The hook is on the script, not on a way of stealing, so it works for the game's animated take (`risen.interact.begin`, the use key) and for anything that asks the same question, such as
--     the standalone `quickloot` mod (right mouse button). While the player stands at a chest the game asks again every second through `AssessPlayerLooting`: that goes through the hook too.
--   * A game variable (`risen.gamevar`) is the simplest switch: it is part of the savegame, other mods and the game's own dialogues can set it (`SetGameEvent`), and `gamevar.changed` tells
--     when it flipped. A quest status (`risen.quest.status`) or an item in the inventory work the same way.
--
-- Run the live check (about a minute; see docs/EXAMPLES.md):
--   .\scripts\test-run.ps1 -Instance 1 -Mods example_ignore_theft,quickloot -WaitForResult -Seconds 300 -Scenario ex_ignore_theft -ShowChecks

local VARIABLE = "EX_ThievesFriend"        -- set it to anything but 0 and the NPCs ignore thefts

local stats = { asked = 0, ignored = 0 }   -- what the scenario looks at

risen.ai.perceive("theft", function(ctx)
    stats.asked = stats.asked + 1
    local v = risen.gamevar.get(VARIABLE)
    if v ~= nil and v ~= 0 then
        stats.ignored = stats.ignored + 1
        return 0                           -- AssessTheft's "no reaction"
    end
    return nil                             -- the game's own reaction
end, { all = true })

if risen.test.is_active() then
    require("test_example_ignore_theft")(stats, VARIABLE)
end
