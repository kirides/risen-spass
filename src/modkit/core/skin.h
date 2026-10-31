// Skeletons, skinning and motions (docs/RE.md section 66): what it takes to play an animation on an actor of core/actor.h. An actor's NODES chunk is the skeleton
// with its bind pose, its SKINNINGINFO gives every vertex up to four bones, a motion (`._xmot`, an EMotion FX "XSM " stream) gives the bones' keys over time. The
// editor skins on the CPU (a monster has about 4000 vertices) and draws the result like any other mesh.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "pack2mod/util.h"

namespace mk {

// the local transform of a bone: scale, then rotation (quaternion x, y, z, w), then translation
struct Xform {
    float rot[4] = {0, 0, 0, 1};
    float pos[3] = {0, 0, 0};
    float scale[3] = {1, 1, 1};
};

struct Skeleton {
    std::vector<std::string> names;
    std::vector<int> parent;  // -1 = a root
    std::vector<Xform> bind;  // the local transforms of the bind pose
    int find(const std::string& name) const;  // case-sensitive, -1 when absent
    bool empty() const { return names.empty(); }
};

// per vertex of the actor's MeshData (same order): up to four bones (indices into `skeleton`) with weights that add up to 1
struct ActorSkin {
    Skeleton skeleton;
    std::vector<std::array<uint16_t, 4>> bone;
    std::vector<std::array<float, 4>> weight;
    bool empty() const { return bone.empty() || skeleton.empty(); }
};

// the skins of the parts of a merged mesh (core/actor.h merge_meshes) as one: the skeletons are joined by bone name, the vertices follow one another
ActorSkin merge_skins(const std::vector<ActorSkin>& parts);

// ---- motions ------------------------------------------------------------------------------------------------------------------------------------

struct MotionTrack {
    std::string name;  // the bone it moves
    Xform pose;        // what the bone is without keys of a kind
    struct K3 {
        float t, v[3];
    };
    struct K4 {
        float t, q[4];
    };
    std::vector<K3> pos, scale;
    std::vector<K4> rot;
};

struct Motion {
    std::vector<MotionTrack> tracks;
    float duration = 0;  // seconds: the last key of any track
};

// throws p2m::Error for a file that is no motion
Motion decode_motion(const p2m::Bytes& file);

// ---- posing -------------------------------------------------------------------------------------------------------------------------------------

using Mat34 = std::array<float, 12>;  // row-major 3 x 4: x' = M * (x, y, z, 1)

// the skeleton's local transforms at `t` seconds of `motion` (looped); bones the motion does not name keep their bind pose, a null motion gives the bind pose
std::vector<Xform> motion_pose(const Skeleton& s, const Motion* motion, float t);
// per bone: global(local) * inverse(global(bind)): what moves a bind-pose vertex of that bone to the pose
std::vector<Mat34> skin_palette(const Skeleton& s, const std::vector<Xform>& local);
// `bind`: vertices of 32 bytes (position, normal, UV: core/actor.h) in the bind pose; `out` gets the same vertices skinned (resized)
void skin_vertices(const ActorSkin& skin, const std::vector<Mat34>& palette, const std::string& bind, std::string& out);

}  // namespace mk
