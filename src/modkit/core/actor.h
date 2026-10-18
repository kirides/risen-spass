// Actors (`._xmac`, docs/RE.md section 54): the game's characters and animals. A file is "GR01" "MA02" around an EMotion FX actor ("XAC ", version 1.0): chunks of
// nodes (the skeleton), materials with their texture layers, meshes with vertex layers and skinning. The editor draws the bind pose without skinning: the vertices are
// stored in model space, so the meshes can be drawn as they are.
#pragma once

#include <string>
#include <vector>

#include "core/gamedata.h"
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

// One MeshData of several (a human is its armour actor plus its head actor, all in the skeleton's model space): vertices and indices joined, sub-meshes kept.
MeshData merge_meshes(const std::vector<MeshData>& parts);

// The actors that make up a character as compiled names ("X._xmac"): the template's own actor (skeletons without a mesh included: the caller skips what does not
// decode), then one body part and one head from the equipped items (`worn`, then stacks with an equipment slot, then the rest of the inventory): items of
// the game carry them as eCBodyPart_PS.ResourceFilePath. Humans and ogres are only a skeleton plus those parts.
std::vector<std::string> actor_parts(const GameData& g, const TemplateInfo& t);

// "compose:A._xmac|B._xmac" is how a list of actors travels as one mesh name through the resource cache; a single name stays as it is
std::string compose_name(const std::vector<std::string>& actors);
bool is_composed(const std::string& name);
std::vector<std::string> composed_parts(const std::string& name);

}  // namespace mk
