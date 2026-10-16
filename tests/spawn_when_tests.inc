// ---- conditional spawning (M8): risen.cond conditions and risen.spawn.when rules on top of the persistent NPC layer; the engine is mocked ------------------
// Included by test_main.cpp inside its anonymous namespace (after persistent_npc_tests.inc, whose mock world it extends).

void setupSpawnWhenMock(sol::state& lua) {
    setupPersistentNpcMock(lua);
    lua.script(R"LUA(
        -- game state the conditions read
        quests, inv, vars = {}, {}, {}
        local player = { position = function() return unpack(world.player) end, is_player = function() return true end, name = function() return "PC_Hero" end }
        function risen.entity.player() return player end
        risen.quest = { get = function(n) local s = quests[n] if s then return { get_property = function() return s end } end end }
        risen.item = { count = function(e, tpl) if e == player then return inv[tpl] or 0 end return 0 end }
        risen.gamevar = { get = function(n) return vars[n] end, exists = function(n) return vars[n] ~= nil end }
        function up(mode) fire("session.start", mode) fire("world.ready", mode) ticks(45) end
        function set_quest(name, status)
            local previous = quests[name] and quests[name]:gsub("^gEQuestStatus_", "") or "None"
            quests[name] = "gEQuestStatus_" .. status
            fire("quest.status_changed", name, status, previous)
        end
        function set_var(name, value) local prev = vars[name] vars[name] = value fire("gamevar.changed", name, value, prev) end
        function set_items(tpl, n) local d = n - (inv[tpl] or 0) inv[tpl] = n fire("inventory.changed", player, tpl, d, n, "script", nil) end
    )LUA");
    spass::game::installSpawnWhenModApi(lua, lua["api"], "testmod");
}

TEST(cond_constructors_validate_and_describe) {
    sol::state lua;
    setupSpawnWhenMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    auto fails = [&](const char* code, const char* needle) {
        const std::string r = run((std::string("local ok, err = pcall(function() ") + code + " end) return tostring(ok) .. '|' .. tostring(err)").c_str());
        return r.rfind("false|", 0) == 0 && r.find(needle) != std::string::npos;
    };
    CHECK(run("return tostring(api.cond.quest('Q_A', 'Success'))") == "quest Q_A is Success");
    CHECK(run("return tostring(api.cond.quest('Q_A'))") == "quest Q_A is Success");
    CHECK(run("return tostring(api.cond.quest('Q_A', { 'Failed', 'gEQuestStatus_Lost' }))") == "quest Q_A is Failed/Lost");
    CHECK(run("return tostring(api.cond.item('It_Apple', 3))") == "player holds >= 3 x It_Apple");
    CHECK(run("return tostring(api.cond.item('It_Apple', 1, 'Hob'))") == "Hob holds >= 1 x It_Apple");
    CHECK(run("return tostring(api.cond.var('X'))") == "var X is set");
    CHECK(run("return tostring(api.cond.event('SW_Open'))") == "game event SW_Open is set");
    CHECK(run("return tostring(api.cond.var('X', 3, '>='))") == "var X >= 3");
    CHECK(run("return tostring(api.cond.dead('Hob')) .. ',' .. tostring(api.cond.alive({ name = 'Hob' }))") == "Hob is dead,Hob is alive");
    CHECK(run("return tostring(api.cond.all{ api.cond.quest('Q'), api.cond.negate(api.cond.var('X')), function() return true end })") == "all(quest Q is Success, not var X is set, predicate)");
    CHECK(fails("api.cond.quest('Q', 'Done')", "unknown status 'Done'"));
    CHECK(fails("api.cond.quest(5)", "quest name"));
    CHECK(fails("api.cond.item('It_Apple', 0)", "integer >= 1"));
    CHECK(fails("api.cond.var('X', 'a')", "must be a number"));
    CHECK(fails("api.cond.var('X', 1, '=')", "unknown operator"));
    CHECK(fails("api.cond.dead(nil)", "expected an entity name"));
    CHECK(fails("api.cond.event('')", "event / variable name"));
    CHECK(fails("api.cond.fn(5)", "expected a function"));
    CHECK(fails("api.cond.fn(function() end, { poll = 0 })", "poll"));
    CHECK(fails("api.cond.all{}", "non-empty list"));
    CHECK(fails("api.cond.any{ 5 }", "expected a condition"));
    CHECK(fails("api.cond.negate('x')", "expected a condition"));
}

