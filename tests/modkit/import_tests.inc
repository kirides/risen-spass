// Tests of the import of exported mods (stage E9). Included by modkit_tests.cpp.

TEST(lua_data_to_json_rules) {
    const json j = lua_data_to_json("return { a = 1, b = { 1, 2, 3 }, c = { x = true, [5] = 'five' }, d = 0.5, e = 'x' }", "t");
    CHECK_EQ(j["a"].get<int>(), 1);
    CHECK(j["b"].is_array() && j["b"].size() == 3);
    CHECK(j["c"].is_object() && j["c"]["5"].get<std::string>() == "five");
    CHECK_EQ(j["d"].get<double>(), 0.5);
    bool threw = false;
    try {
        lua_data_to_json("while true do end", "loop");  // the instruction limit stops it
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
    threw = false;
    try {
        lua_data_to_json("return os.execute('x')", "nolibs");  // no libraries in the state
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
    threw = false;
    try {
        lua_data_to_json("return function() end", "fn");
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(import_round_trip) {
    // export the showcase, import it from its data files alone, export again: every generated file but the project is the same
    const Project p = load_showcase();
    const fs::path dir = temp_dir("import") / "modkit_showcase";
    ExportOptions o;
    o.project_dir = showcase_dir();
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
    CHECK_EQ(r.project.mod.id, p.mod.id);
    CHECK_EQ(r.project.items.size(), p.items.size());
    CHECK_EQ(r.project.dialogs.size(), size_t(1));
    CHECK_EQ(r.project.quests.size(), p.quests.size());
    CHECK(r.project.mod.languages.size() == 2 && r.project.mod.languages[1] == "German");
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
            // the first different line
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
    // the imported graphs are valid and laid out
    const auto v = validate(r.project, nullptr);
    for (auto& x : v)
        if (x.severity == Severity::Error) std::printf("  problem: %s %s\n", x.subject.c_str(), x.message.c_str());
    CHECK_EQ(error_count(v), size_t(0));
}

TEST(import_keeps_unknown_sections) {
    const fs::path dir = temp_dir("import_extra") / "extra_mod";
    write_text(dir / "mod.toml", "id = \"extra_mod\"\nname = \"Spells\"\nversion = \"2.0.0\"\n");
    write_text(dir / "data" / "balance.lua", "return { future_section = { Fireball = { mana_cost = 12 } }, items = { It_Apple = { value = 2 } } }\n");
    const ImportResult r = import_mod(u8str(dir));
    CHECK(r.ok);
    CHECK_EQ(r.project.mod.version, std::string("2.0.0"));
    CHECK_EQ(r.project.items.size(), size_t(1));
    CHECK(r.project.extra_balance.contains("future_section"));
    const json b = balance_spec(r.project);
    CHECK_EQ(b["future_section"]["Fireball"]["mana_cost"].get<int>(), 12);
    CHECK(!r.warnings.empty());
    // the extra sections survive the project file
    const Project q = project_from_json(json::parse(dump_project(r.project)));
    CHECK(q.extra_balance.contains("future_section"));
    // not a mod
    CHECK(!import_mod(u8str(temp_dir("import_none"))).ok);
}

TEST(container_equip_exports_and_imports) {
    // an NPC's inventory (a container edit by entity id) can mark what he wears: the export lists it in `equip`, the runtime equips it, the import reads it back
    Project p = load_showcase();
    ContainerEdit c;
    c.uid = "0123456789abcdef0123456789abcdef";
    c.guid = "11111111-2222-3333-4444-555555555555";
    c.entity_name = "Guard_01";
    c.items = {{"It_1H_Sword_Rusty", 1, true}, {"It_Apple", 3, false}};
    p.world.containers.push_back(c);
    const json w = world_data(p);
    const json& e = w["containers"].back();
    CHECK(e.contains("equip") && e["equip"].size() == 1 && e["equip"][0] == "It_1H_Sword_Rusty");
    CHECK_EQ(e["items"].size(), size_t(2));
    const auto files = render_mod(p);
    CHECK(files.at("modkit/runtime.lua").find("risen.item.equip(ent, name)") != std::string::npos);
    const fs::path dir = temp_dir("contequip") / "m";
    ExportOptions o;
    o.project_dir = showcase_dir();
    CHECK(export_mod(p, u8str(dir), o).ok);
    const ImportResult r = import_mod(u8str(dir), true);
    CHECK(r.ok);
    const ContainerEdit* back = nullptr;
    for (auto& x : r.project.world.containers)
        if (x.guid == c.guid) back = &x;
    CHECK(back != nullptr);
    if (back) {
        CHECK_EQ(back->items.size(), size_t(2));
        CHECK(back->items[0].equip && !back->items[1].equip);
    }
}
