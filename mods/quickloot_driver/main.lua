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
    "ground item: handled as item", "ground item: one stack more", "ground item: events (picked_up, added pickup, destroyed) exactly once", "ground item: the pick-up effect (material sound) started once", "ground item: the engine path starts the same effect", "ground item: rapid pick-ups make one sound, the next one after the gap", "effect.start: bad names and gone entities refused, an unknown name is harmless", "ground item: tutorial hint takeitem handled like the engine path", "ground item: no interaction state", "ground item: same events as the engine path",
    "engine path baseline: ZS_Interact states ran",
    "dead human: handled as corpse", "dead human: tutorial hint loot can be ended", "dead human: the club moved, fists and joint stayed", "dead human: same items as the engine's loot window", "dead human: inventory events, one pair per stack",
    "knocked-out human looted", "living human declined", "witness: a living human nearby does not stop the pick-up (assess mode, no watched area)", "monster corpse (wolf) looted", "monster corpse: trophy taken, fist stayed", "monster corpse: same items as the engine's loot window", "monster corpses of other species looted", "washed-up body (Beach_Seaman_Dead) looted", "washed-up body: the hero says his remark (GamestartLoot)", "washed-up body: same items as the engine's loot window", "trophies without skill: only the unconditional ones", "trophies without skill: nothing left on the carcasses", "trophies with the Trophy skill: fur taken", "trophies with skill and recipes: horns, claws, tusks, stings taken", "trophies: nothing lootable left on the carcasses", "trophies: same items as the engine's loot window (skill learned)", "door-like object declined", "empty chest declined",
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
    "veto: a witness who sees the player stops the chest looting", "veto: nothing was taken, nobody was asked", "veto: a witness behind the player does not stop it",
}
-- M5e: the NPC reaction (docs/QUICKLOOT.md "Witnesses"): the engine's own theft assessment on the quick path against the animated path, inside a watched area (SL_Public)
local PLAN_REACT = {
    "zone: the area names were written and read back", "outside a watched area: quick take handled, nobody reacts",
    "engine rule: AssessTheft answers 1 inside the area and 0 outside", "witness faces the player",
    "animated item: the witness reacted", "animated item: AssessTheft asked before the pick-up, once per human",
    "quick item: handled, assessed, an NPC reacted", "quick item: the witness reacted like in the animated path", "quick item: the same AssessTheft calls and order as the animated path",
    "animated chest: the witness reacted", "quick chest: handled and the witness reacted like in the animated path", "quick chest: the same AssessTheft calls as the animated path",
    "quick chest: asked before anything was taken",
    "cupboard: quick take, the witness reacted", "sarcophagus: neither path asks the NPCs",
    "corpse: quick loot is no theft", "corpse: animated loot is no theft either",
    "witness behind the player: taken, nobody reacts", "witness out of range: taken, nobody reacts", "nobody around: taken, asked, nobody reacts",
    "dropped item: no theft", "declined chest alarms nobody",
}
local PLAN_IGNORE = {
    "ignore: item taken in sight of a witness in a watched area", "ignore: nobody was asked and nobody reacted", "ignore: chest taken, nobody asked", "ignore: pickup_sound = false starts no pick-up effect",
}
local PLANS = { config = PLAN_CONFIG, react = PLAN_REACT, ignore = PLAN_IGNORE }
T.plan(PLANS[phase] or PLAN_DEFAULT)

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
    local tut_take0, tut_loot0 = risen.hud.tutorial_done("takeitem"), risen.hud.tutorial_done("loot")
    local apple = spawn_at("It_Apple", 150)
    local before = risen.item.count(p, "It_Apple")
    local fx0 = risen.effect.count("eff_item_apple_transfer")
    local mark = #rec
    interact_frames = 0
    local r = try(apple)
    T.sleep(40)
    check("ground item: handled as item", r.handled and r.kind == "item", tostring(r.reason))
    local fx1 = risen.effect.count("eff_item_apple_transfer")
    check("ground item: the pick-up effect (material sound) started once", fx1 == fx0 + 1, ("apple transfer effects %d -> %d"):format(fx0, fx1))
    T.eq("ground item: one stack more", risen.item.count(p, "It_Apple"), before + 1)
    check("ground item: events (picked_up, added pickup, destroyed) exactly once",
        count_ev(mark, "^item%.picked_up%(PC_Hero,It_Apple") == 1 and count_ev(mark, "^inventory%.added%(PC_Hero,It_Apple,1,%d+,pickup") == 1 and count_ev(mark, "^entity%.destroyed%(It_Apple") == 1,
        table.concat(ev_since(mark, ".*"), " | "))
    -- Tutorial::IsCompleted stays false after the engine's own take as well (the hint is not what it reports in the test copy): the claim is only that the call works and the state equals the engine path's
    local quick_tut = risen.hud.tutorial_done("takeitem")
    check("ground item: no interaction state", interact_frames == 0, "frames in ZS_Interact: " .. interact_frames)
    local quick_item_events = inv_norm(mark, "PC_Hero")
    -- baseline: the engine's own pick-up of another apple
    local apple2 = spawn_at("It_Apple", 150)
    mark = #rec
    interact_frames = 0
    engine_interaction(apple2, false)
    check("engine path baseline: ZS_Interact states ran", interact_frames > 10, "frames: " .. interact_frames)
    local fx2 = risen.effect.count("eff_item_apple_transfer")
    check("ground item: the engine path starts the same effect", fx2 == fx1 + 1, ("%d -> %d"):format(fx1, fx2))
    local bad1 = risen.effect.start("bad name!", p)
    local bad2 = risen.effect.start("", p)
    local bad3 = risen.effect.start(("x"):rep(200), p)
    local ok_unknown = risen.effect.start("eff_does_not_exist_01", p)
    local gone = assert(risen.entity.spawn("It_Apple", px + 400, py + 20, pz)); T.sleep(3); gone:kill(); T.sleep(3)
    local bad4 = risen.effect.start("eff_item_apple_transfer_01", gone)
    check("effect.start: bad names and gone entities refused, an unknown name is harmless", bad1 == nil and bad2 == nil and bad3 == nil and bad4 == nil and ok_unknown == true,
        ("bad %s %s %s gone %s unknown %s"):format(tostring(bad1), tostring(bad2), tostring(bad3), tostring(bad4), tostring(ok_unknown)))
    check("ground item: tutorial hint takeitem handled like the engine path", risen.hud.tutorial_stop("takeitem") == true and quick_tut == risen.hud.tutorial_done("takeitem"),
        ("before %s, after quick %s, after engine %s"):format(tostring(tut_take0), tostring(quick_tut), tostring(risen.hud.tutorial_done("takeitem"))))
    T.eq("ground item: same events as the engine path", inv_norm(mark, "PC_Hero"), quick_item_events)
    -- holding the key: three apples in the same instant make one sound, a later one makes another (pickup_sound_gap)
    local rapid = { (spawn_at("It_Apple", 150)), (spawn_at("It_Apple", 150)), (spawn_at("It_Apple", 150)) }
    local fr0 = risen.effect.count("eff_item_apple_transfer")
    for _, a in ipairs(rapid) do try(a) end
    local fr1 = risen.effect.count("eff_item_apple_transfer")
    local waited = os.clock()
    T.wait_until(function() return os.clock() - waited > 0.8 end, 10, "the sound gap passes")
    try((spawn_at("It_Apple", 150)))
    local fr2 = risen.effect.count("eff_item_apple_transfer")
    check("ground item: rapid pick-ups make one sound, the next one after the gap", fr1 == fr0 + 1 and fr2 == fr1 + 1, ("%d -> %d (three apples) -> %d (after the gap)"):format(fr0, fr1, fr2))

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
    check("dead human: tutorial hint loot can be ended", risen.hud.tutorial_stop("loot") == true and risen.hud.tutorial_done("loot") == tut_loot0, "before " .. tostring(tut_loot0) .. ", after " .. tostring(risen.hud.tutorial_done("loot")))
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
    T.sleep(5)
    check("witness: a living human nearby does not stop the pick-up (assess mode, no watched area)",
        r.handled and r.assessed == true and r.reacted == false and risen.item.count(p, "It_Apple") == pc.It_Apple + 1, tostring(r.reason) .. " assessed=" .. tostring(r.assessed) .. " reacted=" .. tostring(r.reacted))
    live:kill()
    T.sleep(5)
    local function carcass(tpl, dist)
        local m = spawn_at(tpl, dist)
        T.sleep(3)
        risen.script.call("Kill", p, m)
        m:set_state("ZS_Dead")   -- the Kill script alone leaves the corpse in its attack state: it keeps biting
        m:set_frozen(true)
        T.sleep(10)
        return m
    end
    local M0 = { "It_Teeth", "It_Fist_Monster", "It_Meat_Raw", "It_Fur_Wolf" }
    local wolf = carcass("Wolf", 250)
    pc = counts(p, M0)
    mark = #rec
    r = try(wolf)
    T.sleep(5)
    pd = counts(p, M0)
    check("monster corpse (wolf) looted", r.handled and r.kind == "corpse", tostring(r.reason))
    check("monster corpse: trophy taken, fist stayed", pd.It_Teeth == pc.It_Teeth + 1 and pd.It_Fist_Monster == pc.It_Fist_Monster,
        ("teeth %d->%d fist %d->%d; %s"):format(pc.It_Teeth, pd.It_Teeth, pc.It_Fist_Monster, pd.It_Fist_Monster, table.concat(ev_since(mark, "^inventory%."), " | ")))
    local wolf2 = carcass("Wolf", 250)
    pc = counts(p, M0)
    engine_interaction(wolf2, true)
    pd = counts(p, M0)
    check("monster corpse: same items as the engine's loot window", pd.It_Teeth == pc.It_Teeth + 1 and pd.It_Fist_Monster == pc.It_Fist_Monster,
        ("teeth %d->%d fist %d->%d"):format(pc.It_Teeth, pd.It_Teeth, pc.It_Fist_Monster, pd.It_Fist_Monster))
    local others, bad = {}, {}
    for _, tpl in ipairs { "Boar", "Gnome" } do
        local m = carcass(tpl, 260)
        local r2 = try(m)
        T.sleep(3)
        others[#others + 1] = tpl .. "=" .. tostring(r2.handled) .. "/" .. tostring(r2.kind) .. "/" .. tostring(r2.reason)
        if not (r2.handled and r2.kind == "corpse") then bad[#bad + 1] = tpl end
    end
    check("monster corpses of other species looted", #bad == 0, table.concat(others, "; "))
    local W0 = { "It_Apple", "It_Flute", "It_Fish_Raw", "It_Grog", "It_Ring_Gold" }
    local sea = carcass("Beach_Seaman_Dead", 230)
    local sea_stacks = table.concat(stack_names(sea), ",")
    pc = counts(p, W0)
    r = try(sea)
    T.sleep(5)
    pd = counts(p, W0)
    local gained = 0
    for _, n in ipairs(W0) do gained = gained + pd[n] - pc[n] end
    check("washed-up body (Beach_Seaman_Dead) looted", r.handled and r.kind == "corpse" and gained > 0, tostring(r.reason) .. "; stacks " .. sea_stacks .. "; gained " .. gained)
    local said = T.wait_until(function() return risen.voice.speaking(p) end, 90, "the hero speaks")
    check("washed-up body: the hero says his remark (GamestartLoot)", r.comment == true and said, "comment=" .. tostring(r.comment) .. " speaking=" .. tostring(said))
    local sea2 = carcass("Beach_Seaman_Dead", 230)
    pc = counts(p, W0)
    engine_interaction(sea2, true)
    pd = counts(p, W0)
    local gained2 = 0
    for _, n in ipairs(W0) do gained2 = gained2 + pd[n] - pc[n] end
    check("washed-up body: same items as the engine's loot window", gained2 == gained, "quick " .. gained .. ", window " .. gained2)
    T.section("trophies")
    -- The species can scripts (docs/RE.md section 31) put the trophies into the corpse the first time they are asked: fur and the like only when the player has the Trophy skill (id 33),
    -- tusks/horns/claws/stings also with the matching recipe item in his inventory. Quick loot asks the same script, so what he gets must follow the skill like the animated path.
    local CASES = {
        { "Wolf",          always = { "It_Teeth" },                         skill = { "It_Fur_Wolf" } },
        { "Boar",          always = { "It_Meat_Raw" },                      skill = { "It_Fur_Boar" },       recipe = { "It_Recipe_Tusk", "It_Tusk" } },
        { "Rotworm",       always = { "It_Teeth", "It_Meat_Raw" },          recipe = { "It_Recipe_Rotworm", "It_Claw_Rotworm" } },
        { "GiantScorpion", always = { "It_Claw_Scorpion" },                 recipe = { "It_Recipe_Sting", "It_Sting_Scorpion" } },
        { "Ghul",          always = { "It_Claw" } },
        { "AshBeast",      always = { "It_Teeth", "It_Heart_Ashbeast" },    skill = { "It_Fur_AshBeast" } },
    }
    local ALL, seen = {}, {}
    local function add_all(n) if not seen[n] then seen[n] = true; ALL[#ALL + 1] = n end end
    for _, c in ipairs(CASES) do
        for _, n in ipairs(c.always) do add_all(n) end
        for _, n in ipairs(c.skill or {}) do add_all(n) end
        if c.recipe then add_all(c.recipe[2]) end
    end
    local function left_on(m)
        local n = 0
        for i = 0, (m:stack_count() or 0) - 1 do if (risen.script.call("InvFilter_Loot", m, nil, i) or 0) > 0 then n = n + 1 end end
        return n
    end
    local function loot_all(with_skill)
        local detail, left = {}, 0
        local gained_all = {}
        for _, c in ipairs(CASES) do
            local m = carcass(c[1], 280)
            local before = counts(p, ALL)
            local r2 = try(m)
            T.sleep(3)
            local after = counts(p, ALL)
            local got = {}
            for _, n in ipairs(ALL) do if after[n] > before[n] then got[#got + 1] = n .. "x" .. (after[n] - before[n]) end end
            gained_all[c[1]] = { before = before, after = after }
            detail[#detail + 1] = c[1] .. ": " .. (r2.handled and "" or "DECLINED (" .. tostring(r2.reason) .. ") ") .. table.concat(got, ",")
            left = left + left_on(m)
        end
        return gained_all, detail, left
    end
    local function has(g, c, n) return g[c[1]].after[n] > g[c[1]].before[n] end

    risen.npc.set_skill_base(p, 33, 0)
    local g0, d0, l0 = loot_all(false)
    local ok0 = true
    for _, c in ipairs(CASES) do
        for _, n in ipairs(c.always) do ok0 = ok0 and has(g0, c, n) end
        for _, n in ipairs(c.skill or {}) do ok0 = ok0 and not has(g0, c, n) end
        if c.recipe then ok0 = ok0 and not has(g0, c, c.recipe[2]) end
    end
    check("trophies without skill: only the unconditional ones", ok0, table.concat(d0, " | "))
    check("trophies without skill: nothing left on the carcasses", l0 == 0, "stacks left: " .. l0)
    risen.npc.set_skill_base(p, 33, 1)
    local g1, d1, l1 = loot_all(true)
    local fur_ok = true
    for _, c in ipairs(CASES) do for _, n in ipairs(c.skill or {}) do fur_ok = fur_ok and has(g1, c, n) end end
    check("trophies with the Trophy skill: fur taken", fur_ok and (risen.npc.skill_raw(p, 33) or 0) >= 1, table.concat(d1, " | "))
    for _, c in ipairs(CASES) do if c.recipe then risen.item.give(p, c.recipe[1], 1) end end
    T.sleep(3)
    local g2, d2, l2 = loot_all(true)
    local rec_ok = true
    for _, c in ipairs(CASES) do
        for _, n in ipairs(c.always) do rec_ok = rec_ok and has(g2, c, n) end
        for _, n in ipairs(c.skill or {}) do rec_ok = rec_ok and has(g2, c, n) end
        if c.recipe then rec_ok = rec_ok and has(g2, c, c.recipe[2]) end
    end
    check("trophies with skill and recipes: horns, claws, tusks, stings taken", rec_ok, table.concat(d2, " | "))
    check("trophies: nothing lootable left on the carcasses", l1 == 0 and l2 == 0, "stacks left: " .. l1 .. " / " .. l2)
    -- the engine's loot window with the skill learned: the same set as the quick path (a Boar: fur, tusk, meat)
    local bq = carcass("Boar", 280)
    local pq = counts(p, ALL)
    try(bq); T.sleep(3)
    local aq = counts(p, ALL)
    local be = carcass("Boar", 280)
    local pe = counts(p, ALL)
    engine_interaction(be, true)
    local ae = counts(p, ALL)
    local same = true
    local dq, de = {}, {}
    for _, n in ipairs(ALL) do
        if (aq[n] - pq[n]) ~= (ae[n] - pe[n]) then same = false end
        if aq[n] ~= pq[n] then dq[#dq + 1] = n .. "x" .. (aq[n] - pq[n]) end
        if ae[n] ~= pe[n] then de[#de + 1] = n .. "x" .. (ae[n] - pe[n]) end
    end
    check("trophies: same items as the engine's loot window (skill learned)", same and #dq > 0, "quick " .. table.concat(dq, ",") .. "; window " .. table.concat(de, ","))
    risen.npc.set_skill_base(p, 33, 0)

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

-- ==========================================================================================================================================
-- M5e: NPC reactions. The engine's own theft assessment (the perception broadcast of AssessTheft) on the quick path against the animated path.
-- A theft is only a theft inside a watched area: AreaZone_Default with the area name SL_Public around the player makes the beach a "house".
-- The witness is a Peasant 300 units south of the player; NPCs spawn looking along +z, so he looks at the player ("front"); north of him ("back") he looks away.
-- ==========================================================================================================================================
local function observe_theft()
    risen.ai.perceive("theft", function(ctx)
        local r = ctx:call_original()
        local name = nm(ctx.self)
        if name:find("^Peasant") then add(("AssessTheft(%s,%s)=%d"):format(name, nm(ctx.other), r)) end
        return r
    end, { all = true })
end
local function snap(n)
    local state = (n:state() or ""):gsub("_Loop$", "")
    return { mode = n:get_property("gCScriptRoutine_PS", "AIMode"), comment = n:get_property("gCNPC_PS", "LastPlayerComment"), reason = n:get_property("gCNPC_PS", "Reason"), state = state }
end
local function reacted(s) return s.comment == "gEComment_Theft" and s.reason == "gEReason_Theft" end
local function put(tpl, dx, dz)
    local x, y, z = p:position()
    local e = risen.entity.spawn(tpl, x + dx, y + 20, z + dz)
    T.sleep(3)
    return assert(e, "spawn failed: " .. tpl)
end
local function witness(where)
    local w = put("Peasant_1_01", 0, where == "front" and -300 or where == "back" and 300 or -900)
    T.sleep(30)
    return w
end
local function rchest(tpl, locked, ang, items)
    local c = spawn_at(tpl, 200, ang)
    if locked ~= nil and c:get_property("gCLock_PS", "Status") then
        c:set_property("gCLock_PS", "Status", locked and "gELockStatus_Locked" or "gELockStatus_Unlocked")
    end
    for _, it in ipairs(items or { { "It_Apple", 2 }, { "It_Gold", 15 } }) do risen.item.give(c, it[1], it[2]) end
    T.sleep(3)
    return c
end
local function corpse_of_peasant()
    local n = put("Peasant_1_01", 170, 0)
    n:equip_all()
    T.sleep(3)
    risen.script.call("Kill", p, n)
    T.sleep(10)
    return n
end
local function theft_calls(mark) return ev_since(mark, "^AssessTheft%(") end
local function first_index(mark, pattern)
    for i = mark + 1, #rec do if rec[i]:find(pattern) then return i end end
    return nil
end
-- was `before` recorded before `after` (both patterns, since `mark`)?
local function came_before(mark, before, after)
    local a, b = first_index(mark, before), first_index(mark, after)
    return a ~= nil and b ~= nil and a < b
end
local function unique(list)
    local seen, out = {}, {}
    for _, v in ipairs(list) do if not seen[v] then seen[v] = true; out[#out + 1] = v end end
    table.sort(out)
    return out
end
local function describe(s) return ("%s/%s/%s/%s"):format(tostring(s.state), tostring(s.mode), tostring(s.comment), tostring(s.reason)) end

local function watched_area()
    local x, y, z = p:position() --[[@as number]]
    local zone = assert(risen.entity.spawn("AreaZone_Default", x, y, z), "AreaZone_Default did not spawn")
    T.sleep(5)
    local okset = zone:set_area_names({ "SL_Public" })
    local names = zone:area_names()
    return zone, okset and names and #names == 1 and names[1] == "SL_Public", names and table.concat(names, ",") or "nil"
end

local function react_phase()
    observe_theft()

    T.section("outside a watched area")
    local w = witness("front")
    local a = put("It_Apple", 0, 100)
    local mark = #rec
    local r = try(a)
    T.sleep(40)
    check("outside a watched area: quick take handled, nobody reacts", r.handled and r.assessed == true and r.reacted == false and not reacted(snap(w)),
        tostring(r.reason) .. " " .. describe(snap(w)) .. " reacted=" .. tostring(r.reacted))
    w:kill(); T.sleep(10)

    T.section("the watched area")
    local zone, zone_ok, zone_names = watched_area()
    check("zone: the area names were written and read back", zone_ok, zone_names)
    w = witness("front")
    a = put("It_Apple", 0, 100)
    local far_apple = put("It_Apple", 0, 900)
    local inside, outside = risen.script.call("AssessTheft", w, a), risen.script.call("AssessTheft", w, far_apple)
    check("engine rule: AssessTheft answers 1 inside the area and 0 outside", inside == 1 and outside == 0, ("inside %s, outside %s"):format(tostring(inside), tostring(outside)))
    local sees_w, why_w = risen.world.can_see(w, p)
    check("witness faces the player", sees_w == true, tostring(why_w))
    w:kill(); T.sleep(10)

    T.section("items")
    w = witness("front")
    a = put("It_Apple", 0, 100)
    mark = #rec
    engine_interaction(a, false)
    T.sleep(40)
    local anim_item, anim_item_calls = snap(w), theft_calls(mark)
    check("animated item: the witness reacted", reacted(anim_item), describe(anim_item))
    check("animated item: AssessTheft asked before the pick-up, once per human", #anim_item_calls == 1 and came_before(mark, "^AssessTheft%(", "^item%.picked_up"), table.concat(anim_item_calls, " | "))
    w:kill(); T.sleep(10)

    w = witness("front")
    a = put("It_Apple", 0, 100)
    local apples = risen.item.count(p, "It_Apple")
    mark = #rec
    r = try(a)
    T.sleep(60)
    local quick_item, quick_item_calls = snap(w), theft_calls(mark)
    check("quick item: handled, assessed, an NPC reacted", r.handled and r.assessed == true and r.reacted == true and risen.item.count(p, "It_Apple") == apples + 1,
        tostring(r.reason) .. " assessed=" .. tostring(r.assessed) .. " reacted=" .. tostring(r.reacted))
    T.eq("quick item: the witness reacted like in the animated path", describe(quick_item), describe(anim_item))
    check("quick item: the same AssessTheft calls and order as the animated path", #quick_item_calls == 1 and quick_item_calls[1] == anim_item_calls[1] and came_before(mark, "^AssessTheft%(", "^item%.picked_up"),
        table.concat(quick_item_calls, " | ") .. " vs " .. table.concat(anim_item_calls, " | "))
    w:kill(); T.sleep(10)

    T.section("chests")
    local c1 = rchest("Object_Interact_Animated_Chest_Poor", false, 57.3)
    w = witness("front")
    mark = #rec
    engine_interaction(c1, true)
    T.sleep(40)
    local anim_chest, anim_chest_calls = snap(w), theft_calls(mark)
    check("animated chest: the witness reacted", reacted(anim_chest), describe(anim_chest) .. "; calls: " .. table.concat(anim_chest_calls, " | "))
    w:kill(); T.sleep(10)

    local c2 = rchest("Object_Interact_Animated_Chest_Poor", false, 120)
    w = witness("front")
    apples = risen.item.count(p, "It_Apple")
    mark = #rec
    r = try(c2)
    T.sleep(60)
    local quick_chest, quick_chest_calls = snap(w), theft_calls(mark)
    check("quick chest: handled and the witness reacted like in the animated path", r.handled and r.assessed == true and r.reacted == true and risen.item.count(p, "It_Apple") == apples + 2 and describe(quick_chest) == describe(anim_chest),
        tostring(r.reason) .. " quick " .. describe(quick_chest) .. " animated " .. describe(anim_chest))
    -- the animated path asks again while the player stands at the chest (AssessPlayerLooting's sweeps call the very same script): the set of calls is the comparison
    T.eq("quick chest: the same AssessTheft calls as the animated path", unique(quick_chest_calls), unique(anim_chest_calls))
    check("quick chest: asked before anything was taken", came_before(mark, "^AssessTheft%(", "^inventory%.added%(PC_Hero"), table.concat(quick_chest_calls, " | "))
    w:kill(); T.sleep(10)

    local cup = rchest("Object_Interact_Animated_Cupboard", nil, 200)
    w = witness("front")
    apples = risen.item.count(p, "It_Apple")
    mark = #rec
    r = try(cup)
    T.sleep(60)
    check("cupboard: quick take, the witness reacted", r.handled and r.assessed == true and risen.item.count(p, "It_Apple") == apples + 2 and reacted(snap(w)) and #theft_calls(mark) == 1, tostring(r.reason) .. " " .. describe(snap(w)))
    w:kill(); T.sleep(10)

    local sar1 = rchest("Object_Interact_Sarcophagus", nil, 57.3)
    w = witness("front")
    mark = #rec
    r = try(sar1)
    T.sleep(40)
    local quick_sar_calls, quick_sar = theft_calls(mark), snap(w)
    w:kill(); T.sleep(10)
    local sar2 = rchest("Object_Interact_Sarcophagus", nil, 57.3)
    w = witness("front")
    mark = #rec
    engine_interaction(sar2, true)
    T.sleep(40)
    local anim_sar_calls, anim_sar = theft_calls(mark), snap(w)
    check("sarcophagus: neither path asks the NPCs", r.handled and r.assessed == nil and #quick_sar_calls == 0 and not reacted(quick_sar) and #anim_sar_calls == 0 and not reacted(anim_sar),
        ("quick calls %d (%s), animated calls %d (%s)"):format(#quick_sar_calls, describe(quick_sar), #anim_sar_calls, describe(anim_sar)))
    w:kill(); T.sleep(10)

    T.section("corpses")
    local b1 = corpse_of_peasant()
    local b2 = corpse_of_peasant()
    w = witness("front")
    mark = #rec
    local club = risen.item.count(p, "It_1H_Club")
    r = try(b1)
    T.sleep(40)
    check("corpse: quick loot is no theft", r.handled and r.kind == "corpse" and r.assessed == nil and risen.item.count(p, "It_1H_Club") == club + 1 and #theft_calls(mark) == 0 and not reacted(snap(w)),
        tostring(r.reason) .. " " .. describe(snap(w)) .. " calls " .. #theft_calls(mark))
    mark = #rec
    engine_interaction(b2, true)
    T.sleep(40)
    check("corpse: animated loot is no theft either", risen.item.count(p, "It_1H_Club") == club + 2 and #theft_calls(mark) == 0 and not reacted(snap(w)), describe(snap(w)) .. " calls " .. #theft_calls(mark))
    w:kill(); T.sleep(10)

    T.section("out of sight, out of range, nobody")
    w = witness("back")
    a = put("It_Apple", 0, 100)
    apples = risen.item.count(p, "It_Apple")
    local sees = risen.world.can_see(w, p)
    mark = #rec
    r = try(a)
    T.sleep(60)
    check("witness behind the player: taken, nobody reacts", sees == false and r.handled and r.assessed == true and risen.item.count(p, "It_Apple") == apples + 1 and not reacted(snap(w)),
        ("can_see %s; %s; %s"):format(tostring(sees), tostring(r.reason), describe(snap(w))))
    w:kill(); T.sleep(10)

    w = witness("far")
    a = put("It_Apple", 0, 100)
    apples = risen.item.count(p, "It_Apple")
    local far_d = w:distance_to(p)
    r = try(a)
    T.sleep(60)
    check("witness out of range: taken, nobody reacts", far_d > 800 and r.handled and r.assessed == true and risen.item.count(p, "It_Apple") == apples + 1 and not reacted(snap(w)),
        ("distance %.0f; %s; %s"):format(far_d, tostring(r.reason), describe(snap(w))))
    w:kill(); T.sleep(10)

    a = put("It_Apple", 0, 100)
    apples = risen.item.count(p, "It_Apple")
    mark = #rec
    r = try(a)
    T.sleep(20)
    check("nobody around: taken, asked, nobody reacts", r.handled and r.assessed == true and r.reacted == false and risen.item.count(p, "It_Apple") == apples + 1, tostring(r.reason) .. " reacted=" .. tostring(r.reacted))

    T.section("what is not theft")
    w = witness("front")
    risen.item.give(p, "It_Apple", 1)
    T.sleep(5)
    local dropped = risen.item.drop(p, "It_Apple", 1)
    T.sleep(10)
    apples = risen.item.count(p, "It_Apple")
    mark = #rec
    r = dropped and try(dropped) or {}
    T.sleep(60)
    check("dropped item: no theft", dropped ~= nil and r.handled and risen.item.count(p, "It_Apple") == apples + 1 and not reacted(snap(w)), tostring(r.reason) .. " " .. describe(snap(w)))
    local locked = rchest("Object_Interact_Animated_Chest_Poor", true, 120)
    mark = #rec
    r = try(locked)
    T.sleep(40)
    check("declined chest alarms nobody", not r.handled and #theft_calls(mark) == 0 and not reacted(snap(w)), tostring(r.reason) .. " " .. describe(snap(w)))
    w:kill()
end

-- witness_mode = "ignore" (fixtures/ignore/config.toml): the same watched area, nobody is asked
local function ignore_phase()
    observe_theft()
    watched_area()
    local w = witness("front")
    local a = put("It_Apple", 0, 100)
    local apples = risen.item.count(p, "It_Apple")
    local mark = #rec
    local fx_a = risen.effect.count("eff_item_apple_transfer")
    local r = try(a)
    T.sleep(60)
    check("ignore: pickup_sound = false starts no pick-up effect", r.handled and risen.effect.count("eff_item_apple_transfer") == fx_a, ("%d -> %d"):format(fx_a, risen.effect.count("eff_item_apple_transfer")))
    check("ignore: item taken in sight of a witness in a watched area", r.handled and r.assessed == nil and risen.item.count(p, "It_Apple") == apples + 1, tostring(r.reason))
    check("ignore: nobody was asked and nobody reacted", #theft_calls(mark) == 0 and not reacted(snap(w)), describe(snap(w)) .. " calls " .. #theft_calls(mark))
    local c = rchest("Object_Interact_Animated_Chest_Poor", false, 120)
    mark = #rec
    r = try(c)
    T.sleep(40)
    check("ignore: chest taken, nobody asked", r.handled and r.assessed == nil and #theft_calls(mark) == 0 and not reacted(snap(w)), tostring(r.reason) .. " " .. describe(snap(w)))
end

-- witness_mode = "veto" (fixtures/config/config.toml, together with the key options): a witness who looks at the player stops the chest looting
local function veto_section()
    observe_theft()
    T.section("veto")
    local w = witness("front")
    local c = rchest("Object_Interact_Animated_Chest_Poor", false, 120)
    local apples = risen.item.count(p, "It_Apple")
    local mark = #rec
    local r = try(c)
    T.sleep(10)
    check("veto: a witness who sees the player stops the chest looting", not r.handled and (r.reason or ""):find("seen by") ~= nil and risen.world.can_see(w, p) == true, tostring(r.reason))
    check("veto: nothing was taken, nobody was asked", risen.item.count(p, "It_Apple") == apples and #theft_calls(mark) == 0 and r.assessed == nil, "apples " .. apples .. " -> " .. risen.item.count(p, "It_Apple"))
    w:kill(); T.sleep(10)
    w = witness("back")
    r = try(c)
    T.sleep(20)
    check("veto: a witness behind the player does not stop it", r.handled and risen.world.can_see(w, p) == false and risen.item.count(p, "It_Apple") == apples + 2, tostring(r.reason))
    w:kill()
end

T.run(function()
    new_game_ready()
    if phase == "config" then config_phase(); veto_section()
    elseif phase == "react" then react_phase()
    elseif phase == "ignore" then ignore_phase()
    else default_phase() end
end, phase == "react" and "quickloot_react" or phase == "ignore" and "quickloot_react" or "quickloot")
