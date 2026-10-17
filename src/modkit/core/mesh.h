// Offline decoding of the game's compiled resources for the 3D views (docs/editor/REQUIREMENTS.md WLD-3): meshes (._xmsh, GR01/MS02), materials
// (._xmat, GR01/SM01: the diffuse texture's name) and images (._ximg, GR01/IM04: a DDS file inside). No GPU code: the app uploads the result.
//
// ._xmsh: the GR01 header (u32 schema size, u32 payload offset, u32 payload size at 12), a property stream (eCMeshResource2: SubMeshes with
// MaterialName, FirstIndex, IndexCount, FirstVertex, VertexCount; Boundary), then at the payload offset a D3D9 vertex declaration
// (D3DVERTEXELEMENT9 records up to D3DDECL_END), a header with the vertex data size, the stride, the index data size and the index format
// (101 = 16 bit, 102 = 32 bit) followed by u32 1, the vertex data (stream 0, interleaved), the index data (absolute indices) and one byte.
// Checked on all 1666 meshes of the game: 1665 decode, their positions span exactly the Boundary box (the SpeedTree billboard has FLOAT4 positions).
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "pack2mod/util.h"

namespace mk {

struct SubMesh {
    std::string material;  // "ItWpn_SwordBlades_01_Diffuse_01._xmat"
    uint32_t first_index = 0, index_count = 0, first_vertex = 0, vertex_count = 0;
};

struct MeshData {
    uint32_t stride = 0, vertex_count = 0, index_count = 0;
    int pos = -1, normal = -1, color = -1, uv = -1;  // byte offsets of the attributes in a vertex (-1 = none)
    bool index32 = true;
    std::string vertices;  // vertex_count * stride bytes
    std::string indices;   // index_count * (4 or 2) bytes
    std::vector<SubMesh> submeshes;
    std::array<float, 3> box_min{}, box_max{};
};

// throws p2m::Error with the reason
MeshData decode_mesh(const p2m::Bytes& file);
// the image of the material's first colour sampler (the diffuse map), as the compiled name ("X._ximg"); "" when none
std::string material_texture(const p2m::Bytes& file);
// every image file a material names (diffuse, normal, specular ...), raw names as written
std::vector<std::string> material_images(const p2m::Bytes& file);

struct ImageData {
    enum Format { DXT1, DXT3, DXT5, BGRA8, BGRX8, RGBA8, L8, A8L8 } format = BGRA8;
    uint32_t width = 0, height = 0;
    struct Level {
        uint32_t width, height;
        size_t offset, size;  // in `data`
    };
    std::vector<Level> levels;
    std::string data;
    bool compressed() const { return format == DXT1 || format == DXT3 || format == DXT5; }
};
// the DDS inside a ._ximg (or a plain .dds); throws p2m::Error for a format the editor does not read
ImageData decode_image(const p2m::Bytes& file);
// a compressed image as RGBA8 (the CPU fallback when the GPU has no S3TC); level 0 only
std::string decompress_rgba(const ImageData& img, uint32_t level, uint32_t* w, uint32_t* h);

}  // namespace mk
