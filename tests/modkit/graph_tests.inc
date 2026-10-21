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

TEST(dialog_compile_return_to_start) {
    Project p = load_showcase();
    DialogDef& d = p.dialogs[0];
    GraphNode top = make_node(GraphKind::Dialog, "option", 0, 3000);
    top.data["text"] = json::object({{"English", "Tell me about the island."}});
    GraphNode line = make_node(GraphKind::Dialog, "line", 300, 3000);
    line.data["text"] = json::object({{"English", "It is an island."}});
    GraphNode top_r = make_node(GraphKind::Dialog, "restart", 600, 3000);
    GraphNode par = make_node(GraphKind::Dialog, "option", 0, 3200);
    par.data["text"] = json::object({{"English", "Questions."}});
    GraphNode sub = make_node(GraphKind::Dialog, "option", 400, 3400);
    sub.data["text"] = json::object({{"English", "Who are you?"}});
    GraphNode sub_r = make_node(GraphKind::Dialog, "restart", 700, 3400);
    GraphNode sub_end = make_node(GraphKind::Dialog, "option", 400, 3600);
    sub_end.data["text"] = json::object({{"English", "Bye."}});
    GraphNode sub_loop = make_node(GraphKind::Dialog, "option", 400, 3800);
    sub_loop.data["text"] = json::object({{"English", "About wolves?"}});
    GraphNode sub_m = make_node(GraphKind::Dialog, "menu", 700, 3800);
    d.graph.nodes.insert(d.graph.nodes.end(), {top, line, top_r, par, sub, sub_r, sub_end, sub_loop, sub_m});
    add_link(GraphKind::Dialog, d.graph, par.uid, "choices", sub_loop.uid, "parent");
    add_link(GraphKind::Dialog, d.graph, sub_loop.uid, "then", sub_m.uid, "in");
    add_link(GraphKind::Dialog, d.graph, top.uid, "then", line.uid, "in");
    add_link(GraphKind::Dialog, d.graph, line.uid, "next", top_r.uid, "in");
    add_link(GraphKind::Dialog, d.graph, sub.uid, "then", sub_r.uid, "in");
    add_link(GraphKind::Dialog, d.graph, par.uid, "choices", sub.uid, "parent");
    add_link(GraphKind::Dialog, d.graph, par.uid, "choices", sub_end.uid, "parent");
    std::vector<DialogIssue> issues;
    const json infos = compile_dialog(p, d, issues);
    auto lines_of = [&](const GraphNode& n) {
        for (auto& i : infos)
            if (i["name"] == info_name(p, n)) return i["lines"];
        return json::array();
    };
    for (auto& i : issues) std::printf("  issue: %s\n", i.message.c_str());
    CHECK(issues.empty());
    CHECK_EQ(lines_of(top).size(), size_t(2));                                         // a top-level option: the list comes back by itself
    CHECK_EQ(lines_of(sub).back()["command"].get<std::string>(), std::string("Back"));  // a sub-option returns to the top level with Back
    CHECK_EQ(lines_of(sub_end).back()["command"].get<std::string>(), std::string("End"));  // the default still ends the conversation
    CHECK(lines_of(par).back().contains("text"));                                       // a parent opens its sub-menu
    CHECK_EQ(lines_of(sub_loop).size(), size_t(1));  // Back to the choices: nothing added, the engine keeps the sub-menu
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

TEST(world_load_real) {
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped: no --data)\n");
        return;
    }
    auto w = load_world(*g, "World");
    CHECK(w != nullptr);
    if (!w) return;
    std::map<TplKind, int> kinds;
    size_t with_template = 0, with_mesh = 0, with_box = 0;
    for (auto& e : w->entities) {
        kinds[e.kind]++;
        with_template += !e.template_name.empty();
        with_mesh += !e.mesh.empty();
        with_box += e.has_box();
    }
    std::printf("  %zu entities in %zu layers (%.2f s): %zu with a template, %zu with a mesh, %zu with a box\n", w->entities.size(), w->layers.size(),
                w->load_seconds, with_template, with_mesh, with_box);
    for (auto& [k, n] : kinds) std::printf("    %-12s %d\n", kind_name(k), n);
    CHECK(w->layers.size() > 300);  // 317 layer files (the other half of the folder are .sec sector files)
    CHECK(w->entities.size() > 10000);
    // a placed NPC of the harbour: Ingvar, an NPC with a template and his position (the dump of Harbour_Town_NPC_01.lrent)
    const WorldEntity* ing = w->by_name("Ingvar");
    CHECK(ing != nullptr);
    if (ing) {
        CHECK(ing->npc);
        CHECK(!ing->template_name.empty());
        CHECK(std::fabs(ing->x() - -2240.164f) < 0.01f && std::fabs(ing->z() - -20187.301f) < 0.01f);
        CHECK(ing->has_box());
        std::printf("  Ingvar: template %s, guid %s, yaw %.1f, layer %s\n", ing->template_name.c_str(), ing->guid.c_str(), ing->yaw(),
                    w->layers[ing->layer].name.c_str());
        const auto props = entity_properties(*g, *w, *ing);
        CHECK(std::any_of(props.begin(), props.end(), [](const p2m::Prop& p) { return p.pset == "gCNPC_PS"; }));
    }
    // the landscape mesh
    const WorldEntity* land = w->by_name("Levelmesh_Landscape_01");
    CHECK(land && land->mesh == "Levelmesh_Landscape_01._xmsh");
    // containers with items
    size_t chests = 0, items = 0;
    for (auto& e : w->entities)
        if (e.inventory && !e.npc && e.kind == TplKind::Interactive && chests < 20) {
            const auto inv = entity_inventory(*g, *w, e);
            if (!inv.empty()) {
                ++chests;
                items += inv.size();
            }
        }
    std::printf("  %zu chests looked at hold %zu stacks\n", chests, items);
    CHECK(chests > 0);
    CHECK_EQ(guid_text("\x0c\x91\x5d\x31\x03\x1c\x45\x90\xb5\x19\x11\x32\x16\x97\x56\x3c"), std::string("315d910c-1c03-9045-b519-11321697563c"));
}

