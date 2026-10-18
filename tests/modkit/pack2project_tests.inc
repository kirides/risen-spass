// risen-pack2mod --project: the converter's tables mapped onto the editor's project model (core/pack2project.cpp), the export of that project compared with the
// converted layer mod, and the whole command line path on synthetic archives. No game data and no mod content: everything is made up here.
// Included by modkit_tests.cpp.

#include "../p2m_fixture.inc"

namespace {

p2m::LVal lval_of(const json& j) {
    using p2m::LVal;
    if (j.is_null()) return LVal::nil();
    if (j.is_boolean()) return LVal::boolean(j.get<bool>());
    if (j.is_number_integer()) return LVal::integer(j.get<long long>());
    if (j.is_number()) return LVal::real(j.get<double>());
    if (j.is_string()) return LVal::str(j.get<std::string>());
    if (j.is_array()) {
        LVal l = LVal::list();
        for (auto& x : j) l.push(lval_of(x));
        return l;
    }
    LVal d = LVal::dict(true);
    for (auto& [k, v] : j.items()) d.set(k, lval_of(v));
    return d;
}

void write_bytes(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << s;
}

// the tables of a made-up conversion, in the converter's own shapes
p2m::ProjectInput made_up_input() {
    p2m::ProjectInput in;
    in.id = "made_up";
    in.name = "Made up (converted)";
    in.items = lval_of(json::parse(R"({
        "It_Mace": { "requires": [] },
        "It_Ring": { "modifies": [ { "skill": "Strength", "amount": 2, "mode": "mult" } ], "value": 10 },
        "It_Sword": { "value": 75, "damage": 20,
                      "requires": [ { "skill": "Strength", "amount": 12 }, { "skill": "Strength", "amount": 15 } ],
                      "modifies": [ { "skill": "Dexterity", "amount": 1 } ],
                      "properties": { "gCItem_PS": { "SortValue": 400, "IconImage": "ic_sword" }, "eCMesh_PS": { "MaterialSwitch": 2 } } } })"));
    in.new_items = lval_of(json::parse(R"({
        "It_Axe": { "base": "It_Sword", "value": 120, "damage": 33, "mesh": "It_Wpn_Axe.xmsh", "icon": "ic_axe", "material": 3,
                    "requires": [ { "skill": "Strength", "amount": 25 } ], "properties": { "gCItem_PS": { "SortValue": 500 } } },
        "It_Plan": { "base": "It_Recipe_Test", "value": 50, "icon": "ic_plan", "mesh": "Plan.xmsh", "properties": { "gCItem_PS": { "SortValue": 990 } } } })"));
    in.recipes_edit = lval_of(json::parse(R"({
        "It_Plan": { "station": "anvil", "ingredients": [ { "item": "It_IronOre", "count": 2 }, { "item": "It_Axe", "count": 1 } ], "skills": { "Smith": 2 }, "result": "It_Axe" } })"));
    in.recipes_new = lval_of(json::parse(R"({
        "It_Gold_Goldsmith": { "station": "goldsmith", "ingredients": [ { "item": "It_Gold", "count": 30 } ], "skills": { "Smith": 1 }, "result": "It_GoldOre",
                               "learn": true, "from": "It_Gold" } })"));
    in.patches = lval_of(json::parse(R"({
        "It_Cast_Light": { "eCPointLight_PS": { "Range": 1500 } },
        "It_Projectile_Fireball_S0": { "gCDamage_PS": { "DamageAmount": 30 } },
        "It_Projectile_Frost_S0": { "gCDamage_PS": { "DamageAmount": 5 } },
        "It_Projectile_Frost_S1": { "gCDamage_PS": { "DamageAmount": 5 } },
        "It_Projectile_Frost_S2": { "gCDamage_PS": { "DamageAmount": 5 } },
        "Lizard_X": { "eCAnimation_PS": { "ResourceFilePath": "Ani_X.xac" } } })"));
    in.dialog = lval_of(json::parse(R"({ "layer": "compiled/library.p01",
        "trade": [ { "info": "I_Trade", "chapter": 2, "entries": [ { "trader": "Oscar", "item": "It_Axe", "count": 1 } ] } ],
        "xp": { "add": [ { "info": "I_Xp", "owner": null, "amount": 5 } ], "change": [] } })"));
    in.world = lval_of(json::parse(R"({
        "containers": [ { "id": "aaaaaaaa-0000-4000-8000-000000000001", "name": "Chest", "layer": "Farm.lrent", "items": [ { "item": "It_Axe", "count": 2 } ] } ],
        "equipment": [ { "id": "aaaaaaaa-0000-4000-8000-000000000002", "name": "Guard", "item": "It_Sword" },
                       { "id": "aaaaaaaa-0000-4000-8000-000000000001", "name": "Chest", "item": "It_Ring" } ],
        "teachers": [ { "id": "aaaaaaaa-0000-4000-8000-000000000003", "name": "Teacher", "skill": "Smith", "max": 3, "old": 2 } ] })"));
    in.languages = {"English", "German", "French"};
    in.strings["English"] = {{"FO_It_Axe", "Axe"}, {"ITEMDESC_It_Axe", "A big axe"}, {"FO_IT_SWORD", "Longsword"}, {"ITEMDESC_It_Plan", "Plan\r\nsecond line"}, {"HUD_Other", "Other"}};
    in.strings["German"] = {{"FO_It_Axe", "Axt"}, {"HUD_Other", "Anderes"}};
    in.assets = {"It_Wpn_Axe._xmsh", "Plan._xmsh", "compiled_combatspecies.bin"};
    return in;
}

// the layer mod the converter writes for the same tables (what --out makes), into `dir`
void write_legacy_mod(const p2m::ProjectInput& in, const fs::path& dir) {
    auto lua = [&](const char* rel, const p2m::LVal& v) { write_bytes(dir / rel, "return " + p2m::to_lua(v) + "\n"); };
    lua("data/items.lua", in.items);
    lua("data/new_items.lua", in.new_items);
    p2m::LVal r = p2m::LVal::dict();
    r.set("edit", in.recipes_edit);
    r.set("new", in.recipes_new);
    lua("data/recipes.lua", r);
    lua("data/patches.lua", in.patches);
    lua("data/dialog.lua", in.dialog);
    lua("data/world.lua", in.world);
    p2m::StringsResult s;
    s.languages = in.languages;
    s.rows = in.strings;
    p2m::write_strings(s, u8str(dir));
    for (const std::string& a : in.assets) write_bytes(dir / "assets" / a, "bytes of " + a);
}

}  // namespace

