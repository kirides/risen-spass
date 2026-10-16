// ---- mod manager and hot reload (M15): load plans, mods.toml, versions, the mod state machine of the Runtime, risen.migrate --------------------------------------
// Included by test_main.cpp inside its anonymous namespace.

namespace {

std::vector<std::string> idsOf(const std::vector<spass::lua::ModInfo>& v) {
    std::vector<std::string> out;
    for (const auto& m : v) out.push_back(m.id);
    return out;
}

std::string toml(const std::string& id, const std::string& extra = {}) { return "id = \"" + id + "\"\nversion = \"1.0.0\"\n" + extra; }

}  // namespace

TEST(mods_plan_default_order_is_alphabetical_and_unchanged_without_a_config) {
    TempDir t;
    t.write("c/mod.toml", toml("c"));
    t.write("a/mod.toml", toml("a", "load_after = [\"c\"]\n"));
    t.write("b/mod.toml", toml("b"));
    const auto plan = spass::lua::planLoad(spass::lua::discoverMods(t.path), spass::lua::UserConfig{});
    CHECK(idsOf(plan.ordered) == (std::vector<std::string>{"b", "c", "a"}));   // a waits for c, ties by id
    CHECK(plan.skipped.empty() && plan.warnings.empty());
    CHECK(idsOf(spass::lua::resolveLoadOrder(spass::lua::discoverMods(t.path))) == idsOf(plan.ordered));
}

TEST(mods_plan_disabled_dependency_and_custom_order) {
    TempDir t;
    t.write("base/mod.toml", toml("base"));
    t.write("ext/mod.toml", toml("ext", "requires = [\"base\"]\n"));
    t.write("solo/mod.toml", toml("solo"));
    t.write("zed/mod.toml", toml("zed"));
    const auto mods = spass::lua::discoverMods(t.path);

    spass::lua::UserConfig cfg;
    cfg.order = {"zed", "solo", "ext", "base"};   // ext before base is impossible: base wins
    auto plan = spass::lua::planLoad(mods, cfg);
    CHECK(idsOf(plan.ordered) == (std::vector<std::string>{"zed", "solo", "base", "ext"}));
    CHECK(plan.warnings.size() == 1 && plan.warnings[0].find("'ext' before 'base'") != std::string::npos);

    // switching base off takes ext with it, with a reason
    cfg = {};
    cfg.enabled["base"] = false;
    plan = spass::lua::planLoad(mods, cfg);
    CHECK(idsOf(plan.ordered) == (std::vector<std::string>{"solo", "zed"}));
    CHECK(plan.skipped.size() == 2);
    for (const auto& s : plan.skipped) {
        if (s.mod.id == "base") CHECK(s.kind == "disabled");
        if (s.mod.id == "ext") CHECK(s.kind == "dependency_unavailable" && s.reason.find("'base'") != std::string::npos);
    }

    // an entry that is not loaded is only a warning
    cfg = {};
    cfg.order = {"nobody"};
    plan = spass::lua::planLoad(mods, cfg);
    CHECK(plan.ordered.size() == 4 && plan.warnings.size() == 1);
}

TEST(mods_check_order_rejects_a_mod_before_its_dependencies) {
    TempDir t;
    t.write("base/mod.toml", toml("base"));
    t.write("ext/mod.toml", toml("ext", "requires = [\"base\"]\n"));
    t.write("late/mod.toml", toml("late", "load_after = [\"ext\"]\n"));
    const auto mods = spass::lua::discoverMods(t.path);
    CHECK(spass::lua::checkOrder(mods, {"base", "ext", "late"}).empty());
    CHECK(spass::lua::checkOrder(mods, {"late", "base"}).empty());                 // ext is not listed: nothing to violate
    CHECK(spass::lua::checkOrder(mods, {"ext", "base"}).find("'ext' cannot be loaded before 'base' (requires)") == 0);
    CHECK(spass::lua::checkOrder(mods, {"late", "ext"}).find("(load_after)") != std::string::npos);
    CHECK(spass::lua::checkOrder(mods, {"base", "base"}).find("twice") != std::string::npos);
    CHECK(spass::lua::checkOrder(mods, {"ghost"}).find("not a known mod") != std::string::npos);
    CHECK(spass::lua::checkOrder(mods, {}).empty());
}

TEST(mods_dependents_are_transitive_and_in_dependency_order) {
    TempDir t;
    t.write("a/mod.toml", toml("a"));
    t.write("b/mod.toml", toml("b", "requires = [\"a\"]\n"));
    t.write("c/mod.toml", toml("c", "requires = [\"b\"]\n"));
    t.write("d/mod.toml", toml("d", "load_after = [\"a\"]\n"));   // load_after is not a dependency
    const auto mods = spass::lua::discoverMods(t.path);
    CHECK(spass::lua::dependentsOf(mods, "a") == (std::vector<std::string>{"b", "c"}));
    CHECK(spass::lua::dependentsOf(mods, "b") == (std::vector<std::string>{"c"}));
    CHECK(spass::lua::dependentsOf(mods, "c").empty());
    CHECK(spass::lua::dependentsOf(mods, "ghost").empty());
}

