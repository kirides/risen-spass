-- Edits entity templates at runtime (docs/RE.md section 22, docs/LUA_API.md "risen.template").
--
-- The engine keeps one template object per template name (It_1H_Sword, Peasant_1_01, ...). New spawns, items given to an inventory and
-- the world of a *new game* are created from it, so editing the template early is enough for them. Entities that already exist
-- (a world restored from a savegame keeps its saved property values) need the edit written into them as well: risen.template.patch
-- with `instances = true` does that for exactly the properties listed. Edits are runtime-only: the mod applies them again in every process.
--
--   * earliest moment: the first `tick` (the main menu already ticks once). `mods.loaded` is too early (no template resolves yet).
--   * every `session.start` (new game or savegame loaded): apply again, now also into the live entities.
--
-- Everything is logged with the prefix "templatepatch:"; the verification at the end is a set of risen.test checks (scripts/smoke-test.ps1 -Scenario
-- templates collects them; outside of a test run they only log).
local log = risen.log
local T = risen.test

-- true when the list is not empty and every entry is `value`
local function all_are(list, value)
    for _, v in ipairs(list) do if v ~= value then return false end end
    return #list >= 1
end

local EDITS = {
    It_1H_Sword  = { gCItem_PS = { GoldValue = 777 } },
    Peasant_1_01 = { gCNPC_PS = { Guild = "gEGuild_Cit" } },
}

local function apply(into_instances)
    for name, sets in pairs(EDITS) do
        local ok, applied, touched = risen.template.patch(name, sets, { instances = into_instances })
        if ok then
            log.info(("templatepatch: patched %s: %d values%s"):format(name, applied,
                into_instances and (", %d live entities"):format(touched) or ""))
        else
            log.warn(("templatepatch: could not patch %s: %s"):format(name, tostring(applied)))
        end
    end
end

local applied_early = false
risen.on("tick", function()
    if applied_early then return end
    applied_early = true
    apply(false)
end)

local verify_in = nil
risen.on("session.start", function(mode)
    apply(true)
    verify_in = 200  -- frames; let the world settle first
end)

risen.on("tick", function()
    if not verify_in then return end
    verify_in = verify_in - 1
    if verify_in > 0 then return end
    verify_in = nil

    local player = risen.entity.player()
    if not player then log.warn("templatepatch: no player to verify with") return end
    local x, y, z = player:position()
    x, y, z = x or 0, y or 0, z or 0

    -- 1. a new spawn is created from the edited template
    local sword = risen.entity.spawn("It_1H_Sword", x + 100, y + 10, z)
    log.info("templatepatch: verify spawn: value=" .. tostring(sword and sword:item_value()) .. " (expected 777)")
    T.eq("a new spawn uses the edited template", sword and sword:item_value(), 777)

    -- 2. an item in an inventory: the stack is a reference to the template
    risen.item.give(player, "It_1H_Sword", 1)
    local stacks = {}
    for i = 0, player:stack_count() - 1 do
        local item = player:stack_item(i)
        if item and item:name() == "It_1H_Sword" then
            log.info("templatepatch: verify inventory stack " .. i .. ": value=" .. tostring(item:item_value()) .. " (expected 777)")
            stacks[#stacks + 1] = item:item_value() or -1
        end
    end
    T.expect("an inventory stack follows the template", all_are(stacks, 777),
        #stacks .. " stack(s) of It_1H_Sword, values " .. table.concat(stacks, ","))

    -- 3. entities that existed before: every live It_1H_Sword was patched (a template handle can list them)
    local tpl = assert(risen.template.get("It_1H_Sword"))
    local values = {}
    for _, e in ipairs(tpl:instances() or {}) do values[#values + 1] = e:item_value() or -1 end
    log.info(("templatepatch: verify instances: n=%d values=%s (expected all 777)"):format(#values, table.concat(values, ",")))
    T.expect("existing instances were patched", all_are(values, 777),
        ("n=%d values=%s"):format(#values, table.concat(values, ",")))

    -- 4. an NPC template property
    local peasant = risen.entity.spawn("Peasant_1_01", x - 150, y + 10, z + 150)
    log.info("templatepatch: verify npc: guild=" .. tostring(peasant and peasant:guild()) .. " (expected 4 = Cit)")
    T.eq("an NPC template property applies to spawns", peasant and peasant:guild(), 4)
    if peasant then peasant:kill() end
end)
