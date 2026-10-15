-- Development aid: logs the numbers of every NPC and monster template:
--   "NPC <template> <set> <prop>:<type>=<value>"   every property of every property set (gCNPC_PS, gCCombatSystem_PS, gCDamage_PS, ...)
--   "STATS <template> <name>=<base value> ..."     the non-reflected gCSkills_PS (risen.npc.stats(name, { base = true })), xp_reward, combat, loot
--   "LIMIT <id> <skill> <min> <max>"               the range the engine allows per skill (risen.npc.skill_raw)
--   scripts/test-run.ps1 -Mods npcdump -Seconds 150 -NoAutostart
--   python tools/gen_npc_properties_doc.py <log> docs/TEMPLATES.md > docs/NPC_PROPERTIES.md
-- NPC templates are loaded lazily by the engine; the template API forces the load.
local done = false
risen.on("tick", function()
    if done then return end
    done = true
    local skip = {}
    for _, group in ipairs({ "NPC/Test", "NPC/_HumanParts" }) do
        for _, n in ipairs(risen.template.list(group) or {}) do skip[n] = true end
    end
    local names = {}
    for _, group in ipairs({ "NPC", "Monster" }) do
        for _, n in ipairs(risen.template.list(group) or {}) do
            if not skip[n] then names[#names + 1] = n end
        end
    end
    local IDNAMES = { [0] = "HitPoints", "ManaPoints", "Level", "Experience", "LearnPoints", "MaxHitPoints", "MaxManaPoints", "Strength", "Dexterity", "Intelligence",
        "ProtEdge", "ProtBlunt", "ProtPoint", "ProtFire", "ProtIce", "ProtMagic", "Sword", "Axe", "Staff", "Bow", "CrossBow", "Circle", "Fireball", "Frost",
        "Missile", "Smith", "Mining", "Lockpick", "Pickpocket", "Sneak", "Acrobat", "Alchemy", "Scribe", "Trophy" }
    for id = 0, 33 do
        local _, _, _, mn, mx = risen.npc.skill_raw("Wolf", id)
        risen.log.info(("LIMIT %d %s %s %s"):format(id, IDNAMES[id], tostring(mn), tostring(mx)))
    end
    local seen, count = {}, 0
    for _, name in ipairs(names) do
        if not seen[name] then
            seen[name] = true
            local t = risen.template.get(name)
            if not t then
                risen.log.info("NPC " .. name .. " <missing>")
            else
                count = count + 1
                for _, set in ipairs(t:property_sets() or {}) do
                    for _, p in ipairs(t:list_properties(set) or {}) do
                        risen.log.info(("NPC %s %s %s:%s=%s"):format(name, set, p.name, p.type, tostring(t:get_property(set, p.name))))
                    end
                end
                local s = risen.npc.stats(name, { base = true })
                if s then
                    local parts = { "hp_max=" .. s.hp_max, "mana_max=" .. s.mana_max, "level=" .. s.level, "xp_reward=" .. s.xp_reward }
                    local keys = {}
                    for k in pairs(s.skills) do keys[#keys + 1] = k end
                    table.sort(keys)
                    for _, k in ipairs(keys) do parts[#parts + 1] = k .. "=" .. s.skills[k] end
                    risen.log.info(("STATS %s %s"):format(name, table.concat(parts, " ")))
                    risen.log.info(("COMBAT %s %s %s %s"):format(name, s.combat.species or "-", s.combat.right_fist or "-", s.combat.left_fist or "-"))
                    for _, e in ipairs(s.loot or {}) do risen.log.info(("LOOT %s %s %d"):format(name, e.item, e.amount)) end
                end
            end
        end
    end
    risen.log.info("NPCDUMP done " .. count)
end)
