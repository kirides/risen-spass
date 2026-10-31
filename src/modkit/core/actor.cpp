#include "core/actor.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/catalog.h"
#include "core/skin.h"

namespace mk {

using p2m::Bytes;
using p2m::Error;

namespace {

enum : uint32_t { kMesh = 1, kStdMaterial = 3, kInfo = 7, kNodes = 11 };

uint32_t swap32(uint32_t v) { return (v >> 24) | ((v >> 8) & 0xff00u) | ((v << 8) & 0xff0000u) | (v << 24); }

struct Cur {
    const Bytes& b;
    size_t p;
    bool be = false;  // the actor was exported big endian (a few of the game's actors are)
    uint32_t u32() {
        const uint32_t v = p2m::rd32(b, p);
        p += 4;
        return be ? swap32(v) : v;
    }
    std::string str() {
        const uint32_t n = u32();
        if (n > 4096 || p + n > b.size()) throw Error("a string runs past the end of the actor");
        std::string s(b.data() + p, n);
        p += n;
        return s;
    }
};

}  // namespace

MeshData decode_actor(const Bytes& file, ActorInfo* info, ActorSkin* skin_out) {
    if (file.size() < 0x40 || file.compare(0, 8, "GR01MA02") != 0) throw Error("not a GR01/MA02 actor");
    const size_t xac = file.find("XAC ");
    if (xac == std::string::npos || xac + 8 > file.size()) throw Error("no EMotion FX actor inside");
    ActorInfo local;
    ActorInfo& ai = info ? *info : local;
    ai = ActorInfo{};
    struct Part {
        std::vector<float> pos, nrm, uv;  // per vertex 3, 3, 2
        struct Sub {
            std::vector<uint32_t> idx;
            uint32_t verts = 0, material = 0;
        };
        std::vector<Sub> subs;
        uint32_t vertex_count = 0;
        uint32_t node = 0, orig_count = 0;                 // the mesh's node, the number of original vertices
        std::vector<uint32_t> orig;                        // per vertex: its original vertex (layer type 5)
        std::vector<std::pair<float, uint16_t>> infl;      // the skinning influences: weight, node
        std::vector<std::pair<uint32_t, uint32_t>> table;  // per original vertex: first influence, count
    };
    std::vector<Part> parts;
    int last_part = -1;  // the visible part the latest mesh chunk made (-1: a collision mesh), the skinning chunk that follows belongs to it
    Skeleton skel;
    const bool be = file.at(xac + 6) != 0;  // "XAC ", version 1.0, then the endian byte
    auto rd = [&](size_t at) { return be ? swap32(p2m::rd32(file, at)) : p2m::rd32(file, at); };
    auto rdfl = [&](size_t at) {
        const uint32_t u = rd(at);
        float f;
        std::memcpy(&f, &u, 4);
        return f;
    };
    auto rd16 = [&](size_t at) {
        const uint16_t v = p2m::rd16(file, at);
        return be ? static_cast<uint16_t>((v >> 8) | (v << 8)) : v;
    };
    size_t p = xac + 8;
    bool lods_started = false;
    while (p + 12 <= file.size() && !lods_started) {
        const uint32_t id = rd(p), size = rd(p + 4);
        if (id > 17 || p + 12 + size > file.size()) break;  // the data behind the chunks (the physics stream of the collision mesh follows them)
        Cur c{file, p + 12, be};
        size_t next = p + 12 + size;
        if (id == kInfo) {
            // numLODs, trajectory node, motion extraction node, exporter version (4 bytes), then four strings: source application, file, date, actor name
            c.p += 4 * 3 + 4;
            c.str();
            c.str();
            c.str();
            ai.name = c.str();
        } else if (id == kNodes) {
            // the names only (the editor draws the bind pose): a node is 156 bytes of transform, hierarchy and bounding box, then its name; another version of the
            // node record just leaves the list empty, the meshes do not need it
            try {
                const uint32_t n = c.u32();
                c.u32();  // root nodes
                if (n <= 4096)
                    for (uint32_t i = 0; i < n; ++i) {
                        // rotation (4 floats), scale rotation (4), position (3), scale (3), three -1.0, two 0xffffffff, then the parent index at +76
                        Xform x;
                        for (size_t k = 0; k < 4; ++k) x.rot[k] = rdfl(c.p + k * 4);
                        for (size_t k = 0; k < 3; ++k) x.pos[k] = rdfl(c.p + 32 + k * 4);
                        for (size_t k = 0; k < 3; ++k) x.scale[k] = rdfl(c.p + 44 + k * 4);
                        const uint32_t par = rd(c.p + 76);
                        c.p += 156;
                        ai.nodes.push_back(c.str());
                        skel.names.push_back(ai.nodes.back());
                        skel.parent.push_back(par < n ? static_cast<int>(par) : -1);
                        skel.bind.push_back(x);
                    }
            } catch (const Error&) {
                ai.nodes.clear();
                skel = Skeleton{};
            }
        } else if (id == kStdMaterial) {
            // 4 colours (64 bytes), four floats, then double sided, wireframe, transparency type and the number of layers, then the name; the layers follow
            // the chunk (28 bytes and a name each) and are not part of its size
            const uint8_t layers = static_cast<uint8_t>(file.at(c.p + 83));
            c.p += 84;
            c.str();  // the material's name
            std::string diffuse;
            size_t lp = next;
            for (uint8_t k = 0; k < layers; ++k) {
                if (lp + 32 > file.size()) throw Error("a material layer runs past the end of the actor");
                const uint8_t map_type = static_cast<uint8_t>(file[lp + 26]);
                Cur lc{file, lp + 28, be};
                const std::string tex = lc.str();
                if (map_type == 2 && diffuse.empty()) diffuse = tex;  // 2 = diffuse
                lp = lc.p;
            }
            ai.diffuse_textures.push_back(diffuse);
            next = lp;
        } else if (id == kMesh) {
            const uint32_t node = c.u32();
            const uint32_t orig_count = c.u32();  // original vertex count
            const uint32_t total_verts = c.u32();
            c.u32();  // total indices
            const uint32_t nsubs = c.u32(), nlayers = c.u32();
            const bool collision = file.at(c.p) != 0;
            c.p += 4;
            if (total_verts > 2000000 || nsubs > 1024 || nlayers > 32) throw Error("implausible mesh");
            Part part;
            part.vertex_count = total_verts;
            part.node = node;
            part.orig_count = orig_count;
            for (uint32_t L = 0; L < nlayers; ++L) {
                const uint32_t type = c.u32(), attr = c.u32();
                c.p += 4;  // enable deformations, is scale, 2 pad bytes
                const size_t bytes = static_cast<size_t>(attr) * total_verts;
                if (c.p + bytes > file.size()) throw Error("a vertex layer runs past the end of the actor");
                auto floats = [&](std::vector<float>& out, size_t n_per) {
                    out.resize(static_cast<size_t>(total_verts) * n_per);
                    std::memcpy(out.data(), file.data() + c.p, std::min(bytes, out.size() * 4));
                    if (be)
                        for (float& f : out) {
                            uint32_t u;
                            std::memcpy(&u, &f, 4);
                            u = swap32(u);
                            std::memcpy(&f, &u, 4);
                        }
                };
                if (type == 0 && attr == 12) floats(part.pos, 3);        // positions
                else if (type == 1 && attr == 12) floats(part.nrm, 3);   // normals
                else if (type == 3 && attr == 8 && part.uv.empty()) floats(part.uv, 2);  // the first UV set
                else if (type == 5 && attr == 4) {  // the original vertex number of each vertex (the skinning is per original vertex)
                    part.orig.resize(total_verts);
                    for (uint32_t v = 0; v < total_verts; ++v) part.orig[v] = rd(c.p + static_cast<size_t>(v) * 4);
                }
                c.p += bytes;
            }
            for (uint32_t s = 0; s < nsubs; ++s) {
                const uint32_t ni = c.u32(), nv = c.u32(), mat = c.u32(), nb = c.u32();
                if (ni > 6000000 || ni % 3 != 0 || c.p + static_cast<size_t>(ni) * 4 + static_cast<size_t>(nb) * 4 > file.size()) throw Error("a sub-mesh runs past the end of the actor");
                Part::Sub sub;
                sub.verts = nv;
                sub.material = mat;
                sub.idx.resize(ni);
                std::memcpy(sub.idx.data(), file.data() + c.p, static_cast<size_t>(ni) * 4);
                if (be)
                    for (uint32_t& i : sub.idx) i = swap32(i);
                c.p += static_cast<size_t>(ni) * 4 + static_cast<size_t>(nb) * 4;
                part.subs.push_back(std::move(sub));
            }
            last_part = -1;
            if (collision) ++ai.collision_meshes;
            else if (!part.pos.empty()) {
                ++ai.meshes;
                parts.push_back(std::move(part));
                last_part = static_cast<int>(parts.size()) - 1;
            }
        } else if (id == 2 && last_part >= 0) {  // SKINNINGINFO v3: node, bone count, influence count, collision flag; the influences; a (start, count) per original vertex
            Part& part = parts[static_cast<size_t>(last_part)];
            try {
                c.u32();
                c.u32();
                const uint32_t total = c.u32();
                c.p += 4;
                if (total > 4000000 || c.p + static_cast<size_t>(total) * 8 + static_cast<size_t>(part.orig_count) * 8 > p + 12 + size) throw Error("the skinning runs past its chunk");
                for (uint32_t i = 0; i < total; ++i) part.infl.emplace_back(rdfl(c.p + static_cast<size_t>(i) * 8), rd16(c.p + static_cast<size_t>(i) * 8 + 4));
                c.p += static_cast<size_t>(total) * 8;
                for (uint32_t i = 0; i < part.orig_count; ++i) part.table.emplace_back(rd(c.p + static_cast<size_t>(i) * 8), rd(c.p + static_cast<size_t>(i) * 8 + 4));
            } catch (const Error&) {
                part.infl.clear();
                part.table.clear();
            }
        } else if (id == 8) {  // the levels of detail follow: everything after this is a lower level
            lods_started = true;
        }
        p = next;
    }
    if (parts.empty()) throw Error("the actor has no visible mesh");
    MeshData m;
    m.stride = 32;
    m.pos = 0;
    m.normal = 12;
    m.uv = 24;
    m.index32 = true;
    // one sub-mesh per material (the parts' sub-meshes of the same material are joined: indices of a sub-mesh are relative to its own vertices)
    std::vector<std::vector<uint32_t>> by_material;
    std::vector<std::string> material_names;
    std::string verts, indices;
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    uint32_t vbase = 0;
    struct Draw {
        uint32_t material;
        std::vector<uint32_t> idx;
    };
    std::vector<Draw> draws;
    ActorSkin skin;
    if (skin_out && !skel.empty()) skin.skeleton = skel;
    for (const Part& part : parts) {
        const bool has_n = part.nrm.size() == part.pos.size(), has_uv = part.uv.size() == part.vertex_count * 2;
        if (skin_out && !skel.empty())
            for (uint32_t v = 0; v < part.vertex_count; ++v) {
                // the (up to) four strongest influences of the vertex's original vertex; no skinning data: the mesh's own node carries it rigidly
                std::array<uint16_t, 4> b{};
                std::array<float, 4> w{};
                const uint32_t o = v < part.orig.size() ? part.orig[v] : v;
                if (o < part.table.size()) {
                    const auto [first, count] = part.table[o];
                    for (uint32_t k = 0; k < count && first + k < part.infl.size(); ++k) {
                        const auto& in = part.infl[first + k];
                        if (in.second >= skel.names.size() || in.first <= 0) continue;
                        size_t slot = 4;
                        for (size_t j = 0; j < 4; ++j)
                            if (w[j] < in.first && (slot == 4 || w[j] < w[slot])) slot = j;
                        if (slot < 4) {
                            b[slot] = in.second;
                            w[slot] = in.first;
                        }
                    }
                }
                float sum = w[0] + w[1] + w[2] + w[3];
                if (sum <= 0) {
                    b[0] = static_cast<uint16_t>(std::min<size_t>(part.node, skel.names.size() - 1));
                    w[0] = 1;
                    sum = 1;
                }
                for (float& x : w) x /= sum;
                skin.bone.push_back(b);
                skin.weight.push_back(w);
            }
        for (uint32_t v = 0; v < part.vertex_count; ++v) {
            float rec[8] = {part.pos[v * 3], part.pos[v * 3 + 1], part.pos[v * 3 + 2], 0, 1, 0, 0, 0};
            if (has_n) std::memcpy(rec + 3, &part.nrm[v * 3], 12);
            if (has_uv) std::memcpy(rec + 6, &part.uv[v * 2], 8);
            verts.append(reinterpret_cast<const char*>(rec), 32);
            for (int k = 0; k < 3; ++k) {
                lo[k] = std::min(lo[k], rec[k]);
                hi[k] = std::max(hi[k], rec[k]);
            }
        }
        uint32_t sub_start = vbase;
        for (const auto& s : part.subs) {
            Draw d;
            d.material = s.material;
            for (uint32_t i : s.idx) d.idx.push_back(sub_start + std::min(i, s.verts ? s.verts - 1 : 0));
            sub_start += s.verts;
            draws.push_back(std::move(d));
        }
        vbase += part.vertex_count;
    }
    std::stable_sort(draws.begin(), draws.end(), [](const Draw& a, const Draw& b) { return a.material < b.material; });
    uint32_t first_index = 0;
    for (size_t i = 0; i < draws.size();) {
        SubMesh s;
        const uint32_t mat = draws[i].material;
        s.first_index = first_index;
        for (; i < draws.size() && draws[i].material == mat; ++i) {
            indices.append(reinterpret_cast<const char*>(draws[i].idx.data()), draws[i].idx.size() * 4);
            first_index += static_cast<uint32_t>(draws[i].idx.size());
        }
        s.index_count = first_index - s.first_index;
        s.first_vertex = 0;
        s.vertex_count = vbase;
        if (mat < ai.diffuse_textures.size()) s.texture = ai.diffuse_textures[mat];
        m.submeshes.push_back(std::move(s));
    }
    if (skin_out) *skin_out = std::move(skin);
    m.vertex_count = vbase;
    m.index_count = first_index;
    m.vertices = std::move(verts);
    m.indices = std::move(indices);
    for (int k = 0; k < 3; ++k) {
        m.box_min[static_cast<size_t>(k)] = lo[k];
        m.box_max[static_cast<size_t>(k)] = hi[k];
    }
    return m;
}

MeshData merge_meshes(const std::vector<MeshData>& parts) {
    MeshData m;
    m.stride = 32;
    m.pos = 0;
    m.normal = 12;
    m.uv = 24;
    m.index32 = true;
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (const MeshData& p : parts) {
        if (p.stride != 32 || !p.index32) continue;
        const uint32_t vbase = m.vertex_count, ibase = m.index_count;
        m.vertices += p.vertices;
        const auto* idx = reinterpret_cast<const uint32_t*>(p.indices.data());
        for (uint32_t i = 0; i < p.index_count; ++i) {
            const uint32_t v = idx[i] + vbase;
            m.indices.append(reinterpret_cast<const char*>(&v), 4);
        }
        for (SubMesh s : p.submeshes) {
            s.first_index += ibase;
            s.first_vertex = 0;
            s.vertex_count = vbase + p.vertex_count;
            m.submeshes.push_back(std::move(s));
        }
        m.vertex_count += p.vertex_count;
        m.index_count += p.index_count;
        for (size_t k = 0; k < 3; ++k) {
            lo[k] = std::min(lo[k], p.box_min[k]);
            hi[k] = std::max(hi[k], p.box_max[k]);
        }
    }
    for (size_t k = 0; k < 3; ++k) {
        m.box_min[k] = lo[k];
        m.box_max[k] = hi[k];
    }
    return m;
}

MeshData decode_character(const std::vector<std::string>& actors, const std::function<Bytes(const std::string&)>& read, ActorSkin* skin) {
    std::vector<MeshData> parts;
    std::vector<ActorSkin> skins;
    for (const std::string& a : actors) {
        try {
            ActorSkin sk;
            MeshData m = decode_actor(read(a), nullptr, &sk);
            parts.push_back(std::move(m));
            skins.push_back(std::move(sk));
        } catch (const std::exception&) {  // a skeleton or a head that does not decode is left out
        }
    }
    if (parts.empty()) throw Error("no part of the character has a mesh");
    if (skin) *skin = parts.size() == 1 ? std::move(skins[0]) : merge_skins(skins);
    return parts.size() == 1 ? std::move(parts[0]) : merge_meshes(parts);
}

std::vector<std::string> actor_parts(const GameData& g, const TemplateInfo& t) {
    std::vector<std::string> out;
    auto actor_of = [&](const TemplateInfo* x, const char* key) -> std::string {
        const p2m::Val* v = x ? x->get(key) : nullptr;
        return v && v->is_str() && !v->s.empty() ? compiled_name(v->s) : std::string();
    };
    if (std::string own = actor_of(&t, "eCAnimation_PS.ResourceFilePath"); !own.empty()) out.push_back(own);
    // candidates in order of how surely they are worn
    std::vector<std::string> items;
    for (auto& w : t.worn) items.push_back(w.second);
    for (auto& s : t.inventory)
        if (s.resolved && s.equip_slot != 0) items.push_back(s.item);
    for (auto& s : t.inventory)
        if (s.resolved && s.equip_slot == 0) items.push_back(s.item);
    // monsters (ogres, skeletons) name their parts as plain `Template` references of the inventory set, which the stack reader does not see
    for (size_t i = 0; i < t.props.keys.size(); ++i)
        if (t.props.keys[i].rfind("gCInventory_PS.Template", 0) == 0 && t.props.vals[i].is_str()) {  // "Template", "Template#2", ...
            const std::string& v = t.props.vals[i].s;
            items.push_back(!v.empty() && v[0] == '@' ? g.template_by_guid(v) : v);
        }
    std::string body, head;
    for (const std::string& name : items) {
        const std::string a = actor_of(g.tpl(name), "eCBodyPart_PS.ResourceFilePath");
        if (a.empty()) continue;
        const bool is_head = p2m::contains(p2m::lower(a), "_head");
        std::string& slot = is_head ? head : body;
        if (slot.empty()) slot = a;
    }
    // The head of a world NPC is not in its template (RE.md section 54). A body with a "<body>_Head" actor of its own (the ogres) gets that one; a human gets a
    // stand-in by gender, so that the figure does not stand there headless. The face of the real NPC is not known.
    if (head.empty() && !body.empty()) {
        const size_t dot = body.rfind("._xmac");
        const std::string own_head = dot == std::string::npos ? std::string() : body.substr(0, dot) + "_Head._xmac";
        if (!own_head.empty() && g.exists(own_head)) head = own_head;
    }
    if (head.empty() && !out.empty() && p2m::contains(p2m::lower(out[0]), "hero_skeleton")) {
        const p2m::Val* gender = t.get("gCNPC_PS.Gender");
        const bool woman = gender && gender->is_int() && gender->i == 1;
        head = woman ? "Ani_Hero_Head_Woman_01._xmac" : "Ani_Hero_Head_Normalo_01._xmac";
    }
    if (!body.empty()) out.push_back(body);
    if (!head.empty()) out.push_back(head);
    return out;
}

std::string compose_name(const std::vector<std::string>& actors) {
    if (actors.size() == 1) return actors[0];
    std::string s = "compose:";
    for (size_t i = 0; i < actors.size(); ++i) s += (i ? "|" : "") + actors[i];
    return s;
}

bool is_composed(const std::string& name) { return name.rfind("compose:", 0) == 0; }

std::vector<std::string> composed_parts(const std::string& name) {
    std::vector<std::string> out;
    size_t from = 8;
    while (from <= name.size()) {
        const size_t bar = name.find('|', from);
        out.push_back(name.substr(from, bar == std::string::npos ? std::string::npos : bar - from));
        if (bar == std::string::npos) break;
        from = bar + 1;
    }
    return out;
}

}  // namespace mk
