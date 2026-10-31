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
uniform int mode;        // 0 opaque, 1 masked, 2 additive, 3 alpha blended
uniform float alpha_ref;
out vec4 frag;
void main() {
    vec4 c = texture(tex, v_uv);
    if (mode == 1 && c.a < alpha_ref) discard;
    if (id_mode == 1) {
        // the picking pass: the parts of an additive or blended mesh that show are the pickable ones
        if (mode == 2 && max(c.r, max(c.g, c.b)) < 0.12) discard;
        if (mode == 3 && c.a < 0.35) discard;
        frag = tint;
        return;
    }
    if (mode == 2) {  // additive: glowing strands on black (cobwebs, waterfalls); not lit
        frag = vec4(c.rgb * tint.rgb, 1.0);
        return;
    }
    float d = abs(dot(normalize(v_n), -light));
    vec3 lit = c.rgb * (0.38 + 0.72 * d);
    frag = mode == 3 ? vec4(lit * tint.rgb, c.a * tint.a) : vec4(lit, 1.0) * tint;
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
    // a pass always starts from solid state, whatever ran before it (the blended pass of the last frame, another renderer)
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    vp_ = vp;
    light_ = glm::normalize(light);
    ids_ = ids;
    deferred_.clear();
    glActiveTexture(GL_TEXTURE0);
}

int MeshRenderer::shade_mode(int blend) {
    switch (blend) {
        case 0: return 0;
        case 2: return 2;
        case 7: return 3;
        case 8: return -1;
        default: return 1;  // masked, and the blend modes nobody uses (3 to 6): the alpha test is the safest guess
    }
}

void MeshRenderer::draw_sub(const GpuMesh& mesh, const GpuMesh::Sub& s, int mode, ResourceCache& res) {
    prog_.set("mode", mode);
    prog_.set("alpha_ref", static_cast<float>(s.mask) / 255.0f);
    glBindTexture(GL_TEXTURE_2D, res.texture_of(s));
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(s.count), mesh.index32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT,
                   reinterpret_cast<void*>(static_cast<intptr_t>(s.first) * (mesh.index32 ? 4 : 2)));
}

void MeshRenderer::draw(const GpuMesh& mesh, const glm::mat4& model, ResourceCache& res, const glm::vec4& tint) {
    prog_.set("model", model);
    prog_.set("tint", tint);
    glBindVertexArray(mesh.vao);
    for (size_t i = 0; i < mesh.subs.size(); ++i) {
        const GpuMesh::Sub& s = mesh.subs[i];
        const int mode = shade_mode(s.blend);
        if (mode < 0) continue;  // invisible
        if ((mode == 2 || mode == 3) && !ids_) {
            deferred_.push_back({&mesh, i, mode, model, tint, &res});
            continue;
        }
        draw_sub(mesh, s, mode, res);
    }
}

void MeshRenderer::end() {
    if (!deferred_.empty()) {
        // everything solid is in the depth buffer now: the glowing and see-through surfaces are tested against it but do not write to it
        prog_.use();
        prog_.set("view_proj", vp_);
        prog_.set("light", light_);
        prog_.set("tex", 0);
        prog_.set("id_mode", 0);
        glActiveTexture(GL_TEXTURE0);
        glEnable(GL_BLEND);
        glDepthMask(GL_FALSE);
        int last = -1;
        const GpuMesh* bound = nullptr;
        for (const Deferred& d : deferred_) {
            if (d.mode != last) {
                if (d.mode == 2) glBlendFunc(GL_ONE, GL_ONE);
                else glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                last = d.mode;
            }
            if (d.mesh != bound) {
                glBindVertexArray(d.mesh->vao);
                bound = d.mesh;
            }
            prog_.set("model", d.model);
            prog_.set("tint", d.tint);
            draw_sub(*d.mesh, d.mesh->subs[d.sub], d.mode, *d.res);
        }
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        deferred_.clear();
    }
    glBindVertexArray(0);
}

}  // namespace mk::gfx
