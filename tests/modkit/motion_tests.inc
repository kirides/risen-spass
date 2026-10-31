// Tests of motions and skinning (core/skin.h). Included by modkit_tests.cpp after the actor tests.

#include "core/skin.h"

namespace {

// the box of the positions of 32-byte vertices
std::array<float, 6> vertex_box(const std::string& v) {
    std::array<float, 6> b = {1e30f, 1e30f, 1e30f, -1e30f, -1e30f, -1e30f};
    const auto* f = reinterpret_cast<const float*>(v.data());
    for (size_t i = 0; i < v.size() / 32; ++i)
        for (size_t k = 0; k < 3; ++k) {
            b[k] = std::min(b[k], f[i * 8 + k]);
            b[3 + k] = std::max(b[3 + k], f[i * 8 + k]);
        }
    return b;
}

const AssetEntry* find_motion(const GameData& g, const std::string& part) {
    for (auto& a : g.assets("_xmot"))
        if (a.name.find(part) != std::string::npos) return &a;
    return nullptr;
}

}  // namespace

TEST(motion_rejects_garbage) {
    auto throws = [](const std::string& s) {
        try {
            decode_motion(s);
        } catch (const p2m::Error&) {
            return true;
        }
        return false;
    };
    CHECK(throws(""));
    CHECK(throws(std::string(100, 'x')));
    CHECK(throws("GR01MO01" + std::string(80, '\0')));
    CHECK(throws("GR01MO01" + std::string(40, '\0') + "XSM " + std::string(40, '\0')));
}

TEST(skin_math_on_a_two_bone_chain) {
    // a root at the origin and a child one unit up (y); the child turns 90 degrees about z: a vertex on the child at (0, 2, 0) moves to (-1, 1, 0)
    Skeleton s;
    s.names = {"root", "child"};
    s.parent = {-1, 0};
    s.bind.resize(2);
    s.bind[1].pos[1] = 1;
    ActorSkin skin;
    skin.skeleton = s;
    skin.bone = {{1, 0, 0, 0}};
    skin.weight = {{1, 0, 0, 0}};
    std::string bind(32, '\0');
    const float v[8] = {0, 2, 0, 0, 1, 0, 0, 0};
    std::memcpy(bind.data(), v, 32);
    std::vector<Xform> local = s.bind;
    const float h = std::sqrt(0.5f);
    local[1].rot[2] = h;  // 90 degrees about z
    local[1].rot[3] = h;
    std::string out;
    skin_vertices(skin, skin_palette(s, local), bind, out);
    const auto* f = reinterpret_cast<const float*>(out.data());
    CHECK(std::fabs(f[0] + 1.0f) < 1e-4f && std::fabs(f[1] - 1.0f) < 1e-4f && std::fabs(f[2]) < 1e-4f);
    CHECK(std::fabs(f[3] + 1.0f) < 1e-4f && std::fabs(f[4]) < 1e-4f);  // the normal (0, 1, 0) turns to (-1, 0, 0)
    // the bind pose changes nothing
    skin_vertices(skin, skin_palette(s, s.bind), bind, out);
    CHECK(out == bind);
}

