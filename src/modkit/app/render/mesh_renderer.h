// Draws game meshes (gfx::GpuMesh) with their diffuse textures, a directional light and an alpha test (foliage), and culls by the view frustum.
#pragma once

#include <glm/glm.hpp>

#include <array>

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
    void begin(const glm::mat4& view_proj, const glm::vec3& light_dir_gl);
    // tint multiplies the texture (selection highlight, ghosts); alpha < 1 draws blended
    void draw(const GpuMesh& mesh, const glm::mat4& model, ResourceCache& res, const glm::vec4& tint = glm::vec4(1));
    void end();

private:
    Program prog_;
    bool built_ = false;
};

}  // namespace mk::gfx
