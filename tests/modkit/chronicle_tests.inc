// Tests of the chronicle example (examples/modkit/chronicle.rmk.json -> mods/chronicle_example, docs/editor/CHRONICLE.md). Included by modkit_tests.cpp.

static Project load_chronicle() { return load_project_file(std::string(MODKIT_SOURCE_DIR) + "/examples/modkit/chronicle.rmk.json"); }
static std::string chronicle_dir() { return std::string(MODKIT_SOURCE_DIR) + "/examples/modkit"; }

TEST(export_golden_chronicle) {
    // mods/chronicle_example is the committed export of the chronicle project (the live scenario `chronicle` plays it): re-export with
    // `risen-modkit examples/modkit/chronicle.rmk.json --export mods/chronicle_example` after a change of the exporter or the project.
    // custom.lua is the one hand-written file (the export never overwrites it), so it is not compared.
    const Project p = load_chronicle();
    const auto files = render_mod(p);
    const fs::path dir = fs::path(MODKIT_SOURCE_DIR) / "mods" / "chronicle_example";
    for (auto& [rel, content] : files) {
        const fs::path f = dir / u8(rel);
        if (!fs::exists(f)) {
            ++g_failures;
            std::printf("  golden file missing: %s\n", rel.c_str());
            continue;
        }
        if (lf(read_text(f)) != lf(content)) {
            ++g_failures;
            std::printf("  golden file differs: %s (re-export the chronicle)\n", rel.c_str());
        }
        ++g_checks;
    }
    for (const char* sub : {"data", "strings"}) {
        for (auto& e : fs::directory_iterator(dir / sub)) {
            const std::string rel = std::string(sub) + "/" + u8str(e.path().filename());
            CHECK(files.count(rel) == 1);
        }
    }
    CHECK(files.count("modkit/events.lua") == 1 && files.count("modkit/layers.lua") == 1 && files.count("data/events.lua") == 1);
    // the project file of the example is what the editor itself writes (File > Save)
    CHECK_EQ(lf(read_text(fs::path(MODKIT_SOURCE_DIR) / "examples" / "modkit" / "chronicle.rmk.json")), lf(dump_project(p)));
}

TEST(chronicle_validates_clean_and_uses_every_feature) {
    const Project p = load_chronicle();
    const auto v = validate(p, nullptr);
    for (auto& x : v)
        if (x.severity == Severity::Error) std::printf("  problem: %s %s\n", x.subject.c_str(), x.message.c_str());
    CHECK_EQ(error_count(v), size_t(0));
    // every kind of node, check and trigger the editor has is used at least once (the feature coverage table of docs/editor/CHRONICLE.md)
    std::set<std::string> ev_nodes, ev_tests, q_nodes, d_nodes;
    for (auto& e : p.events)
        for (auto& n : e.graph.nodes) {
            ev_nodes.insert(n.type);
            if (n.type == "condition")
                for (auto& t : n.data["tests"]) ev_tests.insert(t["kind"].get<std::string>());
        }
    for (auto& l : p.layers)
        for (const json* c : {&l.enable_when, &l.disable_when})
            if (c->is_object())
                for (auto& t : (*c)["tests"]) ev_tests.insert(t["kind"].get<std::string>());
    for (auto& q : p.quests)
        for (auto& n : q.graph.nodes) q_nodes.insert(n.type);
    for (auto& d : p.dialogs)
        for (auto& n : d.graph.nodes) d_nodes.insert(n.type);
    for (auto& s : event_specs())
        if (std::string(s.type) != "comment") {
            ++g_checks;
            if (!ev_nodes.count(s.type)) {
                ++g_failures;
                std::printf("  event node kind not used: %s\n", s.type);
            }
        }
    for (auto& t : event_tests()) {
        ++g_checks;
        if (!ev_tests.count(t.kind)) {
            ++g_failures;
            std::printf("  event check not used: %s\n", t.kind);
        }
    }
    for (const char* t : {"start", "objective", "log", "reward", "branch", "outcome"}) CHECK(q_nodes.count(t) == 1);
    for (const char* t : {"option", "line", "command", "quest", "log", "script", "end", "cond", "all", "any", "not", "comment"}) CHECK(d_nodes.count(t) == 1);
    // the sections of the project
    CHECK(!p.items.empty() && !p.item_groups.empty() && !p.recipes.empty() && !p.spells.empty() && !p.npc_templates.empty() && !p.npc_groups.empty());
    CHECK(!p.npcs.empty() && !p.dialogs.empty() && !p.quests.empty() && !p.events.empty() && !p.layers.empty() && !p.assets.empty() && !p.behaviors.empty());
    CHECK(!p.world.spawns.empty() && !p.world.containers.empty() && !p.world.removals.empty() && !p.world.moves.empty());
    // the features since the first version of the example: free string rows, template patches, shipped dialogue edits (XP added and changed, a
    // trader refill, a property edit), a teaching limit, an ordered modifier list with a mode, an equipped item in a container fill, a Lua file
    CHECK(!p.strings.empty() && !p.patches.empty() && !p.teachers.empty());
    bool xp_add = false, xp_change = false, refill = false, op = false;
    for (auto& e : p.dialog_edits) {
        xp_add |= e.xp_add.has_value();
        xp_change |= e.xp_old.has_value() && e.xp_new.has_value();
        refill |= !e.refills.empty();
        op |= !e.ops.empty();
    }
    CHECK(xp_add && xp_change && refill && op);
    bool ordered_mode = false;
    for (auto& it : p.items)
        for (auto& [skill, mode] : it.modifier_modes) ordered_mode |= !it.modifier_order.empty() && mode != "add";
    CHECK(ordered_mode);
    bool equip = false;
    for (auto& c : p.world.containers)
        for (auto& i : c.items) equip |= i.equip;
    CHECK(equip);
    // the Lua file has both hooks, reacts to a mod event, and defines the script a Run script node of an event graph calls
    CHECK(!p.lua_files.empty());
    std::set<std::string> run_scripts;
    for (auto& e : p.events)
        for (auto& n : e.graph.nodes)
            if (n.type == "run_script") run_scripts.insert(n.data.value("script", std::string()));
    bool hooks = false, called = false, reacts = false;
    for (auto& l : p.lua_files) {
        const LuaFileInfo info = lua_file_info(u8str(fs::path(chronicle_dir()) / u8(l.file)));
        CHECK(info.ok);
        hooks |= info.hooks.size() == 2;
        for (auto& s : info.scripts) called |= run_scripts.count(s) == 1;
        reacts |= read_text(fs::path(chronicle_dir()) / u8(l.file)).find("risen.on(\"modkit.") != std::string::npos;
    }
    CHECK(hooks && called && reacts);
}

