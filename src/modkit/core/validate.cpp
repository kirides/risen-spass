#include "core/validate.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "common/template_id.h"
#include "core/assets.h"
#include "core/catalog.h"
#include "core/image.h"
#include "core/dialog.h"
#include "core/events.h"
#include "core/layers.h"
#include "core/behavior.h"
#include "core/quest.h"
#include "core/spells.h"

namespace mk {

std::string mod_id_problem(const std::string& id) {
    if (id.empty()) return "the mod id is empty";
    for (unsigned char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return "the mod id may only contain a-z, 0-9 and _";
    return {};
}

std::string template_name_problem(const std::string& name) { return spass::templates::nameProblem(name); }

std::string npc_name_problem(const std::string& name) {
    if (name.empty()) return "the NPC needs a unique name";
    if (name.size() > 63) return "the name is longer than 63 characters";
    for (unsigned char c : name)
        if (!(std::isalnum(c) && c < 0x80) && c != '_' && c != '-') return "the name may only contain letters, digits, _ and -";
    return {};
}

size_t error_count(const std::vector<Problem>& v) {
    return static_cast<size_t>(std::count_if(v.begin(), v.end(), [](const Problem& p) { return p.severity == Severity::Error; }));
}

namespace {

struct Ctx {
    const Project& p;
    const GameData* g;
    const World* w;
    std::vector<Problem>& out;
    std::set<std::string> skills;
    std::set<std::string> new_templates;  // lower-case names the project creates
    std::set<std::string> asset_files;    // lower-case bare names of the project's asset files
    std::set<std::string> icons;          // lower-case icon names of the shipped items
    bool own_icon_lists = false;          // the project brings .ximl image lists (their icon names are not checked)

    void add(Severity s, const char* area, const std::string& uid, const std::string& subject, const std::string& msg) {
        out.push_back({s, area, uid, subject, msg});
    }
    bool is_project_template(const std::string& name) const { return new_templates.count(p2m::lower(name)) != 0; }
    // a mesh / actor name as a template takes it ("X.xmsh"): in the game archives or among the project's asset files
    bool asset_known(const std::string& raw) const {
        const std::string c = compiled_name(raw);
        if (asset_files.count(p2m::lower(c)) || asset_files.count(p2m::lower(raw))) return true;
        return !g || g->exists(c);
    }
};

void check_skills(Ctx& c, const char* area, const std::string& uid, const std::string& subject, const IntMap& m, const char* what) {
    for (auto& [k, v] : m)
        if (!c.skills.empty() && !c.skills.count(k)) c.add(Severity::Error, area, uid, subject, std::string(what) + ": unknown skill '" + k + "'");
}

void check_properties(Ctx& c, const char* area, const std::string& uid, const std::string& subject, const PropEdits& props, const TemplateInfo* t) {
    for (auto& [set, list] : props) {
        if (list.empty()) continue;
        if (t && !t->has_set(set)) {
            c.add(Severity::Error, area, uid, subject, "the template has no property set " + set + " (a set cannot be added at run time)");
            continue;
        }
        for (auto& [name, v] : list) {
            if (v.is_null() || v.is_object()) c.add(Severity::Error, area, uid, subject, set + "." + name + ": the value must be a number, text, true/false or a vector");
            if (t && !t->props.has(set + "." + name))
                c.add(Severity::Warning, area, uid, subject, set + "." + name + " is not in the template file (it may still exist with a default value)");
        }
    }
}

void check_enum(Ctx& c, const char* area, const std::string& uid, const std::string& subject, const std::optional<std::string>& v, const char* type) {
    if (!v || v->empty()) return;
    const auto names = enum_names(c.g, type);
    if (!names.empty() && std::find(names.begin(), names.end(), *v) == names.end())
        c.add(Severity::Error, area, uid, subject, std::string(type) + ": unknown value '" + *v + "'");
}

void check_items(Ctx& c) {
    std::map<std::string, int> seen;
    for (auto& it : c.p.items) {
        const char* A = "Items";
        const std::string subj = it.name.empty() ? "(unnamed item)" : it.name;
        if (++seen[p2m::lower(it.name)] == 2) c.add(Severity::Error, A, it.uid, subj, "the project has this item twice");
        const TemplateInfo* target = nullptr;
        if (it.mode == Mode::New) {
            if (const std::string why = template_name_problem(it.name); !why.empty()) c.add(Severity::Error, A, it.uid, subj, why);
            if (c.g && c.g->tpl(it.name)) c.add(Severity::Error, A, it.uid, subj, "a game template with this name exists: choose a new, prefixed name");
            if (it.base.empty()) {
                c.add(Severity::Error, A, it.uid, subj, "a new item needs a base item to clone");
            } else if (c.g) {
                target = c.g->tpl(it.base);
                if (!target) c.add(Severity::Error, A, it.uid, subj, "the base '" + it.base + "' is no template of the game");
                else if (target->kind != TplKind::Item && target->kind != TplKind::Recipe)
                    c.add(Severity::Error, A, it.uid, subj, "the base '" + it.base + "' is no item (" + kind_name(target->kind) + ")");
                else if (target->multi) c.add(Severity::Error, A, it.uid, subj, "the base '" + it.base + "' is a multi template (cannot be cloned)");
            }
            if (it.label.empty()) c.add(Severity::Warning, A, it.uid, subj, "no label: the item shows the base's name");
        } else {
            if (it.name.empty()) c.add(Severity::Error, A, it.uid, subj, "choose the shipped item to change");
            else if (c.g) {
                target = c.g->tpl(it.name);
                if (!target) c.add(Severity::Error, A, it.uid, subj, "no game template with this name");
                else if (target->kind != TplKind::Item && target->kind != TplKind::Recipe)
                    c.add(Severity::Error, A, it.uid, subj, std::string("not an item (") + kind_name(target->kind) + ")");
            }
        }
        if (it.requirements) check_skills(c, A, it.uid, subj, *it.requirements, "requirements");
        if (it.modifies) check_skills(c, A, it.uid, subj, *it.modifies, "modifiers");
        for (auto& [skill, mode] : it.modifier_modes) {
            static const char* modes[] = {"add", "set", "max", "percent"};
            if (std::find(std::begin(modes), std::end(modes), mode) == std::end(modes))
                c.add(Severity::Error, A, it.uid, subj, "modifier " + skill + ": unknown mode '" + mode + "' (valid: add, set, max, percent)");
            if (it.modifies && !it.modifies->count(skill)) c.add(Severity::Warning, A, it.uid, subj, "a mode is kept for the skill " + skill + ", which is not among the modifiers");
        }
        check_enum(c, A, it.uid, subj, it.use_type, "gEItemUseType");
        check_enum(c, A, it.uid, subj, it.category, "gEItemCategory");
        if (it.mesh && !it.mesh->empty() && !c.asset_known(*it.mesh))
            c.add(Severity::Error, A, it.uid, subj, "the mesh '" + *it.mesh + "' is neither in the game archives nor among the project's asset files");
        if (it.icon && !it.icon->empty()) {
            if (is_picture(*it.icon)) {
                if (!find_asset(c.p, *it.icon)) c.add(Severity::Error, A, it.uid, subj, "the icon picture '" + *it.icon + "' is not one of the project's asset files");
            } else if (c.g && !c.icons.count(p2m::lower(*it.icon)) && !c.own_icon_lists) {
                c.add(Severity::Warning, A, it.uid, subj, "the icon '" + *it.icon + "' is not used by any shipped item (the layer refuses icons it cannot resolve)");
            }
        }
        if (it.value && *it.value < 0) c.add(Severity::Error, A, it.uid, subj, "the value must not be negative");
        check_properties(c, A, it.uid, subj, it.properties, target);
    }
    for (auto& g : c.p.item_groups) {
        const std::string subj = !g.group.empty() ? g.group : !g.match.empty() ? g.match : "(item group)";
        if (g.group.empty() && g.match.empty() && g.names.empty()) c.add(Severity::Error, "Items", g.uid, subj, "an item group needs a group path, a pattern or names");
        if (!g.value_mult && !g.damage_mult) c.add(Severity::Warning, "Items", g.uid, subj, "the item group changes nothing");
        if ((g.value_mult && *g.value_mult < 0) || (g.damage_mult && *g.damage_mult < 0)) c.add(Severity::Error, "Items", g.uid, subj, "a factor must not be negative");
    }
}

// an item name a recipe refers to: a shipped item or one the project creates
bool item_known(const Ctx& c, const std::string& name) {
    if (c.is_project_template(name)) return true;
    if (!c.g) return true;
    const TemplateInfo* t = c.g->tpl(name);
    return t && (t->kind == TplKind::Item || t->kind == TplKind::Recipe);
}

void check_spells(Ctx& c) {
    const char* A = "Spells";
    std::map<std::string, int> seen;
    const std::set<std::string> projectile_keys_unused;
    for (auto& s : c.p.spells) {
        const std::string subj = s.name.empty() ? "(unnamed spell)" : s.name;
        if (++seen[(s.mode == Mode::New ? "n:" : "e:") + p2m::lower(s.name)] == 2) c.add(Severity::Error, A, s.uid, subj, "the project has this spell twice");
        const ShippedSpell* shipped = nullptr;
        if (s.mode == Mode::Edit) {
            shipped = shipped_spell(s.name);
            if (s.name.empty()) c.add(Severity::Error, A, s.uid, subj, "choose the shipped spell to change");
            else if (!shipped) c.add(Severity::Error, A, s.uid, subj, "'" + s.name + "' is no shipped spell (see the list in the Spells panel)");
        } else {
            if (const std::string why = template_name_problem(s.name); !why.empty()) c.add(Severity::Error, A, s.uid, subj, why);
            shipped = shipped_spell(s.base);
            if (s.base.empty()) c.add(Severity::Error, A, s.uid, subj, "a new spell needs a shipped spell as its base");
            else if (!shipped) c.add(Severity::Error, A, s.uid, subj, "the base '" + s.base + "' is no shipped spell");
            if (shipped) {
                std::set<std::string> forms;
                for (auto& f : s.forms) {
                    if (std::find(shipped->forms.begin(), shipped->forms.end(), f) == shipped->forms.end())
                        c.add(Severity::Error, A, s.uid, subj, "the form '" + f + "' does not exist for " + s.base + " (" + (shipped->projectile ? "crystal, scroll" : "rune, scroll, orb") + ")");
                    if (!forms.insert(f).second) c.add(Severity::Error, A, s.uid, subj, "the form '" + f + "' is listed twice");
                }
                for (auto& f : s.forms.empty() ? std::vector<std::string>{shipped->forms.front()} : s.forms) {
                    const std::string t = new_spell_template(s.name, f);
                    if (c.g && c.g->tpl(t)) c.add(Severity::Error, A, s.uid, subj, "a game template named " + t + " exists: choose another name");
                }
            }
            if (s.label.empty()) c.add(Severity::Warning, A, s.uid, subj, "no label: the item shows the base spell's name");
            if (s.value && *s.value < 0) c.add(Severity::Error, A, s.uid, subj, "the value must not be negative");
        }
        for (auto* v : {&s.mana_cost, &s.scroll_mana_cost, &s.orb_mana_cost})
            if (*v && **v < 0) c.add(Severity::Error, A, s.uid, subj, "a mana cost must not be negative");
        if (s.mana_mult && *s.mana_mult < 0) c.add(Severity::Error, A, s.uid, subj, "the mana factor must not be negative");
        if (s.mana_cost && s.mana_mult) c.add(Severity::Error, A, s.uid, subj, "a mana cost and a mana factor exclude each other");
        if (s.requirements) check_skills(c, A, s.uid, subj, *s.requirements, "requires");
        const bool proj_setting = !s.damage.is_null() || s.damage_mult || s.speed || s.speed_mult || s.radius || s.radius_mult || s.damage_type || s.effect;
        const bool cost_setting = s.scroll_mana_cost || s.orb_mana_cost;
        if (shipped && !shipped->projectile && proj_setting) c.add(Severity::Error, A, s.uid, subj, "damage, speed, radius, effect and damage type only apply to Fireball, Frost and Missile");
        if (shipped && shipped->projectile && cost_setting) c.add(Severity::Error, A, s.uid, subj, "scroll and orb costs only apply to rune spells (a projectile spell has a crystal)");
        if (shipped && shipped->projectile) {
            if (s.damage_mult && s.damage_mult < 0) c.add(Severity::Error, A, s.uid, subj, "the damage factor must not be negative");
            if (s.speed_mult && *s.speed_mult < 0) c.add(Severity::Error, A, s.uid, subj, "the speed factor must not be negative");
            if (s.radius_mult && *s.radius_mult < 0) c.add(Severity::Error, A, s.uid, subj, "the radius factor must not be negative");
            if (s.damage_mult && !s.damage.is_null()) c.add(Severity::Warning, A, s.uid, subj, "a damage value and a damage factor are both set: check the layer's report");
            if (s.damage_type) {
                const auto& t = spell_damage_types();
                if (std::find(t.begin(), t.end(), *s.damage_type) == t.end()) c.add(Severity::Error, A, s.uid, subj, "unknown damage type '" + *s.damage_type + "'");
            }
            if (s.damage.is_array()) {
                if (s.damage.size() > shipped->projectiles.size())
                    c.add(Severity::Error, A, s.uid, subj, "the damage list has " + std::to_string(s.damage.size()) + " values, " + shipped->name + " has " + std::to_string(shipped->projectiles.size()) + " projectile levels");
                for (auto& v : s.damage)
                    if (!v.is_number()) c.add(Severity::Error, A, s.uid, subj, "the damage list must hold numbers");
            } else if (!s.damage.is_null() && !s.damage.is_number()) {
                c.add(Severity::Error, A, s.uid, subj, "the damage must be a number or a list of numbers");
            }
        }
        if (s.mode == Mode::Edit && shipped && !s.mana_cost && !s.mana_mult && !s.scroll_mana_cost && !s.orb_mana_cost && !s.requirements && !s.consume && s.damage.is_null() &&
            !s.damage_mult && !s.speed && !s.speed_mult && !s.radius && !s.radius_mult && !s.damage_type && !s.effect && s.properties.empty() && s.extra.empty())
            c.add(Severity::Warning, A, s.uid, subj, "the spell changes nothing");
        check_properties(c, A, s.uid, subj, s.properties, nullptr);
    }
}

void check_npc_groups(Ctx& c) {
    const char* A = "NPC groups";
    int n = 0;
    std::set<std::string> npc_skills;
    for (auto& s : npc_skill_names(c.g)) npc_skills.insert(s);
    for (auto& g : c.p.npc_groups) {
        ++n;
        const std::string subj = !g.group.empty() ? g.group : !g.match.empty() ? g.match : !g.names.empty() ? g.names.front() : "group " + std::to_string(n);
        if (g.group.empty() && g.match.empty() && g.exclude.empty() && g.names.empty())
            c.add(Severity::Warning, A, g.uid, subj, "no selector: the group applies to every NPC and monster of the game");
        const bool any = g.hp_mult || g.mana_mult || g.level_mult || g.level_add || g.xp_mult || g.damage_mult || g.skills_mult || g.skills_add || g.guild || g.species || g.attitude ||
                         g.bravery || g.damage_calc || g.ai_group || !g.extra.empty();
        if (!any) c.add(Severity::Warning, A, g.uid, subj, "the group changes nothing");
        for (auto* v : {&g.hp_mult, &g.mana_mult, &g.level_mult, &g.xp_mult, &g.damage_mult})
            if (*v && **v < 0) c.add(Severity::Error, A, g.uid, subj, "a factor must not be negative");
        if (g.skills_mult)
            for (auto& [k, v] : *g.skills_mult) {
                if (!npc_skills.empty() && !npc_skills.count(k)) c.add(Severity::Error, A, g.uid, subj, "skills_mult: unknown skill '" + k + "'");
                if (v < 0) c.add(Severity::Error, A, g.uid, subj, "skills_mult." + k + " must not be negative");
            }
        if (g.skills_add) check_skills(c, A, g.uid, subj, *g.skills_add, "skills_add");
        // patterns and what the selector matches
        std::string perr;
        for (const std::string* pat : {&g.match, &g.exclude})
            if (!pat->empty()) {
                lua_pattern_matches(*pat, "x", perr);
                if (!perr.empty()) c.add(Severity::Error, A, g.uid, subj, "bad Lua pattern '" + *pat + "': " + perr);
            }
        if (c.g && perr.empty()) {
            std::string err;
            const auto found = npc_group_templates(g, *c.g, err);
            if (!err.empty()) c.add(Severity::Error, A, g.uid, subj, err);
            else if (found.empty()) c.add(Severity::Error, A, g.uid, subj, "the selector matches no NPC or monster template (the layer refuses a selector that matches nothing)");
            for (auto& name : g.names) {
                const TemplateInfo* t = c.g->tpl(name);
                if (!t) c.add(Severity::Error, A, g.uid, subj, "names: '" + name + "' is no template of the game");
                else if (t->kind != TplKind::Npc && t->kind != TplKind::Monster) c.add(Severity::Error, A, g.uid, subj, "names: '" + name + "' is no NPC or monster");
            }
            if (!g.group.empty()) {
                bool any_in = false;
                for (auto& t : c.g->templates())
                    if (t->group == g.group || t->group.rfind(g.group + "/", 0) == 0) {
                        any_in = true;
                        break;
                    }
                if (!any_in) c.add(Severity::Error, A, g.uid, subj, "no templates in the group '" + g.group + "' (docs/TEMPLATES.md)");
            }
        }
    }
}

void check_recipes(Ctx& c) {
    std::map<std::string, int> seen;
    const auto& stations = recipe_stations();
    for (auto& r : c.p.recipes) {
        const char* A = "Recipes";
        const std::string subj = r.name.empty() ? "(unnamed recipe)" : r.name;
        if (++seen[p2m::lower(r.name)] == 2) c.add(Severity::Error, A, r.uid, subj, "the project has this recipe twice");
        const TemplateInfo* target = nullptr;
        if (r.mode == Mode::New) {
            if (const std::string why = template_name_problem(r.name); !why.empty()) c.add(Severity::Error, A, r.uid, subj, why);
            if (c.g && c.g->tpl(r.name)) c.add(Severity::Error, A, r.uid, subj, "a game template with this name exists: choose a new, prefixed name");
            if (r.station.empty()) c.add(Severity::Error, A, r.uid, subj, "a new recipe needs a station");
            if (!r.ingredients || r.ingredients->empty()) c.add(Severity::Error, A, r.uid, subj, "a new recipe needs at least one ingredient");
            if (!r.result || r.result->empty()) c.add(Severity::Error, A, r.uid, subj, "a new recipe needs a result item");
            if (!r.base.empty() && c.g) {
                target = c.g->tpl(r.base);
                if (!target) c.add(Severity::Error, A, r.uid, subj, "the base '" + r.base + "' is no template of the game");
                else if (!target->has_set("gCRecipe_PS")) c.add(Severity::Error, A, r.uid, subj, "the base '" + r.base + "' is no recipe item (no gCRecipe_PS)");
            }
            if (r.base.empty() && c.g) target = c.g->tpl("It_Recipe_Health_01");
        } else {
            if (r.name.empty()) c.add(Severity::Error, A, r.uid, subj, "choose the shipped recipe to change");
            else if (c.g) {
                target = c.g->tpl(r.name);
                if (!target) c.add(Severity::Error, A, r.uid, subj, "no game template with this name");
                else if (target->kind != TplKind::Recipe) c.add(Severity::Error, A, r.uid, subj, std::string("not a recipe (") + kind_name(target->kind) + ")");
            }
        }
        if (!r.station.empty() && std::find(stations.begin(), stations.end(), r.station) == stations.end())
            c.add(Severity::Error, A, r.uid, subj, "unknown station '" + r.station + "'");
        if (r.ingredients) {
            if (r.ingredients->size() > 16) c.add(Severity::Error, A, r.uid, subj, "at most 16 ingredients");
            std::map<std::string, int> twice;
            for (auto& i : *r.ingredients) {
                if (i.item.empty()) c.add(Severity::Error, A, r.uid, subj, "an ingredient has no item");
                else if (!item_known(c, i.item)) c.add(Severity::Error, A, r.uid, subj, "ingredient '" + i.item + "' is no item of the game or the project");
                if (i.count < 1) c.add(Severity::Error, A, r.uid, subj, "ingredient '" + i.item + "': the count must be 1 or more");
                if (++twice[i.item] == 2) c.add(Severity::Warning, A, r.uid, subj, "ingredient '" + i.item + "' is listed twice");
            }
        }
        if (r.result && !r.result->empty() && !item_known(c, *r.result))
            c.add(Severity::Error, A, r.uid, subj, "result '" + *r.result + "' is no item of the game or the project");
        if (r.skills) check_skills(c, A, r.uid, subj, *r.skills, "skills");
        check_properties(c, A, r.uid, subj, r.properties, target);
    }
}

bool npc_template_known(const Ctx& c, const std::string& name) {
    for (auto& n : c.p.npc_templates)
        if (n.mode == Mode::New && n.name == name) return true;
    if (!c.g) return true;
    const TemplateInfo* t = c.g->tpl(name);
    return t && (t->kind == TplKind::Npc || t->kind == TplKind::Monster);
}

void check_choice(Ctx& c, const char* area, const std::string& uid, const std::string& subject, const std::optional<std::string>& v, const char* what,
                  const std::vector<std::string>& names) {
    if (v && std::find(names.begin(), names.end(), *v) == names.end()) c.add(Severity::Error, area, uid, subject, std::string(what) + ": unknown value '" + *v + "'");
}

void check_npc_templates(Ctx& c) {
    std::map<std::string, int> seen;
    std::set<std::string> npc_skills;
    for (auto& s : npc_skill_names(c.g)) npc_skills.insert(s);
    for (auto& n : c.p.npc_templates) {
        const char* A = "NPCs";
        const std::string subj = n.name.empty() ? "(unnamed NPC template)" : n.name;
        if (++seen[p2m::lower(n.name)] == 2) c.add(Severity::Error, A, n.uid, subj, "the project has this NPC template twice");
        const TemplateInfo* target = nullptr;
        if (n.mode == Mode::New) {
            if (const std::string why = template_name_problem(n.name); !why.empty()) c.add(Severity::Error, A, n.uid, subj, why);
            if (c.g && c.g->tpl(n.name)) c.add(Severity::Error, A, n.uid, subj, "a game template with this name exists: choose a new, prefixed name");
            if (n.base.empty()) c.add(Severity::Error, A, n.uid, subj, "a new NPC template needs a base NPC or monster to clone");
            else if (c.g) {
                target = c.g->tpl(n.base);
                if (!target) c.add(Severity::Error, A, n.uid, subj, "the base '" + n.base + "' is no template of the game");
                else if (target->kind != TplKind::Npc && target->kind != TplKind::Monster)
                    c.add(Severity::Error, A, n.uid, subj, "the base '" + n.base + "' is no NPC or monster (" + kind_name(target->kind) + ")");
                else if (target->multi) c.add(Severity::Error, A, n.uid, subj, "the base '" + n.base + "' is a multi template (cannot be cloned)");
            }
            if (n.label.empty()) c.add(Severity::Warning, A, n.uid, subj, "no label: the NPC shows the base's name");
        } else if (n.name.empty()) {
            c.add(Severity::Error, A, n.uid, subj, "choose the shipped NPC or monster to change");
        } else if (c.g) {
            target = c.g->tpl(n.name);
            if (!target) c.add(Severity::Error, A, n.uid, subj, "no game template with this name");
            else if (target->kind != TplKind::Npc && target->kind != TplKind::Monster)
                c.add(Severity::Error, A, n.uid, subj, std::string("not an NPC or monster (") + kind_name(target->kind) + ")");
        }
        if (n.skills)
            for (auto& [k, v] : *n.skills)
                if (!npc_skills.empty() && !npc_skills.count(k)) c.add(Severity::Error, A, n.uid, subj, "skills: unknown or not settable skill '" + k + "'");
        if (n.hp_max && *n.hp_max < 1) c.add(Severity::Error, A, n.uid, subj, "hit points must be 1 or more");
        if (n.hp && n.hp_max && *n.hp > *n.hp_max) c.add(Severity::Error, A, n.uid, subj, "the hit points are above the maximum");
        if (n.level && *n.level < 1) c.add(Severity::Error, A, n.uid, subj, "the level must be 1 or more");
        if (n.level && n.xp) c.add(Severity::Warning, A, n.uid, subj, "level and xp both set: xp sets the level (a kill pays 10 * level)");
        check_choice(c, A, n.uid, subj, n.guild, "guild", guild_names());
        check_choice(c, A, n.uid, subj, n.species, "species", species_names());
        check_choice(c, A, n.uid, subj, n.attitude, "attitude", attitude_names());
        check_choice(c, A, n.uid, subj, n.bravery, "bravery", bravery_names());
        check_choice(c, A, n.uid, subj, n.damage_calc, "damage calculation", damage_calc_names());
        check_choice(c, A, n.uid, subj, n.damage_type, "damage type", damage_type_names());
        if (n.loot)
            for (auto& [item, amount] : *n.loot) {
                if (!item_known(c, item)) c.add(Severity::Error, A, n.uid, subj, "loot '" + item + "' is no item of the game or the project");
                if (amount < 0) c.add(Severity::Error, A, n.uid, subj, "loot '" + item + "': the amount must not be negative");
            }
        if (n.mesh && !n.mesh->empty() && !c.asset_known(*n.mesh))
            c.add(Severity::Error, A, n.uid, subj, "the actor '" + *n.mesh + "' is neither in the game archives nor among the project's asset files");
        check_properties(c, A, n.uid, subj, n.properties, target);
    }
}

void check_routine_point(Ctx& c, const std::string& uid, const std::string& subj, const json& pt, const std::string& what) {
    if (!pt.is_object()) {
        c.add(Severity::Error, "NPCs", uid, subj, "routine " + what + ": not a point");
        return;
    }
    for (const char* k : {"dx", "dy", "dz"})
        if (!pt.contains(k) || !pt[k].is_number()) c.add(Severity::Error, "NPCs", uid, subj, "routine " + what + ": " + k + " is missing");
}

// the quests and items a condition (exported form, core/graph.h) names: a quest of the project or of the game, an item of either
void check_condition(Ctx& c, const char* A, const std::string& uid, const std::string& subj, const json& cond) {
    std::vector<const json*> todo{&cond};
    while (!todo.empty()) {
        const json& w = *todo.back();
        todo.pop_back();
        if (!w.is_array() || w.empty()) continue;
        const std::string k = w[0].is_string() ? w[0].get<std::string>() : "";
        if (k == "all" || k == "any") {
            if (w.size() > 1)
                for (auto& x : w[1]) todo.push_back(&x);
        } else if (k == "not") {
            if (w.size() > 1) todo.push_back(&w[1]);
        } else if (k == "quest" && w.size() > 1 && w[1].is_string() && !c.p.quest(w[1].get<std::string>())) {
            const std::string q = w[1].get<std::string>();
            if (!c.g || c.g->quests().empty())
                c.add(Severity::Warning, A, uid, subj, "a condition names the quest '" + q + "' that is not in the project: it must be a quest of the game");
            else if (!c.g->quest(q))
                c.add(Severity::Warning, A, uid, subj, "a condition names the quest '" + q + "', no quest of the project or the game (a quest another mod makes?)");
        } else if (k == "item" && w.size() > 1 && w[1].is_string() && !item_known(c, w[1].get<std::string>())) {
            c.add(Severity::Error, A, uid, subj, "a condition names '" + w[1].get<std::string>() + "', no item of the game or the project");
        }
    }
}

// mod layers: names, members, switch conditions
void check_layers(Ctx& c) {
    const char* A = "Layers";
    std::map<std::string, int> seen;
    std::map<std::string, std::string> npc_in, spawn_in, container_in;
    for (auto& l : c.p.layers) {
        const std::string subj = l.name.empty() ? "(unnamed layer)" : l.name;
        if (l.name.empty()) c.add(Severity::Error, A, l.uid, subj, "the layer needs a name");
        for (unsigned char ch : l.name)
            if (!(std::isalnum(ch) && ch < 0x80) && ch != '_') {
                c.add(Severity::Error, A, l.uid, subj, "the layer name may only contain letters, digits and _");
                break;
            }
        if (++seen[l.name] == 2) c.add(Severity::Error, A, l.uid, subj, "the project has two layers with this name");
        for (auto& n : l.npcs) {
            if (!c.p.npc(n)) c.add(Severity::Error, A, l.uid, subj, "the NPC '" + n + "' is not a placed NPC of the project");
            auto [it, fresh] = npc_in.emplace(n, subj);
            if (!fresh) c.add(Severity::Error, A, l.uid, subj, "the NPC '" + n + "' is also in the layer '" + it->second + "': an object belongs to one layer");
        }
        for (auto& u : l.spawns) {
            bool found = false;
            for (auto& s : c.p.world.spawns) found |= s.uid == u;
            if (!found) c.add(Severity::Error, A, l.uid, subj, "a world spawn of this layer does not exist any more");
            auto [it, fresh] = spawn_in.emplace(u, subj);
            if (!fresh) c.add(Severity::Error, A, l.uid, subj, "a world spawn is also in the layer '" + it->second + "'");
        }
        for (auto& u : l.containers) {
            bool found = false;
            for (auto& s : c.p.world.containers) found |= s.uid == u;
            if (!found) c.add(Severity::Error, A, l.uid, subj, "a container of this layer does not exist any more");
            auto [it, fresh] = container_in.emplace(u, subj);
            if (!fresh) c.add(Severity::Error, A, l.uid, subj, "a container is also in the layer '" + it->second + "'");
        }
        std::vector<DialogIssue> issues;
        LayerDef only;  // layers_data checks every layer: look at this one's conditions alone
        Project one;
        one.layers = {l};
        layers_data(one, &issues);
        for (auto& i : issues) c.add(i.error ? Severity::Error : Severity::Warning, A, l.uid, subj, i.message);
        for (const json* w : {&l.enable_when, &l.disable_when})
            for (auto& [kind, name] : event_condition_refs(*w)) {
                if (kind == "item" && !item_known(c, name)) c.add(Severity::Error, A, l.uid, subj, "'" + name + "' is no item of the game or the project");
                else if (kind == "quest" && !c.p.quest(name) && c.g && !c.g->quests().empty() && !c.g->quest(name))
                    c.add(Severity::Warning, A, l.uid, subj, "the quest '" + name + "' is not in the project or the game");
                else if (kind == "skill" && !c.skills.empty() && !c.skills.count(name)) c.add(Severity::Error, A, l.uid, subj, "unknown skill '" + name + "'");
                else if (kind == "layer" && !find_layer(c.p, name)) c.add(Severity::Error, A, l.uid, subj, "the layer '" + name + "' is not in the project");
            }
        if (l.enabled && !l.enable_when.is_null()) c.add(Severity::Warning, A, l.uid, subj, "the layer is on from the start: 'switches on when' has no effect");
        if (l.npcs.empty() && l.spawns.empty() && l.containers.empty()) {
            bool used = false;
            for (auto& e : c.p.events)
                for (auto& r : event_refs(e)) used |= r.kind == "layer" && r.name == l.name;
            if (!used) c.add(Severity::Warning, A, l.uid, subj, "the layer has no members and no event graph switches or asks it");
        }
    }
}

// event graphs: the graph's own problems (core/events.cpp) and the names its nodes point at
void check_events(Ctx& c) {
    const char* A = "Events";
    std::map<std::string, int> seen;
    for (auto& e : c.p.events) {
        const std::string subj = e.name.empty() ? "(unnamed event graph)" : e.name;
        if (e.name.empty()) c.add(Severity::Error, A, e.uid, subj, "the event graph needs a name");
        if (++seen[e.name] == 2) c.add(Severity::Error, A, e.uid, subj, "the project has two event graphs with this name");
        std::vector<DialogIssue> issues;
        compile_events(c.p, e, issues);
        for (auto& i : issues) c.add(i.error ? Severity::Error : Severity::Warning, A, e.uid, subj + (i.node.empty() ? "" : " (node)"), i.message);
        for (auto& r : event_refs(e)) {
            const std::string where = subj + " (node)";
            if (r.kind == "item") {
                if (!item_known(c, r.name)) c.add(Severity::Error, A, e.uid, where, "'" + r.name + "' is no item of the game or the project");
            } else if (r.kind == "quest") {
                if (!c.p.quest(r.name) && (!c.g || c.g->quests().empty() || !c.g->quest(r.name)))
                    c.add(Severity::Warning, A, e.uid, where, "the quest '" + r.name + "' is not in the project" + (c.g && !c.g->quests().empty() ? " or the game" : ""));
            } else if (r.kind == "skill") {
                if (!c.skills.empty() && !c.skills.count(r.name)) c.add(Severity::Error, A, e.uid, where, "unknown skill '" + r.name + "'");
            } else if (r.kind == "template") {
                if (!npc_template_known(c, r.name)) c.add(Severity::Error, A, e.uid, where, "the template '" + r.name + "' is no NPC or monster of the game or the project");
            } else if (r.kind == "layer") {
                if (!find_layer(c.p, r.name)) c.add(Severity::Error, A, e.uid, where, "the layer '" + r.name + "' is not in the project (Layers panel)");
            } else if (r.kind == "sector") {
                if (c.g) {
                    // the game's sector files, read once per game data object
                    static std::map<const GameData*, std::set<std::string>> cache;
                    auto it = cache.find(c.g);
                    if (it == cache.end()) {
                        std::set<std::string> names;
                        for (auto& s : stock_sectors(*c.g)) names.insert(p2m::lower(s.name));
                        it = cache.emplace(c.g, std::move(names)).first;
                    }
                    if (!it->second.empty() && !it->second.count(p2m::lower(r.name)))
                        c.add(Severity::Error, A, e.uid, where, "'" + r.name + "' is no sector of the game's world (a file X.sec below projects/World)");
                }
            } else if (r.kind == "npc") {
                if (!c.p.npc(r.name) && !npc_template_known(c, r.name) && !(c.w && c.w->by_name(r.name)))
                    c.add(Severity::Warning, A, e.uid, where, "'" + r.name + "' is neither an NPC / template of the project or the game nor an NPC of the world");
            }
        }
    }
    // signals: one that nothing sends or nothing waits for
    std::set<std::string> sent, waited;
    for (auto& e : c.p.events)
        for (auto& n : e.graph.nodes) {
            const std::string name = n.data.value("name", std::string());
            if (n.type == "send_signal") sent.insert(name);
            if (n.type == "on_signal") waited.insert(name);
        }
    for (auto& e : c.p.events)
        for (auto& n : e.graph.nodes) {
            const std::string name = n.data.value("name", std::string());
            if (n.type == "on_signal" && !name.empty() && !sent.count(name))
                c.add(Severity::Warning, A, e.uid, e.name + " (node)", "no Send signal node sends '" + name + "' (custom.lua may call require(\"modkit.events\").fire_signal)");
            if (n.type == "send_signal" && !name.empty() && !waited.count(name)) c.add(Severity::Warning, A, e.uid, e.name + " (node)", "nothing waits for the signal '" + name + "'");
        }
}

// behaviour scripts and their attachments (the scripts' contents are checked by the Behaviours panel and the export, which read the files)
void check_behaviors(Ctx& c) {
    const char* A = "Behaviours";
    std::map<std::string, int> seen;
    for (auto& b : c.p.behaviors) {
        const std::string subj = b.name.empty() ? "(unnamed behaviour)" : b.name;
        if (const std::string why = behavior_name_problem(b.name); !why.empty()) c.add(Severity::Error, A, b.uid, subj, why);
        if (++seen[b.name] == 2) c.add(Severity::Error, A, b.uid, subj, "two behaviours have this name");
        if (b.file.empty()) c.add(Severity::Error, A, b.uid, subj, "choose the script file (.lua)");
    }
    auto check_uses = [&](const char* area, const std::string& uid, const std::string& subj, const std::vector<BehaviorUse>& uses) {
        std::set<std::string> on;
        for (auto& u : uses) {
            if (!seen.count(u.behavior)) c.add(Severity::Error, area, uid, subj, "the behaviour '" + u.behavior + "' is not in the project (Behaviours panel)");
            if (!on.insert(u.behavior).second) c.add(Severity::Warning, area, uid, subj, "the behaviour '" + u.behavior + "' is attached twice: the second replaces the first");
        }
    };
    for (auto& n : c.p.npcs) check_uses("NPCs", n.uid, n.name, n.behaviors);
    for (auto& t : c.p.npc_templates) check_uses("NPCs", t.uid, t.name, t.behaviors);
}

// a world name: one of the game's (with the game data), else a warning (another mod's world can be meant)
void check_world_name(Ctx& c, const char* A, const std::string& uid, const std::string& subj, const std::string& own) {
    if (own.empty() || !c.g || c.g->worlds().empty()) return;
    const auto& ws = c.g->worlds();
    if (std::find(ws.begin(), ws.end(), own) == ws.end())
        c.add(Severity::Warning, A, uid, subj, "the world '" + own + "' is not one of the game's (" + std::to_string(ws.size()) + " worlds): a world of another mod?");
}

void check_placed_npcs(Ctx& c) {
    std::map<std::string, int> seen;
    for (auto& n : c.p.npcs) {
        const char* A = "NPCs";
        const std::string subj = n.name.empty() ? "(unnamed NPC)" : n.name;
        if (const std::string why = npc_name_problem(n.name); !why.empty()) c.add(Severity::Error, A, n.uid, subj, why);
        if (++seen[n.name] == 2) c.add(Severity::Error, A, n.uid, subj, "two placed NPCs have this name (names are global)");
        if (n.template_name.empty()) c.add(Severity::Error, A, n.uid, subj, "choose the NPC's template");
        else if (!npc_template_known(c, n.template_name))
            c.add(Severity::Error, A, n.uid, subj, "the template '" + n.template_name + "' is no NPC or monster of the game or the project");
        for (double x : n.position)
            if (!std::isfinite(x) || std::fabs(x) > 1e7) c.add(Severity::Error, A, n.uid, subj, "the position is not a usable number");
        if (!n.offset && n.position == Vec3{0, 0, 0}) c.add(Severity::Warning, A, n.uid, subj, "the position is 0, 0, 0: place the NPC (or use an offset from the player)");
        if (n.level && *n.level < 1) c.add(Severity::Error, A, n.uid, subj, "the level must be 1 or more");
        if (n.hp_max && *n.hp_max < 1) c.add(Severity::Error, A, n.uid, subj, "hit points must be 1 or more");
        if (n.version < 1) c.add(Severity::Error, A, n.uid, subj, "the version must be 1 or more");
        check_choice(c, A, n.uid, subj, n.guild, "guild", guild_names());
        check_world_name(c, A, n.uid, subj, n.world);
        if (!n.appear_when.is_null()) check_condition(c, A, n.uid, subj, n.appear_when);
        if (!n.leave_when.is_null()) check_condition(c, A, n.uid, subj, n.leave_when);
        for (auto& i : n.items) {
            if (i.item.empty()) c.add(Severity::Error, A, n.uid, subj, "an inventory entry has no item");
            else if (!item_known(c, i.item)) c.add(Severity::Error, A, n.uid, subj, "inventory item '" + i.item + "' is no item of the game or the project");
        }
        if (!n.routine.is_null() && !n.routine.empty()) {
            if (!n.routine.is_object()) {
                c.add(Severity::Error, A, n.uid, subj, "the routine is not a table");
            } else {
                for (const char* k : {"at", "work", "relax", "sleep"})
                    if (n.routine.contains(k)) check_routine_point(c, n.uid, subj, n.routine[k], k);
                if (n.routine.contains("patrol")) {
                    const json& pl = n.routine["patrol"];
                    if (!pl.is_object() || !pl.contains("points") || !pl["points"].is_array() || pl["points"].size() < 2)
                        c.add(Severity::Error, A, n.uid, subj, "a patrol needs at least two waypoints");
                    else
                        for (auto& pt : pl["points"]) check_routine_point(c, n.uid, subj, pt, "patrol");
                }
            }
        }
    }
}

void check_quests(Ctx& c) {
    std::map<std::string, int> seen;
    for (auto& q : c.p.quests) {
        const char* A = "Quests";
        const std::string subj = q.name.empty() ? "(unnamed quest)" : q.name;
        if (q.name.empty()) c.add(Severity::Error, A, q.uid, subj, "the quest needs a name");
        for (unsigned char ch : q.name)
            if (!(std::isalnum(ch) && ch < 0x80) && ch != '_') {
                c.add(Severity::Error, A, q.uid, subj, "the quest name may only contain letters, digits and _");
                break;
            }
        if (++seen[q.name] == 2) c.add(Severity::Error, A, q.uid, subj, "the project has this quest twice");
        if (q.title.empty()) c.add(Severity::Warning, A, q.uid, subj, "no title: the quest log shows the name");
        const auto& types = quest_types();
        if (std::find(types.begin(), types.end(), q.type) == types.end()) c.add(Severity::Error, A, q.uid, subj, "unknown quest type '" + q.type + "'");
        if (q.xp && *q.xp < 0) c.add(Severity::Error, A, q.uid, subj, "the experience must not be negative");
        std::vector<DialogIssue> issues;
        const json flow = compile_quest(c.p, q, issues);
        for (auto& i : issues) c.add(i.error ? Severity::Error : Severity::Warning, A, q.uid, subj + (i.node.empty() ? "" : " (node)"), i.message);
        if (flow.is_object()) {
            const auto infos = project_info_names(c.p);
            for (auto& st : flow["steps"]) {
                if (st.value("objective", std::string()) == "talk" && std::find(infos.begin(), infos.end(), st.value("info", std::string())) == infos.end())
                    c.add(Severity::Warning, A, q.uid, subj, "the talk objective names '" + st.value("info", std::string()) + "', no dialogue option of the project");
                if (st.contains("item") && st["item"].is_string() && !item_known(c, st["item"].get<std::string>()))
                    c.add(Severity::Error, A, q.uid, subj, "the objective names '" + st["item"].get<std::string>() + "', no item of the game or the project");
                if (st.contains("items"))
                    for (auto& it : st["items"])
                        if (!item_known(c, it[0].get<std::string>())) c.add(Severity::Error, A, q.uid, subj, "the reward names '" + it[0].get<std::string>() + "', no item of the game or the project");
            }
            const json& t = flow["trigger"];
            if (t.value("kind", std::string()) == "item" && !item_known(c, t.value("item", std::string())))
                c.add(Severity::Error, A, q.uid, subj, "the trigger names '" + t.value("item", std::string()) + "', no item of the game or the project");
        }
    }
}

void check_dialogs(Ctx& c) {
    std::map<std::string, std::string> infos;  // info name -> dialogue
    for (auto& d : c.p.dialogs) {
        const char* A = "Dialogues";
        const std::string subj = d.name.empty() ? "(unnamed dialogue)" : d.name;
        // sounds: a game sound must exist, a language must be one of the mod's
        for (auto& n : d.graph.nodes) {
            const auto s = n.data.find("sound");
            if (s == n.data.end() || !s->is_object()) continue;
            for (auto& [lang, v] : s->items()) {
                if (!v.is_string() || v.get<std::string>().empty()) continue;
                const std::string ref = v.get<std::string>();
                if (is_compiled_sound(ref) && !find_asset(c.p, ref) && c.g && !c.g->exists(ref))
                    c.add(Severity::Error, A, d.uid, subj + " (node)", "the game has no sound " + ref);
                if (lang != "default" && std::find(c.p.mod.languages.begin(), c.p.mod.languages.end(), lang) == c.p.mod.languages.end())
                    c.add(Severity::Warning, A, d.uid, subj + " (node)", "a sound for " + lang + ", which is not a language of the mod");
            }
        }
        std::vector<DialogIssue> issues;
        const json list = compile_dialog(c.p, d, issues);
        for (auto& i : issues) c.add(i.error ? Severity::Error : Severity::Warning, A, d.uid, subj + (i.node.empty() ? "" : " (node)"), i.message);
        if (!d.owner.empty() && !c.p.npc(d.owner)) {
            const WorldEntity* we = c.w ? c.w->by_name(d.owner) : nullptr;
            if (!c.w) c.add(Severity::Warning, A, d.uid, subj, "the owner '" + d.owner + "' is not a placed NPC of the project: it must be the unique name of an NPC in the world");
            else if (!we || !we->npc) c.add(Severity::Error, A, d.uid, subj, "no NPC called '" + d.owner + "' in the project or in the world " + c.w->name);
        }
        bool any_option = false;
        for (auto& n : d.graph.nodes) any_option |= n.type == "option";
        if (!any_option) c.add(Severity::Warning, A, d.uid, subj, "the dialogue has no option");
        for (auto& info : list) {
            const std::string name = info["name"].get<std::string>();
            if (const std::string why = npc_name_problem(name); !why.empty()) c.add(Severity::Error, A, d.uid, subj, "option name '" + name + "': " + why);
            auto [it, fresh] = infos.emplace(name, subj);
            if (!fresh) c.add(Severity::Error, A, d.uid, subj, "the option name '" + name + "' is used twice (also in " + it->second + ")");
            // quest conditions must name a quest that exists when the option is made (else the engine shows a blocking message box)
            if (info.contains("when")) check_condition(c, A, d.uid, subj, info["when"]);
            for (auto& l : info["lines"]) {
                if (l.contains("command") && l["command"] == "Give" && l.contains("Item") && l["Item"].is_string() && !item_known(c, l["Item"].get<std::string>()))
                    c.add(Severity::Error, A, d.uid, subj, "Give names '" + l["Item"].get<std::string>() + "', no item of the game or the project");
            }
        }
    }
}

bool guid_text_ok(const std::string& s) {
    if (s.size() != 36) return false;
    for (size_t i = 0; i < s.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (s[i] != '-') return false;
        } else if (!std::isxdigit(static_cast<unsigned char>(s[i]))) {
            return false;
        }
    }
    return true;
}

void check_world(Ctx& c) {
    const char* A = "World";
    for (auto& s : c.p.world.spawns) {
        const std::string subj = s.template_name.empty() ? "(spawn)" : s.template_name;
        if (s.template_name.empty()) c.add(Severity::Error, A, s.uid, subj, "the spawn names no template");
        else if (!c.is_project_template(s.template_name) && c.g && !c.g->tpl(s.template_name))
            c.add(Severity::Error, A, s.uid, subj, "no template of the game or the project");
        if (s.amount < 1) c.add(Severity::Error, A, s.uid, subj, "the amount must be 1 or more");
        for (double x : s.rotation)
            if (!std::isfinite(x) || std::fabs(x) > 100000) c.add(Severity::Error, A, s.uid, subj, "the rotation is not a usable number");
        for (double x : s.position)
            if (!std::isfinite(x) || std::fabs(x) > 1e7) c.add(Severity::Error, A, s.uid, subj, "the position is not a usable number");
    }
    for (auto& ct : c.p.world.containers) {
        const std::string subj = ct.entity_name.empty() ? ct.guid : ct.entity_name;
        if (!guid_text_ok(ct.guid)) c.add(Severity::Error, A, ct.uid, subj, "the entity id is not of the form xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx");
        else if (c.w) {
            const WorldEntity* e = c.w->by_guid(ct.guid);
            if (!e) c.add(Severity::Error, A, ct.uid, subj, "no entity with this id in the world " + c.w->name);
            else if (!e->inventory) c.add(Severity::Warning, A, ct.uid, subj, "the entity has no inventory in the world file");
        }
        if (ct.items.empty()) c.add(Severity::Warning, A, ct.uid, subj, "no items to add");
        for (auto& i : ct.items)
            if (!item_known(c, i.item)) c.add(Severity::Error, A, ct.uid, subj, "'" + i.item + "' is no item of the game or the project");
    }
    // removals and moves of the world's entities
    std::map<std::string, std::string> seen;  // guid -> what
    auto check_ref = [&](const EntityRef& r, const char* what) {
        const std::string subj = r.entity_name.empty() ? r.guid : r.entity_name;
        if (!guid_text_ok(r.guid)) {
            c.add(Severity::Error, A, r.uid, subj, "the entity id is not of the form xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx");
            return;
        }
        if (auto [it, fresh] = seen.emplace(p2m::lower(r.guid), what); !fresh)
            c.add(Severity::Error, A, r.uid, subj, std::string("the entity is ") + what + " and " + it->second + " at once");
        if (c.w) {
            const WorldEntity* e = c.w->by_guid(r.guid);
            if (!e) c.add(Severity::Error, A, r.uid, subj, "no entity with this id in the world " + c.w->name);
            else if (e->npc) c.add(Severity::Warning, A, r.uid, subj, "an NPC of the world: his daily routine moves him again (and a removed NPC's quests break)");
        }
        for (const auto* v : {&r.position, &r.rotation})
            if (*v)
                for (double x : **v)
                    if (!std::isfinite(x) || std::fabs(x) > 1e7) c.add(Severity::Error, A, r.uid, subj, "the pose is not a usable number");
    };
    for (auto& r : c.p.world.removals) check_ref(r, "removed");
    for (auto& m : c.p.world.moves) {
        check_ref(m, "moved");
        if (!m.position && !m.rotation) c.add(Severity::Warning, A, m.uid, m.entity_name.empty() ? m.guid : m.entity_name, "a move without a new position or rotation");
    }
    for (auto& ct : c.p.world.containers)
        if (auto it = seen.find(p2m::lower(ct.guid)); it != seen.end() && it->second == std::string("removed"))
            c.add(Severity::Error, A, ct.uid, ct.entity_name.empty() ? ct.guid : ct.entity_name, "items are added to a container the mod removes");
}

// free string rows, template patches, edits of shipped dialogue infos and teaching limits
void check_extras(Ctx& c) {
    const std::string first = c.p.mod.languages.empty() ? std::string() : c.p.mod.languages.front();
    std::map<std::string, int> ids;
    for (auto& s : c.p.strings) {
        const char* A = "Strings";
        const std::string subj = s.id.empty() ? "(unnamed row)" : s.id;
        if (s.id.empty()) c.add(Severity::Error, A, s.uid, subj, "a string row needs an id");
        else if (s.id.find(';') != std::string::npos || s.id.find('"') != std::string::npos || s.id.find('\n') != std::string::npos)
            c.add(Severity::Error, A, s.uid, subj, "an id must not contain ; \" or a line break (strings/*.csv)");
        else if (++ids[p2m::lower(s.id)] == 2) c.add(Severity::Error, A, s.uid, subj, "the project has this string id twice (ids are not case sensitive)");
        if (s.text.empty()) c.add(Severity::Warning, A, s.uid, subj, "the row has no text");
        else if (!first.empty() && !s.text.count(first)) c.add(Severity::Warning, A, s.uid, subj, "no text in " + first + ", the fallback language: the other languages show their own texts only");
        for (auto& [lang, t] : s.text)
            if (std::find(c.p.mod.languages.begin(), c.p.mod.languages.end(), lang) == c.p.mod.languages.end())
                c.add(Severity::Warning, A, s.uid, subj, "a text in " + lang + ", which is no language of the mod (it is exported anyway)");
        // an object's own label wins over a free row of the same id
        for (auto& it : c.p.items) {
            const std::string low = p2m::lower(s.id);
            if (!it.label.empty() && low == p2m::lower("FO_" + it.name)) c.add(Severity::Warning, A, s.uid, subj, "the label of the item " + it.name + " has this id and wins");
            if (!it.description.empty() && low == p2m::lower("ITEMDESC_" + it.name)) c.add(Severity::Warning, A, s.uid, subj, "the description of the item " + it.name + " has this id and wins");
        }
    }
    std::map<std::string, int> seen;
    for (auto& x : c.p.patches) {
        const char* A = "Patches";
        const std::string subj = x.template_name.empty() ? "(no template)" : x.template_name;
        if (x.template_name.empty()) {
            c.add(Severity::Error, A, x.uid, subj, "choose the template to patch");
            continue;
        }
        if (++seen[p2m::lower(x.template_name)] == 2) c.add(Severity::Warning, A, x.uid, subj, "this template is patched twice: both patches are applied, the later one wins on a shared property");
        const TemplateInfo* t = c.g ? c.g->tpl(x.template_name) : nullptr;
        if (c.g && !t && !c.is_project_template(x.template_name)) c.add(Severity::Error, A, x.uid, subj, "no template of the game or the project has this name");
        if (x.properties.empty()) c.add(Severity::Warning, A, x.uid, subj, "the patch changes nothing");
        // a project's own new template has the sets of its base: not checked here
        check_properties(c, A, x.uid, subj, x.properties, t);
    }
    std::map<std::string, int> infos;
    for (auto& x : c.p.dialog_edits) {
        const char* A = "Shipped dialogues";
        const std::string subj = x.info.empty() ? "(no info)" : x.info;
        if (x.info.empty()) c.add(Severity::Error, A, x.uid, subj, "choose the shipped dialogue info to edit");
        else if (++infos[x.info] == 2) c.add(Severity::Warning, A, x.uid, subj, "this info is edited twice: both edits are applied");
        if (x.xp_add && x.xp_old) c.add(Severity::Error, A, x.uid, subj, "an XP reward is either added or changed, not both");
        if (x.xp_old.has_value() != x.xp_new.has_value()) c.add(Severity::Error, A, x.uid, subj, "a changed XP reward needs the old and the new amount");
        for (auto v : {x.xp_add, x.xp_new})
            if (v && *v < 0) c.add(Severity::Error, A, x.uid, subj, "an XP reward must not be negative");
        if (!x.xp_add && !x.xp_new && x.refills.empty() && x.ops.empty()) c.add(Severity::Warning, A, x.uid, subj, "the edit changes nothing");
        for (auto& op : x.ops) {
            static const char* kinds[] = {"info", "command", "condition", "add_condition"};
            if (std::find(std::begin(kinds), std::end(kinds), op.kind) == std::end(kinds)) c.add(Severity::Error, A, x.uid, subj, "property edit: unknown kind '" + op.kind + "'");
            if (op.name.empty()) c.add(Severity::Error, A, x.uid, subj, "a property edit needs the property's name");
            if ((op.kind == "command" || op.kind == "condition") && op.index < 1) c.add(Severity::Error, A, x.uid, subj, "a " + op.kind + " edit needs the place (1 = the first) of the " + op.kind);
            if (op.kind == "add_condition" && op.cls.empty()) c.add(Severity::Error, A, x.uid, subj, "an added condition needs its class (PlayerKnows, ItemAmount, ...)");
            if (op.kind != "add_condition" && op.old_value.empty()) c.add(Severity::Warning, A, x.uid, subj, "the edit of " + op.name + " has no old value: it is applied only where the value is empty");
        }
        for (auto& r : x.refills) {
            if (r.trader.empty()) c.add(Severity::Error, A, x.uid, subj, "a refill needs the trader's name");
            if (r.item.empty() || !item_known(c, r.item)) c.add(Severity::Error, A, x.uid, subj, "refill item '" + r.item + "' is no item of the game or the project");
            if (r.count < 0) c.add(Severity::Error, A, x.uid, subj, "a refill count must not be negative");
        }
    }
    for (auto& t : c.p.teachers) {
        const char* A = "Shipped dialogues";
        const std::string subj = !t.entity_name.empty() ? t.entity_name : t.guid;
        if (t.guid.size() != 36) c.add(Severity::Error, A, t.uid, subj, "the NPC's entity ID is not xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx");
        if (!c.skills.empty() && !c.skills.count(t.skill)) c.add(Severity::Error, A, t.uid, subj, "unknown skill '" + t.skill + "'");
        if (t.max < 0) c.add(Severity::Error, A, t.uid, subj, "the limit must not be negative");
        if (c.w && !t.guid.empty() && !c.w->by_guid(t.guid)) c.add(Severity::Warning, A, t.uid, subj, "no entity with this ID in the loaded world");
    }
}

void check_assets(Ctx& c) {
    const char* A = "Assets";
    std::map<std::string, int> names;
    for (auto& a : c.p.assets) {
        const std::string name = p2m::bare_name(a.file);
        if (++names[p2m::lower(name)] == 2)
            c.add(Severity::Error, A, a.uid, name, "two asset files are called " + name + " (the export puts them into one folder)");
        const std::string kind = asset_kind(name);
        if (kind == "other" && !is_loose_data_file(name))
            c.add(Severity::Warning, A, a.uid, name, "not a resource the engine loads (._xmsh, ._xmat, ._ximg, ._xmac, ._xmot, ._xsnd) and no audio file (.ogg, .wav, .mp3)");
        else if (!is_raw_audio(name) && !is_picture(name) && kind != "iconlist" && c.g && c.g->exists(name))
            c.add(Severity::Warning, A, a.uid, name, "replaces the game's file of the same name everywhere it is used");
    }
    // pictures: each becomes <stem>._ximg (or an icon); that name must not be taken twice
    std::map<std::string, std::string> made;  // generated lower-case name -> source
    const auto icons = icon_pictures(c.p);
    for (auto& a : c.p.assets) {
        const std::string name = p2m::bare_name(a.file);
        if (!is_picture(name)) continue;
        if (std::find(icons.begin(), icons.end(), p2m::lower(name)) != icons.end()) continue;
        const std::string out = picture_compiled_name(name);
        if (names.count(p2m::lower(out)))
            c.add(Severity::Error, A, a.uid, name, "becomes " + out + ", which is also an asset file of the project");
        if (auto [it, fresh] = made.emplace(p2m::lower(out), name); !fresh)
            c.add(Severity::Error, A, a.uid, name, "becomes " + out + " like " + it->second);
        else if (c.g && c.g->exists(out))
            c.add(Severity::Warning, A, a.uid, name, "becomes " + out + " and replaces the game's texture of that name everywhere it is used");
    }
    if (!icons.empty())
        for (const std::string gen : {c.p.mod.id + "_Icons.ximl", c.p.mod.id + "_IconAtlas._ximg"})
            if (names.count(p2m::lower(gen))) c.add(Severity::Error, A, "", gen, "the export makes this file for the icon pictures: rename the project's file");
}

}  // namespace

std::vector<Problem> validate(const Project& p, const GameData* game, const World* world) {
    std::vector<Problem> out;
    Ctx c{p, game, world, out, {}, {}, {}, {}};
    for (auto& s : skill_names(game)) c.skills.insert(s);
    for (auto& i : p.items)
        if (i.mode == Mode::New) c.new_templates.insert(p2m::lower(i.name));
    for (auto& r : p.recipes)
        if (r.mode == Mode::New) c.new_templates.insert(p2m::lower(r.name));
    for (auto& n : p.npc_templates)
        if (n.mode == Mode::New) c.new_templates.insert(p2m::lower(n.name));
    for (auto& s : p.spells)  // the item templates a new spell makes
        if (s.mode == Mode::New) {
            if (s.forms.empty()) c.new_templates.insert(p2m::lower(s.name));
            for (auto& f : s.forms) c.new_templates.insert(p2m::lower(new_spell_template(s.name, f)));
        }
    for (auto& a : p.assets) {
        c.asset_files.insert(p2m::lower(p2m::bare_name(a.file)));
        c.own_icon_lists |= asset_kind(a.file) == "iconlist";
    }
    for (auto& n : generated_asset_names(p)) c.asset_files.insert(n);
    if (game)
        for (auto& i : known_icons(*game)) c.icons.insert(p2m::lower(i));

    if (const std::string why = mod_id_problem(p.mod.id); !why.empty()) c.add(Severity::Error, "Mod", "", p.mod.id, why);
    if (p.mod.name.empty()) c.add(Severity::Warning, "Mod", "", p.mod.id, "the mod has no name");
    if (p.mod.languages.empty()) c.add(Severity::Error, "Mod", "", p.mod.id, "the mod needs at least one language");
    check_items(c);
    check_spells(c);
    check_npc_groups(c);
    check_recipes(c);
    check_npc_templates(c);
    check_placed_npcs(c);
    check_quests(c);
    check_events(c);
    check_layers(c);
    check_behaviors(c);
    check_dialogs(c);
    check_world(c);
    check_extras(c);
    check_assets(c);
    return out;
}

}  // namespace mk
