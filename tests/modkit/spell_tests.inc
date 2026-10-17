// Tests of the Spells and NPC groups sections (core/spells.h, the spells / new_spells / npc_groups of data/balance.lua). Included by modkit_tests.cpp.

#include "core/spells.h"

static Project load_spells_example() { return load_project_file(std::string(MODKIT_SOURCE_DIR) + "/examples/modkit/spells.rmk.json"); }

TEST(spells_catalogue) {
    CHECK_EQ(shipped_spells().size(), size_t(21));  // the layer's risen.spell.names()
    const ShippedSpell* f = shipped_spell("Fireball");
    CHECK(f && f->projectile && f->crystal == "It_Crystal_Fire" && f->projectiles.size() == 3);
    CHECK(spell_form_template(*f, "crystal") == "It_Crystal_Fire" && spell_form_template(*f, "rune").empty());
    const ShippedSpell* l = shipped_spell("Light");
    CHECK(l && !l->projectile && l->forms.size() == 3);
    CHECK(spell_form_template(*l, "rune") == "It_Ru_Light" && spell_form_template(*l, "scroll") == "It_Scr_Light" && spell_form_template(*l, "orb") == "It_Orb_Light");
    CHECK(shipped_spell("Nothing") == nullptr);
    CHECK(new_spell_template("X", "crystal") == "X" && new_spell_template("X", "scroll") == "X_Scroll" && new_spell_template("X", "orb") == "X_Orb");
}

TEST(spells_round_trip_and_export) {
    const Project p = load_spells_example();
    CHECK_EQ(p.spells.size(), size_t(6));
    CHECK_EQ(p.npc_groups.size(), size_t(2));
    // the project file round trip is exact
    const Project q = project_from_json(json::parse(dump_project(p)));
    CHECK_EQ(dump_project(q), dump_project(p));
    // the generated balance data
    const json b = balance_spec(p);
    CHECK_EQ(b["spells"]["Fireball"]["mana_cost"].get<int>(), 12);
    CHECK(b["spells"]["Fireball"]["damage"].is_array() && b["spells"]["Fireball"]["damage"].size() == 3);
    CHECK_EQ(b["spells"]["Fireball"]["damage_type"].get<std::string>(), std::string("Fire"));
    CHECK_EQ(b["spells"]["Frost"]["requires"]["Frost"].get<int>(), 2);
    CHECK_EQ(b["spells"]["Frost"]["speed_mult"].get<double>(), 1.5);
    CHECK_EQ(b["spells"]["Light"]["scroll_mana_cost"].get<int>(), 3);
    CHECK_EQ(b["spells"]["Frost"]["projectile_properties"]["gCProjectile2_PS"]["SweepTestRadius"].get<int>(), 12);  // a setting without a field travels along
    const json& n = b["new_spells"]["Spass_Fireball_Strong"];
    CHECK_EQ(n["base"].get<std::string>(), std::string("Fireball"));
    CHECK(n["forms"].size() == 2 && n["forms"][1] == "scroll");
    CHECK_EQ(n["name"]["English"].get<std::string>(), std::string("Strong Fireball"));
    CHECK_EQ(n["name"]["German"].get<std::string>(), std::string("Starker Feuerball"));
    CHECK_EQ(n["damage_mult"].get<double>(), 2.0);
    CHECK_EQ(b["new_spells"]["Spass_Skeleton_Cheap"]["scroll"]["mana_cost"].get<int>(), 7);  // the per-form table is kept
    CHECK(b["npc_groups"].is_array() && b["npc_groups"].size() == 2);
    CHECK_EQ(b["npc_groups"][0]["match"].get<std::string>(), std::string("^Stingrat"));
    CHECK_EQ(b["npc_groups"][0]["hp_mult"].get<double>(), 1.5);
    CHECK_EQ(b["npc_groups"][1]["group"].get<std::string>(), std::string("Monster/Kapitel 2"));
    CHECK_EQ(b["npc_groups"][1]["skills_mult"]["ProtEdge"].get<double>(), 1.1);
    CHECK(!b["npc_groups"][0].contains("exclude"));
}

TEST(spells_import_from_data_files) {
    // export, import from the data files alone (no project.json), export again: the same balance data
    const Project p = load_spells_example();
    const fs::path dir = temp_dir("spells_import") / "modkit_spells";
    ExportOptions o;
    CHECK(export_mod(p, u8str(dir), o).ok);
    const ImportResult r = import_mod(u8str(dir), true);
    CHECK(r.ok);
    if (!r.ok) return;
    CHECK_EQ(r.project.spells.size(), p.spells.size());
    CHECK_EQ(r.project.npc_groups.size(), p.npc_groups.size());
    CHECK(r.project.extra_balance.empty());  // nothing is left as raw data any more
    CHECK_EQ(balance_spec(r.project).dump(), balance_spec(p).dump());
    for (auto& w : r.warnings)
        if (w.find("balance section") != std::string::npos) {
            ++g_failures;
            std::printf("  unexpected: %s\n", w.c_str());
        }
    const SpellDef* strong = nullptr;
    for (auto& s : r.project.spells)
        if (s.name == "Spass_Fireball_Strong") strong = &s;
    CHECK(strong && strong->mode == Mode::New && strong->base == "Fireball" && strong->label.at("German") == "Starker Feuerball");
}

