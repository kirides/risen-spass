#include "core/world.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

#include "core/log.h"

namespace mk {

float WorldEntity::yaw() const {
    // the forward axis is row 2 (z); yaw 0 = +z, 90 = +x
    const float fx = matrix[8], fz = matrix[10];
    float deg = std::atan2(fx, fz) * 180.0f / 3.14159265f;
    if (deg < 0) deg += 360.0f;
    return deg;
}

std::string guid_text(const char* b) {
    auto u8 = [&](int i) { return static_cast<unsigned>(static_cast<unsigned char>(b[i])); };
    char buf[40];
    std::snprintf(buf, sizeof buf, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", u8(3), u8(2), u8(1), u8(0), u8(5), u8(4), u8(7), u8(6),
                  u8(8), u8(9), u8(10), u8(11), u8(12), u8(13), u8(14), u8(15));
    return buf;
}

const WorldEntity* World::by_guid(const std::string& guid) const {
    for (auto& e : entities)
        if (e.guid == guid) return &e;
    return nullptr;
}

const WorldEntity* World::by_name(const std::string& entity_name) const {
    for (auto& e : entities)
        if (e.name == entity_name) return &e;
    return nullptr;
}

namespace {

bool is_type_name(const std::string& n) {
    return p2m::starts_with(n, "gC") || p2m::starts_with(n, "eC") || p2m::starts_with(n, "bC") || p2m::starts_with(n, "bT");
}

struct Record {
    size_t start;
    std::string name;
};

// the entity records of a layer file (byte offsets, names), the same recognition as risen-pack2mod's world scanner
std::vector<Record> find_records(const p2m::Bytes& b, const std::vector<std::string>& strings, size_t table) {
    std::vector<Record> out;
    for (size_t p = 0; p + 24 + 64 <= table; ++p) {
        if (b[p] != '\xD6' || b[p + 1] != '\0') continue;
        if (b.compare(p + 18, 4, std::string(4, '\0')) != 0 || (b[p + 4] == '\x1e' && b[p + 5] == '\0')) continue;
        const size_t idx = p2m::rd16(b, p + 22);
        if (idx >= strings.size()) continue;
        const std::string& nm = strings[idx];
        if (nm.empty() || is_type_name(nm)) continue;
        bool ok = true;
        for (size_t k = 0; k < 16 && ok; ++k) {
            const float f = p2m::rdf(b, p + 24 + 4 * k);
            ok = std::isfinite(f) && std::fabs(f) < 1e7f;
        }
        if (!ok) continue;
        out.push_back({p, nm});
        p += 1;
    }
    return out;
}

void read_entity(const p2m::Bytes& b, size_t p, size_t end, WorldEntity& e, const GameData& g) {
    e.guid = guid_text(b.data() + p + 2);
    for (size_t k = 0; k < 16; ++k) e.matrix[k] = p2m::rdf(b, p + 24 + 4 * k);
    const size_t box = p + 24 + 64;
    if (box + 24 <= end) {
        for (int k = 0; k < 3; ++k) {
            e.box_min[k] = p2m::rdf(b, box + 4 * k);
            e.box_max[k] = p2m::rdf(b, box + 12 + 4 * k);
        }
    }
    // the creator template: "01 00 01" + GUID after the boxes and the sphere (searched a little, the layout differs for some entity kinds)
    const size_t from = box + 24 + 16 + 24;
    for (size_t q = from; q + 3 + 16 <= end && q < from + 48; ++q) {
        if (b[q] == '\x01' && b[q + 1] == '\0' && b[q + 2] == '\x01') {
            const std::string at = "@" + p2m::hex_of(std::string_view(b.data() + q + 3, 16));
            e.template_name = g.template_by_guid(at);
            if (!e.template_name.empty()) break;
        }
    }
}

}  // namespace

std::shared_ptr<World> load_world(const GameData& g, const std::string& world, const WorldProgress& progress, const std::atomic<bool>* cancel) {
    const auto t0 = std::chrono::steady_clock::now();
    const std::string prefix = "projects/" + world + "/";
    std::vector<std::string> files;
    for (auto& path : g.paths_with_prefix(prefix))
        if (p2m::ends_with(p2m::lower(path), ".lrent")) files.push_back(path);
    if (files.empty()) throw std::runtime_error("the game data has no world '" + world + "'");
    auto w = std::make_shared<World>();
    w->name = world;
    size_t done = 0;
    for (const std::string& path : files) {
        if (cancel && cancel->load()) return nullptr;
        if (progress && (++done % 10 == 0)) progress(static_cast<float>(done) / static_cast<float>(files.size()), path);
        p2m::Bytes b;
        p2m::Template t;
        try {
            b = g.read_path(path);
            t = p2m::parse_template(b);
        } catch (const std::exception& ex) {
            log_warn("world layer " + path + " skipped: " + ex.what());
            continue;
        }
        const size_t table = p2m::rd32(b, 10);
        auto recs = find_records(b, t.strings, table);
        WorldLayer L;
        L.path = path;
        const std::string rel = path.substr(prefix.size());
        const auto slash = rel.rfind('/');
        L.group = slash == std::string::npos ? std::string() : rel.substr(0, slash);
        L.name = p2m::bare_name(rel);
        if (p2m::ends_with(p2m::lower(L.name), ".lrent")) L.name.resize(L.name.size() - 6);
        L.first = static_cast<uint32_t>(w->entities.size());
        const uint32_t layer_index = static_cast<uint32_t>(w->layers.size());
        for (size_t i = 0; i < recs.size(); ++i) {
            const size_t end = i + 1 < recs.size() ? recs[i + 1].start : table;
            if (recs[i].name == "DRE") continue;  // the layer's root record (identity matrix, no box): not an entity of the world
            WorldEntity e;
            e.name = recs[i].name;
            e.layer = layer_index;
            e.start = static_cast<uint32_t>(recs[i].start);
            e.end = static_cast<uint32_t>(end);
            read_entity(b, recs[i].start, end, e, g);
            w->entities.push_back(std::move(e));
        }
        // the properties of the layer belong to the entity whose range holds them
        size_t cur = L.first;
        for (const p2m::Prop& pr : t.props) {
            while (cur + 1 < w->entities.size() && pr.off >= w->entities[cur].end) ++cur;
            if (cur >= w->entities.size() || cur < L.first) continue;
            WorldEntity& e = w->entities[cur];
            if (pr.off < e.start || pr.off >= e.end) continue;
            if (pr.pset == "gCNPC_PS") e.npc = true;
            else if (pr.pset == "gCInventory_PS") e.inventory = true;
            else if (pr.pset == "eCMesh_PS" && pr.name == "MeshFileName" && pr.value.is_str() && !pr.value.s.empty()) e.mesh = pr.value.s;
        }
        for (size_t i = L.first; i < w->entities.size(); ++i) {
            WorldEntity& e = w->entities[i];
            const TemplateInfo* tpl = e.template_name.empty() ? nullptr : g.tpl(e.template_name);
            if (tpl) {
                e.kind = tpl->kind;
                if (e.mesh.empty()) e.mesh = tpl->mesh;
            } else {
                e.kind = e.npc ? TplKind::Npc : !e.mesh.empty() ? TplKind::Object : e.inventory ? TplKind::Interactive : TplKind::Other;
            }
        }
        L.count = static_cast<uint32_t>(w->entities.size() - L.first);
        w->layers.push_back(std::move(L));
    }
    w->load_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    log_info("world " + world + ": " + std::to_string(w->entities.size()) + " entities in " + std::to_string(w->layers.size()) + " layers (" +
             std::to_string(w->load_seconds).substr(0, 4) + " s)");
    return w;
}

std::vector<p2m::Prop> entity_properties(const GameData& g, const World& w, const WorldEntity& e) {
    std::vector<p2m::Prop> out;
    if (e.layer >= w.layers.size()) return out;
    try {
        const p2m::Bytes b = g.read_path(w.layers[e.layer].path);
        p2m::Template t = p2m::parse_template(b);
        for (auto& p : t.props) {
            if (p.off < e.start || p.off >= e.end) continue;
            if (p.value.is_str() && p.value.s.size() == 33 && p.value.s[0] == '@') {
                const std::string n = g.template_by_guid(p.value.s);
                if (!n.empty()) p.value.s = n;
            }
            out.push_back(p);
        }
    } catch (const std::exception& ex) {
        log_warn(std::string("entity properties: ") + ex.what());
    }
    return out;
}

std::vector<std::pair<std::string, int>> entity_inventory(const GameData& g, const World& w, const WorldEntity& e) {
    // records of gCInventory_PS: a new record starts at "Amount" (risen-pack2mod's reading, RE.md section 47)
    std::vector<std::pair<std::string, int>> out;
    std::string tpl;
    int amount = 0;
    bool have = false;
    auto flush = [&] {
        if (have && !tpl.empty()) out.push_back({tpl, std::max(1, amount)});
        tpl.clear();
        amount = 0;
    };
    for (const p2m::Prop& p : entity_properties(g, w, e)) {
        if (p.pset != "gCInventory_PS") continue;
        if (p.name == "Amount") {
            flush();
            have = true;
            amount = p.value.is_num() ? static_cast<int>(p.value.num()) : 1;
        } else if (p.name == "Template") {
            have = true;
            tpl = p.value.is_str() ? p.value.s : std::string();
        }
    }
    flush();
    return out;
}

}  // namespace mk
