#include "core/pack2project.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>

#include "core/assets.h"
#include "core/export.h"
#include "core/import.h"
#include "core/lua_writer.h"
#include "core/paths.h"
#include "core/spells.h"
#include "core/validate.h"
#include "pack2mod/util.h"

namespace mk {

namespace {

using p2m::LVal;

json jv(const LVal& v) {
    switch (v.kind) {
        case LVal::Nil: return nullptr;
        case LVal::Bool: return v.b;
        case LVal::Int: return static_cast<long long>(v.i);
        case LVal::Float: return v.d;
        case LVal::Str: return v.s;
        case LVal::List: {
            json a = json::array();
            for (const LVal& x : v.items) a.push_back(jv(x));
            return a;
        }
        case LVal::Dict: {
            json o = json::object();
            for (const auto& [k, x] : v.fields) o[k] = jv(x);
            return o;
        }
    }
    return nullptr;
}

std::optional<int> as_int(const json& v) {
    if (v.is_number_integer()) return v.get<int>();
    if (v.is_number_float()) {
        const double d = v.get<double>();
        if (std::floor(d) == d && std::fabs(d) < 2e9) return static_cast<int>(d);
    }
    return std::nullopt;
}

// the layer's own aliases of the item settings (src/game/lua_balance.cpp ITEM_PROPS): `value` is gCItem_PS.GoldValue, ...
struct Alias {
    const char *key, *set, *prop;
};
constexpr Alias kItemAliases[] = {{"value", "gCItem_PS", "GoldValue"},     {"use_type", "gCItem_PS", "UseType"},          {"category", "gCItem_PS", "Category"},
                                  {"damage", "gCDamage_PS", "DamageAmount"}, {"mana_cost", "gCCastInfo_PS", "ManaCost"}, {"icon", "gCItem_PS", "IconImage"},
                                  {"mesh", "eCMesh_PS", "MeshFileName"},     {"material", "eCMesh_PS", "MaterialSwitch"}};

class Mapper {
public:
    Mapper(const p2m::ProjectInput& in, const GameData* game, Pack2ProjectReport& rep) : in_(in), game_(game), rep_(rep) {}

    Project run() {
        p_.mod.id = in_.id;
        p_.mod.name = in_.name;
        p_.mod.version = "0.1.0";
        p_.mod.author = "converted by risen-pack2mod (the author of the original mod owns the content)";
        p_.mod.description = "Editor project made by risen-pack2mod from the archives of a Risen mod: item numbers, new items and recipes, texts, loose assets. For private use, not for redistribution.";
        p_.mod.languages = in_.languages.empty() ? std::vector<std::string>{"English"} : in_.languages;
        for (auto& [lang, rows] : in_.strings)
            if (std::find(p_.mod.languages.begin(), p_.mod.languages.end(), lang) == p_.mod.languages.end()) p_.mod.languages.push_back(lang);
        p_.world.world = "World";

        const json items = jv(in_.items), new_items = jv(in_.new_items), rec_edit = jv(in_.recipes_edit), rec_new = jv(in_.recipes_new), patches = jv(in_.patches),
                   world = jv(in_.world);
        edit_items(items);
        new_items_and_recipes(new_items, rec_edit);
        new_recipes(rec_new);
        template_patches(patches);
        containers(world);
        dialog_and_teachers(jv(in_.dialog), world);
        assets();
        strings_left_over();
        return std::move(p_);
    }

private:
    const p2m::ProjectInput& in_;
    const GameData* game_;
    Pack2ProjectReport& rep_;
    Project p_;
    std::set<std::string> used_strings_;

    void unmapped(const std::string& area, const std::string& name, const std::string& reason) { rep_.unmapped.push_back({area, name, reason}); }

    // ---- texts
    // `fill`: a new template's name is one inline table of the export ({ English = ..., German = ... }) that knows no fallback, while the converted mod's
    // strings/all.csv gives every other language the English text: the fallback is made explicit
    Text text_for(const std::string& id, bool fill = false) {
        Text t;
        for (const std::string& lang : p_.mod.languages) {
            auto l = in_.strings.find(lang);
            if (l == in_.strings.end()) continue;
            auto r = l->second.find(id);
            if (r == l->second.end()) {  // string ids are matched case-insensitively by the engine (src/game/loc_overlay.cpp): so are the converted mod's rows
                const std::string want = p2m::lower(id);
                for (auto it = l->second.begin(); it != l->second.end() && r == l->second.end(); ++it)
                    if (p2m::lower(it->first) == want) r = it;
            }
            if (r != l->second.end() && !r->second.empty()) {
                t[lang] = r->second;
                used_strings_.insert(p2m::lower(r->first));
            }
        }
        if (fill && t.size() > 1) {
            auto en = t.find("English");
            if (en != t.end()) {
                const std::string text = en->second;
                for (const std::string& lang : p_.mod.languages) t.emplace(lang, text);
            }
        }
        return t;
    }