TEST(mods_user_config_roundtrip_and_damage) {
    TempDir t;
    spass::lua::UserConfig cfg;
    cfg.order = {"b", "a"};
    cfg.enabled["x"] = false;
    cfg.enabled["y"] = true;
    std::string err;
    CHECK(spass::lua::saveUserConfig(t.path / "sub" / "mods.toml", cfg, err));
    std::vector<std::string> problems;
    auto back = spass::lua::loadUserConfig(t.path / "sub" / "mods.toml", problems);
    CHECK(problems.empty() && back.order == cfg.order && back.enabled == cfg.enabled);
    CHECK(!back.isEnabled("x") && back.isEnabled("y") && back.isEnabled("never_mentioned"));
    // no file: empty configuration, no problem
    problems.clear();
    CHECK(spass::lua::loadUserConfig(t.path / "none.toml", problems).empty() && problems.empty());
    // damaged file: ignored with a problem
    t.write("bad.toml", "order = [\n");
    back = spass::lua::loadUserConfig(t.path / "bad.toml", problems);
    CHECK(back.empty() && problems.size() == 1);
    // wrong types: those entries are dropped with a problem each
    problems.clear();
    t.write("types.toml", "order = [\"ok\", 5, \"ok\", \"Not Valid\"]\n[enabled]\nfine = false\nwrong = \"no\"\n");
    back = spass::lua::loadUserConfig(t.path / "types.toml", problems);
    CHECK(back.order == (std::vector<std::string>{"ok"}) && back.enabled.size() == 1 && problems.size() == 4);
}

TEST(mods_versions_compare_and_match) {
    using spass::lua::compareVersions;
    using spass::lua::versionMatches;
    CHECK(compareVersions("1.0.0", "1.0.0") == 0);
    CHECK(compareVersions("1.0", "1.0.0") == 0);
    CHECK(compareVersions("1.2.0", "1.10.0") < 0);          // numbers, not text
    CHECK(compareVersions("2.0.0", "1.99.99") > 0);
    CHECK(compareVersions("1.0.0-beta", "1.0.0") < 0);
    CHECK(compareVersions("1.0.0-alpha", "1.0.0-beta") < 0);
    CHECK(compareVersions("v1.5", "1.5") == 0);
    CHECK(versionMatches("1.0.0", "1.0.0") == std::optional<bool>(true));
    CHECK(versionMatches("1.0.0", "1.0.1") == std::optional<bool>(false));
    CHECK(versionMatches("<2.0.0", "1.9") == std::optional<bool>(true));
    CHECK(versionMatches("<2.0.0", "2.0") == std::optional<bool>(false));
    CHECK(versionMatches("<=2.0.0", "2.0") == std::optional<bool>(true));
    CHECK(versionMatches(">=1.0, <1.5", "1.2") == std::optional<bool>(true));
    CHECK(versionMatches(">=1.0, <1.5", "1.5") == std::optional<bool>(false));
    CHECK(versionMatches("!=1.3", "1.4") == std::optional<bool>(true));
    CHECK(versionMatches("*", "0.0.1") == std::optional<bool>(true));
    CHECK(!versionMatches("", "1.0").has_value());
    CHECK(!versionMatches("banana", "1.0").has_value());
    CHECK(!versionMatches("<", "1.0").has_value());
    CHECK(!versionMatches("1.0,,2.0", "1.0").has_value());
    CHECK(!versionMatches("~1.0", "1.0").has_value());
}

TEST(mods_manifest_issues_and_retire_table) {
    TempDir t;
    t.write("ok/mod.toml", toml("ok", "[retire]\nnpcs = [\"Old_One\", \"Old_Two\"]\nquests = [\"Q_Old\"]\n"));
    t.write("broken/mod.toml", "id = \n");
    t.write("future/mod.toml", "id = \"future\"\napi_version = 99\n");
    t.write("noid/mod.toml", "name = \"x\"\n");
    std::vector<spass::lua::ModIssue> issues;
    const auto mods = spass::lua::discoverMods(t.path, &issues);
    CHECK(mods.size() == 1 && mods[0].id == "ok");
    CHECK(mods[0].retire.at("npcs") == (std::vector<std::string>{"Old_One", "Old_Two"}) && mods[0].retire.at("quests").size() == 1);
    CHECK(issues.size() == 3);
    bool sawFuture = false;
    for (const auto& i : issues) {
        if (i.folder == "future") sawFuture = i.reason.find("API version 99") != std::string::npos;
    }
    CHECK(sawFuture);
}