TEST(cond_test_reads_quests_items_variables_and_npcs) {
    sol::state lua;
    setupSpawnWhenMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    CHECK(run("return tostring(api.cond.test(api.cond.quest('Q', 'None'))) .. tostring(api.cond.test(api.cond.quest('Q')))") == "truefalse");
    CHECK(run("quests.Q = 'gEQuestStatus_Success' return tostring(api.cond.test(api.cond.quest('Q'))) .. tostring(api.cond.test(api.cond.quest('Q', { 'Failed', 'Running' })))") == "truefalse");
    CHECK(run("inv.It_Apple = 2 local c = api.cond.item('It_Apple', 3) local a = api.cond.test(c) inv.It_Apple = 3 return tostring(a) .. tostring(api.cond.test(c))") == "falsetrue");
    // a variable that does not exist counts as 0
    CHECK(run("return tostring(api.cond.test(api.cond.var('V'))) .. tostring(api.cond.test(api.cond.var('V', 0))) .. tostring(api.cond.test(api.cond.var('V', 0, '~=')))") == "falsetruefalse");
    CHECK(run("vars.V = 5 return tostring(api.cond.test(api.cond.var('V'))) .. tostring(api.cond.test(api.cond.var('V', 5))) .. tostring(api.cond.test(api.cond.var('V', 5, '>'))) .. tostring(api.cond.test(api.cond.var('V', 6, '<')))") == "truetruefalsetrue");
    // an event is the existence of the variable (SetGameEvent creates it with the value 0)
    CHECK(run("local c = api.cond.event('E') local a = api.cond.test(c) vars.E = 0 return tostring(a) .. tostring(api.cond.test(c)) .. tostring(api.cond.test(api.cond.var('E')))") == "falsetruefalse");
    // dead / alive by unique name
    CHECK(run("world.by_name.Hob = { id = 'h', name = 'Hob', dead = false } local d, a = api.cond.dead('Hob'), api.cond.alive('Hob') "
              "local r1 = tostring(api.cond.test(d)) .. tostring(api.cond.test(a)) world.by_name.Hob.dead = true return r1 .. tostring(api.cond.test(d)) .. tostring(api.cond.test(a))") == "falsetruetruefalse");
    CHECK(run("return tostring(api.cond.test(api.cond.dead('Nobody'))) .. tostring(api.cond.test(api.cond.alive('Nobody')))") == "falsefalse");
    // combinators and predicates; a predicate that raises (or returns nil) makes the answer unknown (nil), never a wrong boolean
    CHECK(run("local T, F = function() return true end, function() return false end "
              "return tostring(api.cond.test(api.cond.all{ T, F })) .. tostring(api.cond.test(api.cond.any{ F, T })) .. tostring(api.cond.test(api.cond.negate(T))) .. tostring(api.cond.test(api.cond.all{ T, T }))") == "falsetruefalsetrue");
    CHECK(run("local E = function() error('boom') end local T, F = function() return true end, function() return false end "
              "return tostring(api.cond.test(E)) .. tostring(api.cond.test(api.cond.all{ T, E })) .. tostring(api.cond.test(api.cond.all{ F, E })) .. tostring(api.cond.test(api.cond.any{ T, E })) .. tostring(api.cond.test(api.cond.negate(E)))") == "nilnilfalsetruenil");
    CHECK(run("return tostring(lines[#lines]:find('condition .predicate.: .*boom') ~= nil)") == "true");
    CHECK(run("return tostring(api.cond.test(function() return nil end)) .. tostring(api.cond.test(12))") == "nilnil");
}