    // ---- small pieces
    static PropEdits props_of(const json& e) {
        PropEdits p;
        if (e.contains("properties") && e["properties"].is_object())
            for (auto& [set, list] : e["properties"].items())
                if (list.is_object())
                    for (auto& [k, v] : list.items()) p[set][k] = v;
        return p;
    }
    template <class T>
    static void lift(PropEdits& p, const char* set, const char* name, std::optional<T>& out) {
        auto s = p.find(set);
        if (s == p.end()) return;
        auto it = s->second.find(name);
        if (it == s->second.end()) return;
        try {
            out = it->second.template get<T>();
        } catch (const std::exception&) {
            return;  // another type than the field takes: stays a plain property
        }
        s->second.erase(it);
        if (s->second.empty()) p.erase(s);
    }
    std::optional<int> number(const json& e, const char* key, const std::string& area, const std::string& name) {
        if (!e.contains(key) || e[key].is_null()) return std::nullopt;
        if (auto v = as_int(e[key])) return v;
        unmapped(area, name, std::string(key) + " = " + e[key].dump() + " is no whole number");
        rep_.missing_settings.insert(name + "|" + key);
        return std::nullopt;
    }
    // a skill list [{skill=, amount=, mode=}] as the editor's skill -> number map; nullopt (and a report entry) when it does not fit
    std::optional<IntMap> skill_map(const json& list, bool modifier, const std::string& area, const std::string& name, const char* key) {
        IntMap m;
        std::map<std::string, std::string> mode;
        if (list.is_array()) {
            for (auto& e : list) {
                const std::string skill = e.value("skill", std::string());
                const auto amount = e.contains("amount") ? as_int(e["amount"]) : std::nullopt;
                if (skill.empty() || !amount) {
                    unmapped(area, name, std::string(key) + ": an entry without a usable skill or amount");
                    rep_.missing_settings.insert(name + "|" + key);
                    return std::nullopt;
                }
                const std::string md = e.value("mode", std::string("add"));
                if (modifier && md != "add") {
                    unmapped(area, name, std::string(key) + ": " + skill + " with the mode '" + md + "' (the editor's modifier list is skill -> amount, always added)");
                    rep_.missing_settings.insert(name + "|" + key);
                    return std::nullopt;
                }
                auto it = m.find(skill);
                if (it == m.end()) m[skill] = *amount;
                else it->second = std::max(it->second, *amount);  // the layer's skill_map keeps the highest too
            }
        } else if (list.is_object()) {
            for (auto& [k, v] : list.items())
                if (auto a = as_int(v)) m[k] = *a;
        }
        return m;
    }

    // the settings an item entry shares between edits and new items
    void item_fields(ItemDef& d, const json& e, const std::string& area) {
        d.value = number(e, "value", area, d.name);
        d.damage = number(e, "damage", area, d.name);
        d.mana_cost = number(e, "mana_cost", area, d.name);
        if (e.contains("requires"))
            if (auto m = skill_map(e["requires"], false, area, d.name, "requires")) d.requirements = *m;
        if (e.contains("modifies"))
            if (auto m = skill_map(e["modifies"], true, area, d.name, "modifies")) d.modifies = *m;
        if (e.contains("use_type") && e["use_type"].is_string()) d.use_type = e["use_type"].get<std::string>();
        if (e.contains("category") && e["category"].is_string()) d.category = e["category"].get<std::string>();
        if (e.contains("mesh") && e["mesh"].is_string()) d.mesh = e["mesh"].get<std::string>();
        if (e.contains("icon") && e["icon"].is_string()) d.icon = e["icon"].get<std::string>();
        if (auto m = number(e, "material", area, d.name)) d.material = m;
        d.properties = props_of(e);
        lift(d.properties, "eCMesh_PS", "MeshFileName", d.mesh);
        lift(d.properties, "gCItem_PS", "IconImage", d.icon);
        std::optional<int> mat;
        lift(d.properties, "eCMesh_PS", "MaterialSwitch", mat);
        if (mat) d.material = mat;
    }

    // ---- items
    void edit_items(const json& items) {
        if (!items.is_object()) return;
        for (auto& [name, e] : items.items()) {
            ItemDef d;
            d.uid = new_uid();
            d.mode = Mode::Edit;
            d.name = name;
            item_fields(d, e, "Items");
            d.label = text_for("FO_" + name);
            d.description = text_for("ITEMDESC_" + name);
            p_.items.push_back(d);
            rep_.counts["items changed"]++;
        }
    }

    // ---- new items; a new item that is a recipe item (the converter lists its recipe in `recipes.edit`) becomes one new recipe of the editor
    void new_items_and_recipes(const json& new_items, const json& rec_edit) {
        if (!new_items.is_object()) return;
        for (auto& [name, e] : new_items.items()) {
            const bool is_recipe = rec_edit.is_object() && rec_edit.contains(name);
            if (is_recipe) {
                recipe_item(name, e, rec_edit[name]);
                continue;
            }
            ItemDef d;
            d.uid = new_uid();
            d.name = name;
            d.base = e.value("base", std::string());
            item_fields(d, e, "Items");
            d.label = text_for("FO_" + name, true);
            d.description = text_for("ITEMDESC_" + name, true);
            p_.items.push_back(d);
            rep_.counts["items new"]++;
        }
        // recipes of items that are not among the new templates: edits of shipped recipe items
        if (rec_edit.is_object())
            for (auto& [name, r] : rec_edit.items())
                if (!new_items.contains(name)) {
                    RecipeDef d;
                    d.uid = new_uid();
                    d.mode = Mode::Edit;
                    d.name = name;
                    recipe_parts(d, r);
                    d.label = text_for("FO_" + name);
                    d.description = text_for("ITEMDESC_" + name);
                    p_.recipes.push_back(d);
                    rep_.counts["recipes edited"]++;
                }
    }

    void recipe_parts(RecipeDef& d, const json& r) {
        d.station = r.value("station", std::string());
        std::vector<Ingredient> ing;
        if (r.contains("ingredients") && r["ingredients"].is_array())
            for (auto& i : r["ingredients"]) ing.push_back({i.value("item", std::string()), as_int(i.value("count", json(1))).value_or(1)});
        d.ingredients = ing;
        if (r.contains("skills") && r["skills"].is_object()) {
            IntMap m;
            for (auto& [k, v] : r["skills"].items())
                if (auto a = as_int(v)) m[k] = *a;
            d.skills = m;
        }
        if (r.contains("result") && r["result"].is_string()) d.result = r["result"].get<std::string>();
    }

