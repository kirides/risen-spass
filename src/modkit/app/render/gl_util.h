// Small OpenGL 3.3 helpers for the 3D views: a shader program, a batch of coloured lines, an offscreen render target shown as an ImGui image,
// and the fly camera. Game coordinates are left-handed (y up, yaw 0 = +z, 90 = +x); the GL side is right-handed: to_gl() mirrors z.
#pragma once

#include <glad/glad.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace mk::gfx {

inline glm::vec3 to_gl(float x, float y, float z) { return {x, y, -z}; }
inline glm::vec3 to_gl(const glm::vec3& g) { return {g.x, g.y, -g.z}; }
inline glm::vec3 from_gl(const glm::vec3& v) { return {v.x, v.y, -v.z}; }

class Program {
public:
    Program() = default;
    ~Program();
    Program(const Program&) = delete;
    Program& operator=(const Program&) = delete;
    bool build(const char* vs, const char* fs, std::string* error);
    void use() const { glUseProgram(id_); }
    GLint uniform(const char* name) const { return glGetUniformLocation(id_, name); }
    void set(const char* name, const glm::mat4& m) const;
    void set(const char* name, const glm::vec3& v) const;
    void set(const char* name, const glm::vec4& v) const;
    void set(const char* name, float v) const;
    void set(const char* name, int v) const;
    bool ok() const { return id_ != 0; }

private:
    GLuint id_ = 0;
};

// lines with a colour per vertex (0xAABBGGRR, ImGui's packing), uploaded once and drawn many times
class LineBatch {
public:
    ~LineBatch();
    void clear() { verts_.clear(); }
    void line(const glm::vec3& a, const glm::vec3& b, uint32_t color);
    // an axis-aligned box given in GL coordinates
    void box(const glm::vec3& mn, const glm::vec3& mx, uint32_t color);
    void cross(const glm::vec3& p, float size, uint32_t color);
    void upload();
    void draw(const glm::mat4& view_proj) const;
    size_t vertex_count() const { return uploaded_; }

private:
    struct V {
        float x, y, z;
        uint32_t c;
    };
    std::vector<V> verts_;
    GLuint vao_ = 0, vbo_ = 0;
    size_t uploaded_ = 0;
};

// colour + depth textures of a size; bind() renders into it, texture() is shown with ImGui::Image
class RenderTarget {
public:
    ~RenderTarget();
    void resize(int w, int h);
    void bind() const;
    void unbind() const;
    GLuint texture() const { return color_; }
    int width() const { return w_; }
    int height() const { return h_; }

private:
    GLuint fbo_ = 0, color_ = 0, depth_ = 0;
    int w_ = 0, h_ = 0;
};

struct FlyCamera {
    glm::vec3 pos{0, 3000, 0};  // GL coordinates
    float yaw = 0, pitch = -0.3f;  // radians; yaw 0 looks along GL -z (= game +z)
    float fov = 60.0f;
    float speed = 3000.0f;  // units per second
    glm::vec3 forward() const;
    glm::vec3 right() const;
    glm::mat4 view() const;
    glm::mat4 proj(float aspect) const;
    // the ray through a point of the viewport (0..1, 0..1 from the top left), in GL coordinates
    void ray(float u, float v, float aspect, glm::vec3& origin, glm::vec3& dir) const;
    void look_at(const glm::vec3& target, float distance);
};

}  // namespace mk::gfx
