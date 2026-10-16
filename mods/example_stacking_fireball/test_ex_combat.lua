---@diagnostic disable: need-check-nil, param-type-mismatch, cast-local-type, assign-type-mismatch, undefined-field
-- Live scenario `ex_combat` (only loaded in a test run, see main.lua): the player really casts Fireball at a tall dummy (a Gyrger) over and over; the hit points it loses must follow the
-- example's rule (stock damage times 1 + 0.5 per earlier hit), a second dummy has its own counter, a death resets a counter, the counters are in risen.save, and after a quick save and a load in a
-- FRESH PROCESS the counters continue.
--
--   .\scripts\smoke-test.ps1 -Scenario ex_combat          (phase save in a new game, then phase load in a fresh process)
local T = risen.test
local phase = T.param("phase", "save")

return function(ex)
    local function L(...)
        local t = {}
        for i = 1, select("#", ...) do t[#t + 1] = tostring((select(i, ...))) end
        risen.log.info("ex_combat[" .. phase .. "] " .. table.concat(t, " "))
    end

    T.plan(phase == "load" and {
        "loaded: the counters of the savegame are back", "loaded: the next hit continues the count (third hit: +100%)", "loaded: the other target continues too (third hit)", "loaded: counters after the casts",
    } or {
        "the first hit is the stock hit", "every further hit on the same target does 50% more", "the damage follows the rule up to the fourth hit", "the counter is in risen.save",
        "a second target has its own counter", "the weak edge-of-blast hit is scaled too (the falloff survives)", "a death resets the counter", "the first hit after the reset is a stock hit again",
        "other spells and other casters are not touched", "counters before the save",
    })

    local proj_alive, proj_entity = false, nil
    local hits = {}   -- every hit seen after the example's handler: { victim, original, damage, mult }
    risen.on("entity.spawned", function(e, template)
        if e and template:find("^It_Projectile_Fireball") then proj_entity, proj_alive = e, true end
    end)
    -- registered after the example's handler: it sees the damage the example returned
    assert(risen.combat.on_damage(function(hit)
        hits[#hits + 1] = { victim = hit.victim:name(), original = hit.original_damage, damage = hit.damage, mult = hit.multiplier, prot = hit.protection }
    end))

    local function player_now() return assert(risen.entity.player(), "no player") end
    local function draw()
        local p = player_now()
        if p:state() ~= "PS_MagicBall_Loop" then
            if p:state() ~= "PS_Normal" and p:state() ~= "PS_Magic" then p:set_state("PS_Normal"); T.sleep(40) end
            p:set_state("PS_Magic")
            T.wait_until(function() return p:state() == "PS_MagicBall_Loop" end, 30, "PS_MagicBall_Loop")
        end
    end
    local function aim()
        draw()
        local c = assert(risen.world.camera(), "no camera")
        local h = math.sqrt(c.dx * c.dx + c.dz * c.dz)
        local px, py, pz = player_now():position()
        return { x = px, y = py, z = pz, hx = c.dx / h, hz = c.dz / h }
    end
    -- a Gyrger on the line of the shot (the shot leaves the hand 14 degrees above the horizon, a Gyrger is tall and wide), no protection against magic
    local function dummy(a, name, off)
        local dist = 78 + 260
        local x, z = a.x + a.hx * dist - a.hz * off, a.z + a.hz * dist + a.hx * off
        local w = assert(risen.npc.spawn("Gyrger", x, a.y, z, { name = name, hp_max = 5000 }))
        w:set_frozen(true)
        risen.npc.set_stats(w, { skills = { ProtMagic = 0 } })
        return w
    end
    local function shoot()
        local p = player_now()
        draw()
        p:set_hp(p:hp_max())
        proj_alive, proj_entity = false, nil
        p:set_state("PS_MagicBall_Shoot")
        T.wait_until(function() return proj_alive end, 20, "the projectile")
        local lx, ly, lz
        for _ = 1, 150 do
            T.sleep(2)
            if not (proj_entity and proj_entity:valid()) then break end
            lx, ly, lz = proj_entity:position()
        end
        if lx then L(("projectile last seen %.0f %.0f %.0f"):format(lx, ly, lz)) end
        T.sleep(20)
    end
    -- one cast at the named dummies (a hit wakes a frozen one up, so they are made anew each time under the same name: the counters belong to the name); returns hit points lost by name
    local function cast(spec)
        local a = aim()
        local ws = {}
        for _, s in ipairs(spec) do ws[#ws + 1] = dummy(a, s[1], s[2]) end
        T.sleep(30)
        for _, w in ipairs(ws) do
            w:set_hp(5000)
            local x, y, z = w:position()
            L(("dummy %s at %.0f %.0f %.0f"):format(w:name(), x, y, z))
        end
        do
            local x, y, z = player_now():position()
            L(("player %.0f %.0f %.0f, aim %.3f %.3f"):format(x, y, z, a.hx, a.hz))
        end
        hits = {}
        shoot()
        local lost = {}
        for _, w in ipairs(ws) do lost[w:name()] = 5000 - w:hp() end
        for _, w in ipairs(ws) do w:kill() end
        T.sleep(15)
        return lost
    end
    local function first_hit(name)
        for _, h in ipairs(hits) do if h.victim == name then return h end end
    end
    local function counters() return risen.save.get("stacks", {}) end
    local function round(x) return math.floor(x + 0.5) end

    T.run(function()
        T.wait_event("session.start", 240, function(m) return phase ~= "load" and m == "new" or phase == "load" and m == "load" end)
        T.sleep(200)
        local p = player_now()
        assert(risen.npc.set_stats(p, { skills = { Fireball = 3, Circle = 3 }, mana_max = 4000, mana = 4000 }))
        if risen.item.count(p, "It_Crystal_Fire") < 1 then risen.item.give(p, "It_Crystal_Fire", 1) end
        if risen.spell.equipped(p) ~= "It_Crystal_Fire" then risen.item.equip(p, "It_Crystal_Fire") end

        if phase == "load" then
            T.section("fresh process")
            local c = counters()
            T.eq("loaded: the counters of the savegame are back", { c.EXC_A, c.EXC_B }, { 2, 2 })
            local lost = cast({ { "EXC_A", 0 }, { "EXC_B", 100 } })
            local ha, hb = first_hit("EXC_A"), first_hit("EXC_B")
            L(("A lost %s (handler saw %s -> %s), B lost %s (%s -> %s)"):format(lost.EXC_A, ha and ha.original, ha and ha.damage, lost.EXC_B, hb and hb.original, hb and hb.damage))
            T.eq("loaded: the next hit continues the count (third hit: +100%)", { ha and ha.original, ha and ha.damage, lost.EXC_A }, { 175, 350, 350 })
            T.expect("loaded: the other target continues too (third hit)", hb ~= nil and hb.damage == round(hb.original * 2) and lost.EXC_B == hb.damage, hb and ("%s -> %s, lost %s"):format(hb.original, hb.damage, lost.EXC_B) or "no hit")
            c = counters()
            T.eq("loaded: counters after the casts", { c.EXC_A, c.EXC_B }, { 3, 3 })
            return
        end

        -- ---- phase save ----------------------------------------------------------------------------------------------------------------------------------------------------------------
        T.section("stacking")
        risen.save.set("stacks", {})
        local lost_a = {}
        local seen = {}
        for shot = 1, 4 do
            local spec = { { "EXC_A", 0 } }
            if shot <= 2 then spec[2] = { "EXC_B", 190 } end
            local lost = cast(spec)
            lost_a[shot] = lost.EXC_A
            seen[shot] = first_hit("EXC_A")
            if shot <= 2 then seen[10 + shot] = first_hit("EXC_B"); lost_a[10 + shot] = lost.EXC_B end
            L(("shot %d: A lost %s, handler saw %s -> %s"):format(shot, lost.EXC_A, seen[shot] and seen[shot].original, seen[shot] and seen[shot].damage))
        end
        T.eq("the first hit is the stock hit", { seen[1].original, seen[1].damage, lost_a[1] }, { 175, 175, 175 })
        T.eq("every further hit on the same target does 50% more", { seen[2].damage, lost_a[2], seen[3].damage, lost_a[3] }, { round(175 * 1.5), round(175 * 1.5), 350, 350 })
        T.eq("the damage follows the rule up to the fourth hit", { seen[4].damage, lost_a[4] }, { round(175 * 2.5), round(175 * 2.5) })
        local c = counters()
        T.eq("the counter is in risen.save", { c.EXC_A, c.EXC_B }, { 4, 2 })
        local b1, b2 = seen[11], seen[12]
        T.expect("a second target has its own counter", b1 ~= nil and b2 ~= nil and b1.damage == b1.original and lost_a[11] == b1.damage, b1 and ("first hit %s -> %s, lost %s"):format(b1.original, b1.damage, lost_a[11]) or "no hit")
        T.expect("the weak edge-of-blast hit is scaled too (the falloff survives)", b1 and b2 and b1.mult < 0.9 and b2.mult < 0.9 and b2.damage == round(b2.original * 1.5) and lost_a[12] == b2.damage,
            b2 and ("mult %.2f, %s -> %s, lost %s"):format(b2.mult, b2.original, b2.damage, lost_a[12]) or "no hit")

        -- a death resets the counter
        T.section("reset")
        local a = aim()
        local victim = dummy(a, "EXC_A", 0)
        T.sleep(20)
        local ok = risen.script.call("Kill", p, victim)
        T.sleep(30)
        c = counters()
        T.expect("a death resets the counter", c.EXC_A == nil and c.EXC_B == 2, ("A=%s B=%s kill -> %s"):format(tostring(c.EXC_A), tostring(c.EXC_B), tostring(ok)))
        if victim:valid() then victim:kill() end
        T.sleep(20)
        local lost = cast({ { "EXC_A", 0 } })
        local h = first_hit("EXC_A")
        T.eq("the first hit after the reset is a stock hit again", { h and h.damage, lost.EXC_A }, { 175, 175 })
        cast({ { "EXC_A", 0 } })     -- A is at 2 now, B at 2: the state that is saved

        -- other casters are not touched: a wolf and a peasant fighting go through the engine's DoDamage and never reach the example's handler (its filter is the player's Fireball)
        T.section("others")
        local before = counters()
        local snapshot = { before.EXC_A, before.EXC_B }
        local px, py, pz = p:position()
        local peasant = assert(risen.npc.spawn("Peasant_1_01", px + 700, py, pz, { name = "EXC_Peasant", hp_max = 4000 }))
        T.sleep(60)
        local wolf = assert(risen.npc.spawn("Wolf", px + 770, py, pz, { name = "EXC_Wolf", hp_max = 4000 }))
        hits = {}
        T.wait_until(function() return #hits >= 3 end, 60, "three bites and blows")
        wolf:kill(); peasant:kill()
        T.sleep(20)
        local after = counters()
        local keys = 0
        for _ in pairs(after) do keys = keys + 1 end
        T.expect("other spells and other casters are not touched", after.EXC_A == snapshot[1] and after.EXC_B == snapshot[2] and keys == 2 and after.EXC_Wolf == nil and after.EXC_Peasant == nil,
            ("A=%s B=%s keys=%d"):format(tostring(after.EXC_A), tostring(after.EXC_B), keys))
        T.eq("counters before the save", { after.EXC_A, after.EXC_B }, { 2, 2 })

        T.section("save")
        local saved = false
        risen.on("save.after", function(_, ok2) saved = ok2 end)
        p:set_state("PS_Normal")
        T.sleep(60)
        L("quick_save", risen.world.quick_save())
        T.wait_until(function() return saved end, 120, "save.after")
        T.sleep(200)
    end, "ex_combat")
end
