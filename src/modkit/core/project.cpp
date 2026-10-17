#include "core/project.h"

#include <random>
#include <stdexcept>

#include "core/paths.h"

namespace mk {

std::string new_uid() {
    static thread_local std::mt19937_64 rng{std::random_device{}() ^ (static_cast<uint64_t>(std::random_device{}()) << 32)};
    static const char* hex = "0123456789abcdef";
    std::string s(32, '0');
    uint64_t a = rng(), b = rng();
    for (int i = 0; i < 16; ++i) {
        s[i] = hex[(a >> (i * 4)) & 15];
        s[16 + i] = hex[(b >> (i * 4)) & 15];
    }
    return s;
}

const char* mode_name(Mode m) { return m == Mode::New ? "new" : "edit"; }

GraphNode* Graph::find(const std::string& uid) {
    for (auto& n : nodes)
        if (n.uid == uid) return &n;
    return nullptr;
}
const GraphNode* Graph::find(const std::string& uid) const {
    for (auto& n : nodes)
        if (n.uid == uid) return &n;
    return nullptr;
}

ItemDef* Project::item(const std::string& name) {
    for (auto& x : items)
        if (x.name == name) return &x;
    return nullptr;
}
NpcTemplateDef* Project::npc_template(const std::string& name) {
    for (auto& x : npc_templates)
        if (x.name == name) return &x;
    return nullptr;
}
PlacedNpc* Project::npc(const std::string& name) {
    for (auto& x : npcs)
        if (x.name == name) return &x;
    return nullptr;
}
QuestDef* Project::quest(const std::string& name) {
    for (auto& x : quests)
        if (x.name == name) return &x;
    return nullptr;
}

// ---- JSON helpers ---------------------------------------------------------------------------------------------------------------------------------------
namespace {

struct Reader {
    std::vector<std::string>* warnings;
    std::string where;
    void warn(const std::string& what) const {
        if (warnings) warnings->push_back(where + ": " + what);
    }

