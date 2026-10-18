#include "core/pack2project.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>

#include "common/pose.h"
#include "core/assets.h"
#include "core/export.h"
#include "core/genome.h"
#include "core/import.h"
#include "core/lua_writer.h"
#include "core/paths.h"
#include "core/spells.h"
#include "core/validate.h"
#include "core/world.h"
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

// a world entity's ID ("@" + 32 hex, the 16 bytes in file order) as risen.entity.by_id spells it
std::string entity_guid_text(const std::string& at_hex) {
    char raw[16] = {};
    if (at_hex.size() == 33)
        for (size_t k = 0; k < 16; ++k) raw[k] = static_cast<char>(std::stoi(at_hex.substr(1 + 2 * k, 2), nullptr, 16));
    return guid_text(raw);
}

// the layer's own aliases of the item settings (src/game/lua_balance.cpp ITEM_PROPS): `value` is gCItem_PS.GoldValue, ...
struct Alias {
    const char *key, *set, *prop;
};
constexpr Alias kItemAliases[] = {{"value", "gCItem_PS", "GoldValue"},     {"use_type", "gCItem_PS", "UseType"},          {"category", "gCItem_PS", "Category"},
                                  {"damage", "gCDamage_PS", "DamageAmount"}, {"mana_cost", "gCCastInfo_PS", "ManaCost"}, {"icon", "gCItem_PS", "IconImage"},
                                  {"mesh", "eCMesh_PS", "MeshFileName"},     {"material", "eCMesh_PS", "MaterialSwitch"}};

// ---- world layers ---------------------------------------------------------------------------------------------------------------------------------------
bool same_pose(const GEntity& a, const GEntity& b, bool& scale_changed) {
    spass::pose::Matrix ma, mb;
    std::copy(a.matrix.begin(), a.matrix.end(), ma.begin());
    std::copy(b.matrix.begin(), b.matrix.end(), mb.begin());
    const auto sa = spass::pose::scale_of(ma), sb = spass::pose::scale_of(mb);
    scale_changed = false;
    for (size_t i = 0; i < 3; ++i) scale_changed = scale_changed || std::fabs(sa[i] - sb[i]) > 1e-3f;
    if (std::fabs(ma[12] - mb[12]) > 0.05f || std::fabs(ma[13] - mb[13]) > 0.05f || std::fabs(ma[14] - mb[14]) > 0.05f) return false;
    const auto aa = spass::pose::decompose(ma), ab = spass::pose::decompose(mb);
    auto diff = [](float x, float y) {
        float d = std::fmod(std::fabs(x - y), 360.0f);
        return d > 180.0f ? 360.0f - d : d;
    };
    return diff(aa.yaw, ab.yaw) <= 0.05f && diff(aa.pitch, ab.pitch) <= 0.05f && diff(aa.roll, ab.roll) <= 0.05f;
}

Vec3 angles_of(const GEntity& e) {
    spass::pose::Matrix m;
    std::copy(e.matrix.begin(), e.matrix.end(), m.begin());
    const auto a = spass::pose::decompose(m);
    return Vec3{std::round(a.yaw * 100.0) / 100.0, std::round(a.pitch * 100.0) / 100.0, std::round(a.roll * 100.0) / 100.0};
}

class Mapper {
public:
    Mapper(const p2m::ProjectInput& in, const GameData* game, Pack2ProjectReport& rep, const std::vector<LayerPair>* layers) : in_(in), game_(game), rep_(rep), layers_(layers) {}

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
        world_edits();
        assets();
        strings_left_over();
        return std::move(p_);
    }

