// Tests of the Lua writer, the validation and the export (stage E2). Included by modkit_tests.cpp.

static std::string lf(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
    return s;
}

TEST(lua_writer_scalars) {
    CHECK_EQ(lua_string("a\"b\\c\nd"), std::string("\"a\\\"b\\\\c\\nd\""));
    CHECK_EQ(lua_string("Hob\xc3\xa4"), std::string("\"Hob\xc3\xa4\""));  // UTF-8 passes through
    CHECK(lua_identifier("It_1H_Sword"));
    CHECK(!lua_identifier("end"));
    CHECK(!lua_identifier("1abc"));
    CHECK(!lua_identifier("a-b"));
    CHECK_EQ(lua_number(2.0), std::string("2"));
    CHECK_EQ(lua_number(0.5), std::string("0.5"));
    CHECK_EQ(lua_number(1.1), std::string("1.1"));
    CHECK_EQ(lua_number(-3.0), std::string("-3"));
}

TEST(lua_writer_tables) {
    json j = json::object();
    j["b"] = 1;
    j["end"] = true;
    j["a-b"] = "x";
    j["list"] = json::array({1, 2, "three"});
    CHECK_EQ(lua_value(j), std::string("{ b = 1, [\"end\"] = true, [\"a-b\"] = \"x\", list = { 1, 2, \"three\" } }"));
    CHECK_EQ(lua_value(json::object()), std::string("{}"));
    json big = json::object();
    for (int i = 0; i < 12; ++i) big["key_number_" + std::to_string(i)] = i;
    const std::string s = lua_value(big);
    CHECK(s.find("\n    key_number_0 = 0,\n") != std::string::npos);  // long tables: one entry per line
    CHECK(s.back() == '}');
}

static Project load_showcase() {
    return load_project_file(std::string(MODKIT_SOURCE_DIR) + "/examples/modkit/showcase.rmk.json");
}
// the folder of the showcase project: its asset files (audio/) are relative to it
static std::string showcase_dir() { return std::string(MODKIT_SOURCE_DIR) + "/examples/modkit"; }

TEST(export_golden_showcase) {
    // mods/modkit_showcase is the committed export of examples/modkit/showcase.rmk.json (the live scenario `modkit` runs it):
    // re-export with `risen-modkit examples/modkit/showcase.rmk.json --export mods/modkit_showcase` after a change of the exporter
    const Project p = load_showcase();
    const auto files = render_mod(p);
    const fs::path dir = fs::path(MODKIT_SOURCE_DIR) / "mods" / "modkit_showcase";
    for (auto& [rel, content] : files) {
        const fs::path f = dir / u8(rel);
        if (!fs::exists(f)) {
            ++g_failures;
            std::printf("  golden file missing: %s\n", rel.c_str());
            continue;
        }
        if (lf(read_text(f)) != lf(content)) {
            ++g_failures;
            std::printf("  golden file differs: %s (re-export the showcase)\n", rel.c_str());
        }
        ++g_checks;
    }
    // nothing generated is left over in the golden folder
    for (const char* sub : {"data", "strings"}) {
        for (auto& e : fs::directory_iterator(dir / sub)) {
            const std::string rel = std::string(sub) + "/" + u8str(e.path().filename());
            CHECK(files.count(rel) == 1);
        }
    }
    CHECK(files.count("modkit/runtime.lua") == 1);
    CHECK(files.at("modkit/runtime.lua").find("modkit.applied") != std::string::npos);
}