TEST(mesh_decode_real) {
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped: no --data)\n");
        return;
    }
    size_t ok = 0, bad = 0, box_bad = 0;
    std::set<std::string> materials;
    std::string first_bad;
    for (auto& a : g->assets("_xmsh")) {
        try {
            const MeshData m = decode_mesh(g->read(a.name));
            float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
            for (uint32_t i = 0; i < m.vertex_count; ++i)
                for (int k = 0; k < 3; ++k) {
                    float f;
                    std::memcpy(&f, m.vertices.data() + static_cast<size_t>(i) * m.stride + m.pos + 4 * k, 4);
                    mn[k] = std::min(mn[k], f);
                    mx[k] = std::max(mx[k], f);
                }
            float err = 0;
            for (int k = 0; k < 3; ++k) err = std::max({err, std::fabs(mn[k] - m.box_min[k]), std::fabs(mx[k] - m.box_max[k])});
            if (err > 1.0f) ++box_bad;
            // indices are absolute and inside the vertex buffer
            uint32_t maxi = 0;
            for (uint32_t i = 0; i < m.index_count; ++i) {
                uint32_t v = 0;
                std::memcpy(&v, m.indices.data() + static_cast<size_t>(i) * (m.index32 ? 4 : 2), m.index32 ? 4 : 2);
                maxi = std::max(maxi, v);
            }
            if (m.index_count && maxi >= m.vertex_count) ++box_bad;
            for (auto& s : m.submeshes) materials.insert(s.material);
            ++ok;
        } catch (const std::exception& e) {
            if (first_bad.empty()) first_bad = a.name + ": " + e.what();
            ++bad;
        }
    }
    std::printf("  meshes: %zu decoded, %zu refused (%s), %zu with positions outside their box; %zu materials named\n", ok, bad, first_bad.c_str(), box_bad,
                materials.size());
    CHECK(ok > 1600);
    CHECK(bad <= 2);
    CHECK_EQ(box_bad, size_t(0));
    // materials -> textures -> images
    size_t mat_ok = 0, tex_found = 0, img_ok = 0, img_tried = 0;
    std::map<int, int> formats;
    for (auto& name : materials) {
        if (!g->exists(name)) continue;
        const std::string tex = material_texture(g->read(name));
        if (tex.empty()) continue;
        ++mat_ok;
        if (!g->exists(tex)) continue;
        ++tex_found;
        if (img_tried >= 150) continue;
        ++img_tried;
        try {
            const ImageData img = decode_image(g->read(tex));
            formats[img.format]++;
            if (!img.levels.empty() && img.levels[0].width == img.width) ++img_ok;
        } catch (const std::exception& e) {
            std::printf("  image %s: %s\n", tex.c_str(), e.what());
        }
    }
    std::printf("  materials with a texture: %zu, textures found: %zu, images decoded: %zu of %zu\n", mat_ok, tex_found, img_ok, img_tried);
    CHECK(mat_ok > materials.size() / 2);
    CHECK(tex_found > mat_ok * 8 / 10);
    CHECK(img_ok == img_tried);
    // the CPU fallback of a DXT image
    const ImageData sword = decode_image(g->read("ItWpn_SwordBlades_01_Diffuse_01._ximg"));
    CHECK(sword.format == ImageData::DXT1 && sword.width == 1024 && sword.levels.size() == 11);
    uint32_t w = 0, h = 0;
    const std::string rgba = decompress_rgba(sword, 3, &w, &h);
    CHECK(w == 128 && h == 128 && rgba.size() == 128u * 128u * 4u);
}