// ---- the Runtime as a state machine (no engine: plain Lua mods) --------------------------------------------------------------------------------------

namespace {

std::string probe(spass::lua::Runtime& rt, const char* event) {
    const sol::object r = rt.emitUntilHandled(event);
    return r.valid() && r.get_type() == sol::type::string ? r.as<std::string>() : std::string("<nil>");
}

}  // namespace

TEST(runtime_reload_replaces_handlers_and_carries_state) {
    TempDir t;
    t.write("one/mod.toml", toml("one"));
    t.write("one/main.lua",
            "local n = (mod.reload and mod.reload.state and mod.reload.state.n or 0) + 1\n"
            "risen.on('t.who', function() return 'one v1 run ' .. n end)\n"
            "risen.on('t.count', function() return 'x' end)\n"
            "risen.on_unload(function(reason) return { n = n, reason = reason } end)\n");
    t.write("two/mod.toml", toml("two", "requires = [\"one\"]\n"));
    t.write("two/main.lua", "runs = (runs or 0) + 1\nrisen.on('t.two', function() return 'two' end)\n");
    t.write("three/mod.toml", toml("three"));
    t.write("three/main.lua", "risen.on('t.three', function() return 'three' end)\n");

    auto& rt = spass::lua::Runtime::instance();
    CHECK(rt.start(t.path, false));
    CHECK(probe(rt, "t.who") == "one v1 run 1");
    CHECK(rt.loadOrder() == (std::vector<std::string>{"one", "three", "two"}));

    // edit the file on disk, reload now: the old handler is gone (not doubled), the new one answers, the state came through, the dependent re-ran
    t.write("one/main.lua",
            "local n = (mod.reload and mod.reload.state and mod.reload.state.n or 0) + 1\n"
            "risen.on('t.who', function() return 'one v2 run ' .. n .. ' from ' .. tostring(mod.reload.from_version) .. ' after ' .. mod.reload.state.reason end)\n"
            "risen.on_unload(function(reason) return { n = n, reason = reason } end)\n");
    auto report = rt.reload("one", /*now=*/true);
    CHECK(report.ok && !report.queued && report.actions.size() == 2);
    CHECK(report.actions.size() == 2 && report.actions[0].id == "one" && report.actions[0].action == "reloaded");
    CHECK(report.actions.size() == 2 && report.actions[1].id == "two" && report.actions[1].action == "reloaded");
    CHECK(report.actions[0].handlersRemoved == 2 && report.actions[0].handlersAdded == 1 && report.actions[0].versionBefore == "1.0.0");
    CHECK(probe(rt, "t.who") == "one v2 run 2 from 1.0.0 after reload");
    CHECK(!rt.hasHandlers("t.count"));                       // the handler the new version no longer registers is gone
    CHECK(probe(rt, "t.two") == "two" && probe(rt, "t.three") == "three");
    {
        // exactly one handler per event: a second emit of a counting event does not double
        const auto view = rt.modView("one");
        CHECK(view && view->state == spass::lua::ModState::Loaded && view->loadCount == 2 && view->neededBy == std::vector<std::string>{"two"});
    }
    CHECK(rt.lastReport("one").has_value() && rt.lastReport("one")->ok);
    rt.stop();
}

TEST(runtime_failed_reload_is_refused_or_leaves_a_clean_failed_mod) {
    TempDir t;
    t.write("one/mod.toml", toml("one"));
    t.write("one/main.lua", "risen.on('t.who', function() return 'v1' end)\n");
    t.write("two/mod.toml", toml("two", "requires = [\"one\"]\n"));
    t.write("two/main.lua", "risen.on('t.two', function() return 'two' end)\n");
    auto& rt = spass::lua::Runtime::instance();
    CHECK(rt.start(t.path, false));

    // a syntax error: refused before anything is touched, the old version keeps running
    t.write("one/main.lua", "risen.on('t.who', function() return 'broken' end\n");
    auto report = rt.reload("one", true);
    CHECK(!report.ok && report.actions.empty() && !report.error.empty());
    CHECK(probe(rt, "t.who") == "v1" && probe(rt, "t.two") == "two");

    // a runtime error: the mod ends up Failed with none of its half-made registrations, the dependent is skipped
    t.write("one/main.lua", "risen.on('t.half', function() return 'half' end)\nerror('boom at load')\n");
    report = rt.reload("one", true);
    CHECK(!report.ok && report.error.find("boom at load") != std::string::npos);
    CHECK(!rt.hasHandlers("t.half") && !rt.hasHandlers("t.who") && !rt.hasHandlers("t.two"));
    auto view = rt.modView("one");
    CHECK(view && view->state == spass::lua::ModState::Failed && view->kind == "load_error");
    view = rt.modView("two");
    CHECK(view && view->state == spass::lua::ModState::Skipped && view->reason.find("'one'") != std::string::npos);

    // fixed on disk: reloads, the dependent comes back after it
    t.write("one/main.lua", "risen.on('t.who', function() return 'v3' end)\n");
    report = rt.reload("one", true);
    CHECK(report.ok);
    view = rt.modView("two");
    CHECK(!view || view->state == spass::lua::ModState::Skipped);   // a skipped dependent is brought back by load(), not by the reload of its dependency
    auto loadReport = rt.load("two");
    CHECK(loadReport.ok && probe(rt, "t.two") == "two" && probe(rt, "t.who") == "v3");
    rt.stop();
}

