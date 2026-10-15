-- Live verification of the standalone mod `quickloot` (risen.test scenario "quickloot", two game processes: phase default, phase config).
-- TEST ONLY: it drives the game and compares quick loot with the engine's own interaction; the mod itself does not know it.
--
--   .\scripts\test-run.ps1 -Instance 2 -Mods quickloot,quickloot_driver -WaitForResult -Seconds 300 -Scenario quickloot -ShowChecks [-Trace events,calls,hud -TraceScripts "^PreInteract,^Interact_,^Unlock"]
--   .\scripts\test-run.ps1 -Mods quickloot,quickloot_driver -WaitForResult -Scenario quickloot -Param phase=config -CopyInto quickloot=mods\quickloot_driver\fixtures\config
--
-- What is driven: the mod's handler through its programmatic hook (`risen.emit("quickloot.try", entity)`: the same attempt() the key runs on the focused
-- entity), and the real key path with engine action events (`risen.input.simulate` presses the action at the input admin: gCActionAdmin::KeyPressed ->
-- SendActionEvent -> `input.action` -> player state handler), with the engine's focus fixed by `risen.interact.set_focus`. NOT driven: a physical mouse
-- click (the OS input device layer) and what the HUD looks like (screenshots are black).
local T = risen.test
local phase = T.param("phase", "default")

local PLAN_DEFAULT = {
    "ground item: handled as item", "ground item: one stack more", "ground item: events (picked_up, added pickup, destroyed) exactly once", "ground item: no interaction state", "ground item: same events as the engine path",
    "engine path baseline: ZS_Interact states ran",
    "dead human: handled as corpse", "dead human: the club moved, fists and joint stayed", "dead human: same items as the engine's loot window", "dead human: inventory events, one pair per stack",
    "knocked-out human looted", "living human declined", "witness: a living human nearby vetoes the pick-up", "monster corpse declined (trophy)", "door-like object declined", "empty chest declined",
    "plain chest looted", "plain chest: items moved and chest empty", "plain chest: same items as the engine path", "chest is not left in an interaction",
    "locked chest without key declined", "locked chest stays locked", "locked chest with key looted", "key chest unlocked and key kept", "quest-script lock declined while locked", "quest-script lock script not run", "chest with unlocked quest-script lock looted",
    "cupboard looted", "sarcophagus looted", "trap sarcophagus declined", "trap sarcophagus: nothing spawned by the quick path", "trap sarcophagus: engine path spawns the skeleton exactly once",
    "weapon drawn: declined", "weapon drawn: item stays", "weapon away again: works", "too far: declined",
    "key path: press takes the item", "key path: the press was consumed", "key path: no focus target does not consume", "key path: hold takes the next target",
    "save and load: counts equal",
}
local PLAN_CONFIG = {
    "config: items=false declines the apple", "config: the declined press reaches the engine", "config: key PrimaryAction loots the corpse", "config: corpse looted without interaction",
    "config: SecondaryAction is no longer the key",
}
T.plan(phase == "config" and PLAN_CONFIG or PLAN_DEFAULT)

