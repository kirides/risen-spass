-- NPC and monster balancing with risen.balance (docs/LUA_API.md "Balancing NPCs", docs/RE.md section 24).
--
-- An NPC's numbers live in its template: hit points, mana, level, attributes, combat skills and protection in the (non-reflected) gCSkills_PS, guild,
-- species, attitude, bravery in gCNPC_PS, the starting inventory in gCInventory_PS. risen.balance.install writes the table below into the templates on the
-- first `tick` (new spawns) and again, now also into the live NPCs of each template, on every `session.start` (a world keeps its own copies).
-- Relative settings (hp_mult, level_add, damage_mult, ...) are computed from the SHIPPED values, so the repeated application does not compound.
--
-- Every `balance_npcs:` line is checked by scripts/smoke-test.ps1.
local log = risen.log
local function L(...)
    local t = {}
    for i = 1, select("#", ...) do t[#t + 1] = tostring((select(i, ...))) end
    log.info("balance_npcs: " .. table.concat(t, " "))
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

risen.balance.install(SPEC)

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
        L("template", line("Boar", stats("Boar")), "(shipped hp", before.Boar.hp_max, "level", before.Boar.level .. ")")
        L("template", line("Stingrat", stats("Stingrat")), "(shipped str", before.Stingrat.skills.Strength .. ")")
        L("template", line("Ghul", stats("Ghul")), "(shipped level", before.Ghul.level .. ")")
        L("template", line("Peasant_1_01", stats("Peasant_1_01")))
        local loot = risen.npc.template_inventory("Peasant_1_01") or {}
        local first = loot[1]
        L("template loot Peasant_1_01:", first and (first.item .. "x" .. first.amount) or "-")
        L("template Gnome role:", (tpl_of("Gnome"):get_property("gCNPC_PS", "RoleDescription")))
        local potion = tonumber(tpl_of("It_Po_Health_01"):get_property("gCItem_PS", "GoldValue"))
        L("potion value", potion, "shipped", before.potion, "expected", math.floor(before.potion * 0.5 + 0.5))

        -- 2. new spawns (template values) and the experience a kill gives
        wolf = spawn("Wolf", x + 300, y, z)
        L("spawn", line("Wolf", stats(wolf)))
        L("spawn wolf (entity API) hp", wolf:hp(), "/", wolf:hp_max(), "level", wolf:level(), "str", wolf:skill_total("Strength"))
        local xp0 = player:skill_total("Experience")
        L("GiveXP wolf:", risen.script.call("GiveXP", wolf, player, -1), "experience", xp0, "->", player:skill_total("Experience"))
        local peasant = spawn("Peasant_1_01", x + 400, y, z)
        L("spawn peasant guild", peasant:guild(), "apples", risen.item.count(peasant, "It_Apple"), "hp", peasant:hp_max())
        peasant:kill()

        -- 3. the world's NPCs (written by session.start)
        for _, n in ipairs({ "Wolf", "Boar", "Stingrat" }) do
            local count, h = histogram(n)
            L("world", n, "n=" .. count, h)
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
        local count, h = histogram("Boar")
        L("after second apply world Boar", "n=" .. count, h)
        if damaged and healthy then
            L("damaged wolf after second apply hp", damaged:hp(), "/", damaged:hp_max(), "full wolf", healthy:hp(), "/", healthy:hp_max())
        end

        -- 5. a single entity: set_stats on an entity touches only that entity
        local boar = spawn("Boar", x + 500, y, z)
        local r, why = risen.npc.set_stats(boar, { hp_max = 77, level = 3, skills = { ProtFire = 5 } })
        L("entity set_stats:", r and r.written, why, "boar hp", boar:hp(), "/", boar:hp_max(), "level", boar:level(), "prot_fire", boar:skill_total("ProtFire"),
            "template Boar hp", stats("Boar").hp_max)
        boar:kill()
        wolf:kill()

        -- 6. rejected specs write nothing
        local wolf_hp = stats("Wolf").hp_max
        local function bad(label, spec)
            local ok, why2 = risen.balance.apply(spec)
            L("bad spec " .. label .. ":", ok == nil, why2)
        end
        bad("conflict", { npcs = { Wolf = { hp_max = 10, hp_mult = 2 } } })
        bad("unknown key", { npcs = { Wolf = { health = 10 } } })
        bad("unknown skill", { npcs = { Wolf = { skills = { Strenght = 10 } } } })
        bad("range", { npcs = { Wolf = { skills = { Strength = 999 } } } })
        bad("not an NPC", { npcs = { It_1H_Sword = { hp_max = 10 } } })
        bad("unknown guild", { npcs = { Wolf = { hp_max = 10, guild = "Pirates" } } })
        bad("empty group", { npc_groups = { { match = "^NoSuchNpc", hp_mult = 2 } } })
        L("wolf template hp after the rejected specs", stats("Wolf").hp_max, "(was", wolf_hp .. ")")

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
            step = "done"
            L("verify done")
            risen.gamevar.set("SPASS_BALANCE_NPCS_DONE", 1)   -- lets balance_npcs_driver start (scripts/smoke-test.ps1 runs both mods)
        end
    end
end)
