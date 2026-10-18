// risen-pack2mod --project: the converter's tables mapped onto the editor's project model (core/pack2project.cpp), the export of that project compared with the
// converted layer mod, and the whole command line path on synthetic archives. No game data and no mod content: everything is made up here.
// Included by modkit_tests.cpp.

#include "common/pose.h"
#include "core/genome.h"
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
        "It_Charm": { "requires": [ { "skill": "Strength", "amount": 30 }, { "skill": "Dexterity", "amount": 20 }, { "skill": "Axe", "amount": 1 } ],
                      "modifies": [ { "skill": "ProtEdge", "amount": 5 }, { "skill": "HitPoints", "amount": 50, "mode": "set" }, { "skill": "Strength", "amount": 10, "mode": "percent" } ] },
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
    in.strings["German"] = {{"FO_It_Axe", "Axt"}, {"HUD_Other", "Anderes"}, {"ONLY_DE", "Nur deutsch"}};
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
    // the engine's order and the modes are kept (a plain list stays plain)
    const ItemDef* charm = nullptr;
    for (auto& i : p.items)
        if (i.name == "It_Charm") charm = &i;
    CHECK(charm != nullptr);
    if (charm) {
        CHECK((charm->requirement_order == std::vector<std::string>{"Strength", "Dexterity", "Axe"}));
        CHECK((charm->modifier_order == std::vector<std::string>{"ProtEdge", "HitPoints", "Strength"}) && charm->modifier_modes.at("HitPoints") == "set" && charm->modifier_modes.at("Strength") == "percent");
        CHECK(sword->requirement_order.empty() && sword->modifier_modes.empty());
    }
    // a mode the engine does not have is reported, not guessed
    CHECK(!ring->modifies && ring->value == 10);
    bool ring_reported = false;
    for (auto& e : rep.unmapped) ring_reported = ring_reported || (e.name == "It_Ring" && e.reason.find("mult") != std::string::npos);
    CHECK(ring_reported && rep.missing_settings.count("It_Ring|modifies") == 1);
    // texts: matched case-insensitively like the engine; a language without a row has none (the export's `default` serves it)
    CHECK(sword->label.at("English") == "Longsword" && sword->label.size() == 1);
    CHECK(axe->mode == Mode::New && axe->base == "It_Sword" && axe->mesh == std::string("It_Wpn_Axe.xmsh") && axe->material == 3);
    CHECK(axe->label.at("German") == "Axt" && axe->label.size() == 2 && axe->description.at("English") == "A big axe" && axe->description.size() == 1);

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

    // template patches: a monster's actor, the three Frost projectiles as one spell edit; every other patch is a patch of the project
    CHECK_EQ(p.npc_templates.size(), size_t(1));
    CHECK(p.npc_templates[0].mode == Mode::Edit && p.npc_templates[0].name == "Lizard_X" && p.npc_templates[0].mesh == std::string("Ani_X.xac"));
    CHECK(p.npc_templates[0].properties.empty());
    CHECK_EQ(p.spells.size(), size_t(1));
    CHECK(p.spells[0].name == "Frost" && p.spells[0].damage == json(5));
    CHECK_EQ(p.patches.size(), size_t(2));  // It_Cast_Light, and the Fireball level whose siblings' numbers are unknown without game data
    for (auto& x : p.patches) {
        CHECK(x.template_name == "It_Cast_Light" || x.template_name == "It_Projectile_Fireball_S0");
        if (x.template_name == "It_Cast_Light") CHECK(x.properties.at("eCPointLight_PS").at("Range") == 1500);
    }

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
    // edits of shipped dialogue infos (one per info) and the teaching limit
    CHECK_EQ(p.dialog_edits.size(), size_t(2));
    for (auto& e : p.dialog_edits) {
        if (e.info == "I_Xp") CHECK(e.xp_add == 5 && e.refills.empty());
        if (e.info == "I_Trade") CHECK(e.chapter == 2 && e.refills.size() == 1 && e.refills[0].trader == "Oscar" && e.refills[0].item == "It_Axe" && e.refills[0].count == 1);
    }
    CHECK(p.teachers.size() == 1 && p.teachers[0].skill == "Smith" && p.teachers[0].max == 3 && p.teachers[0].guid == "aaaaaaaa-0000-4000-8000-000000000003" && p.teachers[0].entity_name == "Teacher");
    // free string rows: what no object took, one row per id with every language
    CHECK_EQ(p.strings.size(), size_t(2));
    for (auto& s : p.strings) {
        if (s.id == "HUD_Other") CHECK(s.text.at("English") == "Other" && s.text.at("German") == "Anderes");
        else if (s.id == "ONLY_DE") CHECK(s.text.size() == 1 && s.text.count("German") == 1);
        else CHECK(false);
    }
    CHECK(rep.leftover_strings.empty() && rep.leftover_dialog.empty());
    CHECK_EQ(p.assets.size(), size_t(3));
    CHECK(p.assets[0].file == "assets/It_Wpn_Axe._xmsh" && p.assets[0].kind == "mesh" && p.assets[2].kind == "other");

    // the project file round trip keeps it all (and the list of what was not mapped)
    const std::string text = dump_project(p);
    std::vector<std::string> warnings;
    CHECK_EQ(dump_project(project_from_json(json::parse(text), &warnings)), text);
    CHECK(warnings.empty() && p.editor.contains("pack2mod") && p.editor["pack2mod"]["not_mapped"].size() == rep.unmapped.size());
    // what is still not mapped is the mode the engine does not have
    CHECK_EQ(rep.unmapped.size(), size_t(1));
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
    // the row that belongs to no object of the project is a free string row of the project
    CHECK_EQ(p.strings.size(), size_t(1));
    if (!p.strings.empty()) {
        CHECK(p.strings[0].id == "FO_Spear");
        CHECK(p.strings[0].text.count("English") == 1 && p.strings[0].text.at("English") == "Lance" && p.strings[0].text.at("German") == "Lanze");
        CHECK(!fs::exists(root / "out/proj/unmapped"));
    }
    const std::string report = read_text(root / "out/proj/report.md");
    CHECK(report.find("## Editor project") != std::string::npos && report.find("Everything the converter produced has a place in the project") != std::string::npos &&
          report.find("Strings (rows no object owns)") != std::string::npos);
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

