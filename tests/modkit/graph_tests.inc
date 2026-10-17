// Tests of the graph rules and the dialogue compiler (stage E5). Included by modkit_tests.cpp.

TEST(graph_link_rules) {
    Graph g;
    GraphNode o1 = make_node(GraphKind::Dialog, "option", 0, 0), o2 = make_node(GraphKind::Dialog, "option", 0, 200);
    GraphNode l1 = make_node(GraphKind::Dialog, "line", 300, 0), l2 = make_node(GraphKind::Dialog, "line", 300, 200);
    GraphNode c1 = make_node(GraphKind::Dialog, "cond", -300, 0), all = make_node(GraphKind::Dialog, "all", -150, 0);
    g.nodes = {o1, o2, l1, l2, c1, all};
    std::string why;
    CHECK(!link_allowed(GraphKind::Dialog, g, o1.uid, "then", c1.uid, "out", &why));    // an output into an output
    CHECK(!link_allowed(GraphKind::Dialog, g, o1.uid, "then", o2.uid, "parent", &why)); // flow into a choice pin
    CHECK(!link_allowed(GraphKind::Dialog, g, o1.uid, "then", o1.uid, "parent", &why)); // itself
    CHECK(!add_link(GraphKind::Dialog, g, o1.uid, "then", l1.uid, "in").empty());
    CHECK(add_link(GraphKind::Dialog, g, o1.uid, "then", l1.uid, "in").empty());        // twice
    // "then" takes one link: a new one replaces the old
    CHECK(!add_link(GraphKind::Dialog, g, o1.uid, "then", l2.uid, "in").empty());
    CHECK_EQ(links_from(g, o1.uid, "then").size(), size_t(1));
    CHECK_EQ(links_from(g, o1.uid, "then")[0]->to_node, l2.uid);
    // the option tree may not loop
    CHECK(!add_link(GraphKind::Dialog, g, o1.uid, "choices", o2.uid, "parent").empty());
    CHECK(!link_allowed(GraphKind::Dialog, g, o2.uid, "choices", o1.uid, "parent", &why));
    CHECK(why.find("loop") != std::string::npos);
    // conditions: several into "when", an "All of" with several inputs
    CHECK(!add_link(GraphKind::Dialog, g, c1.uid, "out", all.uid, "in").empty());
    CHECK(!add_link(GraphKind::Dialog, g, all.uid, "out", o1.uid, "when").empty());
    CHECK(!add_link(GraphKind::Dialog, g, c1.uid, "out", o2.uid, "when").empty());
    remove_node(g, c1.uid);
    CHECK(links_into(g, all.uid, "in").empty());
    CHECK(g.find(c1.uid) == nullptr);
}

TEST(graph_conditions) {
    Graph g;
    GraphNode q = make_node(GraphKind::Dialog, "cond", 0, 0);
    q.data["args"] = json::object({{"quest", "Q_A"}, {"status", "Running"}});
    GraphNode it = make_node(GraphKind::Dialog, "cond", 0, 100);
    it.data["kind"] = "item";
    it.data["args"] = json::object({{"item", "It_Apple"}, {"count", 3}, {"holder", ""}});
    GraphNode no = make_node(GraphKind::Dialog, "not", 200, 100);
    GraphNode any = make_node(GraphKind::Dialog, "any", 400, 50);
    GraphNode o = make_node(GraphKind::Dialog, "option", 600, 0);
    g.nodes = {q, it, no, any, o};
    add_link(GraphKind::Dialog, g, it.uid, "out", no.uid, "in");
    add_link(GraphKind::Dialog, g, q.uid, "out", any.uid, "in");
    add_link(GraphKind::Dialog, g, no.uid, "out", any.uid, "in");
    add_link(GraphKind::Dialog, g, any.uid, "out", o.uid, "when");
    std::vector<std::string> errs;
    const json c = condition_on(g, o.uid, "when", errs);
    CHECK(errs.empty());
    CHECK_EQ(c.dump(), std::string(R"(["any",[["quest","Q_A","Running"],["not",["item","It_Apple",3]]]])"));  // the empty holder is left out
    CHECK_EQ(describe_condition(c), std::string("(quest Q_A is Running or not player has 3 It_Apple)"));
    // an empty "Not" is reported
    GraphNode lone = make_node(GraphKind::Dialog, "not", 0, 300);
    g.nodes.push_back(lone);
    errs.clear();
    CHECK(compile_condition(g, lone.uid, errs).is_null());
    CHECK(!errs.empty());
}