TEST(pack2project_maps_the_converters_tables) {
    const p2m::ProjectInput in = made_up_input();
    Pack2ProjectReport rep;
    const Project p = project_from_pack2mod(in, nullptr, rep);
    CHECK_EQ(p.mod.id, std::string("made_up"));
    CHECK_EQ(p.mod.languages.size(), size_t(3));

    // shipped items: numbers, requirements (the layer's rule: the highest amount per skill), modifiers, the lifted icon and material
    const ItemDef* sword = nullptr;
    const ItemDef* mace = nullptr;
    const ItemDef* ring = nullptr;
    const ItemDef* axe = nullptr;
    for (auto& i : p.items) {
        if (i.name == "It_Sword") sword = &i;
        if (i.name == "It_Mace") mace = &i;
        if (i.name == "It_Ring") ring = &i;
        if (i.name == "It_Axe") axe = &i;
    }
    CHECK(sword && mace && ring && axe);
    if (!(sword && mace && ring && axe)) return;
    CHECK(sword->mode == Mode::Edit && sword->value == 75 && sword->damage == 20);
    CHECK(sword->requirements && sword->requirements->at("Strength") == 15 && sword->requirements->size() == 1);
    CHECK(sword->modifies && sword->modifies->at("Dexterity") == 1);
    CHECK(sword->icon == std::string("ic_sword") && sword->material == 2);
    CHECK(sword->properties.at("gCItem_PS").at("SortValue") == 400 && !sword->properties.at("gCItem_PS").count("IconImage") && !sword->properties.count("eCMesh_PS"));
    CHECK(mace->requirements && mace->requirements->empty());  // "clear the list" stays "clear the list"
    // a modifier with another mode than "add" has no place in the editor's skill -> number map: reported, not guessed
    CHECK(!ring->modifies && ring->value == 10);
    bool ring_reported = false;
    for (auto& e : rep.unmapped) ring_reported = ring_reported || (e.name == "It_Ring" && e.reason.find("mult") != std::string::npos);
    CHECK(ring_reported && rep.missing_settings.count("It_Ring|modifies") == 1);
    // texts: matched case-insensitively like the engine, English made explicit for the languages without a row (new templates only)
    CHECK(sword->label.at("English") == "Longsword" && sword->label.size() == 1);
    CHECK(axe->mode == Mode::New && axe->base == "It_Sword" && axe->mesh == std::string("It_Wpn_Axe.xmsh") && axe->material == 3);
    CHECK(axe->label.at("German") == "Axt" && axe->label.at("French") == "Axe" && axe->description.at("English") == "A big axe");

    // a new item that is a recipe item becomes one new recipe; a shipped item turned into a recipe is given at the start
    const RecipeDef* plan = nullptr;
    const RecipeDef* gold = nullptr;
    for (auto& r : p.recipes) {
        if (r.name == "It_Plan") plan = &r;
        if (r.name == "It_Gold_Goldsmith") gold = &r;
    }
    CHECK(plan && gold);
    if (plan && gold) {
        CHECK(plan->mode == Mode::New && plan->base == "It_Recipe_Test" && plan->station == "anvil" && plan->result == std::string("It_Axe"));
        CHECK(plan->ingredients && plan->ingredients->size() == 2 && plan->ingredients->at(1).item == "It_Axe" && plan->skills->at("Smith") == 2);
        CHECK(plan->icon == std::string("ic_plan") && plan->mesh == std::string("Plan.xmsh") && plan->value == 50);
        CHECK(plan->properties.at("gCItem_PS").at("SortValue") == 990);
        CHECK(plan->description.at("English") == "Plan\r\nsecond line");
        CHECK(gold->learn && gold->notes.find("It_Gold") != std::string::npos);
    }
    for (auto& i : p.items) CHECK(i.name != "It_Plan");

    // template patches: a monster's actor, the three Frost projectiles as one spell edit; no game data means the others stay unmapped
    CHECK_EQ(p.npc_templates.size(), size_t(1));
    CHECK(p.npc_templates[0].mode == Mode::Edit && p.npc_templates[0].name == "Lizard_X" && p.npc_templates[0].mesh == std::string("Ani_X.xac"));
    CHECK(p.npc_templates[0].properties.empty());
    CHECK_EQ(p.spells.size(), size_t(1));
    CHECK(p.spells[0].name == "Frost" && p.spells[0].damage == json(5));
    CHECK(rep.leftover_patches.contains("It_Cast_Light") && rep.leftover_patches.contains("It_Projectile_Fireball_S0") && !rep.leftover_patches.contains("It_Projectile_Frost_S0"));
    CHECK(rep.missing_templates.count("It_Cast_Light") == 1);

    // the world: the fill of a container with the equipment of the same entity merged, equipment of an NPC as a fill with the equip mark
    CHECK_EQ(p.world.containers.size(), size_t(2));
    const ContainerEdit* chest = nullptr;
    const ContainerEdit* guard = nullptr;
    for (auto& c : p.world.containers) (c.entity_name == "Chest" ? chest : guard) = &c;
    CHECK(chest && guard);
    if (chest && guard) {
        CHECK(chest->layer == "Farm.lrent" && chest->items.size() == 2 && !chest->items[0].equip && chest->items[1].item == "It_Ring" && chest->items[1].equip);
        CHECK(guard->items.size() == 1 && guard->items[0].item == "It_Sword" && guard->items[0].equip);
    }
    CHECK(rep.leftover_dialog.contains("trade") && rep.leftover_teachers.size() == 1);
    CHECK(rep.leftover_strings.at("English").count("HUD_Other") == 1 && rep.leftover_strings.at("English").count("FO_It_Axe") == 0 && rep.leftover_strings.at("English").count("FO_IT_SWORD") == 0);
    CHECK_EQ(p.assets.size(), size_t(3));
    CHECK(p.assets[0].file == "assets/It_Wpn_Axe._xmsh" && p.assets[0].kind == "mesh" && p.assets[2].kind == "other");

    // the project file round trip keeps it all (and the list of what was not mapped)
    const std::string text = dump_project(p);
    std::vector<std::string> warnings;
    CHECK_EQ(dump_project(project_from_json(json::parse(text), &warnings)), text);
    CHECK(warnings.empty() && p.editor.contains("pack2mod") && p.editor["pack2mod"]["not_mapped"].size() == rep.unmapped.size());
}

