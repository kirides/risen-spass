#include "core/model_import.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace mk {

size_t ModelSource::triangles() const {
    size_t n = 0;
    for (auto& g : groups) n += g.indices.size() / 3;
    return n;
}

std::string base64_decode(const std::string& text) {
    static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, bits = -8;
    for (unsigned char c : text) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        const size_t p = chars.find(static_cast<char>(c));
        if (p == std::string::npos) return {};
        val = (val << 6) + static_cast<int>(p);
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<char>((val >> bits) & 0xff));
            bits -= 8;
        }
    }
    return out;
}

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool ends_with(const std::string& s, const std::string& suffix) { return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0; }

// ---- OBJ ----

ModelSource parse_obj(const std::string& text) {
    ModelSource src;
    std::vector<std::array<float, 3>> pos, nrm;
    std::vector<std::array<float, 2>> uv;
    struct Key {
        int v, t, n;
        bool operator==(const Key& o) const { return v == o.v && t == o.t && n == o.n; }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const { return (static_cast<size_t>(k.v) * 73856093u) ^ (static_cast<size_t>(k.t) * 19349663u) ^ (static_cast<size_t>(k.n) * 83492791u); }
    };
    std::unordered_map<std::string, size_t> group_of;
    std::vector<std::unordered_map<Key, uint32_t, KeyHash>> dedup;
    size_t current = SIZE_MAX;
    auto select = [&](const std::string& mat) {
        auto it = group_of.find(mat);
        if (it == group_of.end()) {
            it = group_of.emplace(mat, src.groups.size()).first;
            src.groups.emplace_back();
            src.groups.back().material = mat;
            dedup.emplace_back();
        }
        current = it->second;
    };
    std::istringstream in(text);
    std::string line;
    size_t line_no = 0;
    bool any_normal = true, any_uv = true;
    while (std::getline(in, line)) {
        ++line_no;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string tag;
        ls >> tag;
        if (tag == "v") {
            std::array<float, 3> p{};
            if (!(ls >> p[0] >> p[1] >> p[2])) {
                src.error = "line " + std::to_string(line_no) + ": a vertex needs three numbers";
                return src;
            }
            pos.push_back(p);
        } else if (tag == "vn") {
            std::array<float, 3> p{};
            ls >> p[0] >> p[1] >> p[2];
            nrm.push_back(p);
        } else if (tag == "vt") {
            std::array<float, 2> p{};
            ls >> p[0] >> p[1];
            uv.push_back(p);
        } else if (tag == "usemtl") {
            std::string name;
            std::getline(ls >> std::ws, name);
            select(name);
        } else if (tag == "f") {
            if (current == SIZE_MAX) select("");
            std::vector<uint32_t> poly;
            std::string tok;
            while (ls >> tok) {
                int ix[3] = {0, 0, 0};
                int field = 0;
                std::string cur;
                for (size_t i = 0; i <= tok.size(); ++i) {
                    if (i == tok.size() || tok[i] == '/') {
                        if (!cur.empty()) ix[field] = std::atoi(cur.c_str());
                        cur.clear();
                        ++field;
                        if (field > 2) break;
                    } else {
                        cur += tok[i];
                    }
                }
                auto resolve = [](int i, size_t n) { return i > 0 ? i - 1 : i < 0 ? static_cast<int>(n) + i : -1; };
                const Key k{resolve(ix[0], pos.size()), resolve(ix[1], uv.size()), resolve(ix[2], nrm.size())};
                if (k.v < 0 || k.v >= static_cast<int>(pos.size())) {
                    src.error = "line " + std::to_string(line_no) + ": a face names a vertex that does not exist";
                    return src;
                }
                ModelGroup& g = src.groups[current];
                auto& map = dedup[current];
                auto it = map.find(k);
                if (it == map.end()) {
                    it = map.emplace(k, static_cast<uint32_t>(g.positions.size())).first;
                    g.positions.push_back(pos[static_cast<size_t>(k.v)]);
                    if (k.n >= 0 && k.n < static_cast<int>(nrm.size())) g.normals.push_back(nrm[static_cast<size_t>(k.n)]);
                    else any_normal = false;
                    if (k.t >= 0 && k.t < static_cast<int>(uv.size())) g.uvs.push_back(uv[static_cast<size_t>(k.t)]);
                    else any_uv = false;
                }
                poly.push_back(it->second);
            }
            for (size_t i = 1; i + 1 < poly.size(); ++i) {  // a fan
                ModelGroup& g = src.groups[current];
                g.indices.push_back(poly[0]);
                g.indices.push_back(poly[i]);
                g.indices.push_back(poly[i + 1]);
            }
        }
    }
    // a group where some vertices have no normal / UV gets none (the arrays must match the positions)
    for (auto& g : src.groups) {
        if (g.normals.size() != g.positions.size()) g.normals.clear();
        if (g.uvs.size() != g.positions.size()) g.uvs.clear();
    }
    if (!any_normal) src.warnings.push_back("some faces have no normals: they are computed from the triangles");
    if (!any_uv) src.warnings.push_back("some faces have no texture coordinates: the material shows its first pixel there");
    src.groups.erase(std::remove_if(src.groups.begin(), src.groups.end(), [](const ModelGroup& g) { return g.indices.empty(); }), src.groups.end());
    if (src.groups.empty() && src.error.empty()) src.error = "the OBJ file has no faces";
    return src;
}