TEST(export_balance_spec) {
    Project p = load_showcase();
    const json b = balance_spec(p);
    CHECK_EQ(b["new_items"]["MK_Hob_Knife"]["base"].get<std::string>(), std::string("It_1H_Machete"));
    CHECK_EQ(b["new_items"]["MK_Hob_Knife"]["name"]["German"].get<std::string>(), std::string("Hobs Messer"));
    CHECK_EQ(b["items"]["It_Apple"]["properties"]["gCItem_PS"]["SortValue"].get<int>(), 77);
    CHECK(!b["items"]["It_1H_Sword_Rusty"].contains("name"));  // a shipped item's label goes into strings/*.csv
    // a shipped item's mesh and icon become properties
    p.items[2].mesh = "It_Wpn_1H_Club.xmsh";
    p.items[2].icon = "ic_it_1h_club";
    const json b2 = balance_spec(p);
    CHECK_EQ(b2["items"]["It_1H_Sword_Rusty"]["properties"]["eCMesh_PS"]["MeshFileName"].get<std::string>(), std::string("It_Wpn_1H_Club.xmsh"));
    CHECK_EQ(b2["items"]["It_1H_Sword_Rusty"]["properties"]["gCItem_PS"]["IconImage"].get<std::string>(), std::string("ic_it_1h_club"));
    // one language: plain strings instead of a table
    p.mod.languages = {"English"};
    const json b3 = balance_spec(p);
    CHECK(b3["new_items"]["MK_Hob_Knife"]["name"].is_string());
    const auto files = render_mod(p);
    CHECK(files.count("strings/all.csv") == 1);
    CHECK(files.count("strings/German.csv") == 0);
}

TEST(export_writes_safely) {
    const fs::path root = temp_dir("export");
    const Project p = load_showcase();
    ExportOptions o;
    o.project_dir = showcase_dir();
    o.game_data_dir = u8str(root / "game" / "data");
    fs::create_directories(root / "game" / "data");
    // inside the game data: refused
    ExportResult r = export_mod(p, u8str(root / "game" / "data" / "x"), o);
    CHECK(!r.ok);
    // a foreign non-empty folder: refused
    fs::create_directories(root / "foreign");
    write_text(root / "foreign" / "main.lua", "-- someone else's mod\n");
    r = export_mod(p, u8str(root / "foreign"), o);
    CHECK(!r.ok);
    CHECK_EQ(read_text(root / "foreign" / "main.lua"), std::string("-- someone else's mod\n"));
    // a new folder: written; custom.lua made once
    const fs::path out = root / "mods" / "modkit_showcase";
    r = export_mod(p, u8str(out), o);
    CHECK(r.ok);
    CHECK(fs::exists(out / "mod.toml") && fs::exists(out / "custom.lua") && fs::exists(out / "modkit" / "project.json"));
    write_text(out / "custom.lua", "-- mine\n");
    write_text(out / "data" / "old.lua", "return {}\n");  // a file an older export wrote
    r = export_mod(p, u8str(out), o);
    CHECK(r.ok);
    CHECK(r.written.empty());
    CHECK_EQ(read_text(out / "custom.lua"), std::string("-- mine\n"));
    CHECK(!fs::exists(out / "data" / "old.lua"));
    CHECK(std::find(r.removed.begin(), r.removed.end(), "data/old.lua") != r.removed.end());
    // the project in the mod reads back as the same project
    const Project back = load_project_file(u8str(out / "modkit" / "project.json"));
    CHECK_EQ(dump_project(back), dump_project(p));
    // asset files are copied flat and stale ones removed
    Project q = p;
    write_text(root / "My_Mesh._xmsh", "GR01MS02");
    q.assets.push_back({"a1", u8str(root / "My_Mesh._xmsh"), "mesh"});  // an absolute path
    r = export_mod(q, u8str(out), o);
    CHECK(r.ok && fs::exists(out / "assets" / "My_Mesh._xmsh"));
    CHECK(read_text(out / "mod.toml").find("assets      = \"assets\"") != std::string::npos);
    r = export_mod(p, u8str(out), o);
    CHECK(r.ok && !fs::exists(out / "assets" / "My_Mesh._xmsh"));
    CHECK(fs::exists(out / "sounds" / "hob_who.ogg") && fs::exists(out / "assets" / "modkit_showcase_Icons.ximl"));
}

