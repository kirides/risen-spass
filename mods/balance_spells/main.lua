-- Spell balancing and new spells with risen.balance (docs/LUA_API.md "Spells", docs/RE.md section 29), played as a risen.test scenario:
-- scripts/smoke-test.ps1 -Scenario spells (two game processes: phase `save`, then phase `load` in a fresh process).
--
-- A spell is made of items: a crystal (Fireball, Frost, Missile) or a rune / scroll / orb (everything else) carries the mana cost, the requirement and
-- what is consumed (gCCastInfo_PS), a projectile spell spawns a projectile template whose numbers are the damage, speed and radius (gCDamage_PS,
-- gCProjectile2_PS). The `spells` section edits the shipped ones, `new_spells` builds new ones from the pieces of an existing spell.
--
-- Everything is verified in the engine's own cast path: the player really casts (the state scripts PS_Magic -> PS_MagicBall_Loop -> PS_MagicBall_Shoot spawn
-- the projectile and take the mana through PSCastInfo::Consume), the mana that was taken, the numbers of the projectile that was spawned and the hit
-- points the engine's own area-damage script (DoFireballAreaDamage, what a hit calls) takes from a victim are compared with the formulas.
local T = risen.test
local log = risen.log
local phase = T.param("phase", "save")
local function L(...)
    local t = {}
    for i = 1, select("#", ...) do t[#t + 1] = tostring((select(i, ...))) end
    log.info("balance_spells[" .. phase .. "] " .. table.concat(t, " "))
end

local SPEC = {
    spells = {
        Fireball = { mana_cost = 12, damage = 150 },                      -- the shipped crystal: 8 mana, 100 damage
        Frost = { damage_mult = 2, speed_mult = 1.5 },                    -- relative to the shipped values (never compounds)
        Light = { mana_cost = 7, scroll_mana_cost = 3 },                  -- rune 10 -> 7, scroll 5 -> 3
        Skeleton = { mana_mult = 0.5 },                                   -- rune 40 -> 20, scroll 20 -> 10, orb stays free
    },
    new_spells = {
        -- a Fireball with double damage (of the already edited Fireball: 150 -> 300), double cost and a higher requirement, as crystal and scroll
        Spass_Fireball_Strong = {
            base = "Fireball", name = { English = "Strong Fireball", German = "Starker Feuerball" }, description = "A fireball that hits twice as hard.",
            mana_cost = 24, requires = { Fireball = 2 }, damage_mult = 2, speed_mult = 2, radius_mult = 1.5, forms = { "crystal", "scroll" },
        },
        -- the Skeleton spell for less mana and a lower circle, as rune, scroll and orb
        Spass_Skeleton_Cheap = {
            base = "Skeleton", name = "Cheap Skeleton", mana_cost = 15, requires = { Circle = 2 }, forms = { "rune", "scroll", "orb" }, scroll = { mana_cost = 7 },
        },
    },
}
local STRONG, STRONG_SCROLL = "Spass_Fireball_Strong", "Spass_Fireball_Strong_Scroll"
local CHEAP, CHEAP_SCROLL, CHEAP_ORB = "Spass_Skeleton_Cheap", "Spass_Skeleton_Cheap_Scroll", "Spass_Skeleton_Cheap_Orb"

-- LuaLS: these calls return nil on failure; a verification wants to stop there
local function spell_get(name) return assert(risen.spell.get(name)) end
local function tpl(name) return assert(risen.template.get(name)) end

local applied = { first = false, session = false }
risen.balance.install(SPEC, {
    on_applied = function(when, report)
        if when == "first tick" then
            applied.first = true
            T.expect("the spec is applied on the first tick", report.ok and #report.applied == 6 and #report.failed == 0,
                ("applied %d, failed %d: %s"):format(#report.applied, #report.failed, table.concat(report.failed, "; ")))
        elseif when:find("^session.start") then
            applied.session = true
            T.expect("session.start applies it again (idempotent)", report.ok and #report.applied == 6 and #report.failed == 0,
                ("applied %d, failed %d: %s"):format(#report.applied, #report.failed, table.concat(report.failed, "; ")))
        end
    end,
})

-- ---- the cast helpers ------------------------------------------------------------------------------------------------------------------
local equipped_events = {}
risen.on("item.equipped", function(owner, template, slot) equipped_events[#equipped_events + 1] = template .. "@" .. tostring(slot) end)

-- Every shot: the projectile entity (its own copy of the numbers) and, when `measure` is armed, a victim at its position that the engine's area-damage
-- script hurts, exactly as a hit does (DoFireballAreaDamage of OnHitFireball_S*): the loss is measured synchronously, so the real flight cannot add to it.
---@class SpellShot
---@field template string
---@field damage number|nil
---@field speed number|nil
---@field radius number|nil
---@field damage_type string|nil
---@field shooter string|nil
---@field prot number|nil
---@field area any
---@field lost number|nil
---@field mana number
local SpellShot = {}   -- (only carries the class annotation above)
local measure, victim = false, nil
---@type SpellShot|nil
local shot = nil
risen.on("entity.spawned", function(e, template)
    if not e or not tostring(template):find("^It_Projectile_Fireball") then return end
    ---@type SpellShot
    local s = { mana = 0, template = template, damage = tonumber(e:get_property("gCDamage_PS", "DamageAmount")), speed = tonumber(e:get_property("gCProjectile2_PS", "StartVelocity")),
                radius = tonumber(e:get_property("gCProjectile2_PS", "SweepTestRadius")), damage_type = e:get_property("gCDamage_PS", "DamageType") }
    local shooter = risen.spell.shooter(e)
    s.shooter = shooter and shooter:name()
    if measure and victim then
        victim:teleport(e)
        s.prot = victim:skill_total("ProtMagic")
        victim:set_hp(5000)
        local hp0 = victim:hp()
        s.area = risen.script.call("DoFireballAreaDamage", e, nil, 100)
        s.lost = hp0 - victim:hp()
        victim:kill()   -- it stands where the projectile is: the next shot would hit it at once
        victim = nil
    end
    shot = s
end, -10)

local function player_now() return assert(risen.entity.player(), "no player") end
local victim_n = 0
-- a frozen wolf that the area damage hurts (created a frame before the shot: the engine's NPC list sees it only then)
local function spawn_victim()
    local p = player_now()
    local x, y, z = p:position()
    x, y, z = x or 0, y or 0, z or 0
    victim_n = victim_n + 1
    victim = assert(risen.npc.spawn("Wolf", x + 2500, y, z, { name = "SPELL_VICTIM_" .. victim_n, hp_max = 5000 }))
    victim:set_frozen(true)
    T.sleep(20)
end

-- equips `item` (a crystal or scroll of a projectile spell) and waits until the engine's magic state machine is ready to shoot
local function draw(item)
    local p = player_now()
    local cur = risen.spell.equipped(p)
    if cur and cur ~= item then risen.item.unequip(p, cur) end
    if p:state() ~= "PS_Normal" then p:set_state("PS_Normal"); T.sleep(60) end
    T.expect("equip " .. item, risen.item.equip(p, item), "risen.item.equip")
    p:set_state("PS_Magic")
    T.wait_until(function() return p:state() == "PS_MagicBall_Loop" end, 90, "PS_MagicBall_Loop for " .. item)
end

-- one real cast: returns { mana = mana taken, template, damage, speed, lost, prot, ... }
---@return SpellShot
local function fire(label, armed)
    local p = player_now()
    if armed ~= false then spawn_victim() end
    measure, shot = armed ~= false, nil
    local mana0 = p:mana()
    p:set_state("PS_MagicBall_Shoot")
    T.wait_until(function() return shot ~= nil end, 30, label .. ": the projectile")
    local s = shot --[[@as SpellShot]]
    T.wait_until(function() return p:state() ~= "PS_MagicBall_Shoot" and p:mana() ~= mana0 end, 40, label .. ": the shot is over")
    T.sleep(10)
    measure = false
    s.mana = mana0 - p:mana()
    L(label, "mana", s.mana, "projectile", s.template, "damage", s.damage, "speed", s.speed, "type", s.damage_type, "lost", s.lost, "prot", s.prot, "area", s.area)
    return s
end

local function prepare_caster(fireball, circle, mana)
    local p = player_now()
    assert(risen.npc.set_stats(p, { skills = { Fireball = fireball, Circle = circle }, mana_max = 400, mana = mana or 400 }))
    return p
end

local function count(p, item) return risen.item.count(p, item) end
local function near(a, b) return a ~= nil and b ~= nil and math.abs(a - b) < 0.01 end

-- ---- phase `save` ----------------------------------------------------------------------------------------------------------------------
local PLAN_SAVE = {
    "the spec is applied on the first tick",
    "session.start applies it again (idempotent)",
    "the catalogue lists the spells",
    "the shipped Fireball has the edited numbers",
    "relative settings are computed from the shipped values",
    "rune spells: cost of rune and scroll",
    "mana_mult scales rune and scroll, the orb stays free",
    "the new items are clones with the numbers of the spec",
    "the labels of the new spell",
    "requirements of the new spell",
    "the engine's equip check follows the requirement",
    "the other spells are untouched",
    "equip " .. "It_Crystal_Fire",
    "equip " .. STRONG,
    "equip " .. STRONG_SCROLL,
    "cast: the shipped crystal takes the edited mana cost",
    "cast: the projectile carries the edited damage",
    "cast: a hit takes damage + 25 * Fireball - protection",
    "cast: the new crystal takes its own mana cost",
    "cast: the new crystal's projectile has double damage",
    "cast: a hit of the new crystal takes the doubled damage",
    "cast: the next shot of the plain crystal is not affected",
    "cast: the equipped slot holds the crystal",
    "cast: item.equipped reports the magic slot",
    "cast: the scroll takes half the mana and is used up",
    "cast: the scroll's projectile has double damage",
    "cast: the used-up scroll leaves the stack",
    "rune: the engine's mana check follows the edited cost",
    "rune: casting takes exactly the cost, the rune stays",
    "scroll: casting takes the scroll cost and one scroll",
    "mana_mult: casting takes the scaled cost",
    "new rune: casting takes the new cost",
    "new scroll: casting takes the new cost and one scroll",
    "new orb: free and used up",
    "applying again does not compound",
    "invalid specs are refused with a reason",
    "a refused spec writes nothing",
    "items of the new spells are in the inventory before the save",
}
local PLAN_LOAD = {
    "the spec is applied on the first tick",
    "session.start applies it again (idempotent)",
    "fresh process: the player's items resolved to the new spells",
    "fresh process: the new spells have the numbers of the spec",
    "fresh process: the labels of the new spell",
    "fresh process: the engine's equip check follows the requirement",
    "fresh process: casting the saved scroll takes its mana and one scroll",
    "fresh process: the saved scroll's projectile has double damage",
    "fresh process: a hit takes the doubled damage",
    "fresh process: the shipped Fireball has the edited numbers",
}
T.plan(phase == "load" and PLAN_LOAD or PLAN_SAVE)

local function labels_ok(item)
    local t = tpl(item)
    return t and t:focus_name(), t and t:description()
end

T.run(function()
    T.wait_event("session.start", 240, function(m) return phase ~= "load" and m == "new" or phase == "load" and m == "load" end)
    T.sleep(150)
    local p = player_now()

    if phase == "load" then
        -- ---- phase `load`: a fresh process; the templates were created on the first tick, before the savegame was read ----------------
        T.section("fresh process")
        T.eq("fresh process: the player's items resolved to the new spells", { count(p, STRONG), count(p, STRONG_SCROLL), count(p, CHEAP), count(p, CHEAP_SCROLL), count(p, CHEAP_ORB) },
            { 1, 2, 1, 3, 2 })
        local strong, scroll = risen.spell.get ~= nil and spell_get("Fireball"), nil
        local t_strong = tpl(STRONG)
        T.eq("fresh process: the new spells have the numbers of the spec", { t_strong and t_strong:get_property("gCCastInfo_PS", "ManaCost"),
            tpl(STRONG_SCROLL):get_property("gCCastInfo_PS", "ManaCost"), tpl(STRONG_SCROLL):get_property("gCCastInfo_PS", "ConsumeItem"),
            risen.item.requirements(STRONG).Fireball, tpl(CHEAP):get_property("gCCastInfo_PS", "ManaCost") }, { "24", "12", "True", 2, "15" })
        local name, desc = labels_ok(STRONG)
        T.eq("fresh process: the labels of the new spell", { name, desc }, { "Strong Fireball", "A fireball that hits twice as hard." })
        prepare_caster(1, 3)
        local low = risen.item.can_use(p, STRONG)
        prepare_caster(3, 3)
        T.eq("fresh process: the engine's equip check follows the requirement", { low, risen.item.can_use(p, STRONG) }, { false, true })
        T.section("cast")
        T.eq("fresh process: the shipped Fireball has the edited numbers", { tonumber(tpl("It_Crystal_Fire"):get_property("gCCastInfo_PS", "ManaCost")),
            tonumber(tpl("It_Projectile_Fireball_S0"):get_property("gCDamage_PS", "DamageAmount")) }, { 12, 150 })
        draw(STRONG_SCROLL)
        local n0 = count(p, STRONG_SCROLL)
        local s = fire("saved scroll")
        T.eq("fresh process: casting the saved scroll takes its mana and one scroll", { s.mana, n0, count(p, STRONG_SCROLL) }, { 12, 2, 1 })
        T.eq("fresh process: the saved scroll's projectile has double damage", s.damage, 300)
        T.eq("fresh process: a hit takes the doubled damage", s.lost, 300 + 25 * 3 - (s.prot or 0))
        return
    end

    -- ---- phase `save` ------------------------------------------------------------------------------------------------------------------
    T.section("numbers")
    local names = risen.spell.names()
    T.expect("the catalogue lists the spells", #names == 21 and names[1] == "Beast" and risen.spell.info("Fireball").items.crystal == "It_Crystal_Fire"
        and risen.spell.info("Skeleton").items.scroll == "It_Scr_Skeleton" and risen.spell.info("Nope") == nil, table.concat(names, ","))
    local fb = spell_get("Fireball")
    T.eq("the shipped Fireball has the edited numbers", { fb.items.crystal.mana_cost, fb.projectiles[1].damage, fb.projectiles[2].damage, fb.projectiles[3].damage, fb.items.crystal.requires },
        { 12, 150, 150, 150, { Fireball = 1 } })
    local fr = spell_get("Frost")
    T.eq("relative settings are computed from the shipped values", { fr.projectiles[1].damage, fr.projectiles[1].speed, fr.items.crystal.mana_cost }, { 100, 22.5, 10 })
    local li, sk = spell_get("Light"), spell_get("Skeleton")
    T.eq("rune spells: cost of rune and scroll", { li.items.rune.mana_cost, li.items.scroll.mana_cost, li.items.orb.mana_cost }, { 7, 3, 0 })
    T.eq("mana_mult scales rune and scroll, the orb stays free", { sk.items.rune.mana_cost, sk.items.scroll.mana_cost, sk.items.orb.mana_cost }, { 20, 10, 0 })
    local ns = spell_get("Fireball")
    local strong, strong_scroll = tpl(STRONG), tpl(STRONG_SCROLL)
    T.eq("the new items are clones with the numbers of the spec", {
        tonumber(strong:get_property("gCCastInfo_PS", "ManaCost")), strong:get_property("gCCastInfo_PS", "ConsumeItem"), strong:get_property("gCItem_PS", "UseType"),
        tonumber(strong_scroll:get_property("gCCastInfo_PS", "ManaCost")), strong_scroll:get_property("gCCastInfo_PS", "ConsumeItem"),
        tonumber(tpl(CHEAP):get_property("gCCastInfo_PS", "ManaCost")), tonumber(tpl(CHEAP_SCROLL):get_property("gCCastInfo_PS", "ManaCost")),
        tpl(CHEAP_ORB):get_property("gCCastInfo_PS", "ConsumeItem") },
        { 24, "False", "gEItemUseType_MagicFireball", 12, "True", 15, 7, "True" })
    local n1, d1 = labels_ok(STRONG)
    local n2 = labels_ok(STRONG_SCROLL)
    T.eq("the labels of the new spell", { n1, d1, n2, (labels_ok(CHEAP)), (labels_ok(CHEAP_ORB)) },
        { "Strong Fireball", "A fireball that hits twice as hard.", "Strong Fireball (Scroll)", "Cheap Skeleton", "Cheap Skeleton (Orb)" })
    T.eq("requirements of the new spell", { risen.item.requirements(STRONG), risen.item.requirements(STRONG_SCROLL), risen.item.requirements(CHEAP), risen.item.requirements("It_Crystal_Fire") },
        { { Fireball = 2 }, {}, { Circle = 2 }, { Fireball = 1 } })
    T.eq("the other spells are untouched", { spell_get("Missile").items.crystal.mana_cost, spell_get("Missile").projectiles[1].damage,
        spell_get("Inferno").items.rune.mana_cost, spell_get("Inferno").items.scroll.mana_cost }, { 5, 50, 80, 40 })

    -- the engine's own equip check (what a player sees as "missing skill")
    prepare_caster(1, 3)
    local low = risen.item.can_use(p, STRONG)
    prepare_caster(3, 3)
    T.eq("the engine's equip check follows the requirement", { low, risen.item.can_use(p, STRONG), risen.item.can_use(p, STRONG_SCROLL) }, { false, true, true })

    T.section("real casts")
    local px, py, pz = p:position()
    spawn_victim()
    for _, item in ipairs({ "It_Crystal_Fire", STRONG, STRONG_SCROLL }) do risen.item.give(p, item, item == STRONG_SCROLL and 2 or 1) end
    risen.item.give(p, "It_Ru_Light", 1); risen.item.give(p, "It_Scr_Light", 2); risen.item.give(p, "It_Ru_Skeleton", 1); risen.item.give(p, "It_Scr_Skeleton", 2)
    risen.item.give(p, CHEAP, 1); risen.item.give(p, CHEAP_SCROLL, 3); risen.item.give(p, CHEAP_ORB, 2)

    -- 1. the shipped crystal with the edited numbers
    draw("It_Crystal_Fire")
    local base = fire("shipped crystal")
    T.eq("cast: the shipped crystal takes the edited mana cost", { base.mana, base.template }, { 12, "It_Projectile_Fireball_S0" })
    T.eq("cast: the projectile carries the edited damage", base.damage, 150)
    T.eq("cast: a hit takes damage + 25 * Fireball - protection", base.lost, 150 + 25 * 3 - (base.prot or 0))
    T.expect("cast: the equipped slot holds the crystal", risen.spell.equipped(p) == "It_Crystal_Fire", tostring(risen.spell.equipped(p)))

    -- 2. the new crystal: its own cost, per-shot double damage
    draw(STRONG)
    local strong_shot = fire("strong crystal")
    T.eq("cast: the new crystal takes its own mana cost", { strong_shot.mana, risen.spell.equipped(p) }, { 24, STRONG })
    T.eq("cast: the new crystal's projectile has double damage", { strong_shot.damage, strong_shot.speed, strong_shot.radius }, { 300, 30, 15 })
    T.eq("cast: a hit of the new crystal takes the doubled damage", strong_shot.lost, 300 + 25 * 3 - (strong_shot.prot or 0))
    local seen_magic = false
    for _, e in ipairs(equipped_events) do if e == STRONG .. "@11" then seen_magic = true end end
    T.expect("cast: item.equipped reports the magic slot", seen_magic, table.concat(equipped_events, ","))

    -- 3. back to the plain crystal: nothing of the strong shot sticks to it
    draw("It_Crystal_Fire")
    local again = fire("plain crystal again")
    T.eq("cast: the next shot of the plain crystal is not affected", { again.mana, again.damage, again.speed, again.radius, again.lost }, { 12, 150, 15, 10, 150 + 75 - (again.prot or 0) })

    -- 4. the scroll: half the cost, used up
    draw(STRONG_SCROLL)
    local before = count(p, STRONG_SCROLL)
    local sc = fire("strong scroll")
    T.eq("cast: the scroll takes half the mana and is used up", { sc.mana, before, count(p, STRONG_SCROLL) }, { 12, 2, 1 })
    T.eq("cast: the scroll's projectile has double damage", { sc.damage, sc.speed, sc.lost }, { 300, 30, 300 + 75 - (sc.prot or 0) })
    local sc2 = fire("strong scroll 2")
    -- the engine unequips a used-up scroll from the rune slot only: the layer takes the last scroll of a projectile spell away once its shot is over
    T.wait_until(function() return count(p, STRONG_SCROLL) == 0 end, 30, "the last scroll is taken away")
    T.eq("cast: the used-up scroll leaves the stack", { sc2.mana, count(p, STRONG_SCROLL), tostring(risen.spell.equipped(p)) }, { 12, 0, "nil" })
    p:set_state("PS_Normal")
    T.sleep(60)

    T.section("rune spells")
    local function mana_of(item_fn)
        local m0 = p:mana()
        local ok, why = item_fn()
        return m0 - p:mana(), ok, why
    end
    p:set_mana(5)
    local c_rune, c_scroll = risen.spell.can_cast(p, "It_Ru_Light"), risen.spell.can_cast(p, "It_Scr_Light")
    p:set_mana(400)
    T.eq("rune: the engine's mana check follows the edited cost", { c_rune, c_scroll, risen.spell.can_cast(p, "It_Ru_Light") }, { false, true, true })
    local d1m = mana_of(function() return risen.spell.consume(p, "It_Ru_Light") end)
    T.eq("rune: casting takes exactly the cost, the rune stays", { d1m, count(p, "It_Ru_Light") }, { 7, 1 })
    local d2m = mana_of(function() return risen.spell.consume(p, "It_Scr_Light") end)
    T.eq("scroll: casting takes the scroll cost and one scroll", { d2m, count(p, "It_Scr_Light") }, { 3, 1 })
    T.eq("mana_mult: casting takes the scaled cost", { (mana_of(function() return risen.spell.consume(p, "It_Ru_Skeleton") end)), (mana_of(function() return risen.spell.consume(p, "It_Scr_Skeleton") end)),
        count(p, "It_Scr_Skeleton") }, { 20, 10, 1 })
    T.eq("new rune: casting takes the new cost", { (mana_of(function() return risen.spell.consume(p, CHEAP) end)), count(p, CHEAP) }, { 15, 1 })
    T.eq("new scroll: casting takes the new cost and one scroll", { (mana_of(function() return risen.spell.consume(p, CHEAP_SCROLL) end)), count(p, CHEAP_SCROLL) }, { 7, 2 })
    T.eq("new orb: free and used up", { (mana_of(function() return risen.spell.consume(p, CHEAP_ORB) end)), count(p, CHEAP_ORB) }, { 0, 1 })

    T.section("idempotence and refusals")
    local rep = assert(risen.balance.apply(SPEC, { instances = true }))
    local fb2, fr2, sk2 = spell_get("Fireball"), spell_get("Frost"), spell_get("Skeleton")
    T.eq("applying again does not compound", { rep.ok, fb2.items.crystal.mana_cost, fb2.projectiles[1].damage, fr2.projectiles[1].damage, fr2.projectiles[1].speed, sk2.items.rune.mana_cost,
        tonumber(tpl(STRONG):get_property("gCCastInfo_PS", "ManaCost")) }, { true, 12, 150, 100, 22.5, 20, 24 })
    local function bad(spec, needle)
        local ok, why = risen.balance.apply(spec)
        if ok ~= nil or not tostring(why):find(needle, 1, true) then L("refusal check failed", needle, "->", tostring(why)) return false end
        return true
    end
    local refused = {
        bad({ spells = { Fireballs = { mana_cost = 1 } } }, "spells.Fireballs: unknown spell (valid: Beast"),
        bad({ spells = { Fireball = { mana_cots = 1 } } }, "spells.Fireball: unknown setting 'mana_cots' (valid: consume, damage"),
        bad({ spells = { Light = { damage = 5 } } }, "spells.Light: damage only applies to projectile spells"),
        bad({ spells = { Fireball = { scroll_mana_cost = 5 } } }, "spells.Fireball: scroll_mana_cost only applies to rune spells"),
        bad({ spells = { Fireball = { damage_type = "Acid" } } }, "spells.Fireball: damage_type: unknown value 'Acid' (valid: None, Blunt"),
        bad({ spells = { Fireball = { damage = 10, damage_mult = 2 } } }, "damage and damage_mult exclude each other"),
        bad({ spells = { Fireball = { damage = { 1, 2 } } } }, "damage: expected 3 values"),
        bad({ spells = { Fireball = { mana_cost = -4 } } }, "mana_cost must be a non-negative integer"),
        bad({ new_spells = { Spass_X = { name = "x" } } }, "'base'"),
        bad({ new_spells = { Spass_X = { base = "Fireball", forms = { "orb" } } } }, "form 'orb' does not exist for projectile spells"),
        bad({ new_spells = { Spass_X = { base = "Light", damage = 3 } } }, "damage only applies to projectile spells"),
        bad({ new_spells = { Spass_X = { base = "Fireball", bogus = 1 } } }, "new_spells.Spass_X: unknown setting 'bogus'"),
        bad({ new_spells = { ["Bad Name"] = { base = "Fireball" } } }, "the template name may only contain letters"),
        bad({ spells = { Fireball = { mana_cost = 1 } }, new_spells = { Spass_Y = { base = "Nope" } } }, "unknown spell 'Nope'"),
    }
    local all = true
    for _, ok in ipairs(refused) do all = all and ok end
    T.expect("invalid specs are refused with a reason", all and #refused == 14, "see the log for the failed refusal")
    T.eq("a refused spec writes nothing", { spell_get("Fireball").items.crystal.mana_cost, risen.template.exists("Spass_X"), risen.template.exists("Spass_Y") }, { 12, false, false })

    -- leave the new items in the inventory and save
    T.section("save")
    T.eq("items of the new spells are in the inventory before the save", { risen.item.give(p, STRONG_SCROLL, 2) >= 0, count(p, STRONG), count(p, STRONG_SCROLL), count(p, CHEAP), count(p, CHEAP_SCROLL), count(p, CHEAP_ORB) },
        { true, 1, 2, 1, 2, 1 })
    risen.item.give(p, CHEAP_SCROLL, 1); risen.item.give(p, CHEAP_ORB, 1)
    local saved = false
    risen.on("save.after", function(_, ok) saved = ok end)
    p:set_state("PS_Normal")
    T.sleep(60)
    L("quick_save", risen.world.quick_save())
    T.wait_until(function() return saved end, 120, "save.after")
    T.sleep(200)   -- the engine keeps writing after save.after: do not end the run right away
end, "spells")