    template <class T>
    void get(const json& j, const char* k, T& out) const {
        auto it = j.find(k);
        if (it == j.end() || it->is_null()) return;
        try {
            out = it->get<T>();
        } catch (const std::exception& e) {
            warn(std::string("field '") + k + "': " + e.what());
        }
    }
    template <class T>
    void get(const json& j, const char* k, std::optional<T>& out) const {
        auto it = j.find(k);
        if (it == j.end() || it->is_null()) return;
        try {
            out = it->get<T>();
        } catch (const std::exception& e) {
            warn(std::string("field '") + k + "': " + e.what());
        }
    }
    void get(const json& j, const char* k, json& out) const {
        auto it = j.find(k);
        if (it != j.end()) out = *it;
    }
    void get_mode(const json& j, Mode& m) const {
        std::string s;
        get(j, "mode", s);
        if (s == "edit") m = Mode::Edit;
        else if (s.empty() || s == "new") m = Mode::New;
        else warn("unknown mode '" + s + "'");
    }
    void get_uid(const json& j, std::string& uid) const {
        get(j, "uid", uid);
        if (uid.empty()) uid = new_uid();
    }
};

template <class T>
void put(json& j, const char* k, const std::optional<T>& v) {
    if (v) j[k] = *v;
}
void put_text(json& j, const char* k, const Text& t) {
    if (!t.empty()) j[k] = t;
}
void put_str(json& j, const char* k, const std::string& s) {
    if (!s.empty()) j[k] = s;
}
void put_props(json& j, const PropEdits& p) {
    if (p.empty()) return;
    json o = json::object();
    for (auto& [set, props] : p) {
        json s = json::object();
        for (auto& [name, v] : props) s[name] = v;
        o[set] = s;
    }
    j["properties"] = o;
}
void get_props(const Reader& r, const json& j, PropEdits& p) {
    auto it = j.find("properties");
    if (it == j.end() || !it->is_object()) return;
    for (auto& [set, props] : it->items()) {
        if (!props.is_object()) {
            r.warn("properties." + set + " is not an object");
            continue;
        }
        for (auto& [name, v] : props.items()) p[set][name] = v;
    }
}

json vec3(const Vec3& v) { return json::array({v[0], v[1], v[2]}); }
bool get_vec3(const Reader& r, const json& j, const char* k, Vec3& v) {
    auto it = j.find(k);
    if (it == j.end() || it->is_null()) return false;
    if (!it->is_array() || it->size() != 3) {
        r.warn(std::string(k) + " is not [x, y, z]");
        return false;
    }
    for (size_t i = 0; i < 3; ++i) {
        if (!(*it)[i].is_number()) {
            r.warn(std::string(k) + " has a non-number");
            return false;
        }
        v[i] = (*it)[i].get<double>();
    }
    return true;
}

json inv_items(const std::vector<InvItem>& items) {
    json a = json::array();
    for (auto& x : items) {
        json e = json::array({x.item, x.amount});
        if (x.equip) e.push_back(true);
        a.push_back(e);
    }
    return a;
}
void get_inv_items(const Reader& r, const json& j, const char* k, std::vector<InvItem>& out) {
    auto it = j.find(k);
    if (it == j.end() || !it->is_array()) return;
    for (auto& e : *it) {
        InvItem x;
        if (e.is_array() && !e.empty() && e[0].is_string()) {
            x.item = e[0].get<std::string>();
            if (e.size() > 1 && e[1].is_number_integer()) x.amount = e[1].get<int>();
            if (e.size() > 2 && e[2].is_boolean()) x.equip = e[2].get<bool>();
            out.push_back(x);
        } else {
            r.warn(std::string(k) + ": an entry is not [item, amount, equip]");
        }
    }
}

json graph_json(const Graph& g) {
    json nodes = json::array(), links = json::array();
    for (auto& n : g.nodes) {
        json o = json::object();
        o["uid"] = n.uid;
        o["type"] = n.type;
        o["pos"] = json::array({n.x, n.y});
        if (!n.data.empty()) o["data"] = n.data;
        nodes.push_back(o);
    }
    for (auto& l : g.links) {
        json o = json::object();
        o["uid"] = l.uid;
        o["from"] = json::array({l.from_node, l.from_pin});
        o["to"] = json::array({l.to_node, l.to_pin});
        links.push_back(o);
    }
    json o = json::object();
    o["nodes"] = nodes;
    o["links"] = links;
    return o;
}
void get_graph(const Reader& r, const json& j, Graph& g) {
    auto it = j.find("graph");
    if (it == j.end() || !it->is_object()) return;
    if (auto n = it->find("nodes"); n != it->end() && n->is_array()) {
        for (auto& e : *n) {
            GraphNode node;
            r.get_uid(e, node.uid);
            r.get(e, "type", node.type);
            if (auto p = e.find("pos"); p != e.end() && p->is_array() && p->size() == 2 && (*p)[0].is_number() && (*p)[1].is_number()) {
                node.x = (*p)[0].get<double>();
                node.y = (*p)[1].get<double>();
            }
            if (auto d = e.find("data"); d != e.end() && d->is_object()) node.data = *d;
            g.nodes.push_back(std::move(node));
        }
    }
    if (auto l = it->find("links"); l != it->end() && l->is_array()) {
        for (auto& e : *l) {
            GraphLink link;
            r.get_uid(e, link.uid);
            auto f = e.find("from"), t = e.find("to");
            if (f == e.end() || t == e.end() || !f->is_array() || !t->is_array() || f->size() != 2 || t->size() != 2) {
                r.warn("a graph link is not {from: [node, pin], to: [node, pin]}");
                continue;
            }
            try {
                link.from_node = (*f)[0].get<std::string>();
                link.from_pin = (*f)[1].get<std::string>();
                link.to_node = (*t)[0].get<std::string>();
                link.to_pin = (*t)[1].get<std::string>();
            } catch (...) {
                r.warn("a graph link has non-string ends");
                continue;
            }
            g.links.push_back(std::move(link));
        }
    }
}

json ingredients_json(const std::vector<Ingredient>& v) {
    json a = json::array();
    for (auto& x : v) a.push_back(json::array({x.item, x.count}));
    return a;
}

json entity_ref_json(const EntityRef& e) {
    json o = json::object();
    o["uid"] = e.uid;
    o["guid"] = e.guid;
    put_str(o, "entity_name", e.entity_name);
    put_str(o, "layer", e.layer);
    if (e.position) o["position"] = vec3(*e.position);
    if (e.rotation) o["rotation"] = vec3(*e.rotation);
    return o;
}
EntityRef entity_ref_from(const Reader& r, const json& e) {
    EntityRef x;
    r.get_uid(e, x.uid);
    r.get(e, "guid", x.guid);
    r.get(e, "entity_name", x.entity_name);
    r.get(e, "layer", x.layer);
    Vec3 v;
    if (get_vec3(r, e, "position", v)) x.position = v;
    if (get_vec3(r, e, "rotation", v)) x.rotation = v;
    return x;
}

template <class T, class F>
void read_list(const json& j, const char* key, std::vector<T>& out, std::vector<std::string>* warnings, F&& one) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return;
    if (!it->is_array()) {
        if (warnings) warnings->push_back(std::string(key) + " is not a list");
        return;
    }
    size_t i = 0;
    for (auto& e : *it) {
        Reader r{warnings, std::string(key) + "[" + std::to_string(i++) + "]"};
        if (!e.is_object()) {
            r.warn("not an object");
            continue;
        }
        out.push_back(one(r, e));
    }
}

}  // namespace

