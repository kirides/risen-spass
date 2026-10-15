// Included by test_main.cpp inside the anonymous namespace: risen.test, the test run bookkeeping and the trace channels.

TEST(trace_channel_lists_are_parsed) {
    using spass::trace::parseChannels;
    constexpr auto bit = [](spass::trace::Channel c) { return std::uint64_t{1} << static_cast<unsigned>(c); };
    const auto a = parseChannels("template, save;events\tmymod,Template");
    CHECK(a.mask == (bit(spass::trace::Channel::Template) | bit(spass::trace::Channel::Save) | bit(spass::trace::Channel::Events)));
    CHECK(a.custom.size() == 1 && a.custom[0] == "mymod" && !a.all);
    CHECK(parseChannels("all").all && parseChannels("*").mask == bit(spass::trace::Channel::Count) - 1);
    CHECK(parseChannels("").mask == 0 && parseChannels(" , ;").custom.empty());
    CHECK(spass::trace::channelNames(parseChannels("save,balance").mask) == "balance,save");
    // enabling and disabling at run time; custom channels by name
    spass::trace::disable("all");
    CHECK(!spass::trace::enabled(spass::trace::Channel::Save) && !spass::trace::enabledName("mymod"));
    spass::trace::enable("save,mymod");
    CHECK(spass::trace::enabled(spass::trace::Channel::Save) && spass::trace::enabledName("MyMod") && !spass::trace::enabledName("template"));
    CHECK(spass::trace::enabledList() == "save,mymod");
    spass::trace::disable("save");
    CHECK(!spass::trace::enabled(spass::trace::Channel::Save) && spass::trace::enabledName("mymod"));
    spass::trace::disable("all");
    CHECK(spass::trace::enabledList().empty());
}

TEST(test_run_bookkeeping_and_result_json) {
    using spass::testrun::jsonString;
    CHECK(jsonString("a\"b\\c\n") == "\"a\\\"b\\\\c\\n\"");
    CHECK(jsonString(std::string("x\x01y")) == "\"x\\u0001y\"");
    CHECK(jsonString("caf\xC3\xA9") == "\"caf\xC3\xA9\"");  // valid UTF-8 is kept
    CHECK(jsonString("bad\xE9!") == "\"bad?!\"");            // a lone Latin-1 byte becomes '?'
    CHECK(jsonString("cut\xE2\x82") == "\"cut??\"");         // so does a truncated sequence

    spass::testrun::Run run;
    CHECK(!run.verdict("").pass && run.verdict("").reason == "no check was recorded");
    run.setScenario("demo");
    run.section("first");
    CHECK(run.expect("a", true, "fine", 1.0));
    CHECK(!run.expect("b", false, "went \"wrong\"", 2.0));
    CHECK(run.total() == 2 && run.failed() == 1);
    CHECK(!run.verdict("").pass);
    run.plan({"a", "c"});
    CHECK(run.unfinished() == std::vector<std::string>{"c"});
    const auto wait = run.beginWait("flag (timeout 5s)");
    CHECK(run.unfinished().size() == 2 && run.unfinished()[1] == "waiting: flag (timeout 5s)");
    run.endWait(wait);
    run.mark("first_tick", 3.5);
    run.mark("first_tick", 9.0);  // the first mark counts
    const std::string json = run.toJson("", "summary", 12.5);
    CHECK(json.find("\"scenario\": \"demo\"") != std::string::npos);
    CHECK(json.find("\"status\": \"fail\"") != std::string::npos);
    CHECK(json.find("\"first_tick\": 3.500") != std::string::npos);
    CHECK(json.find("\"detail\": \"went \\\"wrong\\\"\"") != std::string::npos);
    CHECK(json.find("\"unfinished\": [\"c\"]") != std::string::npos);
    CHECK(json.find("\"counts\": {\"total\": 2, \"passed\": 1, \"failed\": 1}") != std::string::npos);

    spass::testrun::Run good;
    good.expect("x", true, "", 0.1);
    CHECK(good.verdict("").pass);
    CHECK(!good.verdict("timeout").pass && good.verdict("timeout").reason == "timeout");
    good.plan({"y"});
    CHECK(!good.verdict("").pass);  // a planned check that never ran
    good.expect("y", true, "", 0.2);
    CHECK(good.verdict("").pass && good.toJson("", "", 1).find("\"status\": \"pass\"") != std::string::npos);
}