// ---- glTF ----

using nlohmann::json;

struct Mat4 {
    double m[16];  // column-major like glTF
    static Mat4 identity() {
        Mat4 r{};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1;
        return r;
    }
    Mat4 operator*(const Mat4& b) const {
        Mat4 r{};
        for (int c = 0; c < 4; ++c)
            for (int rr = 0; rr < 4; ++rr) {
                double s = 0;
                for (int k = 0; k < 4; ++k) s += m[k * 4 + rr] * b.m[c * 4 + k];
                r.m[c * 4 + rr] = s;
            }
        return r;
    }
};

Mat4 node_matrix(const json& n) {
    Mat4 r = Mat4::identity();
    if (n.contains("matrix") && n["matrix"].is_array() && n["matrix"].size() == 16) {
        for (int i = 0; i < 16; ++i) r.m[i] = n["matrix"][static_cast<size_t>(i)].get<double>();
        return r;
    }
    double t[3] = {0, 0, 0}, q[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1};
    if (n.contains("translation")) for (int i = 0; i < 3; ++i) t[i] = n["translation"][static_cast<size_t>(i)].get<double>();
    if (n.contains("rotation")) for (int i = 0; i < 4; ++i) q[i] = n["rotation"][static_cast<size_t>(i)].get<double>();
    if (n.contains("scale")) for (int i = 0; i < 3; ++i) s[i] = n["scale"][static_cast<size_t>(i)].get<double>();
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    const double rot[9] = {1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w),  // column 0
                           2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w),  // column 1
                           2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)};  // column 2
    for (int c = 0; c < 3; ++c)
        for (int rr = 0; rr < 3; ++rr) r.m[c * 4 + rr] = rot[c * 3 + rr] * s[c];
    r.m[12] = t[0];
    r.m[13] = t[1];
    r.m[14] = t[2];
    return r;
}

struct GltfData {
    json doc;
    std::vector<std::string> buffers;
    std::string error;

    // the bytes of accessor `index`, as `count` rows of `comps` numbers converted to double
    bool read_accessor(int index, int comps, std::vector<double>& out, size_t& count, bool integer_ok = true) {
        if (index < 0 || !doc.contains("accessors") || static_cast<size_t>(index) >= doc["accessors"].size()) return false;
        const json& a = doc["accessors"][static_cast<size_t>(index)];
        if (a.contains("sparse")) {
            error = "sparse accessors are not supported";
            return false;
        }
        count = a.value("count", 0u);
        const int ctype = a.value("componentType", 0);
        const std::string type = a.value("type", std::string());
        const int tcomps = type == "SCALAR" ? 1 : type == "VEC2" ? 2 : type == "VEC3" ? 3 : type == "VEC4" ? 4 : 0;
        if (tcomps != comps) return false;
        size_t csize = 0;
        switch (ctype) {
            case 5120: case 5121: csize = 1; break;
            case 5122: case 5123: csize = 2; break;
            case 5125: case 5126: csize = 4; break;
            default: return false;
        }
        if (ctype != 5126 && !integer_ok) return false;
        if (!a.contains("bufferView")) {  // all zeros
            out.assign(count * static_cast<size_t>(comps), 0.0);
            return true;
        }
        const json& bv = doc["bufferViews"][a["bufferView"].get<size_t>()];
        const size_t buf = bv.value("buffer", 0u);
        if (buf >= buffers.size()) return false;
        const size_t offset = bv.value("byteOffset", 0u) + a.value("byteOffset", 0u);
        const size_t stride = bv.value("byteStride", 0u) ? bv.value("byteStride", 0u) : csize * static_cast<size_t>(comps);
        const std::string& data = buffers[buf];
        const bool norm = a.value("normalized", false);
        out.resize(count * static_cast<size_t>(comps));
        for (size_t i = 0; i < count; ++i)
            for (int k = 0; k < comps; ++k) {
                const size_t at = offset + i * stride + static_cast<size_t>(k) * csize;
                if (at + csize > data.size()) return false;
                double v = 0;
                const char* p = data.data() + at;
                switch (ctype) {
                    case 5120: v = static_cast<int8_t>(*p); if (norm) v = std::max(v / 127.0, -1.0); break;
                    case 5121: v = static_cast<uint8_t>(*p); if (norm) v /= 255.0; break;
                    case 5122: { int16_t x; std::memcpy(&x, p, 2); v = x; if (norm) v = std::max(v / 32767.0, -1.0); break; }
                    case 5123: { uint16_t x; std::memcpy(&x, p, 2); v = x; if (norm) v /= 65535.0; break; }
                    case 5125: { uint32_t x; std::memcpy(&x, p, 4); v = x; break; }
                    case 5126: { float x; std::memcpy(&x, p, 4); v = x; break; }
                }
                out[i * static_cast<size_t>(comps) + static_cast<size_t>(k)] = v;
            }
        return true;
    }
};

