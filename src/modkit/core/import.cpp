#include "core/import.h"
#include "core/events.h"

#include <luajit/lua.hpp>
#include <toml++/toml.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <functional>
#include <set>
#include <stdexcept>

#include "core/assets.h"
#include "core/export.h"
#include "core/graph.h"
#include "core/paths.h"
#include "pack2mod/util.h"

namespace mk {

// ---- Lua tables -> JSON ---------------------------------------------------------------------------------------------------------------------------------
namespace {

void count_hook(lua_State* L, lua_Debug*) { luaL_error(L, "the data file runs too long (not plain data?)"); }

json to_json_value(lua_State* L, int idx, int depth) {
    if (depth > 64) throw std::runtime_error("tables nested too deep");
    switch (lua_type(L, idx)) {
        case LUA_TNIL: return nullptr;
        case LUA_TBOOLEAN: return lua_toboolean(L, idx) != 0;
        case LUA_TNUMBER: {
            const double d = lua_tonumber(L, idx);
            if (std::floor(d) == d && std::fabs(d) < 9e15) return static_cast<long long>(d);
            return d;
        }
        case LUA_TSTRING: {
            size_t n = 0;
            const char* s = lua_tolstring(L, idx, &n);
            return std::string(s, n);
        }
        case LUA_TTABLE: {
            idx = idx < 0 ? lua_gettop(L) + idx + 1 : idx;
            // a list when the keys are exactly 1..n
            const size_t n = lua_objlen(L, idx);
            size_t keys = 0;
            bool only_ints = true;
            lua_pushnil(L);
            while (lua_next(L, idx)) {
                ++keys;
                if (lua_type(L, -2) != LUA_TNUMBER) only_ints = false;
                lua_pop(L, 1);
            }
            if (only_ints && keys == n && n > 0) {
                json a = json::array();
                for (size_t i = 1; i <= n; ++i) {
                    lua_rawgeti(L, idx, static_cast<int>(i));
                    a.push_back(to_json_value(L, -1, depth + 1));
                    lua_pop(L, 1);
                }
                return a;
            }
            // keys in sorted order (Lua has none): deterministic results
            std::map<std::string, json> m;
            lua_pushnil(L);
            while (lua_next(L, idx)) {
                std::string k;
                if (lua_type(L, -2) == LUA_TSTRING) k = lua_tostring(L, -2);
                else if (lua_type(L, -2) == LUA_TNUMBER) k = std::to_string(static_cast<long long>(lua_tonumber(L, -2)));
                else throw std::runtime_error("a table key that is no string or number");
                m[k] = to_json_value(L, -1, depth + 1);
                lua_pop(L, 1);
            }
            json o = json::object();
            for (auto& [k, v] : m) o[k] = v;
            return o;
        }
        default: throw std::runtime_error(std::string("a value of type ") + lua_typename(L, lua_type(L, idx)) + " in a data file");
    }
}

}  // namespace

json lua_data_to_json(const std::string& chunk, const std::string& name) {
    lua_State* L = luaL_newstate();  // no libraries: a data file needs none
    if (!L) throw std::runtime_error("no Lua state");
    struct Close {
        lua_State* L;
        ~Close() { lua_close(L); }
    } close{L};
    if (chunk.size() >= 4 && chunk[0] == '\x1b') throw std::runtime_error(name + ": bytecode is not read");
    if (luaL_loadbuffer(L, chunk.data(), chunk.size(), name.c_str()) != 0) throw std::runtime_error(lua_tostring(L, -1));
    lua_sethook(L, count_hook, LUA_MASKCOUNT, 5'000'000);
    if (lua_pcall(L, 0, 1, 0) != 0) throw std::runtime_error(lua_tostring(L, -1));
    if (!lua_istable(L, -1)) throw std::runtime_error(name + " does not return a table");
    return to_json_value(L, -1, 0);
}

// ---- data -> project ------------------------------------------------------------------------------------------------------------------------------------
namespace {

struct Ctx {
    Project& p;
    std::vector<std::string>& warn;
    std::map<std::string, Text> strings;  // id -> per language (strings/*.csv)
    std::set<std::string> languages;
};

std::string code_language(const std::string& code) {
    static const std::map<std::string, std::string> m = {{"en", "English"}, {"de", "German"}, {"fr", "French"}, {"it", "Italian"},
                                                         {"es", "Spanish"}, {"pl", "Polish"}, {"ru", "Russian"}};
    auto it = m.find(code);
    return it == m.end() ? code : it->second;
}

// a text of the export: "x", { English = .., German = .. } or (dialogue lines) { default = .., de = .. }
Text text_of(Ctx& c, const json& v) {
    Text t;
    const std::string first = c.p.mod.languages.empty() ? std::string("English") : c.p.mod.languages.front();
    if (v.is_string()) {
        t[first] = v.get<std::string>();
    } else if (v.is_object()) {
        for (auto& [k, s] : v.items()) {
            if (!s.is_string()) continue;
            const std::string lang = k == "default" ? first : code_language(k);
            t[lang] = s.get<std::string>();
            c.languages.insert(lang);
        }
    }
    return t;
}

// a line's sound ("sounds/x.ogg" or { default = ..., de = ... }) -> node data { default = "x.ogg", German = ... } (references by file name)
json sound_json(const json& v) {
    json o = json::object();
    if (v.is_string()) o["default"] = p2m::bare_name(v.get<std::string>());
    else if (v.is_object())
        for (auto& [k, s] : v.items())
            if (s.is_string()) o[k == "default" ? k : code_language(k)] = p2m::bare_name(s.get<std::string>());
    return o;
}

json text_json(const Text& t) {
    json o = json::object();
    for (auto& [k, v] : t) o[k] = v;
    return o;
}

template <class T>
void opt(const json& e, const char* k, std::optional<T>& out) {
    if (e.contains(k) && !e[k].is_null()) out = e[k].get<T>();
}

IntMap int_map(const json& v) {
    IntMap m;
    if (v.is_object())
        for (auto& [k, x] : v.items())
            if (x.is_number()) m[k] = x.get<int>();
    return m;
}

PropEdits props_of(const json& e) {
    PropEdits p;
    if (e.contains("properties") && e["properties"].is_object())
        for (auto& [set, list] : e["properties"].items())
            if (list.is_object())
                for (auto& [k, v] : list.items()) p[set][k] = v;
    return p;
}

// a property the exporter writes for a named setting of a shipped template: lift it back
template <class T>
void lift(PropEdits& p, const char* set, const char* name, std::optional<T>& out) {
    auto s = p.find(set);
    if (s == p.end()) return;
    auto it = s->second.find(name);
    if (it == s->second.end()) return;
    out = it->second.template get<T>();
    s->second.erase(it);
    if (s->second.empty()) p.erase(s);
}

std::vector<Ingredient> ingredients_of(const json& v) {
    std::vector<Ingredient> out;
    if (v.is_array()) {
        for (auto& e : v) {
            if (e.is_object()) out.push_back({e.value("item", std::string()), e.value("count", 1)});
            else if (e.is_string()) out.push_back({e.get<std::string>(), 1});
        }
    } else if (v.is_object()) {
        for (auto& [k, n] : v.items()) out.push_back({k, n.is_number() ? n.get<int>() : 1});
    }
    return out;
}

void import_balance(Ctx& c, const json& b) {
    static const std::set<std::string> known = {"items", "new_items", "item_groups", "recipes", "new_recipes", "npcs", "new_npcs", "spells", "new_spells", "npc_groups"};
    for (auto& [k, v] : b.items())
        if (!known.count(k)) {
            c.p.extra_balance[k] = v;
            c.warn.push_back("balance section '" + k + "' has no panel in the editor: it is kept and exported again unchanged");
        }
    auto label = [&](const std::string& id) {
        auto it = c.strings.find(id);
        return it == c.strings.end() ? Text{} : it->second;
    };
    auto item_fields = [&](ItemDef& d, const json& e) {
        opt(e, "value", d.value);
        opt(e, "damage", d.damage);
        opt(e, "mana_cost", d.mana_cost);
        if (e.contains("requires")) d.requirements = int_map(e["requires"]);
        if (e.contains("modifies")) d.modifies = int_map(e["modifies"]);
        opt(e, "use_type", d.use_type);
        opt(e, "category", d.category);
        opt(e, "mesh", d.mesh);
        opt(e, "icon", d.icon);
        opt(e, "material", d.material);
        d.properties = props_of(e);
    };
    if (b.contains("items"))
        for (auto& [name, e] : b["items"].items()) {
            ItemDef d;
            d.uid = new_uid();
            d.mode = Mode::Edit;
            d.name = name;
            item_fields(d, e);
            lift(d.properties, "eCMesh_PS", "MeshFileName", d.mesh);
            lift(d.properties, "gCItem_PS", "IconImage", d.icon);
            lift(d.properties, "eCMesh_PS", "MaterialSwitch", d.material);
            d.label = label("FO_" + name);
            d.description = label("ITEMDESC_" + name);
            c.p.items.push_back(d);
        }
    if (b.contains("new_items"))
        for (auto& [name, e] : b["new_items"].items()) {
            ItemDef d;
            d.uid = new_uid();
            d.name = name;
            d.base = e.value("base", std::string());
            if (e.contains("name")) d.label = text_of(c, e["name"]);
            if (e.contains("description")) d.description = text_of(c, e["description"]);
            item_fields(d, e);
            c.p.items.push_back(d);
        }
    // the export writes edits before new ones, both by name: keep that order
    if (b.contains("item_groups"))
        for (auto& e : b["item_groups"]) {
            ItemGroup g;
            g.uid = new_uid();
            g.group = e.value("group", std::string());
            g.match = e.value("match", std::string());
            g.exclude = e.value("exclude", std::string());
            if (e.contains("names")) g.names = e["names"].get<std::vector<std::string>>();
            opt(e, "value_mult", g.value_mult);
            opt(e, "damage_mult", g.damage_mult);
            c.p.item_groups.push_back(g);
        }
    // spells: the settings the editor has fields for; anything else of an entry (per-form tables, projectile_properties) is kept in `extra`
    auto spell_fields = [&](SpellDef& d, const json& e, bool fresh) {
        static const std::set<std::string> typed = {"mana_cost", "mana_mult", "scroll_mana_cost", "orb_mana_cost", "requires", "consume", "damage", "damage_mult", "damage_type",
                                                    "speed", "speed_mult", "radius", "radius_mult", "effect", "properties"};
        static const std::set<std::string> fresh_keys = {"base", "forms", "name", "description", "icon", "value"};
        opt(e, "mana_cost", d.mana_cost);
        opt(e, "mana_mult", d.mana_mult);
        opt(e, "scroll_mana_cost", d.scroll_mana_cost);
        opt(e, "orb_mana_cost", d.orb_mana_cost);
        if (e.contains("requires")) d.requirements = int_map(e["requires"]);
        opt(e, "consume", d.consume);
        if (e.contains("damage")) d.damage = e["damage"];
        opt(e, "damage_mult", d.damage_mult);
        opt(e, "damage_type", d.damage_type);
        opt(e, "speed", d.speed);
        opt(e, "speed_mult", d.speed_mult);
        opt(e, "radius", d.radius);
        opt(e, "radius_mult", d.radius_mult);
        opt(e, "effect", d.effect);
        d.properties = props_of(e);
        for (auto& [k, v] : e.items())
            if (!typed.count(k) && !(fresh && fresh_keys.count(k))) d.extra[k] = v;
    };
    if (b.contains("spells"))
        for (auto& [name, e] : b["spells"].items()) {
            SpellDef d;
            d.uid = new_uid();
            d.mode = Mode::Edit;
            d.name = name;
            spell_fields(d, e, false);
            c.p.spells.push_back(d);
        }
    if (b.contains("new_spells"))
        for (auto& [name, e] : b["new_spells"].items()) {
            SpellDef d;
            d.uid = new_uid();
            d.mode = Mode::New;
            d.name = name;
            d.base = e.value("base", std::string());
            if (e.contains("forms") && e["forms"].is_array())
                for (auto& f : e["forms"])
                    if (f.is_string()) d.forms.push_back(f.get<std::string>());
            if (e.contains("name")) d.label = text_of(c, e["name"]);
            if (e.contains("description")) d.description = text_of(c, e["description"]);
            opt(e, "icon", d.icon);
            opt(e, "value", d.value);
            spell_fields(d, e, true);
            c.p.spells.push_back(d);
        }
    if (b.contains("npc_groups"))
        for (auto& e : b["npc_groups"]) {
            static const std::set<std::string> typed = {"group", "match", "exclude", "names", "hp_mult", "mana_mult", "level_mult", "level_add", "xp_mult", "damage_mult", "skills_mult",
                                                        "skills_add", "guild", "species", "attitude", "bravery", "damage_calc", "ai_group"};
            NpcGroup g;
            g.uid = new_uid();
            g.group = e.value("group", std::string());
            g.match = e.value("match", std::string());
            g.exclude = e.value("exclude", std::string());
            if (e.contains("names") && e["names"].is_array())
                for (auto& n : e["names"])
                    if (n.is_string()) g.names.push_back(n.get<std::string>());
            opt(e, "hp_mult", g.hp_mult);
            opt(e, "mana_mult", g.mana_mult);
            opt(e, "level_mult", g.level_mult);
            opt(e, "level_add", g.level_add);
            opt(e, "xp_mult", g.xp_mult);
            opt(e, "damage_mult", g.damage_mult);
            if (e.contains("skills_mult") && e["skills_mult"].is_object()) {
                std::map<std::string, double> m;
                for (auto& [k, v] : e["skills_mult"].items())
                    if (v.is_number()) m[k] = v.get<double>();
                g.skills_mult = m;
            }
            if (e.contains("skills_add")) g.skills_add = int_map(e["skills_add"]);
            opt(e, "guild", g.guild);
            opt(e, "species", g.species);
            opt(e, "attitude", g.attitude);
            opt(e, "bravery", g.bravery);
            opt(e, "damage_calc", g.damage_calc);
            opt(e, "ai_group", g.ai_group);
            for (auto& [k, v] : e.items())
                if (!typed.count(k)) g.extra[k] = v;
            c.p.npc_groups.push_back(g);
        }
    if (b.contains("recipes"))
        for (auto& [name, e] : b["recipes"].items()) {
            RecipeDef r;
            r.uid = new_uid();
            r.mode = Mode::Edit;
            r.name = name;
            r.station = e.value("station", std::string());
            if (e.contains("ingredients")) r.ingredients = ingredients_of(e["ingredients"]);
            if (e.contains("skills")) r.skills = int_map(e["skills"]);
            opt(e, "result", r.result);
            r.properties = props_of(e);
            lift(r.properties, "gCItem_PS", "IconImage", r.icon);
            lift(r.properties, "eCMesh_PS", "MeshFileName", r.mesh);
            lift(r.properties, "gCItem_PS", "GoldValue", r.value);
            r.label = label("FO_" + name);
            r.description = label("ITEMDESC_" + name);
            c.p.recipes.push_back(r);
        }
    if (b.contains("new_recipes"))
        for (auto& [name, e] : b["new_recipes"].items()) {
            RecipeDef r;
            r.uid = new_uid();
            r.name = name;
            r.base = e.value("base", std::string());
            r.station = e.value("station", std::string());
            r.ingredients = ingredients_of(e.value("ingredients", json::array()));
            if (e.contains("skills")) r.skills = int_map(e["skills"]);
            opt(e, "result", r.result);
            if (e.contains("name")) r.label = text_of(c, e["name"]);
            if (e.contains("description")) r.description = text_of(c, e["description"]);
            opt(e, "icon", r.icon);
            opt(e, "mesh", r.mesh);
            opt(e, "value", r.value);
            r.learn = e.value("learn", false);
            r.properties = props_of(e);
            c.p.recipes.push_back(r);
        }
    auto npc_fields = [&](NpcTemplateDef& n, const json& e) {
        opt(e, "hp_max", n.hp_max);
        opt(e, "hp", n.hp);
        opt(e, "hp_mult", n.hp_mult);
        opt(e, "mana_max", n.mana_max);
        opt(e, "mana", n.mana);
        opt(e, "mana_mult", n.mana_mult);
        opt(e, "level", n.level);
        opt(e, "level_add", n.level_add);
        opt(e, "level_mult", n.level_mult);
        opt(e, "xp", n.xp);
        opt(e, "damage", n.damage);
        opt(e, "damage_mult", n.damage_mult);
        opt(e, "damage_type", n.damage_type);
        if (e.contains("skills")) n.skills = int_map(e["skills"]);
        opt(e, "guild", n.guild);
        opt(e, "species", n.species);
        opt(e, "attitude", n.attitude);
        opt(e, "bravery", n.bravery);
        opt(e, "damage_calc", n.damage_calc);
        opt(e, "combat_species", n.combat_species);
        opt(e, "ai_group", n.ai_group);
        if (e.contains("loot")) n.loot = int_map(e["loot"]);
        n.properties = props_of(e);
    };
    if (b.contains("npcs"))
        for (auto& [name, e] : b["npcs"].items()) {
            NpcTemplateDef n;
            n.uid = new_uid();
            n.mode = Mode::Edit;
            n.name = name;
            npc_fields(n, e);
            lift(n.properties, "eCAnimation_PS", "ResourceFilePath", n.mesh);
            n.label = label("FO_" + name);
            c.p.npc_templates.push_back(n);
        }
    if (b.contains("new_npcs"))
        for (auto& [name, e] : b["new_npcs"].items()) {
            NpcTemplateDef n;
            n.uid = new_uid();
            n.name = name;
            n.base = e.value("base", std::string());
            if (e.contains("name")) n.label = text_of(c, e["name"]);
            opt(e, "mesh", n.mesh);
            npc_fields(n, e);
            c.p.npc_templates.push_back(n);
        }
}

void import_npcs(Ctx& c, const json& list) {
    for (auto& e : list) {
        PlacedNpc n;
        n.uid = new_uid();
        n.name = e.value("name", std::string());
        n.template_name = e.value("template", std::string());
        const json& pos = e.contains("offset") ? e["offset"] : e.value("position", json::array({0, 0, 0}));
        n.offset = e.contains("offset");
        for (size_t i = 0; i < 3 && i < pos.size(); ++i) n.position[i] = pos[i].get<double>();
        n.peaceful = e.value("peaceful", true);
        n.world = e.value("world", std::string());
        opt(e, "guild", n.guild);
        opt(e, "level", n.level);
        opt(e, "hp_max", n.hp_max);
        std::set<std::string> equip;
        if (e.contains("equip"))
            for (auto& x : e["equip"]) equip.insert(x.get<std::string>());
        if (e.contains("items"))
            for (auto& it : e["items"])
                if (it.is_array() && !it.empty()) {
                    const std::string item = it[0].get<std::string>();
                    n.items.push_back({item, it.size() > 1 ? it[1].get<int>() : 1, equip.count(item) != 0});
                }
        if (e.contains("routine")) n.routine = e["routine"];
        n.respawn = e.value("respawn", false);
        n.version = e.value("version", 1);
        if (e.contains("appear_when")) n.appear_when = e["appear_when"];
        if (e.contains("leave_when")) n.leave_when = e["leave_when"];
        if (auto it = c.strings.find("FO_" + n.name); it != c.strings.end()) n.label = it->second;
        c.p.npcs.push_back(n);
    }
}

// a condition (data form) as nodes: returns the uid of the node whose "out" carries it
std::string condition_nodes(Graph& g, const json& cd, double x, double& y) {
    const std::string k = cd.is_array() && !cd.empty() && cd[0].is_string() ? cd[0].get<std::string>() : std::string();
    if (k == "all" || k == "any" || k == "not") {
        GraphNode n = make_node(GraphKind::Dialog, k, x, y);
        g.nodes.push_back(n);
        const json list = k == "not" ? json::array({cd.size() > 1 ? cd[1] : json()}) : (cd.size() > 1 ? cd[1] : json::array());
        for (auto& sub : list) {
            const std::string s = condition_nodes(g, sub, x - 300, y);
            add_link(GraphKind::Dialog, g, s, "out", n.uid, "in");
        }
        return n.uid;
    }
    GraphNode n = make_node(GraphKind::Dialog, "cond", x, y);
    y += 130;
    n.data["kind"] = k;
    json a = json::object();
    auto at = [&](size_t i) { return i < cd.size() ? cd[i] : json(); };
    auto set = [&](const char* key, const json& v) {
        if (!v.is_null()) a[key] = v;
    };
    if (k == "quest") set("quest", at(1)), set("status", at(2));
    else if (k == "item") set("item", at(1)), set("count", at(2)), set("holder", at(3));
    else if (k == "var") set("name", at(1)), set("value", at(2)), set("op", at(3));
    else if (k == "event") set("name", at(1));
    else if (k == "skill") set("skill", at(1)), set("value", at(2)), set("op", at(3)), set("who", at(4));
    else if (k == "guild") set("guild", at(1)), set("who", at(2));
    else if (k == "dead" || k == "alive") set("who", at(1));
    n.data["args"] = a;
    g.nodes.push_back(n);
    return n.uid;
}

void import_dialogs(Ctx& c, const json& d) {
    const json infos = d.value("infos", json::array());
    std::map<std::string, std::pair<std::string, std::string>> actions;  // script -> quest, action
    for (auto& a : d.value("quest_actions", json::array())) actions[a.value("script", std::string())] = {a.value("quest", std::string()), a.value("action", std::string())};
    std::map<std::string, DialogDef*> by_owner;
    std::map<std::string, std::pair<DialogDef*, std::string>> option_of;  // info name -> dialogue, node uid
    std::map<DialogDef*, double> ys;
    for (auto& info : infos) {
        const std::string owner = info.value("owner", std::string());
        DialogDef*& dd = by_owner[owner];
        if (!dd) {
            DialogDef nd;
            nd.uid = new_uid();
            nd.name = owner.empty() ? std::string("Dialogue") : owner;
            nd.owner = owner;
            c.p.dialogs.push_back(nd);
            dd = &c.p.dialogs.back();
            // pointers into the vector: re-resolve below after growing
        }
    }
    // the vector is complete now: resolve the pointers again
    by_owner.clear();
    for (auto& dd : c.p.dialogs) by_owner[dd.owner] = &dd;
    for (auto& info : infos) {
        DialogDef* dd = by_owner[info.value("owner", std::string())];
        Graph& g = dd->graph;
        double& y = ys[dd];
        const std::string name = info.value("name", std::string());
        const std::string parent = info.value("parent", std::string());
        const double x = parent.empty() ? 0 : 400;
        GraphNode opt_node = make_node(GraphKind::Dialog, "option", x, y);
        opt_node.data["info"] = name;
        opt_node.data["type"] = info.value("type", std::string("Info"));
        opt_node.data["permanent"] = info.value("permanent", false);
        const json lines = info.value("lines", json::array());
        size_t first_line = 0;
        if (!lines.empty() && lines[0].value("speaker", std::string()) == "player" && lines[0].contains("text")) {
            opt_node.data["text"] = text_json(text_of(c, lines[0]["text"]));
            if (lines[0].contains("sound")) opt_node.data["sound"] = sound_json(lines[0]["sound"]);
            first_line = 1;
        }
        g.nodes.push_back(opt_node);
        const std::string ouid = opt_node.uid;
        option_of[name] = {dd, ouid};
        if (!parent.empty()) {
            auto it = option_of.find(parent);
            if (it != option_of.end() && it->second.first == dd) add_link(GraphKind::Dialog, g, it->second.second, "choices", ouid, "parent");
            else c.warn.push_back("option " + name + ": the parent " + parent + " is not in the same dialogue");
        }
        double cy = y + 60;
        if (info.contains("when")) {
            const json& w = info["when"];
            // several conditions into "when" are the exporter's "all"
            if (w.is_array() && !w.empty() && w[0] == "all" && w.size() > 1) {
                for (auto& sub : w[1]) add_link(GraphKind::Dialog, g, condition_nodes(g, sub, x - 340, cy), "out", ouid, "when");
            } else {
                add_link(GraphKind::Dialog, g, condition_nodes(g, w, x - 340, cy), "out", ouid, "when");
            }
        }
        std::string prev = ouid, prev_pin = "then";
        double lx = x + 330;
        for (size_t i = first_line; i < lines.size(); ++i) {
            const json& l = lines[i];
            GraphNode n;
            if (l.contains("command")) {
                const std::string cmd = l["command"].get<std::string>();
                if (cmd == "End") {
                    n = make_node(GraphKind::Dialog, "end", lx, y);
                } else {
                    n = make_node(GraphKind::Dialog, "command", lx, y);
                    n.data["command"] = cmd;
                    json params = json::object();
                    for (auto& [k, v] : l.items())
                        if (k != "command" && k != "text" && k != "sound") params[k] = v;
                    n.data["params"] = params;
                    if (l.contains("sound")) n.data["sound"] = sound_json(l["sound"]);
                    if (l.contains("text")) n.data["text"] = text_json(text_of(c, l["text"]));
                }
            } else if (l.contains("script")) {
                const std::string s = l["script"].get<std::string>();
                if (auto it = actions.find(s); it != actions.end()) {
                    n = make_node(GraphKind::Dialog, "quest", lx, y);
                    n.data["quest"] = it->second.first;
                    n.data["action"] = it->second.second;
                } else {
                    n = make_node(GraphKind::Dialog, "script", lx, y);
                    n.data["script"] = s;
                }
            } else if (l.contains("log")) {
                n = make_node(GraphKind::Dialog, "log", lx, y);
                n.data["quest"] = l["log"].value("quest", std::string());
                n.data["text"] = text_json(text_of(c, l["log"].value("text", json())));
            } else {
                n = make_node(GraphKind::Dialog, "line", lx, y);
                n.data["speaker"] = l.value("speaker", std::string("npc"));
                n.data["text"] = text_json(text_of(c, l.value("text", json())));
                if (l.contains("sound")) n.data["sound"] = sound_json(l["sound"]);
            }
            g.nodes.push_back(n);
            add_link(GraphKind::Dialog, g, prev, prev_pin, n.uid, "in");
            prev = n.uid;
            prev_pin = "next";
            lx += 330;
        }
        y = std::max(y + 220, cy + 40);
    }
}

void import_quests(Ctx& c, const json& list) {
    for (auto& e : list) {
        QuestDef q;
        q.uid = new_uid();
        q.name = e.value("name", std::string());
        if (e.contains("title")) q.title = text_of(c, e["title"]);
        q.type = e.value("type", std::string("Report"));
        opt(e, "xp", q.xp);
        q.silent = e.value("silent", false);
        if (e.contains("flow")) {
            const json& f = e["flow"];
            double x = 0;
            GraphNode s = make_node(GraphKind::Quest, "start", x, 0);
            const json t = f.value("trigger", json::object());
            s.data = json::object();
            s.data["trigger"] = t.value("kind", std::string("dialog"));
            for (auto& [k, v] : t.items())
                if (k != "kind") s.data[k == "target" ? "target" : k] = v;
            q.graph.nodes.push_back(s);
            // the flat step list back into a graph: a `race` step becomes an Either / or node whose paths run on below each other, a `stop` ends a path
            const json steps = f.value("steps", json::array());
            std::function<double(size_t, const std::string&, const std::string&, double, double)> chain = [&](size_t i, const std::string& prev, const std::string& prev_pin, double x,
                                                                                                               double y) -> double {
                std::string from = prev, pin = prev_pin;
                double ymax = y;
                for (; i >= 1 && i <= steps.size(); ++i) {
                    const json& st = steps[i - 1];
                    const std::string kind = st.value("kind", std::string());
                    if (kind == "stop") break;
                    x += 300;
                    if (kind == "race") {
                        GraphNode b = make_node(GraphKind::Quest, "branch", x, y);
                        q.graph.nodes.push_back(b);
                        add_link(GraphKind::Quest, q.graph, from, pin, b.uid, "in");
                        double py = y;
                        for (auto& pi : st.value("paths", json::array())) {
                            if (!pi.is_number()) continue;
                            ymax = std::max(ymax, chain(static_cast<size_t>(pi.get<double>()), b.uid, "next", x, py));
                            py = ymax + 220;
                        }
                        return ymax;
                    }
                    GraphNode n = make_node(GraphKind::Quest, kind == "objective" || kind == "log" || kind == "reward" || kind == "outcome" ? kind : "log", x, y);
                    n.data = json::object();
                    if (kind == "objective") {
                        n.data["kind"] = st.value("objective", std::string("items"));
                        for (auto& [k, v] : st.items())
                            if (k != "kind" && k != "objective" && k != "log") n.data[k] = v;
                        if (st.contains("log")) n.data["text"] = text_json(text_of(c, st["log"]));
                    } else if (kind == "log") {
                        n.data["text"] = text_json(text_of(c, st.value("text", json())));
                    } else if (kind == "reward") {
                        for (auto& [k, v] : st.items())
                            if (k != "kind") n.data[k] = v;
                    } else if (kind == "outcome") {
                        n.data["result"] = st.value("result", std::string("success"));
                        if (st.contains("log")) n.data["text"] = text_json(text_of(c, st["log"]));
                        if (st.contains("next_quest")) n.data["next_quest"] = st["next_quest"];
                    }
                    q.graph.nodes.push_back(n);
                    add_link(GraphKind::Quest, q.graph, from, pin, n.uid, "in");
                    from = n.uid;
                    pin = "next";
                    if (kind == "outcome") break;  // an Outcome ends its path: the next step in the list belongs to another one
                }
                return ymax;
            };
            chain(1, s.uid, "next", x, 0);
        }
        c.p.quests.push_back(q);
    }
}

// strings/<file>.csv: id;"text" rows (quoted fields may hold ;, "" and new lines)
void read_csv(Ctx& c, const std::string& text, const std::string& language) {
    size_t i = 0;
    const size_t n = text.size();
    while (i < n) {
        if (text[i] == '#') {
            while (i < n && text[i] != '\n') ++i;
            ++i;
            continue;
        }
        std::vector<std::string> fields(1);
        bool quoted = false;
        for (; i < n; ++i) {
            const char ch = text[i];
            if (quoted) {
                if (ch == '"' && i + 1 < n && text[i + 1] == '"') fields.back() += '"', ++i;
                else if (ch == '"') quoted = false;
                else fields.back() += ch;
            } else if (ch == '"') {
                quoted = true;
            } else if (ch == ';') {
                fields.emplace_back();
            } else if (ch == '\n') {
                ++i;
                break;
            } else if (ch != '\r') {
                fields.back() += ch;
            }
        }
        if (fields.size() >= 2 && !fields[0].empty() && !(fields[0] == "id" && fields[1] == "text")) c.strings[fields[0]][language] = fields[1];
    }
}

}  // namespace

ImportResult import_mod(const std::string& dir, bool prefer_data) {
    ImportResult r;
    const fs::path root = u8(dir);
    std::error_code ec;
    const fs::path pj = root / "modkit" / "project.json";
    if (!prefer_data && fs::exists(pj, ec)) {
        try {
            r.project = load_project_file(u8str(pj), &r.warnings);
            r.from_project_file = true;
            r.ok = true;
        } catch (const std::exception& e) {
            r.error = e.what();
        }
        return r;
    }
    if (!fs::exists(root / "mod.toml", ec)) {
        r.error = dir + " has no mod.toml: not a mod folder";
        return r;
    }
    Project& p = r.project;
    p = Project{};
    p.mod.languages = {"English"};
    Ctx c{p, r.warnings, {}, {"English"}};
    try {
        auto t = toml::parse_file(u8str(root / "mod.toml"));
        p.mod.id = t["id"].value_or(std::string("imported_mod"));
        p.mod.name = t["name"].value_or(p.mod.id);
        p.mod.version = t["version"].value_or(std::string("1.0.0"));
        p.mod.author = t["author"].value_or(std::string());
        p.mod.description = t["description"].value_or(std::string());
        if (auto* a = t["load_after"].as_array())
            for (auto& x : *a)
                if (auto s = x.value<std::string>()) p.mod.load_after.push_back(*s);
        if (auto* a = t["requires"].as_array())
            for (auto& x : *a)
                if (auto s = x.value<std::string>()) p.mod.requires_mods.push_back(*s);
        // strings: all.csv is the first language (English unless only another one exists)
        const fs::path sd = root / "strings";
        if (fs::is_directory(sd, ec)) {
            for (auto it = fs::directory_iterator(sd, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
                const std::string stem = u8str(it->path().stem());
                if (it->path().extension() != ".csv" || stem == "all") continue;
                c.languages.insert(stem);
                read_csv(c, read_text(it->path()), stem);
            }
            if (fs::exists(sd / "all.csv", ec)) {
                Ctx tmp{p, r.warnings, {}, {}};
                read_csv(tmp, read_text(sd / "all.csv"), "English");
                for (auto& [id, t2] : tmp.strings)
                    for (auto& [l, s] : t2) c.strings[id].emplace(l, s);  // a language file wins over all.csv
            }
        }
        auto data = [&](const char* file) -> json {
            const fs::path f = root / "data" / file;
            if (!fs::exists(f, ec)) return nullptr;
            return lua_data_to_json(read_text(f), std::string("data/") + file);
        };
        // dialogue texts name their languages by code; read them first to know the project's languages
        const json balance = data("balance.lua"), npcs = data("npcs.lua"), quests = data("quests.lua"), dialogs = data("dialogs.lua");
        if (balance.is_object()) import_balance(c, balance);
        if (npcs.is_array()) import_npcs(c, npcs);
        if (quests.is_array()) import_quests(c, quests);
        if (dialogs.is_object()) import_dialogs(c, dialogs);
        if (const json events = data("events.lua"); events.is_array())
            for (auto& e : events) p.events.push_back(event_graph_from_data(e));
        const json world = data("world.lua");
        if (world.is_object()) {
            for (auto& s : world.value("spawns", json::array())) {
                WorldSpawn ws;
                ws.uid = s.value("key", std::string()) + new_uid().substr(12);  // the key is the uid's start: kept
                ws.template_name = s.value("template", std::string());
                const json pos = s.value("position", json::array({0, 0, 0}));
                for (size_t i = 0; i < 3 && i < pos.size(); ++i) ws.position[i] = pos[i].get<double>();
                const json rot = s.value("rotation", json::array({0, 0, 0}));
                for (size_t i = 0; i < 3 && i < rot.size(); ++i) ws.rotation[i] = rot[i].get<double>();
                ws.amount = s.value("amount", 1);
                ws.world = s.value("world", std::string());
                p.world.spawns.push_back(ws);
            }
            for (auto& ct : world.value("containers", json::array())) {
                ContainerEdit ce;
                ce.uid = ct.value("key", std::string()) + new_uid().substr(12);
                ce.guid = ct.value("id", std::string());
                ce.entity_name = ct.value("name", std::string());
                ce.world = ct.value("world", std::string());
                for (auto& it : ct.value("items", json::array()))
                    if (it.is_array() && !it.empty()) ce.items.push_back({it[0].get<std::string>(), it.size() > 1 ? it[1].get<int>() : 1, false});
                p.world.containers.push_back(ce);
            }
            auto vec3 = [](const json& a) {
                Vec3 v{0, 0, 0};
                for (size_t i = 0; i < 3 && i < a.size(); ++i) v[i] = a[i].get<double>();
                return v;
            };
            for (const char* section : {"removals", "moves"}) {
                for (auto& m : world.value(section, json::array())) {
                    EntityRef er;
                    er.uid = m.value("key", std::string()) + new_uid().substr(12);
                    er.guid = m.value("id", std::string());
                    er.entity_name = m.value("name", std::string());
                    er.world = m.value("world", std::string());
                    if (m.contains("position")) er.position = vec3(m["position"]);
                    if (m.contains("rotation")) er.rotation = vec3(m["rotation"]);
                    (std::string(section) == "removals" ? p.world.removals : p.world.moves).push_back(er);
                }
            }
        }
        // behaviours: the scripts stay in behaviors/ of the mod (like the asset files), the attachments go back onto their NPCs and templates
        const json behaviors = data("behaviors.lua");
        if (behaviors.is_object()) {
            for (auto& s : behaviors.value("scripts", json::array())) {
                const std::string name = s.value("name", std::string());
                if (name.empty()) continue;
                p.behaviors.push_back({new_uid(), name, u8str(fs::absolute(root / u8(behavior_module_file(name))))});
            }
            auto use = [](const json& a) {
                BehaviorUse u;
                u.behavior = a.value("behavior", std::string());
                if (a.contains("params") && a["params"].is_object()) u.params = a["params"];
                return u;
            };
            for (auto& a : behaviors.value("npcs", json::array())) {
                PlacedNpc* n = p.npc(a.value("target", std::string()));
                if (n) n->behaviors.push_back(use(a));
                else r.warnings.push_back("behaviour " + a.value("behavior", std::string()) + " on " + a.value("target", std::string()) + ": no placed NPC of that name (dropped)");
            }
            for (auto& a : behaviors.value("templates", json::array())) {
                const std::string tname = a.value("template", std::string());
                NpcTemplateDef* d = p.npc_template(tname);
                if (!d) {
                    NpcTemplateDef nt;  // a shipped template the mod only gives behaviours
                    nt.uid = new_uid();
                    nt.mode = Mode::Edit;
                    nt.name = tname;
                    p.npc_templates.push_back(nt);
                    d = &p.npc_templates.back();
                }
                d->behaviors.push_back(use(a));
            }
        }
        // the project's world is the default: entries in it keep an empty world, like the editor makes them
        auto def_world = [&](std::string& w) {
            if (w == world_of(p, "")) w.clear();
        };
        for (auto& x : p.npcs) def_world(x.world);
        for (auto& x : p.world.spawns) def_world(x.world);
        for (auto& x : p.world.containers) def_world(x.world);
        for (auto& x : p.world.removals) def_world(x.world);
        for (auto& x : p.world.moves) def_world(x.world);
        // the languages: English first, then the others in a stable order
        p.mod.languages = {"English"};
        for (auto& l : c.languages)
            if (l != "English") p.mod.languages.push_back(l);
        // asset files stay where they are (the export copies them from there): compiled resources in assets/, audio files in sounds/
        for (const char* sub : {"assets", "sounds"}) {
            const fs::path ad = root / sub;
            if (!fs::is_directory(ad, ec)) continue;
            std::vector<fs::path> files;
            for (auto it = fs::recursive_directory_iterator(ad, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
                if (it->is_regular_file()) files.push_back(it->path());
            std::sort(files.begin(), files.end());
            for (auto& f : files) p.assets.push_back({new_uid(), u8str(fs::absolute(f)), asset_kind(u8str(f.filename()))});
        }
        if (fs::exists(root / "custom.lua", ec)) r.warnings.push_back("custom.lua is not imported: keep it in the mod folder when you export there again");
        r.ok = true;
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    return r;
}

}  // namespace mk
