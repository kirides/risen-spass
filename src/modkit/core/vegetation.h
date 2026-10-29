// The vegetation of the game's world (grass, bushes, plants) and the editor's strokes (docs/editor/REQUIREMENTS.md WLD-14, docs/RE.md section 64). A
// `*_Vegetation.lrent` layer holds one entity whose `eCVegetation_PS` set carries a grid of cells with 44-byte entries (mesh id, position, quaternion, scales,
// colour); `read_vegetation` decodes the tail far enough for the editor to list the plant meshes of the layer and to count and place the entries.
// A stroke is what the editor paints: add plants (a seeded scatter the runtime makes on the ground) or erase plants in a circle.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "pack2mod/util.h"

namespace mk {

struct VegMesh {
    uint32_t id = 0;       // the mesh id as the entries carry it: high 16 bits = the mesh slot, low 16 bits = the type
    std::string path;      // the mesh file of the vegetation ("Brush_Branch_Small_01.xmsh", the author's directory dropped)
    std::string material;  // "Special_Brushes_Nat_Veg_Herbs_01_Diffuse_01._xmat"
};

struct VegEntry {
    uint32_t mesh = 0;
    std::array<float, 3> pos{};
    float scale_h = 1, scale_v = 1;
    uint32_t color = 0;
};

struct VegetationInfo {
    std::vector<VegMesh> meshes;
    float node_dimension = 0;
    std::array<int, 4> rect{};  // left, top, right, bottom cell
    uint32_t nodes = 0, entries = 0;
    std::vector<VegEntry> sample;  // up to `max_entries` of them (for drawing)
    std::map<uint32_t, uint32_t> per_mesh;  // mesh id -> entries
};

// the vegetation set of a layer file; nullopt when the layer has none. `max_entries` limits `sample` (every n-th entry). throws p2m::Error for damaged data
std::optional<VegetationInfo> read_vegetation(const p2m::Bytes& layer_file, size_t max_entries = 0);

struct VegStroke {
    std::string uid;
    std::string entity;         // the vegetation entity's id (risen.entity.by_id form)
    std::string entity_name;    // for display: "Game_Start_Vegetation"
    bool erase = false;         // erase every plant in the circle instead of adding
    double x = 0, y = 0, z = 0; // the centre; y = the ground there (the scatter looks for the ground within +- radius + 500 of it)
    double radius = 300;
    double density = 8;         // plants per 100 x 100 units
    std::vector<uint32_t> meshes;  // the mesh ids to scatter (adding)
    int seed = 1;
    double scale_min = 0.8, scale_max = 1.4;
    uint32_t color = 0xFF154527;
    std::string world;          // "" = the project's world
    std::string notes;
};

}  // namespace mk