TEST(validate_without_game_data) {
    Project p = load_showcase();
    CHECK(error_count(validate(p, nullptr)) == 0);
    p.mod.id = "Bad Id";
    p.items[0].name = "1bad";
    p.items[1].base.clear();
    ItemDef dup = p.items[2];
    dup.uid = "dup";
    p.items.push_back(dup);
    p.items[3].requirements = IntMap{{"Strenght", 3}};
    const auto v = validate(p, nullptr);
    auto has = [&](const std::string& part) {
        return std::any_of(v.begin(), v.end(), [&](const Problem& x) { return x.message.find(part) != std::string::npos; });
    };
    CHECK(has("mod id"));
    CHECK(has("must not start with a digit"));
    CHECK(has("needs a base item"));
    CHECK(has("twice"));
    CHECK(has("unknown skill 'Strenght'"));
}

TEST(validate_with_game_data) {
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped: no --data)\n");
        return;
    }
    Project p = load_showcase();
    const auto ok = validate(p, g.get());
    for (auto& x : ok)
        if (x.severity == Severity::Error) std::printf("  unexpected: %s %s\n", x.subject.c_str(), x.message.c_str());
    CHECK(error_count(ok) == 0);
    p.items[0].base = "Wolf";                                  // not an item
    p.items[1].name = "It_Apple";                              // a new item with a shipped name
    p.items[2].mesh = "No_Such_Mesh.xmsh";                     // unknown mesh
    p.items[3].properties["gCNPC_PS"]["Guild"] = 1;            // a set the apple lacks
    p.items[3].use_type = "gEItemUseType_Nonsense";
    const auto v = validate(p, g.get());
    auto has = [&](const std::string& part) {
        return std::any_of(v.begin(), v.end(), [&](const Problem& x) { return x.message.find(part) != std::string::npos; });
    };
    CHECK(has("is no item"));
    CHECK(has("a game template with this name exists"));
    CHECK(has("No_Such_Mesh.xmsh"));
    CHECK(has("no property set gCNPC_PS"));
    CHECK(has("gEItemUseType: unknown value"));
}

TEST(gamedata_quests_and_conditions_on_them) {
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped: no --data)\n");
        return;
    }
    // compiled_quests.bin: 370 quests with their title ids in quests.tab
    CHECK(g->quests().size() > 300);
    const GameQuest* rufo = g->quest("rufo_gotodruidhut");  // case-insensitive
    CHECK(rufo != nullptr && rufo->name == "Rufo_GoToDruidHut" && rufo->silent);
    const GameQuest* salty = g->quest("Salty_SellShop");
    CHECK(salty != nullptr && !salty->title_id.empty() && !g->text(salty->title_id).empty());
    size_t titled = 0;
    for (auto& q : g->quests()) titled += !q.title_id.empty() && !g->text(q.title_id).empty();
    CHECK(titled > 300);
    // a placed NPC that appears after a quest of the game: no warning; an unknown quest: a warning
    Project p = load_showcase();
    p.npcs[0].appear_when = json::array({"quest", "Rufo_GoToDruidHut", "Success"});
    p.npcs[0].leave_when = json::array({"all", json::array({json::array({"quest", "No_Such_Quest_X", "Success"}), json::array({"item", "It_Apple", 2})})});
    const auto v = validate(p, g.get());
    auto has = [&](const std::string& part) {
        return std::any_of(v.begin(), v.end(), [&](const Problem& x) { return x.message.find(part) != std::string::npos; });
    };
    CHECK(!has("'Rufo_GoToDruidHut'"));
    CHECK(has("'No_Such_Quest_X', no quest of the project or the game"));
    CHECK(error_count(v) == 0);
}

