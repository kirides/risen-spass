-- Live check of example_spell (only loaded in a test run). The player really casts: the engine's state scripts shoot the projectile and take the mana.
local T = risen.test

-- LuaLS: these calls return nil on failure; a verification wants to stop there
local function spell_get(name) return assert(risen.spell.get(name)) end
local function tpl(name) return assert(risen.template.get(name)) end

---@class ExampleShot
---@field damage number|nil
---@field lost number
---@field prot number
---@field mana number
local ExampleShot = {}   -- (only carries the class annotation above)

return function(spec, reports)
    T.plan { "install reported success", "numbers of the edited spells", "numbers of the new spell", "requirements and the engine's equip check",
             "cast: the edited Fireball", "cast: the new crystal", "cast: the new scroll, the last one is used up", "rune spell: the edited cost" }

    local STRONG, SCROLL = "Spass_Fireball_Strong", "Spass_Fireball_Strong_Scroll"
    local measure, victim = false, nil
    ---@type ExampleShot|nil
    local shot = nil

    -- every projectile: its own numbers, and (when measuring) what the engine's hit script takes from a victim standing where the projectile is
    risen.on("entity.spawned", function(e, template)
        if not e or not tostring(template):find("^It_Projectile_Fireball") then return end
        ---@type ExampleShot
        local s = { mana = 0, lost = 0, prot = 0, damage = tonumber(e:get_property("gCDamage_PS", "DamageAmount")) }
        if measure and victim then
            victim:teleport(e)
            local hp0 = victim:hp()
            risen.script.call("DoFireballAreaDamage", e, nil, 100)   -- what OnHitFireball_S* calls when the projectile hits
            s.lost, s.prot = hp0 - victim:hp(), victim:skill_total("ProtMagic")
            victim:kill()
            victim = nil
        end
        shot = s
    end, -10)

    ---@return ExampleShot
    local function cast(item)
        local p = assert(risen.entity.player())
        local cur = risen.spell.equipped(p)
        if cur and cur ~= item then risen.item.unequip(p, cur) end
        if p:state() ~= "PS_Normal" then p:set_state("PS_Normal"); T.sleep(60) end
        assert(risen.item.equip(p, item))
        p:set_state("PS_Magic")                                  -- draw: the engine plays its way to PS_MagicBall_Loop (about 4 seconds)
        T.wait_until(function() return p:state() == "PS_MagicBall_Loop" end, 90, "ready to shoot with " .. item)
        local x, y, z = p:position()
        x, y, z = x or 0, y or 0, z or 0
        victim = assert(risen.npc.spawn("Wolf", x + 2500, y, z, { name = "EX_SPELL_VICTIM", hp_max = 5000 }))
        victim:set_frozen(true)
        T.sleep(20)
        measure, shot = true, nil
        local mana0 = p:mana()
        p:set_state("PS_MagicBall_Shoot")                        -- the engine's shoot state: takes the mana (PSCastInfo::Consume) and spawns the projectile
        T.wait_until(function() return shot ~= nil end, 30, "the projectile of " .. item)
        T.wait_until(function() return p:state() ~= "PS_MagicBall_Shoot" end, 30, "the shot is over")
        measure = false
        local s = shot --[[@as ExampleShot]]
        s.mana = mana0 - p:mana()
        return s
    end

    T.run(function()
        T.wait_event("session.start", 180, function(m) return m == "new" end)
        T.sleep(150)
        local p = assert(risen.entity.player())
        assert(risen.npc.set_stats(p, { skills = { Fireball = 3, Circle = 3 }, mana_max = 300, mana = 300 }))

        local bad = {}
        for _, r in ipairs(reports) do
            if not (r.report.ok and #r.report.failed == 0) then bad[#bad + 1] = r.when end
        end
        T.expect("install reported success", #reports >= 2 and #bad == 0, ("%d applications, failed: %s"):format(#reports, table.concat(bad, ",")))

        local fb, light = spell_get("Fireball"), spell_get("Light")
        T.eq("numbers of the edited spells", { fb.items.crystal.mana_cost, fb.projectiles[1].damage, light.items.rune.mana_cost, light.items.scroll.mana_cost }, { 10, 140, 5, 2 })
        local strong, scroll = tpl(STRONG), tpl(SCROLL)
        T.eq("numbers of the new spell", { tonumber(strong:get_property("gCCastInfo_PS", "ManaCost")), tonumber(scroll:get_property("gCCastInfo_PS", "ManaCost")),
            scroll:get_property("gCCastInfo_PS", "ConsumeItem"), strong:focus_name() }, { 20, 10, "True", "Strong Fireball" })
        assert(risen.npc.set_stats(p, { skills = { Fireball = 1 } }))
        local low = risen.item.can_use(p, STRONG)
        assert(risen.npc.set_stats(p, { skills = { Fireball = 3 } }))
        T.eq("requirements and the engine's equip check", { risen.item.requirements(STRONG), low, risen.item.can_use(p, STRONG) }, { { Fireball = 2 }, false, true })

        risen.item.give(p, "It_Crystal_Fire", 1); risen.item.give(p, STRONG, 1); risen.item.give(p, SCROLL, 1); risen.item.give(p, "It_Ru_Light", 1)

        -- the edited shipped crystal: 10 mana, 140 damage; a hit takes damage + 25 * Fireball skill - the victim's magic protection
        local s = cast("It_Crystal_Fire")
        T.eq("cast: the edited Fireball", { s.mana, s.damage, s.lost }, { 10, 140, 140 + 25 * 3 - s.prot })
        -- the new crystal: 20 mana, the projectile of this shot carries double damage
        s = cast(STRONG)
        T.eq("cast: the new crystal", { s.mana, s.damage, s.lost }, { 20, 280, 280 + 25 * 3 - s.prot })
        -- the scroll: half the mana, one cast, then it is gone
        s = cast(SCROLL)
        T.wait_until(function() return risen.item.count(p, SCROLL) == 0 end, 30, "the scroll is used up")
        T.eq("cast: the new scroll, the last one is used up", { s.mana, s.damage, risen.item.count(p, SCROLL) }, { 10, 280, 0 })
        p:set_state("PS_Normal")
        T.sleep(60)

        -- a rune spell: the engine's cast accounting with the edited cost (the rune stays, the mana goes)
        local m0 = p:mana()
        assert(risen.spell.consume(p, "It_Ru_Light"))
        T.eq("rune spell: the edited cost", { m0 - p:mana(), risen.item.count(p, "It_Ru_Light") }, { 5, 1 })
    end, "example_spell")
end