private:
    const p2m::ProjectInput& in_;
    const GameData* game_;
    Pack2ProjectReport& rep_;
    const std::vector<LayerPair>* layers_;
    Project p_;
    std::set<std::string> used_strings_;

    void unmapped(const std::string& area, const std::string& name, const std::string& reason) { rep_.unmapped.push_back({area, name, reason}); }

    // ---- texts
    // the texts of an id in the project's languages (a row only in a language is only in that language, as in the converted mod)
    Text text_for(const std::string& id) {
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
    // a skill list [{skill=, amount=, mode=}] as the editor's entries (the engine's order, the modes); nullopt (and a report entry) when it does not fit: the
    // engine's amounts are integers and its modes add, set, max and percent. A requirement listed twice keeps the highest amount (all of them have to hold);
    // a modifier listed twice cannot be kept (a map has one entry per skill).
    std::optional<std::vector<SkillEntry>> skill_entries(const json& list, bool modifier, const std::string& area, const std::string& name, const char* key) {
        std::vector<SkillEntry> out;
        auto fail = [&](const std::string& why) {
            unmapped(area, name, std::string(key) + ": " + why);
            rep_.missing_settings.insert(name + "|" + key);
            return std::nullopt;
        };
        if (list.is_array()) {
            for (auto& e : list) {
                SkillEntry x;
                x.skill = e.value("skill", std::string());
                const auto amount = e.contains("amount") ? as_int(e["amount"]) : std::nullopt;
                if (x.skill.empty() || !amount) return fail("an entry without a usable skill or a whole amount (the engine's amounts are integers)");
                x.amount = *amount;
                x.mode = e.value("mode", std::string("add"));
                if (x.mode != "add" && x.mode != "set" && x.mode != "max" && x.mode != "percent")
                    return fail(x.skill + " with the mode '" + x.mode + "' (the engine's modes are add, set, max, percent)");
                auto same = std::find_if(out.begin(), out.end(), [&](const SkillEntry& y) { return y.skill == x.skill; });
                if (same == out.end()) out.push_back(x);
                else if (modifier) return fail(x.skill + " is listed twice (the editor keeps one modifier per skill)");
                else same->amount = std::max(same->amount, x.amount);
            }
        } else if (list.is_object()) {
            for (auto& [k, v] : list.items())
                if (auto a = as_int(v)) out.push_back({k, *a, "add"});
        }
        return out;
    }

    // the settings an item entry shares between edits and new items
    void item_fields(ItemDef& d, const json& e, const std::string& area) {
        d.value = number(e, "value", area, d.name);
        d.damage = number(e, "damage", area, d.name);
        d.mana_cost = number(e, "mana_cost", area, d.name);
        if (e.contains("requires"))
            if (auto l = skill_entries(e["requires"], false, area, d.name, "requires")) set_requirement_entries(d, *l);
        if (e.contains("modifies"))
            if (auto l = skill_entries(e["modifies"], true, area, d.name, "modifies")) set_modifier_entries(d, *l);
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
            d.label = text_for("FO_" + name);
            d.description = text_for("ITEMDESC_" + name);
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
        d.label = text_for("FO_" + name);
        d.description = text_for("ITEMDESC_" + name);
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
        // a clone shows its base's name and description until a row says otherwise; a new recipe of the editor would make up a name: say the base's
        if (game_ && !d.base.empty()) {
            const bool had_label = !d.label.empty(), had_description = !d.description.empty();  // a text in any language is the mod's own: the others get it as the fallback
            for (const std::string& lang : p_.mod.languages) {
                if (!had_label)
                    if (const std::string t = game_->text("FO_" + d.base, lang); !t.empty()) d.label[lang] = t;
                if (!had_description)
                    if (const std::string t = game_->text("ITEMDESC_" + d.base, lang); !t.empty() && t != "-") d.description[lang] = t;
            }
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
                add_patch(name, props, "");
            }
        }
        for (auto& [spell, list] : by_spell) projectile_spell(spell, list);
    }

    // a template patch of the project (Patches panel): the properties no other section of the editor takes
    void add_patch(const std::string& name, const json& props, const std::string& why) {
        TemplatePatch x;
        x.uid = new_uid();
        x.template_name = name;
        x.properties = props_of(json::object({{"properties", props}}));
        if (!why.empty()) x.notes = why;
        p_.patches.push_back(x);
        rep_.counts["template patches"]++;
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
                    if (damage[i]) add_patch(names[i], json::object({{"gCDamage_PS", json::object({{"DamageAmount", *damage[i]}})}}), "DamageAmount of some levels of " + spell + ": the other levels' numbers are unknown");
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
                        if (vals[i]) add_patch(names[i], json::object({{set, json::object({{prop, *vals[i]}})}}), set + "." + prop + " differs per level or is set on some levels only (a spell's projectile_properties apply to every level)");
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

    // XP rewards and trader refills on shipped infos, teaching limits
    void dialog_and_teachers(const json& dialog, const json& world) {
        auto edit_of = [&](const std::string& info) -> DialogEdit& {
            for (auto& e : p_.dialog_edits)
                if (e.info == info) return e;
            DialogEdit x;
            x.uid = new_uid();
            x.info = info;
            p_.dialog_edits.push_back(x);
            return p_.dialog_edits.back();
        };
        if (dialog.is_object()) {
            if (dialog.contains("xp") && dialog["xp"].is_object()) {
                for (auto& e : dialog["xp"].value("add", json::array())) {
                    DialogEdit& x = edit_of(e.value("info", std::string()));
                    x.xp_add = as_int(e.value("amount", json(0)));
                    rep_.counts["XP rewards added"]++;
                }
                for (auto& e : dialog["xp"].value("change", json::array())) {
                    DialogEdit& x = edit_of(e.value("info", std::string()));
                    x.xp_old = as_int(e.value("old", json(0)));
                    x.xp_new = as_int(e.value("new", json(0)));
                    rep_.counts["XP rewards changed"]++;
                }
            }
            for (auto& ch : dialog.value("changes", json::array())) {
                DialogEdit& x = edit_of(ch.value("info", std::string()));
                for (auto& op : ch.value("ops", json::array())) {
                    x.ops.push_back({op.value("kind", std::string()), as_int(op.value("index", json(0))).value_or(0), op.value("class", std::string()), op.value("name", std::string()),
                                     op.value("old", std::string()), op.value("new", std::string())});
                    rep_.counts["property edits of shipped infos"]++;
                }
            }
            for (auto& t : dialog.value("trade", json::array())) {
                DialogEdit& x = edit_of(t.value("info", std::string()));
                if (t.contains("chapter") && t["chapter"].is_number()) x.chapter = as_int(t["chapter"]);
                for (auto& r : t.value("entries", json::array())) {
                    x.refills.push_back({r.value("trader", std::string()), r.value("item", std::string()), as_int(r.value("count", json(1))).value_or(1)});
                    rep_.counts["trader refills"]++;
                }
            }
        }
        if (world.contains("teachers") && world["teachers"].is_array())
            for (auto& t : world["teachers"]) {
                TeacherLimit x;
                x.uid = new_uid();
                x.guid = t.value("id", std::string());
                x.entity_name = t.value("name", std::string());
                x.skill = t.value("skill", std::string());
                x.max = as_int(t.value("max", json(100))).value_or(100);
                p_.teachers.push_back(x);
                rep_.counts["teacher limits"]++;
            }
    }

    // ---- the world: entities of the changed layers that the mod moved, removed or added (world_changes)
    void world_edits() {
        std::vector<LayerPair> parsed;
        if (!layers_) {
            std::vector<std::string> problems;
            parsed = parse_layer_pairs(in_, problems);
            for (auto& why : problems) unmapped("World", "a world layer", why);
        }
        const std::vector<LayerPair>& pairs = layers_ ? *layers_ : parsed;
        WorldChanges wc = world_changes(pairs, in_.template_guids);
        const size_t n = wc.moves.size() + wc.removals.size() + wc.spawns.size();
        if (in_.has_classic) {
            // the same layers against Enhanced: what differs there but not from the classic game is the classic game's own difference
            std::vector<LayerPair> vs_enh;
            for (const LayerPair& L : pairs)
                if (L.has_enhanced) {
                    LayerPair x;
                    x.name = L.name;
                    x.base = L.enhanced;
                    x.mod = L.mod;
                    vs_enh.push_back(std::move(x));
                }
            const WorldChanges we = world_changes(vs_enh, in_.template_guids);
            rep_.classic["world entities moved, removed or added against Enhanced"] = static_cast<int>(we.moves.size() + we.removals.size() + we.spawns.size());
            rep_.classic["  of them by the author (against the classic game; carried)"] = static_cast<int>(n);
            rep_.classic["  moved"] = static_cast<int>(wc.moves.size());
            int turned = 0, under1m = 0, under10m = 0, over10m = 0;
            for (float dd : wc.move_distance) {
                if (dd < 0.05f) ++turned;
                else if (dd < 100) ++under1m;
                else if (dd < 1000) ++under10m;
                else ++over10m;
            }
            rep_.classic["    only turned (the position is the same)"] = turned;
            rep_.classic["    moved by less than 1 m"] = under1m;
            rep_.classic["    moved by 1 to 10 m"] = under10m;
            rep_.classic["    moved by 10 m or more"] = over10m;
            rep_.classic["  removed"] = static_cast<int>(wc.removals.size());
            rep_.classic["  added"] = static_cast<int>(wc.spawns.size());
        }
        if (n && !in_.world_edits && !in_.has_classic) {
            // The mod's layers are the classic game's files: where they differ from Enhanced's the data cannot tell the author's own edits from the base's
            // differences (the Extended Mod's changelog names no moves, but 141 NPCs of its NPC layers stand elsewhere than in Enhanced). Counted, not carried.
            rep_.counts["world entity differences (not carried)"] = static_cast<int>(n);
            std::map<std::string, int> layers;
            for (auto& m : wc.moves) layers[m.layer]++;
            for (auto& m : wc.removals) layers[m.layer]++;
            for (auto& m : wc.spawns) layers[m.notes]++;
            std::string list;
            for (auto& [l, c] : layers) list += (list.empty() ? "" : ", ") + l + " " + std::to_string(c);
            unmapped("World", std::to_string(wc.moves.size()) + " moved, " + std::to_string(wc.removals.size()) + " removed, " + std::to_string(wc.spawns.size()) + " added entities",
                     "not carried by default: the mod's world layers are the classic game's files and differ from Enhanced's in more than the mod's own edits, so moves cannot be told from "
                     "the base's differences (" + list + "); run with --world-edits to carry them");
        } else {
            for (auto& m : wc.moves) p_.world.moves.push_back(m);
            for (auto& m : wc.removals) p_.world.removals.push_back(m);
            for (auto& m : wc.spawns) p_.world.spawns.push_back(m);
            if (!wc.moves.empty()) rep_.counts["entities moved"] = static_cast<int>(wc.moves.size());
            if (!wc.removals.empty()) rep_.counts["entities removed"] = static_cast<int>(wc.removals.size());
            if (!wc.spawns.empty()) rep_.counts["entities added"] = static_cast<int>(wc.spawns.size());
        }
        if (wc.skipped_template) unmapped("World", std::to_string(wc.skipped_template) + " new entities", "their creator template is unknown (neither the game nor the mod has a template with that ID)");
        if (wc.new_npcs) rep_.notes.push_back(std::to_string(wc.new_npcs) + " added entities are NPCs: they are spawned from their template without the properties their layer entity carried (name, routine, inventory)");
        if (wc.scale_changes) unmapped("World", std::to_string(wc.scale_changes) + " entities with another scale", "the editor moves and turns an entity, it does not scale it");
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

    // the rows no object took: free string rows of the project (one row per id, ids compared case-insensitively, the first spelling kept)
    void strings_left_over() {
        std::map<std::string, StringRow> rows;  // lower-case id -> row
        for (const std::string& lang : p_.mod.languages) {
            auto l = in_.strings.find(lang);
            if (l == in_.strings.end()) continue;
            for (auto& [id, text] : l->second) {
                const std::string low = p2m::lower(id);
                if (used_strings_.count(low) || text.empty()) continue;
                StringRow& r = rows[low];
                if (r.id.empty()) {
                    r.uid = new_uid();
                    r.id = id;
                }
                r.text[lang] = text;
            }
        }
        for (auto& [low, r] : rows) p_.strings.push_back(std::move(r));
        rep_.counts["free string rows"] = static_cast<int>(p_.strings.size());
        rep_.counts["string rows used"] = static_cast<int>(used_strings_.size());
    }
};

}  // namespace

