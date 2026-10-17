#include "app/render/resources.h"

#include <algorithm>
#include <chrono>
#include <cstring>

#include "core/log.h"
#include "core/paths.h"

namespace mk::gfx {

namespace {

constexpr GLenum kDxt1 = 0x83F1, kDxt3 = 0x83F2, kDxt5 = 0x83F3;  // GL_EXT_texture_compression_s3tc
constexpr uint32_t kMaxTexture = 1024;  // larger images start at a smaller mip level (memory)

std::string key_of(const std::string& name) { return p2m::lower(name); }

}  // namespace

ResourceCache::ResourceCache(std::shared_ptr<const GameData> game) : game_(std::move(game)) {
    GLint n = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; ++i) {
        const char* e = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        if (e && std::strcmp(e, "GL_EXT_texture_compression_s3tc") == 0) s3tc_ = true;
    }
    if (!s3tc_) log_info("render: no S3TC on this GPU: textures are decompressed on the CPU");
    glGenTextures(1, &white_);
    glBindTexture(GL_TEXTURE_2D, white_);
    const uint32_t px = 0xffd0d0d0;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &px);
    thread_ = std::thread([this] { worker(); });
}

ResourceCache::~ResourceCache() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    for (auto& [k, m] : meshes_) {
        glDeleteBuffers(1, &m->vbo);
        glDeleteBuffers(1, &m->ibo);
        glDeleteVertexArrays(1, &m->vao);
    }
    for (auto& [k, t] : textures_)
        if (t != white_) glDeleteTextures(1, &t);
    glDeleteTextures(1, &white_);
}

void ResourceCache::set_loose(std::unordered_map<std::string, std::string> files) {
    std::vector<std::string> changed;
    {
        std::lock_guard<std::mutex> lock(mu_);
        for (auto& [k, v] : files)
            if (!loose_.count(k) || loose_[k] != v) changed.push_back(k);
        for (auto& [k, v] : loose_)
            if (!files.count(k)) changed.push_back(k);
        loose_ = std::move(files);
    }
    // a file that changed is read again the next time it is asked for
    for (auto& k : changed) {
        requested_.erase(k);
        if (auto it = meshes_.find(k); it != meshes_.end()) {
            glDeleteBuffers(1, &it->second->vbo);
            glDeleteBuffers(1, &it->second->ibo);
            glDeleteVertexArrays(1, &it->second->vao);
            meshes_.erase(it);
        }
        if (auto it = textures_.find(k); it != textures_.end()) {
            if (it->second != white_) glDeleteTextures(1, &it->second);
            textures_.erase(it);
        }
    }
}

p2m::Bytes ResourceCache::read(const std::string& name) {
    std::string path;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (auto it = loose_.find(key_of(name)); it != loose_.end()) path = it->second;
    }
    if (!path.empty()) return read_text(u8(path));
    return game_->read(name);
}

bool ResourceCache::exists(const std::string& name) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (loose_.count(key_of(name))) return true;
    }
    return game_->exists(name);
}

size_t ResourceCache::pending() const {
    std::lock_guard<std::mutex> lock(mu_);
    return jobs_.size() + done_.size();
}

const GpuMesh* ResourceCache::mesh(const std::string& name) {
    const std::string k = key_of(name);
    if (auto it = meshes_.find(k); it != meshes_.end()) return it->second.get();
    if (!requested_.count(k)) {
        requested_[k] = false;
        std::lock_guard<std::mutex> lock(mu_);
        jobs_.push_back({true, name});
        cv_.notify_one();
    }
    return nullptr;
}

bool ResourceCache::failed(const std::string& name) const {
    auto it = requested_.find(key_of(name));
    return it != requested_.end() && it->second;
}

GLuint ResourceCache::texture(const std::string& name) {
    if (name.empty()) return white_;
    const std::string k = key_of(name);
    if (auto it = textures_.find(k); it != textures_.end()) return it->second;
    if (!requested_.count(k)) {
        requested_[k] = false;
        std::lock_guard<std::mutex> lock(mu_);
        jobs_.push_back({false, name});
        cv_.notify_one();
    }
    return white_;
}