TEST(pack2project_export_equals_the_converted_mod) {
    const p2m::ProjectInput in = made_up_input();
    Pack2ProjectReport rep;
    const Project p = project_from_pack2mod(in, nullptr, rep);
    const fs::path root = temp_dir("p2p_equal");
    write_legacy_mod(in, root / "mod");
    for (const std::string& a : in.assets) write_bytes(root / "proj" / "assets" / a, "bytes of " + a);
    ExportOptions eo;
    eo.project_dir = u8str(root / "proj");
    const ExportResult er = export_mod(p, u8str(root / "export"), eo);
    for (auto& e : er.errors) std::printf("  export: %s\n", e.c_str());
    CHECK(er.ok);
    const auto diff = compare_mod_with_export(u8str(root / "mod"), u8str(root / "export"), rep);
    for (auto& d : diff) std::printf("  difference: %s\n", d.c_str());
    CHECK(diff.empty());

    // the equipment of an NPC is equipped by the runtime (the fix of container items with the equip mark)
    const std::string world = read_text(root / "export" / "data" / "world.lua");
    CHECK(world.find("equip = { \"It_Sword\" }") != std::string::npos || world.find("\"It_Sword\"") != std::string::npos);

    // the comparison sees a changed number, a lost recipe ingredient, a lost text, a changed fill and a changed asset
    Project q = p;
    for (auto& i : q.items)
        if (i.name == "It_Sword") i.damage = 21;
    for (auto& r : q.recipes)
        if (r.name == "It_Plan") r.ingredients->pop_back();
    for (auto& i : q.items)
        if (i.name == "It_Axe") i.label["German"] = "Beil";
    q.world.containers[0].items[0].amount = 3;
    const ExportResult er2 = export_mod(q, u8str(root / "export2"), eo);
    CHECK(er2.ok);
    write_bytes(root / "export2" / "assets" / "Plan._xmsh", "other bytes");
    const auto diff2 = compare_mod_with_export(u8str(root / "mod"), u8str(root / "export2"), rep);
    auto has = [&](const char* needle) {
        for (auto& d : diff2)
            if (d.find(needle) != std::string::npos) return true;
        return false;
    };
    CHECK(has("It_Sword prop.gCDamage_PS.DamageAmount") && has("It_Plan recipe.ingredients") && has("text fo_it_axe [German]") && has("container aaaaaaaa-0000-4000-8000-000000000001") &&
          has("asset Plan._xmsh"));
}