std::vector<LayerPair> parse_layer_pairs(const p2m::ProjectInput& in, std::vector<std::string>& problems) {
    std::vector<LayerPair> out;
    for (const p2m::WorldLayerFile& L : in.world_layers) {
        LayerPair pr;
        pr.name = L.name;
        if (p2m::ends_with(p2m::lower(pr.name), ".lrent")) pr.name.resize(pr.name.size() - 6);
        try {
            if (!L.base.empty()) pr.base = parse_genome(L.base);
            pr.mod = parse_genome(L.mod);
            if (!L.enhanced.empty()) {
                pr.enhanced = parse_genome(L.enhanced);
                pr.has_enhanced = true;
            }
        } catch (const std::exception& e) {
            problems.push_back(L.name + " could not be read: " + e.what());
            continue;
        }
        out.push_back(std::move(pr));
    }
    return out;
}

WorldChanges world_changes(const std::vector<LayerPair>& layers, const std::map<std::string, std::string>& template_guids) {
    WorldChanges wc;
    for (const LayerPair& L : layers) {
        std::map<std::string, const GEntity*> in_base, in_mod;
        for (auto& e : L.base.entities)
            if (e.name != "DRE") in_base[e.id] = &e;
        for (auto& e : L.mod.entities)
            if (e.name != "DRE") in_mod[e.id] = &e;
        for (auto& e : L.mod.entities) {
            if (e.name == "DRE") continue;
            auto it = in_base.find(e.id);
            if (it != in_base.end()) {
                bool scaled = false;
                if (!same_pose(*it->second, e, scaled)) {
                    EntityRef r;
                    r.uid = new_uid();
                    r.guid = entity_guid_text(e.id);
                    r.entity_name = e.name;
                    r.layer = L.name;
                    r.position = Vec3{e.matrix[12], e.matrix[13], e.matrix[14]};
                    r.rotation = angles_of(e);
                    wc.moves.push_back(r);
                    wc.move_distance.push_back(std::sqrt((e.matrix[12] - it->second->matrix[12]) * (e.matrix[12] - it->second->matrix[12]) +
                                                         (e.matrix[13] - it->second->matrix[13]) * (e.matrix[13] - it->second->matrix[13]) +
                                                         (e.matrix[14] - it->second->matrix[14]) * (e.matrix[14] - it->second->matrix[14])));
                }
                if (scaled) ++wc.scale_changes;
                continue;
            }
            // a new entity: made again by risen.entity.spawn from its creator template, at its position
            std::string tpl;
            if (auto g = template_guids.find(e.creator); !e.creator.empty() && g != template_guids.end()) tpl = g->second;
            if (tpl.empty()) {
                ++wc.skipped_template;
                continue;
            }
            for (auto& s : e.sets)
                if (s.cls == "gCNPC_PS") {
                    ++wc.new_npcs;
                    break;
                }
            WorldSpawn sp;
            sp.uid = new_uid();
            sp.template_name = tpl;
            sp.position = Vec3{e.matrix[12], e.matrix[13], e.matrix[14]};
            sp.rotation = angles_of(e);
            sp.notes = "placed by the layer " + L.name + " as " + e.name;
            wc.spawns.push_back(sp);
        }
        for (auto& e : L.base.entities) {
            if (e.name == "DRE" || in_mod.count(e.id)) continue;
            EntityRef r;
            r.uid = new_uid();
            r.guid = entity_guid_text(e.id);
            r.entity_name = e.name;
            r.layer = L.name;
            wc.removals.push_back(r);
        }
    }
    return wc;
}

