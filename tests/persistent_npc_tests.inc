// ---- persistent NPCs: the Lua part (declarations, records in risen.save, restore by ID, respawn policy, orphans); the engine is mocked --------------------------
// Included by test_main.cpp inside its anonymous namespace.

void setupPersistentNpcMock(sol::state& lua) {
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math);
    lua.script(R"LUA(
        -- a tiny world: entities by id, names registered like the engine does (a restored entity is nameless: its name is the template's)
        world = { entities = {}, by_name = {}, nextid = 1, spawned = 0, killed = 0, templates = { Peasant = true, Wolf = true }, player = { 10, 0, 20 } }
        local function make_handle(ent)
            local h = {}
            function h:name() return ent.name end
            function h:id() return ent.id end
            function h:valid() return world.entities[ent.id] == ent end
            function h:set_name(n)
                world.by_name[ent.name] = nil
                ent.name = n
                world.by_name[n] = ent
                return true
            end
            function h:kill() world.killed = world.killed + 1 world.entities[ent.id] = nil if world.by_name[ent.name] == ent then world.by_name[ent.name] = nil end end
            function h:is_dead() return ent.dead end
            function h:set_save_relevant(on) ent.saved = on return true end
            function h:position() return ent.x, ent.y, ent.z end
            function h:template() return { name = function() return ent.template end } end
            return h
        end
        local function wrap(ent) if ent then return make_handle(ent) end end
        risen = { log = {}, loc = {}, template = {}, entity = {}, npc = {}, trace = nil }
        lines = {}
        for _, l in ipairs { "info", "warn", "error" } do risen.log[l] = function(m) lines[#lines + 1] = l:sub(1, 1):upper() .. " " .. m end end
        function risen.loc.set(id, text) world.loc = world.loc or {} world.loc[id] = text end
        function risen.template.get(n) if world.templates[n] then return { name = function() return n end } end return nil, "unknown" end
        function risen.entity.player() return { position = function() return unpack(world.player) end } end
        function risen.entity.get(n) return wrap(world.by_name[n]) end
        function risen.entity.by_id(id) return wrap(world.entities[id]) end
        function risen.npc.spawn(tpl, x, y, z, o)
            if not world.templates[tpl] then return nil end
            local ent = { id = "id" .. world.nextid, template = tpl, x = x, y = y, z = z, name = o.name or tpl, dead = false, opts = o }
            world.nextid = world.nextid + 1
            world.spawned = world.spawned + 1
            world.entities[ent.id] = ent
            world.by_name[ent.name] = ent
            return wrap(ent)
        end

        -- the per-mod api: handlers, a save store that "load" swaps, off
        handlers, nexth = {}, 1
        store = {}
        api = setmetatable({ log = risen.log,
            on = function(event, fn) local h = nexth nexth = nexth + 1 handlers[#handlers + 1] = { event = event, fn = fn, h = h } return h end,
            off = function(h) for i = #handlers, 1, -1 do if handlers[i].h == h then table.remove(handlers, i) end end end,
            save = { get = function(k) return store[k] end, set = function(k, v) store[k] = v end } }, { __index = risen })
        function fire(event, ...)
            local snapshot = {}
            for i, h in ipairs(handlers) do snapshot[i] = h end
            for _, h in ipairs(snapshot) do if h.event == event then h.fn(...) end end
        end
        function ticks(n) for _ = 1, n do fire("tick") end end
        -- a session that reaches the reconciliation
        function session(mode) fire("session.start", mode) fire("world.ready", mode) ticks(31) end
        -- what the engine does with the world on a quick save + quick load: entities (and their IDs) survive, names do not; the store comes back from the sidecar
        function save_game()
            fire("save.before", "QuickSave")
            saved = { entities = {}, store = load("return " .. serialize(store))() }
            for id, e in pairs(world.entities) do
                local c = {} for k, v in pairs(e) do c[k] = v end
                saved.entities[id] = c
            end
        end
        function serialize(v)
            if type(v) == "table" then
                local out = {}
                for k, x in pairs(v) do out[#out + 1] = "[" .. (type(k) == "number" and tostring(k) or string.format("%q", k)) .. "]=" .. serialize(x) end
                return "{" .. table.concat(out, ",") .. "}"
            elseif type(v) == "string" then return string.format("%q", v) end
            return tostring(v)
        end
        function load_game(keep_names)
            fire("load.before", "QuickSave")
            world.entities, world.by_name = {}, {}
            for id, e in pairs(saved.entities) do
                local c = {} for k, v in pairs(e) do c[k] = v end
                c.name = c.template   -- the savegame does not keep the unique name
                world.entities[id] = c
                world.by_name[c.name] = world.by_name[c.name] or c
            end
            store = load("return " .. serialize(saved.store))()
        end
        function new_game()
            world.entities, world.by_name = {}, {}
            store = {}
        end
        function count(tpl) local n = 0 for _, e in pairs(world.entities) do if e.template == tpl then n = n + 1 end end return n end
        function die(name, killer)
            world.by_name[name].dead = true
            fire("entity.died", wrap(world.by_name[name]), killer, "combat")
        end
    )LUA");
    spass::game::installPersistentNpcModApi(lua, lua["api"], "testmod");
}

TEST(persistent_npc_validates_the_spec_before_doing_anything) {
    sol::state lua;
    setupPersistentNpcMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    CHECK(run("local h, why = api.npc.persistent(5) return tostring(h) .. '|' .. why").find("expects a table") != std::string::npos);
    CHECK(run("local h, why = api.npc.persistent({ name = 'A B', template = 'Peasant', position = {1,2,3} }) return tostring(h) .. '|' .. why").find("name must be a string") != std::string::npos);
    CHECK(run("local h, why = api.npc.persistent({ name = 'A', template = 'Peasant' }) return why").find("exactly one of position") != std::string::npos);
    CHECK(run("local h, why = api.npc.persistent({ name = 'A', template = 'Peasant', position = {1,2,3}, offset = {1,2,3} }) return why").find("exactly one of position") != std::string::npos);
    CHECK(run("local h, why = api.npc.persistent({ name = 'A', template = 'Peasant', position = {1,2} }) return why").find("position must be") != std::string::npos);
    CHECK(run("local h, why = api.npc.persistent({ name = 'A', template = 'Peasant', position = {1,2,3}, colour = 1 }) return why") == "unknown option 'colour'");
    CHECK(run("local h, why = api.npc.persistent({ name = 'A', template = 'Peasant', position = {1,2,3}, respawn = 'x' }) return why").find("respawn must be") != std::string::npos);
    CHECK(run("local h, why = api.npc.persistent({ name = 'A', template = 'Peasant', position = {1,2,3}, version = 1.5 }) return why").find("version must be an integer") != std::string::npos);
    CHECK(run("local h, why = api.npc.persistent({ name = 'A', template = 'Peasant', position = {1,2,3}, on_ready = 3 }) return why").find("on_ready must be a function") != std::string::npos);
    // nothing was declared, nothing registered, nothing spawned
    CHECK(run("return #handlers .. ',' .. world.spawned") == "0,0");
    // a valid one, declared twice, gives the same handle; another mod may not take the name
    CHECK(run("local a = api.npc.persistent({ name = 'A', template = 'Peasant', position = {1,2,3}, label = 'Ann' }) local b = api.npc.persistent({ name = 'A', template = 'Wolf', position = {1,2,3} }) "
              "return tostring(a == b) .. ',' .. tostring(a) .. ',' .. world.loc.FO_A") == "true,PersistentNpc(A),Ann");
    CHECK(run("return tostring(risen.npc._persistent_owner.A)") == "testmod");
    // another mod may not take the name
    lua.script("api2 = setmetatable({ log = risen.log, on = function() return 0 end, off = function() end, save = { get = function() end, set = function() end } }, { __index = risen })");
    spass::game::installPersistentNpcModApi(lua, lua["api2"], "other");
    CHECK(run("local h, why = api2.npc.persistent({ name = 'A', template = 'Peasant', position = {1,2,3} }) return tostring(h) .. '|' .. why") == "nil|'A' is already declared by the mod 'testmod'");
    CHECK(run("local list = api.npc.persistent_list() return #list .. ',' .. tostring(api.npc.persistent_get('A') == list[1]) .. ',' .. tostring(api.npc.persistent_get('Nope'))") == "1,true,nil");
}

TEST(persistent_npc_spawns_once_in_a_new_game_and_defines_content) {
    sol::state lua;
    setupPersistentNpcMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    lua.script(R"LUA(
        defined, ready_log = 0, {}
        h = api.npc.persistent({ name = 'Hob', template = 'Peasant', offset = { 150, 20, 150 }, hp_max = 300, peaceful = true,
            define = function(handle) defined = defined + 1 end,
            on_ready = function(e, reason) ready_log[#ready_log + 1] = reason .. ':' .. e:name() end })
    )LUA");
    CHECK(run("return h:state()") == "pending");
    // the quests/dialogues are defined on load.before and session.start, the NPC only once the world is ready and has settled
    CHECK(run("fire('load.before') fire('session.start', 'new') return defined .. ',' .. world.spawned") == "2,0");
    CHECK(run("fire('world.ready', 'new') ticks(29) return tostring(world.spawned)") == "0");
    CHECK(run("ticks(1) return world.spawned .. ',' .. h:state() .. ',' .. world.by_name.Hob.x .. ',' .. world.by_name.Hob.y .. ',' .. world.by_name.Hob.z") == "1,alive,160,20,170");
    CHECK(run("local o = world.by_name.Hob.opts return o.name .. ',' .. o.hp_max .. ',' .. tostring(o.peaceful) .. ',' .. tostring(world.by_name.Hob.saved)") == "Hob,300,true,true");
    CHECK(run("return table.concat(ready_log, ',') .. '|' .. store.persistent_npcs.Hob.id .. '|' .. store.persistent_npcs.Hob.template .. '|' .. #handlers") ==
          "spawned:Hob|id1|Peasant|" + run("return #handlers"));
    // the tick handler of the settling phase removed itself
    CHECK(run("local n = 0 for _, h in ipairs(handlers) do if h.event == 'tick' then n = n + 1 end end return tostring(n)") == "0");
    // a second reconciliation (another world.ready) finds the entity and does not make another one
    CHECK(run("session('new') return world.spawned .. ',' .. count('Peasant')") == "1,1");
}

TEST(persistent_npc_is_restored_by_id_after_a_load_without_a_duplicate) {
    sol::state lua;
    setupPersistentNpcMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    lua.script(R"LUA(
        reasons = {}
        h = api.npc.persistent({ name = 'Hob', template = 'Peasant', position = { 1, 2, 3 }, on_ready = function(e, reason) reasons[#reasons + 1] = reason end })
        session('new')
        world.by_name.Hob.x = 99          -- it walked away
        save_game()
        world.by_name.Hob.x = 5           -- and further, after the save
    )LUA");
    CHECK(run("return store.persistent_npcs.Hob.last[1] .. ',' .. store.persistent_npcs.Hob.home[1]") == "99,1");
    // load: the entity is back (same id, nameless), then named again; nothing new was spawned
    CHECK(run("load_game() return tostring(world.by_name.Hob) .. ',' .. world.by_name.Peasant.id") == "nil,id1");
    CHECK(run("session('load') return world.spawned .. ',' .. count('Peasant') .. ',' .. world.by_name.Hob.id .. ',' .. world.by_name.Hob.x .. ',' .. tostring(world.by_name.Peasant)") == "1,1,id1,99,nil");
    CHECK(run("return table.concat(reasons, ',')") == "spawned,restored");
    // loading again (the same savegame) is stable
    CHECK(run("load_game() session('load') return world.spawned .. ',' .. count('Peasant') .. ',' .. world.by_name.Hob.x") == "1,1,99");
    // a new game: nothing is remembered, the world is empty -> one fresh NPC
    CHECK(run("new_game() session('new') return world.spawned .. ',' .. count('Peasant') .. ',' .. world.by_name.Hob.id") == "2,1,id2");
}

TEST(persistent_npc_dead_stays_dead_unless_the_respawn_policy_says_otherwise) {
    sol::state lua;
    setupPersistentNpcMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    lua.script(R"LUA(
        deaths = {}
        stay = api.npc.persistent({ name = 'Stay', template = 'Peasant', position = { 1, 2, 3 }, on_death = function(e, killer) deaths[#deaths + 1] = e:name() .. ':' .. tostring(killer) end })
        back = api.npc.persistent({ name = 'Back', template = 'Wolf', position = { 4, 5, 6 }, respawn = true })
        cond_calls = 0
        respawn_ok = false
        maybe = api.npc.persistent({ name = 'Maybe', template = 'Wolf', position = { 7, 8, 9 }, respawn = function(s) cond_calls = cond_calls + 1 return respawn_ok and s.deaths >= 1 end })
        session('new')
        die('Stay', 'player') die('Back', 'player') die('Maybe', 'player')
    )LUA");
    CHECK(run("return table.concat(deaths, ',') .. '|' .. stay:state() .. ',' .. stay:deaths()") == "Stay:player|dead,1");
    CHECK(run("local r = store.persistent_npcs return tostring(r.Stay.dead) .. ',' .. r.Stay.deaths") == "true,1");
    // a death reported twice counts once
    CHECK(run("fire('entity.died', risen.entity.get('Stay'), 'player', 'combat') return stay:deaths()") == "1");
    CHECK(run("save_game() load_game() session('load') "
              "return stay:state() .. ',' .. back:state() .. ',' .. maybe:state() .. ',' .. world.spawned .. ',' .. count('Peasant') .. ',' .. count('Wolf')") == "dead,alive,dead,4,1,2");
    // the corpse of Stay is still there (named again), Back got a new body at its home, Maybe's policy said no
    CHECK(run("return tostring(world.by_name.Stay.dead) .. ',' .. world.by_name.Back.x .. ',' .. tostring(world.by_name.Back.dead) .. ',' .. cond_calls") == "true,4,false,1");
    // the policy function decides at the next reconciliation: not yet for Maybe, so it is still a corpse; once it says yes, the corpse is replaced
    CHECK(run("respawn_ok = true save_game() load_game() session('load') return maybe:state() .. ',' .. count('Wolf') .. ',' .. maybe:deaths()") == "alive,2,1");
    CHECK(run("die('Back', 'player') save_game() load_game() session('load') return back:state() .. ',' .. back:deaths() .. ',' .. count('Wolf')") == "alive,2,2");
}

TEST(persistent_npc_follows_a_changed_declaration) {
    sol::state lua;
    setupPersistentNpcMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    lua.script("api.npc.persistent({ name = 'Hob', template = 'Peasant', position = { 1, 2, 3 } }) session('new') save_game()");
    // the mod (a new process) declares the NPC with another template: the old entity is removed, the new one made at the new spot
    lua.script("handlers = {} declared_before = world.spawned");
    spass::game::installPersistentNpcModApi(lua, lua["api"], "testmod");
    CHECK(run("api.npc.persistent({ name = 'Hob', template = 'Wolf', position = { 9, 9, 9 } }) load_game() session('load') "
              "return count('Peasant') .. ',' .. count('Wolf') .. ',' .. world.by_name.Hob.template .. ',' .. world.by_name.Hob.x .. ',' .. store.persistent_npcs.Hob.template") == "0,1,Wolf,9,Wolf");
    // `version` forces a fresh NPC the same way
    lua.script("handlers = {}");
    spass::game::installPersistentNpcModApi(lua, lua["api"], "testmod");
    CHECK(run("local old = world.by_name.Hob.id save_game() api.npc.persistent({ name = 'Hob', template = 'Wolf', position = { 2, 2, 2 }, version = 2 }) load_game() session('load') "
              "return tostring(world.by_name.Hob.id ~= old) .. ',' .. count('Wolf') .. ',' .. world.by_name.Hob.x .. ',' .. store.persistent_npcs.Hob.version") == "true,1,2,2");
}

TEST(persistent_npc_removes_what_the_mod_no_longer_declares) {
    sol::state lua;
    setupPersistentNpcMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    lua.script("api.npc.persistent({ name = 'Keep', template = 'Peasant', position = { 1, 2, 3 } }) api.npc.persistent({ name = 'Gone', template = 'Wolf', position = { 4, 5, 6 } }) session('new') save_game()");
    // the next process only declares `Keep`
    lua.script("handlers = {}");
    spass::game::installPersistentNpcModApi(lua, lua["api"], "testmod");
    CHECK(run("api.npc.persistent({ name = 'Keep', template = 'Peasant', position = { 1, 2, 3 } }) load_game() session('load') "
              "return count('Peasant') .. ',' .. count('Wolf') .. ',' .. tostring(store.persistent_npcs.Gone) .. ',' .. tostring(store.persistent_npcs.Keep ~= nil)") == "1,0,nil,true");
    // a mod that was removed altogether never runs: the entity just stays in the world (nothing to test), a missing template makes the engine drop it
    CHECK(run("world.templates.Peasant = nil save_game() load_game() world.entities.id1 = nil return count('Peasant')") == "0");
}

TEST(persistent_npc_survives_unavailable_templates_and_players) {
    sol::state lua;
    setupPersistentNpcMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    lua.script("world.templates.Hob_T = nil h = api.npc.persistent({ name = 'Hob', template = 'Hob_T', position = { 1, 2, 3 } })");
    // no such template (yet): a warning, no record, no error, no spawn
    CHECK(run("session('new') return world.spawned .. ',' .. tostring(store.persistent_npcs and store.persistent_npcs.Hob) .. ',' .. h:state() .. ',' .. #lines") == "0,nil,pending,1");
    CHECK(run("return lines[1]:sub(1, 2) .. tostring(lines[1]:find('template .Hob_T. does not exist', 1, false) ~= nil)") == "W true");
    // it appears later (mods that create templates on the first tick, a later session): the next reconciliation spawns it, once
    CHECK(run("world.templates.Hob_T = true session('new') session('new') return world.spawned .. ',' .. h:state()") == "1,alive");
    // an `offset` NPC waits for the player; a position function receives the player
    lua.script("world.player = nil risen.entity.player = function() return nil end "
               "h2 = api.npc.persistent({ name = 'Ann', template = 'Peasant', offset = { 1, 1, 1 } }) "
               "h3 = api.npc.persistent({ name = 'Fn', template = 'Peasant', position = function(p) return 5, 6, 7 end })");
    CHECK(run("session('new') return tostring(h2:state()) .. ',' .. h3:state()") == "pending,pending");
    // a position function that fails is reported, not thrown
    lua.script("h4 = api.npc.persistent({ name = 'Bad', template = 'Peasant', position = function() error('boom') end }) "
               "risen.entity.player = function() return { position = function() return 0, 0, 0 end } end");
    CHECK(run("session('new') return h4:state() .. ',' .. tostring(lines[#lines]:find('boom', 1, true) ~= nil)") == "pending,true");
}

TEST(persistent_npc_adopts_an_entity_with_its_name_and_handles_late_declarations) {
    sol::state lua;
    setupPersistentNpcMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    lua.script("api.npc.persistent({ name = 'First', template = 'Peasant', position = { 1, 2, 3 } }) "
               "risen.npc.spawn('Peasant', 7, 8, 9, { name = 'Mine' })");
    CHECK(run("session('new') return world.spawned") == "2");
    // declared while the session runs: found/created right away; an entity that already carries the name is adopted, not duplicated
    CHECK(run("local h = api.npc.persistent({ name = 'Mine', template = 'Peasant', position = { 0, 0, 0 } }) return h:state() .. ',' .. world.spawned .. ',' .. store.persistent_npcs.Mine.id") == "alive,2,id1");
    CHECK(run("local h = api.npc.persistent({ name = 'Late', template = 'Wolf', position = { 3, 3, 3 } }) return h:state() .. ',' .. world.spawned") == "alive,3");
    // handle methods: respawn makes a new body at the home position, remove takes it out for good until respawn
    CHECK(run("local h = api.npc.persistent_get('Late') world.by_name.Late.x = 50 local e = h:respawn() return e:id() .. ',' .. world.by_name.Late.x .. ',' .. count('Wolf')") == "id4,3,1");
    CHECK(run("local h = api.npc.persistent_get('Late') h:remove() return h:state() .. ',' .. count('Wolf')") == "removed,0");
    CHECK(run("save_game() load_game() session('load') return api.npc.persistent_get('Late'):state() .. ',' .. count('Wolf')") == "removed,0");
    CHECK(run("local e = api.npc.persistent_get('Late'):respawn() return tostring(e ~= nil) .. ',' .. api.npc.persistent_get('Late'):state() .. ',' .. count('Wolf')") == "true,alive,1");
}

TEST(persistent_npc_removes_a_second_entity_that_carries_the_name) {
    sol::state lua;
    setupPersistentNpcMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    lua.script("api.npc.persistent({ name = 'Hob', template = 'Peasant', position = { 1, 2, 3 } }) session('new') save_game()");
    // a stray, named duplicate (made by an old version of the mod) exists when the saved NPC is found again
    CHECK(run("load_game() risen.npc.spawn('Peasant', 5, 5, 5, { name = 'Hob' }) world.by_name.Hob = world.entities.id2 session('load') "
              "return count('Peasant') .. ',' .. world.by_name.Hob.id") == "1,id1");
}

TEST(persistent_npc_lives_in_one_world) {
    sol::state lua;
    setupPersistentNpcMock(lua);
    lua.script(R"LUA(
        here = "World"
        risen.world = { name = function() return here end }
        local ok, why = api.npc.persistent({ name = "Island_Bob", template = "Peasant", position = { 1, 2, 3 }, world = "World" })
        assert(ok, why)
        assert(api.npc.persistent({ name = "Level_Ann", template = "Peasant", position = { 4, 5, 6 }, world = "Testlevel_Physics" }))
        bad_ok, bad_why = api.npc.persistent({ name = "Bad", template = "Peasant", position = { 0, 0, 0 }, world = "" })
        session("new")
    )LUA");
    // only the NPC of the active world is made
    CHECK(lua.script("return world.by_name.Island_Bob ~= nil and world.by_name.Level_Ann == nil").get<bool>());
    CHECK(lua.script("return bad_ok == nil and tostring(bad_why):find('world must be') ~= nil").get<bool>());
    // in the other world: Ann is made, Bob is left alone (not pruned, not made again)
    lua.script("here = 'Testlevel_Physics' local spawned = world.spawned session('load') ann_spawns = world.spawned - spawned");
    CHECK(lua.script("return world.by_name.Level_Ann ~= nil and ann_spawns == 1 and world.by_name.Island_Bob ~= nil").get<bool>());
}
