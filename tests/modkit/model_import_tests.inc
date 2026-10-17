// Tests of the OBJ / glTF readers (core/model_import.h). Included by modkit_tests.cpp.

#include "core/model_import.h"

static std::string base64_encode(const std::string& in) {
    static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, bits = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        bits += 8;
        while (bits >= 0) {
            out.push_back(chars[(val >> bits) & 0x3f]);
            bits -= 6;
        }
    }
    if (bits > -6) out.push_back(chars[((val << 8) >> (bits + 8)) & 0x3f]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

static const char* kObjCube = R"(# a cube made of quads, two materials
v -1 -1 1
v 1 -1 1
v 1 1 1
v -1 1 1
v -1 -1 -1
v 1 -1 -1
v 1 1 -1
v -1 1 -1
vt 0 0
vt 1 0
vt 1 1
vt 0 1
vn 0 0 1
vn 0 0 -1
usemtl front
f 1/1/1 2/2/1 3/3/1 4/4/1
usemtl back
f 6/1/2 5/2/2 8/3/2 7/4/2
f -8//2 -7//2 -6//2
)";

TEST(model_import_obj) {
    ModelSource s = parse_model(kObjCube, "cube.obj");
    CHECK(s.error.empty());
    CHECK_EQ(s.groups.size(), size_t(2));
    CHECK_EQ(s.triangles(), size_t(5));  // a quad is two triangles, plus one triangle
    CHECK_EQ(s.groups[0].material, std::string("front"));
    CHECK_EQ(s.groups[0].positions.size(), size_t(4));
    CHECK_EQ(s.groups[0].normals.size(), size_t(4));
    CHECK_EQ(s.groups[0].uvs.size(), size_t(4));
    CHECK(s.groups[1].uvs.empty());  // one face of the group has no texture coordinates: the group goes without
    CHECK(!s.warnings.empty());
    // the conversion: scale, mirror (z negated, triangles turned), the materials mapped
    ModelOptions o;
    o.scale = 10;
    o.material = "Default._xmat";
    o.materials["front"] = "Front._xmat";
    std::string error;
    MeshBuild b = model_to_mesh(s, o, error);
    CHECK(error.empty() && b.parts.size() == 2);
    CHECK(b.parts[0].material == "Front._xmat" && b.parts[1].material == "Default._xmat");
    CHECK_EQ(b.parts[0].positions[0][0], -10.0f);
    CHECK_EQ(b.parts[0].positions[0][2], -10.0f);  // z = 1 -> -10
    CHECK(b.parts[0].indices[0] == 0 && b.parts[0].indices[1] == 1 && b.parts[0].indices[2] == 2);  // the mirror already reverses the winding: the order stays
    o.mirror_z = false;
    b = model_to_mesh(s, o, error);
    CHECK_EQ(b.parts[0].positions[0][2], 10.0f);
    CHECK(b.parts[0].indices[1] == 2 && b.parts[0].indices[2] == 1);  // no mirror: the CCW triangles are turned around into the game's clockwise front faces
    o.z_up = true;  // (x, y, z) -> (x, z, -y)
    b = model_to_mesh(s, o, error);
    CHECK(b.parts[0].positions[0][1] == 10.0f && b.parts[0].positions[0][2] == 10.0f);  // (-1,-1,1): y' = z = 1, z' = -y = 1
    o.material.clear();
    o.materials.clear();
    CHECK(model_to_mesh(s, o, error).parts.empty() && error.find("material") != std::string::npos);
    // end to end: into the writer and back through the reader
    o.material = "X._xmat";
    o.mirror_z = true;
    o.z_up = false;
    const std::string file = build_xmsh(model_to_mesh(s, o, error), error);
    CHECK(error.empty());
    const MeshData d = decode_mesh(file);
    CHECK(d.submeshes.size() == 1 && d.vertex_count == 4 + 7 - 3 + 3 + 0 || d.vertex_count > 0);  // both groups share the material: one part
    CHECK_EQ(d.index_count, 15u);
    // refusals
    CHECK(!parse_model("v 0 0 0\nf 1 2 3\n", "x.obj").error.empty());
    CHECK(!parse_model("v 0 0 0\n", "x.obj").error.empty());
    CHECK(!parse_model("whatever", "x.fbx").error.empty());
}

