// Tests of the object looks (core/looks.h). Included by modkit_tests.cpp.

#include "core/looks.h"

TEST(look_builds_mesh_and_material_clones) {
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped: no --data)\n");
        return;
    }
    mk::ObjectLook l;
    l.uid = new_uid();
    l.name = "MK_BigRock";
    l.base = "Obj_Nat_Stone_Small_Flat_01";
    l.mesh = "Obj_Nat_Stone_Small_Flat_01._xmsh";
    l.scale = {2, 2, 2};
    mk::MaterialEdit m;
    m.material = "Nat_Stone_Objects_02_Diffuse_01._xmat";
    m.slots["Diffuse"] = "ItWpn_SwordBlades_01_Diffuse_01.tga";
    l.materials[0] = m;
    std::map<std::string, std::string> files;
    std::string why;
    CHECK(mk::build_look(*g, l, files, why));
    CHECK(files.count("assets/MK_BigRock._xmsh") == 1 && files.count("assets/MK_BigRock_0._xmat") == 1 && files.size() == 2);
    if (files.size() == 2) {
        const mk::MeshInfo info = mk::mesh_info(files["assets/MK_BigRock._xmsh"]);
        CHECK(info.subs.size() == 1 && info.subs[0].material == "MK_BigRock_0._xmat");
        const mk::Material mat = mk::Material::parse(files["assets/MK_BigRock_0._xmat"]);
        CHECK(mat.slots()[0].linked && mat.slot_image(mat.slots()[0]) == "ItWpn_SwordBlades_01_Diffuse_01.tga");
    }
    // a look that cannot be built says why
    mk::ObjectLook bad = l;
    bad.materials[5] = m;
    CHECK(!mk::build_look(*g, bad, files, why) && !why.empty());
    bad = l;
    bad.mesh = "No_Such_Mesh._xmsh";
    CHECK(!mk::build_look(*g, bad, files, why));
    // project: saved, validated (spawns may use its name), exported with its data file
    Project p = load_showcase();
    p.looks.push_back(l);
    WorldSpawn sp;
    sp.uid = new_uid();
    sp.template_name = "MK_BigRock";
    p.world.spawns.push_back(sp);
    const Project q = project_from_json(to_json(p));
    CHECK(q.looks.size() == 1 && q.looks[0].scale[0] == 2 && q.looks[0].materials.at(0).slots.at("Diffuse") == "ItWpn_SwordBlades_01_Diffuse_01.tga");
    size_t errors = 0;
    std::string first;
    for (auto& pr : validate(q, g.get()))
        if ((pr.area == "Looks" || pr.area == "World") && pr.severity == Severity::Error && pr.subject.find("MK_BigRock") != std::string::npos) {
            ++errors;
            first = pr.message;
        }
    if (errors) std::printf("  unexpected: %s\n", first.c_str());
    CHECK(errors == 0);
    const fs::path out = temp_dir("lookexport") / "mods" / "m";
    ExportOptions o;
    o.project_dir = showcase_dir();
    o.game = g.get();
    CHECK(export_mod(q, u8str(out), o).ok);
    CHECK(fs::exists(out / "assets" / "MK_BigRock._xmsh") && fs::exists(out / "assets" / "MK_BigRock_0._xmat"));
    const std::string data = read_text(out / "data" / "looks.lua");
    CHECK(data.find("MK_BigRock.xmsh") != std::string::npos && data.find("Obj_Nat_Stone_Small_Flat_01") != std::string::npos);
    CHECK(read_text(out / "main.lua").find("data.looks") != std::string::npos);
}
