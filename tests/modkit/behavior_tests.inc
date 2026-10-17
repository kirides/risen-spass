// Tests of behaviour scripts (core/behavior.h): reading a script like risen.behavior.define checks it, the export data, validation. Included by modkit_tests.cpp.

TEST(behavior_script_reading) {
    const BehaviorInfo ok = read_behavior_code(R"(
        local log = risen.log            -- the layer's API is a stand-in while the editor reads the script
        local function greet(self) log.info(self.params.text) end
        return { description = "greets", params = { text = "Hi", radius = 300, loud = false, items = { "It_Apple", "It_Bread" } },
                 interval = 0.25, near = 400, start = greet, on_quest = function() end, events = { ["gamevar.changed"] = function() end } }
    )", "greeter.lua");
    CHECK(ok.ok);
    CHECK_EQ(ok.description, std::string("greets"));
    CHECK(ok.params.value("radius", 0) == 300 && ok.params["loud"] == false && ok.params["items"].is_array() && ok.params["items"].size() == 2);
    CHECK(ok.params["text"].get<std::string>() == "Hi");
    CHECK(ok.interval == 0.25 && ok.near == 400);
    CHECK(ok.handlers.size() == 2 && ok.handlers[0] == "start" && ok.handlers[1] == "on_quest");
    CHECK(ok.events.size() == 1 && ok.events[0] == "gamevar.changed");
    auto err = [](const char* code) { return read_behavior_code(code, "x.lua").error; };
    CHECK(err("return {").find("x.lua") != std::string::npos);                              // a syntax error names the file
    CHECK(err("return 5").find("return { ... }") != std::string::npos);
    CHECK(err("return { updat = function() end }").find("unknown key 'updat'") != std::string::npos);
    CHECK(err("return { update = 3 }").find("update must be a function") != std::string::npos);
    CHECK(err("return { params = { f = print } }").find("plain values") != std::string::npos);
    CHECK(err("return { interval = -1 }").find("interval") != std::string::npos);
    CHECK(err("return { near = 0 }").find("near must be") != std::string::npos);
    CHECK(err("return { events = { x = 1 } }").find("events") != std::string::npos);
    CHECK(err("while true do end").find("too long") != std::string::npos);                  // the instruction limit
    CHECK(err("require('os') return {}").find("require") != std::string::npos);              // no loaders while reading
    CHECK(behavior_name_problem("greeter").empty() && behavior_name_problem("a.b-c_1").empty());
    CHECK(!behavior_name_problem("1x").empty() && !behavior_name_problem("a b").empty() && !behavior_name_problem("").empty());
    // the starter file of "New behaviour" is a valid behaviour
    const BehaviorInfo starter = read_behavior_code(behavior_template("my_behavior"), "my_behavior.lua");
    CHECK(starter.ok && starter.params.contains("text") && !starter.handlers.empty());
}

TEST(behavior_export_and_validation) {
    Project p = load_showcase();
    // the showcase: greeter on MK_Watcher (params) and on the template MK_Hob_T (defaults)
    const json d = behaviors_data(p);
    CHECK_EQ(d["scripts"][0]["module"].get<std::string>(), std::string("behaviors.greeter"));
    CHECK_EQ(d["npcs"][0]["target"].get<std::string>(), std::string("MK_Watcher"));
    CHECK_EQ(d["npcs"][0]["params"]["gift"].get<std::string>(), std::string("It_Apple"));
    CHECK_EQ(d["templates"][0]["template"].get<std::string>(), std::string("MK_Hob_T"));
    CHECK(d["templates"][0]["params"].is_object() && d["templates"][0]["params"].empty());
    CHECK_EQ(behavior_module_file("a.b-c"), std::string("behaviors/a_b_c.lua"));
    // the project file keeps them
    const Project back = project_from_json(to_json(p));
    CHECK(back.behaviors.size() == 1 && back.npc("MK_Watcher")->behaviors.size() == 1 && back.npc("MK_Watcher")->behaviors[0].params["text"] == "Halt! Who goes there?");
    // the export copies and checks the script; a broken one is refused
    const fs::path dir = temp_dir("bhv") / "modkit_showcase";
    ExportOptions o;
    o.project_dir = showcase_dir();
    const ExportResult r = export_mod(p, u8str(dir), o);
    CHECK(r.ok && fs::exists(dir / "behaviors" / "greeter.lua"));
    const fs::path bad = temp_dir("bhv_bad") / "broken.lua";
    write_text(bad, "return { update = 5 }");
    Project q = p;
    q.behaviors.push_back({"u2", "broken", u8str(bad)});
    const ExportResult r2 = export_mod(q, u8str(temp_dir("bhv2") / "out"), o);
    CHECK(!r2.ok && !r2.errors.empty() && r2.errors[0].find("behaviour broken: update must be a function") != std::string::npos);
    // validation: an attachment of an unknown behaviour, a bad name, a missing file
    q = p;
    q.npcs[0].behaviors.push_back({"no_such_behavior", json::object()});
    q.behaviors.push_back({"u3", "bad name", ""});
    const auto v = validate(q, nullptr);
    auto has = [&](const std::string& part) { return std::any_of(v.begin(), v.end(), [&](const Problem& x) { return x.message.find(part) != std::string::npos; }); };
    CHECK(has("the behaviour 'no_such_behavior' is not in the project"));
    CHECK(has("letters, digits"));
    CHECK(has("choose the script file"));
}
