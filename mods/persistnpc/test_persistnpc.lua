-- Live scenario `persistnpc` of mods/persistnpc (only loaded in a test run). One game process per phase (scripts/smoke-test.ps1):
--   save          new game: the four declared NPCs exist, dialogue + quest work, state is changed (group, items, a death, a move), quick save,
--                 quick load in the same process (restored by ID, no duplicates, dead stays dead, the phoenix is replaced), then a new game
--   load          a fresh process loads that quick save: the same
--   load_changed  a fresh process whose mod declares Hob with version 2 and no longer declares the Wanderer
--   load_missing  a fresh process whose mod lost its NPC templates
-- The savegame stores the NPCs themselves (docs/RE.md section 32); what the scenario proves is that nothing is created twice and nothing is lost.
local T = risen.test

return function(phase, QUEST)
    local TEMPLATE = { PN_Hob = "Spass_PN_Hob", PN_Victim = "Spass_PN_Victim", PN_Phoenix = "Spass_PN_Phoenix", PN_Wanderer = "Spass_PN_Wanderer" }
    local NAMES = { "PN_Hob", "PN_Victim", "PN_Phoenix", "PN_Wanderer" }

    local spawned = {}
    risen.on("entity.spawned", function(_, tpl) spawned[tpl] = (spawned[tpl] or 0) + 1 end)

    local function count(tpl)
        local t = risen.template.get(tpl)
        local list = t and t:instances(200)
        return list and #list or -1
    end
    local function counts()
        local out = {}
        for _, n in ipairs(NAMES) do out[#out + 1] = count(TEMPLATE[n]) end
        return table.concat(out, ",")
    end
    local function status() local q = risen.quest.get(QUEST) return q and q:get_property("Status") or "missing" end
    local function offered()
        local names = {}
        for _, info in ipairs(risen.dialog.available()) do names[#names + 1] = info:name() end
        return table.concat(names, ",")
    end
    local function converse(player, npc, info_name)
        risen.dialog.start(player, npc)
        T.sleep(90)
        assert(risen.dialog.get(info_name), info_name):run()
        T.wait_event("info.finished", 60, function(name) return name == info_name end)
        T.sleep(60)
    end
    local function kill(victim)
        risen.script.call("Kill", risen.entity.player(), victim, 0)
        T.sleep(30)
    end
    local function wait_npcs(names)
        T.wait_until(function()
            for _, n in ipairs(names or NAMES) do if not risen.entity.get(n) then return false end end
            return true
        end, 40, "the persistent NPCs exist")
        T.sleep(100)
    end
    local function ids()
        local t = {}
        for _, n in ipairs(NAMES) do local e = risen.entity.get(n) t[n] = e and e:id() or false end
        return t
    end
    local function close(a, b, tol) return a ~= nil and b ~= nil and math.abs(a - b) <= (tol or 5) end
    local function apples(e) return risen.item.count(e, "It_Apple") end
    local function reasons() return table.concat(ready_reasons, ",") end
    local function save_game()
        T.wait_until(function() return risen.world.is_save_allowed() end, 20, "saving allowed")
        local saved = false
        risen.on("save.after", function(_, ok) saved = ok end)
        risen.world.quick_save()
        T.wait_until(function() return saved end, 60, "savegame written")
        return saved
    end

    -- ---- the checks every load of the saved game has to pass (in-process load and fresh process) -------------------------------------------
    local function check_loaded(saved, tag)
        local hob, victim, phoenix, wanderer = risen.entity.get("PN_Hob"), risen.entity.get("PN_Victim"), risen.entity.get("PN_Phoenix"), risen.entity.get("PN_Wanderer")
        T.expect(tag .. ": all four exist", hob and victim and phoenix and wanderer, counts())
        T.eq(tag .. ": one live entity per NPC (no duplicates)", counts(), "1,1,1,1")
        T.eq(tag .. ": Hob is the saved entity (same ID)", hob and hob:id(), saved.ids.PN_Hob)
        T.eq(tag .. ": Hob keeps the changed group", hob and hob:group(), saved.hob_group)
        T.eq(tag .. ": Hob keeps the changed inventory", hob and apples(hob), saved.hob_apples)
        T.expect(tag .. ": Wanderer keeps its position (2500 units from home)", wanderer and close(select(1, wanderer:position()), saved.wander[1], 400) and close(select(3, wanderer:position()), saved.wander[3], 400),
            ("saved %.0f,%.0f now %s,%s"):format(saved.wander[1], saved.wander[3], tostring(wanderer and select(1, wanderer:position())), tostring(wanderer and select(3, wanderer:position()))))
        T.eq(tag .. ": Victim is the saved corpse (same ID) and still dead", { victim and victim:id(), victim and victim:is_dead() }, { saved.ids.PN_Victim, true })
        T.expect(tag .. ": Phoenix was replaced by a living one", phoenix and not phoenix:is_dead() and phoenix:id() ~= saved.ids.PN_Phoenix,
            phoenix and (tostring(phoenix:id()) .. " dead=" .. tostring(phoenix:is_dead())) or "missing")
        T.eq(tag .. ": the quest state came back", status():find("Running") ~= nil, true)
    end

    local function finish_quest(tag, player)
        local hob = assert(risen.entity.get("PN_Hob"))
        risen.dialog.start(player, hob)
        T.sleep(90)
        local list = offered()
        T.expect(tag .. ": the option of the running quest is offered by the restored Hob", list:find("PN_Hob_Done", 1, true) ~= nil, list)
        assert(risen.dialog.get("PN_Hob_Done")):run()
        T.wait_event("info.finished", 60, function(name) return name == "PN_Hob_Done" end)
        T.sleep(60)
        T.expect(tag .. ": the quest can be finished", (status() or ""):find("Success") ~= nil, tostring(status()))
    end

    if phase == "save" then
        T.plan {
            "new game: all four exist", "new game: one live entity per template", "new game: entity.spawned once per NPC", "new game: on_ready says spawned",
            "new game: records hold the entity IDs", "new game: label", "new game: Hob is made as declared", "new game: dialogue options defined", "new game: quest not started",
            "work option starts the quest", "group set", "victim is dead", "phoenix is dead", "wanderer teleported to the marker", "wanderer was moved", "three plain spawns exist", "saved",
            "loaded: all four exist", "loaded: one live entity per NPC (no duplicates)", "loaded: Hob is the saved entity (same ID)", "loaded: Hob keeps the changed group",
            "loaded: Hob keeps the changed inventory", "loaded: Wanderer keeps its position (2500 units from home)", "loaded: Victim is the saved corpse (same ID) and still dead",
            "loaded: Phoenix was replaced by a living one", "loaded: the quest state came back",
            "loaded: no entity.spawned for restored NPCs", "loaded: on_ready says restored", "loaded: the option of the running quest is offered by the restored Hob",
            "loaded: the quest can be finished", "loaded: only the plain spawn was saved (temporary / save_relevant=false are not)", "a new game starts clean: one live entity per NPC", "a new game starts clean: Victim is alive", "a new game starts clean: quest not running",
            "a new game starts clean: records are new",
        }
        T.run(function()
            T.wait_event("session.start", 180, function(m) return m == "new" end)
            wait_npcs()
            local player = assert(risen.entity.player())
            local hob = assert(risen.entity.get("PN_Hob"))
            T.expect("new game: all four exist", risen.entity.get("PN_Victim") and risen.entity.get("PN_Phoenix") and risen.entity.get("PN_Wanderer"), counts())
            T.eq("new game: one live entity per template", counts(), "1,1,1,1")
            T.eq("new game: entity.spawned once per NPC", { spawned.Spass_PN_Hob, spawned.Spass_PN_Victim, spawned.Spass_PN_Phoenix, spawned.Spass_PN_Wanderer }, { 1, 1, 1, 1 })
            T.eq("new game: on_ready says spawned", reasons(), "PN_Hob:spawned,PN_Victim:spawned,PN_Phoenix:spawned,PN_Wanderer:spawned")
            local rec = risen.save.get("persistent_npcs") or {}
            T.eq("new game: records hold the entity IDs", { rec.PN_Hob and rec.PN_Hob.id, rec.PN_Wanderer and rec.PN_Wanderer.id },
                { hob:id(), risen.entity.get("PN_Wanderer"):id() })
            T.eq("new game: label", hob:focus_name(), "Hob the Persistent")
            T.eq("new game: Hob is made as declared", { hob:level(), hob:hp_max(), apples(hob), hob:guild() }, { 4, 321, 3, 4 })
            T.expect("new game: dialogue options defined", risen.dialog.get("PN_Hob_Work") ~= nil and risen.dialog.get("PN_Hob_Done") ~= nil)
            T.eq("new game: quest not started", risen.quest.is_running(QUEST), false)

            converse(player, hob, "PN_Hob_Work")
            T.expect("work option starts the quest", risen.quest.is_running(QUEST), tostring(status()))

            -- change what the savegame has to keep: group (hit points regenerate by themselves) and inventory of Hob, a death, a death that is replaced, a move
            T.expect("group set", hob:set_group("PN_Crew"))
            risen.item.give(hob, "It_Apple", 2)
            local victim, phoenix, wanderer = assert(risen.entity.get("PN_Victim")), assert(risen.entity.get("PN_Phoenix")), assert(risen.entity.get("PN_Wanderer"))
            kill(victim)
            kill(phoenix)
            T.expect("victim is dead", victim:is_dead())
            T.expect("phoenix is dead", phoenix:is_dead())
            local wx0, _, wz0 = wanderer:position()
            -- far from where its AI would take it in a few seconds (a peasant wanders ~200 units): next to an apple 2500 units away
            local px, py, pz = player:position()
            local marker = assert(risen.entity.spawn("It_Apple", (px or 0) + 2500, (py or 0) + 20, pz or 0))
            T.sleep(30)
            T.expect("wanderer teleported to the marker", wanderer:teleport(marker))
            T.sleep(60)
            local wx, wy, wz = wanderer:position()
            T.expect("wanderer was moved", wx ~= nil and (math.abs(wx - wx0) > 2000), ("%.0f,%.0f -> %.0f,%.0f"):format(wx0, wz0, wx or 0, wz or 0))
            -- the three kinds of spawn: plain (saved by the engine), temporary (never saved), save_relevant(false) (skipped)
            local function plain_peasants() local n = 0 for _, e in ipairs(risen.entity.npcs(player, 700)) do local t = e:template() if t and t:name() == "Peasant_1_03" then n = n + 1 end end return n end
            local base_n = plain_peasants()
            local ax, ay, az = player:position()
            local plain = assert(risen.entity.spawn("Peasant_1_03", (ax or 0) + 100, (ay or 0) + 20, (az or 0) + 400))
            assert(risen.entity.spawn("Peasant_1_03", (ax or 0) + 200, (ay or 0) + 20, (az or 0) + 400, true))
            local skipped = assert(risen.entity.spawn("Peasant_1_03", (ax or 0) + 300, (ay or 0) + 20, (az or 0) + 400))
            skipped:set_save_relevant(false)
            T.sleep(30)
            T.eq("three plain spawns exist", plain_peasants() - base_n, 3, 0)
            local saved = { ids = ids(), hob_group = hob:group(), hob_apples = apples(hob), wander = { wx, wy, wz }, base_n = base_n, plain_id = plain:id() }
            risen.save.set("test", saved)

            T.expect("saved", save_game())
            T.sleep(100)

            -- ---- quick load in the same process ----
            local before = { hob = spawned.Spass_PN_Hob, victim = spawned.Spass_PN_Victim, phoenix = spawned.Spass_PN_Phoenix, wanderer = spawned.Spass_PN_Wanderer }
            ready_reasons = {}
            risen.world.quick_load()
            T.wait_event("session.start", 120, function(m) return m == "load" end)
            wait_npcs()
            T.sleep(60)
            player = assert(risen.entity.player())
            check_loaded(saved, "loaded")
            T.eq("loaded: no entity.spawned for restored NPCs", { spawned.Spass_PN_Hob - before.hob, spawned.Spass_PN_Victim - before.victim,
                spawned.Spass_PN_Phoenix - before.phoenix, spawned.Spass_PN_Wanderer - before.wanderer }, { 0, 0, 1, 0 })
            T.eq("loaded: on_ready says restored", reasons(), "PN_Hob:restored,PN_Phoenix:respawned,PN_Wanderer:restored")
            finish_quest("loaded", player)
            local plain_after = 0
            for _, e in ipairs(risen.entity.npcs(player, 700)) do local t = e:template() if t and t:name() == "Peasant_1_03" then plain_after = plain_after + 1 end end
            T.eq("loaded: only the plain spawn was saved (temporary / save_relevant=false are not)", { plain_after - saved.base_n, risen.entity.by_id(saved.plain_id) ~= nil }, { 1, true })

            -- ---- a new game afterwards: nothing of the loaded world is left, nothing remembered ----
            risen.world.new_game()
            T.wait_event("session.start", 180, function(m) return m == "new" end)
            wait_npcs()
            T.eq("a new game starts clean: one live entity per NPC", counts(), "1,1,1,1")
            local victim2 = risen.entity.get("PN_Victim")
            T.expect("a new game starts clean: Victim is alive", victim2 and not victim2:is_dead() and victim2:id() ~= saved.ids.PN_Victim)
            T.eq("a new game starts clean: quest not running", risen.quest.is_running(QUEST), false)
            local rec2 = risen.save.get("persistent_npcs") or {}
            T.eq("a new game starts clean: records are new", { rec2.PN_Victim ~= nil and not rec2.PN_Victim.dead, rec2.PN_Hob and rec2.PN_Hob.id == risen.entity.get("PN_Hob"):id() }, { true, true })
        end, "persistnpc")
        return
    end

    if phase == "load" then
        T.plan {
            "loaded (fresh process)", "fresh: all four exist", "fresh: one live entity per NPC (no duplicates)", "fresh: Hob is the saved entity (same ID)",
            "fresh: Hob keeps the changed group", "fresh: Hob keeps the changed inventory", "fresh: Wanderer keeps its position (2500 units from home)",
            "fresh: Victim is the saved corpse (same ID) and still dead", "fresh: Phoenix was replaced by a living one", "fresh: the quest state came back",
            "fresh: on_ready says restored", "fresh: the option of the running quest is offered by the restored Hob", "fresh: the quest can be finished",
        }
        T.run(function()
            local mode = T.wait_event("session.start", 180, function(m) return m == "load" end)
            T.expect("loaded (fresh process)", mode == "load", tostring(mode))
            wait_npcs()
            T.sleep(60)
            local saved = assert(risen.save.get("test"), "the mod data of the savegame")
            check_loaded(saved, "fresh")
            T.eq("fresh: on_ready says restored", reasons(), "PN_Hob:restored,PN_Phoenix:respawned,PN_Wanderer:restored")
            finish_quest("fresh", assert(risen.entity.player()))
        end, "persistnpc")
        return
    end

    if phase == "load_changed" then
        T.plan {
            "changed: loaded", "changed: Hob was replaced (version 2, new entity)", "changed: only one Hob", "changed: the replacement is made as declared",
            "changed: Wanderer is no longer declared: entity removed", "changed: its record is dropped", "changed: Victim is still the saved corpse",
            "changed: Phoenix is alive", "changed: quest state is the savegame's", "changed: the new Hob offers the dialogue option",
        }
        T.run(function()
            local mode = T.wait_event("session.start", 180, function(m) return m == "load" end)
            T.expect("changed: loaded", mode == "load", tostring(mode))
            wait_npcs({ "PN_Hob", "PN_Victim", "PN_Phoenix" })
            T.sleep(60)
            local saved = assert(risen.save.get("test"), "the mod data of the savegame")
            local hob = assert(risen.entity.get("PN_Hob"))
            T.expect("changed: Hob was replaced (version 2, new entity)", hob and hob:id() ~= saved.ids.PN_Hob and ready_reasons[1] == "PN_Hob:replaced",
                hob and (tostring(hob:id()) .. " " .. reasons()) or "missing")
            T.eq("changed: only one Hob", count("Spass_PN_Hob"), 1)
            T.eq("changed: the replacement is made as declared", { hob and apples(hob), hob and hob:hp_max() }, { 3, 321 })
            T.eq("changed: Wanderer is no longer declared: entity removed", { count("Spass_PN_Wanderer"), risen.entity.get("PN_Wanderer") == nil }, { 0, true })
            local rec = risen.save.get("persistent_npcs") or {}
            T.eq("changed: its record is dropped", rec.PN_Wanderer, nil)
            local victim = risen.entity.get("PN_Victim")
            T.eq("changed: Victim is still the saved corpse", { victim and victim:id(), victim and victim:is_dead() }, { saved.ids.PN_Victim, true })
            local phoenix = risen.entity.get("PN_Phoenix")
            T.expect("changed: Phoenix is alive", phoenix and not phoenix:is_dead())
            T.expect("changed: quest state is the savegame's", status():find("Running") ~= nil, tostring(status()))
            local player = assert(risen.entity.player())
            risen.dialog.start(player, hob)
            T.sleep(90)
            T.expect("changed: the new Hob offers the dialogue option", offered():find("PN_Hob_Done", 1, true) ~= nil, offered())
        end, "persistnpc")
        return
    end

    -- load_missing: the mod's NPC templates do not exist in this process
    T.plan { "missing: loaded", "missing: no NPC entity of the lost templates", "missing: nothing was spawned", "missing: handles report the loss", "missing: the records are kept", "missing: the game runs" }
    T.run(function()
        local mode = T.wait_event("session.start", 180, function(m) return m == "load" end)
        T.expect("missing: loaded", mode == "load", tostring(mode))
        T.sleep(250)
        T.eq("missing: no NPC entity of the lost templates", { risen.entity.get("PN_Hob"), risen.entity.get("PN_Victim"), risen.entity.get("PN_Phoenix"), risen.entity.get("PN_Wanderer") },
            { nil, nil, nil, nil })
        T.eq("missing: nothing was spawned", reasons(), "")
        local state = {}
        for _, h in ipairs(risen.npc.persistent_list()) do state[#state + 1] = h.name .. "=" .. h:state() end
        T.eq("missing: handles report the loss", table.concat(state, ","), "PN_Hob=missing,PN_Victim=dead,PN_Phoenix=missing,PN_Wanderer=missing")
        local rec = risen.save.get("persistent_npcs") or {}
        T.expect("missing: the records are kept", rec.PN_Hob ~= nil and rec.PN_Wanderer ~= nil and rec.PN_Victim and rec.PN_Victim.dead == true)
        local player = risen.entity.player()
        T.expect("missing: the game runs", player ~= nil and not player:is_dead() and (player:hp() or 0) > 0)
    end, "persistnpc")
end