    void recipe_item(const std::string& name, const json& item, const json& rec) {
        RecipeDef d;
        d.uid = new_uid();
        d.name = name;
        d.base = item.value("base", std::string());
        recipe_parts(d, rec);
        d.label = text_for("FO_" + name, true);
        d.description = text_for("ITEMDESC_" + name, true);
        // the item's own numbers: a recipe item has icon, mesh and value fields, everything else is a plain property (the same properties the layer sets)
        ItemDef tmp;
        tmp.name = name;
        item_fields(tmp, item, "Recipes");
        d.icon = tmp.icon;
        d.mesh = tmp.mesh;
        d.value = tmp.value;
        d.properties = tmp.properties;
        auto put = [&](const char* set, const char* prop, const json& v) { d.properties[set][prop] = v; };
        if (tmp.material) put("eCMesh_PS", "MaterialSwitch", *tmp.material);
        if (tmp.damage) put("gCDamage_PS", "DamageAmount", *tmp.damage);
        if (tmp.mana_cost) put("gCCastInfo_PS", "ManaCost", *tmp.mana_cost);
        if (tmp.use_type) put("gCItem_PS", "UseType", *tmp.use_type);
        if (tmp.category) put("gCItem_PS", "Category", *tmp.category);
        if ((tmp.requirements && !tmp.requirements->empty()) || (tmp.modifies && !tmp.modifies->empty())) {
            unmapped("Recipes", name, "requirements / modifiers of a recipe item (a recipe has no such fields in the editor)");
            rep_.missing_settings.insert(name + "|requires");
            rep_.missing_settings.insert(name + "|modifies");
        }
        d.notes = "a new item template of the converted mod that is a recipe item";
        p_.recipes.push_back(d);
        rep_.counts["recipes new"]++;
    }

    void new_recipes(const json& rec_new) {
        if (!rec_new.is_object()) return;
        for (auto& [name, r] : rec_new.items()) {
            RecipeDef d;
            d.uid = new_uid();
            d.name = name;
            recipe_parts(d, r);
            d.learn = r.value("learn", false);
            if (r.contains("from")) d.notes = "the converted mod turned the shipped item " + r["from"].get<std::string>() + " into a recipe; a shipped template cannot gain a recipe at run time, so this recipe item is given to the player instead";
            p_.recipes.push_back(d);
            rep_.counts["recipes from shipped items"]++;
        }
    }

    // ---- template patches
    struct Projectile {
        std::string spell;
        size_t level = 0;
    };

    void template_patches(const json& patches) {
        if (!patches.is_object()) return;
        std::map<std::string, Projectile> projectile_of;
        for (const ShippedSpell& s : shipped_spells())
            if (s.projectile)
                for (size_t i = 0; i < s.projectiles.size(); ++i) projectile_of[p2m::lower(s.projectiles[i])] = {s.name, i};
        std::map<std::string, std::vector<std::pair<std::string, json>>> by_spell;  // spell -> patched projectile templates
        for (auto& [name, props] : patches.items()) {
            if (auto it = projectile_of.find(p2m::lower(name)); it != projectile_of.end()) {
                by_spell[it->second.spell].push_back({name, props});
                continue;
            }
            const TemplateInfo* t = game_ ? game_->tpl(name) : nullptr;
            const bool only_actor = props.is_object() && props.size() == 1 && props.contains("eCAnimation_PS");
            TplKind kind = t ? t->kind : (only_actor ? TplKind::Monster : TplKind::Other);
            if (kind == TplKind::Npc || kind == TplKind::Monster) {
                NpcTemplateDef n;
                n.uid = new_uid();
                n.mode = Mode::Edit;
                n.name = name;
                json holder = json::object({{"properties", props}});
                n.properties = props_of(holder);
                lift(n.properties, "eCAnimation_PS", "ResourceFilePath", n.mesh);
                n.label = text_for("FO_" + name);
                p_.npc_templates.push_back(n);
                rep_.counts["NPC / monster templates changed"]++;
            } else if (kind == TplKind::Item) {
                ItemDef d;
                d.uid = new_uid();
                d.mode = Mode::Edit;
                d.name = name;
                item_fields(d, json::object({{"properties", props}}), "Items");
                p_.items.push_back(d);
                rep_.counts["items changed"]++;
            } else if (kind == TplKind::Recipe) {
                RecipeDef d;
                d.uid = new_uid();
                d.mode = Mode::Edit;
                d.name = name;
                d.properties = props_of(json::object({{"properties", props}}));
                p_.recipes.push_back(d);
                rep_.counts["recipes edited"]++;
            } else {
                leave_patch(name, props, t ? std::string("a ") + kind_name(kind) + " template: the editor has no section for it" : std::string("the template is not in the game data (or no game data was given)"));
            }
        }
        for (auto& [spell, list] : by_spell) projectile_spell(spell, list);
    }

    void leave_patch(const std::string& name, const json& props, const std::string& why) {
        rep_.leftover_patches[name] = props;
        rep_.missing_templates.insert(name);
        unmapped("Template patches", name, why);
    }