TEST(dialog_compile_showcase) {
    const Project p = load_showcase();

    if (p.dialogs.empty()) {
        ++g_failures;
        std::printf("  the showcase has no dialogue\n");
        return;
    }
    std::vector<DialogIssue> issues;
    json actions = json::array();
    const json infos = compile_dialog(p, p.dialogs[0], issues, &actions);
    for (auto& i : issues) std::printf("  issue: %s\n", i.message.c_str());
    CHECK(issues.empty());
    std::vector<std::string> names;
    for (auto& i : infos) names.push_back(i["name"].get<std::string>());
    // ordered by the canvas (top to bottom), parents before children
    CHECK_EQ(names.size(), size_t(6));
    const auto pos = [&](const std::string& n) { return std::find(names.begin(), names.end(), n) - names.begin(); };
    CHECK(pos("MK_Hob_Island") < pos("MK_Hob_Pirates") && pos("MK_Hob_Island") < pos("MK_Hob_Never"));
    const json& island = infos[static_cast<size_t>(pos("MK_Hob_Island"))];
    CHECK(island["lines"].back().contains("text"));  // a parent with sub-options does not end the conversation
    const json& never = infos[static_cast<size_t>(pos("MK_Hob_Never"))];
    CHECK_EQ(never["parent"].get<std::string>(), std::string("MK_Hob_Island"));
    CHECK_EQ(never["lines"].back()["command"].get<std::string>(), std::string("End"));  // added: an option without sub-options ends it
    const json& ore = infos[static_cast<size_t>(pos("MK_Hob_Ore"))];
    CHECK_EQ(ore["when"][0].get<std::string>(), std::string("all"));
    CHECK_EQ(ore["lines"][1]["command"].get<std::string>(), std::string("Give"));
    CHECK_EQ(ore["lines"][1]["Amount"].get<int>(), 2);
    CHECK_EQ(ore["lines"][0]["text"]["de"].get<std::string>(), std::string("Hier ist das Erz."));
    CHECK_EQ(actions.size(), size_t(2));
    CHECK_EQ(actions[0]["script"].get<std::string>(), std::string("MK_Q_MK_Ore_start"));
}

TEST(dialog_problems) {
    Project p = load_showcase();
    DialogDef& d = p.dialogs[0];
    // an unknown command, a parameter the command does not have, an option without text, a sequence in a circle
    GraphNode o = make_node(GraphKind::Dialog, "option", 0, 2000);
    GraphNode a = make_node(GraphKind::Dialog, "command", 300, 2000);
    a.data["command"] = "Dance";
    GraphNode b = make_node(GraphKind::Dialog, "command", 600, 2000);
    b.data["command"] = "GiveXP";
    b.data["params"] = json::object({{"Gold", 5}});
    d.graph.nodes.insert(d.graph.nodes.end(), {o, a, b});
    add_link(GraphKind::Dialog, d.graph, o.uid, "then", a.uid, "in");
    add_link(GraphKind::Dialog, d.graph, a.uid, "next", b.uid, "in");
    add_link(GraphKind::Dialog, d.graph, b.uid, "next", a.uid, "in");
    std::vector<DialogIssue> issues;
    compile_dialog(p, d, issues);
    auto has = [&](const std::string& node, const std::string& part) {
        return std::any_of(issues.begin(), issues.end(), [&](const DialogIssue& i) { return i.node == node && i.message.find(part) != std::string::npos; });
    };
    CHECK(has(o.uid, "no text"));
    CHECK(has(a.uid, "unknown dialogue command 'Dance'"));
    CHECK(has(b.uid, "no parameter 'Gold'"));
    CHECK(has(o.uid, "circle"));
    // project-wide: the info name is unique across dialogues, quests named in conditions are checked
    DialogDef copy = d;
    copy.uid = "copy";
    copy.name = "Copy";
    p.dialogs.push_back(copy);
    const auto v = validate(p, nullptr);
    CHECK(std::any_of(v.begin(), v.end(), [](const Problem& x) { return x.message.find("is used twice") != std::string::npos; }));
    CHECK(dialog_command("Give") && dialog_command("Give")->params.size() == 4);
    CHECK(dialog_commands().size() > 40);
}