// ---- Project -> JSON -------------------------------------------------------------------------------------------------------------------------------------
json to_json(const Project& p) {
    json j = json::object();
    j["format"] = "risen-modkit-project";
    j["format_version"] = kProjectFormatVersion;

    json m = json::object();
    m["id"] = p.mod.id;
    m["name"] = p.mod.name;
    m["version"] = p.mod.version;
    m["author"] = p.mod.author;
    m["description"] = p.mod.description;
    m["languages"] = p.mod.languages;
    if (!p.mod.load_after.empty()) m["load_after"] = p.mod.load_after;
    if (!p.mod.requires_mods.empty()) m["requires"] = p.mod.requires_mods;
    j["mod"] = m;

    json items = json::array();
    for (auto& x : p.items) {
        json o = json::object();
        o["uid"] = x.uid;
        o["mode"] = mode_name(x.mode);
        o["name"] = x.name;
        put_str(o, "base", x.base);
        put_text(o, "label", x.label);
        put_text(o, "description", x.description);
        put(o, "value", x.value);
        put(o, "damage", x.damage);
        put(o, "mana_cost", x.mana_cost);
        put(o, "requires", x.requirements);
        put(o, "modifies", x.modifies);
        put(o, "use_type", x.use_type);
        put(o, "category", x.category);
        put(o, "mesh", x.mesh);
        put(o, "icon", x.icon);
        put(o, "material", x.material);
        put_props(o, x.properties);
        put_str(o, "notes", x.notes);
        items.push_back(o);
    }
    j["items"] = items;

    json groups = json::array();
    for (auto& g : p.item_groups) {
        json o = json::object();
        o["uid"] = g.uid;
        put_str(o, "group", g.group);
        put_str(o, "match", g.match);
        put_str(o, "exclude", g.exclude);
        if (!g.names.empty()) o["names"] = g.names;
        put(o, "value_mult", g.value_mult);
        put(o, "damage_mult", g.damage_mult);
        groups.push_back(o);
    }
    j["item_groups"] = groups;

    json recipes = json::array();
    for (auto& x : p.recipes) {
        json o = json::object();
        o["uid"] = x.uid;
        o["mode"] = mode_name(x.mode);
        o["name"] = x.name;
        put_str(o, "base", x.base);
        put_str(o, "station", x.station);
        if (x.ingredients) o["ingredients"] = ingredients_json(*x.ingredients);
        put(o, "skills", x.skills);
        put(o, "result", x.result);
        put_text(o, "label", x.label);
        put_text(o, "description", x.description);
        put(o, "icon", x.icon);
        put(o, "mesh", x.mesh);
        put(o, "value", x.value);
        if (x.learn) o["learn"] = true;
        put_props(o, x.properties);
        put_str(o, "notes", x.notes);
        recipes.push_back(o);
    }
    j["recipes"] = recipes;

    json npct = json::array();
    for (auto& x : p.npc_templates) {
        json o = json::object();
        o["uid"] = x.uid;
        o["mode"] = mode_name(x.mode);
        o["name"] = x.name;
        put_str(o, "base", x.base);
        put_text(o, "label", x.label);
        put(o, "mesh", x.mesh);
        put(o, "hp_max", x.hp_max);
        put(o, "hp", x.hp);
        put(o, "hp_mult", x.hp_mult);
        put(o, "mana_max", x.mana_max);
        put(o, "mana", x.mana);
        put(o, "mana_mult", x.mana_mult);
        put(o, "level", x.level);
        put(o, "level_add", x.level_add);
        put(o, "level_mult", x.level_mult);
        put(o, "xp", x.xp);
        put(o, "damage", x.damage);
        put(o, "damage_mult", x.damage_mult);
        put(o, "damage_type", x.damage_type);
        put(o, "skills", x.skills);
        put(o, "guild", x.guild);
        put(o, "species", x.species);
        put(o, "attitude", x.attitude);
        put(o, "bravery", x.bravery);
        put(o, "damage_calc", x.damage_calc);
        put(o, "combat_species", x.combat_species);
        put(o, "ai_group", x.ai_group);
        put(o, "loot", x.loot);
        put_props(o, x.properties);
        put_str(o, "notes", x.notes);
        npct.push_back(o);
    }
    j["npc_templates"] = npct;

    json npcs = json::array();
    for (auto& x : p.npcs) {
        json o = json::object();
        o["uid"] = x.uid;
        o["name"] = x.name;
        o["template"] = x.template_name;
        o["position"] = vec3(x.position);
        put(o, "yaw", x.yaw);
        put_text(o, "label", x.label);
        o["peaceful"] = x.peaceful;
        put(o, "guild", x.guild);
        put(o, "level", x.level);
        put(o, "hp_max", x.hp_max);
        if (!x.items.empty()) o["items"] = inv_items(x.items);
        if (!x.routine.is_null()) o["routine"] = x.routine;
        if (x.respawn) o["respawn"] = true;
        if (x.version != 1) o["version"] = x.version;
        if (!x.appear_when.is_null()) o["appear_when"] = x.appear_when;
        if (!x.leave_when.is_null()) o["leave_when"] = x.leave_when;
        put_str(o, "notes", x.notes);
        npcs.push_back(o);
    }
    j["npcs"] = npcs;

    json dialogs = json::array();
    for (auto& x : p.dialogs) {
        json o = json::object();
        o["uid"] = x.uid;
        o["name"] = x.name;
        o["owner"] = x.owner;
        o["graph"] = graph_json(x.graph);
        put_str(o, "notes", x.notes);
        dialogs.push_back(o);
    }
    j["dialogs"] = dialogs;

    json quests = json::array();
    for (auto& x : p.quests) {
        json o = json::object();
        o["uid"] = x.uid;
        o["name"] = x.name;
        put_text(o, "title", x.title);
        o["type"] = x.type;
        put(o, "xp", x.xp);
        if (x.silent) o["silent"] = true;
        o["graph"] = graph_json(x.graph);
        put_str(o, "notes", x.notes);
        quests.push_back(o);
    }
    j["quests"] = quests;

    json w = json::object();
    w["world"] = p.world.world;
    json spawns = json::array();
    for (auto& s : p.world.spawns) {
        json o = json::object();
        o["uid"] = s.uid;
        o["template"] = s.template_name;
        o["position"] = vec3(s.position);
        if (s.rotation != Vec3{0, 0, 0}) o["rotation"] = vec3(s.rotation);
        if (s.amount != 1) o["amount"] = s.amount;
        put_str(o, "notes", s.notes);
        spawns.push_back(o);
    }
    w["spawns"] = spawns;
    json containers = json::array();
    for (auto& c : p.world.containers) {
        json o = json::object();
        o["uid"] = c.uid;
        o["guid"] = c.guid;
        put_str(o, "entity_name", c.entity_name);
        put_str(o, "layer", c.layer);
        o["items"] = inv_items(c.items);
        containers.push_back(o);
    }
    w["containers"] = containers;
    json removals = json::array(), moves = json::array();
    for (auto& e : p.world.removals) removals.push_back(entity_ref_json(e));
    for (auto& e : p.world.moves) moves.push_back(entity_ref_json(e));
    w["removals"] = removals;
    w["moves"] = moves;
    j["world"] = w;

    json assets = json::array();
    for (auto& a : p.assets) {
        json o = json::object();
        o["uid"] = a.uid;
        o["file"] = a.file;
        o["kind"] = a.kind;
        assets.push_back(o);
    }
    j["assets"] = assets;
    j["editor"] = p.editor;
    return j;
}