ModelSource parse_gltf(const std::string& bytes, bool glb, const std::function<std::string(const std::string&)>& read_neighbour) {
    ModelSource src;
    GltfData g;
    std::string bin;
    try {
        if (glb) {
            if (bytes.size() < 20 || std::memcmp(bytes.data(), "glTF", 4) != 0) {
                src.error = "not a GLB file";
                return src;
            }
            size_t p = 12;
            std::string jsontext;
            while (p + 8 <= bytes.size()) {
                uint32_t len, type;
                std::memcpy(&len, bytes.data() + p, 4);
                std::memcpy(&type, bytes.data() + p + 4, 4);
                p += 8;
                if (p + len > bytes.size()) break;
                if (type == 0x4E4F534A) jsontext = bytes.substr(p, len);
                else if (type == 0x004E4942 && bin.empty()) bin = bytes.substr(p, len);
                p += len;
            }
            g.doc = json::parse(jsontext);
        } else {
            g.doc = json::parse(bytes);
        }
        if (g.doc.value("asset", json::object()).value("version", std::string("2")).rfind("2", 0) != 0) {
            src.error = "only glTF 2.0 is read";
            return src;
        }
        for (auto& b : g.doc.value("buffers", json::array())) {
            if (!b.contains("uri")) {
                g.buffers.push_back(bin);
                continue;
            }
            const std::string uri = b["uri"].get<std::string>();
            if (uri.rfind("data:", 0) == 0) {
                const size_t comma = uri.find(',');
                g.buffers.push_back(comma == std::string::npos ? std::string() : base64_decode(uri.substr(comma + 1)));
            } else if (read_neighbour) {
                g.buffers.push_back(read_neighbour(uri));
            } else {
                g.buffers.emplace_back();
            }
            if (g.buffers.back().empty()) {
                src.error = "the buffer '" + uri.substr(0, 60) + "' cannot be read (a .gltf file's .bin has to lie next to it)";
                return src;
            }
        }
        const json empty = json::array();
        const json& meshes = g.doc.contains("meshes") ? g.doc["meshes"] : empty;
        const json& nodes = g.doc.contains("nodes") ? g.doc["nodes"] : empty;
        // visit the scene's nodes with their world matrices; a file without nodes shows its meshes as they are
        std::vector<std::pair<int, Mat4>> todo;
        std::function<void(int, const Mat4&, int)> visit = [&](int ni, const Mat4& parent, int depth) {
            if (ni < 0 || static_cast<size_t>(ni) >= nodes.size() || depth > 64) return;
            const json& n = nodes[static_cast<size_t>(ni)];
            const Mat4 world = parent * node_matrix(n);
            if (n.contains("mesh")) todo.push_back({n["mesh"].get<int>(), world});
            for (auto& c : n.value("children", json::array())) visit(c.get<int>(), world, depth + 1);
        };
        if (!nodes.empty()) {
            const int scene = g.doc.value("scene", 0);
            if (g.doc.contains("scenes") && static_cast<size_t>(scene) < g.doc["scenes"].size()) {
                for (auto& n : g.doc["scenes"][static_cast<size_t>(scene)].value("nodes", json::array())) visit(n.get<int>(), Mat4::identity(), 0);
            } else {
                for (size_t i = 0; i < nodes.size(); ++i) visit(static_cast<int>(i), Mat4::identity(), 0);
            }
        } else {
            for (size_t i = 0; i < meshes.size(); ++i) todo.push_back({static_cast<int>(i), Mat4::identity()});
        }
        std::unordered_map<std::string, size_t> group_of;
        for (auto& [mi, world] : todo) {
            if (mi < 0 || static_cast<size_t>(mi) >= meshes.size()) continue;
            // the normal matrix: the inverse transpose of the upper 3x3 (cofactors / determinant), so non-uniform scales keep the normals right
            const double A00 = world.m[0], A10 = world.m[1], A20 = world.m[2], A01 = world.m[4], A11 = world.m[5], A21 = world.m[6], A02 = world.m[8], A12 = world.m[9], A22 = world.m[10];
            const double C[3][3] = {{A11 * A22 - A12 * A21, -(A10 * A22 - A12 * A20), A10 * A21 - A11 * A20},
                                    {-(A01 * A22 - A02 * A21), A00 * A22 - A02 * A20, -(A00 * A21 - A01 * A20)},
                                    {A01 * A12 - A02 * A11, -(A00 * A12 - A02 * A10), A00 * A11 - A01 * A10}};
            const double det = A00 * C[0][0] + A01 * C[0][1] + A02 * C[0][2];
            double nm[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
            if (std::fabs(det) > 1e-12)
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 3; ++c) nm[r][c] = C[r][c] / det;
            const bool flipped = det < 0;
            for (auto& prim : meshes[static_cast<size_t>(mi)].value("primitives", json::array())) {
                const int mode = prim.value("mode", 4);
                if (mode != 4) {
                    src.warnings.push_back("a primitive that is no list of triangles was skipped");
                    continue;
                }
                const json& attrs = prim.value("attributes", json::object());
                if (!attrs.contains("POSITION")) continue;
                std::vector<double> P, N, T, I;
                size_t np = 0, nn = 0, nt = 0, ni = 0;
                if (!g.read_accessor(attrs["POSITION"].get<int>(), 3, P, np, false)) {
                    src.error = g.error.empty() ? "a position accessor cannot be read" : g.error;
                    return src;
                }
                const bool has_n = attrs.contains("NORMAL") && g.read_accessor(attrs["NORMAL"].get<int>(), 3, N, nn, false) && nn == np;
                const bool has_t = attrs.contains("TEXCOORD_0") && g.read_accessor(attrs["TEXCOORD_0"].get<int>(), 2, T, nt) && nt == np;
                std::string mat = "";
                if (prim.contains("material") && g.doc.contains("materials")) {
                    const size_t mx = prim["material"].get<size_t>();
                    if (mx < g.doc["materials"].size()) mat = g.doc["materials"][mx].value("name", "material_" + std::to_string(mx));
                }
                auto it = group_of.find(mat);
                if (it == group_of.end()) {
                    it = group_of.emplace(mat, src.groups.size()).first;
                    src.groups.emplace_back();
                    src.groups.back().material = mat;
                }
                ModelGroup& grp = src.groups[it->second];
                // a primitive is its own vertex set: the group's arrays grow, the indices are shifted
                const uint32_t base = static_cast<uint32_t>(grp.positions.size());
                // arrays of a group must stay parallel: a primitive without normals / UVs makes the whole group go without
                const bool group_had_n = grp.positions.empty() ? true : !grp.normals.empty();
                const bool group_had_t = grp.positions.empty() ? true : !grp.uvs.empty();
                for (size_t i = 0; i < np; ++i) {
                    const double x = P[i * 3], y = P[i * 3 + 1], z = P[i * 3 + 2];
                    grp.positions.push_back({static_cast<float>(world.m[0] * x + world.m[4] * y + world.m[8] * z + world.m[12]),
                                             static_cast<float>(world.m[1] * x + world.m[5] * y + world.m[9] * z + world.m[13]),
                                             static_cast<float>(world.m[2] * x + world.m[6] * y + world.m[10] * z + world.m[14])});
                    if (has_n && group_had_n) {
                        const double nx = N[i * 3], ny = N[i * 3 + 1], nz = N[i * 3 + 2];
                        std::array<float, 3> v = {static_cast<float>(nm[0][0] * nx + nm[0][1] * ny + nm[0][2] * nz), static_cast<float>(nm[1][0] * nx + nm[1][1] * ny + nm[1][2] * nz),
                                                  static_cast<float>(nm[2][0] * nx + nm[2][1] * ny + nm[2][2] * nz)};
                        const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                        if (len > 1e-12f) v = {v[0] / len, v[1] / len, v[2] / len};
                        grp.normals.push_back(v);
                    }
                    if (has_t && group_had_t) grp.uvs.push_back({static_cast<float>(T[i * 2]), static_cast<float>(T[i * 2 + 1])});
                }
                if (!(has_n && group_had_n)) grp.normals.clear();
                if (!(has_t && group_had_t)) grp.uvs.clear();
                if (prim.contains("indices")) {
                    if (!g.read_accessor(prim["indices"].get<int>(), 1, I, ni)) {
                        src.error = "an index accessor cannot be read";
                        return src;
                    }
                } else {
                    ni = np;
                    I.resize(np);
                    for (size_t i = 0; i < np; ++i) I[i] = static_cast<double>(i);
                }
                for (size_t i = 0; i + 2 < ni; i += 3) {
                    const uint32_t a0 = static_cast<uint32_t>(I[i]), a1 = static_cast<uint32_t>(I[i + 1]), a2 = static_cast<uint32_t>(I[i + 2]);
                    if (a0 >= np || a1 >= np || a2 >= np) {
                        src.error = "an index is out of range";
                        return src;
                    }
                    grp.indices.push_back(base + a0);
                    grp.indices.push_back(base + (flipped ? a2 : a1));  // a mirroring node turns the triangle around
                    grp.indices.push_back(base + (flipped ? a1 : a2));
                }
            }
        }
    } catch (const std::exception& e) {
        src.error = std::string("the glTF file is damaged: ") + e.what();
        src.groups.clear();
        return src;
    }
    src.groups.erase(std::remove_if(src.groups.begin(), src.groups.end(), [](const ModelGroup& gr) { return gr.indices.empty(); }), src.groups.end());
    if (src.groups.empty() && src.error.empty()) src.error = "the glTF file has no triangles";
    return src;
}

}  // namespace

