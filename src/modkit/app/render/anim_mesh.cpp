#include "app/render/anim_mesh.h"

#include <unordered_set>

namespace mk::gfx {

bool AnimMesh::load(const std::string& key, const std::vector<std::string>& actors, const std::function<p2m::Bytes(const std::string&)>& read, std::string& error) {
    clear();
    try {
        bind_ = decode_character(actors, read, &skin_);
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    if (skin_.empty() || bind_.stride != 32) {
        error = "the actor has no skeleton";
        return false;
    }
    glGenVertexArrays(1, &gpu_.vao);
    glGenBuffers(1, &gpu_.vbo);
    glGenBuffers(1, &gpu_.ibo);
    glBindVertexArray(gpu_.vao);
    glBindBuffer(GL_ARRAY_BUFFER, gpu_.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bind_.vertices.size()), bind_.vertices.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu_.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(bind_.indices.size()), bind_.indices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, reinterpret_cast<void*>(12));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, reinterpret_cast<void*>(24));
    glBindVertexArray(0);
    gpu_.index32 = true;
    for (const SubMesh& s : bind_.submeshes) {
        gpu_.subs.push_back({s.texture.empty() ? std::string() : s.texture + "._ximg", std::string(), s.first_index, s.index_count});
        gpu_.triangles += s.index_count / 3;
    }
    gpu_.box_min = {bind_.box_min[0], bind_.box_min[1], bind_.box_min[2]};
    gpu_.box_max = {bind_.box_max[0], bind_.box_max[1], bind_.box_max[2]};
    key_ = key;
    return true;
}

void AnimMesh::clear() {
    if (gpu_.vao) glDeleteVertexArrays(1, &gpu_.vao);
    if (gpu_.vbo) glDeleteBuffers(1, &gpu_.vbo);
    if (gpu_.ibo) glDeleteBuffers(1, &gpu_.ibo);
    gpu_ = GpuMesh{};
    bind_ = MeshData{};
    skin_ = ActorSkin{};
    key_.clear();
}

void AnimMesh::pose(const Motion* motion, float t) {
    if (!ready()) return;
    skin_vertices(skin_, skin_palette(skin_.skeleton, motion_pose(skin_.skeleton, motion, t)), bind_.vertices, tmp_);
    glBindBuffer(GL_ARRAY_BUFFER, gpu_.vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(tmp_.size()), tmp_.data());
}

size_t AnimMesh::matching_bones(const Motion& m) const {
    std::unordered_set<std::string> names(skin_.skeleton.names.begin(), skin_.skeleton.names.end());
    size_t n = 0;
    for (const MotionTrack& t : m.tracks) n += names.count(t.name);
    return n;
}

}  // namespace mk::gfx