TEST(spawn_when_validates_the_spec_before_declaring_anything) {
    sol::state lua;
    setupSpawnWhenMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    CHECK(run("local h, why = api.spawn.when(5) return tostring(h) .. '|' .. why").find("expects a table") != std::string::npos);
    CHECK(run("local h, why = api.spawn.when({ template = 'Peasant', position = {1,2,3}, when = function() end }) return why").find("name must be a string") != std::string::npos);
    CHECK(run("local h, why = api.spawn.when({ name = 'A', template = 'Peasant', position = {1,2,3} }) return why").find("`when` is required") != std::string::npos);
    CHECK(run("local h, why = api.spawn.when({ name = 'A', template = 'Peasant', position = {1,2,3}, when = 'yes' }) return why").find("when: expected a condition") != std::string::npos);
    CHECK(run("local h, why = api.spawn.when({ name = 'A', template = 'Peasant', position = {1,2,3}, when = function() end, despawn_when = 3 }) return why").find("despawn_when: expected a condition") != std::string::npos);
    CHECK(run("local h, why = api.spawn.when({ name = 'A', template = 'Peasant', position = {1,2,3}, when = function() end, once = 'x' }) return why").find("once must be") != std::string::npos);
    CHECK(run("local h, why = api.spawn.when({ name = 'A', template = 'Peasant', position = {1,2,3}, when = function() end, on_fire = 1 }) return why").find("on_fire must be a function") != std::string::npos);
    CHECK(run("local h, why = api.spawn.when({ name = 'A', template = 'Peasant', position = {1,2,3}, when = function() end, enabled = function() end }) return why").find("managed by risen.spawn.when") != std::string::npos);
    // the persistent NPC layer validates its own options
    CHECK(run("local h, why = api.spawn.when({ name = 'A', template = 'Peasant', when = function() end }) return why").find("exactly one of position") != std::string::npos);
    CHECK(run("local h, why = api.spawn.when({ name = 'A', template = 'Peasant', position = {1,2,3}, when = function() end, colour = 1 }) return why") == "unknown option 'colour'");
    CHECK(run("return #api.spawn.rules() .. ',' .. #api.npc.persistent_list()") == "0,0");
    // a valid one is declared once; the handle is shared, the NPC is a persistent NPC of the same name
    CHECK(run("local a = api.spawn.when({ name = 'A', template = 'Peasant', position = {1,2,3}, when = api.cond.quest('Q') }) local b = api.spawn.when({ name = 'A', template = 'Wolf', position = {1,2,3}, when = function() end }) "
              "return tostring(a == b) .. ',' .. tostring(a) .. ',' .. #api.spawn.rules() .. ',' .. tostring(api.spawn.get('A') == a) .. ',' .. tostring(api.npc.persistent_get('A') == a.npc)") == "true,SpawnRule(A),1,true,true");
}

