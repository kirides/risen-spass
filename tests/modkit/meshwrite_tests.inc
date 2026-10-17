// Tests of the mesh writer (core/meshwrite.h). Included by modkit_tests.cpp.

#include "core/meshwrite.h"

static MeshPart cube_part(const std::string& material) {
    MeshPart p;
    p.material = material;
    // a unit cube with 24 vertices (4 per face) so that normals and UVs are per face
    const float s = 50;
    struct Face {
        float n[3], u[3], v[3];
    };
    const Face faces[6] = {{{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}}, {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
                           {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}}, {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},  {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}}};
    for (const Face& f : faces) {
        const uint32_t b = static_cast<uint32_t>(p.positions.size());
        for (int k = 0; k < 4; ++k) {
            const float a = (k == 0 || k == 3) ? -1.f : 1.f, c = (k < 2) ? -1.f : 1.f;
            p.positions.push_back({s * (f.n[0] + a * f.u[0] + c * f.v[0]), s * (f.n[1] + a * f.u[1] + c * f.v[1]), s * (f.n[2] + a * f.u[2] + c * f.v[2])});
            p.normals.push_back({f.n[0], f.n[1], f.n[2]});
            p.uvs.push_back({(a + 1) / 2, (c + 1) / 2});
        }
        for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) p.indices.push_back(b + i);
    }
    return p;
}

TEST(meshwrite_round_trip) {
    MeshBuild m;
    m.parts.push_back(cube_part("ItWpn_SwordMisc_01_Diffuse_01._xmat"));
    m.parts.push_back(cube_part("ItWpn_SwordBlades_01_Diffuse_01._xmat"));
    for (auto& v : m.parts[1].positions) v[0] += 200;  // the second part sits beside the first
    std::string error;
    const std::string file = build_xmsh(m, error);
    CHECK(error.empty() && !file.empty());
    CHECK(build_xmsh(m, error) == file);  // deterministic
    MeshData d;
    try {
        d = decode_mesh(file);
    } catch (const std::exception& e) {
        std::printf("  decode failed: %s\n", e.what());
        CHECK(false);
        return;
    }
    CHECK_EQ(d.vertex_count, 48u);
    CHECK_EQ(d.index_count, 72u);
    CHECK_EQ(d.stride, 52u);
    CHECK(d.index32);
    CHECK_EQ(d.submeshes.size(), size_t(2));
    CHECK(d.submeshes[0].material == "ItWpn_SwordMisc_01_Diffuse_01._xmat" && d.submeshes[1].material == "ItWpn_SwordBlades_01_Diffuse_01._xmat");
    CHECK(d.submeshes[0].first_index == 0 && d.submeshes[0].index_count == 36 && d.submeshes[0].first_vertex == 0 && d.submeshes[0].vertex_count == 24);
    CHECK(d.submeshes[1].first_index == 36 && d.submeshes[1].first_vertex == 24 && d.submeshes[1].vertex_count == 24);
    CHECK_EQ(d.box_min[0], -50.0f);
    CHECK_EQ(d.box_max[0], 250.0f);  // the second part ends at 200 + 50
    CHECK_EQ(d.box_min[1], -50.0f);
    // the vertices came through: position and normal of vertex 0, the UV flipped (v stored as 1 - v), the indices absolute for the second part
    float f[3];
    std::memcpy(f, d.vertices.data() + static_cast<size_t>(d.pos), 12);
    CHECK(f[2] == 50.0f || f[2] == -50.0f);
    float uv[2];
    std::memcpy(uv, d.vertices.data() + static_cast<size_t>(d.uv), 8);
    CHECK(uv[0] == 0.0f && uv[1] == 1.0f);  // (u 0, v 0) of the model
    uint32_t first_of_second = 0;
    std::memcpy(&first_of_second, d.indices.data() + 36 * 4, 4);
    CHECK_EQ(first_of_second, 24u);
    // the tangent is a unit vector perpendicular to the normal
    float nrm[3], tan[3];
    std::memcpy(nrm, d.vertices.data() + static_cast<size_t>(d.normal), 12);
    std::memcpy(tan, d.vertices.data() + 24, 12);
    CHECK(std::fabs(nrm[0] * tan[0] + nrm[1] * tan[1] + nrm[2] * tan[2]) < 1e-4f);
    CHECK(std::fabs(std::sqrt(tan[0] * tan[0] + tan[1] * tan[1] + tan[2] * tan[2]) - 1.0f) < 1e-4f);
    // refusals
    MeshBuild bad;
    CHECK(build_xmsh(bad, error).empty() && !error.empty());
    bad.parts.push_back(cube_part("X._xmat"));
    bad.parts[0].indices.push_back(99);
    CHECK(build_xmsh(bad, error).empty() && error.find("whole number") != std::string::npos);
    bad.parts[0].indices.back() = 0;
    bad.parts[0].indices.push_back(1);
    bad.parts[0].indices.push_back(2);
    bad.parts[0].indices[36] = 999;
    CHECK(build_xmsh(bad, error).empty() && error.find("does not exist") != std::string::npos);
    bad.parts[0] = cube_part("");
    CHECK(build_xmsh(bad, error).empty() && error.find("material") != std::string::npos);
    // computed normals when the model has none: a single triangle facing +z
    MeshBuild tri;
    MeshPart t;
    t.material = "X._xmat";
    t.positions = {{0, 0, 0}, {10, 0, 0}, {0, 10, 0}};
    t.indices = {0, 1, 2};
    tri.parts.push_back(t);
    const MeshData td = decode_mesh(build_xmsh(tri, error));
    float tn[3];
    std::memcpy(tn, td.vertices.data() + static_cast<size_t>(td.normal), 12);
    CHECK(tn[2] > 0.99f);
}