TEST(lua_files_compile) {
    // the runtime and every generated data file are valid Lua for LuaJIT (the layer's interpreter); data files return a table
    const Project p = load_showcase();
    auto files = render_mod(p);
    size_t n = 0;
    for (auto& [rel, content] : files) {
        if (rel.size() < 4 || rel.compare(rel.size() - 4, 4, ".lua") != 0) continue;
        lua_State* L = luaL_newstate();
        const int rc = luaL_loadbuffer(L, content.data(), content.size(), rel.c_str());
        if (rc != 0) {
            ++g_failures;
            std::printf("  %s does not compile: %s\n", rel.c_str(), lua_tostring(L, -1));
        } else if (rel.rfind("data/", 0) == 0) {
            // plain data: runs without any API and returns a table
            const int run = lua_pcall(L, 0, 1, 0);
            if (run != 0 || !lua_istable(L, -1)) {
                ++g_failures;
                std::printf("  %s does not return a table: %s\n", rel.c_str(), run ? lua_tostring(L, -1) : "");
            }
        }
        ++g_checks;
        ++n;
        lua_close(L);
    }
    CHECK(n >= 6);  // main, runtime and the four data files
}

TEST(quest_compile) {
    const Project p = load_showcase();
    const QuestDef* w = p.quest("Q_MK_Wolves");
    CHECK(w != nullptr);
    if (!w) return;
    std::vector<DialogIssue> issues;
    const json f = compile_quest(p, *w, issues);
    CHECK(issues.empty());
    CHECK_EQ(f["trigger"]["kind"].get<std::string>(), std::string("quest"));
    CHECK_EQ(f["steps"].size(), size_t(4));
    CHECK_EQ(f["steps"][1]["objective"].get<std::string>(), std::string("kill"));
    CHECK_EQ(f["steps"][2]["gold"].get<int>(), 50);
    CHECK_EQ(f["steps"][3]["result"].get<std::string>(), std::string("success"));
    // a quest without a Start has no flow
    QuestDef plain = *p.quest("Q_MK_Ore");
    CHECK(compile_quest(p, plain, issues).is_null());
    // problems: an objective without an item, a follow-up that does not exist, a circle
    QuestDef bad = *w;
    for (auto& n : bad.graph.nodes) {
        if (n.type == "objective") {
            n.data["kind"] = "items";
            n.data["item"] = "";
        }
        if (n.type == "outcome") n.data["next_quest"] = "Q_Nope";
    }
    issues.clear();
    compile_quest(p, bad, issues);
    auto has = [&](const std::string& part) {
        return std::any_of(issues.begin(), issues.end(), [&](const DialogIssue& i) { return i.message.find(part) != std::string::npos; });
    };
    CHECK(has("needs an item"));
    CHECK(has("'Q_Nope' is not in the project"));
    // the exported quests carry the flow
    const json q = quests_data(p);
    CHECK(q[1].contains("flow") && !q[0].contains("flow"));
}
