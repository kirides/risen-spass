#include "core/layers.h"

#include <algorithm>

#include "core/events.h"
#include "core/log.h"
#include "pack2mod/tple.h"
#include "pack2mod/util.h"

namespace mk {

namespace {

template <class T>
bool has(const std::vector<T>& v, const T& x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

}  // namespace

const LayerDef* layer_of_npc(const Project& p, const std::string& npc) {
    for (auto& l : p.layers)
        if (has(l.npcs, npc)) return &l;
    return nullptr;
}
const LayerDef* layer_of_spawn(const Project& p, const std::string& uid) {
    for (auto& l : p.layers)
        if (has(l.spawns, uid)) return &l;
    return nullptr;
}
const LayerDef* layer_of_container(const Project& p, const std::string& uid) {
    for (auto& l : p.layers)
        if (has(l.containers, uid)) return &l;
    return nullptr;
}
LayerDef* find_layer(Project& p, const std::string& name) {
    for (auto& l : p.layers)
        if (l.name == name) return &l;
    return nullptr;
}
const LayerDef* find_layer(const Project& p, const std::string& name) {
    for (auto& l : p.layers)
        if (l.name == name) return &l;
    return nullptr;
}

json layers_data(const Project& p, std::vector<DialogIssue>* issues) {
    json list = json::array();
    for (auto& l : p.layers) {
        json e = json::object();
        e["name"] = l.name;
        e["enabled"] = l.enabled;
        if (!l.enable_when.is_null()) e["enable_when"] = event_condition_set(l.enable_when);
        if (!l.disable_when.is_null()) e["disable_when"] = event_condition_set(l.disable_when);
        list.push_back(e);
        if (issues) {
            auto check = [&](const json& c, const char* which) {
                if (c.is_null()) return;
                event_check_condition_set(c, [&](bool error, const std::string& m) { issues->push_back({error, l.uid, std::string(which) + ": " + m}); });
            };
            check(l.enable_when, "switches on when");
            check(l.disable_when, "switches off when");
        }
    }
    return list;
}

LayerDef layer_from_data(const json& entry) {
    LayerDef l;
    l.uid = new_uid();
    l.name = entry.value("name", std::string());
    l.enabled = entry.value("enabled", true);
    if (entry.contains("enable_when")) l.enable_when = entry["enable_when"];
    if (entry.contains("disable_when")) l.disable_when = entry["disable_when"];
    return l;
}

std::vector<StockSector> stock_sectors(const GameData& g, const std::string& world) {
    std::vector<StockSector> out;
    const std::string prefix = "projects/" + world + "/";
    for (const std::string& path : g.paths_with_prefix(prefix)) {
        if (!p2m::ends_with(p2m::lower(path), ".sec")) continue;
        StockSector s;
        s.name = p2m::bare_name(path);
        s.name.resize(s.name.size() - 4);
        try {
            const p2m::Template t = p2m::parse_template(g.read_path(path));
            for (const p2m::Prop& pr : t.props)
                if (pr.name == "EnabledOnStartup" && pr.value.is_num()) s.enabled_on_startup = pr.value.num() != 0;
        } catch (const std::exception& e) {
            log_warn("sector " + path + ": " + e.what());
        }
        out.push_back(std::move(s));
    }
    std::sort(out.begin(), out.end(), [](const StockSector& a, const StockSector& b) { return a.name < b.name; });
    return out;
}

unsigned layer_color(size_t index) {
    static const unsigned kColors[] = {0xff4aa3df, 0xffe67e22, 0xff2ecc71, 0xffe84393, 0xfff1c40f, 0xff9b59b6, 0xff1abc9c, 0xffe74c3c};
    return kColors[index % (sizeof(kColors) / sizeof(kColors[0]))];
}

}  // namespace mk
