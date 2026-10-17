#include "app/render/gl_util.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>

namespace mk::gfx {

namespace {

GLuint compile(GLenum type, const char* src, std::string* error) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        if (error) *error = log;
        glDeleteShader(s);
        return 0;
    }
    return s;
}

const char* kLineVs = R"(#version 330 core
layout(location = 0) in vec3 pos;
layout(location = 1) in vec4 col;
uniform mat4 view_proj;
out vec4 v_col;
void main() { v_col = col; gl_Position = view_proj * vec4(pos, 1.0); }
)";
const char* kLineFs = R"(#version 330 core
in vec4 v_col;
out vec4 frag;
void main() { frag = v_col; }
)";

Program& line_program() {
    static Program p;
    if (!p.ok()) p.build(kLineVs, kLineFs, nullptr);
    return p;
}

}  // namespace

Program::~Program() {
    if (id_) glDeleteProgram(id_);
}

bool Program::build(const char* vs, const char* fs, std::string* error) {
    GLuint v = compile(GL_VERTEX_SHADER, vs, error);
    GLuint f = v ? compile(GL_FRAGMENT_SHADER, fs, error) : 0;
    if (!v || !f) {
        if (v) glDeleteShader(v);
        return false;
    }
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, sizeof log, nullptr, log);
        if (error) *error = log;
        glDeleteProgram(p);
        return false;
    }
    if (id_) glDeleteProgram(id_);
    id_ = p;
    return true;
}

void Program::set(const char* name, const glm::mat4& m) const { glUniformMatrix4fv(uniform(name), 1, GL_FALSE, glm::value_ptr(m)); }
void Program::set(const char* name, const glm::vec3& v) const { glUniform3fv(uniform(name), 1, glm::value_ptr(v)); }
void Program::set(const char* name, const glm::vec4& v) const { glUniform4fv(uniform(name), 1, glm::value_ptr(v)); }
void Program::set(const char* name, float v) const { glUniform1f(uniform(name), v); }
void Program::set(const char* name, int v) const { glUniform1i(uniform(name), v); }

LineBatch::~LineBatch() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
}

void LineBatch::line(const glm::vec3& a, const glm::vec3& b, uint32_t color) {
    verts_.push_back({a.x, a.y, a.z, color});
    verts_.push_back({b.x, b.y, b.z, color});
}

void LineBatch::box(const glm::vec3& mn, const glm::vec3& mx, uint32_t c) {
    const glm::vec3 p[8] = {{mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mn.y, mx.z}, {mn.x, mn.y, mx.z},
                            {mn.x, mx.y, mn.z}, {mx.x, mx.y, mn.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z}};
    static const int e[24] = {0, 1, 1, 2, 2, 3, 3, 0, 4, 5, 5, 6, 6, 7, 7, 4, 0, 4, 1, 5, 2, 6, 3, 7};
    for (int i = 0; i < 24; i += 2) line(p[e[i]], p[e[i + 1]], c);
}

void LineBatch::cross(const glm::vec3& p, float s, uint32_t c) {
    line(p - glm::vec3(s, 0, 0), p + glm::vec3(s, 0, 0), c);
    line(p - glm::vec3(0, 0, s), p + glm::vec3(0, 0, s), c);
    line(p, p + glm::vec3(0, s * 2, 0), c);
}

void LineBatch::upload() {
    if (!vao_) {
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(V), reinterpret_cast<void*>(0));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(V), reinterpret_cast<void*>(12));
        glBindVertexArray(0);
    }
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts_.size() * sizeof(V)), verts_.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    uploaded_ = verts_.size();
}

void LineBatch::draw(const glm::mat4& vp) const {
    if (!uploaded_) return;
    Program& p = line_program();
    p.use();
    p.set("view_proj", vp);
    glBindVertexArray(vao_);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(uploaded_));
    glBindVertexArray(0);
}

RenderTarget::~RenderTarget() {
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    if (color_) glDeleteTextures(1, &color_);
    if (depth_) glDeleteRenderbuffers(1, &depth_);
}

void RenderTarget::resize(int w, int h) {
    w = std::max(16, w);
    h = std::max(16, h);
    if (w == w_ && h == h_ && fbo_) return;
    w_ = w;
    h_ = h;
    if (!fbo_) {
        glGenFramebuffers(1, &fbo_);
        glGenTextures(1, &color_);
        glGenRenderbuffers(1, &depth_);
    }
    glBindTexture(GL_TEXTURE_2D, color_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderTarget::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, w_, h_);
}

void RenderTarget::unbind() const { glBindFramebuffer(GL_FRAMEBUFFER, 0); }

glm::vec3 FlyCamera::forward() const { return {std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)}; }
glm::vec3 FlyCamera::right() const { return glm::normalize(glm::cross(forward(), glm::vec3(0, 1, 0))); }
glm::mat4 FlyCamera::view() const { return glm::lookAt(pos, pos + forward(), glm::vec3(0, 1, 0)); }
glm::mat4 FlyCamera::proj(float aspect) const { return glm::perspective(glm::radians(fov), aspect, 20.0f, 400000.0f); }

void FlyCamera::ray(float u, float v, float aspect, glm::vec3& origin, glm::vec3& dir) const {
    const float t = std::tan(glm::radians(fov) * 0.5f);
    const float x = (u * 2 - 1) * t * aspect, y = (1 - v * 2) * t;
    const glm::vec3 f = forward(), r = right(), up = glm::cross(r, f);
    origin = pos;
    dir = glm::normalize(f + r * x + up * y);
}

void FlyCamera::look_at(const glm::vec3& target, float distance) {
    // keep the heading, look down a little, stand back
    pitch = -0.45f;
    pos = target - forward() * distance;
}

}  // namespace mk::gfx
