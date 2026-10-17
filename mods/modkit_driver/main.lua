-- Checks the exported risen-modkit showcase (mods/modkit_showcase, exported from examples/modkit/showcase.rmk.json) in a running game:
--   .\scripts\test-run.ps1 -Mods modkit_showcase,modkit_driver -WaitForResult -Seconds 240 -Scenario modkit
-- What the editor promised is what the engine holds: new items with their numbers and texts, changes of shipped items, the item group.
local T = risen.test

T.plan {
    "the export was applied without failures",
    "new item exists",
    "new item numbers",
    "new item requirement",
    "new item label",
    "new item can be given",
    "new potion modifier",
    "changed item numbers",
    "changed item label",
    "free property written",
    "item group applied",
}

local applied = {}   -- when -> report
risen.on("modkit.applied", function(id, when, report)
    if id == "modkit_showcase" then applied[when] = report end
end)

local function prop(name, set, key)   -- get_property returns the value as text
    local t = risen.template.get(name)
    return t and tonumber(t:get_property(set, key))
end

T.run(function()
    T.wait_event("session.start", 240, function(mode) return mode == "new" end)
    T.wait_until(function() return applied["session.start (new)"] ~= nil end, 30, "modkit.applied on session.start")
    local rep = applied["session.start (new)"]
    T.expect("the export was applied without failures", rep.ok and #(rep.failed or {}) == 0, table.concat(rep.failed or {}, "; "))

    T.expect("new item exists", risen.template.exists("MK_Hob_Knife"))
    T.eq("new item numbers", { prop("MK_Hob_Knife", "gCItem_PS", "GoldValue"), prop("MK_Hob_Knife", "gCDamage_PS", "DamageAmount") }, { 250, 20 })
    local req = risen.item.requirements("MK_Hob_Knife") or {}
    T.eq("new item requirement", req.Strength, 12)
    T.eq("new item label", risen.loc.get("FO_MK_Hob_Knife"), "Hob's Knife")
    local player = assert(risen.entity.player(), "no player")
    risen.item.give(player, "MK_Hob_Knife", 1)
    T.eq("new item can be given", risen.item.count(player, "MK_Hob_Knife"), 1)
    local hp
    for _, m in ipairs(risen.item.modifiers("MK_Strong_Potion") or {}) do
        if m.skill == "HitPoints" then hp = m.amount end
    end
    T.eq("new potion modifier", hp, 120)

    T.eq("changed item numbers", { prop("It_1H_Sword_Rusty", "gCItem_PS", "GoldValue"), prop("It_1H_Sword_Rusty", "gCDamage_PS", "DamageAmount") }, { 7, 9 })
    T.eq("changed item label", risen.loc.get("FO_It_1H_Sword_Rusty"), "Very rusty sword")
    T.eq("free property written", { prop("It_Apple", "gCItem_PS", "SortValue"), prop("It_Apple", "gCItem_PS", "GoldValue") }, { 77, 3 })

    local group
    for _, label in ipairs(rep.applied or {}) do
        if tostring(label):find("Items/Plants", 1, true) then group = label end
    end
    T.expect("item group applied", group ~= nil, table.concat(rep.applied or {}, " | "))
    T.finish()
end, "modkit")