TEST(spawn_when_a_one_shot_rule_creates_the_npc_when_the_quest_succeeds_and_keeps_it) {
    sol::state lua;
    setupSpawnWhenMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    lua.script(R"LUA(
        fired = {}
        r = api.spawn.when({ name = 'Reward', template = 'Peasant', position = { 1, 2, 3 }, when = api.cond.quest('Q_Main', 'Success'),
            on_fire = function(h, why, e) fired[#fired + 1] = why .. ':' .. h.name .. ':' .. tostring(e ~= nil) end })
        quests.Q_Main = 'gEQuestStatus_Running'
    )LUA");
    CHECK(run("return r:state() .. ',' .. r.npc:state()") == "pending,disabled");
    CHECK(run("up('new') return r:state() .. ',' .. r.npc:state() .. ',' .. world.spawned .. ',' .. r:fired()") == "waiting,disabled,0,0");
    // other quests and other statuses change nothing
    CHECK(run("set_quest('Q_Other', 'Success') set_quest('Q_Main', 'Failed') ticks(3) return r:state() .. ',' .. world.spawned") == "waiting,0");
    // the real trigger; the NPC is made on the next tick, not inside the event
    CHECK(run("set_quest('Q_Main', 'Success') local before = world.spawned ticks(1) return before .. ',' .. world.spawned .. ',' .. r:state() .. ',' .. r.npc:state() .. ',' .. r:fired() .. ',' .. table.concat(fired, ',')") == "0,1,present,alive,1,condition:Reward:true");
    CHECK(run("return store.spawn_rules.Reward.state .. ',' .. store.spawn_rules.Reward.spawns .. ',' .. world.by_name.Reward.x .. ',' .. store.persistent_npcs.Reward.id") == "present,1,1,id1");
    // a one-shot rule is latched: the condition going false again removes nothing, and nothing is made twice
    CHECK(run("set_quest('Q_Main', 'Running') ticks(3) set_quest('Q_Main', 'Success') ticks(3) return r:state() .. ',' .. world.spawned .. ',' .. count('Peasant') .. ',' .. r:fired()") == "present,1,1,1");
}

TEST(spawn_when_despawn_condition_removes_the_npc_for_good_in_a_one_shot_rule) {
    sol::state lua;
    setupSpawnWhenMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    lua.script(R"LUA(
        log2 = {}
        r = api.spawn.when({ name = 'Guest', template = 'Peasant', position = { 1, 2, 3 }, when = api.cond.var('SW_Open'), despawn_when = api.cond.var('SW_Gone'),
            on_fire = function(h, why) log2[#log2 + 1] = 'fire:' .. why end, on_despawn = function(h, why) log2[#log2 + 1] = 'gone:' .. why end })
        up('new')
        set_var('SW_Open', 1) ticks(2)
    )LUA");
    CHECK(run("return r:state() .. ',' .. count('Peasant') .. ',' .. tostring(r:entity() ~= nil)") == "present,1,true");
    CHECK(run("set_var('SW_Gone', 1) ticks(2) return r:state() .. ',' .. count('Peasant') .. ',' .. tostring(r:entity()) .. ',' .. r.npc:state() .. ',' .. table.concat(log2, ',')") == "gone,0,nil,removed,fire:condition,gone:condition");
    // gone is final: the spawn condition holding again (or the despawn condition ceasing) does not bring it back
    CHECK(run("set_var('SW_Gone', 0) set_var('SW_Open', 0) ticks(2) set_var('SW_Open', 1) ticks(2) return r:state() .. ',' .. count('Peasant') .. ',' .. r:fired()") == "gone,0,1");
    // both conditions true when the rule is first looked at: nothing is created, and it is over
    CHECK(run("api.spawn.when({ name = 'Both', template = 'Wolf', position = { 4, 5, 6 }, when = api.cond.var('SW_Open'), despawn_when = api.cond.var('SW_Open') }) "
              "up('new') return api.spawn.get('Both'):state() .. ',' .. count('Wolf') .. ',' .. store.spawn_rules.Both.state") == "gone,0,gone");
}

TEST(spawn_when_a_tracking_rule_follows_its_condition_and_comes_back) {
    sol::state lua;
    setupSpawnWhenMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    lua.script(R"LUA(
        r = api.spawn.when({ name = 'Guard', template = 'Peasant', offset = { 100, 0, 0 }, once = false, when = api.cond.item('It_Apple', 3) })
        up('new')
    )LUA");
    CHECK(run("return r:state() .. ',' .. count('Peasant')") == "waiting,0");
    CHECK(run("set_items('It_Apple', 2) ticks(2) return r:state() .. ',' .. count('Peasant')") == "waiting,0");
    CHECK(run("set_items('It_Apple', 3) ticks(2) return r:state() .. ',' .. count('Peasant') .. ',' .. world.by_name.Guard.x .. ',' .. r:fired()") == "present,1,110,1");
    CHECK(run("set_items('It_Apple', 5) ticks(2) return r:state() .. ',' .. count('Peasant') .. ',' .. r:fired()") == "present,1,1");   // still true: nothing new
    // false again: removed (and the rule is waiting, not gone)
    CHECK(run("set_items('It_Apple', 1) ticks(2) return r:state() .. ',' .. count('Peasant') .. ',' .. store.spawn_rules.Guard.state") == "waiting,0,waiting");
    // true again: made again at its home position, as a new entity, never two
    CHECK(run("world.player = { 500, 0, 500 } set_items('It_Apple', 3) ticks(2) return r:state() .. ',' .. count('Peasant') .. ',' .. world.by_name.Guard.x .. ',' .. world.by_name.Guard.id .. ',' .. r:fired()") == "present,1,110,id2,2");
}

TEST(spawn_when_is_remembered_per_savegame_and_a_load_makes_the_world_consistent) {
    sol::state lua;
    setupSpawnWhenMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    lua.script(R"LUA(
        once = api.spawn.when({ name = 'Reward', template = 'Peasant', position = { 1, 2, 3 }, when = api.cond.quest('Q_Main') })
        track = api.spawn.when({ name = 'Guard', template = 'Wolf', position = { 4, 5, 6 }, once = false, when = api.cond.item('It_Apple', 3) })
        up('new')
        quests.Q_Main = 'gEQuestStatus_Running'
        save_game()                      -- save A: before anything fired
        saved_before = saved
    )LUA");
    CHECK(run("set_quest('Q_Main', 'Success') set_items('It_Apple', 3) ticks(2) return once:state() .. ',' .. track:state() .. ',' .. count('Peasant') .. ',' .. count('Wolf')") == "present,present,1,1");
    CHECK(run("save_game() saved_after = saved return 'saved'") == "saved");
    // load the earlier save (in the same process): nothing of the later state survives, the rules wait again
    CHECK(run("saved = saved_before load_game() quests.Q_Main = 'gEQuestStatus_Running' inv.It_Apple = 0 up('load') "
              "return once:state() .. ',' .. track:state() .. ',' .. count('Peasant') .. ',' .. count('Wolf') .. ',' .. tostring(store.spawn_rules)") == "waiting,waiting,0,0,nil");
    // ... and they fire again when the player does the thing again: exactly one NPC each
    CHECK(run("set_quest('Q_Main', 'Success') set_items('It_Apple', 4) ticks(2) return once:state() .. ',' .. count('Peasant') .. ',' .. count('Wolf') .. ',' .. world.spawned") == "present,1,1,4");
    // the later save: restored by ID, no duplicate, no new spawn, the rules are consistent
    CHECK(run("saved = saved_after load_game() quests.Q_Main = 'gEQuestStatus_Success' inv.It_Apple = 3 up('load') "
              "return once:state() .. ',' .. track:state() .. ',' .. count('Peasant') .. ',' .. count('Wolf') .. ',' .. world.spawned .. ',' .. tostring(world.by_name.Reward ~= nil) .. ',' .. once:fired()") == "present,present,1,1,4,true,1");
    // the same savegame again, in a "fresh process" (the Lua state is the same here, but the rules start from the sidecar): stable
    CHECK(run("load_game() up('load') return count('Peasant') .. ',' .. count('Wolf') .. ',' .. world.spawned") == "1,1,4");
}

TEST(spawn_when_evaluates_every_rule_once_after_a_load_also_when_the_condition_changed_meanwhile) {
    sol::state lua;
    setupSpawnWhenMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    lua.script(R"LUA(
        reasons = {}
        local function note(h, why) reasons[#reasons + 1] = h.name .. ':' .. why end
        track = api.spawn.when({ name = 'Guard', template = 'Wolf', position = { 4, 5, 6 }, once = false, when = api.cond.item('It_Apple', 3), on_fire = note, on_despawn = note })
        up('new')
        set_items('It_Apple', 3) ticks(2)
        save_game()                                     -- saved: the guard stands there, the savegame holds 3 apples
        reasons = {}
    )LUA");
    // the savegame's own state is consistent: the load restores the guard, evaluating finds nothing to do
    CHECK(run("load_game() inv.It_Apple = 3 up('load') return track:state() .. ',' .. count('Wolf') .. ',' .. #reasons") == "present,1,0");
    // the player's inventory differs from what the rule remembered (an old savegame, a mod that edited it): the condition is false at load -> despawned
    CHECK(run("load_game() inv.It_Apple = 0 up('load') return track:state() .. ',' .. count('Wolf') .. ',' .. table.concat(reasons, ',')") == "waiting,0,Guard:load");
    // a rule that was added later (the sidecar knows nothing of it) fires at the load when its condition already holds: "became true while the mod did not know"
    CHECK(run("inv.It_Apple = 3 quests.Q_New = 'gEQuestStatus_Success' reasons = {} "
              "late = api.spawn.when({ name = 'Late', template = 'Peasant', position = { 7, 8, 9 }, when = api.cond.quest('Q_New'), on_fire = function(h, why) reasons[#reasons + 1] = h.name .. ':' .. why end }) "
              "load_game() up('load') return late:state() .. ',' .. count('Peasant') .. ',' .. table.concat(reasons, ',')") == "present,1,Late:load");
}

TEST(spawn_when_reacts_to_predicates_events_polling_and_other_npcs) {
    sol::state lua;
    setupSpawnWhenMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    lua.script(R"LUA(
        flag, polled = false, 0
        victim = api.npc.persistent({ name = 'Victim', template = 'Wolf', position = { 1, 1, 1 } })
        by_event = api.spawn.when({ name = 'ByEvent', template = 'Peasant', position = { 2, 2, 2 }, when = api.cond.fn(function() return flag end, { on = 'my.changed' }) })
        by_poll = api.spawn.when({ name = 'ByPoll', template = 'Peasant', position = { 3, 3, 3 }, when = api.cond.fn(function() polled = polled + 1 return flag end, { poll = 10 }) })
        avenger = api.spawn.when({ name = 'Avenger', template = 'Peasant', position = { 4, 4, 4 }, when = api.cond.all{ api.cond.dead('Victim'), api.cond.negate(api.cond.var('Peace')) } })
        up('new')
    )LUA");
    CHECK(run("return by_event:state() .. ',' .. by_poll:state() .. ',' .. avenger:state() .. ',' .. count('Wolf') .. ',' .. count('Peasant')") == "waiting,waiting,waiting,1,0");
    // a predicate changes silently: nothing happens until its event (or the poll interval)
    CHECK(run("flag = true ticks(3) return by_event:state() .. ',' .. count('Peasant')") == "waiting,0");
    CHECK(run("fire('my.changed') ticks(1) return by_event:state() .. ',' .. count('Peasant')") == "present,1");
    CHECK(run("local n = polled ticks(12) return by_poll:state() .. ',' .. count('Peasant') .. ',' .. tostring(polled > n)") == "present,2,true");
    // a combination: the death of a named NPC and a variable that is not set
    CHECK(run("set_var('Peace', 1) die('Victim', 'player') ticks(2) return avenger:state()") == "waiting");
    CHECK(run("set_var('Peace', 0) ticks(2) return avenger:state() .. ',' .. count('Peasant')") == "present,3");
    // a rule's own NPC can be the subject of another rule: alive() follows the spawn
    CHECK(run("api.spawn.when({ name = 'Follower', template = 'Wolf', position = { 5, 5, 5 }, when = api.cond.alive('ByEvent') }) ticks(2) return api.spawn.get('Follower'):state() .. ',' .. count('Wolf')") == "present,2");
}

TEST(spawn_when_reset_and_check_and_a_missing_player) {
    sol::state lua;
    setupSpawnWhenMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    lua.script(R"LUA(
        x = false
        r = api.spawn.when({ name = 'Hob', template = 'Peasant', position = { 1, 2, 3 }, when = function() return x end })
        up('new')
    )LUA");
    CHECK(run("x = true local s = r:check() return s .. ',' .. count('Peasant')") == "present,1");
    CHECK(run("x = false r:reset() ticks(2) return r:state() .. ',' .. count('Peasant') .. ',' .. r.npc:state()") == "waiting,0,removed");
    CHECK(run("x = true api.spawn.check() return r:state() .. ',' .. count('Peasant') .. ',' .. r:fired()") == "present,1,2");
    // a predicate that raises leaves the rule alone
    CHECK(run("local bad = api.spawn.when({ name = 'Bad', template = 'Wolf', position = { 1, 2, 3 }, when = function() error('nope') end }) ticks(2) api.spawn.check() return bad:state() .. ',' .. count('Wolf')") == "waiting,0");
}
