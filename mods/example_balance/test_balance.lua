-- Live check of example_balance (only loaded in a test run).
local T = risen.test

local function mods_text(list)
    local parts = {}
    for _, m in ipairs(list or {}) do parts[#parts + 1] = ("%s%+d"):format(m.skill, m.amount) end
    table.sort(parts)
    return table.concat(parts, ",")
end

return function(spec, reports)
    T.plan { "install reported success", "item numbers", "armor and potion modifiers", "requirements are enforced for the player", "new item spawn",
             "wolf numbers", "boar numbers (relative)", "wolf loot", "potion value (group)", "live wolves of the world" }

    T.run(function()
        T.wait_event("session.start", 180, function(m) return m == "new" end)
        T.sleep(150)
        local player = assert(risen.entity.player())
        local x, y, z = player:position()
        x, y, z = x or 0, y or 0, z or 0

        -- install(): once on the first tick (templates), once on session.start (templates + live entities)
        local bad = {}
        for _, r in ipairs(reports) do
            if not (r.report.ok and #r.report.failed == 0) then bad[#bad + 1] = r.when end
        end
        T.expect("install reported success", #reports >= 2 and #bad == 0, ("%d applications, failed: %s"):format(#reports, table.concat(bad, ",")))

        -- items: numbers live in the template
        local sword = assert(risen.template.get("It_1H_Sword"))
        local req = risen.item.requirements("It_1H_Sword") or {}
        T.eq("item numbers", { value = sword:get_property("gCItem_PS", "GoldValue"), damage = sword:get_property("gCDamage_PS", "DamageAmount"), str = req.Strength },
            { value = "120", damage = "25", str = 5 })
        T.eq("armor and potion modifiers", { mods_text(risen.item.modifiers("It_Armor_Guard")), mods_text(risen.item.modifiers("It_Po_Health_01")) },
            { "ProtBlunt+25,ProtEdge+35,ProtPoint+20", "HitPoints+40" })
        -- the engine's own equip check (only the player is held to requirements): the sword is allowed, the two-hander is not
        local can_sword, can_two = risen.item.can_use(player, "It_1H_Sword"), risen.item.can_use(player, "It_2H_TwoHandedSword")
        T.eq("requirements are enforced for the player", { can_sword, can_two }, { true, false })
        local spawned = risen.entity.spawn("It_1H_Sword", x + 100, y + 10, z)
        T.eq("new item spawn", spawned and spawned:item_value(), 120)

        -- NPCs: a spawn carries the template's numbers and loot
        local wolf = assert(risen.entity.spawn("Wolf", x + 2000, y + 20, z))
        T.eq("wolf numbers", { hp = wolf:hp_max(), level = wolf:level(), str = wolf:skill_total("Strength") }, { hp = 500, level = 20, str = 90 })
        T.eq("wolf loot", risen.item.count(wolf, "It_Apple"), 3)
        -- shipped Boar (docs/NPC_PROPERTIES.md): 180 hit points, level 6 -> hp_mult = 2 and level_add = 2 give 360 and 8
        local boar = assert(risen.entity.spawn("Boar", x + 2100, y + 20, z))
        T.eq("boar numbers (relative)", { boar:hp_max(), boar:level() }, { 360, 8 })
        T.eq("potion value (group)", tonumber(risen.template.get("It_Po_Health_01"):get_property("gCItem_PS", "GoldValue")), 15)

        -- the wolves that lived in the world before the edit got the numbers on session.start (instances = true)
        local live, good = risen.template.get("Wolf"):instances(1000) or {}, 0
        for _, w in ipairs(live) do
            if w:hp_max() == 500 then good = good + 1 end
        end
        T.expect("live wolves of the world", #live > 2 and good == #live, ("%d of %d live wolves have 500 hit points"):format(good, #live))
    end, "example_balance")
end
