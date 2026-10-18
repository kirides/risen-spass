#include "core/actor.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/catalog.h"

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

MeshData decode_actor(const Bytes& file, ActorInfo* info) {
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
    };
    std::vector<Part> parts;
    const bool be = file.at(xac + 6) != 0;  // "XAC ", version 1.0, then the endian byte
    auto rd = [&](size_t at) { return be ? swap32(p2m::rd32(file, at)) : p2m::rd32(file, at); };
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
                        c.p += 156;
                        ai.nodes.push_back(c.str());
                    }
            } catch (const Error&) {
                ai.nodes.clear();
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
            (void)node;
            c.u32();  // original vertex count
            const uint32_t total_verts = c.u32();
            c.u32();  // total indices
            const uint32_t nsubs = c.u32(), nlayers = c.u32();
            const bool collision = file.at(c.p) != 0;
            c.p += 4;
            if (total_verts > 2000000 || nsubs > 1024 || nlayers > 32) throw Error("implausible mesh");
            Part part;
            part.vertex_count = total_verts;
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
            if (collision) ++ai.collision_meshes;
            else if (!part.pos.empty()) {
                ++ai.meshes;
                parts.push_back(std::move(part));
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
    for (const Part& part : parts) {
        const bool has_n = part.nrm.size() == part.pos.size(), has_uv = part.uv.size() == part.vertex_count * 2;
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
