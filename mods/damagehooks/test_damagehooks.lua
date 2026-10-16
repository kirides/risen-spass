---@diagnostic disable: need-check-nil, param-type-mismatch, cast-local-type, assign-type-mismatch, undefined-field
-- Live scenario `damagehooks` (only loaded in a test run, see main.lua): risen.combat.on_damage against REAL hits.
--
--   * the player really casts Fireball (PS_Magic -> PS_MagicBall_Loop -> PS_MagicBall_Shoot): the projectile flies, hits three tall dummies (Gyrgers) and the engine's own hit script
--     (OnHitFireball_S0 -> DoFireballAreaDamage -> DoMagicFireballDamage -> DoDamage) runs; the hit points each dummy loses are compared with what the handlers returned;
--   * a wolf and a peasant with a club fight each other: melee hits by fist (DoFistDamage) and by weapon (DoSwordDamage);
--   * cancel, area falloff, no double application, filters, chains, errors in handlers, unregistering, a handler that applies damage itself;
--   * phase `load`: a fresh process loads the savegame, the mod registers its handler again and a real cast still goes through it.
--
--   .\scripts\test-run.ps1 -Instance 1 -Mods damagehooks -WaitForResult -Seconds 300 -Scenario damagehooks -ShowChecks [-Param phase=load -Start load -Keep]
local T = risen.test
local log = risen.log
local phase = T.param("phase", "save")

