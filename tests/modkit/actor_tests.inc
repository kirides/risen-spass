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

TEST(world_humans_are_assembled_from_parts) {
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped: no --data)\n");
        return;
    }
    auto w = load_world(*g, "World");
    CHECK(w != nullptr);
    if (!w) return;
    size_t npcs = 0, with_body = 0, with_head = 0, decodes = 0;
    std::map<std::string, bool> tried;
    for (auto& e : w->entities) {
        if (e.kind != TplKind::Npc || e.template_name.empty() || !e.npc) continue;
        const TemplateInfo* t = g->tpl(e.template_name);
        if (!t) continue;
        ++npcs;
        const auto parts = actor_parts(*g, *t);
        bool body = false, head = false;
        for (size_t i = 1; i < parts.size(); ++i) (p2m::contains(p2m::lower(parts[i]), "_head") ? head : body) = true;
        with_body += body;
        with_head += head;
        const std::string name = compose_name(parts);
        auto it = tried.find(name);
        if (it == tried.end()) {
            bool ok = false;
            try {
                std::vector<MeshData> ms;
                for (auto& a : parts)
                    try {
                        ms.push_back(decode_actor(g->read(a)));
                    } catch (const std::exception&) {
                    }
                if (!ms.empty()) {
                    const MeshData m = merge_meshes(ms);
                    ok = m.vertex_count > 0 && m.index_count % 3 == 0;
                    const auto* idx = reinterpret_cast<const uint32_t*>(m.indices.data());
                    for (uint32_t i = 0; i < m.index_count && ok; ++i) ok = idx[i] < m.vertex_count;
                }
            } catch (const std::exception&) {
            }
            it = tried.emplace(name, ok).first;
        }
        decodes += it->second;
    }
    std::printf("  %zu NPCs in the world: %zu with a body part, %zu with a head, %zu draw as actors (%zu distinct)\n", npcs, with_body, with_head, decodes, tried.size());
    CHECK(npcs > 100);
    CHECK(decodes * 10 >= npcs * 7);
    // Woman_01 (head), Smith (armour), the ogre head and the alchemist head are stored big endian
    for (const char* n : {"Ani_Hero_Head_Woman_01._xmac", "Ani_Hero_Armor_Smith._xmac", "Ani_Ogre_Monster_Ogre_Head._xmac", "Ani_Hero_Armor_Woman_Dress._xmac"}) {
        try {
            ActorInfo ai;
            const MeshData m = decode_actor(g->read(n), &ai);
            CHECK(m.vertex_count > 100 && m.box_max[1] > m.box_min[1] && m.box_max[1] < 400.0f && m.box_min[1] > -50.0f);  // plausible: model space, y up
        } catch (const std::exception& e) {
            std::printf("  %s: %s\n", n, e.what());
        }
    }
    if (const TemplateInfo* gy = g->tpl("Gyrger")) {
        std::printf("  Gyrger:");
        for (auto& p : actor_parts(*g, *gy)) std::printf(" %s", p.c_str());
        std::printf("\n");
        if (const WorldEntity* e = w->by_name("Gyrger")) std::printf("  Gyrger stands at %.0f %.0f %.0f yaw %.0f\n", e->x(), e->y(), e->z(), e->yaw());
    }
    const TemplateInfo* sara = g->tpl("Sara");
    CHECK(sara != nullptr);
    if (sara) {
        const auto parts = actor_parts(*g, *sara);
        std::printf("  Sara:");
        for (auto& p : parts) std::printf(" %s", p.c_str());
        std::printf("\n");
        if (const WorldEntity* e = w->by_name("Sara")) std::printf("  Sara stands at %.0f %.0f %.0f yaw %.0f\n", e->x(), e->y(), e->z(), e->yaw());
        CHECK(parts.size() >= 2);
    }
}
