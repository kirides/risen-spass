-- Development aid: logs everything that makes up a spell, for docs/SPELL_PROPERTIES.md (tools/gen_spell_properties_doc.py):
--   "SPELL <template> <set> <prop>:<type>=<value>"   every property of the spell-related item templates: runes/scrolls/crystals/orbs (gCCastInfo_PS),
--                                                    cast items, magic ammo, projectiles and effects of Items/Magic
--   "SARR <template> required|modifiers <skill> <amount> [<mode>]"   their RequiredSkills / ModifySkills arrays
--   "CASTER <npc template> <item> <amount>"          NPC and monster templates that carry such items in their inventory
--   scripts/test-run.ps1 -Mods spelldump -Seconds 150 -NoAutostart -Filter 'SPELLDUMP'
--   python tools/gen_spell_properties_doc.py <test copy>/bin/spass/spass.log docs/TEMPLATES.md > docs/SPELL_PROPERTIES.md
-- The first tick fires at the main menu; item templates are loaded at startup, NPC templates are loaded on demand by the template API.
local done = false

local function spell_related(name, t)
    for _, set in ipairs(t:property_sets() or {}) do
        if set == "gCCastInfo_PS" or set == "gCProjectile2_PS" then return true end
    end
    return false
end

risen.on("tick", function()
    if done then return end
    done = true
    local magic = {}
    for _, n in ipairs(risen.template.list("Items/Magic") or {}) do magic[n] = true end
    local names, seen = {}, {}
    for _, n in ipairs(risen.template.list("Items") or {}) do
        local t = risen.template.get(n)
        if t and (magic[n] or spell_related(n, t)) and not seen[n] then
            seen[n] = true
            names[#names + 1] = n
        end
    end
    local wanted = {}
    for _, name in ipairs(names) do
        wanted[name] = true
        local t = risen.template.get(name)
        for _, set in ipairs(t and t:property_sets() or {}) do
            for _, p in ipairs(t and t:list_properties(set) or {}) do
                risen.log.info(("SPELL %s %s %s:%s=%s"):format(name, set, p.name, p.type, tostring(t and t:get_property(set, p.name))))
            end
        end
        for _, kind in ipairs({ "required", "modifiers" }) do
            local arr = risen.item.skill_array(name, kind)
            for _, e in ipairs(arr or {}) do
                risen.log.info(("SARR %s %s %s %d %s"):format(name, kind, e.skill, e.amount, e.modifier or ""))
            end
        end
    end
    local skip = {}
    for _, group in ipairs({ "NPC/Test", "NPC/_HumanParts" }) do
        for _, n in ipairs(risen.template.list(group) or {}) do skip[n] = true end
    end
    local carriers = 0
    for _, group in ipairs({ "NPC", "Monster" }) do
        for _, n in ipairs(risen.template.list(group) or {}) do
            if not skip[n] and not seen["npc:" .. n] then
                seen["npc:" .. n] = true
                local s = risen.npc.stats(n, { base = true })
                if s then
                    local has = false
                    for _, e in ipairs(s.loot or {}) do
                        if wanted[e.item] and (e.item:find("^It_Ru_") or e.item:find("^It_Scr_") or e.item:find("^It_Crystal_") or e.item:find("^It_Orb_")) then
                            risen.log.info(("CASTER %s %s %d"):format(n, e.item, e.amount))
                            has = true
                        end
                    end
                    if has then
                        carriers = carriers + 1
                        risen.log.info(("CASTERSTATS %s mana_max=%d Fireball=%d Frost=%d Missile=%d Circle=%d Intelligence=%d level=%d"):format(n, s.mana_max,
                            s.skills.Fireball or 0, s.skills.Frost or 0, s.skills.Missile or 0, s.skills.Circle or 0, s.skills.Intelligence or 0, s.level))
                    end
                end
            end
        end
    end
    risen.log.info(("SPELLDUMP done %d items, %d npc templates with spell items"):format(#names, carriers))
end)