TEST(risen_test_lua_api_checks_waits_and_scenarios) {
    namespace test = spass::game::test;
    test::resetForTests();
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math, sol::lib::debug);
    sol::table risen = lua.create_table();
    lua["risen"] = risen;
    test::bindTest(lua, risen);
    auto& run = test::currentRun();

    lua.script(
        "local T = risen.test\n"
        "assert(T.expect('plain', 1 == 1, 'detail') == true)\n"
        "assert(T.expect('nil is false', nil) == false)\n"
        "assert(T.eq('number', 3, 3) and T.eq('table', { 1, 2, a = { b = 3 } }, { 1, 2, a = { b = 3 } }) and T.eq('close', 0.1 + 0.2, 0.3, 1e-9))\n"
        "assert(not T.eq('mismatch', { 1, 2 }, { 1, 3 }))\n"
        "assert(T.matches('pattern', 'value=777', 'value=%d+') and not T.matches('no pattern', 'abc', '%d'))\n"
        "assert(T.param('missing', 'fallback') == 'fallback' and T.is_active() == false)\n"
        "T.section('second')\n"
        "assert(not pcall(T.sleep, 1))  -- outside of run()\n");
    CHECK(run.total() == 8 && run.failed() == 3);
    CHECK(run.currentSection() == "second");
    CHECK(run.toJson("", "", 1).find("expected {1, 3}, got {1, 2}") != std::string::npos);

    // a scenario: sleep for ticks, wait for a condition, check, finish (returning from the function calls finish())
    test::resetForTests();
    lua.script(
        "flag = false\n"
        "risen.test.run(function()\n"
        "  risen.test.section('scenario')\n"
        "  risen.test.sleep(2)\n"
        "  risen.test.wait_until(function() return flag end, 5, 'flag')\n"
        "  risen.test.expect('after wait', flag == true)\n"
        "end, 'luaunit')\n");
    CHECK(run.scenario() == "luaunit" && !test::finished());
    test::pollWaits(lua);  // the scenario starts and sleeps 2 ticks
    test::pollWaits(lua);
    CHECK(run.currentSection() == "scenario" && run.total() == 0);
    test::pollWaits(lua);  // sleep over: now waiting for `flag`
    CHECK(run.unfinished().size() == 1 && run.unfinished()[0].rfind("waiting: flag", 0) == 0);
    test::pollWaits(lua);
    CHECK(!test::finished());
    lua["flag"] = true;
    test::pollWaits(lua);
    CHECK(test::finished() && run.total() == 1 && run.verdict("").pass);

    // a timeout fails the check and reports to the callback
    test::resetForTests();
    lua.script("risen.test.wait_for(function() return false end, 0.01, function(ok, why) fired = tostring(ok) .. ':' .. tostring(why) end, 'never')");
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    test::pollWaits(lua);
    CHECK(lua["fired"].get<std::string>() == "false:timeout" && run.failed() == 1 && run.unfinished().empty());

    // an error inside the scenario fails the run (run() allows one scenario per state)
    test::resetForTests();
    sol::state lua2;
    lua2.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math, sol::lib::debug);
    sol::table risen2 = lua2.create_table();
    lua2["risen"] = risen2;
    test::bindTest(lua2, risen2);
    lua2.script("risen.test.run(function() error('kaput') end)");
    test::pollWaits(lua2);
    CHECK(test::finished() && !run.verdict("").pass && run.toJson("", "", 1).find("kaput") != std::string::npos);
    test::resetForTests();
}

TEST(risen_test_wait_event_inside_a_scenario_mod) {
    namespace test = spass::game::test;
    test::resetForTests();
    TempDir t;
    t.write("sc/mod.toml", "id = \"sc\"\n");
    t.write("sc/main.lua",
            "got = nil\n"
            "risen.test.run(function()\n"
            "  local mode, extra = risen.test.wait_event('session.start', 5, function(m) return m == 'load' end)\n"
            "  got = tostring(mode) .. ',' .. tostring(extra)\n"
            "  risen.test.expect('event arrived', mode == 'load')\n"
            "end, 'evt')\n"
            "risen.on('probe', function() return got end)\n");
    auto& rt = spass::lua::Runtime::instance();
    rt.addBinding(spass::game::test::bindTest);
    rt.addModBinding([](sol::state_view lua, sol::table api, const spass::lua::ModInfo&) { spass::game::test::bindTestForMod(lua, api); });
    CHECK(rt.start(t.path, false));
    {
        auto lock = rt.lock();
        test::pollWaits(rt.lua());                     // start of the scenario: registers the event handler
        rt.emit("session.start", std::string("new"));  // filtered out
        test::pollWaits(rt.lua());
        CHECK(!test::finished());
        rt.emit("session.start", std::string("load"), 7);
        test::pollWaits(rt.lua());  // the condition holds, the scenario resumes and finishes
        CHECK(test::finished() && test::currentRun().total() == 1 && test::currentRun().verdict("").pass);
    }
    {
        const sol::object r = rt.emitUntilHandled("probe");
        CHECK(r.valid() && r.is<std::string>() && r.as<std::string>() == "load,7");
    }
    rt.stop();
    test::resetForTests();
}
