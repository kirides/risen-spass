#include "core/validate.h"

#include <algorithm>
#include <map>
#include <set>

#include "common/template_id.h"
#include "core/catalog.h"

namespace mk {

std::string mod_id_problem(const std::string& id) {
    if (id.empty()) return "the mod id is empty";
    for (unsigned char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return "the mod id may only contain a-z, 0-9 and _";
    return {};
}

std::string template_name_problem(const std::string& name) { return spass::templates::nameProblem(name); }

size_t error_count(const std::vector<Problem>& v) {
    return static_cast<size_t>(std::count_if(v.begin(), v.end(), [](const Problem& p) { return p.severity == Severity::Error; }));
}

namespace {

struct Ctx {
    const Project& p;
    const GameData* g;
    std::vector<Problem>& out;
    std::set<std::string> skills;
    std::set<std::string> new_templates;  // lower-case names the project creates
    std::set<std::string> asset_files;    // lower-case bare names of the project's asset files
    std::set<std::string> icons;          // lower-case icon names of the shipped items

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
        check_enum(c, A, it.uid, subj, it.use_type, "gEItemUseType");
        check_enum(c, A, it.uid, subj, it.category, "gEItemCategory");
        if (it.mesh && !it.mesh->empty() && !c.asset_known(*it.mesh))
            c.add(Severity::Error, A, it.uid, subj, "the mesh '" + *it.mesh + "' is neither in the game archives nor among the project's asset files");
        if (it.icon && !it.icon->empty() && c.g) {
            if (!c.icons.count(p2m::lower(*it.icon))) c.add(Severity::Warning, A, it.uid, subj, "the icon '" + *it.icon + "' is not used by any shipped item (the layer refuses icons it cannot resolve)");
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

}  // namespace

std::vector<Problem> validate(const Project& p, const GameData* game) {
    std::vector<Problem> out;
    Ctx c{p, game, out, {}, {}, {}, {}};
    for (auto& s : skill_names(game)) c.skills.insert(s);
    for (auto& i : p.items)
        if (i.mode == Mode::New) c.new_templates.insert(p2m::lower(i.name));
    for (auto& r : p.recipes)
        if (r.mode == Mode::New) c.new_templates.insert(p2m::lower(r.name));
    for (auto& n : p.npc_templates)
        if (n.mode == Mode::New) c.new_templates.insert(p2m::lower(n.name));
    for (auto& a : p.assets) c.asset_files.insert(p2m::lower(p2m::bare_name(a.file)));
    if (game)
        for (auto& i : known_icons(*game)) c.icons.insert(p2m::lower(i));

    if (const std::string why = mod_id_problem(p.mod.id); !why.empty()) c.add(Severity::Error, "Mod", "", p.mod.id, why);
    if (p.mod.name.empty()) c.add(Severity::Warning, "Mod", "", p.mod.id, "the mod has no name");
    if (p.mod.languages.empty()) c.add(Severity::Error, "Mod", "", p.mod.id, "the mod needs at least one language");
    check_items(c);
    check_recipes(c);
    return out;
}

}  // namespace mk
