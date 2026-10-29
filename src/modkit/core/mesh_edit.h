// Changes of a shipped mesh (the project's `meshes` section, docs/editor/REQUIREMENTS.md WLD-12): the material a sub-mesh names and a scale. The mesh is decoded
// (core/mesh.h), changed and written again with the model writer (core/meshwrite.h, one bounding node per sub-mesh) and exported as a loose copy under the same name
// in assets/, which shadows the packed one for every entity that uses the mesh. Only the game's static vertex format (52 bytes: position, normal, tangent, two
// colours, one UV set) can be written; other meshes (vegetation, actors) are refused with a reason.
#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

#include "pack2mod/util.h"

namespace mk {

struct MeshEdit {
    std::string uid;
    std::string mesh;                             // "Obj_Nat_Stone_Small_Flat_01._xmsh"
    std::map<int, std::string> materials;         // sub-mesh index -> the material it names now ("X._xmat")
    std::array<float, 3> scale{1, 1, 1};          // about the mesh's origin
    std::string notes;
    bool changes_nothing() const { return materials.empty() && scale[0] == 1 && scale[1] == 1 && scale[2] == 1; }
};

// the file's sub-meshes as the editor lists them
struct MeshInfo {
    struct Sub {
        std::string material;
        uint32_t vertices = 0, triangles = 0;
    };
    std::vector<Sub> subs;
    uint32_t stride = 0;
    std::array<float, 3> box_min{}, box_max{};
    bool writable = false;  // the static 52-byte vertex format
    std::string why_not;
};
MeshInfo mesh_info(const p2m::Bytes& file);  // throws p2m::Error when the file is no readable mesh

// the changed mesh file; false with `error` when the mesh cannot be written again or an edit does not fit (sub-mesh out of range, a scale <= 0, ...)
bool apply_mesh_edit(const p2m::Bytes& original, const MeshEdit& e, p2m::Bytes& out, std::string& error);

}  // namespace mk