TEST(motion_and_skin_match_the_game) {
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped: no --data)\n");
        return;
    }
    // every motion of the game decodes to its last byte (the reader throws otherwise)
    size_t files = 0, ok = 0;
    std::string failed;
    for (auto& a : g->assets("_xmot")) {
        if (a.path.rfind("_emfx36/", 0) != 0) continue;  // the lip-sync motions of the speech are another format (XPM)
        ++files;
        try {
            const Motion m = decode_motion(g->read(a.name));
            if (!m.tracks.empty() && m.duration >= 0) ++ok;
        } catch (const std::exception& e) {
            if (failed.size() < 600) failed += " " + a.name + " (" + e.what() + ")";
        }
    }
    std::printf("  motions: %zu files, %zu decode\n", files, ok);
    if (!failed.empty()) std::printf("  failed:%s\n", failed.c_str());
    CHECK(files > 2000 && ok == files);

    // the wolf: every vertex has weights that add up to 1 on bones of its skeleton, the bind pose stays put, and a walk keeps the wolf in one piece
    ActorSkin skin;
    const MeshData wolf = decode_actor(g->read("Ani_Wolf_Monster_Wolf._xmac"), nullptr, &skin);
    CHECK_EQ(skin.skeleton.names.size(), size_t(59));
    CHECK_EQ(skin.bone.size(), size_t(wolf.vertex_count));
    bool weights_ok = true;
    for (size_t v = 0; v < skin.bone.size(); ++v) {
        float sum = 0;
        for (size_t k = 0; k < 4; ++k) {
            sum += skin.weight[v][k];
            if (skin.weight[v][k] > 0 && skin.bone[v][k] >= skin.skeleton.names.size()) weights_ok = false;
        }
        if (std::fabs(sum - 1.0f) > 1e-3f) weights_ok = false;
    }
    CHECK(weights_ok);
    std::string out;
    skin_vertices(skin, skin_palette(skin.skeleton, skin.skeleton.bind), wolf.vertices, out);
    const auto b0 = vertex_box(wolf.vertices), b1 = vertex_box(out);
    float drift = 0;
    for (size_t i = 0; i < 6; ++i) drift = std::max(drift, std::fabs(b0[i] - b1[i]));
    CHECK(drift < 0.01f);

    const AssetEntry* walk = find_motion(*g, "Wolf_Stand_None_None_P0_Move_Walk_N_Fwd");
    CHECK(walk != nullptr);
    if (!walk) return;
    const Motion m = decode_motion(g->read(walk->name));
    CHECK(m.duration > 1.0f && m.duration < 3.0f);
    float worst = 0;
    for (float t = 0; t < m.duration; t += 0.1f) {
        skin_vertices(skin, skin_palette(skin.skeleton, motion_pose(skin.skeleton, &m, t)), wolf.vertices, out);
        const auto b = vertex_box(out);
        for (size_t i = 0; i < 3; ++i) worst = std::max({worst, b0[i] - b[i], b[3 + i] - b0[3 + i]});
    }
    std::printf("  bind box y %.0f..%.0f z %.0f..%.0f x %.0f..%.0f; the walk leaves it by at most %.0f\n", b0[1], b0[4], b0[2], b0[5], b0[0], b0[3], worst);
    CHECK(worst < 60.0f);  // legs swing, the body does not blow up (the opposite rotation direction leaves the bind box by 184)
}

TEST(human_walk_moves_a_character) {
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped: no --data)\n");
        return;
    }
    // Sara: skeleton + dress + head, one skin over all parts; the human walk cycle names her bones
    const TemplateInfo* t = g->tpl("Sara");
    CHECK(t != nullptr);
    if (!t) return;
    ActorSkin skin;
    const MeshData body = decode_character(actor_parts(*g, *t), [&](const std::string& n) { return g->read(n); }, &skin);
    std::printf("  Sara: %u vertices, %zu bones\n", body.vertex_count, skin.skeleton.names.size());
    CHECK(body.vertex_count > 1000 && skin.bone.size() == body.vertex_count);
    const AssetEntry* walk = find_motion(*g, "Hero_Stand_None_None_P0_Move_Walk_N_Fwd");
    CHECK(walk != nullptr);
    if (!walk) return;
    const Motion m = decode_motion(g->read(walk->name));
    size_t named = 0;
    for (auto& tr : m.tracks)
        if (skin.skeleton.find(tr.name) >= 0) ++named;
    std::printf("  walk '%s': %zu tracks, %zu of them name her bones, %.2f s\n", walk->name.c_str(), m.tracks.size(), named, m.duration);
    CHECK(named * 2 > m.tracks.size());
    const auto b0 = vertex_box(body.vertices);
    std::string out;
    float worst = 0;
    for (float tm = 0; tm < m.duration; tm += 0.1f) {
        skin_vertices(skin, skin_palette(skin.skeleton, motion_pose(skin.skeleton, &m, tm)), body.vertices, out);
        const auto b = vertex_box(out);
        for (size_t i = 0; i < 3; ++i) worst = std::max({worst, b0[i] - b[i], b[3 + i] - b0[3 + i]});
    }
    std::printf("  bind box y %.0f..%.0f z %.0f..%.0f x %.0f..%.0f; the walk leaves it by at most %.0f\n", b0[1], b0[4], b0[2], b0[5], b0[0], b0[3], worst);
    CHECK(worst < 150.0f);
}