namespace {

// a made-up game and mod (as in the converter's own end-to-end test): Sword improves, Axe is new, one mesh is new, one string table has a changed and a new row
void write_synthetic_game_and_mod(const fs::path& root) {
    write_bytes(root / "game/data/common/templates.pak",
                p2m::build_pak({{"Items/Weapons/It_Sword.tple", item_tpl("It_Sword", 1, 1, 50, 15, {{7, 10}}, "It_Wpn_Sword.xmsh"), true},
                                {"Items/Weapons/It_Spear.tple", item_tpl("It_Spear", 2, 2, 90, 30, {{7, 40}, {8, 10}}, "It_Wpn_Spear.xmsh"), true}}));
    write_bytes(root / "game/data/common/materials.pak", p2m::build_pak({{"Mat/Old._xmat", "old material", true}}));
    write_bytes(root / "game/data/compiled/strings.pak",
                p2m::build_pak({{"items.tab", p2m::build_tab({"German_Text", "English_Text"}, {{"FO_It_Sword", {"Schwert", "Sword"}}, {"FO_It_Spear", {"Speer", "Spear"}}}), true}}));
    write_bytes(root / "mod/common/templates.p00",
                p2m::build_pak({{"It_Sword.tple", item_tpl("It_Sword", 1, 1, 75, 20, {{7, 12}}, "It_Wpn_Sword.xmsh"), true},
                                {"It_Axe.tple", item_tpl("It_Axe", 5, 1, 120, 33, {{7, 25}}, "It_Wpn_Axe.xmsh"), true}}));
    write_bytes(root / "mod/common/materials.p00", p2m::build_pak({{"Old._xmat", "new material", false}}));
    write_bytes(root / "mod/common/meshes.p00", p2m::build_pak({{"It_Wpn_Axe._xmsh", "mesh data", false}}));
    write_bytes(root / "mod/compiled/strings.p00",
                p2m::build_pak({{"items.tab",
                                 p2m::build_tab({"German_Text", "English_Text"},
                                                {{"FO_It_Sword", {"Langschwert", "Longsword"}}, {"FO_It_Axe", {"Axt", "Axe"}}, {"FO_Spear", {"Lanze", "Lance"}}}),
                                 false}}));
}

}  // namespace