TEST(runtime_unload_cascade_disable_and_the_mods_toml) {
    TempDir t;
    t.write("mods/base/mod.toml", toml("base"));
    t.write("mods/base/main.lua", "risen.on('t.base', function() return 'base' end)\n");
    t.write("mods/ext/mod.toml", toml("ext", "requires = [\"base\"]\n"));
    t.write("mods/ext/main.lua", "risen.on('t.ext', function() return 'ext' end)\n");
    t.write("mods/solo/mod.toml", toml("solo"));
    t.write("mods/solo/main.lua", "risen.on('t.solo', function() return 'solo' end)\n");
    const auto cfgFile = t.path / "mods.toml";
    auto& rt = spass::lua::Runtime::instance();
    CHECK(rt.start(t.path / "mods", false, cfgFile));
    CHECK(rt.loadOrder() == (std::vector<std::string>{"base", "ext", "solo"}));

    // unloading a mod that is required needs the cascade
    auto r = rt.unload("base", false);
    CHECK(!r.ok && r.error.find("ext") != std::string::npos && rt.hasHandlers("t.base"));
    // disabling it is refused likewise, and nothing is written
    r = rt.setEnabled("base", false, false, true);
    CHECK(!r.ok && !std::filesystem::exists(cfgFile));
    // with the cascade both go, are marked disabled and the choice is on disk
    r = rt.setEnabled("base", false, true, true);
    CHECK(r.ok && r.actions.size() == 2 && !rt.hasHandlers("t.base") && !rt.hasHandlers("t.ext") && rt.hasHandlers("t.solo"));
    CHECK(rt.modView("ext")->state == spass::lua::ModState::Disabled && !rt.modView("ext")->userEnabled);
    CHECK(rt.loadOrder() == (std::vector<std::string>{"solo"}));
    std::vector<std::string> problems;
    auto saved = spass::lua::loadUserConfig(cfgFile, problems);
    CHECK(!saved.isEnabled("base") && !saved.isEnabled("ext") && saved.isEnabled("solo"));
    // enabling ext alone needs base: refused without the cascade, loads both with it
    r = rt.setEnabled("ext", true, false, true);
    CHECK(!r.ok && !rt.hasHandlers("t.ext"));
    r = rt.setEnabled("ext", true, true, true);
    CHECK(r.ok && rt.hasHandlers("t.base") && rt.hasHandlers("t.ext"));
    CHECK(rt.loadOrder() == (std::vector<std::string>{"base", "ext", "solo"}));
    // a custom order is checked against the constraints, saved, and not applied to the running order
    CHECK(!rt.setOrder({"ext", "base"}).empty());
    CHECK(rt.setOrder({"solo", "base"}).empty());
    CHECK(rt.plannedOrder(true) == (std::vector<std::string>{"solo", "base", "ext"}));
    CHECK(rt.plannedOrder(false) == (std::vector<std::string>{"base", "ext", "solo"}));
    rt.stop();
    // the next start honours the file: solo first
    CHECK(rt.start(t.path / "mods", false, cfgFile));
    CHECK(rt.loadOrder() == (std::vector<std::string>{"solo", "base", "ext"}));
    rt.stop();
    // ... and a disabled mod stays off
    spass::lua::UserConfig off;
    off.enabled["solo"] = false;
    std::string err;
    CHECK(spass::lua::saveUserConfig(cfgFile, off, err));
    CHECK(rt.start(t.path / "mods", false, cfgFile));
    CHECK(rt.loadOrder() == (std::vector<std::string>{"base", "ext"}) && rt.modView("solo")->state == spass::lua::ModState::Disabled);
    rt.stop();
}

