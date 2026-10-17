// Tests of the actor reader (core/actor.h). Included by modkit_tests.cpp.

#include "core/actor.h"

TEST(actor_rejects_garbage) {
    auto throws = [](const std::string& s) {
        try {
            decode_actor(s);
        } catch (const p2m::Error&) {
            return true;
        }
        return false;
    };
    CHECK(throws(""));
    CHECK(throws(std::string(100, 'x')));
    CHECK(throws("GR01MA02" + std::string(80, '\0')));  // no XAC inside
    CHECK(throws("GR01MA02" + std::string(40, '\0') + "XAC " + std::string(40, '\0')));  // no chunks: no mesh
}

TEST(actor_matches_the_game) {
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped: no --data)\n");
        return;
    }
    // the wolf: 59 nodes, two materials with their diffuse textures, one visible mesh of 3857 vertices and 5744 triangles in two sub-meshes, in the wolf's size
    ActorInfo info;
    const MeshData wolf = decode_actor(g->read("Ani_Wolf_Monster_Wolf._xmac"), &info);
    CHECK_EQ(info.nodes.size(), size_t(59));
    CHECK_EQ(info.name, std::string(""));
    CHECK_EQ(info.meshes, size_t(1));
    CHECK_EQ(info.collision_meshes, size_t(1));
    CHECK(info.diffuse_textures.size() == 3 && info.diffuse_textures[0] == "Ani_Monster_Wolf_Body_01_Diffuse_S1" && info.diffuse_textures[1] == "Ani_Monster_Wolf_Claws_01_Diffuse_S1");
    CHECK_EQ(wolf.vertex_count, 3857u);
    CHECK_EQ(wolf.index_count, 17232u);
    CHECK_EQ(wolf.submeshes.size(), size_t(2));
    CHECK(wolf.submeshes[0].texture == "Ani_Monster_Wolf_Body_01_Diffuse_S1" && wolf.submeshes[1].texture == "Ani_Monster_Wolf_Claws_01_Diffuse_S1");
    CHECK(wolf.box_max[1] > 140.0f && wolf.box_max[1] < 155.0f && wolf.box_min[1] > -5.0f);        // about 150 high, standing on y = 0
    CHECK(wolf.box_max[2] - wolf.box_min[2] > 280.0f && wolf.box_max[0] - wolf.box_min[0] < 80.0f);  // about 295 long along z, 72 wide
    CHECK(g->exists("Ani_Monster_Wolf_Body_01_Diffuse_S1._ximg"));
    // every actor of the game that has a mesh decodes, and its diffuse textures exist
    size_t files = 0, ok = 0, textured = 0, textures_found = 0;
    std::string failed;
    for (auto& a : g->assets("_xmac")) {
        ++files;
        try {
            ActorInfo ai;
            const MeshData m = decode_actor(g->read(a.name), &ai);
            ++ok;
            for (auto& s : m.submeshes) {
                if (s.texture.empty()) continue;
                ++textured;
                if (g->exists(s.texture + "._ximg")) ++textures_found;
            }
            // indices stay inside the vertices
            const auto* idx = reinterpret_cast<const uint32_t*>(m.indices.data());
            for (uint32_t i = 0; i < m.index_count; ++i)
                if (idx[i] >= m.vertex_count) {
                    CHECK(false);
                    break;
                }
        } catch (const std::exception& e) {
            if (failed.size() < 900) failed += " " + a.name + " (" + e.what() + ")";
        }
    }
    std::printf("  actors: %zu files, %zu with a visible mesh; sub-meshes with a diffuse texture: %zu, found as images: %zu\n", files, ok, textured, textures_found);
    if (!failed.empty()) std::printf("  without a visible mesh / unreadable:%s\n", failed.c_str());
    CHECK(ok * 10 >= files * 7);  // most actors are bodies; the rest are skeletons only
    CHECK(textured > 50 && textures_found * 100 >= textured * 90);
}

TEST(world_monsters_have_actors) {
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped: no --data)\n");
        return;
    }
    auto w = load_world(*g, "World");
    CHECK(w != nullptr);
    if (!w) return;
    size_t monsters = 0, named = 0, decodes = 0;
    std::map<std::string, bool> tried;
    const WorldEntity* sample = nullptr;
    for (auto& e : w->entities) {
        if (e.kind != TplKind::Monster || e.template_name.empty()) continue;
        ++monsters;
        const TemplateInfo* t = g->tpl(e.template_name);
        const p2m::Val* v = t ? t->get("eCAnimation_PS.ResourceFilePath") : nullptr;
        if (!v || !v->is_str() || v->s.empty()) continue;
        ++named;
        const std::string actor = compiled_name(v->s);
        auto it = tried.find(actor);
        if (it == tried.end()) {
            bool ok = false;
            try {
                decode_actor(g->read(actor));
                ok = true;
            } catch (const std::exception&) {
            }
            it = tried.emplace(actor, ok).first;
        }
        decodes += it->second;
        if (!sample && e.template_name == "Wolf") sample = &e;
    }
    std::printf("  %zu monsters in the world, %zu name an actor, %zu of them decode (%zu distinct actors)\n", monsters, named, decodes, tried.size());
    if (sample) std::printf("  a wolf: %s at %.0f %.0f %.0f yaw %.0f\n", sample->name.c_str(), sample->x(), sample->y(), sample->z(), sample->yaw());
    CHECK(monsters > 100 && named * 10 >= monsters * 9);
    CHECK(decodes * 10 >= named * 9);
}
