// Wavefront OBJ and glTF 2.0 (.gltf with embedded or neighbouring buffers, .glb) models for the mesh writer (core/meshwrite.h): triangles with positions,
// normals and one UV set, grouped by material. docs/editor/REQUIREMENTS.md AST-4.
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "core/meshwrite.h"

namespace mk {

// a model as read: one group per source material, in the source's coordinates
struct ModelGroup {
    std::string material;  // the material's name in the file ("" = none)
    std::vector<std::array<float, 3>> positions, normals;  // normals may be empty
    std::vector<std::array<float, 2>> uvs;                  // may be empty
    std::vector<uint32_t> indices;                          // triangles
};
struct ModelSource {
    std::vector<ModelGroup> groups;
    std::vector<std::string> warnings;
    std::string error;  // not empty: nothing usable
    size_t triangles() const;
};

// `name` decides the format by its extension (.obj, .gltf, .glb). `read_neighbour` reads a file next to a .gltf (its .bin buffers); may be empty
ModelSource parse_model(const std::string& bytes, const std::string& name, const std::function<std::string(const std::string&)>& read_neighbour = {});

struct ModelOptions {
    double scale = 1.0;       // glTF is in metres, the game in about centimetres: 100
    bool z_up = false;        // the model's up axis is +Z (Blender export without conversion, CAD): turned to the game's +Y
    bool mirror_z = true;     // right-handed models (OBJ, glTF) to the game's left-handed space: z is negated and the triangles turned around
    std::string material;     // the game material every group gets that is not named below ("X._xmat")
    std::map<std::string, std::string> materials;  // source material name -> game material
};

// the writer's input: groups with the same target material are merged, the transforms of `opt` applied; `error` set when no group is left
MeshBuild model_to_mesh(const ModelSource& src, const ModelOptions& opt, std::string& error);

// base64 (glTF's data URIs); empty on malformed input
std::string base64_decode(const std::string& text);

}  // namespace mk
