#include "core/mesh_edit.h"

#include <cmath>
#include <cstring>

#include "core/mesh.h"
#include "core/meshwrite.h"

namespace mk {

namespace {

// the static vertex the writer knows: position 0, normal 12, tangent 24, colours 36 / 40, uv 44, stride 52, 32-bit or 16-bit indices
bool static_layout(const MeshData& m, std::string& why) {
    if (m.stride != 52 || m.pos != 0 || m.normal != 12 || m.uv != 44) {
        why = "the vertex format is not the game's static one (stride " + std::to_string(m.stride) + ")";
        return false;
    }
    return true;
}

uint32_t index_at(const MeshData& m, size_t i) {
    uint32_t v = 0;
    if (m.index32) std::memcpy(&v, m.indices.data() + i * 4, 4);
    else {
        uint16_t s = 0;
        std::memcpy(&s, m.indices.data() + i * 2, 2);
        v = s;
    }
    return v;
}

}  // namespace

MeshInfo mesh_info(const p2m::Bytes& file) {
    const MeshData m = decode_mesh(file);
    MeshInfo r;
    r.stride = m.stride;
    r.box_min = m.box_min;
    r.box_max = m.box_max;
    for (const SubMesh& s : m.submeshes) r.subs.push_back({s.material, s.vertex_count, s.index_count / 3});
    r.writable = static_layout(m, r.why_not);
    return r;
}

bool apply_mesh_edit(const p2m::Bytes& original, const MeshEdit& e, p2m::Bytes& out, std::string& error) {
    try {
        const MeshData m = decode_mesh(original);
        if (!static_layout(m, error)) return false;
        for (float s : e.scale)
            if (!(s > 0.0f) || !std::isfinite(s)) {
                error = "the scale must be a positive number";
                return false;
            }
        for (auto& [idx, mat] : e.materials)
            if (idx < 0 || static_cast<size_t>(idx) >= m.submeshes.size()) {
                error = "sub-mesh " + std::to_string(idx) + " does not exist (the mesh has " + std::to_string(m.submeshes.size()) + ")";
                return false;
            } else if (mat.empty()) {
                error = "sub-mesh " + std::to_string(idx) + ": the material name is empty";
                return false;
            }
        std::vector<RawPart> parts;
        for (size_t si = 0; si < m.submeshes.size(); ++si) {
            const SubMesh& s = m.submeshes[si];
            RawPart p;
            auto it = e.materials.find(static_cast<int>(si));
            p.material = it != e.materials.end() ? it->second : s.material;
            if (static_cast<uint64_t>(s.first_vertex) + s.vertex_count > m.vertex_count) {
                error = "sub-mesh " + std::to_string(si) + " reaches past the vertex data";
                return false;
            }
            for (uint32_t v = 0; v < s.vertex_count; ++v) {
                RawVertex rv;
                std::memcpy(&rv, m.vertices.data() + static_cast<size_t>(s.first_vertex + v) * 52, 52);
                for (int k = 0; k < 3; ++k) rv.pos[k] *= e.scale[static_cast<size_t>(k)];
                p.vertices.push_back(rv);
            }
            for (uint32_t i = 0; i < s.index_count; ++i) {
                const uint32_t idx = index_at(m, static_cast<size_t>(s.first_index) + i);
                if (idx < s.first_vertex || idx >= s.first_vertex + s.vertex_count) {
                    error = "sub-mesh " + std::to_string(si) + " uses vertices of another sub-mesh";
                    return false;
                }
                p.indices.push_back(idx - s.first_vertex);
            }
            parts.push_back(std::move(p));
        }
        out = build_xmsh_raw(parts, p2m::rd64(original, 0x18), nullptr);
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }
}

}  // namespace mk
