// Tests of the painted water bodies (core/water.h). Included by modkit_tests.cpp.

#include "core/water.h"

TEST(water_polygon_checks_and_mesh) {
    mk::WaterBody w;
    w.name = "MK_Pond";
    w.height = 123;
    w.points = {{0, 0}, {1000, 0}, {1000, 600}, {500, 300}, {0, 600}};  // a concave outline (a notch in the top edge)
    CHECK(mk::water_polygon_simple(w));
    CHECK(std::fabs(mk::water_area(w) - (1000.0 * 600 - 0.5 * 1000 * 300)) < 1.0);
    std::map<std::string, std::string> files;
    std::string why;
    CHECK(mk::build_water(w, files, why));
    CHECK(files.size() == 1 && files.count("assets/MK_Pond._xmsh") == 1);
    if (files.size() == 1) {
        const MeshData d = decode_mesh(files["assets/MK_Pond._xmsh"]);
        CHECK(d.submeshes.size() == 1 && d.submeshes[0].material == "Special_Water_Island_01_Diffuse_01._xmat");
        CHECK(d.index_count == 2 * 3 * 3 && d.vertex_count == 5);  // 3 triangles, both faces
        CHECK(std::fabs((d.box_max[0] - d.box_min[0]) - 1000.0f) < 1.0f && std::fabs(d.box_max[1] - d.box_min[1]) < 0.01f);
    }
    // crossing edges, too few points, no area: refused
    mk::WaterBody bad = w;
    bad.points = {{0, 0}, {1000, 600}, {1000, 0}, {0, 600}};  // a bow tie
    CHECK(!mk::water_polygon_simple(bad) && !mk::build_water(bad, files, why));
    bad.points = {{0, 0}, {10, 0}};
    CHECK(!mk::water_polygon_simple(bad));
    bad.points = {{0, 0}, {10, 0}, {20, 0}};
    CHECK(!mk::water_polygon_simple(bad));
    // clockwise works too
    mk::WaterBody cw = w;
    std::reverse(cw.points.begin(), cw.points.end());
    CHECK(mk::build_water(cw, files, why));
}

TEST(water_is_saved_and_exported_as_template_mesh_and_spawn) {
    Project p = load_showcase();
    mk::WaterBody w;
    w.uid = new_uid();
    w.name = "MK_Pond";
    w.material = "Special_Water_Ocean_01_Diffuse_01._xmat";
    w.height = 500;
    w.points = {{-31000, -4000}, {-30000, -4000}, {-30000, -3000}, {-31000, -3000}};
    p.waters.push_back(w);
    const Project q = project_from_json(to_json(p));
    CHECK(q.waters.size() == 1 && q.waters[0].points.size() == 4 && q.waters[0].height == 500 && q.waters[0].material == "Special_Water_Ocean_01_Diffuse_01._xmat");
    const auto files = render_mod(q);
    CHECK(files.count("data/looks.lua") == 1 && files.at("data/looks.lua").find("MK_Pond") != std::string::npos);
    CHECK(files.at("data/looks.lua").find("Obj_Nat_Waterfall_Large") != std::string::npos);
    const std::string world = files.at("data/world.lua");
    CHECK(world.find("MK_Pond") != std::string::npos && world.find("-30500") != std::string::npos);  // the centre
    CHECK(files.at("main.lua").find("data.looks") != std::string::npos);
    auto g = game_data();
    if (!g) return;
    size_t errors = 0;
    for (auto& pr : validate(q, g.get()))
        if (pr.area == "Water" && pr.severity == Severity::Error) {
            ++errors;
            std::printf("  %s: %s\n", pr.subject.c_str(), pr.message.c_str());
        }
    CHECK(errors == 0);
    const fs::path out = temp_dir("waterexport") / "mods" / "m";
    ExportOptions o;
    o.project_dir = showcase_dir();
    o.game = g.get();
    CHECK(export_mod(q, u8str(out), o).ok);
    CHECK(fs::exists(out / "assets" / "MK_Pond._xmsh"));
}