TEST(recipes_export_and_validate) {
    Project p = load_showcase();
    const json b = balance_spec(p);
    CHECK_EQ(b["new_recipes"]["MK_Recipe_Hob_Knife"]["station"].get<std::string>(), std::string("anvil"));
    CHECK_EQ(b["new_recipes"]["MK_Recipe_Hob_Knife"]["ingredients"][1]["item"].get<std::string>(), std::string("It_IronOre"));
    CHECK_EQ(b["new_recipes"]["MK_Recipe_Hob_Knife"]["learn"].get<bool>(), true);
    CHECK_EQ(b["recipes"]["It_1H_Steel_Hot"]["skills"]["Smith"].get<int>(), 1);
    CHECK(!b["recipes"]["It_1H_Steel_Hot"].contains("ingredients"));  // unchanged parts stay out
    RecipeDef bad;
    bad.uid = "bad";
    bad.name = "MK_Bad_Plan";
    bad.station = "oven";
    bad.ingredients = std::vector<Ingredient>{{"", 0}};
    p.recipes.push_back(bad);
    auto v = validate(p, nullptr);
    auto has = [&](const std::string& part) {
        return std::any_of(v.begin(), v.end(), [&](const Problem& x) { return x.uid == "bad" && x.message.find(part) != std::string::npos; });
    };
    CHECK(has("unknown station 'oven'"));
    CHECK(has("needs a result item"));
    CHECK(has("an ingredient has no item"));
    CHECK(has("count must be 1 or more"));
    if (auto g = game_data()) {
        p.recipes.back().ingredients = std::vector<Ingredient>{{"Wolf", 1}, {"MK_Hob_Knife", 1}};  // a monster is no ingredient, a project item is
        p.recipes.back().result = "No_Such_Item";
        v = validate(p, g.get());
        CHECK(has("ingredient 'Wolf' is no item"));
        CHECK(!has("ingredient 'MK_Hob_Knife'"));
        CHECK(has("result 'No_Such_Item'"));
    }
}

TEST(gamedata_npc_skills) {
    // the decoded gCSkills_PS and enum names against docs/NPC_PROPERTIES.md (generated from the engine's own values by mods/npcdump)
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped: no --data)\n");
        return;
    }
    const std::string doc = read_text(fs::path(MODKIT_SOURCE_DIR) / "docs" / "NPC_PROPERTIES.md");
    static const std::regex row(R"(\| `([^`]+)` \| (\d+) \| (\d+) \| (\d+) \| (\d+) \| (\d+) \| (\d+) \| (-?\d+) \| [^|]+\| (\w+) \| (\w+) \|)");
    int rows = 0, found = 0, wrong = 0;
    for (auto it = std::sregex_iterator(doc.begin(), doc.end(), row); it != std::sregex_iterator(); ++it) {
        const std::smatch& m = *it;
        ++rows;
        const TemplateInfo* t = g->tpl(m[1].str());
        if (!t || t->skills.size() != 34) continue;
        ++found;
        const NpcStats s = npc_stats(*g, *t);
        auto sk = [&](const char* n) { return s.skills.count(n) ? s.skills.at(n) : 0; };
        const bool ok = s.hp_max == std::stoi(m[2].str()) && s.level == std::stoi(m[3].str()) && s.xp == std::stoi(m[4].str()) &&
                        sk("Strength") == std::stoi(m[5].str()) && sk("Dexterity") == std::stoi(m[6].str()) && sk("Intelligence") == std::stoi(m[7].str()) &&
                        sk("ProtEdge") == std::stoi(m[8].str()) && s.guild == m[9].str() && s.species == m[10].str();
        if (!ok && ++wrong <= 5) std::printf("  %s differs from the table\n", m[1].str().c_str());
    }
    std::printf("  %d table rows, %d templates with skills, %d differ\n", rows, found, wrong);
    CHECK(rows > 90);
    CHECK(found > 90);
    CHECK_EQ(wrong, 0);
    size_t npcs = 0, with = 0;
    for (auto& t : g->templates())
        if (t->kind == TplKind::Npc || t->kind == TplKind::Monster) {
            ++npcs;
            with += t->skills.size() == 34;
        }
    std::printf("  %zu of %zu NPC/monster templates have decoded skills\n", with, npcs);
    CHECK(with * 10 >= npcs * 9);
}

