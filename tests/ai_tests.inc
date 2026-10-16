// ---- AI states (M11): the pure logic of src/game/ai_names.h and the per-mod Lua chunk of src/game/ai_lua.h (persist / stop / forget / restore) against a mocked native layer -----------
// Included by test_main.cpp inside its anonymous namespace.

TEST(ai_perception_aliases) {
    using namespace spass::game::ai;
    CHECK(perceptionScript("notice") == "AssessActive");
    CHECK(perceptionScript("damage") == "AssessDamage");
    CHECK(perceptionScript("talk") == "AssessTalk");
    CHECK(perceptionScript("theft") == "AssessTheft");
    // the engine's own names pass through, other scripts do not
    CHECK(perceptionScript("AssessDamage") == "AssessDamage");
    CHECK(perceptionScript("PerceiveItem") == "PerceiveItem");
    CHECK(perceptionScript("Kill").empty());
    CHECK(perceptionScript("").empty());
    CHECK(perceptionScript("Assess").empty());   // the prefix alone is not a script
    CHECK(perceptionScript("sniff").empty());
    // every alias points at an Assess* script and no key appears twice
    std::set<std::string> keys;
    for (const Perception& p : kPerceptions) {
        CHECK(std::string(p.script).rfind("Assess", 0) == 0);
        CHECK(keys.insert(p.key).second);
    }
}

TEST(ai_names_and_results) {
    using namespace spass::game::ai;
    CHECK(validateName("ZS_MyGuard").empty());
    CHECK(validateName("a").empty());
    CHECK(validateName(std::string(100, 'x')).empty());
    CHECK(!validateName("").empty());
    CHECK(!validateName(std::string(101, 'x')).empty());
    CHECK(!validateName("1abc").empty());
    CHECK(!validateName("ZS My").empty());
    CHECK(!validateName("ZS-My").empty());
    CHECK(!validateName("Zs\xC3\xA4").empty());

    // loop results: nothing / true go on, false / "done" end, another string ends and names the next state
    LoopResult r = interpretLoopResult(true, false, false, false, "");
    CHECK(!r.end && r.next.empty());
    r = interpretLoopResult(false, true, true, false, "");
    CHECK(!r.end);
    r = interpretLoopResult(false, true, false, false, "");
    CHECK(r.end && r.next.empty());
    r = interpretLoopResult(false, false, false, true, "done");
    CHECK(r.end && r.next.empty());
    r = interpretLoopResult(false, false, false, true, "ZS_HangAround");
    CHECK(r.end && r.next == "ZS_HangAround");
    r = interpretLoopResult(false, false, false, true, "");
    CHECK(r.end && r.next.empty());
    r = interpretLoopResult(false, false, false, false, "");   // a number or a table: ignored
    CHECK(!r.end);

    // cadence: the first call always runs, then every `interval` seconds of state time, and a state time that went back means a restart
    CHECK(dueForLoop(0, 0.0, 0.0, 0.5));
    CHECK(!dueForLoop(1, 0.2, 0.0, 0.5));
    CHECK(dueForLoop(1, 0.5, 0.0, 0.5));
    CHECK(dueForLoop(3, 7.0, 6.0, 0.5));
    CHECK(dueForLoop(3, 1.0, 6.0, 0.5));
    CHECK(dueForLoop(5, 0.001, 0.0, 0.0));     // interval 0: every frame

    CHECK(finishReason("") == "replaced");
    CHECK(finishReason("done") == "done");
    CHECK(finishReason("stopped") == "stopped");

    CHECK(normalizeInterval(false, 0) == 0.25);
    CHECK(normalizeInterval(true, 0.5) == 0.5);
    CHECK(normalizeInterval(true, -3) == 0.0);
    CHECK(normalizeInterval(true, 1e9) == 60.0);
    CHECK(normalizeInterval(true, std::nan("")) == 0.0);
}

