// 3D models -> the engine's compiled meshes (docs/editor/REQUIREMENTS.md AST-4, docs/RE.md section 53). A `._xmsh` is "GR01" "MS02": a 0x28-byte header, a property stream
// (eCMeshResource2: one eCSubMesh per material with its box, index and vertex ranges and a one-node bounding tree, LightmapName, Boundary), then the payload: the
// D3D9 vertex declaration, a table of stream sizes and the vertex and index data. The writer reproduces the layout of the shipped meshes (checked byte for byte
// against It_Wpn_1H_Club._xmsh); the vertex format is the game's static one: position, normal, tangent (3 floats each), two colours, one UV set (52 bytes).
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace mk {

struct MeshPart {
    std::string material;                      // "ItWpn_SwordMisc_01_Diffuse_01._xmat": a material the engine finds (the game's or the project's)
    std::vector<std::array<float, 3>> positions;
    std::vector<std::array<float, 3>> normals;  // one per position (empty: computed from the triangles)
    std::vector<std::array<float, 2>> uvs;      // one per position, v as in OpenGL / glTF (up): the writer stores 1 - v
    std::vector<uint32_t> indices;              // triangles, indices into this part's vertices
};

struct MeshBuild {
    std::vector<MeshPart> parts;
    uint64_t filetime = 0;  // the header's time stamp (0 = a fixed value: the output is deterministic)
};

// The file's bytes. `error` is set (and the result empty) for an empty mesh, a part without triangles, an index out of range or a mismatch of the arrays.
std::string build_xmsh(const MeshBuild& m, std::string& error);

// ---- the pieces, for the tests ----
struct RawVertex {  // the game's static vertex as stored (52 bytes)
    float pos[3], normal[3], tangent[3];
    uint8_t color0[4], color1[4];
    float uv[2];
};
struct RawPart {
    std::string material;
    std::vector<RawVertex> vertices;
    std::vector<uint32_t> indices;
};
// the same from raw vertices (v is stored as it is) with a given tree node list per part: `nodes` is the bytes of the bounding tree (empty: a single node)
std::string build_xmsh_raw(const std::vector<RawPart>& parts, uint64_t filetime, const std::vector<std::string>* node_bytes = nullptr);

}  // namespace mk