TEST(npcs_export_and_validate) {
    Project p = load_showcase();
    const json b = balance_spec(p);
    CHECK_EQ(b["new_npcs"]["MK_Hob_T"]["base"].get<std::string>(), std::string("Peasant_1_01"));
    CHECK_EQ(b["new_npcs"]["MK_Hob_T"]["loot"]["It_Apple"].get<int>(), 2);
    CHECK_EQ(b["npcs"]["Boar"]["skills"]["Strength"].get<int>(), 33);
    const json n = npcs_data(p);
    CHECK_EQ(n.size(), size_t(3));  // Hob (offset from the player), the watcher (a world position), MK_Elsewhere (in another world)
    CHECK_EQ(n[1]["world"].get<std::string>(), std::string("World"));          // the project's world, written out
    CHECK_EQ(n[2]["world"].get<std::string>(), std::string("Testlevel_Physics"));
    CHECK(n[0].contains("offset") && !n[0].contains("position"));
    CHECK_EQ(n[0]["equip"][0].get<std::string>(), std::string("MK_Hob_Knife"));
    CHECK_EQ(n[0]["items"][1][1].get<int>(), 3);
    const auto files = render_mod(p);
    CHECK(files.at("strings/all.csv").find("FO_MK_Hob;\"Hob\"") != std::string::npos);  // the label goes into the string tables, per language
    CHECK(files.at("main.lua").find("require(\"data.npcs\")") != std::string::npos);
    // absolute positions
    p.npcs[0].offset = false;
    p.npcs[0].position = {1200, 50, -3400};
    CHECK_EQ(npcs_data(p)[0]["position"][2].get<double>(), -3400.0);
    // validation
    PlacedNpc bad;
    bad.uid = "bad";
    bad.name = "Bad Name!";
    bad.template_name = "";
    bad.items = {{"", 1, false}};
    bad.routine = json::object({{"patrol", json::object({{"points", json::array({json::object({{"dx", 1}})})}})}});
    p.npcs.push_back(bad);
    NpcTemplateDef badt;
    badt.uid = "badt";
    badt.name = "MK_Bad_T";
    badt.base = "";
    badt.guild = "Pirates";
    badt.skills = IntMap{{"HitPoints", 5}};
    badt.hp = 10;
    badt.hp_max = 5;
    p.npc_templates.push_back(badt);
    const auto v = validate(p, nullptr);
    auto has = [&](const std::string& uid, const std::string& part) {
        return std::any_of(v.begin(), v.end(), [&](const Problem& x) { return x.uid == uid && x.message.find(part) != std::string::npos; });
    };
    CHECK(has("bad", "letters, digits, _ and -"));
    CHECK(has("bad", "choose the NPC's template"));
    CHECK(has("bad", "an inventory entry has no item"));
    CHECK(has("bad", "a patrol needs at least two waypoints"));
    CHECK(has("badt", "needs a base"));
    CHECK(has("badt", "guild: unknown value 'Pirates'"));
    CHECK(has("badt", "not settable skill 'HitPoints'"));
    CHECK(has("badt", "above the maximum"));
    if (auto g = game_data()) {
        p.npc_templates.back().base = "It_Apple";
        p.npcs.back().template_name = "Wolf";  // a monster may be placed
        const auto v2 = validate(p, g.get());
        CHECK(std::any_of(v2.begin(), v2.end(), [&](const Problem& x) { return x.uid == "badt" && x.message.find("is no NPC or monster") != std::string::npos; }));
        CHECK(!std::any_of(v2.begin(), v2.end(), [&](const Problem& x) { return x.uid == "bad" && x.message.find("template") != std::string::npos; }));
    }
}
