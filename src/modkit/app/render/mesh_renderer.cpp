#include "app/render/mesh_renderer.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "core/log.h"

namespace mk::gfx {

namespace {

const char* kVs = R"(#version 330 core
layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 nrm;
layout(location = 2) in vec2 uv;
uniform mat4 model;
uniform mat4 view_proj;
out vec3 v_n;
out vec2 v_uv;
void main() {
    v_n = mat3(model) * nrm;
    v_uv = uv;
    gl_Position = view_proj * model * vec4(pos, 1.0);
}
)";
const char* kFs = R"(#version 330 core
in vec3 v_n;
in vec2 v_uv;
uniform sampler2D tex;
uniform vec3 light;
uniform vec4 tint;
uniform int id_mode;
out vec4 frag;
void main() {
    vec4 c = texture(tex, v_uv);
    if (c.a < 0.35) discard;
    if (id_mode == 1) {
        frag = tint;
        return;
    }
    float d = abs(dot(normalize(v_n), -light));
    vec3 lit = c.rgb * (0.38 + 0.72 * d);
    frag = vec4(lit, 1.0) * tint;
}
)";

}  // namespace

Frustum::Frustum(const glm::mat4& vp) {
    const glm::mat4 m = glm::transpose(vp);
    planes[0] = m[3] + m[0];
    planes[1] = m[3] - m[0];
    planes[2] = m[3] + m[1];
    planes[3] = m[3] - m[1];
    planes[4] = m[3] + m[2];
    planes[5] = m[3] - m[2];
}

bool Frustum::sees(const glm::vec3& mn, const glm::vec3& mx) const {
    for (const glm::vec4& p : planes) {
        const glm::vec3 v(p.x >= 0 ? mx.x : mn.x, p.y >= 0 ? mx.y : mn.y, p.z >= 0 ? mx.z : mn.z);
        if (glm::dot(glm::vec3(p), v) + p.w < 0) return false;
    }
    return true;
}

glm::mat4 entity_model(const float m[16]) {
    return glm::scale(glm::mat4(1.0f), glm::vec3(1, 1, -1)) * glm::make_mat4(m);
}

glm::vec4 MeshRenderer::id_color(uint32_t id) {
    return glm::vec4(static_cast<float>(id & 0xff) / 255.0f, static_cast<float>((id >> 8) & 0xff) / 255.0f, static_cast<float>((id >> 16) & 0xff) / 255.0f, 1.0f);
}

uint32_t MeshRenderer::id_of(const unsigned char rgba[4]) { return static_cast<uint32_t>(rgba[0]) | (static_cast<uint32_t>(rgba[1]) << 8) | (static_cast<uint32_t>(rgba[2]) << 16); }

void MeshRenderer::begin(const glm::mat4& vp, const glm::vec3& light, bool ids) {
    if (!built_) {
        std::string err;
        if (!prog_.build(kVs, kFs, &err)) log_error("mesh shader: " + err);
        built_ = true;
    }
    prog_.use();
    prog_.set("view_proj", vp);
    prog_.set("light", glm::normalize(light));
    prog_.set("tex", 0);
    prog_.set("id_mode", ids ? 1 : 0);
    glActiveTexture(GL_TEXTURE0);
}

void MeshRenderer::draw(const GpuMesh& mesh, const glm::mat4& model, ResourceCache& res, const glm::vec4& tint) {
    prog_.set("model", model);
    prog_.set("tint", tint);
    glBindVertexArray(mesh.vao);
    for (auto& s : mesh.subs) {
        glBindTexture(GL_TEXTURE_2D, res.texture(s.texture));
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(s.count), mesh.index32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT,
                       reinterpret_cast<void*>(static_cast<intptr_t>(s.first) * (mesh.index32 ? 4 : 2)));
    }
}

void MeshRenderer::end() { glBindVertexArray(0); }

}  // namespace mk::gfx
