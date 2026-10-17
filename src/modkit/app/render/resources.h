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
    // main thread: uploads finished work, at most `budget_ms` milliseconds
    void update(double budget_ms);
    size_t loaded_meshes() const { return meshes_.size(); }
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
    void upload_mesh(Done& d);
    void upload_texture(Done& d);

    std::shared_ptr<const GameData> game_;
    std::unordered_map<std::string, std::unique_ptr<GpuMesh>> meshes_;  // lower-case name
    std::unordered_map<std::string, GLuint> textures_;
    std::unordered_map<std::string, bool> requested_;  // lower-case name -> failed
    GLuint white_ = 0;
    bool s3tc_ = false;

    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Job> jobs_;
    std::deque<Done> done_;
    bool stop_ = false;
    std::thread thread_;
};

}  // namespace mk::gfx
