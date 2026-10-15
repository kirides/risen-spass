-- Live check of example_template_patch (only loaded in a test run). Returns a function that registers the scenario.
local T = risen.test

return function(last_application)
    T.plan { "new game", "first tick wrote the values", "template reads back", "new spawn follows the template", "inventory stack follows the template",
             "existing world entities were patched", "NPC template property reaches spawns" }

    T.run(function()
        T.wait_event("session.start", 180, function(m) return m == "new" end)
        T.sleep(150)
        T.expect("new game", true)
        local player = assert(risen.entity.player())
        local x, y, z = player:position()
        x, y, z = x or 0, y or 0, z or 0

        local report = last_application()
        T.expect("first tick wrote the values", report.It_Apple ~= nil and report.It_Apple.applied == 1 and report.It_1H_Sword.applied == 2,
            ("apple %s, sword %s"):format(tostring(report.It_Apple and report.It_Apple.applied), tostring(report.It_1H_Sword and report.It_1H_Sword.applied)))

        -- the template itself
        local sword = assert(risen.template.get("It_1H_Sword"))
        T.eq("template reads back", { sword:get_property("gCItem_PS", "GoldValue"), sword:get_property("gCDamage_PS", "DamageAmount") }, { "321", "33" })

        -- a new spawn, and an item in an inventory (the stack references the template)
        local spawned = risen.entity.spawn("It_Apple", x + 100, y + 10, z)
        T.eq("new spawn follows the template", spawned and spawned:item_value(), 77)
        risen.item.give(player, "It_Apple", 1)
        local stack_value
        for i = 0, player:stack_count() - 1 do
            local item = player:stack_item(i)
            if item and item:name() == "It_Apple" then stack_value = item:item_value() end
        end
        T.eq("inventory stack follows the template", stack_value, 77)

        -- the apples that grow in the world of the new game were there before the edit: session.start wrote them (instances = true)
        local world, patched = risen.template.get("It_Apple"):instances(100000) or {}, 0
        for _, e in ipairs(world) do
            if e:item_value() == 77 then patched = patched + 1 end
        end
        T.expect("existing world entities were patched", #world > 1 and patched == #world, ("%d of %d live apples have value 77"):format(patched, #world))

        -- an NPC template property
        local peasant = risen.entity.spawn("Peasant_1_01", x - 150, y + 10, z + 150)
        T.eq("NPC template property reaches spawns", peasant and peasant:guild(), 4)  -- 4 = Cit
        if peasant then peasant:kill() end
    end, "example_template_patch")
end