// The mock: `risen.ai` natives record their calls; entities are userdata with id(); the mod's api has on / off / save / log like the real one.
static void setupAiMock(sol::state& lua) {
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math);
    lua.script(R"LUA(
        local protos = {}
        local function entity(id)
            if protos[id] then return protos[id] end
            local u = newproxy(true)
            getmetatable(u).__index = { id = function() return id end, name = function() return "N" .. id end }
            protos[id] = u
            return u
        end
        function npc(id) return entity(id) end
        world = { ["a"] = true, ["b"] = true, ["c"] = true }       -- entities that exist (by_id)
        calls = {}
        defined = { ZS_Mine = true, ZS_Other = true }
        generation = 10
        refuse = nil
        risen = { log = {}, entity = {} }
        lines = {}
        for _, l in ipairs { "info", "warn", "error" } do risen.log[l] = function(m) lines[#lines + 1] = l:sub(1, 1):upper() .. " " .. m end end
        function risen.entity.by_id(id) if world[id] then return entity(id) end return nil end
        shared = {}
        function shared.start(n, state, args, opts)
            calls[#calls + 1] = { "start", n:id(), state, args, opts }
            if refuse then return nil, refuse end
            return true
        end
        function shared.stop(n, nxt)
            calls[#calls + 1] = { "stop", n:id(), nxt }
            if refuse then return nil, refuse end
            return true
        end
        function shared.exists(kind, name) return kind == "state" and defined[name] == true end
        function shared.generation() generation = generation + 1 return generation end
        risen.ai = shared

        handlers, nexth = {}, 1
        store = {}
        api = setmetatable({ log = risen.log, entity = risen.entity,
            on = function(event, fn) local h = nexth nexth = nexth + 1 handlers[#handlers + 1] = { event = event, fn = fn, h = h } return h end,
            off = function(h) for i = #handlers, 1, -1 do if handlers[i].h == h then table.remove(handlers, i) end end end,
            save = { get = function(k) return store[k] end, set = function(k, v) store[k] = v end } }, { __index = risen })
        api.ai = shared
        function fire(event, ...)
            local snapshot = {}
            for i, h in ipairs(handlers) do snapshot[i] = h end
            for _, h in ipairs(snapshot) do if h.event == event then h.fn(...) end end
        end
        function ticks(n) for _ = 1, n do fire("tick") end end
        function ids() local out = {} for _, r in ipairs(api.ai.persisted()) do out[#out + 1] = r.id end return table.concat(out, ",") end
    )LUA");
    spass::game::installAiModApi(lua, lua["api"], "testmod");
}

TEST(ai_persist_records_what_a_load_would_lose) {
    sol::state lua;
    setupAiMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };

    // the mod's view replaces start / stop and adds forget / persisted, everything else falls through to the shared table
    CHECK(run("return tostring(api.ai ~= risen.ai) .. tostring(api.ai.generation == risen.ai.generation)") == "truetrue");

    // without persist: passed on as it is, nothing remembered
    CHECK(run("local ok = api.ai.start(npc('a'), 'ZS_Mine', { x = 1 }) return tostring(ok) .. calls[1][3] .. calls[1][4].x .. tostring(calls[1][5]) .. ids()") == "trueZS_Mine1nil");
    CHECK(run("return #api.ai.persisted()") == "0");

    // bad persist usage is refused before the native is called
    lua.script("calls = {}");
    CHECK(run("local ok, why = api.ai.start(npc('a'), 'ZS_Mine', { f = function() end }, { persist = true }) return tostring(ok) .. '|' .. (why:find('plain data') and 'plain' or why)") == "nil|plain");
    CHECK(run("local ok, why = api.ai.start(npc('a'), 'ZS_Mine', nil, { persist = 'yes' }) return tostring(ok) .. '|' .. why") == "nil|persist must be true or false");
    CHECK(run("local ok, why = api.ai.start(npc('a'), 'ZS_Mine', nil, 5) return tostring(ok) .. '|' .. (why:find('table') and 'table' or why)") == "nil|table");
    CHECK(run("local t = {} t.self = t local ok, why = api.ai.start(npc('a'), 'ZS_Mine', t, { persist = true }) return tostring(ok) .. '|' .. (why:find('contains itself') and 'cycle' or why)") == "nil|cycle");
    CHECK(run("local deep = {} local t = deep for _ = 1, 12 do t.x = {} t = t.x end local ok, why = api.ai.start(npc('a'), 'ZS_Mine', deep, { persist = true }) return tostring(ok) .. '|' .. (why:find('deeper') and 'deep' or why)") == "nil|deep");
    CHECK(run("return #calls") == "0");

    // persist: remembered with the arguments; the other options go on to the native, persist does not
    CHECK(run("local ok = api.ai.start(npc('a'), 'ZS_Mine', { tag = 'x', list = { 1, 2 } }, { persist = true, interrupt = false }) "
              "return tostring(ok) .. tostring(calls[1][5].interrupt) .. tostring(calls[1][5].persist) .. ids()") == "truefalsenila");
    CHECK(run("local r = api.ai.persisted()[1] return r.state .. r.args.tag .. r.args.list[2]") == "ZS_Minex2");
    CHECK(run("return type(store.ai_states.a.after)") == "number");

    // the native refusing leaves the record alone and makes none
    lua.script("refuse = 'no way'");
    CHECK(run("local ok, why = api.ai.start(npc('b'), 'ZS_Mine', nil, { persist = true }) return tostring(ok) .. '|' .. why .. '|' .. ids()") == "nil|no way|a");
    CHECK(run("local ok, why = api.ai.stop(npc('a')) return tostring(ok) .. '|' .. why .. '|' .. ids()") == "nil|no way|a");
    lua.script("refuse = nil");

    // a plain start replaces what was remembered for the NPC; stop forgets; forget drops once
    CHECK(run("api.ai.start(npc('b'), 'ZS_Mine', nil, { persist = true }) api.ai.start(npc('a'), 'ZS_Other') return ids()") == "b");
    CHECK(run("assert(api.ai.stop(npc('b'), 'ZS_Other')) return ids() .. '|' .. calls[#calls][3]") == "|ZS_Other");
    CHECK(run("api.ai.start(npc('c'), 'ZS_Mine', nil, { persist = true }) return tostring(api.ai.forget(npc('c'))) .. tostring(api.ai.forget(npc('c'))) .. tostring(api.ai.forget(5)) .. ids()") == "truefalsefalse");
}

TEST(ai_persist_forgets_a_state_that_ended_and_restores_the_others) {
    sol::state lua;
    setupAiMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };

    // ai.finished: done / stopped / error drop the record, replaced keeps it; a finish of an older run or another state is not the record's
    lua.script("api.ai.start(npc('a'), 'ZS_Mine', { n = 1 }, { persist = true }) rec_after = store.ai_states.a.after");
    CHECK(run("fire('ai.finished', npc('a'), 'ZS_Mine', 'replaced', 3, rec_after + 5) return ids()") == "a");
    CHECK(run("fire('ai.finished', npc('a'), 'ZS_Other', 'done', 3, rec_after + 5) return ids()") == "a");        // another state ended
    CHECK(run("fire('ai.finished', npc('a'), 'ZS_Mine', 'done', 3, rec_after - 1) return ids()") == "a");          // a run that began before the record
    CHECK(run("fire('ai.finished', npc('b'), 'ZS_Mine', 'done', 3, rec_after + 5) return ids()") == "a");          // another NPC
    CHECK(run("fire('ai.finished', nil, 'ZS_Mine', 'done', 3, rec_after + 5) return ids()") == "a");               // an NPC that is gone
    CHECK(run("fire('ai.finished', npc('a'), 'ZS_Mine', 'done', 3, rec_after + 5) return ids()") == "");
    lua.script("api.ai.start(npc('a'), 'ZS_Mine', nil, { persist = true }) rec_after = store.ai_states.a.after");
    CHECK(run("fire('ai.finished', npc('a'), 'ZS_Mine', 'error', 1, rec_after) return ids()") == "");              // a run that began exactly when the record was made counts
    lua.script("api.ai.start(npc('a'), 'ZS_Mine', nil, { persist = true }) rec_after = store.ai_states.a.after");
    CHECK(run("fire('ai.finished', npc('a'), 'ZS_Mine', 'stopped', 1, rec_after + 1) return ids()") == "");

    // restore: 30 frames after world.ready every remembered state is started again with its arguments and resumed = true
    lua.script("calls = {} api.ai.start(npc('a'), 'ZS_Mine', { tag = 'A' }, { persist = true }) api.ai.start(npc('b'), 'ZS_Other', { tag = 'B' }, { persist = true }) calls = {}");
    lua.script("fire('world.ready', 'load') ticks(29)");
    CHECK(run("return #calls") == "0");              // not yet
    lua.script("ticks(1)");
    CHECK(run("return #calls") == "2");
    CHECK(run("return calls[1][2] .. calls[1][3] .. calls[1][4].tag .. tostring(calls[1][5].resumed) .. '|' .. calls[2][2] .. calls[2][3] .. calls[2][4].tag .. tostring(calls[2][5].resumed)") == "aZS_MineAtrue|bZS_OtherBtrue");
    CHECK(run("return tostring(store.ai_states.a.after > rec_after)") == "true");
    lua.script("ticks(60)");
    CHECK(run("return #calls") == "2");              // once per world.ready, not every frame

    // a record of an NPC that is not in the savegame goes, one of a state that is not defined any more is dropped with a warning; the others are restored
    lua.script("calls = {} store.ai_states.c = { state = 'ZS_Mine', args = {} } store.ai_states.b.state = 'ZS_Gone' world.c = nil lines = {}");
    lua.script("fire('world.ready', 'load') ticks(30)");
    CHECK(run("return #calls .. '|' .. calls[1][2] .. '|' .. ids()") == "1|a|a");
    CHECK(run("return #lines .. lines[1]:sub(1, 1) .. tostring(lines[1]:find('ZS_Gone') ~= nil)") == "1Wtrue");

    // a session that stops before the 30 frames are over restores nothing
    lua.script("calls = {} fire('world.ready', 'load') ticks(10) fire('session.stop') ticks(60)");
    CHECK(run("return #calls") == "0");
    // a refusal of the native is a warning, not an error
    lua.script("calls = {} lines = {} refuse = 'the engine said no' fire('world.ready', 'load') ticks(30)");
    CHECK(run("return #lines .. lines[1]:sub(1, 1)") == "1W");
}