// moved, removed and added entities of hand-built layers (no archive: the parser's output is made by hand)
static GEntity layer_entity(const std::string& id_hex_tail, const std::string& name, float x, float y, float z, float yaw_deg, const std::string& creator = std::string()) {
    GEntity e;
    e.id = "@0000000000000000000000000" + id_hex_tail;  // 33 characters: "@" + 32 hex
    e.name = name;
    e.creator = creator;
    e.matrix = spass::pose::compose(x, y, z, spass::pose::Angles{yaw_deg, 0, 0});
    return e;
}

TEST(pack2project_world_changes_from_layers) {
    LayerPair L;
    L.name = "Test_NPC_01";
    L.base.layer = L.mod.layer = true;
    L.base.entities = {layer_entity("0000001", "root", 0, 0, 0, 0), layer_entity("0000002", "Guard", 100, 0, 200, 90), layer_entity("0000003", "Gone", 5, 5, 5, 0),
                       layer_entity("0000004", "Still", 7, 0, 7, 45), layer_entity("0000005", "Turned", 9, 0, 9, 0)};
    L.base.entities[0].name = "DRE";
    L.mod.entities = {layer_entity("0000001", "root", 0, 0, 0, 0), layer_entity("0000002", "Guard", 150, 0, 260, 180), layer_entity("0000004", "Still", 7, 0, 7, 45),
                      layer_entity("0000005", "Turned", 9, 0, 9, 30),
                      layer_entity("0000006", "New_Chest", 1, 2, 3, 270, "@11111111111111111111111111111111"), layer_entity("0000007", "Orphan", 1, 1, 1, 0, "@22222222222222222222222222222222")};
    L.mod.entities[0].name = "DRE";
    const std::map<std::string, std::string> guids = {{"@11111111111111111111111111111111", "Object_Chest_Test"}};
    const WorldChanges wc = world_changes({L}, guids);
    CHECK_EQ(wc.moves.size(), size_t(2));  // Guard moved and turned, Turned only turned; Still stays
    CHECK_EQ(wc.removals.size(), size_t(1));
    CHECK_EQ(wc.spawns.size(), size_t(1));
    CHECK_EQ(wc.skipped_template, 1);  // the entity whose creator no template has
    if (wc.moves.size() == 2 && wc.removals.size() == 1 && wc.spawns.size() == 1) {
        CHECK(wc.moves[0].entity_name == "Guard" && wc.moves[0].position && (*wc.moves[0].position)[0] == 150 && (*wc.moves[0].rotation)[0] == 180);
        CHECK(wc.moves[1].entity_name == "Turned" && std::fabs((*wc.moves[1].rotation)[0] - 30) < 0.01);
        CHECK(wc.moves[0].layer == "Test_NPC_01" && wc.moves[0].guid.size() == 36);
        CHECK(wc.removals[0].entity_name == "Gone");
        CHECK(wc.spawns[0].template_name == "Object_Chest_Test" && wc.spawns[0].position[2] == 3 && std::fabs(wc.spawns[0].rotation[0] - 270) < 0.01);
    }
    // the project: carried only on request (the converted mod's layers also differ from the game's where the mod did nothing)
    p2m::ProjectInput in = made_up_input();
    in.template_guids = guids;
    Pack2ProjectReport off, on;
    const std::vector<LayerPair> pairs{L};
    const Project p_off = project_from_pack2mod(in, nullptr, off, &pairs);
    CHECK(p_off.world.moves.empty() && p_off.world.removals.empty() && p_off.world.spawns.empty());
    bool noted = false;
    for (auto& e : off.unmapped) noted = noted || (e.area == "World" && e.name.find("2 moved, 1 removed, 1 added") != std::string::npos && e.reason.find("--world-edits") != std::string::npos);
    CHECK(noted);
    in.world_edits = true;
    Project p_on = project_from_pack2mod(in, nullptr, on, &pairs);
    p_on.assets.clear();  // the files are not here
    CHECK(p_on.world.moves.size() == 2 && p_on.world.removals.size() == 1 && p_on.world.spawns.size() == 1);
    // the export's world.lua is checked against the layers: the moves recompose the mod's matrices
    const fs::path dir = temp_dir("p2p_world") / "m";
    ExportOptions eo;
    CHECK(export_mod(p_on, u8str(dir), eo).ok);
    const auto bad = verify_world_edits(pairs, u8str(dir));
    for (auto& d : bad) std::printf("  difference: %s\n", d.c_str());
    CHECK(bad.empty());
    Project broken = p_on;
    (*broken.world.moves[0].position)[0] += 40;
    const fs::path dir2 = temp_dir("p2p_world2") / "m";
    CHECK(export_mod(broken, u8str(dir2), eo).ok);
    CHECK(!verify_world_edits(pairs, u8str(dir2)).empty());
}