Project project_from_pack2mod(const p2m::ProjectInput& in, const GameData* game, Pack2ProjectReport& rep, const std::vector<LayerPair>* layers) {
    Mapper m(in, game, rep, layers);
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

// requires / modifies as the layer reads them, in the engine's order: a list of { skill, amount, mode } (the highest amount per skill, at its first place) or a
// map skill -> amount (alphabetical, the layer writes a map in that order)
std::string skill_text(const json& v, bool modifier) {
    std::vector<std::pair<std::string, std::pair<double, std::string>>> list;
    auto put = [&](const std::string& k, double a, const std::string& mode) {
        for (auto& x : list)
            if (x.first == k) {
                if (a > x.second.first) x.second = {a, mode};
                return;
            }
        list.push_back({k, {a, mode}});
    };
    if (v.is_array())
        for (auto& e : v) put(e.value("skill", std::string()), e.contains("amount") && e["amount"].is_number() ? e["amount"].get<double>() : 0, e.value("mode", std::string("add")));
    else if (v.is_object()) {
        std::map<std::string, double> sorted;
        for (auto& [k, a] : v.items())
            if (a.is_number()) sorted[k] = a.get<double>();
        for (auto& [k, a] : sorted) put(k, a, "add");
    }
    std::string s;
    for (auto& [k, x] : list) s += k + ":" + canon(x.first) + (modifier ? ":" + x.second : std::string()) + ";";
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
    const json patches = data_file(dir, "data/patches.lua");
    if (patches.is_array())
        for (auto& e : patches)
            if (e.is_object()) add_entry(s, e.value("template", std::string()), json::object({{"properties", e.value("properties", json::object())}}));
    return s;
}

// ---- edits of shipped dialogue infos and teaching limits: per info the operations, per NPC the limits
using DialogOps = std::map<std::string, std::set<std::string>>;  // info -> "xp+40", "xp 10>25", "refill Oscar It_Axe x2", "chapter 1"

DialogOps legacy_dialog_ops(const fs::path& mod) {
    DialogOps ops;
    const json d = data_file(mod, "data/dialog.lua");
    if (!d.is_object()) return ops;
    if (d.contains("xp") && d["xp"].is_object()) {
        for (auto& e : d["xp"].value("add", json::array())) ops[e.value("info", std::string())].insert("xp+" + canon(e.value("amount", json(0))));
        for (auto& e : d["xp"].value("change", json::array())) ops[e.value("info", std::string())].insert("xp " + canon(e.value("old", json(0))) + ">" + canon(e.value("new", json(0))));
    }
    for (auto& ch : d.value("changes", json::array()))
        for (auto& op : ch.value("ops", json::array()))
            ops[ch.value("info", std::string())].insert("op " + op.value("kind", std::string()) + " " + canon(op.value("index", json(0))) + " " + op.value("class", std::string()) + " " +
                                                         op.value("name", std::string()) + " " + op.value("old", std::string()) + ">" + op.value("new", std::string()));
    for (auto& t : d.value("trade", json::array())) {
        auto& o = ops[t.value("info", std::string())];
        if (t.contains("chapter") && t["chapter"].is_number()) o.insert("chapter " + canon(t["chapter"]));
        for (auto& r : t.value("entries", json::array())) o.insert("refill " + r.value("trader", std::string()) + " " + r.value("item", std::string()) + " x" + canon(r.value("count", json(1))));
    }
    return ops;
}

DialogOps export_dialog_ops(const fs::path& dir) {
    DialogOps ops;
    const json d = data_file(dir, "data/dialog_edits.lua");
    if (!d.is_array()) return ops;
    for (auto& e : d) {
        auto& o = ops[e.value("info", std::string())];
        if (e.contains("xp_add")) o.insert("xp+" + canon(e["xp_add"]));
        if (e.contains("xp_old") && e.contains("xp_new")) o.insert("xp " + canon(e["xp_old"]) + ">" + canon(e["xp_new"]));
        if (e.contains("chapter") && e["chapter"].is_number()) o.insert("chapter " + canon(e["chapter"]));
        for (auto& op : e.value("ops", json::array()))
            o.insert("op " + op.value("kind", std::string()) + " " + canon(op.value("index", json(0))) + " " + op.value("class", std::string()) + " " + op.value("name", std::string()) + " " +
                     op.value("old", std::string()) + ">" + op.value("new", std::string()));
        for (auto& r : e.value("refills", json::array())) o.insert("refill " + r.value("trader", std::string()) + " " + r.value("item", std::string()) + " x" + canon(r.value("count", json(1))));
    }
    return ops;
}

std::set<std::string> teacher_set(const json& list) {
    std::set<std::string> out;
    if (list.is_array())
        for (auto& t : list) out.insert(p2m::lower(t.value("id", std::string())) + " " + t.value("skill", std::string()) + " " + canon(t.value("max", json(0))));
    return out;
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
                            if (s.is_string()) t.by_lang[lang == "default" ? std::string() : lang][p2m::lower(prefix + name)] = no_cr(s.get<std::string>());
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

        // edits of shipped dialogue infos and teaching limits
        {
            const DialogOps da = legacy_dialog_ops(a), db = export_dialog_ops(b);
            for (auto& [info, ops] : da) {
                st.dialog_ops += static_cast<int>(ops.size());
                auto it = db.find(info);
                if (it == db.end()) diff.push_back("dialogue info " + info + ": the edits are missing in the export");
                else if (it->second != ops) diff.push_back("dialogue info " + info + ": the edits differ");
            }
            for (auto& [info, ops] : db)
                if (!da.count(info) && !ops.empty()) diff.push_back("dialogue info " + info + ": edits only in the export");
            const json wa = data_file(a, "data/world.lua"), wb = data_file(b, "data/world.lua");
            const auto la = teacher_set(wa.is_object() && wa.contains("teachers") ? wa["teachers"] : json()), lb = teacher_set(wb.is_object() && wb.contains("teachers") ? wb["teachers"] : json());
            st.teachers = static_cast<int>(la.size());
            for (auto& x : la)
                if (!lb.count(x)) diff.push_back("teaching limit " + x + ": missing in the export");
            for (auto& x : lb)
                if (!la.count(x)) diff.push_back("teaching limit " + x + ": only in the export");
        }

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

std::vector<std::string> verify_world_edits(const std::vector<LayerPair>& layers, const std::string& export_dir, CompareStats* stats) {
    std::vector<std::string> diff;
    CompareStats local;
    CompareStats& st = stats ? *stats : local;
    const json w = [&] {
        try {
            return data_file(u8(export_dir), "data/world.lua");
        } catch (const std::exception&) {
            return json();
        }
    }();
    if (!w.is_object()) return diff;
    struct Pair {
        const GEntity* base = nullptr;
        const GEntity* mod = nullptr;
    };
    std::map<std::string, Pair> by_text;  // by_id text -> the entity in the game's and in the mod's layer
    std::vector<const GEntity*> added;
    for (const LayerPair& L : layers) {
        for (auto& e : L.base.entities)
            if (e.name != "DRE") by_text[entity_guid_text(e.id)].base = &e;
        for (auto& e : L.mod.entities)
            if (e.name != "DRE") {
                Pair& pr = by_text[entity_guid_text(e.id)];
                pr.mod = &e;
                if (!pr.base) added.push_back(&e);
            }
    }
    auto vec = [](const json& a) { return std::array<double, 3>{a.size() > 0 ? a[0].get<double>() : 0, a.size() > 1 ? a[1].get<double>() : 0, a.size() > 2 ? a[2].get<double>() : 0}; };
    for (auto& m : w.value("moves", json::array())) {
        ++st.world_edits;
        auto it = by_text.find(m.value("id", std::string()));
        if (it == by_text.end() || !it->second.mod || !it->second.base) {
            diff.push_back("move " + m.value("id", std::string()) + ": no such entity in the changed layers");
            continue;
        }
        spass::pose::Matrix mm, mb;
        std::copy(it->second.mod->matrix.begin(), it->second.mod->matrix.end(), mm.begin());
        std::copy(it->second.base->matrix.begin(), it->second.base->matrix.end(), mb.begin());
        const auto pos = vec(m.value("position", json::array())), rot = vec(m.value("rotation", json::array()));
        const auto got = spass::pose::compose(static_cast<float>(pos[0]), static_cast<float>(pos[1]), static_cast<float>(pos[2]),
                                              spass::pose::Angles{static_cast<float>(rot[0]), static_cast<float>(rot[1]), static_cast<float>(rot[2])}, spass::pose::scale_of(mb));
        const auto scale = spass::pose::scale_of(mm);
        bool ok = true;
        for (size_t i = 0; i < 16; ++i) ok = ok && std::fabs(got[i] - mm[i]) <= (i >= 12 ? 0.05f : 0.01f * std::max(1.0f, scale[i < 12 ? i / 4 : 0]));
        if (!ok) diff.push_back("move " + m.value("name", std::string()) + ": the pose does not recompose the mod's matrix");
    }
    for (auto& r : w.value("removals", json::array())) {
        ++st.world_edits;
        auto it = by_text.find(r.value("id", std::string()));
        if (it == by_text.end() || !it->second.base || it->second.mod) diff.push_back("removal " + r.value("name", std::string()) + ": the entity is not removed by the mod's layer");
    }
    for (auto& s : w.value("spawns", json::array())) {
        ++st.world_edits;
        const auto pos = vec(s.value("position", json::array()));
        bool found = false;
        for (const GEntity* e : added)
            found = found || (std::fabs(e->matrix[12] - pos[0]) < 0.05 && std::fabs(e->matrix[13] - pos[1]) < 0.05 && std::fabs(e->matrix[14] - pos[2]) < 0.05);
        if (!found) diff.push_back("spawn " + s.value("template", std::string()) + ": no new entity of the mod's layers at its position");
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
            std::vector<std::string> layer_problems;
            for (auto& d : verify_world_edits(parse_layer_pairs(in, layer_problems), u8str(tmp), &cmp)) diff.push_back(d);
            for (auto& d : layer_problems) diff.push_back("world layer: " + d);
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
                              {"template patches", "Patches (any template's properties)"},
                              {"container fills", "World (container fills)"},
                              {"NPC equipment", "World (container fills with the equip mark)"},
                              {"entities moved", "World (moves)"},
                              {"entities removed", "World (removals)"},
                              {"entities added", "World (spawns)"},
                              {"XP rewards added", "Shipped dialogues (dialogue edits)"},
                              {"XP rewards changed", "Shipped dialogues (dialogue edits)"},
                              {"trader refills", "Shipped dialogues (dialogue edits)"},
                              {"property edits of shipped infos", "Shipped dialogues (property edits)"},
                              {"teacher limits", "Shipped dialogues (teacher limits)"},
                              {"asset files", "Assets"},
                              {"string rows used", "labels and descriptions of the objects above (Languages: project)"},
                              {"free string rows", "Strings (rows no object owns)"}};
    for (auto& w : where)
        if (auto it = rep.counts.find(w[0]); it != rep.counts.end()) md << "| " << w[0] << " | " << it->second << " | " << w[1] << " |\n";
    md << "\n";
    if (rep.unmapped.empty()) {
        md << "Everything the converter produced has a place in the project.\n\n";
    } else {
        md << "### Not representable in the project\n\nListed here with the reason (anything that was kept as data is in `unmapped/`); the editor's export does not apply these.\n\n| Area | What | Why |\n|---|---|---|\n";
        for (auto& e : rep.unmapped) md << "| " << md_cell(e.area) << " | " << md_cell(e.name) << " | " << md_cell(e.reason) << " |\n";
        md << "\n";
    }
    if (!rep.classic.empty()) {
        md << "### World layers against the classic game\n\n| Category | Count |\n|---|---:|\n";
        for (auto& [k, v] : rep.classic) md << "| " << md_cell(k) << " | " << v << " |\n";
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
            md << "**equivalent** (" << cmp.templates << " templates with " << cmp.settings << " settings, " << cmp.texts << " texts, " << cmp.fills << " container fills, " << cmp.dialog_ops
               << " dialogue edits, " << cmp.teachers << " teaching limits, " << cmp.assets << " asset files compared" << (cmp.world_edits ? ", " + std::to_string(cmp.world_edits) + " world edits recomputed from the layers" : std::string())
               << ").\n\n";
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
    if (!rep.classic.empty()) {
        json cl = json::object();
        for (auto& [k, v] : rep.classic) cl[k] = v;
        man["world_against_classic"] = cl;
    }
    man["validation"] = json{{"game_data", game != nullptr}, {"errors", errors}, {"warnings", warnings}};
    if (verified)
        man["verify"] = json{{"differences", diff.size()}, {"templates", cmp.templates}, {"settings", cmp.settings}, {"texts", cmp.texts}, {"container_fills", cmp.fills}, {"asset_files", cmp.assets},
                            {"dialogue_edits", cmp.dialog_ops}, {"teacher_limits", cmp.teachers}, {"world_edits", cmp.world_edits}};
    r.manifest = man.dump(2) + "\n";

    std::ostringstream sum;
    sum << "project: " << p2m::bare_name(in.project_file) << ": " << p.items.size() << " items, " << p.recipes.size() << " recipes, " << p.npc_templates.size() << " NPC templates, "
        << p.spells.size() << " spells, " << p.world.containers.size() << " container fills, " << p.assets.size() << " assets; not mapped: " << rep.unmapped.size() << "; editor validation: " << errors
        << " errors, " << warnings << " warnings" << (verified ? (diff.empty() ? "; verified: export equals the converted mod (" + std::to_string(cmp.settings) + " settings of " + std::to_string(cmp.templates) + " templates, " +
                                                                                   std::to_string(cmp.texts) + " texts, " + std::to_string(cmp.fills) + " fills, " +
                                                                                   std::to_string(cmp.dialog_ops) + " dialogue edits, " + std::to_string(cmp.teachers) + " teaching limits, " + std::to_string(cmp.assets) +
                                                                                   " assets" + (cmp.world_edits ? ", " + std::to_string(cmp.world_edits) + " world edits" : std::string()) + ")"
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
