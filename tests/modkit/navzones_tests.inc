// Tests of the walkable ground from the world's NavZone entities (core/navzones.h). Included by modkit_tests.cpp.

#include "core/navzones.h"

TEST(navzone_geometry) {
    const std::vector<std::array<double, 2>> square = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};
    CHECK(point_in_polygon(square, 50, 50) && point_in_polygon(square, 1, 99));
    CHECK(!point_in_polygon(square, -1, 50) && !point_in_polygon(square, 50, 101) && !point_in_polygon(square, 150, 150));
    const std::vector<std::array<double, 2>> l_shape = {{0, 0}, {100, 0}, {100, 40}, {40, 40}, {40, 100}, {0, 100}};
    CHECK(point_in_polygon(l_shape, 20, 80) && point_in_polygon(l_shape, 80, 20) && !point_in_polygon(l_shape, 80, 80));
    CHECK(!point_in_polygon({}, 0, 0));
    NavMap m;
    NavZone z;
    z.poly = square;
    z.min_x = z.min_z = 0;
    z.max_x = z.max_z = 100;
    z.low = -50;
    z.high = 200;
    m.zones.push_back(z);
    CHECK(m.on_ground(50, 0, 50) && m.on_ground(50, 199, 50));
    CHECK(!m.on_ground(50, 300, 50));   // above the tolerance: a floor above
    CHECK(!m.on_ground(50, -60, 50));   // below it
    CHECK(!m.on_ground(150, 0, 50));    // beside it
    CHECK_EQ(m.distance_to_ground(50, 0, 50), 0.0);
    CHECK(std::fabs(m.distance_to_ground(130, 0, 50) - 30.0) < 1e-6);
    CHECK(std::fabs(m.distance_to_ground(130, 0, 140) - 50.0) < 1e-6);  // from the corner (100, 100)
    CHECK(m.distance_to_ground(5000, 0, 5000) < 0);                     // nothing near
    CHECK(m.distance_to_ground(130, 500, 50) < 0);                      // another height
    CHECK(NavMap{}.distance_to_ground(0, 0, 0) < 0 && !NavMap{}.on_ground(0, 0, 0));
}

TEST(navzones_match_the_world) {
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped: no --data)\n");
        return;
    }
    auto w = load_world(*g, "World");
    CHECK(w != nullptr);
    if (!w) return;
    const auto t0 = std::chrono::steady_clock::now();
    const auto map = build_navmap(*g, *w);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(map && map->zones.size() > 1000);
    if (!map) return;
    // the humans of the world stand on walkable ground: nearly all of them are inside a zone (the others stand on paths, in doorways, ...)
    size_t npcs = 0, on = 0, near = 0;
    for (auto& e : w->entities) {
        if (e.kind != TplKind::Npc || !e.npc) continue;
        ++npcs;
        if (map->on_ground(e.x(), e.y(), e.z())) ++on;
        else if (const double d = map->distance_to_ground(e.x(), e.y(), e.z(), 400.0); d >= 0) ++near;
    }
    std::printf("  %zu zones in %.2f s; of %zu world NPCs: %zu on walkable ground, %zu more within 400 units\n", map->zones.size(), secs, npcs, on, near);
    CHECK(npcs > 200 && on * 100 >= npcs * 85);
}

TEST(navzones_contain_the_game_freepoints) {
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped: no --data)\n");
        return;
    }
    auto w = load_world(*g, "World");
    if (!w) return;
    const auto map = build_navmap(*g, *w);
    // the places the game's own NPCs walk to (freepoints FP_Guard, FP_LookAt, FP_Sleep_Ground, ...): they are on the mesh by construction
    size_t points = 0, on = 0;
    for (auto& e : w->entities) {
        if (e.template_name.rfind("FP_", 0) != 0 || e.template_name == "FP_Stand" || e.template_name == "FP_Sit_Ground" || e.template_name == "FP_Smalltalk") continue;
        // freepoints below an anchor store their matrix relative to it (positions within a few hundred units of the origin): their world position is not known here
        if (std::hypot(e.x(), e.z()) < 1500) continue;
        ++points;
        if (map->on_ground(e.x(), e.y(), e.z())) ++on;
    }
    std::printf("  of %zu walking freepoints of the world, %zu are on walkable ground\n", points, on);
    CHECK(points > 50 && on * 100 >= points * 80);
}