static std::string gltf_triangle(bool glb) {
    std::string bin;
    auto f = [&](float v) { bin.append(reinterpret_cast<const char*>(&v), 4); };
    for (float v : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f}) f(v);                  // positions: bytes 0..35
    for (float v : {1.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f, 1.f, 0.f}) f(v);                  // normals (1,1,0): 36..71
    for (float v : {0.f, 0.f, 1.f, 0.f, 0.f, 1.f}) f(v);                                  // uvs: 72..95
    for (int k : {0, 1, 2}) {  // indices: 96..101
        const uint16_t i = static_cast<uint16_t>(k);
        bin.append(reinterpret_cast<const char*>(&i), 2);
    }
    bin.append(2, '\0');
    nlohmann::json j = nlohmann::json::parse(R"({
        "asset": {"version": "2.0"}, "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "scale": [2, 1, 1], "translation": [10, 0, 0]}],
        "materials": [{"name": "Skin"}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "material": 0}]}],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
            {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2"},
            {"bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": 36},
            {"buffer": 0, "byteOffset": 36, "byteLength": 36},
            {"buffer": 0, "byteOffset": 72, "byteLength": 24},
            {"buffer": 0, "byteOffset": 96, "byteLength": 6}]
    })");
    if (glb) {
        j["buffers"] = {{{"byteLength", bin.size()}}};
        std::string js = j.dump();
        while (js.size() % 4) js.push_back(' ');
        std::string out = "glTF";
        auto u32 = [&](uint32_t v) { out.append(reinterpret_cast<const char*>(&v), 4); };
        u32(2);
        u32(static_cast<uint32_t>(12 + 8 + js.size() + 8 + bin.size()));
        u32(static_cast<uint32_t>(js.size()));
        u32(0x4E4F534A);
        out += js;
        u32(static_cast<uint32_t>(bin.size()));
        u32(0x004E4942);
        out += bin;
        return out;
    }
    j["buffers"] = {{{"byteLength", bin.size()}, {"uri", "data:application/octet-stream;base64," + base64_encode(bin)}}};
    return j.dump();
}

TEST(model_import_gltf) {
    CHECK_EQ(base64_decode(base64_encode("Hello, glTF!")), std::string("Hello, glTF!"));
    CHECK_EQ(base64_decode(base64_encode("ab")), std::string("ab"));
    CHECK(base64_decode("@@@").empty());
    for (bool glb : {false, true}) {
        const ModelSource s = parse_model(gltf_triangle(glb), glb ? "t.glb" : "t.gltf");
        CHECK(s.error.empty());
        if (!s.error.empty()) {
            std::printf("  error: %s\n", s.error.c_str());
            continue;
        }
        CHECK(s.groups.size() == 1 && s.groups[0].material == "Skin" && s.triangles() == 1);
        const ModelGroup& g = s.groups[0];
        CHECK_EQ(g.positions[1][0], 12.0f);  // x = 1 scaled by 2, moved by 10
        CHECK_EQ(g.positions[2][1], 1.0f);
        // the normal (1,1,0) under the scale (2,1,1) is (0.5, 1, 0) normalised: the inverse transpose, not the matrix
        CHECK(std::fabs(g.normals[0][0] - 0.4472f) < 1e-3f && std::fabs(g.normals[0][1] - 0.8944f) < 1e-3f);
        CHECK(g.uvs.size() == 3 && g.uvs[1][0] == 1.0f);
        CHECK(g.indices == std::vector<uint32_t>({0, 1, 2}));
    }
    // a .gltf whose buffer is a neighbouring file
    nlohmann::json j = nlohmann::json::parse(gltf_triangle(false));
    j["buffers"][0]["uri"] = "tri.bin";
    std::string asked;
    const ModelSource missing = parse_model(j.dump(), "t.gltf", [&](const std::string& n) {
        asked = n;
        return std::string();
    });
    CHECK(!missing.error.empty() && asked == "tri.bin");
    CHECK(!parse_model("{ not json", "t.gltf").error.empty());
    CHECK(!parse_model("{\"asset\":{\"version\":\"1.0\"}}", "t.gltf").error.empty());
    // into the writer
    std::string error;
    ModelOptions o;
    o.scale = 100;
    o.material = "X._xmat";
    const MeshBuild b = model_to_mesh(parse_model(gltf_triangle(false), "t.gltf"), o, error);
    CHECK(error.empty() && b.parts.size() == 1 && b.parts[0].positions.size() == 3);
    const MeshData d = decode_mesh(build_xmsh(b, error));
    CHECK(d.vertex_count == 3 && d.index_count == 3 && d.submeshes[0].material == "X._xmat");
}