ModelSource parse_model(const std::string& bytes, const std::string& name, const std::function<std::string(const std::string&)>& read_neighbour) {
    const std::string n = lower(name);
    if (ends_with(n, ".obj")) return parse_obj(bytes);
    if (ends_with(n, ".gltf")) return parse_gltf(bytes, false, read_neighbour);
    if (ends_with(n, ".glb")) return parse_gltf(bytes, true, read_neighbour);
    ModelSource s;
    s.error = "unknown model format (OBJ, glTF and GLB are read)";
    return s;
}

MeshBuild model_to_mesh(const ModelSource& src, const ModelOptions& opt, std::string& error) {
    error.clear();
    MeshBuild out;
    std::map<std::string, size_t> part_of;
    for (const ModelGroup& g : src.groups) {
        auto it = opt.materials.find(g.material);
        const std::string mat = it != opt.materials.end() && !it->second.empty() ? it->second : opt.material;
        if (mat.empty()) {
            error = "choose a game material" + (g.material.empty() ? std::string() : " for '" + g.material + "'");
            return {};
        }
        auto pit = part_of.find(mat);
        if (pit == part_of.end()) {
            pit = part_of.emplace(mat, out.parts.size()).first;
            out.parts.emplace_back();
            out.parts.back().material = mat;
        }
        MeshPart& part = out.parts[pit->second];
        const uint32_t base = static_cast<uint32_t>(part.positions.size());
        // normals and UVs travel together or not at all
        const bool keep_n = !g.normals.empty() && (part.positions.empty() || !part.normals.empty());
        const bool keep_t = !g.uvs.empty() && (part.positions.empty() || !part.uvs.empty());
        if (!keep_n) part.normals.clear();
        if (!keep_t) part.uvs.clear();
        auto xf = [&](std::array<float, 3> v, bool is_position) {
            if (opt.z_up) v = {v[0], v[2], -v[1]};
            if (opt.mirror_z) v[2] = -v[2];
            if (is_position)
                for (float& f : v) f = static_cast<float>(f * opt.scale);
            return v;
        };
        for (size_t i = 0; i < g.positions.size(); ++i) {
            part.positions.push_back(xf(g.positions[i], true));
            if (keep_n) part.normals.push_back(xf(g.normals[i], false));
            if (keep_t) part.uvs.push_back(g.uvs[i]);
        }
        for (size_t i = 0; i + 2 < g.indices.size(); i += 3) {
            // OBJ and glTF wind their front faces counter-clockwise, D3D culls counter-clockwise faces (its front faces are clockwise). A mirror (z negated)
            // already reverses the winding; without the mirror the triangles are turned around here. (Found in the game: the first version had it the other way
            // round and the probe model rendered inside out.)
            part.indices.push_back(base + g.indices[i]);
            part.indices.push_back(base + (opt.mirror_z ? g.indices[i + 1] : g.indices[i + 2]));
            part.indices.push_back(base + (opt.mirror_z ? g.indices[i + 2] : g.indices[i + 1]));
        }
    }
    if (out.parts.empty()) error = "the model has no triangles";
    return out;
}

}  // namespace mk