TEST(runtime_replays_the_lifecycle_to_a_reloaded_mod_only) {
    TempDir t;
    t.write("one/mod.toml", toml("one"));
    t.write("one/main.lua",
            "seen = {}\n"
            "for _, e in ipairs { 'mods.loaded', 'session.start', 'world.ready' } do\n"
            "  risen.on(e, function(arg) seen[#seen + 1] = e .. ':' .. tostring(arg) end)\n"
            "end\n"
            "risen.on('t.seen', function() return table.concat(seen, ',') end)\n");
    t.write("two/mod.toml", toml("two"));
    t.write("two/main.lua", "count = 0\nrisen.on('session.start', function() count = count + 1 end)\nrisen.on('t.count', function() return tostring(count) end)\n");
    auto& rt = spass::lua::Runtime::instance();
    CHECK(rt.start(t.path, false));
    CHECK(probe(rt, "t.seen") == "mods.loaded:nil");
    rt.noteSessionStart("new");
    rt.emit("session.start", std::string("new"));
    rt.noteWorldReady("new");
    rt.emit("world.ready", std::string("new"));
    CHECK(probe(rt, "t.seen") == "mods.loaded:nil,session.start:new,world.ready:new");
    CHECK(probe(rt, "t.count") == "1");
    auto r = rt.reload("one", true);
    CHECK(r.ok);
    // the new instance was told what it missed, in order; mod two heard nothing
    CHECK(probe(rt, "t.seen") == "mods.loaded:nil,session.start:new,world.ready:new");
    CHECK(probe(rt, "t.count") == "1");
    // after the session stopped nothing is replayed
    rt.noteSessionStop();
    r = rt.reload("one", true);
    CHECK(r.ok && probe(rt, "t.seen") == "mods.loaded:nil");
    rt.stop();
}

TEST(runtime_queues_a_reload_requested_from_a_running_handler) {
    TempDir t;
    auto& rt = spass::lua::Runtime::instance();
    rt.addBinding([](sol::state_view lua, sol::table risen) {
        // a stand-in for risen.mods.reload(id, { now = true }): the handler of the mod itself is on the stack
        risen.set_function("hotreload_request", [](const std::string& id) {
            const auto r = spass::lua::Runtime::instance().reload(id, /*now=*/true);
            return r.queued ? std::string("queued") : (r.ok ? std::string("done") : "refused: " + r.error);
        });
        (void)lua;
    });
    t.write("one/mod.toml", toml("one"));
    t.write("one/main.lua", "risen.on('t.self', function() return risen.hotreload_request('one') end)\nrisen.on('t.v', function() return 'v1' end)\n");
    CHECK(rt.start(t.path, false));
    CHECK(probe(rt, "t.self") == "queued");                // the handler of the very mod is running: it must wait
    CHECK(probe(rt, "t.v") == "v1");
    t.write("one/main.lua", "risen.on('t.self', function() return 'asked' end)\nrisen.on('t.v', function() return 'v2' end)\n");
    rt.processPending();                                   // the start of the next frame
    CHECK(probe(rt, "t.v") == "v2" && probe(rt, "t.self") == "asked");
    rt.noteSessionStart("new");
    const auto queued = rt.reload("one", false);           // a session runs: the default is the next frame
    CHECK(queued.queued);
    rt.processPending();
    CHECK(rt.modView("one")->loadCount == 3);
    CHECK(rt.reload("ghost", true).error.find("no mod") == 0);
    CHECK(!rt.reload("", true).ok);
    rt.noteSessionStop();
    rt.stop();
}

TEST(runtime_wraps_registering_functions_with_the_owner_and_tears_down) {
    // a registering native sees the mod that called it; the teardown it registered is called with that mod's id
    static std::vector<std::string> registered;
    static std::vector<std::string> torn;
    registered.clear();
    torn.clear();
    auto& rt = spass::lua::Runtime::instance();
    rt.addBinding([](sol::state_view lua, sol::table risen) {
        sol::table script = lua.create_table();
        script.set_function("define", [](const std::string& name) { registered.push_back(spass::lua::Runtime::currentOwner() + ":" + name); });
        script.set_function("boom", [](sol::this_state) -> int { throw sol::error("native boom"); });
        risen["script"] = script;
    });
    rt.addTeardown([](const std::string& mod, std::vector<std::string>& released) {
        torn.push_back(mod);
        released.push_back("fake registration of " + mod);
    });
    TempDir t;
    t.write("one/mod.toml", toml("one"));
    t.write("one/main.lua", "risen.script.define('A')\nrisen.on('t.again', function() risen.script.define('B') return 'x' end)\nrisen.on('t.err', function() return select(2, pcall(risen.script.define)) end)\n");
    t.write("two/mod.toml", toml("two"));
    t.write("two/main.lua", "risen.script.define('C')\n");
    CHECK(rt.start(t.path, false));
    CHECK(registered == (std::vector<std::string>{"one:A", "two:C"}));
    probe(rt, "t.again");
    CHECK(registered.back() == "one:B");
    CHECK(spass::lua::Runtime::currentOwner().empty());                      // restored after every call
    const auto err = probe(rt, "t.err");
    CHECK(err.find("define") != std::string::npos || err != "<nil>");        // an error passes through the wrapper unchanged
    CHECK(spass::lua::Runtime::currentOwner().empty());
    auto r = rt.reload("one", true);
    CHECK(r.ok && torn == (std::vector<std::string>{"one"}));
    CHECK(r.actions[0].released == (std::vector<std::string>{"fake registration of one"}));
    CHECK(registered.size() == 4 && registered.back() == "one:A");           // main.lua ran again as mod one
    r = rt.unload("two", false);
    CHECK(r.ok && r.actions[0].action == "unloaded" && torn.back() == "two");
    CHECK(rt.modView("two")->state == spass::lua::ModState::Unloaded);
    r = rt.load("two");
    CHECK(r.ok && registered.back() == "two:C");
    rt.stop();
}

