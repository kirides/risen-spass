---@diagnostic disable: need-check-nil
-- Local check of the Risen Extended Mod's converted EDITOR PROJECT in the running game (docs/PACK2MOD.md "Editor project"): the project (risen-pack2mod --project)
-- exported by risen-modkit into mods/extproj by scripts/pack2project-live.ps1 -- items, texts, template patches, XP and refills on shipped infos, teaching limits,
-- container fills. Needs the user's own copy of the mod, so it is not part of the smoke suite.
--   .\scripts\pack2project-live.ps1
local T = risen.test

T.plan {
    "a new item has its numbers",
    "a new item's modifier list keeps the order of the mod",
    "a changed shipped item has its numbers and requirements",
    "a new item's text is served",
    "a monster's actor is swapped",
    "a template patch of the project is applied",
    "an XP reward is added to a shipped info",
    "an XP reward is changed on a shipped info",
    "the refills are on the chapter 1 info",
    "the author's property edits of shipped infos are applied",
    "an entity the author moved stands where the mod puts it",
    "a teaching limit is raised",
    "a chest of the world holds the mod's items",
    "no errors logged",
}

local function last_command_with(inf, name, value)
    for i = inf:command_count(), 1, -1 do
        if inf:get_command_property(i, name) == value then return i end
    end
end

T.run(function()
    T.section("new game")
    T.wait_event("session.start", 180, function(m) return m == "new" end)
    T.sleep(300)

    T.section("items and texts")
    local ruby = risen.template.get("It_1H_Blade_Ruby")
    T.expect("a new item has its numbers", ruby and tonumber(ruby:get_property("gCItem_PS", "GoldValue")) == 500 and tonumber(ruby:get_property("gCDamage_PS", "DamageAmount")) == 60,
        ruby and (ruby:get_property("gCItem_PS", "GoldValue") .. " / " .. ruby:get_property("gCDamage_PS", "DamageAmount")) or "no template")
    local got = {}
    for _, m in ipairs(risen.item.modifiers("It_1H_Blade_Ruby") or {}) do got[#got + 1] = ("%s:%d:%s"):format(m.skill, m.amount, m.mode) end
    T.expect("a new item's modifier list keeps the order of the mod", table.concat(got, ",") == "Sword:1:add,Strength:1:add", table.concat(got, ","))
    local req = risen.item.requirements("It_1H_Katana") or {}
    local katana = risen.template.get("It_1H_Katana")
    T.expect("a changed shipped item has its numbers and requirements", katana and tonumber(katana:get_property("gCDamage_PS", "DamageAmount")) == 80 and req.Dexterity == 140 and req.Strength == 60,
        ("damage %s dex %s str %s"):format(katana and tostring(katana:get_property("gCDamage_PS", "DamageAmount")) or "?", tostring(req.Dexterity), tostring(req.Strength)))
    T.expect("a new item's text is served", risen.loc.get("FO_It_1H_Blade_Ruby") == "Ruby Blade", tostring(risen.loc.get("FO_It_1H_Blade_Ruby")))

    T.section("template edits")
    local priest = risen.template.get("LizardPriest_Elite")
    local actor = priest and priest:get_property("eCAnimation_PS", "ResourceFilePath")
    T.expect("a monster's actor is swapped", actor and tostring(actor):find("Priest_High", 1, true) ~= nil, tostring(actor))
    local light = risen.template.get("It_Cast_Light")
    local range = light and light:get_property("eCPointLight_PS", "Range")
    T.expect("a template patch of the project is applied", tonumber(range) == 1500, "Range = " .. tostring(range))

    T.section("shipped dialogue infos")
    local fred = risen.dialog.get("FILLERX2_00029")
    T.expect("an XP reward is added to a shipped info", fred and fred:get_command_property(fred:command_count(), "Amount") == "100", fred and tostring(fred:get_command_property(fred:command_count(), "Amount")) or "no info")
    local cathy = risen.dialog.get("FILLERX2_00646")
    T.expect("an XP reward is changed on a shipped info", cathy and last_command_with(cathy, "Amount", "150") ~= nil, "")
    local chap = risen.dialog.get("PANKRATZX2_01754")
    T.expect("the refills are on the chapter 1 info", chap and last_command_with(chap, "Other", "It_Helmet_Leather") ~= nil, "")

    T.section("world")
    local esteban = risen.dialog.get("PANKRATZX2_00964")
    local graves = risen.dialog.get("PANKRATZX2_00736")
    local cole = risen.dialog.get("FILLERX2_00743")
    local last = graves and graves:condition_count() or 0
    T.expect("the author's property edits of shipped infos are applied",
        esteban and esteban:get_command_property(4, "Amount") == "400" and last > 0 and graves:get_condition_property(last, "PlayerKnows") == "Ursegor_FindGraves" and
            cole and cole:get_command_property(4, "Item") == "It_Bow_Cole_01",
        ("amount %s, conditions %d, give %s"):format(esteban and tostring(esteban:get_command_property(4, "Amount")) or "?", last, cole and tostring(cole:get_command_property(4, "Item")) or "?"))
    local brogar = risen.entity.by_id("c685f868-bfd1-439c-8c25-1b0627f9daaa")
    local bx, by, bz
    if brogar then bx, by, bz = brogar:position() end
    local dist = bx and math.sqrt((bx + 27325.77) ^ 2 + (bz - 34831.31) ^ 2) or -1
    T.expect("an entity the author moved stands where the mod puts it", dist >= 0 and dist < 400, "distance " .. tostring(dist))
    local sam = risen.entity.by_id("3d71aefb-0ee1-47bd-9b11-02fec2149ef1")
    local limit
    for _, e in ipairs(sam and risen.npc.teach_skills(sam) or {}) do
        if e.skill == "gESkill_Stat_DEX" then limit = e.max end
    end
    T.expect("a teaching limit is raised", limit == 200, "limit = " .. tostring(limit))
    local chest = risen.entity.by_id("28e2d7f0-1e5e-4b6d-a0e2-5dfecef81841")
    local pearls = chest and risen.item.count(chest, "It_BlackPearl") or -1
    T.expect("a chest of the world holds the mod's items", pearls >= 1, "pearls " .. tostring(pearls))
    T.finish("extproj done")
end, "extproj")