TEST(spells_validation) {
    Project p = load_spells_example();
    auto problems = [&](const Project& x) { return validate(x, nullptr); };
    CHECK_EQ(error_count(problems(p)), size_t(0));
    auto with = [&](auto&& edit) {
        Project x = load_spells_example();
        edit(x);
        std::string all;
        for (auto& pr : problems(x))
            if (pr.severity == Severity::Error) all += pr.message + "\n";
        return all;
    };
    CHECK(with([](Project& x) { x.spells[0].name = "Fireballs"; }).find("no shipped spell") != std::string::npos);
    CHECK(with([](Project& x) { x.spells[2].damage_mult = 2.0; }).find("only apply to Fireball") != std::string::npos);           // projectile settings on a rune spell
    CHECK(with([](Project& x) { x.spells[0].scroll_mana_cost = 2; }).find("only apply to rune spells") != std::string::npos);     // scroll cost on a projectile spell
    CHECK(with([](Project& x) { x.spells[0].mana_mult = 2.0; }).find("exclude each other") != std::string::npos);
    CHECK(with([](Project& x) { x.spells[0].damage_type = "Wet"; }).find("unknown damage type") != std::string::npos);
    CHECK(with([](Project& x) { x.spells[0].damage = json::array({1, 2, 3, 4}); }).find("projectile levels") != std::string::npos);
    CHECK(with([](Project& x) { x.spells[4].forms = {"rune"}; }).find("does not exist for Fireball") != std::string::npos);
    CHECK(with([](Project& x) { x.spells[4].base = "Nope"; }).find("no shipped spell") != std::string::npos);
    CHECK(with([](Project& x) { x.spells[4].name = "bad name!"; }).size() > 0);
    CHECK(with([](Project& x) { x.spells.push_back(x.spells[0]); }).find("twice") != std::string::npos);
    CHECK(with([](Project& x) { x.spells[0].mana_cost = -3; }).find("negative") != std::string::npos);
    // NPC groups
    CHECK(with([](Project& x) { x.npc_groups[0].match = "[unclosed"; }).find("bad Lua pattern") != std::string::npos);
    CHECK(with([](Project& x) { x.npc_groups[0].hp_mult = -1.0; }).find("must not be negative") != std::string::npos);
    CHECK(with([](Project& x) { x.npc_groups[1].skills_mult = std::map<std::string, double>{{"Strenth", 2.0}}; }).find("unknown skill") != std::string::npos);
    // the new spell's item templates are known to the project (pickers, recipes)
    Project q = load_spells_example();
    CHECK(q.spells[4].forms.size() == 2);
}

TEST(lua_patterns_like_the_layer) {
    std::string err;
    CHECK(lua_pattern_matches("^Wolf", "Wolf_Alpha", err) && err.empty());
    CHECK(!lua_pattern_matches("^Wolf", "BigWolf", err) && err.empty());
    CHECK(lua_pattern_matches("Dead$", "Beach_Seaman_Dead", err));
    CHECK(lua_pattern_matches("%d+", "Gnome_02", err));
    lua_pattern_matches("[unclosed", "x", err);
    CHECK(!err.empty());
    lua_pattern_matches("%", "x", err);
    CHECK(!err.empty());
}

TEST(npc_groups_select_templates) {
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped: no --data)\n");
        return;
    }
    std::string err;
    NpcGroup wolves;
    wolves.match = "^Wolf";
    const auto w = npc_group_templates(wolves, *g, err);
    CHECK(err.empty() && w.size() >= 2);
    for (auto& n : w) CHECK(n.rfind("Wolf", 0) == 0);
    NpcGroup none;  // no selector: every NPC and monster, never the player's template
    const auto all = npc_group_templates(none, *g, err);
    CHECK(all.size() > 300 && std::find(all.begin(), all.end(), "PC_Hero") == all.end());
    NpcGroup chapter2;
    chapter2.group = "Monster/Kapitel 2";
    const auto c2 = npc_group_templates(chapter2, *g, err);
    CHECK(!c2.empty() && c2.size() < all.size());
    NpcGroup excl;
    excl.group = "Monster/Kapitel 2";
    excl.exclude = "^Wolf";
    CHECK(npc_group_templates(excl, *g, err).size() <= c2.size());
    NpcGroup named;
    named.names = {"Wolf", "Boar"};
    CHECK_EQ(npc_group_templates(named, *g, err).size(), size_t(2));  // names alone: no universe
    NpcGroup bad;
    bad.match = "[x";
    CHECK(npc_group_templates(bad, *g, err).empty() && !err.empty());
    // the example project's groups select something with the real data
    const Project p = load_spells_example();
    for (auto& grp : p.npc_groups) {
        const auto sel = npc_group_templates(grp, *g, err);
        if (sel.empty()) std::printf("  note: the group '%s%s' selects nothing in this game\n", grp.group.c_str(), grp.match.c_str());
    }
    const auto problems = validate(p, g.get());
    for (auto& pr : problems)
        if (pr.severity == Severity::Error) std::printf("  problem: %s: %s\n", pr.subject.c_str(), pr.message.c_str());
    CHECK_EQ(error_count(problems), size_t(0));
}
