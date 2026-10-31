// An actor the editor poses: the character's mesh in its bind pose, its skin (core/skin.h) and a GPU mesh whose vertices are skinned again for every pose.
#pragma once

#include <glad/glad.h>

#include <functional>
#include <string>
#include <vector>

#include "app/render/resources.h"
#include "core/actor.h"
#include "core/skin.h"

namespace mk::gfx {

class AnimMesh {
public:
    AnimMesh() = default;
    ~AnimMesh() { clear(); }
    AnimMesh(const AnimMesh&) = delete;
    AnimMesh& operator=(const AnimMesh&) = delete;

    // decodes the actors (a character's parts) and uploads the bind pose; false with the reason in `error`. The mesh name it was made for is kept in key().
    bool load(const std::string& key, const std::vector<std::string>& actors, const std::function<p2m::Bytes(const std::string&)>& read, std::string& error);
    void clear();
    bool ready() const { return gpu_.vao != 0; }
    const std::string& key() const { return key_; }
    const Skeleton& skeleton() const { return skin_.skeleton; }
    // poses the mesh: `motion` at `t` seconds (nullptr: the bind pose)
    void pose(const Motion* motion, float t);
    const GpuMesh& mesh() const { return gpu_; }
    // how many of the motion's bones the skeleton has (a motion for another skeleton matches few or none)
    size_t matching_bones(const Motion& m) const;

private:
    std::string key_;
    MeshData bind_;
    ActorSkin skin_;
    GpuMesh gpu_;
    std::string tmp_;
};

}  // namespace mk::gfx