TEST(world_meshes_resolve) {
    // every mesh a world entity names is found in the archives by its bare name (level meshes name virtual paths)
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped: no --data)\n");
        return;
    }
    auto w = load_world(*g, "World");
    size_t with = 0, found = 0;
    std::string missing;
    for (auto& e : w->entities) {
        if (e.mesh.empty()) continue;
        ++with;
        if (g->exists(compiled_name(e.mesh))) ++found;
        else if (missing.empty()) missing = e.name + ": " + e.mesh;
    }
    std::printf("  %zu of %zu entity meshes found (first missing: %s)\n", found, with, missing.c_str());
    CHECK(found * 100 >= with * 99);
    const WorldEntity* harbour = w->by_name("Harbour_01_L01");
    CHECK(harbour && harbour->mesh == "Harbour_01_L01._xmsh");
    // a few chests of the harbour (the placement test uses one)
    int shown = 0;
    for (auto& e : w->entities)
        if (e.kind == TplKind::Interactive && e.inventory && !e.npc && w->layers[e.layer].group == "Cities/Harbour_Town" && shown < 4) {
            const auto inv = entity_inventory(*g, *w, e);
            std::printf("  chest %s %s at %.0f %.0f %.0f (%zu stacks) layer %s\n", e.name.c_str(), e.guid.c_str(), e.x(), e.y(), e.z(), inv.size(),
                        w->layers[e.layer].name.c_str());
            ++shown;
        }
}

TEST(leaf_conditions_round_trip) {
    std::vector<std::string> errs;
    const json q = leaf_condition("quest", json{{"quest", "Rufo_GoToDruidHut"}, {"status", "Success"}}, errs);
    CHECK(errs.empty());
    CHECK_EQ(q.dump(), json::array({"quest", "Rufo_GoToDruidHut", "Success"}).dump());
    std::string kind;
    json args;
    CHECK(leaf_args(q, kind, args));
    CHECK_EQ(kind, std::string("quest"));
    CHECK_EQ(args.value("status", std::string()), std::string("Success"));
    // optional arguments: trailing ones left out, a gap kept as null and read back as absent
    const json it = leaf_condition("item", json{{"item", "It_Apple"}, {"holder", "Hob"}}, errs);
    CHECK_EQ(it.dump(), json::array({"item", "It_Apple", nullptr, "Hob"}).dump());
    CHECK(leaf_args(it, kind, args));
    CHECK(!args.contains("count") && args.value("holder", std::string()) == "Hob");
    const json sk = leaf_condition("skill", json{{"skill", "Strength"}, {"value", 30}}, errs);
    CHECK(leaf_args(sk, kind, args) && args.value("value", 0) == 30);
    // every kind survives the way there and back
    for (const char* k : {"quest", "item", "var", "event", "skill", "guild", "dead", "alive"}) {
        json a = json::object();
        if (std::string(k) == "quest") a["quest"] = "Q";
        else if (std::string(k) == "item") a["item"] = "It_Apple";
        else if (std::string(k) == "var" || std::string(k) == "event") a["name"] = "V";
        else if (std::string(k) == "skill") a["skill"] = "Strength";
        else if (std::string(k) == "guild") a["guild"] = "Grd";
        else a["who"] = "Hob";
        std::vector<std::string> e;
        const json c = leaf_condition(k, a, e);
        std::string k2;
        json a2;
        CHECK(e.empty() && leaf_args(c, k2, a2) && k2 == k);
        CHECK_EQ(leaf_condition(k2, a2, e).dump(), c.dump());
    }
    CHECK(!leaf_args(json::array({"all", json::array()}), kind, args));
    CHECK(!leaf_args(json::array({"bogus", 1}), kind, args));
    errs.clear();
    CHECK(leaf_condition("quest", json::object(), errs).is_array() && !errs.empty());  // an empty quest is reported
}