-- ------------------------------------------------------------------------------------------------------------------------------------------
local rec = {}          -- recorded events (strings, in order)
local function add(s) rec[#rec + 1] = s end
local function nm(v) if type(v) == "userdata" then local ok, n = pcall(function() return v:name() end); return ok and n or "?" end return tostring(v) end
for _, e in ipairs { "inventory.added", "inventory.removed", "item.picked_up", "entity.destroyed", "entity.died", "entity.defeated", "entity.spawned", "item.equipped", "item.unequipped" } do
    risen.on(e, function(...)
        local t = {}
        for i = 1, select("#", ...) do t[#t + 1] = nm((select(i, ...))) end
        add(e .. "(" .. table.concat(t, ",") .. ")")
    end)
end
local interact_frames = 0
local last_result
risen.on("quickloot.result", function(r) last_result = r end)
local action_seen = {}  -- driver-side `input.action` events that were NOT consumed by quick loot (low priority)
risen.on("input.action", function(ev) if ev.action == "SecondaryAction" or ev.action == "PrimaryAction" then action_seen[#action_seen + 1] = ev.action .. ":" .. ev.phase end end, -100)

---@type Entity
local p = nil --[[@as Entity]]
local px, py, pz = 0, 0, 0 --[[@as number]]
local lock_script_calls = 0
risen.script.define("SPASS_QL_LockChanged", function() lock_script_calls = lock_script_calls + 1; return 0 end)
risen.on("tick", function()
    if p then
        local s = p:state() or ""
        if s:find("^ZS_Interact") then interact_frames = interact_frames + 1 end
    end
end)

local function check(name, ok, detail) T.expect(name, ok and true or false, detail) end
local function count_ev(mark, pattern)
    local n = 0
    for i = mark + 1, #rec do if rec[i]:find(pattern) then n = n + 1 end end
    return n
end
local function ev_since(mark, pattern)
    local out = {}
    for i = mark + 1, #rec do if rec[i]:find(pattern) then out[#out + 1] = rec[i] end end
    return out
end
-- the inventory events of `mark` as sorted "kind owner tpl count" without the cause (the cause differs by design: pickup/loot/script)
local function inv_norm(mark, owner_pattern)
    local out = {}
    for i = mark + 1, #rec do
        local k, owner, tpl, n = rec[i]:match("^inventory%.(%a+)%(([^,]*),([^,]*),([^,]*),")
        if k and (not owner_pattern or owner:find(owner_pattern)) then out[#out + 1] = k .. " " .. owner .. " " .. tpl .. " " .. n end
    end
    table.sort(out)
    return out
end
local counter = 0
local function spawn_at(tpl, dist, ang)
    counter = counter + 1
    px, py, pz = p:position() --[[@as number]]   -- the engine paths walk the player around: always spawn relative to where it stands now
    local a = math.rad(ang or (counter * 47))
    local e = risen.entity.spawn(tpl, px + math.cos(a) * dist, py + 20, pz + math.sin(a) * dist)
    T.sleep(3)
    return assert(e, "spawn failed: " .. tpl)
end
local function try(e)
    last_result = nil
    risen.emit("quickloot.try", e)
    return last_result or {}
end
local function wait_ready(label)
    local ok = T.wait_until(function() local s = p:state() or ""; return s:sub(1, 9) == "PS_Normal" end, 60, label or "player in PS_Normal")
    T.sleep(10)
    return ok
end
local function counts(e, list)
    local t = {}
    for _, n in ipairs(list) do t[n] = risen.item.count(e, n) end
    return t
end
local function stack_names(e)
    local out = {}
    for i = 0, (e:stack_count() or 0) - 1 do local it = e:stack_item(i); out[#out + 1] = it and it:name() or "?" end
    return out
end
-- the engine's own interaction up to the end (for chests and corpses it opens the loot window: "take all" is the game's own button script)
local function engine_interaction(target, loot_window)
    if not risen.interact.begin(p, target) then return false end
    if loot_window then
        local n = 0
        local opened = T.wait_until(function()
            n = n + 1
            local h = risen.hud.state(); return h and h.loot
        end, 30, "loot window")
        if opened then T.sleep(10); risen.script.call("BtnEvent_OnClick_LootAll", p, target) end
    end
    wait_ready("engine interaction done")
    return true
end

local function new_game_ready()
    T.wait_event("session.start", 180, function(m) return m == "new" end)
    p = assert(risen.entity.player())
    T.wait_until(function() return p:state() == "PS_Normal_Loop" end, 120, "intro over")
    T.sleep(80)
    px, py, pz = p:position() --[[@as number]]
    p:set_hp_max(5000); p:set_hp(5000)   -- nothing in this test may kill the player (monsters, the trap's skeleton)
end

-- ==========================================================================================================================================
local function default_phase()
    local P0 = { "It_Apple", "It_1H_Club", "It_Fist_Left", "It_Joint", "It_Gold", "It_Key_Gamestart" }

    T.section("ground item")
    local apple = spawn_at("It_Apple", 150)
    local before = risen.item.count(p, "It_Apple")
    local mark = #rec
    interact_frames = 0
    local r = try(apple)
    T.sleep(40)
    check("ground item: handled as item", r.handled and r.kind == "item", tostring(r.reason))
    T.eq("ground item: one stack more", risen.item.count(p, "It_Apple"), before + 1)
    check("ground item: events (picked_up, added pickup, destroyed) exactly once",
        count_ev(mark, "^item%.picked_up%(PC_Hero,It_Apple") == 1 and count_ev(mark, "^inventory%.added%(PC_Hero,It_Apple,1,%d+,pickup") == 1 and count_ev(mark, "^entity%.destroyed%(It_Apple") == 1,
        table.concat(ev_since(mark, ".*"), " | "))
    check("ground item: no interaction state", interact_frames == 0, "frames in ZS_Interact: " .. interact_frames)
    local quick_item_events = inv_norm(mark, "PC_Hero")
    -- baseline: the engine's own pick-up of another apple
    local apple2 = spawn_at("It_Apple", 150)
    mark = #rec
    interact_frames = 0
    engine_interaction(apple2, false)
    check("engine path baseline: ZS_Interact states ran", interact_frames > 10, "frames: " .. interact_frames)
    T.eq("ground item: same events as the engine path", inv_norm(mark, "PC_Hero"), quick_item_events)

    local function chest(tpl, locked, items, ang)
        local c = spawn_at(tpl, 200, ang)
        if locked ~= nil and c:get_property("gCLock_PS", "Status") then
            c:set_property("gCLock_PS", "Status", locked and "gELockStatus_Locked" or "gELockStatus_Unlocked")
        end
        for _, it in ipairs(items or { { "It_Apple", 2 }, { "It_Gold", 15 } }) do risen.item.give(c, it[1], it[2]) end
        T.sleep(3)
        return c
    end
    -- the engine's own chest interaction first, while the area around the player is still clear (it walks to the chest and kneels)
    local c2 = chest("Object_Interact_Animated_Chest_Poor", false, nil, 57.3)
    mark = #rec
    engine_interaction(c2, true)
    local engine_chest_events = inv_norm(mark, nil)

    T.section("bodies")
    local function body(how)
        local n = spawn_at("Peasant_1_01", 170)
        n:equip_all()
        T.sleep(3)
        if how == "dead" then risen.script.call("Kill", p, n) elseif how == "down" then risen.script.call("Defeat", p, n) end
        T.sleep(10)
        return n
    end
    local b1 = body("dead")
    local pc = counts(p, P0)
    mark = #rec
    r = try(b1)
    T.sleep(5)
    local pd = counts(p, P0)
    check("dead human: handled as corpse", r.handled and r.kind == "corpse", tostring(r.reason))
    check("dead human: the club moved, fists and joint stayed", pd.It_1H_Club == pc.It_1H_Club + 1 and pd.It_Fist_Left == pc.It_Fist_Left and pd.It_Joint == pc.It_Joint,
        ("club %d->%d fist %d->%d joint %d->%d"):format(pc.It_1H_Club, pd.It_1H_Club, pc.It_Fist_Left, pd.It_Fist_Left, pc.It_Joint, pd.It_Joint))
    local quick_body = inv_norm(mark, nil)
    check("dead human: inventory events, one pair per stack", #quick_body == 2 and count_ev(mark, "^inventory%.added%(PC_Hero,It_1H_Club") == 1, table.concat(quick_body, " | "))
    local b2 = body("dead")
    pc = counts(p, P0)
    engine_interaction(b2, true)
    pd = counts(p, P0)
    check("dead human: same items as the engine's loot window", pd.It_1H_Club == pc.It_1H_Club + 1 and pd.It_Fist_Left == pc.It_Fist_Left and pd.It_Joint == pc.It_Joint,
        ("club %d->%d fist %d->%d joint %d->%d"):format(pc.It_1H_Club, pd.It_1H_Club, pc.It_Fist_Left, pd.It_Fist_Left, pc.It_Joint, pd.It_Joint))
    local b3 = body("down")
    pc = counts(p, P0)
    r = try(b3)
    T.sleep(5)
    check("knocked-out human looted", r.handled and r.kind == "corpse" and risen.item.count(p, "It_1H_Club") == pc.It_1H_Club + 1, tostring(r.reason))
    local live = spawn_at("Peasant_1_01", 170)
    live:equip_all()
    T.sleep(3)
    pc = counts(p, P0)
    r = try(live)
    check("living human declined", not r.handled and risen.item.count(p, "It_1H_Club") == pc.It_1H_Club, tostring(r.reason))
    local seen_apple = spawn_at("It_Apple", 150)
    pc = counts(p, P0)
    r = try(seen_apple)
    check("witness: a living human nearby vetoes the pick-up", not r.handled and (r.reason or ""):find("seen by") and risen.item.count(p, "It_Apple") == pc.It_Apple, tostring(r.reason))
    live:kill()
    T.sleep(5)
    r = try(seen_apple)
    T.sleep(5)
    local wolf = spawn_at("Wolf", 250)
    T.sleep(3)
    risen.script.call("Kill", p, wolf)
    wolf:set_state("ZS_Dead")   -- the Kill script alone leaves the corpse in its attack state: it keeps biting
    wolf:set_frozen(true)
    T.sleep(10)
    mark = #rec
    r = try(wolf)
    T.sleep(5)
    check("monster corpse declined (trophy)", not r.handled and count_ev(mark, "^inventory%.%a+%(PC_Hero") == 0, tostring(r.reason) .. "; " .. table.concat(ev_since(mark, "^inventory%."), " | "))
    local bed = spawn_at("Object_Interact_Bed", 200)
    r = try(bed)
    check("door-like object declined", not r.handled, tostring(r.reason))
    local empty = spawn_at("Object_Interact_Animated_Chest_Poor", 200)
    empty:set_property("gCLock_PS", "Status", "gELockStatus_Unlocked")
    r = try(empty)
    check("empty chest declined", not r.handled, tostring(r.reason))

    T.section("chests")
    local c1 = chest("Object_Interact_Animated_Chest_Poor", false)
    pc = counts(p, P0)
    mark = #rec
    interact_frames = 0
    r = try(c1)
    T.sleep(5)
    pd = counts(p, P0)
    check("plain chest looted", r.handled and r.kind == "container", tostring(r.reason))
    check("plain chest: items moved and chest empty", pd.It_Apple == pc.It_Apple + 2 and pd.It_Gold == pc.It_Gold + 15 and risen.item.count(c1, "It_Apple") == 0 and risen.item.count(c1, "It_Gold") == 0,
        ("apples %d->%d gold %d->%d"):format(pc.It_Apple, pd.It_Apple, pc.It_Gold, pd.It_Gold))
    local quick_chest = inv_norm(mark, nil)
    T.eq("plain chest: same items as the engine path", engine_chest_events, quick_chest)
    check("chest is not left in an interaction", risen.interact.current(p) == nil and (p:state() or ""):sub(1, 9) == "PS_Normal", tostring(p:state()))

    local c3 = chest("Object_Interact_Animated_Chest_Poor", true)
    pc = counts(p, P0)
    r = try(c3)
    check("locked chest without key declined", not r.handled and risen.item.count(p, "It_Apple") == pc.It_Apple, tostring(r.reason))
    T.eq("locked chest stays locked", c3:get_property("gCLock_PS", "Status"), "gELockStatus_Locked")
    c3:set_property("gCLock_PS", "Key", "It_Key_Gamestart")
    risen.item.give(p, "It_Key_Gamestart", 1)
    T.sleep(3)
    pc = counts(p, P0)
    r = try(c3)
    T.sleep(5)
    check("locked chest with key looted", r.handled and risen.item.count(p, "It_Apple") == pc.It_Apple + 2, tostring(r.reason))
    check("key chest unlocked and key kept", c3:get_property("gCLock_PS", "Status") == "gELockStatus_Unlocked" and risen.item.count(p, "It_Key_Gamestart") == 1,
        tostring(c3:get_property("gCLock_PS", "Status")))
    local c4 = chest("Object_Interact_Animated_Chest_Poor", true)
    c4:set_property("gCLock_PS", "Key", "It_Key_Gamestart")
    c4:set_property("gCLock_PS", "OnLockStatusChanged", "SPASS_QL_LockChanged")
    pc = counts(p, P0)
    r = try(c4)
    check("quest-script lock declined while locked", not r.handled and c4:get_property("gCLock_PS", "Status") == "gELockStatus_Locked" and risen.item.count(p, "It_Apple") == pc.It_Apple, tostring(r.reason))
    T.eq("quest-script lock script not run", lock_script_calls, 0)
    c4:set_property("gCLock_PS", "Status", "gELockStatus_Unlocked")
    r = try(c4)
    check("chest with unlocked quest-script lock looted", r.handled and risen.item.count(p, "It_Apple") == pc.It_Apple + 2, tostring(r.reason))
    local cup = chest("Object_Interact_Animated_Cupboard", nil)
    pc = counts(p, P0)
    r = try(cup)
    check("cupboard looted", r.handled and risen.item.count(p, "It_Apple") == pc.It_Apple + 2, tostring(r.reason))
    local sar = chest("Object_Interact_Sarcophagus", nil)
    pc = counts(p, P0)
    r = try(sar)
    check("sarcophagus looted", r.handled and risen.item.count(p, "It_Apple") == pc.It_Apple + 2, tostring(r.reason))

    T.section("weapon drawn")
    local a3 = spawn_at("It_Apple", 150)
    risen.item.give(p, "It_1H_Sword", 1)
    risen.item.equip(p, "It_1H_Sword")
    T.sleep(10)
    risen.input.simulate("WeaponMode", true); T.sleep(4); risen.input.simulate("WeaponMode", false)
    T.wait_until(function() return (p:state() or ""):find("^PS_Melee") end, 10, "weapon drawn")
    T.sleep(60)
    local drawn_state = p:state()
    pc = counts(p, P0)
    local apples_destroyed = count_ev(0, "^entity%.destroyed%(It_Apple")
    r = try(a3)
    check("weapon drawn: declined", not r.handled and (drawn_state or ""):find("^PS_Melee"), tostring(drawn_state) .. ": " .. tostring(r.reason))
    check("weapon drawn: item stays", risen.item.count(p, "It_Apple") == pc.It_Apple and count_ev(0, "^entity%.destroyed%(It_Apple") == apples_destroyed)
    risen.input.simulate("WeaponMode", true); T.sleep(4); risen.input.simulate("WeaponMode", false)
    wait_ready("weapon put away")
    r = try(a3)
    T.sleep(5)
    check("weapon away again: works", r.handled and risen.item.count(p, "It_Apple") == pc.It_Apple + 1, tostring(r.reason))
    local far = spawn_at("It_Apple", 900)
    r = try(far)
    check("too far: declined", not r.handled and far:position() ~= nil, tostring(r.reason))

    T.section("key path")
    local k1 = spawn_at("It_Apple", 150)
    local k2 = spawn_at("It_Apple", 170)
    pc = counts(p, P0)
    action_seen = {}
    risen.interact.set_focus(k1, true)
    T.sleep(3)
    risen.input.simulate("SecondaryAction", true)
    T.sleep(4)
    check("key path: press takes the item", risen.item.count(p, "It_Apple") == pc.It_Apple + 1, "apples " .. pc.It_Apple .. " -> " .. risen.item.count(p, "It_Apple"))
    -- still holding: the next target in focus is taken by the repeat
    risen.interact.set_focus(k2, true)
    T.wait_until(function() return risen.item.count(p, "It_Apple") == pc.It_Apple + 2 end, 5, "repeat takes the second apple")
    risen.input.simulate("SecondaryAction", false)
    T.sleep(5)
    check("key path: hold takes the next target", risen.item.count(p, "It_Apple") == pc.It_Apple + 2, "apples " .. risen.item.count(p, "It_Apple"))
    check("key path: the press was consumed", #action_seen == 0, table.concat(action_seen, ","))
    risen.interact.set_focus(nil, true)
    T.sleep(3)
    action_seen = {}
    risen.input.simulate("SecondaryAction", true); T.sleep(4); risen.input.simulate("SecondaryAction", false); T.sleep(5)
    check("key path: no focus target does not consume", #action_seen >= 2, table.concat(action_seen, ","))
    risen.interact.set_focus(nil, false)

    T.section("save and load")
    local saved = counts(p, P0)
    risen.world.quick_save()
    T.wait_event("save.after", 30)
    T.sleep(200)
    local extra = spawn_at("It_Apple", 150)
    try(extra)
    T.sleep(5)
    local more = risen.item.count(p, "It_Apple")
    risen.world.quick_load()
    T.wait_event("session.start", 120, function(m) return m == "load" end)
    p = assert(risen.entity.player())
    T.sleep(120)
    local after = counts(p, P0)
    check("save and load: counts equal", more == saved.It_Apple + 1 and after.It_Apple == saved.It_Apple and after.It_1H_Club == saved.It_1H_Club and after.It_Gold == saved.It_Gold,
        ("saved apples %d, after pickup %d, after load %d; club %d/%d gold %d/%d"):format(saved.It_Apple, more, after.It_Apple, saved.It_1H_Club, after.It_1H_Club, saved.It_Gold, after.It_Gold))
    wait_ready("after load")
    px, py, pz = p:position() --[[@as number]]

    T.section("traps")
    local trap1 = spawn_at("Object_Interact_Sarcophagus_Trap", 200, 57.3)
    mark = #rec
    r = try(trap1)
    T.sleep(10)
    check("trap sarcophagus declined", not r.handled, tostring(r.reason))
    check("trap sarcophagus: nothing spawned by the quick path", count_ev(mark, "^entity%.spawned") == 0, table.concat(ev_since(mark, "^entity%.spawned"), " | "))
    mark = #rec
    risen.interact.begin(p, trap1)
    T.wait_until(function() return count_ev(mark, "^entity%.spawned%(Skeleton") >= 1 end, 40, "skeleton of the trap")
    T.sleep(60)
    check("trap sarcophagus: engine path spawns the skeleton exactly once", count_ev(mark, "^entity%.spawned%(Skeleton") == 1, table.concat(ev_since(mark, "^entity%.spawned"), " | "))
end

-- ==========================================================================================================================================
local function config_phase()
    -- config.toml of the fixture: key = PrimaryAction, items = false (everything else default)
    T.section("config")
    local apple = spawn_at("It_Apple", 150)
    local before = risen.item.count(p, "It_Apple")
    risen.interact.set_focus(apple, true)
    T.sleep(3)
    interact_frames = 0
    action_seen = {}
    risen.input.simulate("PrimaryAction", true); T.sleep(3); risen.input.simulate("PrimaryAction", false)
    T.sleep(20)
    check("config: items=false declines the apple", last_result and not last_result.handled and last_result.reason == "switched off in config.toml", last_result and tostring(last_result.reason) or "no result")
    -- (the engine's own use key is ignored while the GUI reports "browsing", as it does in this test copy: only the delivery of the event is checked, see RE.md section 31)
    check("config: the declined press reaches the engine", #action_seen >= 2 and action_seen[1] == "PrimaryAction:pressed", table.concat(action_seen, ",") .. "; frames in ZS_Interact: " .. interact_frames)
    wait_ready("engine pick-up done")
    local body = spawn_at("Peasant_1_01", 170)
    body:equip_all()
    T.sleep(3)
    risen.script.call("Kill", p, body)
    T.sleep(10)
    local club = risen.item.count(p, "It_1H_Club")
    interact_frames = 0
    action_seen = {}
    risen.interact.set_focus(body, true)
    T.sleep(3)
    last_result = nil
    risen.input.simulate("PrimaryAction", true); T.sleep(3); risen.input.simulate("PrimaryAction", false)
    T.sleep(20)
    check("config: key PrimaryAction loots the corpse", risen.item.count(p, "It_1H_Club") == club + 1 and last_result and last_result.kind == "corpse", last_result and tostring(last_result.reason) or "no result")
    check("config: corpse looted without interaction", interact_frames == 0 and #action_seen == 0, "frames: " .. interact_frames .. "; events that reached the engine: " .. table.concat(action_seen, ","))
    local apple2 = spawn_at("It_Apple", 150)
    local n = risen.item.count(p, "It_Apple")
    risen.interact.set_focus(apple2, true)
    T.sleep(3)
    last_result = nil
    risen.input.simulate("SecondaryAction", true); T.sleep(3); risen.input.simulate("SecondaryAction", false)
    T.sleep(20)
    check("config: SecondaryAction is no longer the key", last_result == nil and risen.item.count(p, "It_Apple") == n, "apples " .. n .. " -> " .. risen.item.count(p, "It_Apple"))
    risen.interact.set_focus(nil, false)
end

T.run(function()
    new_game_ready()
    if phase == "config" then config_phase() else default_phase() end
end, "quickloot")
