// Tests of packages and conflicts (core/package.h). Included by modkit_tests.cpp.

#include "core/package.h"

TEST(zip_round_trip) {
    std::string big(100000, 'a');
    for (size_t i = 0; i < big.size(); i += 7) big[i] = static_cast<char>('a' + (i % 26));
    std::string noise;
    uint32_t x = 12345;
    for (int i = 0; i < 5000; ++i) {
        x = x * 1664525u + 1013904223u;
        noise.push_back(static_cast<char>(x >> 24));
    }
    ZipFiles in = {{"mod/mod.toml", "id = \"x\"\n"}, {"mod/empty.txt", ""}, {"mod/data/big.lua", big}, {"mod/data/noise.bin", noise}, {"mod/strings/\xC3\xA4.csv", "\xC3\xA4;b\n"}};
    const std::string zip = zip_files(in);
    CHECK(zip_files(in) == zip);          // the same content gives the same file
    CHECK(zip.size() < big.size() / 2);  // the compressible part is deflated
    const ZipFiles out = unzip_files(zip);
    CHECK_EQ(out.size(), in.size());
    for (size_t i = 0; i < out.size() && i < in.size(); ++i) CHECK(out[i] == in[i]);
    // damaged and unsafe data is refused
    auto throws = [](const std::string& z) {
        try {
            unzip_files(z);
        } catch (const std::exception&) {
            return true;
        }
        return false;
    };
    CHECK(throws(""));
    CHECK(throws("PK not a zip but long enough to hold a record............"));
    std::string bad = zip;
    bad[45] = static_cast<char>(bad[45] ^ 0x55);  // inside the first entry's data
    CHECK(throws(bad));
    CHECK(throws(zip_files({{"../evil.lua", "x"}})));
    CHECK(throws(zip_files({{"/abs.lua", "x"}})));
    CHECK(throws(zip_files({{"a\\b.lua", "x"}})));
}

TEST(package_of_the_showcase_example) {
    Project p = load_project_file(std::string(MODKIT_SOURCE_DIR) + "/examples/modkit/showcase.rmk.json");
    CHECK(valid_mod_id(p.mod.id));
    CHECK(!valid_mod_id("1abc") && !valid_mod_id("a b") && !valid_mod_id("../x") && !valid_mod_id(""));
    const fs::path dir = temp_dir("package");
    ExportOptions o;
    o.project_dir = std::string(MODKIT_SOURCE_DIR) + "/examples/modkit";
    const std::string zip_path = u8str(dir / "chronicle.zip");
    const PackageResult r = build_package(p, o, zip_path);
    for (auto& e : r.errors) std::printf("  error: %s\n", e.c_str());
    CHECK(r.ok);
    if (!r.ok) return;
    std::ifstream f(u8(zip_path), std::ios::binary);
    const std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK_EQ(data.size(), r.bytes);
    const ZipFiles files = unzip_files(data);
    CHECK_EQ(files.size(), r.files);
    std::set<std::string> names;
    for (auto& [n, c] : files) {
        names.insert(n);
        CHECK(n.rfind(p.mod.id + "/", 0) == 0);  // everything sits below <id>/
    }
    CHECK(names.count(p.mod.id + "/mod.toml") && names.count(p.mod.id + "/main.lua") && names.count(p.mod.id + "/INSTALL.txt") && names.count(p.mod.id + "/modkit/project.json"));
    std::string toml;
    for (auto& [n, c] : files)
        if (n == p.mod.id + "/mod.toml") toml = c;
    CHECK(toml.find("id          = \"" + p.mod.id + "\"") != std::string::npos);
    // the package is the export: the project inside reads back as the same project
    for (auto& [n, c] : files)
        if (n == p.mod.id + "/modkit/project.json") {
            const Project back = project_from_json(json::parse(c));
            CHECK_EQ(back.items.size(), p.items.size());
            CHECK_EQ(back.mod.version, p.mod.version);
        }
    // no temporary folder is left behind and a bad id is refused
    Project bad = p;
    bad.mod.id = "no good";
    CHECK(!build_package(bad, o, u8str(dir / "bad.zip")).ok);
    CHECK(!fs::exists(dir / "bad.zip"));
    const std::string text = install_text(p);
    CHECK(text.find(p.mod.name) != std::string::npos && text.find("bin\\spass\\mods") != std::string::npos);
}

