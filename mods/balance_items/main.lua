-- Item balancing with risen.balance (docs/LUA_API.md "risen.balance", docs/RE.md section 23).
--
-- An item's numbers live in its template: gold value (gCItem_PS.GoldValue), weapon damage (gCDamage_PS.DamageAmount), the equip conditions
-- (gCItem_PS.RequiredSkills) and the effects of wearing/using it (gCItem_PS.ModifySkills: armor protection, potion healing, ring bonuses).
-- risen.balance.install applies the table below at the right moments: into the templates on the first `tick`, and again -- now also into
-- the live entities of each template -- on every `session.start`, because a world (savegame or new game) keeps its own copies.
--
-- Every verification below is a risen.test check (scripts/smoke-test.ps1 -Scenario balance collects them; outside a test run they only log);
-- the `balance_items:` lines stay as the narrative.
local log = risen.log
local T = risen.test

local SPEC = {
    items = {
        -- value and damage are plain properties; `requires` replaces the equip condition (here: 5 Strength instead of 10)
        It_1H_Sword = { value = 120, damage = 25, requires = { Strength = 5 } },
        -- nobody can wield this one now
        It_2H_TwoHandedSword = { requires = { Strength = 999 } },
        -- armor protection is a list of skill modifiers applied while the armor is worn
        It_Armor_Guard = {
            value = 1500,
            requires = { Strength = 5 },
            modifies = { ProtEdge = 35, ProtBlunt = 25, ProtPoint = 20, ProtFire = 10, ProtIce = 10, ProtMagic = 10 },
        },
        -- a potion heals through a modifier of the current hit points
        It_Po_Health_01 = { value = 99, modifies = { HitPoints = 40 } },
        It_Ri_STR_Small = { modifies = { Strength = 4 } },
        -- runes and scrolls: the mana cost is gCCastInfo_PS.ManaCost, the circle needed is a requirement
        It_Ru_Inferno = { mana_cost = 60, requires = { Circle = 3 } },
        -- lots of these lie around in the world: the instances = true pass of session.start rewrites them
        It_Plant_Berry = { value = 33, modifies = { ManaPoints = 9 } },
    },
}

