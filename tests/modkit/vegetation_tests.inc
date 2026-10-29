// Tests of the vegetation reader and the strokes (core/vegetation.h, docs/RE.md section 64). Included by modkit_tests.cpp.

#include "core/vegetation.h"

TEST(vegetation_reads_every_shipped_layer) {
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped: no --data)\n");
        return;
    }
    size_t layers = 0, bad = 0;
    uint64_t entries = 0;
    std::string failures;
    for (const std::string& path : g->paths_with_prefix("projects/")) {
        if (!p2m::ends_with(p2m::lower(path), ".lrent")) continue;
        if (p2m::lower(path).find("vegetation") == std::string::npos && p2m::lower(path).find("testlevel_plane") == std::string::npos) continue;
        try {
            const auto v = mk::read_vegetation(g->read_path(path));
            if (!v) continue;
            ++layers;
            entries += v->entries;
            if (p2m::lower(path).find("game_start_vegetation") != std::string::npos) {
                CHECK_EQ(v->meshes.size(), static_cast<size_t>(36));
                CHECK_EQ(v->nodes, 604u);
                CHECK_EQ(v->entries, 15466u);
                CHECK(v->node_dimension == 1000.0f && v->rect[0] == -52 && v->rect[1] == -20);
                CHECK(v->meshes[0].path.find(".xmsh") != std::string::npos && v->meshes[0].material.find("_xmat") != std::string::npos);
                uint32_t summed = 0;
                for (auto& [id, n] : v->per_mesh) summed += n;
                CHECK_EQ(summed, v->entries);
                for (auto& [id, n] : v->per_mesh) {
                    bool known = false;
                    for (auto& m : v->meshes) known |= m.id == id;
                    CHECK(known);
                }
                const auto s = mk::read_vegetation(g->read_path(path), 500);
                CHECK(s && s->sample.size() >= 400 && s->sample.size() <= 600);
            }
        } catch (const std::exception& e) {
            if (++bad < 6) failures += "\n    " + path + ": " + e.what();
        }
    }
    std::printf("  %zu vegetation layers, %llu entries\n", layers, static_cast<unsigned long long>(entries));
    if (bad) std::printf("  failures:%s\n", failures.c_str());
    CHECK(bad == 0);
    CHECK(layers >= 12 && entries > 100000);
}

TEST(vegetation_strokes_are_saved_validated_and_exported) {
    Project p = load_showcase();
    mk::VegStroke add;
    add.uid = new_uid();
    add.entity = "00000000-0000-0000-0000-000000000001";
    add.entity_name = "Game_Start_Vegetation";
    add.x = -31000;
    add.y = 600;
    add.z = -4000;
    add.radius = 400;
    add.density = 6;
    add.meshes = {0x00030000u, 0x00040000u};
    add.seed = 7;
    mk::VegStroke erase;
    erase.uid = new_uid();
    erase.entity = add.entity;
    erase.erase = true;
    erase.x = -31500;
    erase.z = -4200;
    erase.radius = 250;
    p.vegetation = {add, erase};
    const Project q = project_from_json(to_json(p));
    CHECK(q.vegetation.size() == 2 && q.vegetation[0].meshes.size() == 2 && q.vegetation[0].meshes[1] == 0x00040000u && q.vegetation[1].erase && q.vegetation[0].seed == 7);
    size_t errors = 0;
    for (auto& pr : validate(q, nullptr))
        if (pr.area == "Vegetation" && pr.severity == Severity::Error) ++errors;
    CHECK(errors == 0);
    Project bad = q;
    bad.vegetation[0].meshes.clear();
    bad.vegetation[1].radius = 0;
    size_t errs = 0;
    for (auto& pr : validate(bad, nullptr))
        if (pr.area == "Vegetation" && pr.severity == Severity::Error) ++errs;
    CHECK_EQ(errs, static_cast<size_t>(2));
    const auto files = render_mod(q);
    const std::string w = files.at("data/world.lua");
    CHECK(w.find("vegetation") != std::string::npos && w.find("\"erase\"") != std::string::npos && w.find("Game_Start_Vegetation") != std::string::npos);
    CHECK(files.at("modkit/runtime.lua").find("apply_vegetation") != std::string::npos);
}

TEST(world_marks_the_vegetation_entities) {
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped: no --data)\n");
        return;
    }
    const auto w = load_world(*g, "World");
    size_t n = 0;
    for (const auto& e : w->entities)
        if (e.vegetation) {
            ++n;
            if (w->layers[e.layer].name == "Game_Start_Vegetation") std::printf("  Game_Start_Vegetation entity: %s (%s)\n", e.guid.c_str(), e.name.c_str());
        }
    CHECK(n >= 12);
}
