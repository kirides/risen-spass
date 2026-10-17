// Actors (`._xmac`, docs/RE.md section 54): the game's characters and animals. A file is "GR01" "MA02" around an EMotion FX actor ("XAC ", version 1.0): chunks of
// nodes (the skeleton), materials with their texture layers, meshes with vertex layers and skinning. The editor draws the bind pose without skinning: the vertices are
// stored in model space, so the meshes can be drawn as they are.
#pragma once

#include <string>
#include <vector>

#include "core/mesh.h"

namespace mk {

struct ActorInfo {
    std::string name;               // the actor's name inside the file
    std::vector<std::string> nodes;  // the skeleton's node names
    std::vector<std::string> diffuse_textures;  // per material (as the file numbers them): the diffuse layer's image, "" = none
    size_t meshes = 0, collision_meshes = 0;
};

// The visible meshes of the actor as one MeshData in the editor's own vertex layout (position, normal, UV: 32 bytes; offsets 0, 12, 24); one sub-mesh per
// material; `SubMesh::texture` names the diffuse image ("X" of "X._ximg"). Collision meshes are left out. Throws p2m::Error for a file that is no actor.
MeshData decode_actor(const p2m::Bytes& file, ActorInfo* info = nullptr);

}  // namespace mk