TEST(meshwrite_reproduces_a_shipped_mesh) {
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped: no --data)\n");
        return;
    }
    // It_Wpn_1H_Club._xmsh: read it, write it again from its pieces (raw vertices, indices, the bounding tree and the time stamp as they are): the same bytes
    const std::string orig = g->read("It_Wpn_1H_Club._xmsh");
    const MeshData d = decode_mesh(orig);
    CHECK(d.stride == 52 && d.index32 && d.submeshes.size() == 1);
    RawPart part;
    part.material = d.submeshes[0].material;
    for (uint32_t i = 0; i < d.vertex_count; ++i) {
        RawVertex v;
        std::memcpy(&v, d.vertices.data() + static_cast<size_t>(i) * 52, 52);
        part.vertices.push_back(v);
    }
    part.indices.resize(d.index_count);
    std::memcpy(part.indices.data(), d.indices.data(), d.indices.size());
    // the bounding tree bytes: after the VertexCount property of the submesh
    const size_t vc = orig.find("VertexCount");
    CHECK(vc != std::string::npos);
    const size_t nodes_at = vc + 11 + 2 + 4 + 2 + 4 + 4;  // name, type "long", 0x1e, size, the value
    CHECK(p2m::rd16(orig, nodes_at) == 0xc9);
    const uint32_t count = p2m::rd32(orig, nodes_at + 2);
    std::vector<std::string> nodes{orig.substr(nodes_at + 6, static_cast<size_t>(count) * 32)};
    const uint64_t filetime = p2m::rd64(orig, 0x18);
    const std::string again = build_xmsh_raw({part}, filetime, &nodes);
    CHECK(again.size() == orig.size());
    CHECK(again == orig);
    // and every shipped mesh with the game's static vertex (stride 52, one tree node per part) decodes with the same layout the writer makes
    size_t checked = 0, same_header = 0;
    for (auto& a : g->assets("_xmsh")) {
        if (checked >= 40) break;
        try {
            const std::string f = g->read(a.name);
            const MeshData m = decode_mesh(f);
            if (m.stride != 52 || !m.index32 || m.pos != 0 || m.normal != 12) continue;
            ++checked;
            // the writer's first 12 header bytes: magic, 0x28, the stream length is data dependent; the declaration of the payload is the writer's
            const uint32_t payload = p2m::rd32(f, 16);
            MeshBuild probe;
            probe.parts.push_back(cube_part("X._xmat"));
            std::string err;
            const std::string mine = build_xmsh(probe, err);
            if (f.compare(payload, 64, mine, p2m::rd32(mine, 16), 64) == 0) ++same_header;
        } catch (const std::exception&) {
        }
    }
    std::printf("  shipped static meshes with the writer's declaration and stream table: %zu of %zu\n", same_header, checked);
    CHECK(checked > 10 && same_header == checked);
}
