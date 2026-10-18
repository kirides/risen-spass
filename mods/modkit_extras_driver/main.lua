---@diagnostic disable: need-check-nil
-- Checks the editor's extras in a running game: the exported examples/modkit/extras.rmk.json (mods/modkit_extras): template patches, an ordered modifier list with
-- modes, free string rows (CRLF kept, no foreign fallback), edits of shipped dialogue infos (XP added / changed, a trader refill), a teaching limit, and the layer's
-- `default` text of a name table.
--   .\scripts\test-run.ps1 -Instance 5 -Mods modkit_extras,modkit_extras_driver -WaitForResult -Seconds 300 -Scenario extras
local T = risen.test

T.plan {
    "the patches are applied to the templates",
    "the patch of a shipped item is applied",
    "the modifier list keeps its order and modes",
    "the requirements are all there",
    "a free string row is served",
    "a row with CRLF keeps it",
    "a row without English text is not served in English",
    "a name table's default serves the other languages",
    "recipes that name each other (and themselves) are created",
    "an XP reward is added to a shipped info",
    "an XP reward is changed on a shipped info",
    "a trader refill is added to a shipped info",
    "a command value of a shipped info is changed",
    "a template reference of a command and of a condition is changed",
    "a condition is appended to a shipped info",
    "the teaching limit of a placed NPC is written",
    "the patches are written again on every session start",
}

local function last_command_with(inf, name, value)
    for i = inf:command_count(), 1, -1 do
        if inf:get_command_property(i, name) == value then return i end
    end
end

T.run(function()
    T.section("new game")
    T.wait_event("session.start", 180, function(m) return m == "new" end)
    T.sleep(200)

    T.section("template patches")
    local light = risen.template.get("It_Cast_Light")
    local range = light and light:get_property("eCPointLight_PS", "Range")
    T.expect("the patches are applied to the templates", tonumber(range) == 1500, "Range = " .. tostring(range))
    local apple = risen.template.get("It_Apple")
    local sort = apple and apple:get_property("gCItem_PS", "SortValue")
    T.expect("the patch of a shipped item is applied", tonumber(sort) == 777, "SortValue = " .. tostring(sort))

    T.section("modifiers and requirements")
    local mods = risen.item.modifiers("MKX_Ring")
    local got = {}
    for _, m in ipairs(mods or {}) do got[#got + 1] = ("%s:%d:%s"):format(m.skill, m.amount, m.mode) end
    local want = "ProtEdge:5:add,Strength:20:percent,Dexterity:3:set"
    T.expect("the modifier list keeps its order and modes", table.concat(got, ",") == want, table.concat(got, ","))
    local req = risen.item.requirements("MKX_Ring") or {}
    T.expect("the requirements are all there", req.Strength == 30 and req.Dexterity == 20 and req.Axe == 1, ("%s %s %s"):format(tostring(req.Strength), tostring(req.Dexterity), tostring(req.Axe)))

    T.section("texts")
    T.expect("a free string row is served", risen.loc.get("MKX_TEST_BUTTON") == "Quest map", tostring(risen.loc.get("MKX_TEST_BUTTON")))
    T.expect("a row with CRLF keeps it", risen.loc.get("MKX_TEST_DOC") == "line one\r\nline two", tostring(risen.loc.get("MKX_TEST_DOC")))
    local german_only = risen.loc.get("MKX_ONLY_GERMAN")
    T.expect("a row without English text is not served in English", german_only == nil or german_only == "", tostring(german_only))
    local report, why = risen.balance.apply({ new_items = { MKX_DefaultName = { base = "It_Ri_DEX_Small", name = { German = "Nur deutsch", default = "Fallback text" } } } })
    T.expect("a name table's default serves the other languages", report and report.ok and risen.loc.get("FO_MKX_DefaultName") == "Fallback text",
        tostring(why) .. " / " .. tostring(risen.loc.get("FO_MKX_DefaultName")))

    T.section("recipes that name each other")
    T.expect("recipes that name each other (and themselves) are created", risen.template.get("MKX_Stage_A") ~= nil and risen.template.get("MKX_Stage_B") ~= nil,
        tostring(risen.template.get("MKX_Stage_A")) .. " / " .. tostring(risen.template.get("MKX_Stage_B")))

    T.section("shipped dialogue infos")
    local fred = risen.dialog.get("FILLERX2_00029")
    local n = fred and fred:command_count() or 0
    T.expect("an XP reward is added to a shipped info", fred and n > 0 and fred:get_command_property(n, "Amount") == "40", fred and tostring(fred:get_command_property(n, "Amount")) or "no info")
    local cathy = risen.dialog.get("FILLERX2_00646")
    T.expect("an XP reward is changed on a shipped info", cathy and last_command_with(cathy, "Amount", "150") ~= nil and last_command_with(cathy, "Amount", "10") == nil)
    local chap = risen.dialog.get("KALVERAMX2B_00460")
    local idx = chap and last_command_with(chap, "Other", "It_Helmet_Leather")
    T.expect("a trader refill is added to a shipped info", idx ~= nil and chap:get_command_property(idx, "Self") == "Oscar", tostring(idx))

    T.section("property edits of shipped infos")
    local esteban = risen.dialog.get("PANKRATZX2_00964")
    T.expect("a command value of a shipped info is changed", esteban and esteban:get_command_property(4, "Amount") == "400", esteban and tostring(esteban:get_command_property(4, "Amount")) or "no info")
    local cole = risen.dialog.get("FILLERX2_00743")
    local cmd_item = cole and cole:get_command_property(4, "Item")
    local cond_item = cole and cole:get_condition_property(2, "Item")
    T.expect("a template reference of a command and of a condition is changed", cmd_item == "It_Apple" and cond_item == "It_Apple", tostring(cmd_item) .. " / " .. tostring(cond_item))
    local graves = risen.dialog.get("PANKRATZX2_00736")
    local last = graves and graves:condition_count() or 0
    T.expect("a condition is appended to a shipped info", last > 0 and graves:get_condition_property(last, "PlayerKnows") == "Ursegor_FindGraves", "conditions " .. tostring(last))

    T.section("teachers")
    local sam = risen.entity.by_id("3d71aefb-0ee1-47bd-9b11-02fec2149ef1")
    local limit
    for _, e in ipairs(sam and risen.npc.teach_skills(sam) or {}) do
        if e.skill == "gESkill_Stat_DEX" then limit = e.max end
    end
    T.expect("the teaching limit of a placed NPC is written", limit == 150, "limit = " .. tostring(limit))

    T.section("session start")
    risen.template.get("It_Cast_Light"):set_property("eCPointLight_PS", "Range", "100")  -- as if the state had been lost
    risen.world.quick_save()
    T.wait_event("save.after", 60)
    risen.world.quick_load()
    T.wait_event("session.start", 120, function(m) return m == "load" end)
    T.sleep(240)
    local again = risen.template.get("It_Cast_Light"):get_property("eCPointLight_PS", "Range")
    T.expect("the patches are written again on every session start", tonumber(again) == 1500, "Range = " .. tostring(again))
    T.finish("extras done")
end, "extras")