    // the projectile templates of a shipped spell (It_Projectile_Frost_S0..S2) are edited through the spell's `damage` (one number or one per level) and
    // `projectile_properties` (the same for every level)
    void projectile_spell(const std::string& spell, const std::vector<std::pair<std::string, json>>& list) {
        const ShippedSpell* s = shipped_spell(spell);
        if (!s) return;
        const size_t levels = s->projectiles.size();
        std::vector<std::optional<json>> damage(levels);
        std::map<std::string, std::map<std::string, std::vector<std::optional<json>>>> other;  // set -> prop -> value per level
        std::vector<std::string> names(levels);
        auto level_of = [&](const std::string& tpl) {
            for (size_t i = 0; i < levels; ++i)
                if (p2m::lower(s->projectiles[i]) == p2m::lower(tpl)) return i;
            return levels;
        };
        for (auto& [tpl, props] : list) {
            const size_t i = level_of(tpl);
            if (i >= levels || !props.is_object()) continue;
            names[i] = tpl;
            for (auto& [set, ps] : props.items())
                for (auto& [prop, v] : ps.items()) {
                    if (set == "gCDamage_PS" && prop == "DamageAmount") damage[i] = v;
                    else {
                        auto& slot = other[set][prop];
                        slot.resize(levels);
                        slot[i] = v;
                    }
                }
        }
        SpellDef d;
        d.uid = new_uid();
        d.mode = Mode::Edit;
        d.name = spell;
        bool mapped_any = false;
        // damage: every level needs a value; an unpatched level keeps the game's number
        bool any_damage = false;
        for (auto& x : damage) any_damage = any_damage || x.has_value();
        if (any_damage) {
            json arr = json::array();
            bool ok = true;
            for (size_t i = 0; i < levels && ok; ++i) {
                if (damage[i]) arr.push_back(*damage[i]);
                else if (const TemplateInfo* t = game_ ? game_->tpl(s->projectiles[i]) : nullptr; t && t->get("gCDamage_PS.DamageAmount") && t->get("gCDamage_PS.DamageAmount")->is_num())
                    arr.push_back(static_cast<long long>(t->get("gCDamage_PS.DamageAmount")->num()));
                else ok = false;
            }
            bool whole = ok;
            for (auto& v : arr) whole = whole && as_int(v).has_value();
            if (whole) {
                bool same = true;
                for (auto& v : arr) same = same && v == arr[0];
                d.damage = same ? arr[0] : arr;
                mapped_any = true;
            } else {
                for (size_t i = 0; i < levels; ++i)
                    if (damage[i]) {
                        rep_.leftover_patches[names[i]]["gCDamage_PS"]["DamageAmount"] = *damage[i];
                        rep_.missing_templates.insert(names[i]);
                    }
                unmapped("Template patches", spell + " projectiles", "DamageAmount of some levels, the other levels' numbers are unknown (no game data) or not whole");
            }
        }
        // the other properties: the editor applies them to every level, so they map only when every level has the same value
        for (auto& [set, props] : other)
            for (auto& [prop, vals] : props) {
                bool uniform = true;
                for (size_t i = 0; i < levels; ++i) uniform = uniform && vals[0] && vals[i] && *vals[i] == *vals[0];
                if (uniform) {
                    if (!d.extra.contains("projectile_properties")) d.extra["projectile_properties"] = json::object();
                    d.extra["projectile_properties"][set][prop] = *vals[0];
                    mapped_any = true;
                } else {
                    for (size_t i = 0; i < levels; ++i)
                        if (vals[i]) {
                            rep_.leftover_patches[names[i]][set][prop] = *vals[i];
                            rep_.missing_templates.insert(names[i]);
                        }
                    unmapped("Template patches", spell + " projectiles", set + "." + prop + " differs per level or is set on some levels only (the editor applies projectile_properties to every level)");
                }
            }
        if (mapped_any) {
            p_.spells.push_back(d);
            rep_.counts["spells changed"]++;
        }
    }

    // ---- the world: container fills (and NPC equipment as an inventory fill with the equip mark)
    void containers(const json& world) {
        std::map<std::string, size_t> by_guid;
        if (world.contains("containers") && world["containers"].is_array())
            for (auto& c : world["containers"]) {
                ContainerEdit ce;
                ce.uid = new_uid();
                ce.guid = c.value("id", std::string());
                ce.entity_name = c.value("name", std::string());
                ce.layer = c.value("layer", std::string());
                for (auto& it : c.value("items", json::array())) ce.items.push_back({it.value("item", std::string()), as_int(it.value("count", json(1))).value_or(1), false});
                by_guid[p2m::lower(ce.guid)] = p_.world.containers.size();
                p_.world.containers.push_back(ce);
                rep_.counts["container fills"]++;
            }
        if (world.contains("equipment") && world["equipment"].is_array())
            for (auto& q : world["equipment"]) {
                const std::string guid = q.value("id", std::string());
                auto it = by_guid.find(p2m::lower(guid));
                if (it == by_guid.end()) {
                    ContainerEdit ce;
                    ce.uid = new_uid();
                    ce.guid = guid;
                    ce.entity_name = q.value("name", std::string());
                    it = by_guid.emplace(p2m::lower(guid), p_.world.containers.size()).first;
                    p_.world.containers.push_back(ce);
                }
                p_.world.containers[it->second].items.push_back({q.value("item", std::string()), 1, true});
                rep_.counts["NPC equipment"]++;
            }
    }

    void dialog_and_teachers(const json& dialog, const json& world) {
        int xp = 0, trade = 0;
        if (dialog.is_object()) {
            if (dialog.contains("xp"))
                for (const char* k : {"add", "change"})
                    if (dialog["xp"].contains(k) && dialog["xp"][k].is_array()) xp += static_cast<int>(dialog["xp"][k].size());
            if (dialog.contains("trade") && dialog["trade"].is_array())
                for (auto& t : dialog["trade"]) trade += static_cast<int>(t.value("entries", json::array()).size());
        }
        if (xp || trade) {
            rep_.leftover_dialog = dialog;
            unmapped("Dialogue", "XP rewards", std::to_string(xp) + " GiveXP commands added to or changed on shipped dialogue infos: the editor's dialogues create new options, they do not edit shipped infos");
            unmapped("Dialogue", "trader refills", std::to_string(trade) + " RunScript OnTradeInfo_RefillTradeItems entries on shipped infos (the chapter-wise trader stock): not representable");
        }
        if (world.contains("teachers") && world["teachers"].is_array() && !world["teachers"].empty()) {
            rep_.leftover_teachers = world["teachers"];
            unmapped("World", "teacher limits", std::to_string(world["teachers"].size()) + " changed TeachSkills limits of placed NPCs (risen.npc.set_teach_limit): the editor has no field for them");
        }
    }

    // ---- assets: the files sit in <project>/assets, referenced relatively; the combat table and weather files are plain files of kind "other"
    void assets() {
        for (const std::string& name : in_.assets) {
            AssetFile a;
            a.uid = new_uid();
            a.file = "assets/" + name;
            a.kind = asset_kind(name);
            p_.assets.push_back(a);
        }
        rep_.counts["asset files"] = static_cast<int>(p_.assets.size());
    }

