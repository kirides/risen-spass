// GPU resources of the 3D views: meshes and textures of the game, decoded on a worker thread (core/mesh.h) and uploaded on the main thread a
// few per frame. A mesh is asked for by its compiled name ("X._xmsh"); until it is ready (or when it cannot be read) mesh() returns nullptr and
// the view draws the entity's box.
#pragma once

#include <glad/glad.h>

#include <glm/glm.hpp>

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "core/gamedata.h"
#include "core/mesh.h"

namespace mk::gfx {

struct GpuMesh {
    GLuint vao = 0, vbo = 0, ibo = 0;
    bool index32 = true;
    struct Sub {
        std::string texture;  // compiled image name ("" = none)
        uint32_t first = 0, count = 0;
    };
    std::vector<Sub> subs;
    glm::vec3 box_min{0}, box_max{0};  // local, game coordinates
    size_t triangles = 0;
};

class ResourceCache {
public:
    explicit ResourceCache(std::shared_ptr<const GameData> game);
    ~ResourceCache();
    ResourceCache(const ResourceCache&) = delete;
    ResourceCache& operator=(const ResourceCache&) = delete;

    // nullptr while loading or when the mesh cannot be read (failed() tells)
    const GpuMesh* mesh(const std::string& name);
    bool failed(const std::string& name) const;
    // the texture of an image name, a white texture until it is ready or when it cannot be read
    GLuint texture(const std::string& name);
    // the size of the picture the texture was made from (not of the level that was uploaded); false until it is ready
    bool texture_size(const std::string& name, int& width, int& height) const;
    // main thread: uploads finished work, at most `budget_ms` milliseconds
    void update(double budget_ms);
    size_t loaded_meshes() const { return meshes_.size(); }
    // the project's own resource files: lower-case bare name -> path on disk; they shadow the game's files of the same name
    void set_loose(std::unordered_map<std::string, std::string> files);
    size_t pending() const;
    bool s3tc() const { return s3tc_; }

private:
    struct Job {
        bool is_mesh;
        std::string name;
    };
    struct Done {
        bool is_mesh;
        std::string name;
        std::unique_ptr<MeshData> mesh;
        std::vector<std::string> textures;  // per sub-mesh
        std::unique_ptr<ImageData> image;
        std::string error;
    };
    void worker();
    p2m::Bytes read(const std::string& name);  // a loose file, else the archives (worker thread)
    bool exists(const std::string& name);
    void upload_mesh(Done& d);
    void upload_texture(Done& d);

    std::shared_ptr<const GameData> game_;
    std::unordered_map<std::string, std::unique_ptr<GpuMesh>> meshes_;  // lower-case name
    std::unordered_map<std::string, GLuint> textures_;
    std::unordered_map<std::string, std::pair<int, int>> texture_sizes_;
    std::unordered_map<std::string, bool> requested_;  // lower-case name -> failed
    GLuint white_ = 0;
    bool s3tc_ = false;
    float aniso_ = 1.0f;  // the anisotropic filter level used for world textures (4x, or the GPU's maximum when lower)

    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Job> jobs_;
    std::deque<Done> done_;
    bool stop_ = false;
    std::unordered_map<std::string, std::string> loose_;  // guarded by mu_
    std::thread thread_;
};

}  // namespace mk::gfx