// ---- JSON -> Project -------------------------------------------------------------------------------------------------------------------------------------
Project project_from_json(const json& j, std::vector<std::string>* warnings) {
    if (!j.is_object() || j.value("format", std::string()) != "risen-modkit-project") throw std::runtime_error("not a risen-modkit project (\"format\" is missing)");
    const int ver = j.value("format_version", 0);
    if (ver > kProjectFormatVersion)
        throw std::runtime_error("the project was saved by a newer risen-modkit (format " + std::to_string(ver) + ", this one reads " +
                                 std::to_string(kProjectFormatVersion) + ")");
    Project p;
    if (auto m = j.find("mod"); m != j.end() && m->is_object()) {
        Reader r{warnings, "mod"};
        r.get(*m, "id", p.mod.id);
        r.get(*m, "name", p.mod.name);
        r.get(*m, "version", p.mod.version);
        r.get(*m, "author", p.mod.author);
        r.get(*m, "description", p.mod.description);
        r.get(*m, "languages", p.mod.languages);
        r.get(*m, "load_after", p.mod.load_after);
        r.get(*m, "requires", p.mod.requires_mods);
        if (p.mod.languages.empty()) p.mod.languages = {"English"};
    }
    read_list(j, "items", p.items, warnings, [](const Reader& r, const json& e) {
        ItemDef x;
        r.get_uid(e, x.uid);
        r.get_mode(e, x.mode);
        r.get(e, "name", x.name);
        r.get(e, "base", x.base);
        r.get(e, "label", x.label);
        r.get(e, "description", x.description);
        r.get(e, "value", x.value);
        r.get(e, "damage", x.damage);
        r.get(e, "mana_cost", x.mana_cost);
        r.get(e, "requires", x.requirements);
        r.get(e, "modifies", x.modifies);
        r.get(e, "use_type", x.use_type);
        r.get(e, "category", x.category);
        r.get(e, "mesh", x.mesh);
        r.get(e, "icon", x.icon);
        r.get(e, "material", x.material);
        get_props(r, e, x.properties);
        r.get(e, "notes", x.notes);
        return x;
    });
    read_list(j, "item_groups", p.item_groups, warnings, [](const Reader& r, const json& e) {
        ItemGroup g;
        r.get_uid(e, g.uid);
        r.get(e, "group", g.group);
        r.get(e, "match", g.match);
        r.get(e, "exclude", g.exclude);
        r.get(e, "names", g.names);
        r.get(e, "value_mult", g.value_mult);
        r.get(e, "damage_mult", g.damage_mult);
        return g;
    });
    read_list(j, "recipes", p.recipes, warnings, [](const Reader& r, const json& e) {
        RecipeDef x;
        r.get_uid(e, x.uid);
        r.get_mode(e, x.mode);
        r.get(e, "name", x.name);
        r.get(e, "base", x.base);
        r.get(e, "station", x.station);
        if (auto it = e.find("ingredients"); it != e.end() && it->is_array()) {
            std::vector<Ingredient> v;
            for (auto& i : *it) {
                if (i.is_array() && i.size() == 2 && i[0].is_string() && i[1].is_number_integer()) v.push_back({i[0].get<std::string>(), i[1].get<int>()});
                else r.warn("an ingredient is not [item, count]");
            }
            x.ingredients = std::move(v);
        }
        r.get(e, "skills", x.skills);
        r.get(e, "result", x.result);
        r.get(e, "label", x.label);
        r.get(e, "description", x.description);
        r.get(e, "icon", x.icon);
        r.get(e, "mesh", x.mesh);
        r.get(e, "value", x.value);
        r.get(e, "learn", x.learn);
        get_props(r, e, x.properties);
        r.get(e, "notes", x.notes);
        return x;
    });
    read_list(j, "npc_templates", p.npc_templates, warnings, [](const Reader& r, const json& e) {
        NpcTemplateDef x;
        r.get_uid(e, x.uid);
        r.get_mode(e, x.mode);
        r.get(e, "name", x.name);
        r.get(e, "base", x.base);
        r.get(e, "label", x.label);
        r.get(e, "mesh", x.mesh);
        r.get(e, "hp_max", x.hp_max);
        r.get(e, "hp", x.hp);
        r.get(e, "hp_mult", x.hp_mult);
        r.get(e, "mana_max", x.mana_max);
        r.get(e, "mana", x.mana);
        r.get(e, "mana_mult", x.mana_mult);
        r.get(e, "level", x.level);
        r.get(e, "level_add", x.level_add);
        r.get(e, "level_mult", x.level_mult);
        r.get(e, "xp", x.xp);
        r.get(e, "damage", x.damage);
        r.get(e, "damage_mult", x.damage_mult);
        r.get(e, "damage_type", x.damage_type);
        r.get(e, "skills", x.skills);
        r.get(e, "guild", x.guild);
        r.get(e, "species", x.species);
        r.get(e, "attitude", x.attitude);
        r.get(e, "bravery", x.bravery);
        r.get(e, "damage_calc", x.damage_calc);
        r.get(e, "combat_species", x.combat_species);
        r.get(e, "ai_group", x.ai_group);
        r.get(e, "loot", x.loot);
        get_props(r, e, x.properties);
        r.get(e, "notes", x.notes);
        return x;
    });
    read_list(j, "npcs", p.npcs, warnings, [](const Reader& r, const json& e) {
        PlacedNpc x;
        r.get_uid(e, x.uid);
        r.get(e, "name", x.name);
        r.get(e, "template", x.template_name);
        get_vec3(r, e, "position", x.position);
        r.get(e, "yaw", x.yaw);
        r.get(e, "label", x.label);
        r.get(e, "peaceful", x.peaceful);
        r.get(e, "guild", x.guild);
        r.get(e, "level", x.level);
        r.get(e, "hp_max", x.hp_max);
        get_inv_items(r, e, "items", x.items);
        r.get(e, "routine", x.routine);
        r.get(e, "respawn", x.respawn);
        r.get(e, "version", x.version);
        r.get(e, "appear_when", x.appear_when);
        r.get(e, "leave_when", x.leave_when);
        r.get(e, "notes", x.notes);
        return x;
    });
    read_list(j, "dialogs", p.dialogs, warnings, [](const Reader& r, const json& e) {
        DialogDef x;
        r.get_uid(e, x.uid);
        r.get(e, "name", x.name);
        r.get(e, "owner", x.owner);
        get_graph(r, e, x.graph);
        r.get(e, "notes", x.notes);
        return x;
    });
    read_list(j, "quests", p.quests, warnings, [](const Reader& r, const json& e) {
        QuestDef x;
        r.get_uid(e, x.uid);
        r.get(e, "name", x.name);
        r.get(e, "title", x.title);
        r.get(e, "type", x.type);
        r.get(e, "xp", x.xp);
        r.get(e, "silent", x.silent);
        get_graph(r, e, x.graph);
        r.get(e, "notes", x.notes);
        return x;
    });
    if (auto w = j.find("world"); w != j.end() && w->is_object()) {
        Reader r{warnings, "world"};
        r.get(*w, "world", p.world.world);
        read_list(*w, "spawns", p.world.spawns, warnings, [](const Reader& r2, const json& e) {
            WorldSpawn s;
            r2.get_uid(e, s.uid);
            r2.get(e, "template", s.template_name);
            get_vec3(r2, e, "position", s.position);
            get_vec3(r2, e, "rotation", s.rotation);
            r2.get(e, "amount", s.amount);
            r2.get(e, "notes", s.notes);
            return s;
        });
        read_list(*w, "containers", p.world.containers, warnings, [](const Reader& r2, const json& e) {
            ContainerEdit c;
            r2.get_uid(e, c.uid);
            r2.get(e, "guid", c.guid);
            r2.get(e, "entity_name", c.entity_name);
            r2.get(e, "layer", c.layer);
            get_inv_items(r2, e, "items", c.items);
            return c;
        });
        read_list(*w, "removals", p.world.removals, warnings, [](const Reader& r2, const json& e) { return entity_ref_from(r2, e); });
        read_list(*w, "moves", p.world.moves, warnings, [](const Reader& r2, const json& e) { return entity_ref_from(r2, e); });
    }
    read_list(j, "assets", p.assets, warnings, [](const Reader& r, const json& e) {
        AssetFile a;
        r.get_uid(e, a.uid);
        r.get(e, "file", a.file);
        r.get(e, "kind", a.kind);
        return a;
    });
    if (auto e = j.find("editor"); e != j.end() && e->is_object()) p.editor = *e;
    return p;
}

std::string dump_project(const Project& p) { return to_json(p).dump(2) + "\n"; }

Project load_project_file(const std::string& path, std::vector<std::string>* warnings) {
    const std::string text = read_text(u8(path));
    json j;
    try {
        j = json::parse(text);
    } catch (const std::exception& e) {
        throw std::runtime_error(path + ": not valid JSON: " + e.what());
    }
    return project_from_json(j, warnings);
}

void save_project_file(const Project& p, const std::string& path) { write_text(u8(path), dump_project(p)); }

}  // namespace mk