TEST(conflicts_between_mods) {
    Project a, b;
    a.mod.id = "a";
    b.mod.id = "b";
    ItemDef sword;
    sword.name = "My_Sword";
    sword.mode = Mode::New;
    a.items.push_back(sword);
    b.items.push_back(sword);  // both create it
    ItemDef club;
    club.name = "It_1H_Club";
    club.mode = Mode::Edit;
    a.items.push_back(club);
    b.items.push_back(club);  // both change it
    ItemDef mix;
    mix.name = "Shared_Thing";
    mix.mode = Mode::New;
    a.items.push_back(mix);
    mix.mode = Mode::Edit;
    b.items.push_back(mix);  // one creates, one changes
    ItemDef own;
    own.name = "Only_A";
    own.mode = Mode::New;
    a.items.push_back(own);
    PlacedNpc n1;
    n1.name = "Guard_Joe";
    a.npcs.push_back(n1);
    n1.name = "guard_joe";  // names are compared without case
    b.npcs.push_back(n1);
    TemplatePatch pa, pb;
    pa.template_name = pb.template_name = "Wolf";
    pa.properties["gCNPC_PS"]["Level"] = 3;
    pb.properties["gCNPC_PS"]["Level"] = 5;
    pb.properties["gCNPC_PS"]["Other"] = 1;
    a.patches.push_back(pa);
    b.patches.push_back(pb);
    const auto cs = find_conflicts(a, b);
    auto find = [&](const char* kind, const char* name) -> const Conflict* {
        for (auto& c : cs)
            if (c.kind == kind && p2m::lower(c.name) == p2m::lower(name)) return &c;
        return nullptr;
    };
    CHECK_EQ(cs.size(), size_t(5));
    CHECK(find("item", "My_Sword") && find("item", "My_Sword")->error);
    CHECK(find("item", "It_1H_Club") && !find("item", "It_1H_Club")->error);
    CHECK(find("item", "Shared_Thing") && !find("item", "Shared_Thing")->error && find("item", "Shared_Thing")->detail.find("load_after") != std::string::npos);
    CHECK(find("placed NPC", "Guard_Joe") && find("placed NPC", "Guard_Joe")->error);
    CHECK(find("template property", "Wolf.gCNPC_PS.Level") && !find("template property", "Wolf.gCNPC_PS.Level")->error);
    CHECK(!find("item", "Only_A") && !find("template property", "Wolf.gCNPC_PS.Other"));
    CHECK(find_conflicts(a, a).size() >= 4);
    CHECK(find_conflicts(Project{}, a).empty());
}

TEST(installed_mods_are_read) {
    const fs::path mods = temp_dir("installed");
    // one mod made with the editor (its project is inside), one without, one folder with a broken project
    Project p = sample_project();
    p.mod.id = "made_here";
    fs::create_directories(mods / "made_here" / "modkit");
    save_project_file(p, u8str(mods / "made_here" / "modkit" / "project.json"));
    fs::create_directories(mods / "by_hand");
    fs::create_directories(mods / "broken" / "modkit");
    {
        std::ofstream f(mods / "broken" / "modkit" / "project.json");
        f << "{ not json";
    }
    const auto all = read_installed_mods(u8str(mods), "");
    CHECK_EQ(all.size(), size_t(3));
    for (auto& m : all) {
        if (m.id == "made_here") CHECK(m.has_project && m.problem.empty());
        if (m.id == "by_hand") CHECK(!m.has_project && m.problem.empty());
        if (m.id == "broken") CHECK(!m.has_project && !m.problem.empty());
    }
    CHECK_EQ(read_installed_mods(u8str(mods), "made_here").size(), size_t(2));
    CHECK(read_installed_mods(u8str(mods / "nowhere"), "").empty());
}