// ---- --classic: the mod's changes are what differs from the CLASSIC game; what differs from the baseline only because classic and Enhanced differ is not applied --------

namespace {

// a made-up infos file: sets in sequence, one record per info (gCInfo.Name starts it); `give` = the Give command's Amount (0 = none), `knows` = PlayerKnows conditions in order
p2m::Bytes infos_file(const std::vector<std::tuple<std::string, int, std::vector<std::string>>>& infos) {
    return make_tpl("compiled_infos", guid_bytes(9), guid_bytes(109), [&](TplBuilder& b) {
        for (const auto& [name, give, knows] : infos) {
            b.set("gCInfo");
            b.str("Name", name);
            b.lng("ConditionType", 3);
            b.set("gCInfoCommandSay");
            b.str("Speaker", "player");
            if (give) {
                b.set("gCInfoCommandGive");
                b.str("Entity1", "Esteban");
                b.lng("Amount", give);
            }
            for (const std::string& k : knows) {
                b.set("gCInfoConditionPlayerKnows");
                b.str("PlayerKnows", k);
            }
        }
    });
}

}  // namespace

TEST(pack2mod_classic_three_way) {
    const fs::path root = temp_dir("p2p_classic");
    register_pack2mod_project_writer();
    // Enhanced (the baseline): It_Sword damage 18 (rebalanced), It_Spear gold 95 (changed); classic: 15 / 90; the mod (made against classic): sword value 75 (the author), the
    // spear as classic has it (noise), a new axe (the author), an old item Enhanced removed (noise)
    write_bytes(root / "enh/data/common/templates.pak",
                p2m::build_pak({{"Items/Weapons/It_Sword.tple", item_tpl("It_Sword", 1, 1, 50, 18, {{7, 10}}, "It_Wpn_Sword.xmsh"), true},
                                {"Items/Weapons/It_Spear.tple", item_tpl("It_Spear", 2, 2, 95, 30, {{7, 40}}, "It_Wpn_Spear.xmsh"), true}}));
    write_bytes(root / "cla/data/common/templates.pak",
                p2m::build_pak({{"Items/Weapons/It_Sword.tple", item_tpl("It_Sword", 1, 1, 50, 15, {{7, 10}}, "It_Wpn_Sword.xmsh"), true},
                                {"Items/Weapons/It_Spear.tple", item_tpl("It_Spear", 2, 2, 90, 30, {{7, 40}}, "It_Wpn_Spear.xmsh"), true},
                                {"Items/Weapons/It_Old.tple", item_tpl("It_Old", 6, 1, 5, 5, {}, "It_Wpn_Old.xmsh"), true}}));
    write_bytes(root / "mod/common/templates.p00",
                p2m::build_pak({{"It_Sword.tple", item_tpl("It_Sword", 1, 1, 75, 15, {{7, 10}}, "It_Wpn_Sword.xmsh"), true},
                                {"It_Spear.tple", item_tpl("It_Spear", 2, 2, 90, 30, {{7, 40}}, "It_Wpn_Spear.xmsh"), true},
                                {"It_Old.tple", item_tpl("It_Old", 6, 1, 5, 5, {}, "It_Wpn_Old.xmsh"), true},
                                {"It_Axe.tple", item_tpl("It_Axe", 5, 1, 120, 33, {{7, 25}}, "It_Wpn_Axe.xmsh"), true}}));
    // strings: a row the author changed, a row only classic and Enhanced differ in, a row whose quote marks the mod's spreadsheet lost
    auto tab = [&](std::vector<std::pair<std::string, std::vector<std::string>>> rows) { return p2m::build_tab({"German_Text", "English_Text"}, rows); };
    write_bytes(root / "enh/data/compiled/strings.pak",
                p2m::build_pak({{"items.tab", tab({{"FO_It_Sword", {"Schwert", "Sword"}}, {"FO_Other", {"Anderes", "Other!"}}, {"FO_Q", {"Sag \"hi\"", "Say \"hi\""}}}), true}}));
    write_bytes(root / "cla/data/compiled/strings.pak",
                p2m::build_pak({{"items.tab", tab({{"FO_It_Sword", {"Schwert", "Sword"}}, {"FO_Other", {"Anderes", "Other"}}, {"FO_Q", {"Sag \"hi\"", "Say \"hi\""}}}), true}}));
    write_bytes(root / "mod/compiled/strings.p00",
                p2m::build_pak({{"items.tab", tab({{"FO_It_Sword", {"Langschwert", "Longsword"}}, {"FO_Other", {"Anderes", "Other"}}, {"FO_Q", {"Sag hi", "Say hi"}}}), false}}));
    // resources: one the mod carries from classic where Enhanced changed it (noise), one the author changed, one new
    write_bytes(root / "enh/data/common/materials.pak", p2m::build_pak({{"Mat/Old._xmat", "enhanced", true}, {"Mat/Chg._xmat", "same", true}}));
    write_bytes(root / "cla/data/common/materials.pak", p2m::build_pak({{"Mat/Old._xmat", "classic", true}, {"Mat/Chg._xmat", "same", true}}));
    write_bytes(root / "mod/common/materials.p00", p2m::build_pak({{"Old._xmat", "classic", false}, {"Chg._xmat", "author", false}, {"New._xmat", "new", false}}));
    // dialogue infos: I_1 the author raised the gold and put a condition in front; I_2 only classic and Enhanced differ; I_3 a classic info Enhanced removed
    write_bytes(root / "enh/data/compiled/library.pak", p2m::build_pak({{"compiled_infos.bin", infos_file({{"I_1", 100, {"A"}}, {"I_2", 50, {}}}), true}}));
    write_bytes(root / "cla/data/compiled/library.pak", p2m::build_pak({{"compiled_infos.bin", infos_file({{"I_1", 100, {"A"}}, {"I_2", 100, {}}, {"I_3", 7, {}}}), true}}));
    write_bytes(root / "mod/compiled/library.p00", p2m::build_pak({{"compiled_infos.bin", infos_file({{"I_1", 400, {"New", "A"}}, {"I_2", 100, {}}, {"I_3", 7, {}}}), false}}));

    auto run = [&](std::vector<std::string> args, std::string* text = nullptr) {
        std::vector<const char*> argv{"risen-pack2mod"};
        for (auto& a : args) argv.push_back(a.c_str());
        std::ostringstream out, err;
        const int rc = p2m::pack2mod_main(static_cast<int>(argv.size()), argv.data(), out, err);
        if (text) *text = out.str() + err.str();
        return rc;
    };
    const std::string mod_in = u8str(root / "mod"), enh = u8str(root / "enh/data"), cla = u8str(root / "cla/data");
    std::string text;

    // without --classic everything that differs from Enhanced is carried (the converter's old behaviour)
    CHECK_EQ(run({mod_in, "--baseline", enh, "--out", u8str(root / "plain")}, &text), 0);
    const std::string plain_items = read_text(root / "plain/data/items.lua");
    CHECK(plain_items.find("It_Spear") != std::string::npos && plain_items.find("damage = 15") != std::string::npos);

    // with it: only the author's changes
    const int crc = run({mod_in, "--baseline", enh, "--classic", cla, "--out", u8str(root / "out"), "--project", u8str(root / "proj" / "c.rmk.json"), "--verify", "--id", "classic"}, &text);
    if (crc != 0) std::printf("%s\n", text.c_str());
    CHECK_EQ(crc, 0);
    const std::string items = read_text(root / "out/data/items.lua");
    CHECK(items.find("It_Sword = {") != std::string::npos && items.find("value = 75") != std::string::npos);
    CHECK(items.find("damage") == std::string::npos);        // the sword's 15 is classic's number, Enhanced's 18 stays
    CHECK(items.find("It_Spear") == std::string::npos);      // the spear's 90 is classic's too
    const std::string nw = read_text(root / "out/data/new_items.lua");
    CHECK(nw.find("It_Axe = {") != std::string::npos && nw.find("It_Old") == std::string::npos);  // a classic template Enhanced removed is not the author's
    const std::string en = read_text(root / "out/strings/all.csv");
    CHECK(en.find("FO_It_Sword;Longsword") != std::string::npos && en.find("FO_Other") == std::string::npos && en.find("FO_Q") == std::string::npos);
    CHECK(fs::exists(root / "out/assets/Chg._xmat") && fs::exists(root / "out/assets/New._xmat") && !fs::exists(root / "out/assets/Old._xmat"));
    const std::string dialog = read_text(root / "out/data/dialog.lua");
    CHECK(dialog.find("info = \"I_1\"") != std::string::npos && dialog.find("kind = \"command\"") != std::string::npos && dialog.find("old = \"100\", new = \"400\"") != std::string::npos);
    CHECK(dialog.find("kind = \"add_condition\"") != std::string::npos && dialog.find("class = \"PlayerKnows\"") != std::string::npos && dialog.find("new = \"New\"") != std::string::npos);
    CHECK(dialog.find("I_2") == std::string::npos && dialog.find("I_3") == std::string::npos);  // classic-vs-Enhanced noise and a classic info
    const std::string manifest = read_text(root / "out/manifest.json");
    CHECK(manifest.find("against_classic") != std::string::npos && manifest.find("of them only classic-vs-Enhanced differences (not applied)") != std::string::npos);
    const std::string report = read_text(root / "out/report.md");
    CHECK(report.find("## Against the classic game") != std::string::npos && report.find("identical to the classic game's (not carried)") != std::string::npos &&
          report.find("only typographic artefacts") != std::string::npos && report.find("classic infos (not carried)") != std::string::npos);
    // the project holds the same: verified equal to the mod, the property edits as the shipped dialogue edit of I_1
    CHECK(text.find("verified: export equals the converted mod") != std::string::npos);
    Project p = load_project_file(u8str(root / "proj" / "c.rmk.json"));
    const ItemDef* sword = p.item("It_Sword");
    CHECK(sword && sword->value == 75 && !sword->damage.has_value() && !p.item("It_Spear") && !p.item("It_Old") && p.item("It_Axe"));
    CHECK_EQ(p.dialog_edits.size(), size_t(1));
    if (p.dialog_edits.size() == 1) {
        const DialogEdit& e = p.dialog_edits[0];
        CHECK(e.info == "I_1" && e.ops.size() == 2 && e.ops[0].kind == "command" && e.ops[0].index == 2 && e.ops[0].name == "Amount" && e.ops[0].old_value == "100" && e.ops[0].new_value == "400");
        CHECK(e.ops[1].kind == "add_condition" && e.ops[1].cls == "PlayerKnows" && e.ops[1].name == "PlayerKnows" && e.ops[1].new_value == "New");
    }
    // a bad --classic folder is refused
    CHECK_EQ(run({mod_in, "--baseline", enh, "--classic", u8str(root / "nothing"), "--out", u8str(root / "x")}, &text), 1);
    CHECK(text.find("--classic must be the classic game's data directory") != std::string::npos);
}
