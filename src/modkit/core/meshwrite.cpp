#include "core/meshwrite.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "pack2mod/util.h"

namespace mk {

namespace {

using p2m::Bytes;

void put16(Bytes& b, uint16_t v) { p2m::wr16(b, v); }
void put32(Bytes& b, uint32_t v) { p2m::wr32(b, v); }
void putf(Bytes& b, float v) { p2m::wrf(b, v); }
void put_str(Bytes& b, const std::string& s) {
    put16(b, static_cast<uint16_t>(s.size()));
    b += s;
}
// a property of the stream: name, type, 0x1e, size, payload
Bytes prop(const std::string& name, const std::string& type, const Bytes& payload) {
    Bytes b;
    put_str(b, name);
    put_str(b, type);
    put16(b, 0x1e);
    put32(b, static_cast<uint32_t>(payload.size()));
    b += payload;
    return b;
}
// an object body: v0, size, v1, number of properties, properties, extra
Bytes body(const Bytes& props, uint32_t nprops, const Bytes& extra = {}) {
    Bytes rest;
    put16(rest, 0xc9);
    put32(rest, nprops);
    rest += props;
    rest += extra;
    Bytes b;
    put16(b, 0xc9);
    put32(b, static_cast<uint32_t>(rest.size()));
    return b + rest;
}
Bytes box_bytes(const float lo[3], const float hi[3]) {
    Bytes b;
    for (int i = 0; i < 3; ++i) putf(b, lo[i]);
    for (int i = 0; i < 3; ++i) putf(b, hi[i]);
    return b;
}

constexpr uint16_t kStride = 52;
const char kEnd[9] = "\xff\x00\x00\x00\x11\x00\x00\x00";  // D3DDECL_END()

Bytes vertex_declaration() {
    struct E {
        uint16_t stream, offset;
        uint8_t type, method, usage, index;
    };
    static const E elems[] = {{0, 0, 2, 0, 0, 0},   // POSITION  FLOAT3
                              {0, 12, 2, 0, 3, 0},  // NORMAL    FLOAT3
                              {0, 24, 2, 0, 6, 0},  // TANGENT   FLOAT3
                              {0, 36, 4, 0, 10, 0},  // COLOR 0   D3DCOLOR
                              {0, 40, 4, 0, 10, 1},  // COLOR 1   D3DCOLOR
                              {0, 44, 1, 0, 5, 0},   // TEXCOORD 0 FLOAT2
                              {3, 0, 0, 0, 5, 4}};   // the placeholder the shipped meshes carry for their (unused) stream 3
    Bytes b;
    for (const E& e : elems) {
        put16(b, e.stream);
        put16(b, e.offset);
        b.push_back(static_cast<char>(e.type));
        b.push_back(static_cast<char>(e.method));
        b.push_back(static_cast<char>(e.usage));
        b.push_back(static_cast<char>(e.index));
    }
    return b;
}

}  // namespace

std::string build_xmsh_raw(const std::vector<RawPart>& parts, uint64_t filetime, const std::vector<std::string>* node_bytes) {
    // ---- the property stream ----
    Bytes elements;
    uint32_t first_index = 0, first_vertex = 0;
    float all_lo[3] = {1e30f, 1e30f, 1e30f}, all_hi[3] = {-1e30f, -1e30f, -1e30f};
    size_t part_no = 0;
    for (const RawPart& p : parts) {
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        for (const RawVertex& v : p.vertices)
            for (int k = 0; k < 3; ++k) {
                lo[k] = std::min(lo[k], v.pos[k]);
                hi[k] = std::max(hi[k], v.pos[k]);
            }
        for (int k = 0; k < 3; ++k) {
            all_lo[k] = std::min(all_lo[k], lo[k]);
            all_hi[k] = std::max(all_hi[k], hi[k]);
        }
        Bytes ms;
        put_str(ms, p.material);
        Bytes props = prop("MaterialName", "bCString", ms);
        const Bytes ext = box_bytes(lo, hi);
        props += prop("Extends", "bCBox", ext);
        Bytes v;
        put32(v, first_index);
        props += prop("FirstIndex", "long", v);
        v.clear();
        put32(v, static_cast<uint32_t>(p.indices.size()));
        props += prop("IndexCount", "long", v);
        v.clear();
        put32(v, first_vertex);
        props += prop("FirstVertex", "long", v);
        v.clear();
        put32(v, static_cast<uint32_t>(p.vertices.size()));
        props += prop("VertexCount", "long", v);
        // the bounding tree: by default one node = the part's box, first index 0, one node
        Bytes tree;
        uint32_t nodes = 1;
        if (node_bytes && part_no < node_bytes->size() && !(*node_bytes)[part_no].empty()) {
            tree = (*node_bytes)[part_no];
            nodes = static_cast<uint32_t>(tree.size() / 32);
        } else {
            tree = ext;
            put32(tree, 0);
            put32(tree, 1);
        }
        Bytes extra;
        put16(extra, 0xc9);
        put32(extra, nodes);
        extra += tree;
        elements += body(props, 6, extra);
        first_index += static_cast<uint32_t>(p.indices.size());
        first_vertex += static_cast<uint32_t>(p.vertices.size());
        ++part_no;
    }
    Bytes sub_payload;
    sub_payload.push_back(1);
    put32(sub_payload, static_cast<uint32_t>(parts.size()));
    sub_payload += elements;
    Bytes lm;
    put16(lm, 0);
    Bytes props = prop("SubMeshes", "bTObjArray<class eCSubMesh>", sub_payload);
    props += prop("LightmapName", "bCString", lm);
    props += prop("Boundary", "bCBox", box_bytes(all_lo, all_hi));
    Bytes stream;
    put16(stream, 1);
    stream.push_back(1);
    put16(stream, 1);
    stream.push_back(1);
    put_str(stream, "eCMeshResource2");
    put16(stream, 1);
    stream.push_back(0);
    put16(stream, 0xc9);
    Bytes tail2;
    put16(tail2, 0xc9);
    stream += body(props, 3, tail2);

    // ---- the payload: declaration, stream table, vertices, indices ----
    uint32_t nverts = 0, nidx = 0;
    for (const RawPart& p : parts) {
        nverts += static_cast<uint32_t>(p.vertices.size());
        nidx += static_cast<uint32_t>(p.indices.size());
    }
    Bytes pl = vertex_declaration();
    pl.append(kEnd, 8);
    pl.append(kEnd, 8);
    pl.append(56, '\0');
    put32(pl, nverts * kStride);
    put32(pl, 0);
    put32(pl, 0);
    put32(pl, 1);
    put32(pl, kStride);
    pl.append(40, '\0');
    put32(pl, nidx * 4);
    put32(pl, 0);
    put32(pl, 102);  // 32-bit indices
    put32(pl, 1);
    uint32_t base = 0;
    for (const RawPart& p : parts) {
        for (const RawVertex& v : p.vertices) {
            for (float f : v.pos) putf(pl, f);
            for (float f : v.normal) putf(pl, f);
            for (float f : v.tangent) putf(pl, f);
            pl.append(reinterpret_cast<const char*>(v.color0), 4);
            pl.append(reinterpret_cast<const char*>(v.color1), 4);
            putf(pl, v.uv[0]);
            putf(pl, v.uv[1]);
        }
        (void)base;
    }
    // the indices of the file are absolute: a part's own come after the vertices of the parts before it
    base = 0;
    for (const RawPart& p : parts) {
        for (uint32_t i : p.indices) put32(pl, i + base);
        base += static_cast<uint32_t>(p.vertices.size());
    }
    pl.push_back(1);

    Bytes out = "GR01MS02";
    put32(out, 0x28);
    put32(out, static_cast<uint32_t>(stream.size()));
    put32(out, 0x28 + static_cast<uint32_t>(stream.size()));
    put32(out, static_cast<uint32_t>(pl.size()));
    p2m::wr64(out, filetime ? filetime : 0x01c95546abdf2750ull);
    p2m::wr64(out, 0);
    return out + stream + pl;
}

std::string build_xmsh(const MeshBuild& m, std::string& error) {
    error.clear();
    if (m.parts.empty()) {
        error = "the model has no mesh";
        return {};
    }
    std::vector<RawPart> raw;
    for (size_t pi = 0; pi < m.parts.size(); ++pi) {
        const MeshPart& p = m.parts[pi];
        const std::string what = m.parts.size() > 1 ? "part " + std::to_string(pi + 1) + ": " : std::string();
        const size_t n = p.positions.size();
        if (n == 0 || p.indices.size() < 3) {
            error = what + "no triangles";
            return {};
        }
        if (p.indices.size() % 3 != 0) {
            error = what + "the index list is no whole number of triangles";
            return {};
        }
        if (!p.normals.empty() && p.normals.size() != n) {
            error = what + "the normals do not match the positions";
            return {};
        }
        if (!p.uvs.empty() && p.uvs.size() != n) {
            error = what + "the texture coordinates do not match the positions";
            return {};
        }
        for (uint32_t i : p.indices)
            if (i >= n) {
                error = what + "a triangle names a vertex that does not exist";
                return {};
            }
        if (p.material.empty()) {
            error = what + "no material";
            return {};
        }
        RawPart rp;
        rp.material = p.material;
        rp.indices = p.indices;
        // normals: the area-weighted mean of the faces' normals when the model has none
        std::vector<std::array<float, 3>> normals = p.normals;
        if (normals.empty()) {
            normals.assign(n, {0, 0, 0});
            for (size_t t = 0; t + 2 < p.indices.size(); t += 3) {
                const auto &a = p.positions[p.indices[t]], &b = p.positions[p.indices[t + 1]], &c = p.positions[p.indices[t + 2]];
                const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
                const float fn[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
                for (int k = 0; k < 3; ++k)
                    for (int j = 0; j < 3; ++j) normals[p.indices[t + static_cast<size_t>(k)]][static_cast<size_t>(j)] += fn[j];
            }
        }
        for (auto& v : normals) {
            const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            if (len > 1e-12f) {
                v[0] /= len;
                v[1] /= len;
                v[2] /= len;
            } else {
                v = {0, 1, 0};
            }
        }
        // tangents from the UV layout (the direction of increasing u), orthogonalised against the normal
        std::vector<std::array<float, 3>> tan(n, {0, 0, 0});
        if (!p.uvs.empty()) {
            for (size_t t = 0; t + 2 < p.indices.size(); t += 3) {
                const uint32_t i0 = p.indices[t], i1 = p.indices[t + 1], i2 = p.indices[t + 2];
                const auto &p0 = p.positions[i0], &p1 = p.positions[i1], &p2 = p.positions[i2];
                const auto &w0 = p.uvs[i0], &w1 = p.uvs[i1], &w2 = p.uvs[i2];
                const float x1 = p1[0] - p0[0], y1 = p1[1] - p0[1], z1 = p1[2] - p0[2], x2 = p2[0] - p0[0], y2 = p2[1] - p0[1], z2 = p2[2] - p0[2];
                const float s1 = w1[0] - w0[0], s2 = w2[0] - w0[0], t1 = w1[1] - w0[1], t2 = w2[1] - w0[1];
                const float det = s1 * t2 - s2 * t1;
                if (std::fabs(det) < 1e-20f) continue;
                const float r = 1.0f / det;
                const float tx = (t2 * x1 - t1 * x2) * r, ty = (t2 * y1 - t1 * y2) * r, tz = (t2 * z1 - t1 * z2) * r;
                for (uint32_t i : {i0, i1, i2}) {
                    tan[i][0] += tx;
                    tan[i][1] += ty;
                    tan[i][2] += tz;
                }
            }
        }
        for (size_t i = 0; i < n; ++i) {
            RawVertex v{};
            for (int k = 0; k < 3; ++k) {
                v.pos[k] = p.positions[i][static_cast<size_t>(k)];
                v.normal[k] = normals[i][static_cast<size_t>(k)];
            }
            // Gram-Schmidt against the normal; a vertex without a usable tangent gets any direction perpendicular to it
            float t[3] = {tan[i][0], tan[i][1], tan[i][2]};
            const float d = t[0] * v.normal[0] + t[1] * v.normal[1] + t[2] * v.normal[2];
            for (int k = 0; k < 3; ++k) t[k] -= v.normal[k] * d;
            float len = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
            if (len < 1e-8f) {
                const float ref[3] = {std::fabs(v.normal[0]) < 0.9f ? 1.0f : 0.0f, std::fabs(v.normal[0]) < 0.9f ? 0.0f : 1.0f, 0.0f};
                const float dd = ref[0] * v.normal[0] + ref[1] * v.normal[1] + ref[2] * v.normal[2];
                for (int k = 0; k < 3; ++k) t[k] = ref[k] - v.normal[k] * dd;
                len = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
            }
            for (int k = 0; k < 3; ++k) v.tangent[k] = len > 0 ? t[k] / len : 0.0f;
            // the colours the game's static meshes carry: colour 0 = (r 1, a 0), colour 1 = (a 1) in D3DCOLOR byte order (b, g, r, a)
            const uint8_t c0[4] = {0x00, 0x00, 0xff, 0x00}, c1[4] = {0x00, 0x00, 0x00, 0xff};
            std::memcpy(v.color0, c0, 4);
            std::memcpy(v.color1, c1, 4);
            v.uv[0] = p.uvs.empty() ? 0.0f : p.uvs[i][0];
            v.uv[1] = p.uvs.empty() ? 0.0f : 1.0f - p.uvs[i][1];  // OpenGL v is up, D3D v is down
            rp.vertices.push_back(v);
        }
        raw.push_back(std::move(rp));
    }
    return build_xmsh_raw(raw, m.filetime, nullptr);
}

}  // namespace mk
