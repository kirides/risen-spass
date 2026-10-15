-- Development aid: logs everything that makes up a crafting recipe, for docs/RECIPE_PROPERTIES.md (tools/gen_recipe_properties_doc.py):
--   "RECIPE <template> <set> <prop>:<type>=<value>"   every property of the recipe item templates (templates with a gCRecipe_PS): the recipe set, gCItem_PS, gCInteraction_PS
--   "RSETS <template> <set> <set> ..."                 the property sets of the template
--   "RING <template> <ingredient item> <count>"       CraftIngredients (risen.recipe.ingredients_raw)
--   "RREQ <template> <skill> <amount>"                RequiredSkills (risen.item.skill_array(name, "recipe_required"))
--   "RNAME <template> <focus name>"                   the label of the recipe item and "RITEM <item> <focus name>" of every item it names
--   scripts/test-run.ps1 -Mods recipedump -Seconds 150 -NoAutostart -Filter 'RECIPE|RSETS|RING|RREQ|RNAME|RITEM|RECIPEDUMP'
--   python tools/gen_recipe_properties_doc.py <test copy>/bin/spass/spass.log > docs/RECIPE_PROPERTIES.md
-- The first tick fires at the main menu; item templates are loaded at startup.
local done = false

risen.on("tick", function()
    if done then return end
    done = true
    local names, items = {}, {}
    for _, n in ipairs(risen.template.list("Items") or {}) do
        local t = risen.template.get(n)
        if t then
            local sets = t:property_sets() or {}
            local has = false
            for _, set in ipairs(sets) do if set == "gCRecipe_PS" then has = true end end
            if has then
                names[#names + 1] = n
                risen.log.info(("RSETS %s %s"):format(n, table.concat(sets, " ")))
                for _, set in ipairs({ "gCRecipe_PS", "gCItem_PS", "gCInteraction_PS", "gCCastInfo_PS" }) do
                    for _, p in ipairs(t:list_properties(set) or {}) do
                        risen.log.info(("RECIPE %s %s %s:%s=%s"):format(n, set, p.name, p.type, tostring(t:get_property(set, p.name))))
                    end
                end
                local ing, why = risen.recipe.ingredients_raw(n)
                if not ing then risen.log.info(("RING %s <%s>"):format(n, tostring(why))) end
                for _, e in ipairs(ing or {}) do
                    risen.log.info(("RING %s %s %d"):format(n, e.item, e.count))
                    items[e.item] = true
                end
                local req = risen.item.skill_array(n, "recipe_required")
                for _, e in ipairs(req or {}) do risen.log.info(("RREQ %s %s %d"):format(n, e.skill, e.amount)) end
                risen.log.info(("RNAME %s %s"):format(n, tostring(t:focus_name())))
                local result = t:get_property("gCRecipe_PS", "ResultItem")
                if result then items[result] = true end
            end
        end
    end
    for item in pairs(items) do
        local t = risen.template.get(item)
        risen.log.info(("RITEM %s %s"):format(item, t and tostring(t:focus_name()) or "<missing>"))
    end
    risen.log.info(("RECIPEDUMP done %d recipes"):format(#names))
end)
