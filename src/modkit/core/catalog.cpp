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

namespace {

// gESpecies by number. Learned from the shipped templates against the engine's names (docs/NPC_PROPERTIES.md); 4, 22 and 26 are not used by
// a shipped NPC and are inferred from the layer's name list (Ghost, Ogre_Tame, Wolf_Tame).
const char* species_of(int64_t n) {
    static const char* kNames[] = {"None",    "Human",         "Lizard", "Brontok",  "Ghost",       "Lurker",      "Ashbeast", "Nautilus", "Dragonfly",
                                   "Mantis",  "Scorpion",      "Skeleton", "Swampmummy", "Rotworm", "Skeleton_Tame", "Gnome", "Boar",     "Wolf",
                                   "Stingrat", "Vulture",      "Thundertail", "Ogre",  "Ogre_Tame",  "Cow",         "Pig",      "Chicken",  "Wolf_Tame"};
    return n >= 0 && n < static_cast<int64_t>(std::size(kNames)) ? kNames[n] : "";
}
const char* by_index(const std::vector<std::string>& names, int64_t n) { return n >= 0 && n < static_cast<int64_t>(names.size()) ? names[n].c_str() : ""; }

}  // namespace

NpcStats npc_stats(const GameData& g, const TemplateInfo& t) {
    NpcStats s;
    const auto& sk = t.skills;
    if (sk.size() == 34) {
        s.hp = sk[0];
        s.level = sk[2];
        s.hp_max = sk[5];
        s.mana_max = sk[6];
        s.xp = std::max(50, 10 * sk[2]);
        const auto names = skill_names(&g);
        for (size_t i = 7; i < 34 && i < g.ctx().skills.size(); ++i)
            if (sk[i] != 0 && !g.ctx().skills[i].empty()) s.skills[g.ctx().skills[i]] = sk[i];
    }
    auto num = [&](const char* key) -> std::optional<int64_t> {
        const p2m::Val* v = t.get(key);
        if (!v || !v->is_num()) return std::nullopt;
        return static_cast<int64_t>(v->num());
    };
    if (auto v = num("gCNPC_PS.Guild")) s.guild = by_index(guild_names(), *v);
    if (auto v = num("gCNPC_PS.Species")) s.species = species_of(*v);
    // a kill pays 10 * level, at least 50; the small species Chicken and Nautilus exactly 10 * level (the GiveXP script)
    if (s.level && (s.species == "Chicken" || s.species == "Nautilus")) s.xp = 10 * *s.level;
    if (auto v = num("gCNPC_PS.AttitudeLock")) s.attitude = by_index(attitude_names(), *v);
    if (auto v = num("gCNPC_PS.BraveryOverride")) s.bravery = by_index(bravery_names(), *v);
    if (auto v = num("gCNPC_PS.DamageCalculationType")) s.damage_calc = by_index(damage_calc_names(), *v);
    if (const p2m::Val* v = t.get("gCDamage_PS.DamageType")) {
        if (const std::string* ty = t.props.type_of("gCDamage_PS.DamageType")) {
            std::string e = g.enum_name(*ty, *v);
            if (e.rfind("gEDamageType_", 0) == 0) s.damage_type = e.substr(13);
        }
    }
    if (auto v = num("gCDamage_PS.DamageAmount")) s.damage = static_cast<int>(*v);
    if (const p2m::Val* v = t.get("gCCombatSystem_PS.CombatSpecies"); v && v->is_str()) s.combat_species = v->s;
    if (const p2m::Val* v = t.get("gCNPC_PS.Group"); v && v->is_str()) s.ai_group = v->s;
    if (const p2m::Val* v = t.get("eCAnimation_PS.ResourceFilePath"); v && v->is_str()) s.mesh = v->s;
    return s;
}

const std::vector<std::string>& guild_names() {
    static const std::vector<std::string> v = {"None", "Don", "Dig", "Grd", "Cit", "Inq", "Mag", "Pir"};
    return v;
}
const std::vector<std::string>& species_names() {
    static const std::vector<std::string> v = {"None", "Ashbeast", "Boar", "Brontok", "Chicken", "Cow", "Dragonfly", "Ghost", "Gnome", "Human",
                                               "Lizard", "Lurker", "Mantis", "Nautilus", "Ogre", "Ogre_Tame", "Pig", "Rotworm", "Scorpion", "Skeleton",
                                               "Skeleton_Tame", "Stingrat", "Swampmummy", "Thundertail", "Vulture", "Wolf", "Wolf_Tame"};
    return v;
}
const std::vector<std::string>& attitude_names() {
    static const std::vector<std::string> v = {"None", "Neutral", "Friendly", "Angry", "Hostile"};
    return v;
}
const std::vector<std::string>& bravery_names() {
    static const std::vector<std::string> v = {"None", "Brave", "Coward"};
    return v;
}
const std::vector<std::string>& damage_calc_names() {
    static const std::vector<std::string> v = {"Normal", "Monster", "Immortal"};
    return v;
}
const std::vector<std::string>& damage_type_names() {
    static const std::vector<std::string> v = {"None", "Blunt", "Edge", "Fire", "Ice", "Magic", "Physics", "Point"};
    return v;
}
std::vector<std::string> npc_skill_names(const GameData* g) {
    std::vector<std::string> v;
    for (auto& s : skill_names(g))
        if (s != "HitPoints" && s != "ManaPoints" && s != "Level" && s != "Experience" && s != "LearnPoints" && s != "MaxHitPoints" && s != "MaxManaPoints")
            v.push_back(s);
    return v;
}

const std::vector<std::string>& quest_types() {
    static const std::vector<std::string> v = {"Report", "Kill", "HasItems", "EnterArea", "BringNpc", "FollowNpc", "DriveAway", "Defeat", "Duel", "Arena",
                                               "Plunder", "Sparring"};
    return v;
}
const std::vector<std::string>& info_types() {
    static const std::vector<std::string> v = {"Info", "Important", "Comment", "News", "Refuse", "Parent"};
    return v;
}
const std::vector<std::string>& quest_states() {
    static const std::vector<std::string> v = {"Running", "Success", "Failed", "Cancelled", "Open", "Obsolete", "Won", "Lost"};
    return v;
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
