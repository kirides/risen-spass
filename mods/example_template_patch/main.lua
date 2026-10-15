-- EXAMPLE 1: patch existing templates (items and NPCs) with risen.template.patch.
--
-- What you learn
--   * Every item / NPC / monster is made from a *template* (docs/TEMPLATES.md lists the names; docs/ITEM_PROPERTIES.md and
--     docs/NPC_PROPERTIES.md say which property of which property set holds which number).
--   * risen.template.patch(name, { PropertySet = { Property = value } }, { instances = true|false }) edits a template at runtime. It
--     validates everything first and writes nothing when anything is wrong (nil, "reason").
--   * Edits live in memory only. Apply them again in EVERY game process, at the right moments:
--       - first `tick`        earliest moment the templates resolve (`mods.loaded` is too early). Edits reach new spawns, items handed
--                             out and inventory stacks at once.
--       - every `session.start`  (new game AND savegame loaded): a world keeps its own copies of the values, so write the edit into the
--                             live entities as well: `instances = true` (only the listed properties, hit points etc. stay untouched).
--   * Nothing here needs the player to do anything; open the game, look at the apple in the inventory (value 77).
--
-- Run the live check (about a minute, needs the game; see docs/EXAMPLES.md):
--   .\scripts\test-run.ps1 -Instance 2 -Mods example_template_patch -WaitForResult -Seconds 200 -Scenario example_template_patch -ShowChecks

-- property set -> property -> new value. Numbers and booleans may be written as numbers/booleans, enum values by their engine name.
local EDITS = {
    It_Apple    = { gCItem_PS = { GoldValue = 77 } },                                   -- apples are precious now
    It_1H_Sword = { gCItem_PS = { GoldValue = 321 }, gCDamage_PS = { DamageAmount = 33 } },
    Peasant_1_01 = { gCNPC_PS = { Guild = "gEGuild_Cit" } },                            -- spawned Peasant_1_01 become citizens
}

local last = {}   -- what the last application did, for the test: { [template] = { applied = n, touched = n } }

local function apply(into_live_entities)
    for name, sets in pairs(EDITS) do
        local ok, applied, touched = risen.template.patch(name, sets, { instances = into_live_entities })
        if ok then
            last[name] = { applied = applied, touched = touched or 0 }
            risen.log.info(("example_template_patch: %s: %d values written%s"):format(name, applied,
                into_live_entities and (", %d live entities"):format(touched or 0) or ""))
        else
            risen.log.warn(("example_template_patch: cannot patch %s: %s"):format(name, tostring(applied)))
        end
    end
end

-- 1. earliest moment: templates only (no world exists at the main menu)
local first = true
risen.on("tick", function()
    if first then
        first = false
        apply(false)
    end
end)

-- 2. a new game or a loaded savegame: the world's entities keep their own values, so write into them too
risen.on("session.start", function() apply(true) end)

-- the live check belongs to the test run only
if risen.test.is_active() then
    require("test_template_patch")(function() return last end)
end