TEST(chronicle_import_round_trip) {
    // export the chronicle, import it from its data files alone, export again: every generated file but the project is the same
    const Project p = load_chronicle();
    const fs::path dir = temp_dir("import_chronicle") / "chronicle_example";
    ExportOptions o;
    o.project_dir = chronicle_dir();
    CHECK(export_mod(p, u8str(dir), o).ok);
    const ImportResult via_project = import_mod(u8str(dir));
    CHECK(via_project.ok && via_project.from_project_file);
    CHECK_EQ(dump_project(via_project.project), dump_project(p));
    const ImportResult r = import_mod(u8str(dir), true);
    for (auto& w : r.warnings) std::printf("  import: %s\n", w.c_str());
    CHECK(r.ok && !r.from_project_file);
    if (!r.ok) {
        std::printf("  error: %s\n", r.error.c_str());
        return;
    }
    CHECK_EQ(r.project.items.size(), p.items.size());
    CHECK_EQ(r.project.events.size(), p.events.size());
    CHECK_EQ(r.project.layers.size(), p.layers.size());
    CHECK_EQ(r.project.quests.size(), p.quests.size());
    CHECK_EQ(r.project.dialogs.size(), p.dialogs.size());
    CHECK_EQ(r.project.npcs.size(), p.npcs.size());
    const auto a = render_mod(p), b = render_mod(r.project);
    for (auto& [rel, content] : a) {
        if (rel == "modkit/project.json") continue;
        auto it = b.find(rel);
        if (it == b.end()) {
            ++g_failures;
            std::printf("  missing after the import: %s\n", rel.c_str());
        } else if (it->second != content) {
            ++g_failures;
            std::printf("  differs after the import: %s\n", rel.c_str());
            std::istringstream x(content), y(it->second);
            std::string lx, ly;
            int line = 0;
            while (std::getline(x, lx) && std::getline(y, ly)) {
                ++line;
                if (lx != ly) {
                    std::printf("    line %d:\n      %s\n      %s\n", line, lx.c_str(), ly.c_str());
                    break;
                }
            }
        }
        ++g_checks;
    }
    const auto v = validate(r.project, nullptr);
    for (auto& x : v)
        if (x.severity == Severity::Error) std::printf("  problem: %s %s\n", x.subject.c_str(), x.message.c_str());
    CHECK_EQ(error_count(v), size_t(0));
}
