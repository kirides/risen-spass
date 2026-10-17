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

// the names of the project's new items and recipe items (for pickers)
std::vector<std::string> project_item_names(const Project& p);

}  // namespace mk::ui