    void strings_left_over() {
        for (auto& [lang, rows] : in_.strings)
            for (auto& [id, text] : rows)
                if (!used_strings_.count(p2m::lower(id))) rep_.leftover_strings[lang][id] = text;
        std::set<std::string> ids;
        for (auto& [lang, rows] : rep_.leftover_strings)
            for (auto& [id, text] : rows) ids.insert(id);
        if (!ids.empty())
            unmapped("Strings", std::to_string(ids.size()) + " string rows", "rows that are no name or description of an item, recipe or NPC template of the project (the editor keeps texts on the objects that own them)");
        rep_.counts["string rows used"] = static_cast<int>(used_strings_.size());
    }
};

}  // namespace

Project project_from_pack2mod(const p2m::ProjectInput& in, const GameData* game, Pack2ProjectReport& rep) {
    Mapper m(in, game, rep);
    Project p = m.run();
    // the editor keeps what the converter did not map next to the project's own state
    json info = json::object();
    info["generator"] = "risen-pack2mod " P2M_VERSION;
    json un = json::array();
    for (auto& e : rep.unmapped) un.push_back(json{{"area", e.area}, {"name", e.name}, {"reason", e.reason}});
    info["not_mapped"] = un;
    p.editor["pack2mod"] = info;
    return p;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------
// comparison of the converted layer mod with the editor's export of the project

namespace {

// ---- effective settings per template: key -> canonical text
using Settings = std::map<std::string, std::map<std::string, std::string>>;

std::string canon(const json& v) {
    if (v.is_number()) {
        const double d = v.get<double>();
        if (std::floor(d) == d && std::fabs(d) < 9e15) return std::to_string(static_cast<long long>(d));
        return lua_number(d);
    }
    if (v.is_string()) return v.get<std::string>();
    return v.dump();
}

void put_prop(std::map<std::string, std::string>& t, const std::string& set, const std::string& prop, const json& v) { t["prop." + set + "." + prop] = canon(v); }

// requires / modifies as the layer reads them: a list of { skill, amount, mode } or a map skill -> amount; the highest amount per skill
std::string skill_text(const json& v, bool modifier) {
    std::map<std::string, std::pair<double, std::string>> m;
    if (v.is_array())
        for (auto& e : v) {
            const std::string k = e.value("skill", std::string());
            const double a = e.contains("amount") && e["amount"].is_number() ? e["amount"].get<double>() : 0;
            auto it = m.find(k);
            if (it == m.end() || a > it->second.first) m[k] = {a, e.value("mode", std::string("add"))};
        }
    else if (v.is_object())
        for (auto& [k, a] : v.items())
            if (a.is_number()) m[k] = {a.get<double>(), "add"};
    std::string s;
    for (auto& [k, x] : m) s += k + ":" + canon(x.first) + (modifier ? ":" + x.second : std::string()) + ";";
    return s;
}

void add_entry(Settings& out, const std::string& name, const json& e) {
    auto& t = out[name];
    if (!e.is_object()) return;
    for (auto& [key, v] : e.items()) {
        bool aliased = false;
        for (const Alias& a : kItemAliases)
            if (key == a.key) {
                put_prop(t, a.set, a.prop, v);
                aliased = true;
            }
        if (aliased) continue;
        if (key == "properties" && v.is_object()) {
            for (auto& [set, list] : v.items())
                if (list.is_object())
                    for (auto& [prop, x] : list.items()) put_prop(t, set, prop, x);
        } else if (key == "requires") {
            t["requires"] = skill_text(v, false);
        } else if (key == "modifies") {
            t["modifies"] = skill_text(v, true);
        } else if (key == "ingredients") {
            std::string s;
            if (v.is_array())
                for (auto& i : v) s += i.value("item", std::string()) + " x" + canon(i.value("count", json(1))) + ";";
            t["recipe.ingredients"] = s;
        } else if (key == "skills" && e.contains("station")) {
            t["recipe.skills"] = skill_text(v, false);
        } else if (key == "station" || key == "result") {
            t["recipe." + key] = canon(v);
        } else if (key == "name" || key == "description" || key == "from") {
            continue;  // texts are compared separately
        } else if (v.is_object() || v.is_array()) {
            t["x." + key] = v.dump();
        } else {
            t[key] = canon(v);
        }
    }
}

// the projectile templates a `spells` entry edits (core/spells.h): damage per level and projectile_properties on every level
void add_spell(Settings& out, const std::string& spell, const json& e) {
    const ShippedSpell* s = shipped_spell(spell);
    if (!s) return;
    for (size_t i = 0; i < s->projectiles.size(); ++i) {
        auto& t = out[s->projectiles[i]];
        if (e.contains("damage")) {
            const json& d = e["damage"];
            if (d.is_array()) {
                if (i < d.size()) put_prop(t, "gCDamage_PS", "DamageAmount", d[i]);
            } else if (d.is_number()) {
                put_prop(t, "gCDamage_PS", "DamageAmount", d);
            }
        }
        if (e.contains("projectile_properties") && e["projectile_properties"].is_object())
            for (auto& [set, list] : e["projectile_properties"].items())
                for (auto& [prop, v] : list.items()) put_prop(t, set, prop, v);
    }
}

bool file_exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

json data_file(const fs::path& dir, const char* rel) {
    const fs::path f = dir / u8(rel);
    if (!file_exists(f)) return nullptr;
    return lua_data_to_json(read_text(f), rel);
}

Settings legacy_settings(const fs::path& mod) {
    Settings s;
    for (const char* file : {"items.lua", "new_items.lua"}) {
        const json d = data_file(mod, (std::string("data/") + file).c_str());
        if (d.is_object())
            for (auto& [name, e] : d.items()) add_entry(s, name, e);
    }
    const json rec = data_file(mod, "data/recipes.lua");
    if (rec.is_object()) {
        for (const char* section : {"edit", "new"})
            if (rec.contains(section) && rec[section].is_object())
                for (auto& [name, e] : rec[section].items()) {
                    json copy = e;
                    if (std::string(section) == "new") copy.erase("from");
                    add_entry(s, name, copy);
                }
    }
    const json patches = data_file(mod, "data/patches.lua");
    if (patches.is_object())
        for (auto& [name, props] : patches.items()) add_entry(s, name, json::object({{"properties", props}}));
    return s;
}

Settings export_settings(const fs::path& dir) {
    Settings s;
    const json b = data_file(dir, "data/balance.lua");
    if (!b.is_object()) return s;
    for (const char* section : {"items", "new_items", "recipes", "new_recipes", "npcs", "new_npcs"})
        if (b.contains(section) && b[section].is_object())
            for (auto& [name, e] : b[section].items()) add_entry(s, name, e);
    if (b.contains("spells") && b["spells"].is_object())
        for (auto& [name, e] : b["spells"].items()) add_spell(s, name, e);
    return s;
}

// ---- texts
struct Texts {
    std::map<std::string, std::map<std::string, std::string>> by_lang;  // "" = all.csv (the fallback), else the language -> id -> text
};

void read_csv_rows(const std::string& text, std::map<std::string, std::string>& out) {
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
                else if (ch != '\r') fields.back() += ch;  // the converter writes CRLF line ends, also inside quoted texts: compared as LF
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
        if (fields.size() >= 2 && !fields[0].empty() && !(fields[0] == "id" && fields[1] == "text")) out[p2m::lower(fields[0])] = fields[1];
    }
}

Texts read_texts(const fs::path& dir) {
    Texts t;
    std::error_code ec;
    const fs::path sd = dir / "strings";
    if (!fs::is_directory(sd, ec)) return t;
    for (auto it = fs::directory_iterator(sd, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (it->path().extension() != ".csv") continue;
        const std::string stem = u8str(it->path().stem());
        read_csv_rows(read_text(it->path()), t.by_lang[stem == "all" ? std::string() : stem]);
    }
    return t;
}

// the converter writes its CSV files with CRLF line ends (also inside quoted texts); texts are compared with LF
std::string no_cr(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
    return s;
}

// inline names and descriptions of the export's new templates (`name` / `description`: one text for every language, or a table per language)
void inline_texts(const fs::path& dir, Texts& t) {
    const json b = data_file(dir, "data/balance.lua");
    if (!b.is_object()) return;
    for (const char* section : {"new_items", "new_recipes", "new_npcs", "new_spells"})
        if (b.contains(section) && b[section].is_object())
            for (auto& [name, e] : b[section].items())
                for (const auto& [field, prefix] : {std::pair<const char*, const char*>{"name", "FO_"}, {"description", "ITEMDESC_"}}) {
                    if (!e.contains(field)) continue;
                    const json& v = e[field];
                    if (v.is_string()) t.by_lang[""][p2m::lower(prefix + name)] = no_cr(v.get<std::string>());
                    else if (v.is_object())
                        for (auto& [lang, s] : v.items())
                            if (s.is_string()) t.by_lang[lang][p2m::lower(prefix + name)] = no_cr(s.get<std::string>());
                }
}

std::optional<std::string> effective(const Texts& t, const std::string& lang, const std::string& id) {
    if (auto l = t.by_lang.find(lang); l != t.by_lang.end())
        if (auto r = l->second.find(id); r != l->second.end()) return r->second;
    if (auto l = t.by_lang.find(""); l != t.by_lang.end())
        if (auto r = l->second.find(id); r != l->second.end()) return r->second;
    return std::nullopt;
}

// ---- the world
using Fill = std::map<std::string, std::vector<std::string>>;  // lower-case entity id -> sorted "item x n [equip]"

void add_fill(Fill& f, const std::string& id, const std::string& item, int n, bool equip) { f[p2m::lower(id)].push_back(item + " x" + std::to_string(n) + (equip ? " equip" : "")); }

Fill legacy_fill(const fs::path& mod) {
    Fill f;
    const json w = data_file(mod, "data/world.lua");
    if (!w.is_object()) return f;
    for (auto& c : w.value("containers", json::array()))
        for (auto& it : c.value("items", json::array())) add_fill(f, c.value("id", std::string()), it.value("item", std::string()), it.value("count", 1), false);
    for (auto& q : w.value("equipment", json::array())) add_fill(f, q.value("id", std::string()), q.value("item", std::string()), 1, true);
    for (auto& [id, v] : f) std::sort(v.begin(), v.end());
    return f;
}

Fill export_fill(const fs::path& dir) {
    Fill f;
    const json w = data_file(dir, "data/world.lua");
    if (!w.is_object()) return f;
    for (auto& c : w.value("containers", json::array())) {
        std::set<std::string> equip;
        for (auto& x : c.value("equip", json::array())) equip.insert(x.get<std::string>());
        for (auto& it : c.value("items", json::array())) {
            const std::string item = it[0].get<std::string>();
            add_fill(f, c.value("id", std::string()), item, it.size() > 1 ? it[1].get<int>() : 1, equip.count(item) != 0);
        }
    }
    for (auto& [id, v] : f) std::sort(v.begin(), v.end());
    return f;
}

// ---- files
bool same_file(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    if (fs::file_size(a, ec) != fs::file_size(b, ec)) return false;
    return read_text(a) == read_text(b);
}

}  // namespace

std::vector<std::string> compare_mod_with_export(const std::string& mod_dir, const std::string& export_dir, const Pack2ProjectReport& rep, CompareStats* stats) {
    std::vector<std::string> diff;
    CompareStats local;
    CompareStats& st = stats ? *stats : local;
    st = CompareStats{};
    const fs::path a = u8(mod_dir), b = u8(export_dir);
    try {
        // template settings
        const Settings sa = legacy_settings(a), sb = export_settings(b);
        for (auto& [name, ta] : sa) {
            if (rep.missing_templates.count(name)) {
                if (sb.count(name) && !sb.at(name).empty()) diff.push_back("template " + name + ": listed as not mapped, but the export has settings for it");
                continue;
            }
            auto it = sb.find(name);
            if (it == sb.end()) {
                if (!ta.empty()) diff.push_back("template " + name + ": missing in the export");
                continue;
            }
            ++st.templates;
            for (auto& [key, va] : ta) {
                if (rep.missing_settings.count(name + "|" + key)) continue;
                ++st.settings;
                auto k = it->second.find(key);
                if (k == it->second.end()) diff.push_back(name + " " + key + ": missing in the export (mod: " + va + ")");
                else if (k->second != va) diff.push_back(name + " " + key + ": mod " + va + ", export " + k->second);
            }
            for (auto& [key, vb] : it->second)
                if (!ta.count(key)) diff.push_back(name + " " + key + ": only in the export (" + vb + ")");
        }
        for (auto& [name, tb] : sb)
            if (!sa.count(name) && !tb.empty()) diff.push_back("template " + name + ": only in the export");

        // texts, per language
        Texts ta = read_texts(a), tb = read_texts(b);
        inline_texts(b, tb);
        std::set<std::string> langs{"English"}, ids;
        for (auto* t : {&ta, &tb})
            for (auto& [lang, rows] : t->by_lang) {
                if (!lang.empty()) langs.insert(lang);
                for (auto& [id, s] : rows) ids.insert(id);
            }
        std::set<std::string> not_mapped;  // the rows the report lists as not in the project
        for (auto& [lang, rows] : rep.leftover_strings)
            for (auto& [id, s] : rows) not_mapped.insert(p2m::lower(id));
        for (const std::string& id : ids)
            for (const std::string& lang : langs) {
                if (not_mapped.count(id)) continue;
                const auto x = effective(ta, lang, id), y = effective(tb, lang, id);
                if (x) ++st.texts;
                if (x == y) continue;
                // a string the converted mod does not carry (an export's default for a new recipe's name) is not a loss
                if (!x) continue;
                diff.push_back("text " + id + " [" + lang + "]: mod '" + *x + "', export '" + (y ? *y : std::string("(none)")) + "'");
            }

        // container fills and equipment
        const Fill fa = legacy_fill(a), fb = export_fill(b);
        for (auto& [id, v] : fa) {
            ++st.fills;
            auto it = fb.find(id);
            if (it == fb.end()) diff.push_back("container " + id + ": missing in the export");
            else if (it->second != v) diff.push_back("container " + id + ": the fill differs");
        }
        for (auto& [id, v] : fb)
            if (!fa.count(id)) diff.push_back("container " + id + ": only in the export");

        // asset files: the same names with the same bytes (the editor copies compiled resources as they are)
        std::error_code ec;
        std::set<std::string> names;
        for (auto it = fs::directory_iterator(a / "assets", ec); !ec && it != fs::directory_iterator(); it.increment(ec))
            if (it->is_regular_file()) names.insert(u8str(it->path().filename()));
        std::set<std::string> exported;
        for (auto it = fs::directory_iterator(b / "assets", ec); !ec && it != fs::directory_iterator(); it.increment(ec))
            if (it->is_regular_file()) exported.insert(u8str(it->path().filename()));
        for (const std::string& n : names) {
            ++st.assets;
            if (!exported.count(n)) diff.push_back("asset " + n + ": missing in the export");
            else if (!same_file(a / "assets" / u8(n), b / "assets" / u8(n))) diff.push_back("asset " + n + ": the bytes differ");
        }
        for (const std::string& n : exported)
            if (!names.count(n)) diff.push_back("asset " + n + ": only in the export");
    } catch (const std::exception& e) {
        diff.push_back(std::string("comparison failed: ") + e.what());
    }
    return diff;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------
// the writer

namespace {

std::string md_cell(const std::string& s) { return p2m::replace_all(p2m::replace_all(s, "|", "\\|"), "\n", " "); }

std::string csv_quote(const std::string& s) { return "\"" + p2m::replace_all(s, "\"", "\"\"") + "\""; }

}  // namespace

p2m::ProjectResult write_pack2mod_project(const p2m::ProjectInput& in, std::ostream& log) {
    p2m::ProjectResult r;
    std::shared_ptr<GameData> game;
    std::string game_note;
    try {
        game = GameData::load(in.baseline);
    } catch (const std::exception& e) {
        game_note = std::string("the game data could not be loaded (") + e.what() + "): template patches were classified without it and the project was not validated against the game";
    }

    Pack2ProjectReport rep;
    Project p = project_from_pack2mod(in, game.get(), rep);
    if (!game_note.empty()) rep.notes.push_back(game_note);

    try {
        save_project_file(p, in.project_file);
        const fs::path dir = u8(in.project_dir);
        const std::string hdr = "-- left over by risen-pack2mod --project: the converter's own data format (docs/PACK2MOD.md). Private: do not commit or redistribute.\n";
        auto lua_file = [&](const char* rel, const json& v) { write_text(dir / u8(rel), hdr + "return " + lua_value(v) + "\n"); };
        if (!rep.leftover_patches.empty()) lua_file("unmapped/patches.lua", rep.leftover_patches);
        if (!rep.leftover_dialog.empty()) lua_file("unmapped/dialog.lua", rep.leftover_dialog);
        if (!rep.leftover_teachers.empty()) lua_file("unmapped/world.lua", json::object({{"teachers", rep.leftover_teachers}}));
        for (auto& [lang, rows] : rep.leftover_strings) {
            std::string text = "id;text\n";
            for (auto& [id, s] : rows) text += id + ";" + csv_quote(s) + "\n";
            write_text(dir / u8("unmapped/strings/" + (lang == "English" ? std::string("all") : lang) + ".csv"), text);
        }
        // the assets are where the project says they are
        for (const AssetFile& a : p.assets)
            if (!file_exists(dir / u8(a.file))) throw std::runtime_error("asset file missing in the project folder: " + a.file);
    } catch (const std::exception& e) {
        r.rc = 1;
        r.error = e.what();
        return r;
    }

    // the editor's own validation, with the game data
    std::vector<Problem> problems = validate(p, game.get());
    const size_t errors = error_count(problems), warnings = problems.size() - errors;

    // --verify: export the project again and compare with the converted mod
    std::vector<std::string> diff;
    CompareStats cmp;
    bool verified = false;
    if (in.verify && !in.mod_dir.empty()) {
        const fs::path tmp = fs::temp_directory_path() / ("p2m-verify-" + new_uid().substr(0, 12));
        ExportOptions eo;
        eo.project_dir = in.project_dir;
        const ExportResult er = export_mod(p, u8str(tmp), eo);
        if (!er.ok) {
            for (auto& e : er.errors) diff.push_back("export: " + e);
        } else {
            diff = compare_mod_with_export(in.mod_dir, u8str(tmp), rep, &cmp);
        }
        std::error_code ec;
        fs::remove_all(tmp, ec);
        verified = true;
    }

    // ---- the report section
    std::ostringstream md;
    md << "## Editor project\n\nWritten as a risen-modkit project: `" << p2m::bare_name(in.project_file) << "` with its asset files in `assets/`. Open it in risen-modkit; the editor's export makes the layer mod from it.\n\n";
    md << "| What | In the project | Where |\n|---|---:|---|\n";
    const char* where[][2] = {{"items changed", "Items (Edit)"},
                              {"items new", "Items (New)"},
                              {"recipes new", "Recipes (New): the converted mod's new recipe items"},
                              {"recipes from shipped items", "Recipes (New, learned at the start): shipped items the mod turned into recipes"},
                              {"recipes edited", "Recipes (Edit)"},
                              {"NPC / monster templates changed", "NPCs (Edit templates)"},
                              {"spells changed", "Spells (Edit): projectile templates of a shipped spell"},
                              {"container fills", "World (container fills)"},
                              {"NPC equipment", "World (container fills with the equip mark)"},
                              {"asset files", "Assets"},
                              {"string rows used", "labels and descriptions of the objects above (Languages: project)"}};
    for (auto& w : where)
        if (auto it = rep.counts.find(w[0]); it != rep.counts.end()) md << "| " << w[0] << " | " << it->second << " | " << w[1] << " |\n";
    md << "\n";
    if (rep.unmapped.empty()) {
        md << "Everything the converter produced has a place in the project.\n\n";
    } else {
        md << "### Not representable in the project\n\nKept as data in `unmapped/` (the converter's own format) and listed here; the editor's export does not apply them. Use `--out` for a complete layer mod.\n\n| Area | What | Why |\n|---|---|---|\n";
        for (auto& e : rep.unmapped) md << "| " << md_cell(e.area) << " | " << md_cell(e.name) << " | " << md_cell(e.reason) << " |\n";
        md << "\n";
    }
    md << "### Editor validation" << (game ? "" : " (without game data)") << "\n\n" << errors << " error(s), " << warnings << " warning(s).\n\n";
    {
        std::map<std::string, int> seen;
        size_t shown = 0;
        for (const Problem& pr : problems) {
            const std::string line = std::string(pr.severity == Severity::Error ? "error" : "warning") + " " + pr.area + " " + pr.subject + ": " + pr.message;
            if (shown < 60) {
                md << "* " << line << "\n";
                ++shown;
            }
        }
        if (problems.size() > shown) md << "* ... " << (problems.size() - shown) << " more\n";
        md << "\n";
    }
    if (verified) {
        md << "### Verification (--verify)\n\nThe project was exported again and compared with the converted mod (effective template settings, recipes, texts per language, container fills, asset bytes): ";
        if (diff.empty())
            md << "**equivalent** (" << cmp.templates << " templates with " << cmp.settings << " settings, " << cmp.texts << " texts, " << cmp.fills << " container fills, " << cmp.assets
               << " asset files compared).\n\n";
        else {
            md << "**" << diff.size() << " difference(s)**:\n\n";
            for (size_t i = 0; i < diff.size() && i < 80; ++i) md << "* " << md_cell(diff[i]) << "\n";
            md << "\n";
        }
    }
    if (!rep.notes.empty()) {
        md << "### Notes\n\n";
        for (auto& n : rep.notes) md << "* " << n << "\n";
        md << "\n";
    }
    r.report_section = md.str();

    // ---- the manifest
    json man = json::object();
    man["generator"] = "risen-pack2mod " P2M_VERSION;
    man["id"] = in.id;
    man["project"] = p2m::bare_name(in.project_file);
    json counts = json::object();
    for (auto& [k, v] : rep.counts) counts[k] = v;
    man["counts"] = counts;
    json un = json::array();
    for (auto& e : rep.unmapped) un.push_back(json{{"area", e.area}, {"name", e.name}, {"reason", e.reason}});
    man["not_mapped"] = un;
    man["validation"] = json{{"game_data", game != nullptr}, {"errors", errors}, {"warnings", warnings}};
    if (verified)
        man["verify"] = json{{"differences", diff.size()}, {"templates", cmp.templates}, {"settings", cmp.settings}, {"texts", cmp.texts}, {"container_fills", cmp.fills}, {"asset_files", cmp.assets}};
    r.manifest = man.dump(2) + "\n";

    std::ostringstream sum;
    sum << "project: " << p2m::bare_name(in.project_file) << ": " << p.items.size() << " items, " << p.recipes.size() << " recipes, " << p.npc_templates.size() << " NPC templates, "
        << p.spells.size() << " spells, " << p.world.containers.size() << " container fills, " << p.assets.size() << " assets; not mapped: " << rep.unmapped.size() << "; editor validation: " << errors
        << " errors, " << warnings << " warnings" << (verified ? (diff.empty() ? "; verified: export equals the converted mod (" + std::to_string(cmp.settings) + " settings of " + std::to_string(cmp.templates) + " templates, " +
                                                                                   std::to_string(cmp.texts) + " texts, " + std::to_string(cmp.fills) + " fills, " + std::to_string(cmp.assets) + " assets)"
                                                                             : "; VERIFY FAILED: " + std::to_string(diff.size()) + " difference(s)") : std::string());
    r.summary = sum.str();
    if (verified && !diff.empty()) {
        r.rc = 1;
        r.error = "--verify: " + std::to_string(diff.size()) + " difference(s) between the converted mod and the export of the project, first: " + diff.front() + " (all in the report)";
    }
    (void)log;
    return r;
}

void register_pack2mod_project_writer() { p2m::set_project_writer([](const p2m::ProjectInput& in, std::ostream& log) { return write_pack2mod_project(in, log); }); }

}  // namespace mk
