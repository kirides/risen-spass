#include "core/world.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

#include "core/genome.h"
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
        GFile f;
        try {
            f = parse_genome(g.read_path(path));
        } catch (const std::exception& ex) {
            log_warn("world layer " + path + " skipped: " + ex.what());
            continue;
        }
        WorldLayer L;
        L.path = path;
        const std::string rel = path.substr(prefix.size());
        const auto slash = rel.rfind('/');
        L.group = slash == std::string::npos ? std::string() : rel.substr(0, slash);
        L.name = p2m::bare_name(rel);
        if (p2m::ends_with(p2m::lower(L.name), ".lrent")) L.name.resize(L.name.size() - 6);
        L.first = static_cast<uint32_t>(w->entities.size());
        const uint32_t layer_index = static_cast<uint32_t>(w->layers.size());
        for (const GEntity& ge : f.entities) {
            if (ge.name == "DRE") continue;  // the layer's root record (identity matrix, no box): not an entity of the world
            WorldEntity e;
            e.name = ge.name;
            e.layer = layer_index;
            e.start = static_cast<uint32_t>(ge.begin);
            e.end = static_cast<uint32_t>(ge.end);
            {
                // "@" + 32 hex = the 16 bytes in file order
                char raw[16];
                for (size_t k = 0; k < 16; ++k) raw[k] = static_cast<char>(std::stoi(ge.id.substr(1 + 2 * k, 2), nullptr, 16));
                e.guid = guid_text(raw);
            }
            e.matrix = ge.matrix;
            e.box_min = ge.box_min;
            e.box_max = ge.box_max;
            // the template the entity was made from: the creator proxy of its record (an entity without one has no template)
            if (!ge.creator.empty()) e.template_name = g.template_by_guid(ge.creator);
            for (const GSet& s : ge.sets) {
                if (s.cls == "gCNPC_PS") e.npc = true;
                else if (s.cls == "gCInventory_PS") e.inventory = true;
                // level meshes name a virtual path ("#G3:/data/raw/meshes/World//Harbour_01_L01._xmsh"): resources are found by the bare name (RE.md section 26)
                else if (s.cls == "eCMesh_PS")
                    for (const p2m::Prop& pr : s.props)
                        if (pr.name == "MeshFileName" && pr.value.is_str() && !pr.value.s.empty()) e.mesh = p2m::bare_name(pr.value.s);
            }
            w->entities.push_back(std::move(e));
        }
        for (size_t i = L.first; i < w->entities.size(); ++i) {
            WorldEntity& e = w->entities[i];
            const TemplateInfo* tpl = e.template_name.empty() ? nullptr : g.tpl(e.template_name);
            if (tpl) {
                e.kind = tpl->kind;
                if (e.mesh.empty()) e.mesh = p2m::bare_name(tpl->mesh);
            } else {
                e.kind = e.npc ? TplKind::Npc : !e.mesh.empty() ? TplKind::Object : e.inventory ? TplKind::Interactive : TplKind::Other;
            }
        }
        L.count = static_cast<uint32_t>(w->entities.size() - L.first);
        w->layers.push_back(std::move(L));
    }
    w->sectors = load_sectors(g, world);
    {
        std::unordered_map<std::string, int> by_name;
        for (size_t i = 0; i < w->sectors.size(); ++i) by_name[p2m::lower(w->sectors[i].name)] = static_cast<int>(i);
        for (WorldLayer& L : w->layers)
            if (auto it = by_name.find(p2m::lower(L.name)); it != by_name.end()) L.sector = it->second;
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
        const GFile f = parse_genome(g.read_path(w.layers[e.layer].path));
        for (const GEntity& ge : f.entities) {
            if (ge.begin != e.start) continue;
            const p2m::Template t = genome_entity_props(ge);
            for (p2m::Prop p : t.props) {
                if (p.value.is_str() && p.value.s.size() == 33 && p.value.s[0] == '@') {
                    const std::string n = g.template_by_guid(p.value.s);
                    if (!n.empty()) p.value.s = n;
                }
                out.push_back(std::move(p));
            }
            break;
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
