// The project model of risen-modkit (docs/editor/REQUIREMENTS.md section 6): everything a mod consists of, as plain data. Panels edit it, validation and
// export read it, the project file is its JSON form. Objects refer to each other by name (template names, NPC names, quest names), exactly as the exported
// Lua does; `uid` identifies an object across renames (selection, graph links).
#pragma once

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace mk {

using json = nlohmann::ordered_json;
using Text = std::map<std::string, std::string>;  // language name ("English", "German") -> text
using IntMap = std::map<std::string, int>;        // skill / item name -> number
using PropEdits = std::map<std::string, std::map<std::string, json>>;  // property set -> property -> value (number, string, bool)
using Vec3 = std::array<double, 3>;

constexpr int kProjectFormatVersion = 1;

std::string new_uid();  // 32 random hex digits

enum class Mode { New, Edit };  // a template the mod creates / a change of a shipped one

struct ModInfo {
    std::string id = "my_mod";
    std::string name = "My Mod";
    std::string version = "1.0.0";
    std::string author;
    std::string description;
    std::vector<std::string> languages = {"English", "German"};  // the first is the fallback for every other language
    std::vector<std::string> load_after, requires_mods;
};

struct ItemDef {
    std::string uid;
    Mode mode = Mode::New;
    std::string name;  // New: the new template name; Edit: the shipped template
    std::string base;  // New only: the template to clone
    Text label, description;
    std::optional<int> value, damage, mana_cost, material;
    std::optional<IntMap> requirements, modifies;  // present = replaces the template's list (an empty map clears it)
    std::optional<std::string> use_type, category, mesh, icon;
    PropEdits properties;
    std::string notes;
};

struct ItemGroup {
    std::string uid;
    std::string group, match, exclude;
    std::vector<std::string> names;
    std::optional<double> value_mult, damage_mult;
};

struct Ingredient {
    std::string item;
    int count = 1;
};

struct RecipeDef {
    std::string uid;
    Mode mode = Mode::New;
    std::string name;  // New: the recipe item to create; Edit: the shipped recipe item
    std::string base;  // New only, optional: the recipe item to clone (default It_Recipe_Health_01)
    std::string station;
    std::optional<std::vector<Ingredient>> ingredients;
    std::optional<IntMap> skills;
    std::optional<std::string> result;
    Text label, description;
    std::optional<std::string> icon, mesh;
    std::optional<int> value;
    bool learn = false;
    PropEdits properties;
    std::string notes;
};

struct NpcTemplateDef {
    std::string uid;
    Mode mode = Mode::New;
    std::string name, base;
    Text label;
    std::optional<std::string> mesh;  // the actor of a monster (.xac)
    std::optional<int> hp_max, hp, mana_max, mana, level, level_add, xp, damage;
    std::optional<double> hp_mult, mana_mult, level_mult, damage_mult;
    std::optional<IntMap> skills;
    std::optional<std::string> damage_type, guild, species, attitude, bravery, damage_calc, combat_species, ai_group;
    std::optional<IntMap> loot;  // target amounts the template carries
    PropEdits properties;
    std::string notes;
};

struct InvItem {
    std::string item;
    int amount = 1;
    bool equip = false;
};

struct PlacedNpc {
    std::string uid;
    std::string name;      // the unique entity name (FO_<name> is its label)
    std::string template_name;
    Vec3 position{0, 0, 0};
    std::optional<double> yaw;
    Text label;
    bool peaceful = true;
    std::optional<std::string> guild;
    std::optional<int> level, hp_max;
    std::vector<InvItem> items;
    json routine;            // RoutineSpec as the layer takes it (null = none)
    bool respawn = false;
    int version = 1;
    json appear_when, leave_when;  // conditions (condition data of section 7.2; null = always / never)
    std::string notes;
};

// Graphs of the node editors (dialogues, quests). Node data is free-form per node type; pins are named per type.
struct GraphNode {
    std::string uid;
    std::string type;
    double x = 0, y = 0;
    json data = json::object();
};
struct GraphLink {
    std::string uid;
    std::string from_node, from_pin, to_node, to_pin;
};
struct Graph {
    std::vector<GraphNode> nodes;
    std::vector<GraphLink> links;
    GraphNode* find(const std::string& uid);
    const GraphNode* find(const std::string& uid) const;
};

struct DialogDef {
    std::string uid;
    std::string name;   // the graph's name in the editor (also the prefix of its info names)
    std::string owner;  // the NPC's unique entity name
    Graph graph;
    std::string notes;
};

struct QuestDef {
    std::string uid;
    std::string name;  // the quest id
    Text title;
    std::string type = "Report";
    std::optional<int> xp;
    bool silent = false;
    Graph graph;
    std::string notes;
};

struct WorldSpawn {
    std::string uid;
    std::string template_name;
    Vec3 position{0, 0, 0};
    Vec3 rotation{0, 0, 0};  // yaw, pitch, roll in degrees
    int amount = 1;          // items
    std::string notes;
};
struct ContainerEdit {
    std::string uid;
    std::string guid;         // the world entity's id (risen.entity.by_id form)
    std::string entity_name;  // for display
    std::string layer;        // the layer file it came from
    std::vector<InvItem> items;
};
struct EntityRef {
    std::string uid;
    std::string guid, entity_name, layer;
    std::optional<Vec3> position, rotation;  // moves only
};
struct WorldEdits {
    std::string world = "World";
    std::vector<WorldSpawn> spawns;
    std::vector<ContainerEdit> containers;
    std::vector<EntityRef> removals, moves;
};

struct AssetFile {
    std::string uid;
    std::string file;  // path relative to the project file
    std::string kind;  // mesh, material, image, actor, sound, other
};

struct Project {
    ModInfo mod;
    std::vector<ItemDef> items;
    std::vector<ItemGroup> item_groups;
    std::vector<RecipeDef> recipes;
    std::vector<NpcTemplateDef> npc_templates;
    std::vector<PlacedNpc> npcs;
    std::vector<DialogDef> dialogs;
    std::vector<QuestDef> quests;
    WorldEdits world;
    std::vector<AssetFile> assets;
    json editor = json::object();  // editor-only state (notes, view settings); exported with project.json

    // lookups by name (nullptr when absent)
    ItemDef* item(const std::string& name);
    NpcTemplateDef* npc_template(const std::string& name);
    PlacedNpc* npc(const std::string& name);
    QuestDef* quest(const std::string& name);
};

json to_json(const Project& p);
// tolerant: missing fields keep their defaults; problems (wrong types, unknown format) are appended to `warnings`. Throws on a file that is no project.
Project project_from_json(const json& j, std::vector<std::string>* warnings = nullptr);

std::string dump_project(const Project& p);  // the project file's text (2-space indented, trailing newline)
Project load_project_file(const std::string& path, std::vector<std::string>* warnings = nullptr);  // throws
void save_project_file(const Project& p, const std::string& path);                                // throws

const char* mode_name(Mode m);

}  // namespace mk