TEST(pack2project_command_line_end_to_end) {
    const fs::path root = temp_dir("p2p_cli");
    write_synthetic_game_and_mod(root);
    register_pack2mod_project_writer();
    const std::string proj = u8str(root / "out" / "proj" / "made.rmk.json");
    auto run = [&](std::vector<std::string> args, std::string* text = nullptr) {
        std::vector<const char*> argv{"risen-pack2mod"};
        for (auto& a : args) argv.push_back(a.c_str());
        std::ostringstream out, err;
        const int rc = p2m::pack2mod_main(static_cast<int>(argv.size()), argv.data(), out, err);
        if (text) *text = out.str() + err.str();
        return rc;
    };
    const std::string mod_in = u8str(root / "mod"), data = u8str(root / "game/data");
    std::string text;

    // the project alone: no mod folder, the assets next to the project file
    CHECK_EQ(run({mod_in, "--baseline", data, "--project", proj, "--id", "made"}, &text), 0);
    if (!fs::exists(u8(proj))) {
        std::printf("%s\n", text.c_str());
        return;
    }
    CHECK(!fs::exists(root / "out" / "data") && fs::exists(root / "out" / "proj" / "assets" / "It_Wpn_Axe._xmsh") && fs::exists(root / "out" / "proj" / "assets" / "Old._xmat"));
    CHECK(fs::exists(root / "out/proj/pack2mod-project.json") && fs::exists(root / "out/proj/report.md") && fs::exists(root / "out/proj/.gitignore") && fs::exists(root / "out/proj/NOTICE.txt"));
    Project p = load_project_file(proj);
    CHECK_EQ(p.mod.id, std::string("made"));
    const ItemDef* sword = p.item("It_Sword");
    const ItemDef* axe = p.item("It_Axe");
    CHECK(sword && axe);
    if (sword && axe) {
        CHECK(sword->mode == Mode::Edit && sword->value == 75 && sword->damage == 20 && sword->requirements && sword->requirements->at("Strength") == 12);
        CHECK(sword->label.at("English") == "Longsword" && sword->label.at("German") == "Langschwert");
        CHECK(axe->mode == Mode::New && axe->base == "It_Sword" && axe->value == 120 && axe->damage == 33 && axe->mesh == std::string("It_Wpn_Axe.xmsh"));
        CHECK(axe->requirements && axe->requirements->at("Strength") == 25 && axe->label.at("German") == "Axt");
    }
    CHECK_EQ(p.assets.size(), size_t(2));
    // the row that belongs to no object of the project is kept in unmapped/ and listed
    CHECK(fs::exists(root / "out/proj/unmapped/strings/all.csv"));
    CHECK(read_text(root / "out/proj/unmapped/strings/all.csv").find("FO_Spear") != std::string::npos);
    const std::string report = read_text(root / "out/proj/report.md");
    CHECK(report.find("## Editor project") != std::string::npos && report.find("Not representable in the project") != std::string::npos && report.find("string rows") != std::string::npos);
    // the editor validates what the converter wrote: no error
    {
        const auto problems = validate(p, nullptr);
        for (auto& x : problems)
            if (x.severity == Severity::Error) std::printf("  problem: %s %s\n", x.subject.c_str(), x.message.c_str());
        CHECK_EQ(error_count(problems), size_t(0));
    }
    // a rerun replaces what it made; a folder it did not make is refused
    CHECK_EQ(run({mod_in, "--baseline", data, "--project", proj, "--id", "made"}), 0);
    write_bytes(root / "other" / "keep.txt", "x");
    CHECK_EQ(run({mod_in, "--baseline", data, "--project", u8str(root / "other" / "x.rmk.json")}, &text), 1);
    CHECK(text.find("pack2mod-project.json") != std::string::npos && fs::exists(root / "other" / "keep.txt") && !fs::exists(root / "other" / "x.rmk.json"));
    CHECK_EQ(run({mod_in, "--baseline", data, "--project", u8str(root / "game/data/inside/x.rmk.json")}, &text), 1);
    CHECK(text.find("inside") != std::string::npos);

    // both at once and --verify: the exported project equals the converted mod
    const std::string both = u8str(root / "both");
    CHECK_EQ(run({mod_in, "--baseline", data, "--out", u8str(root / "both" / "mod"), "--project", u8str(root / "both" / "proj" / "b.rmk.json"), "--verify", "--id", "both"}, &text), 0);
    CHECK(text.find("verified: export equals the converted mod") != std::string::npos);
    CHECK(fs::exists(root / "both/mod/data/items.lua") && fs::exists(root / "both/mod/assets/Old._xmat") && fs::exists(root / "both/proj/assets/Old._xmat"));
    CHECK(read_text(root / "both/mod/report.md").find("**equivalent**") != std::string::npos);
    // --verify needs both outputs
    CHECK_EQ(run({mod_in, "--baseline", data, "--project", proj, "--verify"}, &text), 1);
    CHECK(text.find("--verify") != std::string::npos);

    // a build without the editor says so and writes nothing
    p2m::set_project_writer({});
    CHECK_EQ(run({mod_in, "--baseline", data, "--project", u8str(root / "none" / "n.rmk.json")}, &text), 1);
    CHECK(text.find("modkit feature") != std::string::npos && !fs::exists(root / "none"));
    register_pack2mod_project_writer();
}
