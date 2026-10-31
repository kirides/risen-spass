// Tests of the SpeedTree reader and stand-ins (core/speedtree.h). Included by modkit_tests.cpp.

#include "core/speedtree.h"

TEST(speedtree_rejects_garbage) {
    auto throws = [](const std::string& s) {
        try {
            read_speedtree(s);
        } catch (const p2m::Error&) {
            return true;
        }
        return false;
    };
    CHECK(throws(""));
    CHECK(throws(std::string(200, 'x')));
    CHECK(throws("GR01ST01" + std::string(200, '\0')));  // no boundary
    CHECK_EQ(speedtree_file("ST_Tree_S_MimosaTree_01.spt"), std::string("ST_Tree_S_MimosaTree_01._xspt"));
    CHECK_EQ(speedtree_file("a/b\\Oak.spt"), std::string("Oak._xspt"));
    CHECK_EQ(speedtree_file(""), std::string(""));
}

TEST(speedtrees_match_the_game) {
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped: no --data)\n");
        return;
    }
    size_t files = 0, ok = 0, textures_found = 0, textures = 0;
    std::string failed;
    for (auto& a : g->assets("_xspt")) {
        ++files;
        try {
            const std::string tree = a.name.substr(0, a.name.size() - 6);
            const p2m::Bytes data = g->read(a.name);
            const SpeedTreeInfo info = read_speedtree(data);
            const MeshData m = speedtree_proxy(data, tree);
            bool inside = m.vertex_count > 20 && m.index_count % 3 == 0;
            const auto* idx = reinterpret_cast<const uint32_t*>(m.indices.data());
            for (uint32_t i = 0; i < m.index_count; ++i) inside = inside && idx[i] < m.vertex_count;
            // the stand-in stays within the tree's boundary (a little more for the crown's round shape)
            const auto* f = reinterpret_cast<const float*>(m.vertices.data());
            for (uint32_t v = 0; v < m.vertex_count; ++v)
                inside = inside && f[v * 8 + 1] >= -1.0f && f[v * 8 + 1] <= info.box_max[1] * 1.02f + 1.0f;
            if (inside) ++ok;
            else {
                float ymin = 1e30f, ymax = -1e30f;
                for (uint32_t v = 0; v < m.vertex_count; ++v) {
                    ymin = std::min(ymin, f[v * 8 + 1]);
                    ymax = std::max(ymax, f[v * 8 + 1]);
                }
                std::printf("    %s: %u vertices, y %.1f..%.1f, box top %.1f\n", tree.c_str(), m.vertex_count, ymin, ymax, info.box_max[1]);
            }
            for (auto& s : m.submeshes) {
                ++textures;
                if (!s.texture.empty() && g->exists(s.texture + "._ximg")) ++textures_found;
            }
        } catch (const std::exception& e) {
            if (failed.size() < 400) failed += " " + a.name + " (" + e.what() + ")";
        }
    }
    std::printf("  speedtrees: %zu files, %zu give a stand-in; sub-meshes with an existing image: %zu of %zu\n", files, ok, textures_found, textures);
    if (!failed.empty()) std::printf("  failed:%s\n", failed.c_str());
    CHECK(files >= 39 && ok == files);
    CHECK(textures_found * 10 >= textures * 9);
}
