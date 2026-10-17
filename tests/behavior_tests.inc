// ---- behaviours: risen.behavior (scripts attached to NPCs) on top of the events, risen.entity and risen.save; the engine is mocked -----------------------------------
// Included by test_main.cpp inside its anonymous namespace (after persistent_npc_tests.inc, whose mock world it extends).

void setupBehaviorMock(sol::state& lua) {
    setupPersistentNpcMock(lua);
    lua.script(R"LUA(
        local get, by_id = risen.entity.get, risen.entity.by_id
        local function aug(h)
            if not h then return nil end
            local id0 = h.id
            function h:id() if self:valid() then return id0(self) end end   -- the engine's handle answers nil for a removed entity
            function h:distance_to(o)
                local x, y, z = self:position()
                local a, b, c = o:position()
                return math.sqrt((x - a) ^ 2 + (y - b) ^ 2 + (z - c) ^ 2)
            end
            return h
        end
        function risen.entity.get(n) return aug(get(n)) end
        function risen.entity.by_id(id) return aug(by_id(id)) end
        function risen.entity.npcs()
            local out = {}
            for id in pairs(world.entities) do out[#out + 1] = risen.entity.by_id(id) end
            return out
        end
        now = 0
        api.clock = function() return now end
        -- n frames of dt seconds each (1/8 s: exact in binary, so 4 frames are 0.5 s)
        function step(n, dt) for _ = 1, n do now = now + (dt or 0.125) fire("tick") end end
        -- a session that reaches the start of the behaviours (40 frames after world.ready)
        function up(mode) fire("session.start", mode or "new") fire("world.ready", mode or "new") step(40) end
        function spawn(tpl, name, x) return risen.npc.spawn(tpl, x or 1000, 0, 0, { name = name }) end
        function move(name, x) world.by_name[name].x = x end
        calls = {}
        function log_call(...) calls[#calls + 1] = table.concat({ ... }, " ") end
        function has_call(text) for _, c in ipairs(calls) do if c == text then return true end end return false end
        function count_calls(prefix) local n = 0 for _, c in ipairs(calls) do if c:sub(1, #prefix) == prefix then n = n + 1 end end return n end
        function warned(needle) for _, l in ipairs(lines) do if l:find(needle, 1, true) then return true end end return false end
    )LUA");
    spass::game::installBehaviorModApi(lua, lua["api"], "testmod");
}

TEST(behavior_define_and_attach_validate) {
    sol::state lua;
    setupBehaviorMock(lua);
    auto reason = [&](const char* code) { return lua.script(std::string("local ok, why = ") + code + " return tostring(ok) .. '|' .. tostring(why)").get<std::string>(); };
    CHECK(reason("api.behavior.define('greeter', { params = { radius = 300 }, update = function() end })") == "true|nil");
    CHECK(reason("api.behavior.define('2bad', {})").find("behaviour name") != std::string::npos);
    CHECK(reason("api.behavior.define('x', 5)").find("must be a table") != std::string::npos);
    CHECK(reason("api.behavior.define('x', { updat = function() end })").find("unknown key 'updat'") != std::string::npos);
    CHECK(reason("api.behavior.define('x', { start = 5 })").find("start must be a function") != std::string::npos);
    CHECK(reason("api.behavior.define('x', { interval = -1 })").find("interval") != std::string::npos);
    CHECK(reason("api.behavior.define('x', { near = 0 })").find("near must be") != std::string::npos);
    CHECK(reason("api.behavior.define('x', { params = { f = print } })").find("plain values") != std::string::npos);
    CHECK(reason("api.behavior.attach('Bob', 'nope')").find("no behaviour 'nope'") != std::string::npos);
    CHECK(reason("api.behavior.attach('', 'greeter')").find("empty") != std::string::npos);
    CHECK(reason("api.behavior.attach(5, 'greeter')").find("expected an NPC") != std::string::npos);
    CHECK(reason("api.behavior.attach('Bob', 'greeter', { f = print })").find("plain values") != std::string::npos);
    CHECK(reason("api.behavior.attach_template('', 'greeter')").find("template name") != std::string::npos);
    CHECK(reason("api.behavior.attach('Bob', 'greeter')") == "true|nil");
    CHECK(lua.script("return #api.behavior.list() == 1 and api.behavior.list()[1].running == false and api.behavior.defined()[1] == 'greeter'").get<bool>());
}

TEST(behavior_starts_updates_and_keeps_state_across_a_load) {
    sol::state lua;
    setupBehaviorMock(lua);
    lua.script(R"LUA(
        api.behavior.define("counter", {
            params = { step = 1, label = "x" },
            interval = 0.5,
            start = function(self) log_call("start", self.entity:name(), tostring(self.resumed), self.params.label, tostring(self.params.step)) end,
            update = function(self, dt) self.state.n = (self.state.n or 0) + self.params.step log_call("update", string.format("%.1f", dt)) end,
            stop = function(self, why) log_call("stop", why) end,
        })
        api.behavior.attach("Bob", "counter", { step = 2 })   -- declared while the mod loads
        spawn("Peasant", "Bob")
        fire("session.start", "new") fire("world.ready", "new") step(39)
    )LUA");
    CHECK(lua.script("return #calls").get<int>() == 0);  // not before 40 frames after world.ready
    lua.script("step(1)");
    CHECK(lua.script("return has_call('start Bob false x 2')").get<bool>());
    lua.script("step(8)");  // one second: two updates of 0.5 s
    CHECK(lua.script("return count_calls('update')").get<int>() == 2);
    CHECK(lua.script("return has_call('update 0.5')").get<bool>());
    CHECK(lua.script("return api.behavior.get('Bob', 'counter').state.n").get<int>() == 4);
    CHECK(lua.script("return api.behavior.list()[1].running").get<bool>());
    // a long frame (a menu) counts as 0.25 s at most
    lua.script("calls = {} now = now + 30 fire('tick')");
    CHECK(lua.script("return count_calls('update')").get<int>() == 0);
    // quick save + load: the NPC is named again (the persistent NPC layer does that), the behaviour resumes with its saved state
    lua.script(R"LUA(
        save_game()
        load_game()
        for id, e in pairs(world.entities) do if e.template == "Peasant" then risen.entity.by_id(id):set_name("Bob") end end
        calls = {}
        fire("world.ready", "load") step(40)
    )LUA");
    CHECK(lua.script("return has_call('start Bob true x 2')").get<bool>());
    CHECK(lua.script("return has_call('stop unloaded')").get<bool>() == false);  // no stop while the world is torn down
    CHECK(lua.script("return api.behavior.get('Bob', 'counter').state.n").get<int>() == 4);
    // new parameters restart it
    lua.script("calls = {} api.behavior.attach('Bob', 'counter', { step = 5, label = 'y' })");
    CHECK(lua.script("return has_call('stop restarted') and has_call('start Bob true y 5')").get<bool>());
}

TEST(behavior_events_death_and_late_npcs) {
    sol::state lua;
    setupBehaviorMock(lua);
    lua.script(R"LUA(
        api.behavior.define("watch", {
            start = function(self) log_call("start", self.entity:name()) end,
            on_death = function(self, killer) log_call("death", tostring(killer)) end,
            on_damage = function(self, attacker, dmg) log_call("damage", tostring(attacker), tostring(dmg)) end,
            on_talk = function(self, info) log_call("talk", info) end,
            on_items = function(self, tpl, delta, total, cause) log_call("items", tpl, tostring(delta), tostring(total), cause) end,
            stop = function(self, why) log_call("stop", why) end,
        })
        api.behavior.attach("Ann", "watch")
        up()
    )LUA");
    CHECK(lua.script("return #calls").get<int>() == 0);  // Ann is not in the world yet
    lua.script("spawn('Peasant', 'Ann') step(15)");       // found by the rescan
    CHECK(lua.script("return has_call('start Ann')").get<bool>());
    lua.script(R"LUA(
        local ann = risen.entity.get("Ann")
        fire("entity.damaged", ann, "PC_Hero", 12, 20)
        fire("info.started", "Ann_Hello", "Ann")
        fire("info.started", "Other_Hello", "Other")
        fire("inventory.changed", ann, "It_Apple", 2, 3, "script", nil)
        die("Ann", "PC_Hero")
        step(30)
    )LUA");
    CHECK(lua.script("return has_call('damage PC_Hero 12')").get<bool>());
    CHECK(lua.script("return has_call('talk Ann_Hello') and not has_call('talk Other_Hello')").get<bool>());
    CHECK(lua.script("return has_call('items It_Apple 2 3 script')").get<bool>());
    CHECK(lua.script("return has_call('death PC_Hero') and has_call('stop died')").get<bool>());
    CHECK(lua.script("return count_calls('start')").get<int>() == 1);  // a dead NPC is not started again
}

TEST(behavior_on_every_npc_of_a_template) {
    sol::state lua;
    setupBehaviorMock(lua);
    lua.script(R"LUA(
        api.behavior.define("pack", { start = function(self) log_call("start", self.key) self.state.seen = true end })
        api.behavior.attach_template("Wolf", "pack")
        spawn("Wolf", "Wolf") spawn("Wolf", "Wolf") spawn("Peasant", "Bob")
        up()
    )LUA");
    CHECK(lua.script("return count_calls('start @')").get<int>() == 2);  // keyed by ID: the wolves have no unique name
    CHECK(lua.script("return api.behavior.list()[1].count").get<int>() == 2);
    // a wolf spawned during the session starts a few frames after entity.spawned
    lua.script("local w = spawn('Wolf', 'Wolf') fire('entity.spawned', w, 'Wolf') step(2)");
    CHECK(lua.script("return count_calls('start @')").get<int>() == 2);
    lua.script("step(2)");
    CHECK(lua.script("return count_calls('start @')").get<int>() == 3);
    // its state is saved per ID
    CHECK(lua.script("local n = 0 for k, v in pairs(store.behaviors) do if k:sub(1, 1) == '@' and v.pack.seen then n = n + 1 end end return n").get<int>() == 3);
    // detach_template stops it everywhere
    lua.script("calls = {} api.behavior.detach_template('Wolf', 'pack')");
    CHECK(lua.script("return #api.behavior.list() == 0 and api.behavior.get(risen.entity.npcs()[1], 'pack') == nil").get<bool>());
}

TEST(behavior_errors_switch_one_attachment_off) {
    sol::state lua;
    setupBehaviorMock(lua);
    lua.script(R"LUA(
        api.behavior.define("bad", { interval = 0, update = function(self) error("boom") end, stop = function() log_call("stop bad") end })
        api.behavior.define("good", { interval = 0, update = function(self) log_call("good") end })
        api.behavior.attach("Bob", "bad")
        api.behavior.attach("Bob", "good")
        spawn("Peasant", "Bob")
        up()
        calls = {}
        step(20)
    )LUA");
    CHECK(lua.script("return warned('bad on Bob: update failed') and warned('boom')").get<bool>());
    CHECK(lua.script("local n = api.behavior.failures() return n").get<int>() == 1);  // once: switched off, not restarted by the rescan
    CHECK(lua.script("return count_calls('good')").get<int>() == 20);
    CHECK(lua.script("return has_call('stop bad')").get<bool>() == false);
    // the next session tries again
    lua.script("up() step(1)");
    CHECK(lua.script("return (api.behavior.failures())").get<int>() == 2);
}

TEST(behavior_player_near_and_far) {
    sol::state lua;
    setupBehaviorMock(lua);
    lua.script(R"LUA(
        api.behavior.define("greet", { near = 300,
            on_player_near = function(self, p, d) log_call("near") end,
            on_player_far = function(self, p, d) log_call("far") end })
        api.behavior.attach("Bob", "greet")
        spawn("Peasant", "Bob", 2000)    -- the player stands at 10, 0, 20
        up()
        step(5)
    )LUA");
    CHECK(lua.script("return #calls").get<int>() == 0);
    lua.script("move('Bob', 100) step(5)");
    CHECK(lua.script("return count_calls('near')").get<int>() == 1);  // once when he comes in, not every frame
    lua.script("move('Bob', 5000) step(1)");
    CHECK(lua.script("return count_calls('far')").get<int>() == 1);
}

TEST(behavior_runtime_attachments_persist_only_when_asked) {
    sol::state lua;
    setupBehaviorMock(lua);
    lua.script(R"LUA(
        api.behavior.define("a", { start = function(self) log_call("start a") end })
        api.behavior.define("b", { start = function(self) log_call("start b") end })
        spawn("Peasant", "Bob")
        up()
        api.behavior.attach("Bob", "a", nil, { persist = true })   -- during the session (a quest handler)
        api.behavior.attach("Bob", "b")
    )LUA");
    CHECK(lua.script("return has_call('start a') and has_call('start b')").get<bool>());
    lua.script(R"LUA(
        save_game() load_game()
        for id, e in pairs(world.entities) do risen.entity.by_id(id):set_name("Bob") end
        calls = {}
        fire("world.ready", "load") step(40)
    )LUA");
    CHECK(lua.script("return has_call('start a') and not has_call('start b')").get<bool>());
    // a new game forgets the persisted one
    lua.script("new_game() spawn('Peasant', 'Bob') calls = {} up()");
    CHECK(lua.script("return #calls").get<int>() == 0);
}

TEST(behavior_detach_and_unsaveable_state) {
    sol::state lua;
    setupBehaviorMock(lua);
    lua.script(R"LUA(
        api.behavior.define("once", { interval = 0,
            update = function(self) self.state.done = true self:detach() end,
            stop = function(self, why) log_call("stop", why) end })
        api.behavior.define("messy", { start = function(self) self.state.fn = print self.state.ok = 1 end })
        api.behavior.attach("Bob", "once")
        api.behavior.attach("Bob", "messy")
        spawn("Peasant", "Bob")
        up() step(1)
    )LUA");
    CHECK(lua.script("return has_call('stop detached')").get<bool>());
    CHECK(lua.script("return store.behaviors.Bob.once == nil and #api.behavior.list() == 1").get<bool>());  // declaration and state gone
    lua.script("step(30)");
    CHECK(lua.script("return count_calls('stop')").get<int>() == 1);  // not attached again by the rescan
    lua.script("fire('save.before', 'x')");
    CHECK(lua.script("return warned('messy on Bob: its state holds something that cannot be saved') and next(store.behaviors.Bob.messy) == nil").get<bool>());
}

TEST(behavior_quests_and_any_event) {
    sol::state lua;
    setupBehaviorMock(lua);
    lua.script(R"LUA(
        api.behavior.define("watcher", {
            on_quest = function(self, quest, status, previous) log_call("quest", quest, status, previous) end,
            events = {
                ["gamevar.changed"] = function(self, name, value) log_call("var", name, tostring(value)) end,
                ["mymod.alarm"] = function(self, level) log_call("alarm", tostring(level), self.entity:name()) end,
            },
        })
        api.behavior.attach("Bob", "watcher")
        spawn("Peasant", "Bob")
        fire("quest.status_changed", "Rufo_GoToDruidHut", "Success", "Running")   -- before the behaviours run: nobody hears it
        up()
        fire("quest.status_changed", "Rufo_GoToDruidHut", "Success", "Running")   -- a quest of the game
        fire("gamevar.changed", "Alarm", 3, nil)
        fire("mymod.alarm", 2)
    )LUA");
    CHECK(lua.script("return count_calls('quest')").get<int>() == 1);
    CHECK(lua.script("return has_call('quest Rufo_GoToDruidHut Success Running')").get<bool>());
    CHECK(lua.script("return has_call('var Alarm 3') and has_call('alarm 2 Bob')").get<bool>());
    auto reason = [&](const char* code) { return lua.script(std::string("local ok, why = ") + code + " return tostring(why)").get<std::string>(); };
    CHECK(reason("api.behavior.define('x', { events = 5 })").find("events must be a table") != std::string::npos);
    CHECK(reason("api.behavior.define('x', { events = { ['bad name!'] = function() end } })").find("event names") != std::string::npos);
    CHECK(reason("api.behavior.define('x', { events = { tick = 5 } })").find("must be a function") != std::string::npos);
}
