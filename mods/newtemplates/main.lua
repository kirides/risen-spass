-- New item and NPC templates created at runtime (docs/LUA_API.md "New templates", docs/RE.md section 25).
--
-- risen.template.create(name, base, overrides) clones an existing template under a new unique name. The clone is a full, independent copy
-- (property sets, mesh, icon, skills, inventory) with the deterministic ID of its name, so savegames that contain items of it resolve again
-- as long as the mod creates it again in every process. risen.balance.install does that declaratively: the `new_items` / `new_npcs` sections
-- below are applied on the first `tick` and on every `session.start` (idempotent: an existing clone only gets its overrides again).
--
-- Texts: the label of an item/NPC is the localization id FO_<name>, an item's description ITEMDESC_<name>; `name` / `description` set them
-- (a string for every language or { English = "...", German = "..." }; a mod's strings/*.csv works the same way).
--
-- Every verification below is a risen.test check (scripts/smoke-test.ps1 -Scenario newtemplates collects them; outside a test run they only
-- log); the `newtemplates:` lines stay as the narrative. -Param phase=... selects what the scenario plays (see newtemplates_driver).
local log = risen.log
local T = risen.test
local phase = T.param("phase", "save")
local function deps_of(e) return e and e:dependencies() or {} end

local SPEC = {
    new_items = {
        -- a sword with its own numbers, texts, and the mesh and icon of another weapon
        Spass_Flame_Sword = {
            base = "It_1H_Sword",
            name = { English = "Flame Tongue", German = "Flammenzunge" },
            description = "A sword that feels warm to the touch.",
            value = 900, damage = 55,
            requires = { Strength = 25 }, modifies = { Strength = 5 },
            mesh = "It_Wpn_1H_Club.xmsh", icon = "ic_it_1h_club",
        },
        -- a berry that heals more (only the numbers change; the base's label is inherited unless `name` is given)
        Spass_Healing_Berry = { base = "It_Plant_Berry", value = 40, modifies = { HitPoints = 120 }, name = "Healing Berry" },
    },
    new_npcs = {
        -- a stronger wolf that carries an apple and the new sword
        Spass_Dire_Wolf = {
            base = "Wolf", name = "Dire Wolf",
            hp_max = 600, level = 15, skills = { Strength = 80, ProtEdge = 40 },
            loot = { It_Apple = 2, Spass_Flame_Sword = 1 },
        },
        -- a human clone
        Spass_Bandit_Leader = { base = "Peasant_1_01", name = "Bandit Leader", hp_max = 320, level = 12, skills = { Strength = 55 }, guild = "Cit" },
        -- a wolf with the boar's body
        Spass_Wolfboar = { base = "Wolf", name = "Wolfboar", mesh = "Ani_Boar_Monster_Boar.xac", hp_max = 250 },
    },
}

-- phases of the fresh-process runs: "load_missing" plays a session without the mod's templates (what happens to saved items then?),
-- "load_late" creates them only after the savegame was read; every other phase creates them on the first tick, before anything is loaded
local function install()
    risen.balance.install(SPEC, {
        on_applied = function(when, report)
            if when == "first tick" then
                T.expect("the spec is applied on the first tick", report.ok and #report.applied == 5 and #report.failed == 0,
                    ("applied %d, failed %d"):format(#report.applied, #report.failed))
            elseif when:find("^session.start") then
                T.expect("session.start applies it again (idempotent)", report.ok and #report.applied == 5 and #report.failed == 0,
                    ("applied %d, failed %d"):format(#report.applied, #report.failed))
            end
        end,
    })
end
if phase == "load_late" then
    local done = false
    risen.on("session.start", function() if not done then done = true install() end end)
elseif phase ~= "load_missing" then
    install()
end
if phase ~= "save" then return end   -- the verification below belongs to the first, "save" run

local function L(...)
    local t = {}
    for i = 1, select("#", ...) do t[#t + 1] = tostring((select(i, ...))) end
    log.info("newtemplates: " .. table.concat(t, " "))
end
local function fmt_req(map)
    local parts = {}
    for k, v in pairs(map or {}) do parts[#parts + 1] = k .. "=" .. v end
    table.sort(parts)
    return table.concat(parts, ",")
end
local function fmt_mods(list)
    local parts = {}
    for _, m in ipairs(list or {}) do parts[#parts + 1] = ("%s%+d"):format(m.skill, m.amount) end
    return table.concat(parts, ",")
end
local function same_sets(a, b)
    local x, y = table.concat(a:property_sets() or {}, ","), table.concat(b:property_sets() or {}, ",")
    return x == y, x
end

local verify_in = nil
local verified = false
risen.on("session.start", function()
    if not verified then verify_in = 200 end   -- frames; let the world settle first
end)

risen.on("tick", function()
    if not verify_in then return end
    verify_in = verify_in - 1
    if verify_in > 0 then return end
    verify_in = nil
    verified = true

    local player = risen.entity.player()
    if not player then log.warn("newtemplates: no player to verify with") return end
    local x, y, z = player:position()
    x, y, z = x or 0, y or 0, z or 0

    -- 1. what exists, and that it is a copy of the base
    local sword = assert(risen.template.get("Spass_Flame_Sword"))
    local base = assert(risen.template.get("It_1H_Sword"))
    local created = risen.template.created()
    L("created", created.Spass_Flame_Sword, created.Spass_Healing_Berry, created.Spass_Dire_Wolf, created.Spass_Bandit_Leader, created.Spass_Wolfboar)
    T.expect("every template of the spec was created", created.Spass_Flame_Sword == "It_1H_Sword" and created.Spass_Healing_Berry == "It_Plant_Berry"
        and created.Spass_Dire_Wolf == "Wolf" and created.Spass_Bandit_Leader == "Peasant_1_01" and created.Spass_Wolfboar == "Wolf", "risen.template.created()")
    local same, sets = same_sets(sword, base)
    T.expect("the clone has the property sets of its base", same, sets)
    T.eq("the clone has the ID derived from its name", sword:id(), risen.template.id_for("Spass_Flame_Sword"))
    T.expect("the clone is a different template than its base", sword:id() ~= base:id() and sword ~= base, tostring(sword:id()) .. " vs " .. tostring(base:id()))
    L("sword id", sword:id(), "base id", base:id(), "info", sword:info().created, sword:info().base)

    -- 2. the overrides read back; the base is untouched
    local function numbers(name)
        local t = assert(risen.template.get(name))
        return { fmt_req(risen.item.requirements(name)), tostring(t:get_property("gCItem_PS", "GoldValue")),
                 tostring(t:get_property("gCDamage_PS", "DamageAmount")), fmt_mods(risen.item.modifiers(name)) }
    end
    local clone_numbers, base_numbers = numbers("Spass_Flame_Sword"), numbers("It_1H_Sword")
    L("sword numbers", table.concat(clone_numbers, " | "), "base", table.concat(base_numbers, " | "))
    T.eq("item overrides read back", clone_numbers, { "Strength=25", "900", "55", "Strength+5" })
    T.expect("the base template is untouched", base_numbers[2] == "50" and base_numbers[1] ~= "Strength=25", table.concat(base_numbers, " | "))
    local berry = numbers("Spass_Healing_Berry")
    T.eq("a second item clone has its own numbers", { berry[2], berry[4] }, { "40", "HitPoints+120" })

    -- 3. labels and description as the engine shows them
    local german = risen.loc.language() == "German"
    local focus, desc = sword:focus_name(), sword:description()
    L("sword label", focus, "description", desc, "| berry label", risen.template.get("Spass_Healing_Berry"):focus_name())
    T.eq("the label comes from FO_<name>", focus, german and "Flammenzunge" or "Flame Tongue")
    T.eq("the description comes from ITEMDESC_<name>", desc, "A sword that feels warm to the touch.")
    T.eq("an NPC clone has its label too", risen.template.get("Spass_Dire_Wolf"):focus_name(), "Dire Wolf")

    -- 4. spawn, give, count; mesh and icon as the engine really loaded them
    local spawned = risen.entity.spawn("Spass_Flame_Sword", x + 100, y + 10, z)
    local club = risen.entity.spawn("It_1H_Club", x + 130, y + 10, z)
    local plain = risen.entity.spawn("It_1H_Sword", x + 160, y + 10, z)
    local deps, club_deps, plain_deps = deps_of(spawned), deps_of(club), deps_of(plain)
    L("spawn", spawned, "value", spawned and spawned:item_value(), "icon", spawned and spawned:item_icon(), "deps", table.concat(deps, ";"),
        "| club deps", table.concat(club_deps, ";"), "| sword deps", table.concat(plain_deps, ";"))
    T.expect("a spawn uses the new numbers", spawned ~= nil and spawned:item_value() == 900 and spawned:get_property("gCDamage_PS", "DamageAmount") == "55"
        and fmt_req(spawned:requirements()) == "Strength=25", tostring(spawned and spawned:item_value()))
    T.expect("the spawn is created from the clone", spawned ~= nil and tostring(spawned:template()) == "Template(Spass_Flame_Sword)", tostring(spawned and spawned:template()))
    T.expect("the mesh override reaches the engine's resource list", #deps == 1 and #club_deps == 1 and deps[1] == club_deps[1] and deps[1] ~= plain_deps[1],
        table.concat(deps, ";") .. " / club " .. table.concat(club_deps, ";") .. " / sword " .. table.concat(plain_deps, ";"))
    T.eq("the icon override is read by the engine", spawned and spawned:item_icon(), "ic_it_1h_club")
    T.eq("the spawn's label is the clone's", spawned and spawned:focus_name(), german and "Flammenzunge" or "Flame Tongue")

    local given = risen.item.give(player, "Spass_Flame_Sword", 2)
    T.expect("an item of the clone can be given", given ~= nil and given >= 0 and risen.item.count(player, "Spass_Flame_Sword") == 2,
        "count " .. tostring(risen.item.count(player, "Spass_Flame_Sword")))
    local stack_ok, stack_item = false, nil
    for i = 0, player:stack_count() - 1 do
        local it = player:stack_item(i)
        if it and it:name() == "Spass_Flame_Sword" then stack_item = it stack_ok = it:item_value() == 900 and fmt_mods(it:modifiers()) == "Strength+5" end
    end
    T.expect("the inventory stack is the clone template", stack_ok, stack_item and tostring(stack_item:item_value()) or "no stack")

    -- 5. the engine's equip check against the clone's requirement; creating again re-applies the overrides
    local str = player:skill_total("Strength")
    local can = risen.item.can_use(player, "Spass_Flame_Sword")
    L("player strength", str, "can_use (needs 25):", can)
    T.eq("the engine check follows the clone's requirement", can, str >= 25)
    local t2, created_again = risen.template.create("Spass_Flame_Sword", "It_1H_Sword", { requires = { Strength = 500 } })
    T.expect("create again re-applies the overrides to the existing clone", t2 ~= nil and created_again == false and risen.item.can_use(player, "Spass_Flame_Sword") == false
        and fmt_req(risen.item.requirements("Spass_Flame_Sword")) == "Strength=500", tostring(created_again))
    risen.item.set_requirements("Spass_Flame_Sword", { Strength = 1 })
    T.eq("a lowered requirement passes the check", risen.item.can_use(player, "Spass_Flame_Sword"), true)
    risen.item.set_requirements("Spass_Flame_Sword", { Strength = 25 })

    -- 6. NPC clones
    local wolf0 = assert(risen.npc.stats("Wolf"))
    local dire = risen.npc.spawn("Spass_Dire_Wolf", x + 2500, y + 10, z, {})
    local s = dire and risen.npc.stats(dire) or {}
    L("dire wolf", dire, "hp_max", s.hp_max, "level", s.level, "Strength", s.skills and s.skills.Strength, "ProtEdge", s.skills and s.skills.ProtEdge,
        "| wolf", wolf0.hp_max, wolf0.level, wolf0.skills.Strength)
    T.eq("an NPC clone spawns with its own stats", { s.hp_max, s.level, s.skills and s.skills.Strength, s.skills and s.skills.ProtEdge }, { 600, 15, 80, 40 })
    T.eq("the base NPC is untouched", { wolf0.hp_max, wolf0.level, wolf0.skills.Strength }, { 180, 6, 30 })
    T.expect("the clone keeps the species and carries its loot", dire ~= nil and s.species == wolf0.species and risen.item.count(dire, "It_Apple") == 2
        and risen.item.count(dire, "Spass_Flame_Sword") == 1, ("species %s apples %s swords %s"):format(tostring(s.species), tostring(dire and risen.item.count(dire, "It_Apple")),
        tostring(dire and risen.item.count(dire, "Spass_Flame_Sword"))))
    local boar = risen.npc.spawn("Spass_Wolfboar", x + 2600, y + 10, z, {})
    local real_wolf = risen.npc.spawn("Wolf", x + 2700, y + 10, z, {})
    local real_boar = risen.npc.spawn("Boar", x + 2800, y + 10, z, {})
    local wb = deps_of(boar)
    L("wolfboar", boar, "deps", table.concat(wb, ";"), "| boar", table.concat(deps_of(real_boar), ";"), "| wolf", table.concat(deps_of(real_wolf), ";"))
    T.expect("an NPC clone with another body loads that actor", boar ~= nil and real_boar ~= nil and real_wolf ~= nil and #wb == 1 and wb[1] == deps_of(real_boar)[1]
        and wb[1] ~= deps_of(real_wolf)[1], table.concat(wb, ";"))
    local leader = risen.npc.spawn("Spass_Bandit_Leader", x + 2900, y + 10, z, { peaceful = true })
    local ls = leader and risen.npc.stats(leader) or {}
    T.eq("a human clone has its own stats", { ls.hp_max, ls.level, ls.skills and ls.skills.Strength, ls.guild }, { 320, 12, 55, "Cit" })
    for _, e in ipairs({ dire, boar, real_wolf, real_boar, leader, spawned, club, plain }) do if e then e:kill() end end

    -- 7. invalid requests are refused and create nothing
    local before = 0
    for _ in pairs(risen.template.created()) do before = before + 1 end
    local function refused(...) local t, why = risen.template.create(...) return t == nil and type(why) == "string" and why or nil end
    local r1 = refused("Spass_Bad_Base", "NoSuchTemplate_XYZ", {})
    local r2 = refused("It_1H_Sword", "It_1H_Club", {})                                   -- a shipped name
    local r3 = refused("Spass Bad Name", "It_1H_Sword", {})                               -- a bad name
    local r4 = refused("Spass_Bad_Override", "It_1H_Sword", { value = "lots" })
    local r5 = refused("Spass_Bad_Override2", "It_1H_Sword", { no_such_setting = 1 })
    local r6 = refused("Spass_Bad_Kind", "Wolf", { value = 5 })                           -- an item setting on an NPC
    local r7 = refused("Spass_Flame_Sword", "It_1H_Club", {})                             -- an existing clone with another base
    local r8 = refused("Spass_Bad_Mesh", "It_1H_Sword", { mesh = "x.xmsh", properties = { eCMesh_PS = { MeshFileName = "y.xmsh" } } })
    local after = 0
    for _ in pairs(risen.template.created()) do after = after + 1 end
    L("refused:", r1, "|", r2, "|", r3, "|", r4, "|", r5, "|", r6, "|", r7, "|", r8)
    T.expect("invalid requests are refused with a reason", r1 and r2 and r3 and r4 and r5 and r6 and r7 and r8 and true, tostring(r1))
    T.expect("a refused request creates nothing", before == after and not risen.template.exists("Spass_Bad_Override") and not risen.template.exists("Spass_Bad_Base"),
        ("%d -> %d"):format(before, after))
    local rb, rw = risen.balance.apply({ new_items = { Spass_Bad_Spec = { base = "It_1H_Sword", value = -3, requires = { Nonsense = 4 } } } })
    T.expect("a bad spec is refused as a whole", rb == nil and not risen.template.exists("Spass_Bad_Spec"), tostring(rw))

    -- 8. removing a clone gives its names back; creating it again brings the same entity back
    local tmp = risen.template.create("Spass_Temp_Clone", "It_1H_Club", { value = 77 })
    local tmp_id = tmp and tmp:id()
    local removed, why_rm = risen.template.remove("Spass_Temp_Clone")
    T.expect("remove gives the name of a clone back", tmp ~= nil and removed == true and not risen.template.exists("Spass_Temp_Clone")
        and risen.template.created().Spass_Temp_Clone == nil, tostring(why_rm))
    local back = risen.template.create("Spass_Temp_Clone", "It_1H_Club", { value = 88 })
    T.expect("create brings a removed clone back with the same ID", back ~= nil and back:id() == tmp_id and back:get_property("gCItem_PS", "GoldValue") == "88",
        tostring(back and back:id()))
    local tmp_item = risen.entity.spawn("Spass_Temp_Clone", x + 100, y + 10, z)
    local refused_rm, why_busy = risen.template.remove("Spass_Temp_Clone")
    T.expect("remove is refused while entities of the clone exist", tmp_item ~= nil and refused_rm == nil and risen.template.exists("Spass_Temp_Clone"), tostring(why_busy))
    local shipped_rm = risen.template.remove("It_1H_Sword")
    T.expect("remove refuses templates this layer did not create", shipped_rm == nil and risen.template.exists("It_1H_Sword"), tostring(shipped_rm))
    if tmp_item then tmp_item:kill() end

    L("DONE")
    risen.gamevar.set("SPASS_NEWTPL_DONE", 1)
end)
