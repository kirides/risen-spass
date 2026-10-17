#include "core/catalog.h"

#include <algorithm>
#include <set>

namespace mk {

namespace {

std::optional<int> int_of(const p2m::Val* v) {
    if (!v || !v->is_num()) return std::nullopt;
    return static_cast<int>(v->num());
}

const char* station_of(int64_t craft) {
    switch (craft) {
        case 0: return "alchemy";
        case 3: return "frying";
        case 4: return "goldsmith";
        case 5: return "scroll";
        case 6: return "forge";
        case 7: return "anvil";
        case 8: return "coolweapon";
        case 9: return "grindstone";
    }
    return "";
}

}  // namespace

ItemStats item_stats(const GameData& g, const TemplateInfo& t) {
    ItemStats s;
    s.value = int_of(t.get("gCItem_PS.GoldValue"));
    s.damage = int_of(t.get("gCDamage_PS.DamageAmount"));
    s.mana_cost = int_of(t.get("gCCastInfo_PS.ManaCost"));
    s.material = int_of(t.get("eCMesh_PS.MaterialSwitch"));
    if (const p2m::Val* v = t.get("gCItem_PS.UseType"))
        if (const std::string* ty = t.props.type_of("gCItem_PS.UseType")) s.use_type = g.enum_name(*ty, *v);
    if (const p2m::Val* v = t.get("gCItem_PS.Category"))
        if (const std::string* ty = t.props.type_of("gCItem_PS.Category")) s.category = g.enum_name(*ty, *v);
    for (const p2m::Elem& e : p2m::arrays_of(t.props, "gCItem_PS.RequiredSkills[].")) {
        const std::string sk = g.ctx().skill(e.get("Skill"));
        if (!sk.empty()) s.requirements[sk] = int_of(e.get("Amount")).value_or(0);
    }
    for (const p2m::Elem& e : p2m::arrays_of(t.props, "gCItem_PS.ModifySkills[].")) {
        const std::string sk = g.ctx().skill(e.get("Skill"));
        if (!sk.empty()) s.modifies[sk] = int_of(e.get("Amount")).value_or(0);
    }
    s.mesh = t.mesh;
    s.icon = t.icon;
    return s;
}

std::optional<RecipeStats> recipe_stats(const GameData& g, const TemplateInfo& t) {
    const p2m::Val* craft = t.get("gCRecipe_PS.Craft");
    if (!craft) return std::nullopt;
    RecipeStats r;
    if (craft->is_int()) r.station = station_of(craft->i);
    for (const p2m::Elem& e : p2m::arrays_of(t.props, "gCRecipe_PS.CraftIngredients[].")) {
        const p2m::Val* it = e.get("ItemTemplate");
        if (it && it->is_str()) r.ingredients.push_back({it->s, int_of(e.get("ItemAmount")).value_or(1)});
    }
    for (const p2m::Elem& e : p2m::arrays_of(t.props, "gCRecipe_PS.RequiredSkills[].")) {
        const std::string sk = g.ctx().skill(e.get("Skill"));
        if (!sk.empty()) r.skills[sk] = int_of(e.get("Amount")).value_or(0);
    }
    if (const p2m::Val* res = t.get("gCRecipe_PS.ResultItem"); res && res->is_str()) r.result = res->s;
    return r;
}

const std::vector<std::string>& recipe_stations() {
    static const std::vector<std::string> v = {"alchemy", "anvil", "coolweapon", "cooking", "forge", "frying", "goldsmith", "grindstone", "scroll"};
    return v;
}

std::vector<std::string> skill_names(const GameData* g) {
    static const p2m::Ctx fallback;
    const p2m::Ctx& c = g ? g->ctx() : fallback;
    std::vector<std::string> v;
    for (auto& s : c.skills)
        if (!s.empty()) v.push_back(s);
    return v;
}

std::vector<std::string> enum_names(const GameData* g, const std::string& enum_type) {
    static const p2m::Ctx fallback;
    const p2m::Ctx& c = g ? g->ctx() : fallback;
    std::vector<std::string> v;
    if (auto it = c.enums.find(enum_type); it != c.enums.end())
        for (auto& [n, name] : it->second) v.push_back(enum_type + "_" + name);
    return v;
}

std::vector<std::string> known_icons(const GameData& g) {
    std::set<std::string> s;
    for (auto& t : g.templates())
        if (!t->icon.empty()) s.insert(t->icon);
    return {s.begin(), s.end()};
}

std::string compiled_name(const std::string& raw) {
    const auto dot = raw.rfind('.');
    if (dot == std::string::npos || dot + 1 >= raw.size() || raw[dot + 1] == '_') return raw;
    std::string ext = p2m::lower(raw.substr(dot + 1));
    if (ext == "tga" || ext == "dds" || ext == "png" || ext == "jpg") ext = "ximg";
    else if (ext == "xac") ext = "xmac";
    return raw.substr(0, dot) + "._" + ext;
}

}  // namespace mk
