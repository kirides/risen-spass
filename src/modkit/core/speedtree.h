// SpeedTrees (`._xspt`, docs/RE.md section 67): the game's trees, bushes and water plants. A file is "GR01" "ST01" around a SpeedTree 5 `.spt` (the SDK grows the tree from its
// parameters at run time: branch curves, leaf and frond cards), which the editor does not reproduce. What it can read is the Boundary box and the names of the textures; from
// those it builds a stand-in so that a forest is not a field of boxes: a bark-textured trunk and a crown of leaf texture, in the tree's size.
#pragma once

#include <array>
#include <string>
#include <vector>

#include "core/mesh.h"

namespace mk {

struct SpeedTreeInfo {
    std::array<float, 3> box_min{}, box_max{};
    std::vector<std::string> textures;  // the images the file names ("ST_Bark_05_Diffuse_01.tga", "MimosaLeaves.tga", ...), in file order
};

// throws p2m::Error for a file that is no SpeedTree
SpeedTreeInfo read_speedtree(const p2m::Bytes& file);

// "ST_Tree_S_MimosaTree_01" (no extension): a stand-in mesh in the editor's vertex layout (32 bytes: position, normal, UV) with two sub-meshes, trunk (the tree's bark image)
// and crown (a leaf region of the shared composite image `ST_Composite_01_Diffuse_01`). Throws p2m::Error for a file that is no SpeedTree.
MeshData speedtree_proxy(const p2m::Bytes& file, const std::string& tree_name);

// the file a template's `eCSpeedTree_PS.ResourceFilePath` ("X.spt") names as a compiled name ("X._xspt"); "" for an empty path
std::string speedtree_file(const std::string& resource_path);

}  // namespace mk