-- on_applied: the report of every application (first tick: templates; session.start: with the live entities)
risen.balance.install(SPEC, {
    on_applied = function(when, report)
        if when == "first tick" then
            T.expect("the spec is applied on the first tick", report.ok and #report.applied == 7 and #report.failed == 0,
                ("applied %d, failed %d"):format(#report.applied, #report.failed))
        elseif when == "session.start (new)" then
            T.expect("session.start writes the live entities", report.ok and #report.applied == 7 and #report.failed == 0 and report.instances > 0,
                ("applied %d, failed %d, %d live entities written"):format(#report.applied, #report.failed, report.instances))
        end
    end,
})

local function fmt_mods(list)
    local parts = {}
    for _, m in ipairs(list or {}) do parts[#parts + 1] = ("%s%+d"):format(m.skill, m.amount) end
    return table.concat(parts, ",")
end
local function fmt_req(map)
    local parts = {}
    for k, v in pairs(map or {}) do parts[#parts + 1] = k .. "=" .. v end
    table.sort(parts)
    return table.concat(parts, ",")
end
local function L(...)
    local t = {}
    for i = 1, select("#", ...) do t[#t + 1] = tostring((select(i, ...))) end
    log.info("balance_items: " .. table.concat(t, " "))
end

local verify_in = nil
local verified = false
risen.on("session.start", function()  -- frames; let the world settle first
    if not verified then verify_in = 200 end
end)

risen.on("tick", function()
    if not verify_in then return end
    verify_in = verify_in - 1
    if verify_in > 0 then return end
    verify_in = nil
    verified = true

    local player = risen.entity.player()
    if not player then log.warn("balance_items: no player to verify with") return end
    local x, y, z = player:position()
    x, y, z = x or 0, y or 0, z or 0

    -- 1. the templates themselves
    L("template sword: requires", fmt_req(risen.item.requirements("It_1H_Sword")), "value",
        risen.template.get("It_1H_Sword"):get_property("gCItem_PS", "GoldValue"), "damage",
        risen.template.get("It_1H_Sword"):get_property("gCDamage_PS", "DamageAmount"))
    local tr = assert(risen.template.get("It_Ru_Inferno"))
    L("template rune: mana", tr:get_property("gCCastInfo_PS", "ManaCost"), "requires", fmt_req(risen.item.requirements("It_Ru_Inferno")))
    L("template armor: modifies", fmt_mods(risen.item.modifiers("It_Armor_Guard")))
    local tsword = assert(risen.template.get("It_1H_Sword"))
    T.eq("the template carries the new numbers", { fmt_req(risen.item.requirements("It_1H_Sword")), tostring(tsword:get_property("gCItem_PS", "GoldValue")),
        tostring(tsword:get_property("gCDamage_PS", "DamageAmount")) }, { "Strength=5", "120", "25" })
    T.eq("the armor modifiers read back by name", fmt_mods(risen.item.modifiers("It_Armor_Guard")),
        "ProtBlunt+25,ProtEdge+35,ProtFire+10,ProtIce+10,ProtMagic+10,ProtPoint+20")
    T.eq("a rune: mana cost property and circle requirement", { tostring(tr:get_property("gCCastInfo_PS", "ManaCost")), fmt_req(risen.item.requirements("It_Ru_Inferno")) },
        { "60", "Circle=3" })

    -- 2. a new spawn
    local sword = risen.entity.spawn("It_1H_Sword", x + 100, y + 10, z)
    L("spawn sword: requires", fmt_req(sword and sword:requirements()), "value", sword and sword:item_value(),
        "damage", sword and sword:get_property("gCDamage_PS", "DamageAmount"))
    T.eq("a new spawn uses the new numbers", { fmt_req(sword and sword:requirements()), sword and sword:item_value(),
        tostring(sword and sword:get_property("gCDamage_PS", "DamageAmount")) }, { "Strength=5", 120, "25" })

    -- 3. an item in an inventory (the stack references the template)
    risen.item.give(player, "It_Armor_Guard", 1)
    local stacks, stacks_ok = 0, true
    for i = 0, player:stack_count() - 1 do
        local it = player:stack_item(i)
        if it and it:name() == "It_Armor_Guard" then
            L("inventory armor: requires", fmt_req(it:requirements()), "modifies", fmt_mods(it:modifiers()), "value", it:item_value())
            stacks = stacks + 1
            stacks_ok = stacks_ok and fmt_req(it:requirements()) == "Strength=5" and it:item_value() == 1500
                and fmt_mods(it:modifiers()) == "ProtBlunt+25,ProtEdge+35,ProtFire+10,ProtIce+10,ProtMagic+10,ProtPoint+20"
        end
    end
    T.expect("an inventory stack follows the template", stacks >= 1 and stacks_ok, stacks .. " stack(s) of It_Armor_Guard")

    -- 4. entities that existed before the edit (the world's berries)
    local tpl = assert(risen.template.get("It_Plant_Berry"))
    local n, good = 0, 0
    for _, e in ipairs(tpl:instances(1000000) or {}) do
        n = n + 1
        local m = e:modifiers()
        if m and #m == 1 and m[1].skill == "ManaPoints" and m[1].amount == 9 and e:item_value() == 33 then good = good + 1 end
    end
    L("world berries: n=" .. n, "patched=" .. good)
    T.expect("every world berry was rewritten", n >= 1 and n == good, ("n=%d patched=%d"):format(n, good))

    -- 5. the engine's own equip check against the edited requirements
    local str = player:skill_total("Strength")
    L("player strength", str)
    local can_sword, can_two_hander = risen.item.can_use(player, "It_1H_Sword"), risen.item.can_use(player, "It_2H_TwoHandedSword")
    L("can_use sword (needs 5):", can_sword)
    L("can_use two-hander (needs 999):", can_two_hander)
    T.eq("the engine check passes with enough Strength", can_sword, true, nil)
    T.eq("the engine check fails for 999 Strength", can_two_hander, false)
    -- raising the requirement of the sword above the player's strength makes the very same check fail, lowering it makes it pass
    risen.item.set_requirements("It_1H_Sword", { Strength = 500 })
    local can_500 = risen.item.can_use(player, "It_1H_Sword")
    L("can_use sword after requires Strength=500:", can_500)
    T.eq("raising the requirement fails the check", can_500, false)
    risen.item.set_requirements("It_1H_Sword", { Strength = 1 })
    local can_1 = risen.item.can_use(player, "It_1H_Sword")
    L("can_use sword after requires Strength=1:", can_1)
    T.eq("lowering the requirement passes the check", can_1, true)
    risen.item.set_requirements("It_1H_Sword", {})
    L("can_use sword after requires nothing:", risen.item.can_use(player, "It_1H_Sword"))
    risen.item.set_requirements("It_1H_Sword", { Strength = 5 })

    -- 6. wearing the armor applies its modifiers (the protection skills of the wearer)
    local before = player:skill_total("ProtEdge")
    L("equip: ProtEdge before", before)
    risen.item.give(player, "It_2H_TwoHandedSword", 1)
    local equipped = player:equip_all()
    local after = player:skill_total("ProtEdge")
    L("equip: equip_all=" .. tostring(equipped), "ProtEdge after", after, "delta", (after or 0) - (before or 0))
    T.eq("wearing armor applies its modifiers", { equipped, after, (after or 0) - (before or 0) }, { 1, 35, 35 })
    -- the two-hander (needs 999 Strength) stayed in the bag; once its requirement is lowered the very same equip call takes it
    risen.item.set_requirements("It_2H_TwoHandedSword", { Strength = 1 })
    local equipped_again = player:equip_all()
    L("equip: after lowering the two-hander requirement equip_all=" .. tostring(equipped_again))
    T.eq("a lowered requirement lets the engine equip it", equipped_again, 2)
    risen.item.set_requirements("It_2H_TwoHandedSword", { Strength = 999 })

    -- an entity-level edit touches that entity only
    local copy = risen.entity.spawn("It_1H_Sword", x + 120, y + 10, z)
    local edited = copy and copy:set_requirements({ Dexterity = 3 })
    local entity_req, template_req = fmt_req(copy and copy:requirements()), fmt_req(risen.item.requirements("It_1H_Sword"))
    L("entity edit:", edited, "entity", entity_req, "template", template_req)
    T.eq("an entity-level edit leaves the template alone", { edited, entity_req, template_req }, { true, "Dexterity=3", "Strength=5" })

    -- 7. using a potion up applies its modifiers (HitPoints +40 instead of +75)
    risen.item.give(player, "It_Po_Health_01", 1)
    player:set_hp(10)
    local hp0 = player:hp()
    L("consume potion:", risen.item.consume(player, "It_Po_Health_01"))
    local hp1, hp_max, left = player:hp(), player:hp_max(), risen.item.count(player, "It_Po_Health_01")
    L("potion: hp", hp0, "->", hp1, "max", hp_max, "potions left", left)
    T.eq("using a potion applies its modifiers", { hp0, hp1, hp_max, left }, { 10, 50, 80, 0 })

    -- 8. validation: nothing is written when something in the spec is wrong
    local rep, why = risen.balance.apply({ items = { It_1H_Sword = { value = 1, requires = { Strenght = 5 } } } })
    L("bad spec:", rep == nil, why)
    T.expect("a bad skill name is refused with the list", rep == nil and why ~= nil
        and why:find("items.It_1H_Sword: requires: unknown skill 'Strenght' (valid: Strength, Dexterity", 1, true) ~= nil, why)
    local rep2, why2 = risen.balance.apply({ items = { It_Nope = { value = 1 } }, no_such_section = {} })
    L("bad spec 2:", rep2 == nil, why2)
    T.expect("an unknown template and section are refused", rep2 == nil and why2 ~= nil
        and why2:find("items.It_Nope: unknown template .*unknown section 'no_such_section'") ~= nil, why2)
    local value_after = risen.template.get("It_1H_Sword"):get_property("gCItem_PS", "GoldValue")
    L("sword value after the rejected specs", value_after)
    T.eq("a rejected spec writes nothing", tostring(value_after), "120")
    L("verify done")
    risen.gamevar.set("SPASS_BALANCE_ITEMS_DONE", 1)  -- lets balance_driver start (it runs in the same scenario)
end)
