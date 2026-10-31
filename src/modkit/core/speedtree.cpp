#include "core/speedtree.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

#include "pack2mod/util.h"

namespace mk {

using p2m::Bytes;
using p2m::Error;

SpeedTreeInfo read_speedtree(const Bytes& file) {
    if (file.size() < 0x80 || file.compare(0, 8, "GR01ST01") != 0) throw Error("not a GR01/ST01 SpeedTree");
    SpeedTreeInfo info;
    // the property stream: ... "Boundary" "bCBox" u16 0x1e, u32 24, six floats
    const size_t bnd = file.find("bCBox");
    if (bnd == std::string::npos || bnd + 5 + 6 + 24 > file.size()) throw Error("the SpeedTree has no boundary");
    const size_t f = bnd + 5 + 6;
    for (size_t i = 0; i < 3; ++i) {
        info.box_min[i] = p2m::rdf(file, f + i * 4);
        info.box_max[i] = p2m::rdf(file, f + 12 + i * 4);
    }
    if (!(info.box_max[1] > info.box_min[1]) || info.box_max[1] - info.box_min[1] > 20000.0f) throw Error("implausible SpeedTree boundary");
    // the texture names are plain strings in the .spt part: "...Bark_05_Diffuse_01.tga"
    for (size_t p = file.find(".tga"); p != std::string::npos && info.textures.size() < 32; p = file.find(".tga", p + 4)) {
        size_t s = p;
        while (s > 0 && (std::isalnum(static_cast<unsigned char>(file[s - 1])) || file[s - 1] == '_' || file[s - 1] == '-')) --s;
        if (s < p) {
            const std::string name = file.substr(s, p + 4 - s);
            if (std::find(info.textures.begin(), info.textures.end(), name) == info.textures.end()) info.textures.push_back(name);
        }
    }
    return info;
}

std::string speedtree_file(const std::string& resource_path) {
    std::string n = resource_path;
    if (const size_t sl = n.find_last_of("/\\"); sl != std::string::npos) n = n.substr(sl + 1);
    if (const size_t dot = n.rfind('.'); dot != std::string::npos) n.resize(dot);
    return n.empty() ? std::string() : n + "._xspt";
}

namespace {

struct V3 {
    float x = 0, y = 0, z = 0;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3 norm(V3 a) {
    const float l = std::sqrt(dot(a, a));
    return l > 1e-6f ? a * (1.0f / l) : V3{0, 1, 0};
}

struct Builder {
    std::string verts;
    std::string idx;
    uint32_t nv = 0, ni = 0;
    uint32_t vertex(V3 p, V3 n, float u, float v) {
        const float rec[8] = {p.x, p.y, p.z, n.x, n.y, n.z, u, v};
        verts.append(reinterpret_cast<const char*>(rec), 32);
        return nv++;
    }
    void tri(uint32_t a, uint32_t b, uint32_t c) {
        idx.append(reinterpret_cast<const char*>(&a), 4);
        idx.append(reinterpret_cast<const char*>(&b), 4);
        idx.append(reinterpret_cast<const char*>(&c), 4);
        ni += 3;
    }
};

// a region of the shared composite image `ST_Composite_01_Diffuse_01` (u, v from the top left): the leaf clusters, needles, fronds and twigs the game's trees are made of
struct Rect {
    float u0, v0, u1, v1;
};
constexpr Rect kOak{0.255f, 0.272f, 0.495f, 0.368f}, kMaple{0.52f, 0.327f, 0.73f, 0.42f}, kWeeping{0.775f, 0.392f, 0.98f, 0.48f}, kTwigLeaves{0.02f, 0.388f, 0.22f, 0.488f},
    kBoxwood{0.815f, 0.16f, 0.995f, 0.315f}, kHolly{0.775f, 0.505f, 0.985f, 0.625f}, kAzalea{0.275f, 0.388f, 0.48f, 0.48f}, kJuniper{0.005f, 0.51f, 0.48f, 0.725f},
    kPine{0.0f, 0.76f, 0.49f, 0.98f}, kPalm{0.505f, 0.065f, 0.75f, 0.31f}, kFern{0.51f, 0.878f, 0.69f, 0.98f}, kFern2{0.705f, 0.878f, 0.87f, 0.98f},
    kLily{0.16f, 0.19f, 0.27f, 0.25f}, kDense{0.52f, 0.695f, 0.74f, 0.805f};

struct Rng {
    uint32_t s;
    float next() {  // 0..1
        s = s * 1664525u + 1013904223u;
        return static_cast<float>(s >> 8) / 16777216.0f;
    }
    float range(float a, float b) { return a + (b - a) * next(); }
};

// a tapered tube from `a` to `b`, `sides` around, the bark texture wrapping once around and every 90 units along
void tube(Builder& b, V3 a, V3 c, float r0, float r1, int sides) {
    const V3 axis = norm(c - a);
    const V3 side = norm(std::fabs(axis.y) > 0.95f ? cross(axis, V3{1, 0, 0}) : cross(axis, V3{0, 1, 0}));
    const V3 up2 = cross(axis, side);
    const float len = std::sqrt(dot(c - a, c - a));
    const uint32_t base = b.nv;
    for (int ring = 0; ring < 2; ++ring)
        for (int s = 0; s <= sides; ++s) {
            const float ang = 6.2831853f * static_cast<float>(s) / static_cast<float>(sides);
            const V3 n = side * std::cos(ang) + up2 * std::sin(ang);
            b.vertex((ring ? c : a) + n * (ring ? r1 : r0), n, static_cast<float>(s) / static_cast<float>(sides) * 2.0f, ring ? len / 90.0f : 0.0f);
        }
    for (int s = 0; s < sides; ++s) {
        const uint32_t p = base + static_cast<uint32_t>(s), q = base + static_cast<uint32_t>(sides + 1 + s);
        b.tri(p, q, p + 1);
        b.tri(p + 1, q, q + 1);
    }
}

// a card (two triangles) centred at `c`, `right` and `up` the half extents; the picture is the rect, its bottom at -up
void card(Builder& b, V3 c, V3 right, V3 up, const Rect& r, V3 normal) {
    const uint32_t i = b.nv;
    b.vertex(c - right - up, normal, r.u0, r.v1);
    b.vertex(c + right - up, normal, r.u1, r.v1);
    b.vertex(c + right + up, normal, r.u1, r.v0);
    b.vertex(c - right + up, normal, r.u0, r.v0);
    b.tri(i, i + 1, i + 2);
    b.tri(i, i + 2, i + 3);
}

// a cluster of leaves: `n` cards through the centre at random turns (like a crossed billboard) so the cluster has volume from every side
void cluster(Builder& b, Rng& rng, V3 c, float width, const Rect& r, V3 crown_centre, int n, float tilt = 0.6f) {
    const float aspect = (r.v1 - r.v0) * 2.0f / ((r.u1 - r.u0) * 1.0f);  // the composite is 1024 x 2048: v spans twice the pixels of u
    const float half_w = width * 0.5f, half_h = std::max(width / std::max(aspect, 0.2f), width * 0.25f) * 0.5f;
    V3 out = norm(c - crown_centre);
    out = norm(out * 0.7f + V3{0, 0.5f, 0});  // lit like a round crown: the normal leans away from its centre and up
    for (int k = 0; k < n; ++k) {
        const float yaw = rng.range(0, 3.14159f), pitch = rng.range(-tilt, tilt);
        const V3 right = V3{std::cos(yaw), 0, std::sin(yaw)} * half_w;
        const V3 up = norm(V3{-std::sin(yaw) * std::sin(pitch), std::cos(pitch), std::cos(yaw) * std::sin(pitch)}) * half_h;
        card(b, c, right, up, r, out);
    }
}

bool has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

}  // namespace

MeshData speedtree_proxy(const Bytes& file, const std::string& tree_name) {
    const SpeedTreeInfo info = read_speedtree(file);
    const float H = info.box_max[1];
    const float cx = (info.box_min[0] + info.box_max[0]) * 0.5f, cz = (info.box_min[2] + info.box_max[2]) * 0.5f;
    const float rx = std::max((info.box_max[0] - info.box_min[0]) * 0.5f, 10.0f), rz = std::max((info.box_max[2] - info.box_min[2]) * 0.5f, 10.0f);
    const float R = std::max(rx, rz);
    const bool marine = has(tree_name, "_Marine_"), bush = has(tree_name, "_Bush_"), palm = has(tree_name, "Palm");
    const bool conifer = has(tree_name, "Pine") || has(tree_name, "Fir") || has(tree_name, "Juniper") || has(tree_name, "Bristle");
    Rng rng{static_cast<uint32_t>(std::hash<std::string>{}(tree_name)) | 1u};

    // the picture of the foliage by species: the composite has needles, fronds and leaf clusters of the trees the game ships
    Rect leaf = kOak;
    if (has(tree_name, "Banyan") || has(tree_name, "Maple")) leaf = kMaple;
    else if (has(tree_name, "RedOak")) leaf = kOak;
    else if (has(tree_name, "Mimosa")) leaf = kFern;
    else if (has(tree_name, "Juniper")) leaf = kJuniper;
    else if (has(tree_name, "Pine") || has(tree_name, "Fir") || has(tree_name, "Bristle")) leaf = kPine;
    else if (palm) leaf = kPalm;
    else if (has(tree_name, "Boxwood")) leaf = kBoxwood;
    else if (has(tree_name, "AzaleaFlower")) leaf = kAzalea;
    else if (has(tree_name, "Azalea")) leaf = kTwigLeaves;
    else if (has(tree_name, "Holly")) leaf = kHolly;
    else if (has(tree_name, "Lily")) leaf = kLily;
    else if (has(tree_name, "Hydrilla")) leaf = kFern2;
    else if (marine) leaf = kDense;
    else if (const size_t h = std::hash<std::string>{}(tree_name) % 2; h == 1) leaf = kWeeping;

    Builder b;
    MeshData m;
    m.stride = 32;
    m.pos = 0;
    m.normal = 12;
    m.uv = 24;
    m.index32 = true;

    // ---- wood: the trunk and the limbs (bark texture), collecting where the foliage goes
    SubMesh wood;
    for (const std::string& t : info.textures)
        if (has(t, "Bark")) {
            wood.texture = t.substr(0, t.size() - 4);
            break;
        }
    struct Tuft {
        V3 at;
        float width;
        int cards;
    };
    std::vector<Tuft> tufts;
    V3 palm_top{};
    bool has_palm_crown = false;
    std::vector<std::pair<V3, std::pair<float, float>>> whorl_cards;  // conifers: a needle spray: centre, (length, direction yaw)
    const float r0 = std::clamp(H * 0.028f, 5.0f, 45.0f);
    if (!bush && !marine) {
        if (palm) {
            // a trunk with a gentle lean, thick at the foot, with the fronds from its top
            const int seg = 6;
            const float lean = rng.range(-0.12f, 0.12f) * H, yaw = rng.range(0, 6.28f);
            V3 prev{0, 0, 0};
            for (int i = 1; i <= seg; ++i) {
                const float t = static_cast<float>(i) / seg, y = H * 0.86f * t, off = lean * t * t;
                const V3 p{off * std::cos(yaw), y, off * std::sin(yaw)};
                tube(b, prev, p, r0 * (1.0f - 0.45f * (t - 1.0f / seg)), r0 * (1.0f - 0.45f * t), 7);
                prev = p;
            }
            palm_top = prev;
            has_palm_crown = true;
        } else if (conifer) {
            const float top = H * 0.94f;
            tube(b, V3{0, 0, 0}, V3{rng.range(-5, 5), top, rng.range(-5, 5)}, r0, r0 * 0.12f, 7);
            const int whorls = 9;
            for (int i = 0; i < whorls; ++i) {
                const float t = static_cast<float>(i) / (whorls - 1), y = H * (0.2f + 0.72f * t);
                const float reach = R * (1.0f - 0.85f * t) * rng.range(0.9f, 1.05f);
                const int arms = 6 + static_cast<int>(2 * (1.0f - t));
                const float ph = rng.range(0, 6.28f);
                for (int a = 0; a < arms; ++a) {
                    const float yaw = ph + 6.2831853f * static_cast<float>(a) / static_cast<float>(arms);
                    const V3 dir{std::cos(yaw), 0, std::sin(yaw)};
                    const V3 tip = V3{0, y, 0} + dir * reach + V3{0, -reach * 0.18f, 0};
                    tube(b, V3{0, y, 0}, tip, r0 * 0.12f, r0 * 0.03f, 4);
                    whorl_cards.push_back({tip * 0.55f + V3{0, y * 0.45f, 0}, {reach, yaw}});
                }
            }
        } else {
            // a broadleaf tree: a bent trunk, then limbs that rise and spread, foliage at their ends and over the top
            const float fork = H * rng.range(0.38f, 0.5f);
            const V3 mid{rng.range(-0.03f, 0.03f) * H, fork * 0.5f, rng.range(-0.03f, 0.03f) * H}, top{rng.range(-0.04f, 0.04f) * H, fork, rng.range(-0.04f, 0.04f) * H};
            tube(b, V3{0, 0, 0}, mid, r0, r0 * 0.8f, 8);
            tube(b, mid, top, r0 * 0.8f, r0 * 0.55f, 8);
            const int limbs = has(tree_name, "Banyan") ? 8 : 6;
            const float ph = rng.range(0, 6.28f);
            for (int i = 0; i < limbs; ++i) {
                const float yaw = ph + 6.2831853f * static_cast<float>(i) / limbs + rng.range(-0.25f, 0.25f);
                const float rise = rng.range(0.55f, 1.0f), len = std::min(R, H * 0.5f) * rng.range(0.7f, 1.0f);
                const V3 dir = norm(V3{std::cos(yaw), rise, std::sin(yaw)});
                const V3 base = top * rng.range(0.7f, 1.0f) + V3{0, -fork * 0.05f * static_cast<float>(i % 3), 0};
                const V3 knee = base + dir * (len * 0.5f) + V3{0, len * 0.08f, 0};
                const V3 tip = knee + norm(dir + V3{0, 0.35f, 0}) * (len * 0.55f);
                tube(b, base, knee, r0 * 0.5f, r0 * 0.32f, 6);
                tube(b, knee, tip, r0 * 0.32f, r0 * 0.12f, 5);
                tufts.push_back({tip, R * 0.85f, 3});
                tufts.push_back({knee + (tip - knee) * 0.5f + V3{0, R * 0.1f, 0}, R * 0.7f, 2});
                // a branch off to the side of the limb
                const V3 side = norm(cross(dir, V3{0, 1, 0}));
                const V3 sprig = knee + side * (len * 0.35f * (i % 2 ? 1.0f : -1.0f)) + V3{0, len * 0.2f, 0};
                tube(b, knee, sprig, r0 * 0.2f, r0 * 0.08f, 4);
                tufts.push_back({sprig, R * 0.6f, 2});
            }
            tufts.push_back({top + V3{0, H * 0.3f, 0}, R * 0.95f, 3});
            tufts.push_back({top + V3{0, H * 0.18f, 0}, R * 1.0f, 3});
        }
    }
    wood.first_index = 0;
    wood.index_count = b.ni;
    wood.vertex_count = b.nv;

    // ---- foliage (alpha-cut cards of the composite image)
    SubMesh leaves;
    leaves.texture = "ST_Composite_01_Diffuse_01";
    leaves.first_index = b.ni;
    const V3 centre{cx, H * 0.6f, cz};
    if (has_palm_crown) {
        const V3 top = palm_top;
            const int fronds = 11;
            // the fronds: arched strips from the crown, each a card bent in four parts
            for (int i = 0; i < fronds; ++i) {
                const float yaw2 = 6.2831853f * static_cast<float>(i) / fronds + rng.range(-0.2f, 0.2f), length = H * rng.range(0.34f, 0.44f), droop = rng.range(0.5f, 1.1f);
                const V3 dir{std::cos(yaw2), 0, std::sin(yaw2)};
                const V3 side{-dir.z, 0, dir.x};
                const int parts = 5;
                V3 pos = top;
                float pitch = rng.range(0.55f, 0.95f);
                const float w = length * 0.22f;
                uint32_t prev_l = 0, prev_r = 0;
                for (int s = 0; s <= parts; ++s) {
                    const float t = static_cast<float>(s) / parts;
                    const V3 n{0, 1, 0};
                    const uint32_t l = b.nv;
                    b.vertex(pos - side * w * (1.0f - 0.5f * t), n, leaf.u0, leaf.v1 + (leaf.v0 - leaf.v1) * t);
                    b.vertex(pos + side * w * (1.0f - 0.5f * t), n, leaf.u1, leaf.v1 + (leaf.v0 - leaf.v1) * t);
                    if (s > 0) {
                        b.tri(prev_l, prev_r, l);
                        b.tri(prev_r, l + 1, l);
                    }
                    prev_l = l;
                    prev_r = l + 1;
                    pos = pos + dir * (std::cos(pitch) * length / parts) + V3{0, std::sin(pitch) * length / parts, 0};
                    pitch -= droop / parts;
                }
            }
    }
    if (!whorl_cards.empty()) {
        // needle sprays along each arm of a conifer: one flat card, one standing card
        for (auto& [at, rest] : whorl_cards) {
            const float reach = rest.first, yaw = rest.second;
            const V3 dir{std::cos(yaw), 0, std::sin(yaw)}, side{-dir.z, 0, dir.x};
            const float w = std::max(reach * 0.55f, H * 0.05f);
            const V3 c = at + V3{0, -w * 0.12f, 0};
            card(b, c, side * (w * 0.5f), dir * (w * 0.5f), leaf, V3{0, 1, 0});  // laid flat along the arm
            card(b, c + V3{0, w * 0.1f, 0}, dir * (w * 0.5f), V3{0, w * 0.32f, 0}, leaf, norm(dir * 0.6f + V3{0, 0.6f, 0}));
        }
    }
    for (const Tuft& t : tufts) {
        V3 at = t.at;
        at.y = std::min(at.y, H - t.width * 0.3f);
        cluster(b, rng, at, t.width, leaf, centre, t.cards);
        // a few smaller clusters around it fill the gaps between limbs
        for (int k = 0; k < 3; ++k) {
            V3 p = at + V3{rng.range(-1, 1), rng.range(-0.5f, 0.6f), rng.range(-1, 1)} * (t.width * 0.45f);
            p.y = std::clamp(p.y, H * 0.12f, H - t.width * 0.25f);
            cluster(b, rng, p, t.width * 0.7f, leaf, centre, 2);
        }
    }
    if (bush || marine) {
        // mounds of leaves on the ground: a lily pad lies flat on the water, the rest are dense heaps
        if (has(tree_name, "Lily")) {
            for (int k = 0; k < 6; ++k) {
                const V3 c{cx + rng.range(-rx, rx) * 0.6f, std::max(H * 0.5f, 2.0f), cz + rng.range(-rz, rz) * 0.6f};
                const float w = R * rng.range(0.35f, 0.55f);
                card(b, c, V3{w, 0, 0}, V3{0, 0, w}, leaf, V3{0, 1, 0});
            }
        } else {
            const int clusters = 16;
            for (int k = 0; k < clusters; ++k) {
                const float a = rng.range(0, 6.28f), rr = std::sqrt(rng.next());
                const float yy = H * rng.range(0.15f, 0.85f) * (1.0f - 0.5f * rr);
                const V3 p{cx + std::cos(a) * rx * 0.75f * rr, yy, cz + std::sin(a) * rz * 0.75f * rr};
                cluster(b, rng, p, std::max(H, R * 0.8f) * rng.range(0.5f, 0.75f), leaf, V3{cx, H * 0.4f, cz}, 2, 0.8f);
            }
        }
    }
    leaves.index_count = b.ni - leaves.first_index;
    leaves.first_vertex = 0;
    leaves.vertex_count = b.nv;

    // fit to the boundary the game stores for the tree: the extents of the generated tree are scaled, side by side about the trunk's foot, to the box's (not by more than a
    // factor of 2 either way), so a crown that leans or spreads to one side does so here too and nothing sticks out of the box
    {
        auto* f = reinterpret_cast<float*>(b.verts.data());
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        for (uint32_t v = 0; v < b.nv; ++v)
            for (int k = 0; k < 3; ++k) {
                lo[k] = std::min(lo[k], f[v * 8 + static_cast<uint32_t>(k)]);
                hi[k] = std::max(hi[k], f[v * 8 + static_cast<uint32_t>(k)]);
            }
        float pos_scale[3], neg_scale[3];
        for (int k = 0; k < 3; ++k) {
            const float bhi = info.box_max[static_cast<size_t>(k)], blo = info.box_min[static_cast<size_t>(k)];
            pos_scale[k] = hi[k] > 1.0f && bhi > 1.0f ? std::clamp(bhi / hi[k], 0.5f, 2.0f) : 1.0f;
            neg_scale[k] = lo[k] < -1.0f && blo < -1.0f ? std::clamp(blo / lo[k], 0.5f, 2.0f) : 1.0f;
        }
        for (uint32_t v = 0; v < b.nv; ++v)
            for (uint32_t k = 0; k < 3; ++k) {
                float& c = f[v * 8 + k];
                c *= c >= 0 ? pos_scale[k] : neg_scale[k];
                if (k == 1) c = std::max(c, -0.5f);  // nothing below the ground: the lowest cards of a bush are cut off there
            }
    }

    if (wood.index_count) m.submeshes.push_back(wood);
    if (leaves.index_count) m.submeshes.push_back(leaves);
    m.vertices = std::move(b.verts);
    m.indices = std::move(b.idx);
    m.vertex_count = b.nv;
    m.index_count = b.ni;
    m.box_min = info.box_min;
    m.box_max = info.box_max;
    return m;
}

}  // namespace mk
