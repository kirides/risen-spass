-- Item balancing with risen.balance (docs/LUA_API.md "risen.balance", docs/RE.md section 23).
--
-- An item's numbers live in its template: gold value (gCItem_PS.GoldValue), weapon damage (gCDamage_PS.DamageAmount), the equip conditions
-- (gCItem_PS.RequiredSkills) and the effects of wearing/using it (gCItem_PS.ModifySkills: armor protection, potion healing, ring bonuses).
-- risen.balance.install applies the table below at the right moments: into the templates on the first `tick`, and again -- now also into
-- the live entities of each template -- on every `session.start`, because a world (savegame or new game) keeps its own copies.
--
-- Every `balance_items:` line is checked by scripts/smoke-test.ps1.
local log = risen.log

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

risen.balance.install(SPEC)

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

    -- 2. a new spawn
    local sword = risen.entity.spawn("It_1H_Sword", x + 100, y + 10, z)
    L("spawn sword: requires", fmt_req(sword and sword:requirements()), "value", sword and sword:item_value(),
        "damage", sword and sword:get_property("gCDamage_PS", "DamageAmount"))

    -- 3. an item in an inventory (the stack references the template)
    risen.item.give(player, "It_Armor_Guard", 1)
    for i = 0, player:stack_count() - 1 do
        local it = player:stack_item(i)
        if it and it:name() == "It_Armor_Guard" then
            L("inventory armor: requires", fmt_req(it:requirements()), "modifies", fmt_mods(it:modifiers()), "value", it:item_value())
        end
    end

    -- 4. entities that existed before the edit (the world's berries)
    local tpl = assert(risen.template.get("It_Plant_Berry"))
    local n, good = 0, 0
    for _, e in ipairs(tpl:instances(1000000) or {}) do
        n = n + 1
        local m = e:modifiers()
        if m and #m == 1 and m[1].skill == "ManaPoints" and m[1].amount == 9 and e:item_value() == 33 then good = good + 1 end
    end
    L("world berries: n=" .. n, "patched=" .. good)

    -- 5. the engine's own equip check against the edited requirements
    local str = player:skill_total("Strength")
    L("player strength", str)
    L("can_use sword (needs 5):", risen.item.can_use(player, "It_1H_Sword"))
    L("can_use two-hander (needs 999):", risen.item.can_use(player, "It_2H_TwoHandedSword"))
    -- raising the requirement of the sword above the player's strength makes the very same check fail, lowering it makes it pass
    risen.item.set_requirements("It_1H_Sword", { Strength = 500 })
    L("can_use sword after requires Strength=500:", risen.item.can_use(player, "It_1H_Sword"))
    risen.item.set_requirements("It_1H_Sword", { Strength = 1 })
    L("can_use sword after requires Strength=1:", risen.item.can_use(player, "It_1H_Sword"))
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
    -- the two-hander (needs 999 Strength) stayed in the bag; once its requirement is lowered the very same equip call takes it
    risen.item.set_requirements("It_2H_TwoHandedSword", { Strength = 1 })
    L("equip: after lowering the two-hander requirement equip_all=" .. tostring(player:equip_all()))
    risen.item.set_requirements("It_2H_TwoHandedSword", { Strength = 999 })

    -- an entity-level edit touches that entity only
    local copy = risen.entity.spawn("It_1H_Sword", x + 120, y + 10, z)
    L("entity edit:", copy and copy:set_requirements({ Dexterity = 3 }), "entity", fmt_req(copy and copy:requirements()),
        "template", fmt_req(risen.item.requirements("It_1H_Sword")))

    -- 7. using a potion up applies its modifiers (HitPoints +40 instead of +75)
    risen.item.give(player, "It_Po_Health_01", 1)
    player:set_hp(10)
    local hp0 = player:hp()
    L("consume potion:", risen.item.consume(player, "It_Po_Health_01"))
    L("potion: hp", hp0, "->", player:hp(), "max", player:hp_max(), "potions left", risen.item.count(player, "It_Po_Health_01"))

    -- 8. validation: nothing is written when something in the spec is wrong
    local rep, why = risen.balance.apply({ items = { It_1H_Sword = { value = 1, requires = { Strenght = 5 } } } })
    L("bad spec:", rep == nil, why)
    local rep2, why2 = risen.balance.apply({ items = { It_Nope = { value = 1 } }, spells = {} })
    L("bad spec 2:", rep2 == nil, why2)
    L("sword value after the rejected specs", risen.template.get("It_1H_Sword"):get_property("gCItem_PS", "GoldValue"))
    L("verify done")
end)