// risen.migrate: the per-mod chunk against a mocked API (version_matches is the real native logic)
TEST(migrate_runs_matching_hooks_in_order_and_survives_errors) {
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math);
    lua.script(R"LUA(
        log = { info = function(...) logged[#logged + 1] = table.concat({ ... }, " ") end, error = function(...) errors[#errors + 1] = table.concat({ ... }, " ") end }
        logged, errors, order, handlers, data = {}, {}, {}, {}, { count = 1 }
        api = {
            log = log,
            save = { get = function(k) return data[k] end, set = function(k, v) data[k] = v end },
            mods = {},
            on = function(event, fn, prio) handlers[event] = { fn = fn, prio = prio } end,
        }
    )LUA");
    spass::game::installMigrateModApi(lua, lua["api"], [] {
        spass::lua::ModInfo m;
        m.id = "mymod";
        m.version = "2.0.0";
        return m;
    }());
    // the mock's api.mods needs the native version helper: bind the real one on the mock table through bindMods' pure helpers
    lua.script(R"LUA(
        function api.mods.saved_version(id) return saved end
    )LUA");
    // the real natives (bindMods needs an engine only for the lists; the version helpers are pure)
    sol::table risen = lua.create_table();
    spass::game::bindMods(lua, risen);
    lua["api"]["mods"]["version_matches"] = risen["mods"]["version_matches"];
    lua.script(R"LUA(
        local ok, why = api.migrate("banana", function() end)
        assert(ok == nil and why:find("not a version specification"), why)
        ok, why = api.migrate(5, function() end)
        assert(ok == nil)
        ok, why = api.migrate("<2.0.0", "not a function")
        assert(ok == nil)
        assert(api.migrate("<2.0.0", function(ctx) order[#order + 1] = "lt2:" .. ctx.from .. ">" .. ctx.to; ctx.save.set("count", ctx.save.get("count") + 10) end))
        assert(api.migrate("1.0.0", function(ctx) order[#order + 1] = "exactly1" ; error("migration blew up") end))
        assert(api.migrate(">=1.5", function(ctx) order[#order + 1] = "ge15" end))
        assert(api.migrate("*", function(ctx) order[#order + 1] = "any" end))
        assert(handlers["load.before"] and handlers["load.before"].prio == 1000 and handlers["mods.migrate"])
        -- a savegame written by 1.0.0
        saved = "1.0.0"
        handlers["load.before"].fn()
        assert(table.concat(order, ",") == "lt2:1.0.0>2.0.0,exactly1,any", table.concat(order, ","))
        assert(#errors == 1 and errors[1]:find("migration blew up"))
        assert(data.count == 11)
        -- the same version: nothing to do; an unknown version (no record): nothing to do
        order = {}
        saved = "2.0.0"; handlers["load.before"].fn()
        saved = nil; handlers["load.before"].fn()
        assert(#order == 0)
        -- a reload of the mod: the previous instance's version is the "from"
        handlers["mods.migrate"].fn("1.7.0", "2.0.0")
        assert(table.concat(order, ",") == "lt2:1.7.0>2.0.0,ge15,any", table.concat(order, ","))
    )LUA");
}

TEST(test_run_suite_appends_scenarios_with_prefixed_checks) {
    spass::testrun::Run suite;
    suite.setScenario("suite");
    spass::testrun::Run a;
    a.setScenario("alpha");
    a.expect("one", true, "fine", 1.0);
    a.expect("two", false, "bad", 2.0);
    a.plan({"one", "two", "three"});
    suite.appendFrom(a, "alpha", "", 5.0);
    spass::testrun::Run empty;
    suite.appendFrom(empty, "beta", "", 6.0);
    spass::testrun::Run aborted;
    aborted.expect("x", true, "", 1.0);
    suite.appendFrom(aborted, "gamma", "aborted: boom", 7.0);
    const std::string json = suite.toJson("", "done", 9.0);
    CHECK(suite.total() == 2 + 1 + 1 + 1 + 1);   // alpha: one, two, "unfinished three"; beta: nothing recorded; gamma: x, early end -> 7
    CHECK(json.find("\"name\": \"alpha: one\", \"ok\": true") != std::string::npos);
    CHECK(json.find("\"name\": \"alpha: two\", \"ok\": false") != std::string::npos);
    CHECK(json.find("\"name\": \"alpha: unfinished\", \"ok\": false, \"detail\": \"three\"") != std::string::npos);
    CHECK(json.find("beta: no check was recorded") != std::string::npos);
    CHECK(json.find("gamma: scenario ended early") != std::string::npos && json.find("aborted: boom") != std::string::npos);
    CHECK(!suite.verdict("").pass);
}

// risen.retire against a mocked world
TEST(retire_declares_validates_and_applies_when_the_world_is_ready) {
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math);
    lua.script(R"LUA(
        handlers, nexth = {}, 1
        killed, renamed, removed_quests, removed_vars, taken = {}, {}, {}, {}, {}
        store = { persistent_npcs = { Old = { id = "id-old", template = "T" }, Keep = { id = "id-keep", template = "T" } }, spawn_rules = { Old = { state = "present" } },
                  ai_states = { ["id-old"] = { state = "ZS_X" } }, npc_routines = { npcs = { ["id-old"] = {} } }, junk = 1 }
        local function entity(id, name)
            local e = { _id = id, _name = name }
            function e:valid() return true end
            function e:id() return self._id end
            function e:name() return self._name end
            function e:set_name(n) renamed[#renamed + 1] = n return true end
            function e:kill() killed[#killed + 1] = self._id return true end
            return e
        end
        world = { ["id-old"] = entity("id-old", "Peasant_1_02") }
        declared_by_mod = { Keep = true }
        api = {
            log = { info = function() end, warn = function() end, error = function(...) errors = (errors or "") .. table.concat({ ... }, " ") end },
            save = { get = function(k) return store[k] end, remove = function(k) store[k] = nil end, set = function(k, v) store[k] = v end },
            npc = { persistent_get = function(n) return declared_by_mod[n] and {} or nil end, clear_routine = function() end, persistent_list = function() return { { name = "Keep", template = "T" } } end },
            entity = { by_id = function(id) return world[id] end, get = function(name) return nil end, player = function() return nil end },
            quest = { exists = function(n) return n == "Q_Old" end, remove = function(n) removed_quests[#removed_quests + 1] = n return true end },
            gamevar = { exists = function(n) return n == "OldVar" end, remove = function(n) removed_vars[#removed_vars + 1] = n return true end },
            item = { count = function(p, t) return t == "It_Old" and 3 or 0 end, take = function(p, t, n) taken[#taken + 1] = t .. "=" .. n return true end },
            on = function(event, fn) local h = nexth nexth = nexth + 1 handlers[h] = { event = event, fn = fn } return h end,
            off = function(h) handlers[h] = nil end,
            on_unload = function(f) on_unload_fn = f end,
        }
        registered = nil
        function register(id, handle) registered = handle end
        player = {}
        api.entity.player = function() return player end
    )LUA");
    sol::load_result chunk = lua.load(spass::game::kRetireChunk, "=risen.retire");
    CHECK(chunk.valid());
    sol::protected_function fn = chunk;
    const auto r = fn(lua["api"], std::string("mymod"), sol::lua_nil, lua["register"]);
    CHECK(r.valid());
    auto run = [&](const char* code) { return lua.script(code).get<std::string>(); };
    // wrong shapes are refused with a reason, nothing is declared
    CHECK(run("local ok, why = api.retire('x') return tostring(ok) .. '|' .. tostring(why:find('expects a table', 1, true) ~= nil)") == "nil|true");
    CHECK(run("local ok, why = api.retire({ pets = {} }) return tostring(ok) .. '|' .. tostring(why:find('unknown kind', 1, true) ~= nil)") == "nil|true");
    CHECK(run("local ok, why = api.retire({ npcs = { 5 } }) return tostring(ok)") == "nil");
    CHECK(run("return tostring(api.retire({ npcs = { 'Old', 'Keep', 'Ghost' }, quests = { 'Q_Old', 'Q_Never' }, game_vars = { 'OldVar' }, items = { 'It_Old' }, save_keys = { 'junk' } }))") == "true");
    // the dry run reports and changes nothing
    CHECK(run("local d = registered.apply(true) return d.npcs[1].name .. ':' .. d.npcs[1].entities .. ' ' .. tostring(d.npcs[2].skipped) .. ' ' .. tostring(d.quests[1].exists) .. tostring(d.quests[2].exists) .. ' ' .. d.items[1].count .. ' removed=' .. d.removed") ==
          "Old:1 this mod still declares it truefalse 3 removed=0");
    CHECK(lua["killed"].get<sol::table>().size() == 0 && lua["store"]["junk"].get_type() == sol::type::number);
    // the player exists at declaration time in this mock, so the countdown is already armed; world.ready restarts it. After 89 frames nothing, after 90 everything declared goes
    lua.script("for _, h in pairs(handlers) do if h.event == 'world.ready' then h.fn() end end");
    CHECK(run("local tick for _, h in pairs(handlers) do if h.event == 'tick' then tick = h.fn end end for i = 1, 89 do tick() end return tostring(#killed)") == "0");
    lua.script("local tick for _, h in pairs(handlers) do if h.event == 'tick' then tick = h.fn end end tick()");
    CHECK(run("return table.concat(killed, ',') .. ' ' .. table.concat(removed_quests, ',') .. ' ' .. table.concat(removed_vars, ',') .. ' ' .. table.concat(taken, ',')") == "id-old Q_Old OldVar It_Old=3");
    CHECK(run("return tostring(store.persistent_npcs.Old) .. tostring(store.persistent_npcs.Keep ~= nil) .. tostring(store.spawn_rules.Old) .. tostring(store.ai_states['id-old']) .. tostring(store.npc_routines.npcs['id-old']) .. tostring(store.junk)") == "niltruenilnilnilnil");
    CHECK(run("return tostring(select(2, errors:gsub('retire', 'retire')) >= 3)") == "true");   // each refused declaration was logged as an error
    // a torn-down instance stops answering for what it declared (the manager asks `orphans` after it switched the mod off)
    CHECK(lua["registered"].get_type() == sol::type::table);
    lua.script("on_unload_fn()");
    CHECK(lua.script("return registered == nil").get<bool>());
}

TEST(orphans_list_undeclared_npcs_and_strays_of_created_templates_only) {
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math);
    lua.script(R"LUA(
        killed, forgotten = {}, {}
        local function entity(id, name)
            local e = { _id = id, _name = name }
            function e:valid() return true end
            function e:id() return self._id end
            function e:name() return self._name end
            function e:is_npc() return true end
            function e:set_name() return true end
            function e:kill() killed[#killed + 1] = self._id return true end
            return e
        end
        world = { ["id-a"] = entity("id-a", "Peasant_1_02"), ["id-stray"] = entity("id-stray", "") }
        instances = { Made = { entity("id-owned", "Owned"), entity("id-stray", "") }, Shipped = { entity("id-other", "") } }
        risen = { mods = {}, entity = { by_id = function(id) return world[id] end },
            template = { created = function() return { Made = "Base" } end, get = function(t) return instances[t] and { instances = function() return instances[t] end } end },
            npc = {} }
        function risen.mods.record(id)
            if id ~= "upd" then return nil end
            return { persistent_npcs = { Gone = { id = "id-a", template = "Shipped" }, Owned = { id = "id-owned", template = "Made" } } }
        end
        function risen.mods.forget_record(id, key, sub) forgotten[#forgotten + 1] = key .. "/" .. sub return true end
    )LUA");
    sol::load_result chunk = lua.load(spass::game::kModsChunk, "=risen.mods");
    CHECK(chunk.valid());
    sol::protected_function fn = chunk;
    const auto r = fn(lua["risen"]);
    CHECK(r.valid());
    auto run = [&](const char* code) { return lua.script(code).get<std::string>(); };
    // the mod is not running: its record names Gone (entity alive, nameless) -> undeclared; the stray is an NPC of a created template that no record owns; a shipped template is not searched
    CHECK(run("local l = risen.mods.orphans('upd') local t = {} for _, o in ipairs(l) do t[#t + 1] = o.kind .. ':' .. o.name .. ':' .. o.id end return table.concat(t, ',')") ==
          "undeclared:Gone:id-a,stray::id-stray");
    CHECK(run("local l, why = risen.mods.orphans(5) return tostring(l) .. '|' .. why") == "nil|the first argument must be a mod id (a string)");
    CHECK(run("local l = risen.mods.orphans('nobody') return tostring(#l)") == "0");
    // removal only for what the list names; the record of an undeclared one goes too
    CHECK(run("return tostring(risen.mods.remove_orphans('upd', risen.mods.orphans('upd')))") == "2");
    CHECK(run("return table.concat(killed, ',') .. ' ' .. table.concat(forgotten, ',')") == "id-a,id-stray persistent_npcs/Gone,spawn_rules/Gone,ai_states/id-a");
    CHECK(run("local n, why = risen.mods.remove_orphans('upd', 'all') return tostring(n) .. '|' .. why") == "nil|the second argument must be a list returned by risen.mods.orphans");
    CHECK(run("local h, why = risen.mods.retire('upd') return tostring(h)") == "nil");
}
