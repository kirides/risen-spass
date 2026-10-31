// Draws game meshes (gfx::GpuMesh) with their diffuse textures, a directional light and an alpha test (foliage), and culls by the view frustum.
#pragma once

#include <glm/glm.hpp>

#include <array>
#include <vector>

#include "app/render/gl_util.h"
#include "app/render/resources.h"

namespace mk::gfx {

struct Frustum {
    std::array<glm::vec4, 6> planes{};
    explicit Frustum(const glm::mat4& view_proj);
    bool sees(const glm::vec3& mn, const glm::vec3& mx) const;  // an axis-aligned box in GL coordinates
};

// the model matrix of a game entity for GL: the world matrix (row-major, row vectors) as a column-major matrix, mirrored in z (gl_util.h)
glm::mat4 entity_model(const float m[16]);

class MeshRenderer {
public:
    // `ids`: an ID pass for picking: every fragment gets the draw's `tint` as it is (an entity number as a colour, id_color); the alpha test stays
    void begin(const glm::mat4& view_proj, const glm::vec3& light_dir_gl, bool ids = false);
    // tint multiplies the texture (selection highlight, ghosts); alpha < 1 draws blended
    void draw(const GpuMesh& mesh, const glm::mat4& model, ResourceCache& res, const glm::vec4& tint = glm::vec4(1));
    void end();

    // an entity number (>= 1, < 2^24) as the colour of the ID pass, and back (0 = nothing)
    static glm::vec4 id_color(uint32_t id);
    static uint32_t id_of(const unsigned char rgba[4]);

private:
    // how a sub-mesh is shaded: 0 opaque (alpha ignored), 1 masked (alpha test), 2 additive (unlit, black adds nothing), 3 alpha blended; -1 invisible
    static int shade_mode(int blend);
    void draw_sub(const GpuMesh& mesh, const GpuMesh::Sub& s, int mode, ResourceCache& res);
    // additive and blended sub-meshes wait until everything solid is drawn (end()): they do not write depth, so a solid mesh drawn later would paint over them
    struct Deferred {
        const GpuMesh* mesh;
        size_t sub;
        int mode;
        glm::mat4 model;
        glm::vec4 tint;
        ResourceCache* res;
    };
    std::vector<Deferred> deferred_;
    glm::mat4 vp_{1.0f};
    glm::vec3 light_{0, -1, 0};
    bool ids_ = false;
    Program prog_;
    bool built_ = false;
};

}  // namespace mk::gfx