local function L(...)
    local t = {}
    for i = 1, select("#", ...) do t[#t + 1] = tostring((select(i, ...))) end
    log.info("damagehooks[" .. phase .. "] " .. table.concat(t, " "))
end

local PLAN_SAVE = {
    "the API is there", "bad arguments are refused with a reason",
    "calibration: the shot flies along a line", "a plain cast hits three dummies and each loses its hit points",
    "an observing handler sees the hit and changes nothing", "the handler's context: attacker, victim, projectile, spell, type, damage",
    "area falloff: the multiplier of the outer dummies is below 1 and the damage follows it",
    "a handler that scales the damage: every dummy loses the new damage", "area falloff survives the handler (damage stays proportional to the multiplier)",
    "no double application: one call and one entity.damaged per dummy", "stock protection still applies after the handler", "the minimum damage floor still applies after the handler",
    "cancel: false protects the victim, the others are hit", "cancel: 0 does the same", "chained handlers: the second sees the first one's damage", "a cancel ends the chain",
    "filters: a handler for another spell, kind or attacker is not called", "filters: the weapon filter matches the projectile template", "filters: victim and attacker",
    "an error in a handler is a warning and the stock damage applies", "a bad return value is a warning and the stock damage applies", "a handler after a failing one still works",
    "failures() counts the failed calls", "unregistering: the handler is gone and the stock damage applies again",
    "wolf bites: a fist hit, an edge hit, by the wolf", "club blows: a weapon hit, blunt, by the peasant", "melee: the new damage reaches the victim",
    "a handler may apply damage with DoDamage itself (the nested call is not offered again)", "every handler of the scenario was removed again", "saved",
}
local PLAN_LOAD = {
    "loaded: the handler registered by the mod is active", "loaded: a real cast goes through the handler", "loaded: the hit points follow the handler",
}
T.plan(phase == "load" and PLAN_LOAD or PLAN_SAVE)

-- ---- the cast rig --------------------------------------------------------------------------------------------------------------------------
-- Aiming: the player cannot aim in a scripted run. Fixed facts of the beach start (RE.md section 39): the shot leaves the hand along the camera direction, which is 14 degrees above the horizon,
-- in a straight line (no gravity). A first shot with nothing in the way measures the line; the dummies stand on it: Gyrgers are tall enough to meet it 260 units from the hand.
local LINE_T = 260
local OFFSETS = { 0, 200, -200 }
local rig = { p0 = nil, d = nil }
local proj_alive, proj_entity = false, nil
local records, damaged = {}, {}

risen.on("entity.spawned", function(e, template)
    if e and template:find("^It_Projectile_Fireball") then proj_entity = e; proj_alive = true end
end)
risen.on("entity.damaged", function(victim, attacker, lost, raw)
    damaged[#damaged + 1] = { victim = victim and victim:name() or "?", attacker = attacker and attacker:name() or "-", lost = lost, raw = raw }
end)

local function player_now() return assert(risen.entity.player(), "no player") end

local function draw()
    local p = player_now()
    if p:state() ~= "PS_MagicBall_Loop" then
        if p:state() ~= "PS_Normal" and p:state() ~= "PS_Magic" then p:set_state("PS_Normal"); T.sleep(40) end
        p:set_state("PS_Magic")
        T.wait_until(function() return p:state() == "PS_MagicBall_Loop" end, 30, "PS_MagicBall_Loop")
    end
end

local function prepare_caster()
    local p = player_now()
    assert(risen.npc.set_stats(p, { skills = { Fireball = 3, Circle = 3 }, mana_max = 4000, mana = 4000 }))
    if risen.item.count(p, "It_Crystal_Fire") < 1 then risen.item.give(p, "It_Crystal_Fire", 1) end
    if risen.spell.equipped(p) ~= "It_Crystal_Fire" then risen.item.equip(p, "It_Crystal_Fire") end
    p:set_hp(p:hp_max())
end

-- one shot; returns when the projectile is gone and the events of the hits were delivered
local function shoot()
    local p = player_now()
    draw()
    p:set_hp(p:hp_max())
    proj_alive, proj_entity = false, nil
    p:set_state("PS_MagicBall_Shoot")
    T.wait_until(function() return proj_alive end, 20, "the projectile")
    if proj_entity and proj_entity:valid() then
        local x, y, z = proj_entity:position()
        local px, py, pz = p:position()
        L(("shot starts %.0f %.0f %.0f (line %.0f %.0f %.0f) player %.0f %.0f %.0f state %s hp %s"):format(x, y, z, rig.p0[1], rig.p0[2], rig.p0[3], px, py, pz, tostring(p:state()), tostring(p:hp())))
    end
    local lx, ly, lz
    for _ = 1, 150 do
        T.sleep(2)
        if not (proj_entity and proj_entity:valid()) then break end
        lx, ly, lz = proj_entity:position()
    end
    if lx then L(("projectile last seen %.0f %.0f %.0f"):format(lx, ly, lz)) end
    T.sleep(20)
end

local seq = 0
-- Where the shot will go: the player is in the aiming state, the camera direction (horizontal part) is the direction of the shot. The hand is about 78 units ahead of the player along it,
-- LINE_T units further the line meets a Gyrger (the shot is 14 degrees above the horizon and a Gyrger is tall and wide: +-100 units do not matter). Read right before the dummies are made:
-- the camera drifts a little from cast to cast.
local function aim()
    draw()
    local c = assert(risen.world.camera(), "no camera")
    local h = math.sqrt(c.dx * c.dx + c.dz * c.dz)
    local px, py, pz = player_now():position()
    return { x = px, y = py, z = pz, hx = c.dx / h, hz = c.dz / h }
end

local function dummy_at(a, off, label)
    seq = seq + 1
    local dist = 78 + LINE_T
    local x, z = a.x + a.hx * dist - a.hz * off, a.z + a.hz * dist + a.hx * off
    local name = ("DHD_%s_%d"):format(label, seq)
    local w = assert(risen.npc.spawn("Gyrger", x, a.y, z, { name = name, hp_max = 5000 }))
    w:set_frozen(true)
    risen.npc.set_stats(w, { skills = { ProtMagic = 0 } })
    return w
end

-- a volley: fresh dummies (a hit wakes a frozen one), one shot; returns { dummies = { {name, off, lost, hp0, hp1} ... }, calls = records, damaged = events }
local function volley(label, offsets)
    offsets = offsets or OFFSETS
    local ws = {}
    local a = aim()
    for i, off in ipairs(offsets) do ws[i] = dummy_at(a, off, label) end
    T.sleep(30)
    local hp0 = {}
    for i, w in ipairs(ws) do
        w:set_hp(5000); hp0[i] = w:hp()
        local x, y, z = w:position()
        L(("dummy %s at %.0f %.0f %.0f state %s dead %s"):format(w:name(), x, y, z, tostring(w:state()), tostring(w:is_dead())))
    end
    records, damaged = {}, {}
    shoot()
    local out = { dummies = {}, calls = records, damaged = damaged }
    for i, w in ipairs(ws) do
        out.dummies[i] = { name = w:name(), off = offsets[i], hp0 = hp0[i], hp1 = w:hp(), lost = hp0[i] - w:hp(), prot = w:skill_total("ProtMagic") }
        w:kill()
    end
    T.sleep(10)
    return out
end

local function by_victim(v, name)
    local list = {}
    for _, r in ipairs(v.calls) do if r.victim == name then list[#list + 1] = r end end
    return list
end

local function damaged_of(v, name)
    local list = {}
    for _, e in ipairs(v.damaged) do if e.victim == name then list[#list + 1] = e end end
    return list
end

local function summary(v)
    local parts = {}
    for _, d in ipairs(v.dummies) do
        local c = by_victim(v, d.name)[1]
        parts[#parts + 1] = ("%s lost %d%s"):format(d.off, d.lost, c and (" (handler saw %s x%.3f, gave %s)"):format(c.damage, c.mult, tostring(c.gave)) or " (no call)")
    end
    return table.concat(parts, "; ")
end

-- a recording handler: `fn(hit)` decides the result; every call is noted
local function recorder(fn, filter)
    local h, why = risen.combat.on_damage(function(hit)
        local rec = {
            victim = hit.victim:name(), attacker = hit.attacker and hit.attacker:name(), source = hit.source and hit.source:name(), weapon = hit.weapon, kind = hit.kind, spell = hit.spell,
            type = hit.type, script = hit.damage_script, damage = hit.damage, original = hit.original_damage, mult = hit.multiplier, prot = hit.protection,
        }
        local r = fn and fn(hit, rec)
        rec.gave = r
        records[#records + 1] = rec
        return r
    end, filter)
    return assert(h, why)
end

local function expected_loss(new_damage, prot, mult)
    return math.max(new_damage - prot, math.floor(mult * 10))
end

T.run(function()
    T.wait_event("session.start", 240, function(m) return phase ~= "load" and m == "new" or phase == "load" and m == "load" end)
    T.sleep(200)
    local p = player_now()
    prepare_caster()

    if phase == "load" then
        -- ---- phase `load`: a fresh process, the mod registered its handler again (main.lua) ------------------------------------------------------------------------
        T.section("fresh process")
        T.eq("loaded: the handler registered by the mod is active", #risen.combat.handlers(), 1)
        -- calibrate with a cast into the air
        draw()
        proj_alive, proj_entity = false, nil
        p:set_state("PS_MagicBall_Shoot")
        T.wait_until(function() return proj_alive end, 20, "the projectile")
        local a = { proj_entity:position() }
        T.sleep(8)
        local b = { proj_entity:position() }
        local d = { b[1] - a[1], b[2] - a[2], b[3] - a[3] }
        local len = math.sqrt(d[1] ^ 2 + d[2] ^ 2 + d[3] ^ 2)
        rig.p0, rig.d = a, { d[1] / len, d[2] / len, d[3] / len }
        T.sleep(100)
        -- the mod's handler (main.lua) doubles the damage; this observer is registered after it and sees the doubled number
        recorder(nil)
        local v = volley("load", { 0 })
        local c = by_victim(v, v.dummies[1].name)[1]
        T.expect("loaded: a real cast goes through the handler", c ~= nil and c.kind == "spell" and c.spell == "fireball" and c.original == 175 and c.damage == 350, summary(v))
        T.eq("loaded: the hit points follow the handler", v.dummies[1].lost, c and expected_loss(c.damage, c.prot or 0, c.mult) or -1)
        return
    end

    -- ---- phase `save` ----------------------------------------------------------------------------------------------------------------------------------------------
    T.section("API")
    T.expect("the API is there", type(risen.combat.on_damage) == "function" and type(risen.combat.off_damage) == "function" and type(risen.combat.handlers) == "function"
        and type(risen.combat.failures) == "function" and #risen.combat.handlers() == 0)
    local bad = {
        { risen.combat.on_damage(nil) },
        { risen.combat.on_damage(42) },
        { risen.combat.on_damage(function() end, 5) },
        { risen.combat.on_damage(function() end, { colour = "red" }) },
        { risen.combat.on_damage(function() end, { kind = "magic" }) },
        { risen.combat.on_damage(function() end, { spell = "lightning" }) },
        { risen.combat.on_damage(function() end, { kind = {} }) },
        { risen.combat.on_damage(function() end, { kind = 7 }) },
        { risen.combat.on_damage(function() end, { attacker = "bob" }) },
        { risen.combat.on_damage(function() end, { victim = {} }) },
        { risen.combat.on_damage(function() end, { weapon = { 1 } }) },
    }
    local refused = true
    for i, r in ipairs(bad) do
        if r[1] ~= nil or type(r[2]) ~= "string" then refused = false; L("not refused:", i, tostring(r[1]), tostring(r[2])) end
    end
    T.expect("bad arguments are refused with a reason", refused and #risen.combat.handlers() == 0 and risen.combat.off_damage(nil) == false and risen.combat.off_damage("x") == false
        and risen.combat.off_damage(0) == false and risen.combat.off_damage(-3) == false and risen.combat.off_damage(0 / 0) == false and risen.combat.off_damage(1e300) == false
        and risen.combat.off_damage({}) == false, tostring(#risen.combat.handlers()))
    local k, s = risen.combat.classify("DoMagicFireballDamage")
    L("classify:", k, s, risen.combat.classify("DoSwordDamage"), risen.combat.classify("Nope"), risen.combat.classify(5))

    -- ---- calibration: the first shot flies off into the air and tells where the line goes ------------------------------------------------------------------------------------------
    T.section("calibration")
    draw()
    proj_alive, proj_entity = false, nil
    p:set_state("PS_MagicBall_Shoot")
    T.wait_until(function() return proj_alive end, 20, "the calibration projectile")
    local a = { proj_entity:position() }
    T.sleep(8)
    local b = { proj_entity:position() }
    local d = { b[1] - a[1], b[2] - a[2], b[3] - a[3] }
    local len = math.sqrt(d[1] ^ 2 + d[2] ^ 2 + d[3] ^ 2)
    rig.p0, rig.d = a, { d[1] / len, d[2] / len, d[3] / len }
    L(("line from %.0f %.0f %.0f along %.3f %.3f %.3f"):format(a[1], a[2], a[3], rig.d[1], rig.d[2], rig.d[3]))
    T.expect("calibration: the shot flies along a line", len > 10 and math.abs(rig.d[2]) < 0.9)
    T.sleep(100)

    -- ---- no handler yet: a plain cast, the stock numbers -----------------------------------------------------------------------------------------------------------------------------
    T.section("plain cast")
    local plain = volley("plain")
    L("plain:", summary(plain))
    local all_hit = true
    for _, dm in ipairs(plain.dummies) do all_hit = all_hit and dm.lost > 0 end
    T.expect("a plain cast hits three dummies and each loses its hit points", all_hit, summary(plain))

    -- ---- an observing handler -----------------------------------------------------------------------------------------------------------------------------------------------------
    T.section("observing handler")
    local obs = recorder(function() return nil end)
    local v = volley("obs")
    L("observer:", summary(v))
    local same = true
    local c1 = by_victim(v, v.dummies[1].name)[1]
    for _, dm in ipairs(v.dummies) do
        local c = by_victim(v, dm.name)[1]
        same = same and c ~= nil and dm.lost == expected_loss(c.damage, c.prot or 0, c.mult)
    end
    T.expect("an observing handler sees the hit and changes nothing", same, summary(v))
    T.expect("the handler's context: attacker, victim, projectile, spell, type, damage", c1 and c1.attacker == player_now():name() and c1.weapon == "It_Projectile_Fireball_S0" and c1.kind == "spell"
        and c1.spell == "fireball" and c1.type == "Magic" and c1.script == "DoMagicFireballDamage" and c1.damage == 175 and c1.original == 175 and c1.source ~= nil, c1 and (
        ("attacker %s weapon %s kind %s spell %s type %s script %s damage %s"):format(tostring(c1.attacker), c1.weapon, c1.kind, tostring(c1.spell), c1.type, c1.script, tostring(c1.damage))) or "no call")
    local o1, o2, o3 = by_victim(v, v.dummies[1].name)[1], by_victim(v, v.dummies[2].name)[1], by_victim(v, v.dummies[3].name)[1]
    T.expect("area falloff: the multiplier of the outer dummies is below 1 and the damage follows it", o1 and o2 and o3 and o1.mult > 0.99 and o2.mult < 0.9 and o3.mult < 0.9 and o2.mult > 0 and o3.mult > 0
        and math.abs(o2.damage - 100 * o2.mult) <= 1 and math.abs(o3.damage - 100 * o3.mult) <= 1, o1 and o2 and o3 and ("%.3f %.3f %.3f / %s %s %s"):format(o1.mult, o2.mult, o3.mult, o1.damage, o2.damage, o3.damage) or "?")
    assert(risen.combat.off_damage(obs))

    -- ---- scale the damage ------------------------------------------------------------------------------------------------------------------------------------------------------------------
    T.section("scaling")
    local scale = recorder(function(hit) return hit.damage * 4 end, { spell = "fireball" })
    v = volley("scale")
    L("x4:", summary(v))
    local ok_all, ok_prop = true, true
    local prev
    for _, dm in ipairs(v.dummies) do
        local c = by_victim(v, dm.name)[1]
        ok_all = ok_all and c ~= nil and dm.lost == expected_loss(c.damage * 4, c.prot or 0, c.mult)
    end
    local s1, s2, s3 = by_victim(v, v.dummies[1].name)[1], by_victim(v, v.dummies[2].name)[1], by_victim(v, v.dummies[3].name)[1]
    T.expect("a handler that scales the damage: every dummy loses the new damage", ok_all and #v.calls == 3, summary(v))
    T.expect("area falloff survives the handler (damage stays proportional to the multiplier)", s1 and s2 and s3 and v.dummies[1].lost == 700 - (s1.prot or 0)
        and math.abs(v.dummies[2].lost - (s2.damage * 4 - (s2.prot or 0))) <= 1 and v.dummies[2].lost < v.dummies[1].lost and v.dummies[3].lost < v.dummies[1].lost, summary(v))
    local one_each = #v.calls == 3 and #v.damaged == 3
    for _, dm in ipairs(v.dummies) do
        one_each = one_each and #by_victim(v, dm.name) == 1 and #damaged_of(v, dm.name) == 1 and damaged_of(v, dm.name)[1].lost == dm.lost
    end
    T.expect("no double application: one call and one entity.damaged per dummy", one_each, ("calls %d, damaged %d"):format(#v.calls, #v.damaged))
    assert(risen.combat.off_damage(scale))
    -- protection: the engine subtracts the victim's protection from what the handler returned, but never goes below its minimum (the hit's multiplier times 10, RE.md section 39)
    local want = 700
    local hp_h = recorder(function() return want end)
    local prot_lost, prot_seen = {}, nil
    for _, give in ipairs { 700, 50 } do
        want = give
        local armored = dummy_at(aim(), 0, "prot")
        risen.npc.set_stats(armored, { skills = { ProtMagic = 100 } })
        T.sleep(20)
        armored:set_hp(5000)
        records, damaged = {}, {}
        shoot()
        prot_lost[#prot_lost + 1] = 5000 - armored:hp()
        prot_seen = armored:skill_total("ProtMagic")
        armored:kill()
        T.sleep(10)
    end
    L("protection:", prot_seen, prot_lost[1], prot_lost[2])
    T.eq("stock protection still applies after the handler", { prot_seen, prot_lost[1] }, { 100, 600 })
    T.eq("the minimum damage floor still applies after the handler", prot_lost[2], 10)
    risen.combat.off_damage(hp_h)

    -- ---- cancel ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    T.section("cancel")
    local protect
    local cancel = recorder(function(hit)
        if hit.victim:name() == protect then return false end
        return nil
    end)
    protect = nil
    -- the name of the middle dummy is only known after it exists: spawn by hand
    local a = aim()
    local mid = dummy_at(a, 200, "cancel")
    local w1, w3 = dummy_at(a, 0, "cancel"), dummy_at(a, -200, "cancel")
    protect = mid:name()
    T.sleep(30)
    for _, w in ipairs { w1, mid, w3 } do w:set_hp(5000) end
    records, damaged = {}, {}
    shoot()
    local lost1, lostm, lost3 = 5000 - w1:hp(), 5000 - mid:hp(), 5000 - w3:hp()
    L("cancel false:", lost1, lostm, lost3, #records, #damaged)
    T.expect("cancel: false protects the victim, the others are hit", lost1 > 0 and lostm == 0 and lost3 > 0 and #damaged == 2, ("%d %d %d, damaged events %d"):format(lost1, lostm, lost3, #damaged))
    for _, w in ipairs { w1, mid, w3 } do w:kill() end
    assert(risen.combat.off_damage(cancel))
    T.sleep(10)
    local cancel0 = recorder(function(hit)
        if hit.multiplier < 0.99 then return 0 end
    end)
    v = volley("cancel0")
    local lost_centre, lost_outer = v.dummies[1].lost, v.dummies[2].lost + v.dummies[3].lost
    T.expect("cancel: 0 does the same", lost_centre > 0 and lost_outer == 0, summary(v))
    assert(risen.combat.off_damage(cancel0))

    -- ---- chains ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    T.section("chains")
    local first = recorder(function(hit) return hit.damage * 2 end)
    local seen_second
    local second = recorder(function(hit)
        if hit.victim:name() == nil then return nil end
        seen_second = hit.damage
        return hit.damage + 10
    end)
    v = volley("chain", { 0 })
    local ch = v.calls
    local c_first, c_second = ch[1], ch[2]
    L("chain:", summary(v), c_first and c_first.damage, c_second and c_second.damage)
    T.expect("chained handlers: the second sees the first one's damage", c_first and c_second and c_second.damage == c_first.damage * 2 and c_second.original == c_first.damage
        and v.dummies[1].lost == expected_loss(c_first.damage * 2 + 10, c_first.prot or 0, c_first.mult), summary(v))
    assert(risen.combat.off_damage(first)); assert(risen.combat.off_damage(second))
    -- cancel in the first handler ends the chain
    local stopper = recorder(function() return false end)
    local after = recorder(function() return nil end)
    v = volley("stop", { 0 })
    T.expect("a cancel ends the chain", #v.calls == 1 and v.dummies[1].lost == 0, ("%d calls, lost %d"):format(#v.calls, v.dummies[1].lost))
    assert(risen.combat.off_damage(stopper)); assert(risen.combat.off_damage(after))

    -- ---- filters ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    T.section("filters")
    local pn = player_now():name()
    local n_frost, n_melee, n_other_attacker, n_fire, n_proj, n_player_dummy = 0, 0, 0, 0, 0, 0
    local hs = {
        risen.combat.on_damage(function() n_frost = n_frost + 1 end, { spell = "frost" }),
        risen.combat.on_damage(function() n_melee = n_melee + 1 end, { kind = "melee" }),
        risen.combat.on_damage(function() n_other_attacker = n_other_attacker + 1 end, { attacker = p, kind = "melee" }),
        risen.combat.on_damage(function() n_fire = n_fire + 1 end, { spell = { "fireball", "missile" }, type = "Magic", attacker = "player" }),
        risen.combat.on_damage(function() n_proj = n_proj + 1 end, { weapon = "It_Projectile_Fireball_S0", script = "DoMagicFireballDamage" }),
        risen.combat.on_damage(function() n_player_dummy = n_player_dummy + 1 end, { victim = "player" }),
    }
    v = volley("filter")
    L(("filters: frost %d melee %d player-melee %d fire %d proj %d victim-player %d"):format(n_frost, n_melee, n_other_attacker, n_fire, n_proj, n_player_dummy))
    T.expect("filters: a handler for another spell, kind or attacker is not called", n_frost == 0 and n_melee == 0 and n_player_dummy == 0 and n_fire == 3, ("frost %d melee %d victim-player %d fire %d"):format(n_frost, n_melee, n_player_dummy, n_fire))
    T.expect("filters: the weapon filter matches the projectile template", n_proj == 3, tostring(n_proj))
    for _, h in ipairs(hs) do risen.combat.off_damage(h) end
    -- victim and attacker by entity
    local target_name
    local nv, na = 0, 0
    local a2 = aim()
    local mid2 = dummy_at(a2, 200, "filt")
    local c0 = dummy_at(a2, 0, "filt")
    T.sleep(30)
    local hv = risen.combat.on_damage(function() nv = nv + 1 end, { victim = mid2 })
    local ha = risen.combat.on_damage(function() na = na + 1 end, { attacker = { p, c0 } })
    local nz = 0
    local hz = risen.combat.on_damage(function() nz = nz + 1 end, { attacker = c0 })
    mid2:set_hp(5000); c0:set_hp(5000)
    records, damaged = {}, {}
    shoot()
    T.expect("filters: victim and attacker", nv == 1 and na == 2 and nz == 0, ("victim filter %d, attacker filter %d, other attacker %d"):format(nv, na, nz))
    risen.combat.off_damage(hv); risen.combat.off_damage(ha); risen.combat.off_damage(hz)
    mid2:kill(); c0:kill()
    T.sleep(10)

    -- ---- errors -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    T.section("errors")
    local f0 = risen.combat.failures()
    local boom = recorder(function() error("handler boom") end)
    v = volley("err")
    local stock_ok = true
    for _, dm in ipairs(v.dummies) do
        local c = by_victim(v, dm.name)[1]
        -- the recorder noted nothing for a failing call (the error came first): stock numbers are checked from the plain cast
    end
    local lost_err = v.dummies[1].lost
    T.expect("an error in a handler is a warning and the stock damage applies", lost_err == plain.dummies[1].lost and v.dummies[2].lost > 0 and v.dummies[3].lost > 0 and #v.damaged == 3,
        ("centre lost %d (plain %d), damaged events %d"):format(lost_err, plain.dummies[1].lost, #v.damaged))
    assert(risen.combat.off_damage(boom))
    local garbage = { "lots", -5, 0 / 0, 1 / 0, {}, function() end }
    local lost_bad = {}
    for i, g in ipairs(garbage) do
        local h = recorder(function() return g end)
        local vv = volley("bad" .. i, { 0 })
        lost_bad[#lost_bad + 1] = vv.dummies[1].lost
        risen.combat.off_damage(h)
    end
    local all_stock = true
    for i, l in ipairs(lost_bad) do
        -- 1/0 is not finite and a function is not a damage: stock; -5 and NaN too
        if l ~= plain.dummies[1].lost then all_stock = false end
    end
    T.expect("a bad return value is a warning and the stock damage applies", all_stock, table.concat(lost_bad, ",") .. " vs plain " .. plain.dummies[1].lost)
    local bad_first = recorder(function() error("first fails") end)
    local good_second = recorder(function(hit) return hit.damage * 3 end)
    v = volley("after", { 0 })
    local cg = by_victim(v, v.dummies[1].name)[1]
    T.expect("a handler after a failing one still works", cg ~= nil and v.dummies[1].lost == expected_loss(cg.damage * 3, cg.prot or 0, cg.mult), summary(v))
    local f1 = risen.combat.failures()
    T.expect("failures() counts the failed calls", f1 - f0 >= 3 + 6 + 1, ("%d -> %d"):format(f0, f1))
    risen.combat.off_damage(bad_first); risen.combat.off_damage(good_second)

    -- ---- unregister -------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    T.section("unregister")
    local gone = 0
    local hg = risen.combat.on_damage(function(hit) gone = gone + 1; return hit.damage * 10 end)
    local was = risen.combat.off_damage(hg)
    local twice = risen.combat.off_damage(hg)
    v = volley("off", { 0 })
    T.eq("unregistering: the handler is gone and the stock damage applies again", { was, twice, gone, v.dummies[1].lost, #risen.combat.handlers() }, { true, false, 0, plain.dummies[1].lost, 0 })

    -- ---- melee: a wolf against a peasant ------------------------------------------------------------------------------------------------------------------------------------------------
    T.section("melee")
    local bites, blows = {}, {}
    local dmg_log = {}
    local hm = risen.combat.on_damage(function(hit)
        local rec = { attacker = hit.attacker and hit.attacker:name() or "-", victim = hit.victim:name(), weapon = hit.weapon, script = hit.damage_script, kind = hit.kind, type = hit.type,
                      damage = hit.damage, prot = hit.protection, mult = hit.multiplier }
        local give = hit.damage * 2
        rec.gave = give
        dmg_log[#dmg_log + 1] = rec
        if rec.weapon == "It_Fist_Monster" then bites[#bites + 1] = rec else blows[#blows + 1] = rec end
        return give
    end, { kind = "melee" })
    local px, py, pz = p:position()
    local victim = assert(risen.npc.spawn("Peasant_1_01", px + 700, py, pz, { name = "DHM_Peasant", hp_max = 4000 }))
    local wolf_hp0 = nil
    T.sleep(60)
    local wolf = assert(risen.npc.spawn("Wolf", px + 770, py, pz, { name = "DHM_Wolf", hp_max = 4000 }))
    damaged = {}
    local function enough() return #bites >= 2 and #blows >= 2 end
    for _ = 1, 600 do
        if enough() then break end
        T.sleep(10)
        if victim:hp() < 1000 then victim:set_hp(4000) end
        if wolf:valid() and wolf:hp() < 1000 then wolf:set_hp(4000) end
    end
    T.sleep(30)
    risen.combat.off_damage(hm)
    L(("melee: %d bites, %d blows"):format(#bites, #blows))
    if wolf:valid() then wolf:kill() end
    if victim:valid() then victim:kill() end
    local b1, w1r = bites[1], blows[1]
    T.expect("wolf bites: a fist hit, an edge hit, by the wolf", b1 and b1.script == "DoFistDamage" and b1.type == "Edge" and b1.attacker == "DHM_Wolf" and b1.victim == "DHM_Peasant" and b1.kind == "melee",
        b1 and ("%s %s %s %s -> %s"):format(b1.weapon, b1.script, b1.type, b1.attacker, b1.victim) or "no bite")
    T.expect("club blows: a weapon hit, blunt, by the peasant", w1r and w1r.script == "DoSwordDamage" and w1r.type == "Blunt" and w1r.attacker == "DHM_Peasant" and w1r.victim == "DHM_Wolf" and w1r.weapon:find("^It_") ~= nil,
        w1r and ("%s %s %s %s -> %s"):format(w1r.weapon, w1r.script, w1r.type, w1r.attacker, w1r.victim) or "no blow")
    -- every hit that took hit points reported (entity.damaged) the doubled damage minus protection (the order of the two lists is the order of the hits)
    local matched, checked = 0, 0
    local di = 1
    for _, rec in ipairs(dmg_log) do
        local lost_expected = math.max(rec.gave - (rec.prot or 0), math.floor(rec.mult * 10))
        -- find the next damaged event of this victim
        while di <= #damaged and damaged[di].victim ~= rec.victim do di = di + 1 end
        if di > #damaged then break end
        checked = checked + 1
        if damaged[di].lost == lost_expected then matched = matched + 1 end
        di = di + 1
    end
    T.expect("melee: the new damage reaches the victim", checked >= 3 and matched == checked, ("%d of %d hits matched"):format(matched, checked))

    -- ---- a handler applies damage itself ---------------------------------------------------------------------------------------------------------------------------------------------
    T.section("nested damage")
    local px2, py2, pz2 = p:position()
    local extra = assert(risen.npc.spawn("Gyrger", px2 + 100, py2, pz2 + 1500, { name = "DHN_Extra", hp_max = 5000 }))
    extra:set_frozen(true)
    risen.npc.set_stats(extra, { skills = { ProtMagic = 0 } })
    local target = dummy_at(aim(), 0, "nest")
    T.sleep(30)
    local nest_calls, nested = 0, nil
    local hn = risen.combat.on_damage(function(hit)
        nest_calls = nest_calls + 1
        if nest_calls == 1 and hit.source then
            -- the handler hits another victim itself, through the engine's DoDamage; this call must not be offered to the handlers again
            nested = risen.script.call("DoDamage", hit.source, extra, 50)
        end
    end, { spell = "fireball" })
    target:set_hp(5000); extra:set_hp(5000)
    shoot()
    local extra_lost = 5000 - extra:hp()
    L("nested:", nest_calls, nested, extra_lost)
    T.expect("a handler may apply damage with DoDamage itself (the nested call is not offered again)", nest_calls == 1 and nested == 1 and extra_lost > 0 and extra_lost <= 50, ("handler calls %d, DoDamage -> %s, extra lost %d"):format(nest_calls, tostring(nested), extra_lost))
    risen.combat.off_damage(hn)
    target:kill(); extra:kill()

    -- ---- save ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    T.section("save")
    T.expect("every handler of the scenario was removed again", #risen.combat.handlers() == 0, tostring(#risen.combat.handlers()))
    local saved = false
    risen.on("save.after", function(_, ok) saved = ok end)
    p:set_state("PS_Normal")
    T.sleep(60)
    L("quick_save", risen.world.quick_save())
    T.wait_until(function() return saved end, 120, "save.after")
    T.expect("saved", saved)
    T.sleep(200)
end, "damagehooks")
