-- Development aid: logs "ITEM <template> <set> <prop>:<type>=<value>" for every property of every property set of item templates.
--   scripts/test-run.ps1 -Mods itemdump -Seconds 100 -NoAutostart
--   python tools/gen_item_properties_doc.py <log> > docs/ITEM_PROPERTIES.md
-- The first tick fires at the main menu; item templates are loaded at startup.
local REPS = {
    "It_1H_Sword", "It_1H_Club", "It_Axe_Waraxe", "It_2H_TwoHandedSword", "It_Bow_Long", "It_Arrow", "It_Staff_Combat", "It_Armor_Player", "It_Armor_Guard", "It_Helmet_Guard",
    "It_Shield_Guard", "It_Ri_STR_Small", "It_Ri_Prot_Missile", "It_Am_Prot", "It_Am_STR", "It_Po_Health_01", "It_Po_Mana_01", "It_Po_STR_Max", "It_Apple", "It_Stew", "It_Beer",
    "It_Ru_Inferno", "It_Scr_Inferno", "It_Gold", "It_Cast_Fireball",
}
local ALL = true  -- false: only a few representative templates
local done = false
risen.on("tick", function()
    if done then return end
    done = true
    local names = {}
    for _, n in ipairs(risen.template.list("Items") or {}) do names[#names + 1] = n end
    local list = ALL and names or REPS
    for _, name in ipairs(list) do
        local t = risen.template.get(name)
        if not t then
            risen.log.info("ITEM " .. name .. " <missing>")
        else
            for _, set in ipairs(t:property_sets() or {}) do
                for _, p in ipairs(t:list_properties(set) or {}) do
                    risen.log.info(("ITEM %s %s %s:%s=%s"):format(name, set, p.name, p.type, tostring(t:get_property(set, p.name))))
                end
            end
        end
    end
    -- the array properties (RequiredSkills / ModifySkills) of every item template: "ARR <template> <kind> <skill> <amount> [<modifier>]"
    for _, name in ipairs(names) do
        for _, kind in ipairs({ "required", "modifiers" }) do
            local arr, why = risen.item.skill_array(name, kind)
            if not arr then
                risen.log.info(("ARR %s %s <%s>"):format(name, kind, tostring(why)))
            else
                for _, e in ipairs(arr) do
                    risen.log.info(("ARR %s %s %s %d %s"):format(name, kind, e.skill, e.amount, e.modifier or ""))
                end
            end
        end
    end
    -- numeric gESkill values (parsed by the engine from the enum names): "SKILLID <name> <id>"
    for _, n in ipairs({ "Stat_STR", "Stat_DEX", "Stat_INT", "Stat_HP", "Stat_MP", "Stat_LP", "Stat_XP", "Stat_LV", "Atrib_HP", "Atrib_MP", "Combat_Axe",
                         "Combat_Sword", "Combat_Staff", "Combat_Bow", "Combat_CrossBow", "Prot_Edge", "Prot_Blunt", "Prot_Point", "Prot_Fire", "Prot_Ice",
                         "Prot_Magic", "Magic_Missile", "Magic_Frost", "Magic_Fireball", "Magic_Circle", "Misc_Scribe", "Misc_Trophy", "Misc_Acrobat",
                         "Misc_Pickpocket", "Misc_Lockpick", "Misc_Sneak", "Misc_Mining", "Misc_Smith", "Misc_Alchemy", "None" }) do
        risen.log.info(("SKILLID %s %s"):format(n, tostring(risen.item.skill_id("gESkill_" .. n))))
    end
    risen.log.info("ITEMDUMP done " .. #list)
end)
