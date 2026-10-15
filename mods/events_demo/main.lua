-- Live verification of the gameplay events (risen.test scenario "events"; docs/LUA_API.md "Events", docs/RE.md section 27).
--
--   .\scripts\test-run.ps1 -Mods events_demo -WaitForResult -Seconds 240 -Scenario events [-Trace events]
--
-- Every route that changes an inventory or kills / creates / destroys an entity is driven once and the events it produced are compared with the
-- exact list expected: a missing event and a duplicate both fail. Outside a test run the mod only logs the events (a development aid).
local T = risen.test

local EVENTS = {
    "world.ready", "inventory.added", "inventory.removed", "inventory.changed", "item.picked_up", "item.equipped", "item.unequipped", "item.consumed",
    "npc.killed", "npc.defeated", "entity.died", "entity.defeated", "entity.spawned", "entity.destroyed", "entity.damaged",
}

local seen = {}   -- every event in order: { ev = name, n = argument count, [1..n] = arguments (entities as their names) }

local function plain(v)
    if type(v) == "userdata" then
        local ok, name = pcall(function() return v:name() end)
        return ok and name or "?"
    end
    return v
end

for _, ev in ipairs(EVENTS) do
    risen.on(ev, function(...)
        local rec = { ev = ev, n = select("#", ...) }
        local parts = {}
        for i = 1, rec.n do
            rec[i] = plain((select(i, ...)))
            parts[#parts + 1] = tostring(rec[i])
        end
        seen[#seen + 1] = rec
        risen.log.info(("EV %s(%s)"):format(ev, table.concat(parts, ", ")))
    end)
end

-- events after index `mark`, of the given names, about one of the templates (argument 2 of inventory.*/item.*) when `tpls` is given
local function since(mark, names, tpls)
    local out = {}
    for i = mark + 1, #seen do
        local r = seen[i]
        if names[r.ev] and (tpls == nil or tpls[r[2]]) then out[#out + 1] = r end
    end
    return out
end

-- "added PC_Hero It_Apple 3 total=3 script other=nil"
local function inv_line(r)
    return ("%s %s %s %s total=%s %s other=%s"):format(r.ev:sub(11), tostring(r[1]), tostring(r[2]), tostring(r[3]), tostring(r[4]), tostring(r[5]), tostring(r[6]))
end

local function inv_lines(mark, tpls)
    local lines = {}
    for _, r in ipairs(since(mark, { ["inventory.added"] = true, ["inventory.removed"] = true }, tpls)) do lines[#lines + 1] = inv_line(r) end
    table.sort(lines)
    return lines
end

-- inventory.changed must mirror added/removed one to one (signed delta, same owner/template/total/cause/other)
local function mirrored(mark, tpls)
    local want, got = {}, {}
    for _, r in ipairs(since(mark, { ["inventory.added"] = true, ["inventory.removed"] = true }, tpls)) do
        local d = r.ev == "inventory.added" and r[3] or -r[3]
        want[#want + 1] = ("%s %s %s total=%s %s other=%s"):format(tostring(r[1]), tostring(r[2]), tostring(d), tostring(r[4]), tostring(r[5]), tostring(r[6]))
    end
    for _, r in ipairs(since(mark, { ["inventory.changed"] = true }, tpls)) do
        got[#got + 1] = ("%s %s %s total=%s %s other=%s"):format(tostring(r[1]), tostring(r[2]), tostring(r[3]), tostring(r[4]), tostring(r[5]), tostring(r[6]))
    end
    table.sort(want); table.sort(got)
    return want, got
end

local function lines_of(mark, ev, tpls, fmt)
    local out = {}
    for _, r in ipairs(since(mark, { [ev] = true }, tpls)) do
        local line = fmt(r)
        if line then out[#out + 1] = line end
    end
    table.sort(out)
    return out
end

local function set(...) local s = {}; for _, v in ipairs({ ... }) do s[v] = true end; return s end

T.plan {
    "world.ready once", "settling: no inventory events of other owners", "give 3", "give 2 more", "take 4", "changed mirrors added/removed (script)",
    "consume: inventory.removed", "consume: item.consumed", "equip: item.equipped", "unequip: item.unequipped",
    "spawn: entity.spawned", "script give to an NPC", "script transfer", "changed mirrors (transfer)", "dialogue Give", "changed mirrors (dialogue)",
    "loot one stack part", "loot everything", "changed mirrors (loot)", "trade", "drop", "pick up: inventory.added", "pick up: item.picked_up",
    "pick up: entity.destroyed of the world item", "Entity:kill: entity.destroyed", "Kill script: entity.died", "Kill script: npc.killed", "Kill twice: entity.died once",
    "Kill without attacker: entity.died", "Defeat script: npc.defeated", "Defeat script: entity.defeated", "Defeat script: no entity.died", "OnCheatKill: entity.died",
    "no effect entities reported", "combat: an entity dies with cause combat", "combat: entity.damaged",
    "consume: a handler may give to the same inventory", "world.ready after a load", "no foreign inventory events around a load", "give works after a load",
}

T.run(function()
    T.section("new game")
    T.wait_event("session.start", 180, function(m) return m == "new" end)
    T.wait_until(function() return #since(0, { ["world.ready"] = true }) > 0 end, 30, "world.ready")
    T.sleep(60)
    T.eq("world.ready once", lines_of(0, "world.ready", nil, function(r) return tostring(r[1]) end), { "new" })
    local foreign = 0
    for _, r in ipairs(since(0, { ["inventory.added"] = true, ["inventory.removed"] = true, ["inventory.changed"] = true, ["item.equipped"] = true, ["entity.destroyed"] = true })) do
        if r[1] ~= "PC_Hero" and r.ev ~= "entity.destroyed" or r.ev == "entity.destroyed" then foreign = foreign + 1 end
    end
    T.eq("settling: no inventory events of other owners", foreign, 0)

    local p = assert(risen.entity.player())
    local x, y, z = p:position()
    x, y, z = x or 0, y or 0, z or 0

    T.section("script give / take")
    local m = #seen
    risen.item.give(p, "It_Apple", 3); T.sleep(3)
    T.eq("give 3", inv_lines(m, set("It_Apple")), { "added PC_Hero It_Apple 3 total=3 script other=nil" })
    m = #seen
    risen.item.give(p, "It_Apple", 2); T.sleep(3)
    T.eq("give 2 more", inv_lines(m, set("It_Apple")), { "added PC_Hero It_Apple 2 total=5 script other=nil" })
    m = #seen
    risen.item.take(p, "It_Apple", 4); T.sleep(3)
    T.eq("take 4", inv_lines(m, set("It_Apple")), { "removed PC_Hero It_Apple 4 total=1 script other=nil" })
    local want, got = mirrored(0, set("It_Apple"))
    T.eq("changed mirrors added/removed (script)", got, want)

    T.section("consume / equip")
    risen.item.give(p, "It_Po_Health_01", 2); T.sleep(3)
    -- reentrancy: the event is dispatched after the engine operation, so a handler may change the very inventory the event is about
    risen.on("item.consumed", function(owner, tpl) if tpl == "It_Po_Health_01" then risen.item.give(owner, "It_Po_Mana_01", 1) end end)
    m = #seen
    risen.item.consume(p, "It_Po_Health_01"); T.sleep(3)
    T.eq("consume: inventory.removed", inv_lines(m, set("It_Po_Health_01")), { "removed PC_Hero It_Po_Health_01 1 total=1 consume other=nil" })
    T.eq("consume: a handler may give to the same inventory", inv_lines(m, set("It_Po_Mana_01")), { "added PC_Hero It_Po_Mana_01 1 total=1 script other=nil" })
    T.eq("consume: item.consumed", lines_of(m, "item.consumed", nil, function(r) return ("%s %s %s"):format(tostring(r[1]), tostring(r[2]), tostring(r[3])) end),
         { "PC_Hero It_Po_Health_01 1" })
    risen.item.give(p, "It_1H_Sword", 1); T.sleep(3)
    m = #seen
    assert(risen.item.equip(p, "It_1H_Sword")); T.sleep(3)
    T.eq("equip: item.equipped", lines_of(m, "item.equipped", set("It_1H_Sword"), function(r) return ("%s %s"):format(tostring(r[1]), tostring(r[2])) end), { "PC_Hero It_1H_Sword" })
    m = #seen
    assert(risen.item.unequip(p, "It_1H_Sword")); T.sleep(3)
    T.eq("unequip: item.unequipped", lines_of(m, "item.unequipped", set("It_1H_Sword"), function(r) return ("%s %s"):format(tostring(r[1]), tostring(r[2])) end), { "PC_Hero It_1H_Sword" })

    T.section("spawn, NPC inventory, transfer")
    m = #seen
    local npc = assert(risen.npc.spawn("Peasant_1_01", x + 250, y, z, { name = "T_Npc", hp_max = 80, peaceful = true }))
    T.sleep(30)
    T.eq("spawn: entity.spawned", lines_of(m, "entity.spawned", nil, function(r) return ("%s %s"):format(tostring(r[1]), tostring(r[2])) end), { "T_Npc Peasant_1_01" })
    m = #seen
    risen.item.give(npc, "It_Bread", 4); T.sleep(3)
    T.eq("script give to an NPC", inv_lines(m, set("It_Bread")), { "added T_Npc It_Bread 4 total=4 script other=nil" })
    m = #seen
    assert(risen.item.transfer(npc, p, "It_Bread", 3) == 3); T.sleep(3)
    T.eq("script transfer", inv_lines(m, set("It_Bread")), {
        "added PC_Hero It_Bread 3 total=3 script other=T_Npc",
        "removed T_Npc It_Bread 3 total=1 script other=PC_Hero" })
    want, got = mirrored(m, set("It_Bread"))
    T.eq("changed mirrors (transfer)", got, want)

    T.section("dialogue Give")
    risen.dialog.create({ name = "EV_Give", owner = "T_Npc", lines = {
        { speaker = "player", listener = "npc", text = "Bread?" },
        { command = "Give", Entity1 = "npc", Entity2 = "player", Item = "It_Bread", Amount = 1 },
        { command = "End" } } })
    m = #seen
    risen.dialog.start(p, npc)
    T.sleep(90)
    assert(risen.dialog.get("EV_Give")):run()
    T.wait_event("info.finished", 60, function(name) return name == "EV_Give" end)
    T.sleep(30)
    T.eq("dialogue Give", inv_lines(m, set("It_Bread")), {
        "added PC_Hero It_Bread 1 total=4 dialogue other=T_Npc",
        "removed T_Npc It_Bread 1 total=0 dialogue other=PC_Hero" })
    want, got = mirrored(m, set("It_Bread"))
    T.eq("changed mirrors (dialogue)", got, want)

    T.section("loot")
    risen.item.give(npc, "It_Cheese", 5); T.sleep(3)
    risen.script.call("Kill", p, npc, 0); T.sleep(60)
    m = #seen
    assert(risen.item.loot(p, npc, "It_Cheese", 2)); T.sleep(3)
    T.eq("loot one stack part", inv_lines(m, set("It_Cheese")), {
        "added PC_Hero It_Cheese 2 total=2 loot other=T_Npc",
        "removed T_Npc It_Cheese 2 total=3 loot other=PC_Hero" })
    m = #seen
    assert(risen.item.loot(p, npc)); T.sleep(3)
    T.eq("loot everything", inv_lines(m, set("It_Cheese")), {
        "added PC_Hero It_Cheese 3 total=5 loot other=T_Npc",
        "removed T_Npc It_Cheese 3 total=0 loot other=PC_Hero" })
    want, got = mirrored(m, set("It_Cheese"))
    T.eq("changed mirrors (loot)", got, want)

    T.section("trade")
    local trader = assert(risen.npc.spawn("Peasant_1_01", x - 250, y, z, { name = "T_Trader", hp_max = 80, peaceful = true }))
    T.sleep(30)
    risen.item.give(trader, "It_Beer", 2); T.sleep(3)
    m = #seen
    assert(risen.item.trade(trader, p, "It_Beer", 1)); T.sleep(3)
    T.eq("trade", inv_lines(m, set("It_Beer")), {
        "added PC_Hero It_Beer 1 total=1 trade other=T_Trader",
        "removed T_Trader It_Beer 1 total=1 trade other=PC_Hero" })

    T.section("drop / pick up")
    m = #seen
    local dropped = assert(risen.item.drop(p, "It_Apple", 1)); T.sleep(3)
    T.eq("drop", inv_lines(m, set("It_Apple")), { "removed PC_Hero It_Apple 1 total=0 drop other=nil" })
    local dropped_name = dropped:name()
    m = #seen
    assert(risen.item.pick_up(p, dropped)); T.sleep(5)
    T.eq("pick up: inventory.added", inv_lines(m, set("It_Apple")), { "added PC_Hero It_Apple 1 total=1 pickup other=nil" })
    T.eq("pick up: item.picked_up", lines_of(m, "item.picked_up", nil, function(r) return ("%s %s %s"):format(tostring(r[1]), tostring(r[2]), tostring(r[3])) end),
         { "PC_Hero It_Apple 1" })
    T.eq("pick up: entity.destroyed of the world item",
         lines_of(m, "entity.destroyed", nil, function(r) if r[1] == dropped_name then return ("%s %s"):format(tostring(r[2]), tostring(r[3])) end end), { "item It_Apple" })

    T.section("destroy")
    local bread = assert(risen.entity.spawn("It_Bread", x + 40, y, z))
    local bread_name = bread:name()
    T.sleep(3)
    m = #seen
    bread:kill(); T.sleep(5)
    T.eq("Entity:kill: entity.destroyed",
         lines_of(m, "entity.destroyed", nil, function(r) if r[1] == bread_name then return ("%s %s"):format(tostring(r[2]), tostring(r[3])) end end), { "item It_Bread" })

    T.section("deaths")
    local function pair(r) return ("%s %s"):format(tostring(r[1]), tostring(r[2])) end
    local function triple(r) return ("%s %s %s"):format(tostring(r[1]), tostring(r[2]), tostring(r[3])) end
    local wolf = assert(risen.npc.spawn("Wolf", x + 400, y, z, { name = "T_Wolf1", hp_max = 50 }))
    T.sleep(30)
    m = #seen
    risen.script.call("Kill", p, wolf, 0); T.sleep(10)
    T.eq("Kill script: entity.died", lines_of(m, "entity.died", nil, triple), { "T_Wolf1 PC_Hero script" })
    T.eq("Kill script: npc.killed", lines_of(m, "npc.killed", nil, pair), { "T_Wolf1 PC_Hero" })
    m = #seen
    risen.script.call("Kill", p, wolf, 0); T.sleep(10)
    T.eq("Kill twice: entity.died once", lines_of(0, "entity.died", nil, function(r) if r[1] == "T_Wolf1" then return triple(r) end end), { "T_Wolf1 PC_Hero script" })
    local wolf3 = assert(risen.npc.spawn("Wolf", x + 450, y, z, { name = "T_Wolf3", hp_max = 50 }))
    T.sleep(30)
    m = #seen
    risen.script.call("Kill", nil, wolf3, 0); T.sleep(10)
    T.eq("Kill without attacker: entity.died", lines_of(m, "entity.died", nil, triple), { "T_Wolf3 nil script" })

    local human = assert(risen.npc.spawn("Peasant_1_01", x + 300, y + 200, z, { name = "T_Human", hp_max = 50 }))
    T.sleep(30)
    m = #seen
    risen.script.call("Defeat", p, human, 0); T.sleep(10)
    T.eq("Defeat script: npc.defeated", lines_of(m, "npc.defeated", nil, pair), { "T_Human PC_Hero" })
    T.eq("Defeat script: entity.defeated", lines_of(m, "entity.defeated", nil, pair), { "T_Human PC_Hero" })
    T.eq("Defeat script: no entity.died", lines_of(m, "entity.died", nil, triple), {})

    local wolf2 = assert(risen.npc.spawn("Wolf", x + 500, y, z, { name = "T_Wolf2", hp_max = 50 }))
    T.sleep(30)
    m = #seen
    risen.script.call("OnCheatKill", nil, wolf2, 0); T.sleep(60)
    T.eq("OnCheatKill: entity.died", lines_of(m, "entity.died", nil, triple), { "T_Wolf2 PC_Hero cheat" })

    local effects = 0
    for _, r in ipairs(since(0, { ["entity.destroyed"] = true, ["entity.spawned"] = true })) do
        if tostring(r[3] or r[2]):find("^VFX_") then effects = effects + 1 end
    end
    T.eq("no effect entities reported", effects, 0)

    -- a savegame round trip: the world is rebuilt, nothing of it is gameplay, and events work again afterwards
    T.section("save and load")
    risen.world.quick_save()
    T.wait_event("save.after", 60)
    T.sleep(200)
    m = #seen
    risen.world.quick_load()
    T.wait_event("session.start", 120, function(mode) return mode == "load" end)
    T.wait_until(function() return #since(0, { ["world.ready"] = true }) >= 2 end, 30, "world.ready after the load")
    T.sleep(60)
    T.eq("world.ready after a load", lines_of(0, "world.ready", nil, function(r) return tostring(r[1]) end), { "load", "new" })
    foreign = 0
    for _, r in ipairs(since(m, { ["inventory.added"] = true, ["inventory.removed"] = true, ["inventory.changed"] = true, ["item.equipped"] = true, ["entity.destroyed"] = true })) do
        if r.ev == "entity.destroyed" or r[1] ~= "PC_Hero" then foreign = foreign + 1 end
    end
    T.eq("no foreign inventory events around a load", foreign, 0)
    p = assert(risen.entity.player())
    x, y, z = p:position()
    x, y, z = x or 0, y or 0, z or 0
    m = #seen
    risen.item.give(p, "It_Apple", 1); T.sleep(3)
    T.eq("give works after a load", inv_lines(m, set("It_Apple")), { "added PC_Hero It_Apple 1 total=2 script other=nil" })

    -- a real fight, last because the game may be over afterwards: a wolf against a player with one hit point. Whoever is closest gets bitten (the
    -- player or a villager of the world), so the check is: something died in the fight, killed by the wolf or by whoever it attacked, cause "combat".
    T.section("combat death")
    local fighter = assert(risen.npc.spawn("Wolf", x + 150, y, z, { name = "T_WolfC", hp_max = 80 }))
    p:set_hp(1)
    m = #seen
    T.wait_until(function() return #since(m, { ["entity.died"] = true }) > 0 end, 90, "something dies in the fight")
    T.sleep(5)
    local died = lines_of(m, "entity.died", nil, triple)
    T.expect("combat: an entity dies with cause combat", #died >= 1 and died[1]:find(" combat$") ~= nil and not died[1]:find(" nil combat$"), table.concat(died, "; "))
    local hit
    for _, r in ipairs(since(m, { ["entity.damaged"] = true })) do
        if r[2] ~= nil and r[3] >= 1 then hit = r; break end
    end
    T.expect("combat: entity.damaged", hit ~= nil, hit and ("%s <- %s lost %s raw %s"):format(tostring(hit[1]), tostring(hit[2]), tostring(hit[3]), tostring(hit[4])) or "no hit")
    T.sleep(200)

    T.finish("events explored")
end, "events")