void ResourceCache::worker() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [this] { return stop_ || !jobs_.empty(); });
            if (stop_) return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        Done d;
        d.is_mesh = job.is_mesh;
        d.name = job.name;
        try {
            if (job.is_mesh) {
                d.mesh = std::make_unique<MeshData>(decode_mesh(read(job.name)));
                for (auto& s : d.mesh->submeshes) {
                    std::string tex;
                    try {
                        if (!s.material.empty() && exists(s.material)) tex = material_texture(read(s.material));
                    } catch (...) {
                    }
                    d.textures.push_back(tex);
                }
            } else {
                d.image = std::make_unique<ImageData>(decode_image(read(job.name)));
            }
        } catch (const std::exception& e) {
            d.error = e.what();
        }
        std::lock_guard<std::mutex> lock(mu_);
        done_.push_back(std::move(d));
    }
}

void ResourceCache::update(double budget_ms) {
    const auto t0 = std::chrono::steady_clock::now();
    for (;;) {
        Done d;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (done_.empty()) return;
            d = std::move(done_.front());
            done_.pop_front();
        }
        if (!d.error.empty()) {
            requested_[key_of(d.name)] = true;
            log_debug("render: " + d.name + ": " + d.error);
        } else if (d.is_mesh) {
            upload_mesh(d);
        } else {
            upload_texture(d);
        }
        if (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() > budget_ms) return;
    }
}

void ResourceCache::upload_mesh(Done& d) {
    const MeshData& m = *d.mesh;
    auto g = std::make_unique<GpuMesh>();
    glGenVertexArrays(1, &g->vao);
    glGenBuffers(1, &g->vbo);
    glGenBuffers(1, &g->ibo);
    glBindVertexArray(g->vao);
    glBindBuffer(GL_ARRAY_BUFFER, g->vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m.vertices.size()), m.vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(m.indices.size()), m.indices.data(), GL_STATIC_DRAW);
    const GLsizei stride = static_cast<GLsizei>(m.stride);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(static_cast<intptr_t>(m.pos)));
    if (m.normal >= 0) {
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(static_cast<intptr_t>(m.normal)));
    } else {
        glDisableVertexAttribArray(1);
        glVertexAttrib3f(1, 0, 1, 0);
    }
    if (m.uv >= 0) {
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(static_cast<intptr_t>(m.uv)));
    } else {
        glDisableVertexAttribArray(2);
        glVertexAttrib2f(2, 0, 0);
    }
    glBindVertexArray(0);
    g->index32 = m.index32;
    for (size_t i = 0; i < m.submeshes.size(); ++i) {
        const SubMesh& s = m.submeshes[i];
        g->subs.push_back({i < d.textures.size() ? d.textures[i] : std::string(), s.first_index, s.index_count});
        g->triangles += s.index_count / 3;
    }
    g->box_min = {m.box_min[0], m.box_min[1], m.box_min[2]};
    g->box_max = {m.box_max[0], m.box_max[1], m.box_max[2]};
    meshes_[key_of(d.name)] = std::move(g);
}

void ResourceCache::upload_texture(Done& d) {
    ImageData& img = *d.image;
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    // start at the level that fits kMaxTexture
    size_t first = 0;
    while (first + 1 < img.levels.size() && std::max(img.levels[first].width, img.levels[first].height) > kMaxTexture) ++first;
    int lvl = 0;
    if (img.compressed() && s3tc_) {
        const GLenum fmt = img.format == ImageData::DXT1 ? kDxt1 : img.format == ImageData::DXT3 ? kDxt3 : kDxt5;
        for (size_t i = first; i < img.levels.size(); ++i, ++lvl) {
            const auto& L = img.levels[i];
            glCompressedTexImage2D(GL_TEXTURE_2D, lvl, fmt, static_cast<GLsizei>(L.width), static_cast<GLsizei>(L.height), 0, static_cast<GLsizei>(L.size),
                                   img.data.data() + L.offset);
        }
    } else {
        for (size_t i = first; i < img.levels.size(); ++i, ++lvl) {
            uint32_t w = 0, h = 0;
            const std::string rgba = decompress_rgba(img, static_cast<uint32_t>(i), &w, &h);
            glTexImage2D(GL_TEXTURE_2D, lvl, GL_RGBA8, static_cast<GLsizei>(w), static_cast<GLsizei>(h), 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
            if (lvl >= 6) break;  // the smallest levels are cheap but the CPU decode is not: enough for the editor
        }
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, std::max(0, lvl - 1));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, lvl > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    textures_[key_of(d.name)] = t;
}

}  // namespace mk::gfx
