-- NPC and monster balancing with risen.balance (docs/LUA_API.md "Balancing NPCs", docs/RE.md section 24).
--
-- An NPC's numbers live in its template: hit points, mana, level, attributes, combat skills and protection in the (non-reflected) gCSkills_PS, guild,
-- species, attitude, bravery in gCNPC_PS, the starting inventory in gCInventory_PS. risen.balance.install writes the table below into the templates on the
-- first `tick` (new spawns) and again, now also into the live NPCs of each template, on every `session.start` (a world keeps its own copies).
-- Relative settings (hp_mult, level_add, damage_mult, ...) are computed from the SHIPPED values, so the repeated application does not compound.
--
-- Every verification below is a risen.test check (scripts/smoke-test.ps1 -Scenario npcs collects them; outside a test run they only log);
-- the `balance_npcs:` lines stay as the narrative.
local log = risen.log
local T = risen.test
local function L(...)
    local t = {}
    for i = 1, select("#", ...) do t[#t + 1] = tostring((select(i, ...))) end
    log.info("balance_npcs: " .. table.concat(t, " "))
end

-- the numbers of an NPC (risen.npc.stats) that the checks compare
local function fields(s)
    return { hp = s.hp, hp_max = s.hp_max, level = s.level, str = s.skills.Strength, prot_edge = s.skills.ProtEdge, xp = s.xp_reward, guild = s.guild, bravery = s.bravery }
end
local function pick(t, ...)
    local r = {}
    for _, k in ipairs({ ... }) do r[k] = t[k] end
    return r
end

-- LuaLS: these calls return nil on failure; a balancing demo wants to stop there
local function stats(x, o) return assert(risen.npc.stats(x, o)) end
local function spawn(name, x, y, z) return assert(risen.entity.spawn(name, x, y, z)) end
local function tpl_of(name) return assert(risen.template.get(name)) end

local SPEC = {
    npcs = {
        -- absolute values (Strength 30 -> 90 triples the bite: the wolf's fists have no damage of their own, damage = Strength; xp = 200 -> level 20)
        Wolf = { hp_max = 500, xp = 200, skills = { Strength = 90, ProtEdge = 60 }, bravery = "Brave" },
        -- relative values
        Boar = { hp_mult = 2, level_add = 2 },
        -- humans: guild, extra inventory (spawns carry it), attributes
        Peasant_1_01 = { guild = "Cit", hp_max = 300, skills = { Strength = 50, Sword = 3 }, loot = { It_Apple = 3 } },
        -- anything else by name: properties = { PropertySet = { Property = value } }
        Gnome = { properties = { gCNPC_PS = { RoleDescription = "balanced by balance_npcs" } } },
    },
    npc_groups = {
        -- every template whose name matches a Lua pattern, or a template group path ("Monster/Kapitel 2")
        { match = "^Stingrat", hp_mult = 1.5, damage_mult = 2 },
        { group = "Monster/Kapitel 2", level_add = 1 },
    },
    item_groups = {
        { group = "Items/Potions", value_mult = 0.5 },
    },
}

-- shipped values, read before the first apply (this tick handler is registered before install's)
local before = {}
local first_read = false
risen.on("tick", function()
    if first_read then return end
    first_read = true
    for _, n in ipairs({ "Wolf", "Boar", "Stingrat", "Ghul", "Peasant_1_01" }) do
        local s = stats(n)
        before[n] = s
        L("shipped", n, "hp", s.hp_max, "level", s.level, "str", s.skills.Strength, "prot_edge", s.skills.ProtEdge, "guild", s.guild)
    end
    before.potion = tonumber(tpl_of("It_Po_Health_01"):get_property("gCItem_PS", "GoldValue"))
end)

-- on_applied: the report of every application (first tick: templates; session.start: with the live NPCs)
risen.balance.install(SPEC, {
    on_applied = function(when, report)
        if when == "first tick" then
            T.expect("the spec is applied on the first tick", report.ok and #report.applied == 7 and #report.failed == 0,
                ("applied %d, failed %d"):format(#report.applied, #report.failed))
        elseif when == "session.start (new)" then
            T.expect("session.start writes the live NPCs", report.ok and #report.applied == 7 and #report.failed == 0 and report.instances > 0,
                ("applied %d, failed %d, %d live entities written"):format(#report.applied, #report.failed, report.instances))
        end
    end,
})

local function line(name, s)
    return ("%s hp=%d/%d level=%d str=%d prot_edge=%d xp=%d guild=%s bravery=%s"):format(name, s.hp, s.hp_max, s.level, s.skills.Strength,
        s.skills.ProtEdge, s.xp_reward, s.guild, s.bravery)
end

-- histogram of the live instances of a template: "hp_max/level/strength x count"
local function histogram(name)
    local tpl = tpl_of(name)
    local list = tpl:instances(100000) or {}
    local h = {}
    for _, e in ipairs(list) do
        local key = ("%d/%d/%d"):format(e:hp_max(), e:level(), e:skill_total("Strength"))
        h[key] = (h[key] or 0) + 1
    end
    local parts = {}
    for k, n in pairs(h) do parts[#parts + 1] = ("{%s}x%d"):format(k, n) end
    table.sort(parts)
    return #list, table.concat(parts, ",")
end

local verify_in, verified = nil, false
risen.on("session.start", function() if not verified then verify_in = 200 end end)

-- one fight: a wolf and a victim next to each other, far from the player's start; reports the first bite
local fight
local function start_fight(label, weaken_wolf)
    local player = assert(risen.entity.player())
    local x, y, z = player:position()
    x, y, z = x or 0, y or 0, z or 0
    local wolf = spawn("Wolf", x + 3000, y, z)
    if weaken_wolf then
        -- an entity-level edit: this one wolf gets the shipped Strength back
        assert(risen.npc.set_stats(wolf, { skills = { Strength = before.Wolf.skills.Strength } }))
    end
    local victim = spawn("Peasant_1_02", x + 3150, y, z)
    local vs = stats(victim)
    fight = { label = label, wolf = wolf, victim = victim, last = victim:hp(), first_bite = nil, ticks = 0, str = wolf:skill_total("Strength"),
              prot = vs.skills.ProtEdge, start_hp = victim:hp() }
    L("fight " .. label .. ": wolf str", fight.str, "victim", victim:name(), "hp", fight.start_hp, "prot_edge", fight.prot, "wolf guild", wolf:guild(), "victim guild", victim:guild())
end

local step, wait = "idle", 0
local wolf
risen.on("tick", function()
    if step == "idle" then
        if not verify_in then return end
        verify_in = verify_in - 1
        if verify_in > 0 then return end
        verify_in, verified = nil, true
        step, wait = "verify", 0
    end
    local player = risen.entity.player()
    if not player then log.warn("balance_npcs: no player to verify with") step = "done" return end
    local x, y, z = player:position()
    x, y, z = x or 0, y or 0, z or 0

    if step == "verify" then
        -- 1. the templates
        L("template", line("Wolf", stats("Wolf")))
        T.eq("absolute settings reach the template", fields(stats("Wolf")), { hp = 500, hp_max = 500, level = 20, str = 90, prot_edge = 60, xp = 200, guild = "None", bravery = "Brave" })
        L("template", line("Boar", stats("Boar")), "(shipped hp", before.Boar.hp_max, "level", before.Boar.level .. ")")
        local boar_fields = fields(stats("Boar"))
        boar_fields.shipped_hp, boar_fields.shipped_level = before.Boar.hp_max, before.Boar.level
        T.eq("relative settings are computed from the shipped values", boar_fields,
            { hp = 360, hp_max = 360, level = 8, str = 30, prot_edge = 24, xp = 80, guild = "None", bravery = "None", shipped_hp = 180, shipped_level = 6 })
        L("template", line("Stingrat", stats("Stingrat")), "(shipped str", before.Stingrat.skills.Strength .. ")")
        local rat = pick(fields(stats("Stingrat")), "hp", "hp_max", "level", "str")
        rat.shipped_str = before.Stingrat.skills.Strength
        T.eq("a pattern group scales hit points and damage", rat, { hp = 180, hp_max = 180, level = 4, str = 40, shipped_str = 20 })
        L("template", line("Ghul", stats("Ghul")), "(shipped level", before.Ghul.level .. ")")
        local ghul = pick(fields(stats("Ghul")), "hp", "hp_max", "level")
        ghul.shipped_level = before.Ghul.level
        T.eq("a template group path works", ghul, { hp = 390, hp_max = 390, level = 14, shipped_level = 13 })
        L("template", line("Peasant_1_01", stats("Peasant_1_01")))
        T.eq("guild, attributes and loot of a human", pick(fields(stats("Peasant_1_01")), "hp", "hp_max", "level", "str", "prot_edge", "xp", "guild"),
            { hp = 300, hp_max = 300, level = 5, str = 50, prot_edge = 20, xp = 50, guild = "Cit" })
        local loot = risen.npc.template_inventory("Peasant_1_01") or {}
        local first = loot[1]
        L("template loot Peasant_1_01:", first and (first.item .. "x" .. first.amount) or "-")
        T.eq("the template inventory carries the loot", first and (first.item .. "x" .. first.amount) or "-", "It_Applex3")
        L("template Gnome role:", (tpl_of("Gnome"):get_property("gCNPC_PS", "RoleDescription")))
        T.eq("any property by name", tostring((tpl_of("Gnome"):get_property("gCNPC_PS", "RoleDescription"))), "balanced by balance_npcs")
        local potion = tonumber(tpl_of("It_Po_Health_01"):get_property("gCItem_PS", "GoldValue"))
        L("potion value", potion, "shipped", before.potion, "expected", math.floor(before.potion * 0.5 + 0.5))
        T.eq("item groups scale the shipped gold value", { potion, before.potion, math.floor(before.potion * 0.5 + 0.5) }, { 15, 30, 15 })

        -- 2. new spawns (template values) and the experience a kill gives
        wolf = spawn("Wolf", x + 300, y, z)
        L("spawn", line("Wolf", stats(wolf)))
        T.eq("a new spawn has the template numbers", fields(stats(wolf)), { hp = 500, hp_max = 500, level = 20, str = 90, prot_edge = 60, xp = 200, guild = "None", bravery = "Brave" })
        local w_str, w_str_base = wolf:skill_total("Strength")
        L("spawn wolf (entity API) hp", wolf:hp(), "/", wolf:hp_max(), "level", wolf:level(), "str", w_str, w_str_base)
        T.eq("...also through the entity API", { wolf:hp(), wolf:hp_max(), wolf:level(), w_str, w_str_base }, { 500, 500, 20, 90, 90 })
        local xp0 = player:skill_total("Experience")
        local gave = risen.script.call("GiveXP", wolf, player, -1)
        local xp1, xp1_base = player:skill_total("Experience")
        L("GiveXP wolf:", gave, "experience", xp0, "->", xp1, xp1_base)
        T.eq("the engine pays 10 * level for a kill", { gave, xp0, xp1, xp1_base }, { 1, 0, 200, 200 })
        local peasant = spawn("Peasant_1_01", x + 400, y, z)
        local apples = risen.item.count(peasant, "It_Apple")
        L("spawn peasant guild", peasant:guild(), "apples", apples, "hp", peasant:hp_max())
        T.eq("spawns carry the loot", { peasant:guild(), apples, peasant:hp_max() }, { 4, 3, 300 })
        peasant:kill()

        -- 3. the world's NPCs (written by session.start)
        local world_names = { Wolf = { "every world wolf was rewritten", "{500/20/90}" }, Boar = { "every world boar was rewritten", "{360/8/30}" },
                              Stingrat = { "every world stingrat was rewritten", "{180/4/40}" } }
        for _, n in ipairs({ "Wolf", "Boar", "Stingrat" }) do
            local count, h = histogram(n)
            L("world", n, "n=" .. count, h)
            T.expect(world_names[n][1], count >= 1 and h == world_names[n][2] .. "x" .. count, ("n=%d %s"):format(count, h))
        end

        -- 4. applying again must not compound the relative values nor refill a damaged NPC
        local live = tpl_of("Wolf"):instances(100) or {}
        local damaged, healthy = live[1], live[2]
        if damaged and healthy then
            damaged:set_hp(7)
            L("damaged wolf hp", damaged:hp(), "/", damaged:hp_max())
        end
        local rep = risen.balance.apply(SPEC, { instances = true })
        L("second apply:", rep and rep.ok, rep and #rep.applied, "live", rep and rep.instances)
        L("after second apply Boar template hp", stats("Boar").hp_max, "level", stats("Boar").level)
        T.eq("applying again does not compound (template)", { stats("Boar").hp_max, stats("Boar").level }, { 360, 8 })
        local count, h = histogram("Boar")
        L("after second apply world Boar", "n=" .. count, h)
        T.expect("applying again does not compound (world)", count >= 1 and h == "{360/8/30}x" .. count, ("n=%d %s"):format(count, h))
        if damaged and healthy then
            L("damaged wolf after second apply hp", damaged:hp(), "/", damaged:hp_max(), "full wolf", healthy:hp(), "/", healthy:hp_max())
        end
        T.eq("applying again keeps a damaged NPC damaged", { damaged and damaged:hp(), damaged and damaged:hp_max(), healthy and healthy:hp(), healthy and healthy:hp_max() },
            { 7, 500, 500, 500 })

        -- 5. a single entity: set_stats on an entity touches only that entity
        local boar = spawn("Boar", x + 500, y, z)
        local r, why = risen.npc.set_stats(boar, { hp_max = 77, level = 3, skills = { ProtFire = 5 } })
        L("entity set_stats:", r and r.written, why, "boar hp", boar:hp(), "/", boar:hp_max(), "level", boar:level(), "prot_fire", boar:skill_total("ProtFire"),
            "template Boar hp", stats("Boar").hp_max)
        T.eq("set_stats on an entity touches only it", { r and r.written, why, boar:hp(), boar:hp_max(), boar:level(), (boar:skill_total("ProtFire")), stats("Boar").hp_max },
            { 1, nil, 77, 77, 3, 5, 360 })
        boar:kill()
        wolf:kill()

        -- 6. rejected specs write nothing
        local wolf_hp = stats("Wolf").hp_max
        local function bad(label, spec, check, needle)
            local ok, why2 = risen.balance.apply(spec)
            L("bad spec " .. label .. ":", ok == nil, why2)
            T.expect(check, ok == nil and why2 ~= nil and why2:find(needle, 1, true) ~= nil, why2)
        end
        bad("conflict", { npcs = { Wolf = { hp_max = 10, hp_mult = 2 } } }, "conflicting settings are refused", "npcs.Wolf: hp_max and hp_mult exclude each other")
        bad("unknown key", { npcs = { Wolf = { health = 10 } } }, "unknown settings are refused", "npcs.Wolf: unknown setting 'health' (valid: ai_group, attitude")
        bad("unknown skill", { npcs = { Wolf = { skills = { Strenght = 10 } } } }, "unknown skills are refused with the list",
            "npcs.Wolf: skills: unknown skill 'Strenght' (valid: HitPoints")
        bad("range", { npcs = { Wolf = { skills = { Strength = 999 } } } }, "out-of-range values are refused", "npcs.Wolf: Strength: 999 is outside the allowed range 0..200")
        bad("not an NPC", { npcs = { It_1H_Sword = { hp_max = 10 } } }, "non-NPC templates are refused", "npcs.It_1H_Sword: not an NPC template")
        bad("unknown guild", { npcs = { Wolf = { hp_max = 10, guild = "Pirates" } } }, "unknown enum names are refused", "npcs.Wolf: guild: unknown value 'Pirates' (valid: None, Don")
        bad("empty group", { npc_groups = { { match = "^NoSuchNpc", hp_mult = 2 } } }, "an empty group is refused", "npc_groups[1]: matches no NPC template")
        L("wolf template hp after the rejected specs", stats("Wolf").hp_max, "(was", wolf_hp .. ")")
        T.eq("rejected specs write nothing", { stats("Wolf").hp_max, wolf_hp }, { 500, 500 })

        -- 7. combat, first round: a wolf with the shipped Strength
        start_fight("shipped", true)
        step, wait = "fight1", 0
    elseif step == "fight1" or step == "fight2" then
        -- watch the victim's hit points: the first drop is one bite = Strength + fist damage (0) - the victim's protection
        wait = wait + 1
        local hp = fight.victim:hp()
        if hp ~= nil and hp < fight.last and not fight.first_bite then fight.first_bite = fight.last - hp end
        fight.last = hp or fight.last
        if fight.first_bite or wait >= 1500 then
            L(("fight %s: first bite %s (wolf strength %d - victim ProtEdge %d = %d) victim hp %s -> %s"):format(fight.label, tostring(fight.first_bite),
                fight.str, fight.prot, fight.str - fight.prot, fight.start_hp, tostring(hp)))
            T.eq(fight.label == "shipped" and "combat: the shipped wolf bites for Strength - protection" or "combat: the balanced wolf bites harder",
                { fight.first_bite, fight.str, fight.prot }, fight.label == "shipped" and { 10, 30, 20 } or { 70, 90, 20 })
            fight.wolf:kill()
            fight.victim:kill()
            if step == "fight1" then
                start_fight("balanced", false)
                step, wait = "fight2", 0
            else
                step, wait = "later", 0
                wolf = spawn("Wolf", x + 300, y, z)
            end
        end
    elseif step == "later" then
        wait = wait + 1
        if wait == 300 then
            -- the spawn init scripts / AI ticks did not undo the template edit
            L("later spawn wolf hp", wolf:hp(), "/", wolf:hp_max(), "level", wolf:level(), "str", wolf:skill_total("Strength"), "state", wolf:state())
            local later_str = wolf:skill_total("Strength")
            T.expect("the edit survives spawn init and AI ticks", wolf:hp() == 500 and wolf:hp_max() == 500 and wolf:level() == 20 and later_str == 90
                and tostring(wolf:state()):find("^ZS_") ~= nil,
                ("hp %s / %s level %s str %s state %s"):format(tostring(wolf:hp()), tostring(wolf:hp_max()), tostring(wolf:level()), tostring(later_str), tostring(wolf:state())))
            step = "done"
            L("verify done")
            risen.gamevar.set("SPASS_BALANCE_NPCS_DONE", 1)   -- lets balance_npcs_driver start (scripts/smoke-test.ps1 runs both mods)
        end
    end
end)
