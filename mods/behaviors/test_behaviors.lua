---@diagnostic disable: need-check-nil, param-type-mismatch, assign-type-mismatch
-- Live scenario `behaviors` of mods/behaviors (only loaded in a test run), one process: a new game, the behaviours on persistent NPCs and on template NPCs, their events
-- (player near / far, a dialogue, items, a kill), an error, attachments made during the session; quick save, quick load in the same process: states resumed.
local T = risen.test

return function(rec)
    local function has(prefix)
        for _, l in ipairs(rec.log) do if l.text:sub(1, #prefix) == prefix then return true end end
        return false
    end
    local function count(prefix)
        local n = 0
        for _, l in ipairs(rec.log) do if l.text:sub(1, #prefix) == prefix then n = n + 1 end end
        return n
    end
    local function find(prefix)
        for _, l in ipairs(rec.log) do if l.text:sub(1, #prefix) == prefix then return l.text end end
        return "(none)"
    end
    local function save_game()
        T.wait_until(function() return risen.world.is_save_allowed() end, 30, "saving allowed")
        local saved = false
        local h = risen.on("save.after", function(_, ok) saved = ok end)
        risen.world.quick_save()
        T.wait_until(function() return saved end, 60, "savegame written")
        risen.off(h)
        return saved
    end
    local function place(e, dx, dz)
        local px, py, pz = risen.entity.player():position()
        return e:set_pose({ x = px + dx, y = py + 20, z = pz + dz })
    end

    T.run(function()
        T.wait_event("session.start", 180, function(m) return m == "new" end)
        T.wait_until(function() return risen.behavior.get("BHV_Guard", "bhv_counter") ~= nil end, 60, "the guard's behaviours started")
        local guard = assert(risen.entity.get("BHV_Guard"))
        local player = assert(risen.entity.player())

        -- ---- refusals ---------------------------------------------------------------------------------------------------------------------------------
        T.section("refusals")
        local cases = {
            { "bad name", risen.behavior.define("1x", {}), "behaviour name" },
            { "unknown key", risen.behavior.define("x", { updat = function() end }), "unknown key" },
            { "not a function", risen.behavior.define("x", { update = 3 }), "must be a function" },
            { "unsaveable params", risen.behavior.define("x", { params = { e = guard } }), "plain values" },
            { "unknown behaviour", risen.behavior.attach("BHV_Guard", "no_such"), "no behaviour" },
            { "bad target", risen.behavior.attach(42, "bhv_counter"), "expected an NPC" },
        }
        for _, c in ipairs(cases) do T.expect("refused: " .. c[1], c[2] == nil) end
        local okr, why = risen.behavior.define("x", { updat = function() end })
        T.expect("refusals say why", okr == nil and type(why) == "string" and why:find("unknown key 'updat'") ~= nil, tostring(why))

        -- ---- start, cadence, params, state ------------------------------------------------------------------------------------------------------------
        T.section("lifecycle")
        T.expect("started on the persistent NPC with merged params", has("start bhv_counter BHV_Guard false 2 default 0"), find("start bhv_counter"))
        T.expect("the events behaviour started on both NPCs", has("start bhv_events BHV_Guard") and has("start bhv_events BHV_Victim"))
        local inst = risen.behavior.get("BHV_Guard", "bhv_counter")
        local n0, dts0 = inst.state.n or 0, #(rec.counter_dt or {})
        T.sleep(240)
        local updates = #(rec.counter_dt or {}) - dts0
        local elapsed = 0
        for i = dts0 + 1, #rec.counter_dt do elapsed = elapsed + rec.counter_dt[i] end
        T.expect("update about every 0.5 s", updates >= 3 and math.abs(elapsed / updates - 0.5) < 0.15, ("%d updates, %.3f s each"):format(updates, updates > 0 and elapsed / updates or 0))
        T.eq("the state counts by the param step", (inst.state.n or 0) - n0, updates * 2)
        T.expect("the instance knows its NPC", inst.entity:name() == "BHV_Guard" and inst.time > 1, tostring(inst.time))

        -- ---- an error switches one attachment off -----------------------------------------------------------------------------------------------------
        T.section("errors")
        local fails, last = risen.behavior.failures()
        T.expect("the broken behaviour failed once and is off", fails == 1 and last and last:find("boom in update") ~= nil and risen.behavior.get("BHV_Guard", "bhv_broken") == nil, tostring(last))
        T.expect("the guard's other behaviours go on", risen.behavior.get("BHV_Guard", "bhv_counter") ~= nil and risen.behavior.get("BHV_Guard", "bhv_events") ~= nil)

        -- ---- player near / far ------------------------------------------------------------------------------------------------------------------------
        T.section("near")
        T.expect("not near while 15 m away", not has("near BHV_Guard"))
        T.expect("moved next to the player", place(guard, 200, 0))
        T.wait_until(function() return has("near BHV_Guard") end, 10, "on_player_near")
        T.expect("on_player_near with the player", has("near BHV_Guard true"), find("near BHV_Guard"))
        place(guard, 2000, 0)
        T.wait_until(function() return has("far BHV_Guard") end, 10, "on_player_far")
        T.eq("near / far once each", { count("near BHV_Guard"), count("far BHV_Guard") }, { 1, 1 })
        place(guard, 250, 0)

        -- ---- dialogue and items -----------------------------------------------------------------------------------------------------------------------
        T.section("events")
        risen.dialog.start(player, guard)
        T.sleep(90)
        assert(risen.dialog.get("BHV_Guard_Hello"), "the dialogue"):run()
        T.wait_until(function() return has("talk BHV_Guard") end, 60, "on_talk")
        T.expect("on_talk names the option", has("talk BHV_Guard BHV_Guard_Hello"), find("talk BHV_Guard"))
        T.wait_event("info.finished", 60)
        T.sleep(30)
        risen.item.give(guard, "It_Apple", 2)
        T.wait_until(function() return has("items BHV_Guard It_Apple") end, 10, "on_items")
        T.expect("on_items with delta, total and cause", has("items BHV_Guard It_Apple 2 2 script"), find("items BHV_Guard"))

        -- ---- a quest of the original game, any event ------------------------------------------------------------------------------------------------
        T.section("quests")
        local GQ = "Rufo_GoToDruidHut"
        T.expect("the game's quest exists", risen.quest.get(GQ) ~= nil)
        T.expect("the ambusher waits for the game's quest", risen.entity.get("BHV_Ambusher") == nil)
        risen.quest.open(GQ)
        risen.quest.run(GQ)
        risen.quest.succeed(GQ)
        T.wait_until(function() return has("quest BHV_Guard " .. GQ .. " Success") end, 10, "on_quest of a game quest")
        T.expect("on_quest saw the game quest's states", has("quest BHV_Guard " .. GQ .. " Running") and has("quest BHV_Guard " .. GQ .. " Success"), find("quest BHV_Guard"))
        T.wait_until(function() return risen.entity.get("BHV_Ambusher") ~= nil end, 20, "the ambusher appears after the game's quest was solved")
        T.wait_until(function() return has("start bhv_events BHV_Ambusher") end, 20, "the ambusher's behaviour")
        risen.emit("behaviors.ping", 7)
        T.wait_until(function() return has("ping BHV_Guard 7") end, 5, "a mod event through `events`")

        -- ---- death -----------------------------------------------------------------------------------------------------------------------------------
        T.section("death")
        local victim = assert(risen.entity.get("BHV_Victim"))
        risen.script.call("Kill", player, victim, 0)
        T.wait_until(function() return has("stop bhv_events BHV_Victim died") end, 20, "the victim's behaviour stopped")
        T.expect("on_death with the killer", has("death BHV_Victim true"), find("death BHV_Victim"))
        T.sleep(60)
        T.eq("a dead NPC is not started again", count("start bhv_events BHV_Victim"), 1)

        -- ---- every NPC of a template ------------------------------------------------------------------------------------------------------------------
        T.section("template")
        local px, py, pz = player:position()
        local w1 = assert(risen.npc.spawn("Peasant_1_03", px + 400, py + 20, pz + 400, { peaceful = true }))
        local w2 = assert(risen.npc.spawn("Peasant_1_03", px - 400, py + 20, pz + 400, { peaceful = true }))
        T.wait_until(function() return risen.behavior.get(w1, "bhv_tagged") ~= nil and risen.behavior.get(w2, "bhv_tagged") ~= nil end, 10, "the spawned NPCs got the behaviour")
        T.expect("keyed by ID (no unique name)", risen.behavior.get(w1, "bhv_tagged").key == "@" .. w1:id())
        T.expect("the template attachment counts them", (function()
            for _, a in ipairs(risen.behavior.list()) do if a.kind == "template" and a.target == "Peasant_1_03" then return a.count >= 2 end end
            return false
        end)())
        local w2_id = w2:id()
        w2:kill()
        T.wait_until(function() return has("stop bhv_tagged @" .. w2_id) end, 10, "a removed NPC's behaviour stops")
        T.expect("stopped as removed", has("stop bhv_tagged @" .. w2_id .. " removed"), find("stop bhv_tagged"))

        -- ---- attachments made during the session ------------------------------------------------------------------------------------------------------
        T.section("runtime")
        T.expect("attach with persist", risen.behavior.attach("BHV_Guard", "bhv_runtime", { tag = "kept" }, { persist = true }))
        T.expect("attach for this session", risen.behavior.attach(w1, "bhv_counter", { step = 3 }))
        T.wait_until(function() return has("start bhv_runtime BHV_Guard false kept") end, 5, "the runtime attachment started")
        T.expect("the session attachment runs on the nameless NPC", risen.behavior.get(w1, "bhv_counter") ~= nil)

        -- ---- quick save, quick load: states resumed ---------------------------------------------------------------------------------------------------
        T.section("load")
        T.sleep(60)
        local n_before = risen.behavior.get("BHV_Guard", "bhv_counter").state.n
        local n_after
        local hs = risen.on("save.after", function() n_after = risen.behavior.get("BHV_Guard", "bhv_counter").state.n end)
        local w1_id = w1:id()
        T.expect("saved", save_game())
        risen.off(hs)
        T.sleep(30)
        local mark = #rec.log
        risen.world.quick_load()
        T.wait_event("session.start", 120, function(m) return m == "load" end)
        T.wait_until(function() return risen.behavior.get("BHV_Guard", "bhv_counter") ~= nil end, 60, "the guard's behaviour after the load")
        T.sleep(30)
        local after = {}
        for i = mark + 1, #rec.log do after[#after + 1] = rec.log[i].text end
        local text = table.concat(after, " | ")
        T.expect("no stop while the world was torn down", not text:find("unloaded"), text)
        local n_loaded = tonumber(text:match("start bhv_counter BHV_Guard true 2 default (%d+)"))
        T.expect("resumed with the saved state", n_loaded ~= nil and n_after ~= nil and n_loaded >= n_before and n_loaded <= n_after,
            ("%s (saved between %s and %s)"):format(tostring(n_loaded), tostring(n_before), tostring(n_after)))
        T.expect("the persisted runtime attachment came back, resumed", text:find("start bhv_runtime BHV_Guard true kept", 1, true) ~= nil, text)
        local w1b = risen.entity.by_id(w1_id)
        T.expect("the template NPC got its behaviour again (by ID, state kept)", w1b ~= nil and risen.behavior.get(w1b, "bhv_tagged") ~= nil and risen.behavior.get(w1b, "bhv_tagged").state.tag == 2,
            w1b and risen.behavior.get(w1b, "bhv_tagged") and tostring(risen.behavior.get(w1b, "bhv_tagged").state.tag))
        T.expect("the session-only attachment is gone", w1b ~= nil and risen.behavior.get(w1b, "bhv_counter") == nil)
        T.expect("the dead victim stays without behaviour", risen.behavior.get("BHV_Victim", "bhv_events") == nil)
        T.expect("the ambusher is still there after the load, with his behaviour", risen.entity.get("BHV_Ambusher") ~= nil and risen.behavior.get("BHV_Ambusher", "bhv_events") ~= nil)
        local f2 = risen.behavior.failures()
        T.expect("the broken behaviour is tried again in the new session", f2 == 2, tostring(f2))
    end, "behaviors")
end
