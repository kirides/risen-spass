// Helpers shared by the content panels (items, recipes, NPCs): name suggestions, "set or keep shipped" widgets over skill lists and enums, the
// ingredient / inventory list editors.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "app/app.h"

namespace mk::ui {

// "Modkit_showcase": the mod id with a capital first letter, as a prefix for suggested template names
std::string name_prefix(const Project& p);
// `want`, or want_2, want_3 ... until no game template and no project object has the name
std::string unique_template_name(const Project& p, const GameData* g, std::string want);

// an optional enum value: "set" checkbox + combo of names (the shipped value is shown while unset)
bool opt_enum(const char* label, std::optional<std::string>& v, const std::vector<std::string>& names, const std::string& shipped);
// an optional combo over plain strings (stations, guilds, ...)
bool opt_choice(const char* label, std::optional<std::string>& v, const std::vector<std::string>& names, const std::string& shipped);
// "replace the list" checkbox + skill table (copied from the shipped list when ticked)
bool opt_skills(const char* label, std::optional<IntMap>& m, const IntMap& shipped, const std::vector<std::string>& skills, const char* help_text);

// a list of (item, count) rows with an item picker per row; `extra` are the project's item names
bool ingredient_list(const char* id, std::vector<Ingredient>& v, App& app, const std::vector<std::string>& extra, size_t max_rows = 16);
// a list of (item, amount, equip) rows
bool inventory_list(const char* id, std::vector<InvItem>& v, App& app, const std::vector<std::string>& extra, bool with_equip);

// one condition as the dialogue editor keeps it: { kind = "quest" | "item" | "var" | "event" | "skill" | "guild" | "dead" | "alive", args = { ... } }
bool condition_args(App& app, json& dd);
// a condition of the exported form (core/graph.h leaf_condition; null = none): a list of conditions that must all (or any) hold. `none_label` names the
// state without conditions ("from the start"). A nested condition (an import) is shown as text and can be replaced by a list.
bool condition_list(const char* id, json& cond, App& app, const char* none_label);

// the behaviours attached to an NPC or a template: per behaviour its parameters (fields made from the script's defaults; only values that differ
// are stored), remove; attach from a combo or by dropping a behaviour of the Behaviours panel on the section
bool behavior_uses(const char* id, std::vector<BehaviorUse>& uses, App& app);

// the world an NPC or a world edit belongs to: the game's worlds (and the current value); "" = the project's world, shown as "<name> (project)"
bool world_combo(const char* label, std::string& world, App& app);

// the kind of a template name: the game's kind; a new template of the project has the kind of its base (items Item, recipes Recipe, NPC templates
// Npc when the base is unknown); Other when nothing knows the name
TplKind kind_of(const App& app, const std::string& name);

// the names of the project's new items and recipe items (for pickers)
std::vector<std::string> project_item_names(const Project& p);

}  // namespace mk::ui
