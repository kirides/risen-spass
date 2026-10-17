// Views of game templates in the terms of the layer's balancing API (BalanceItem, RecipeInfo, BalanceNpc): what the editors show as "shipped value"
// next to a field, and the choices of the pickers.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/gamedata.h"
#include "core/project.h"

namespace mk {

struct ItemStats {
    std::optional<int> value, damage, mana_cost, material;
    std::string use_type, category;  // "gEItemUseType_1H", "gEItemCategory_Weapon" ("" when absent or unknown)
    IntMap requirements, modifies;   // skill -> amount
    std::string mesh, icon;
};
ItemStats item_stats(const GameData& g, const TemplateInfo& t);

struct RecipeStats {
    std::string station;  // anvil, forge, ... ("" when unknown)
    std::vector<Ingredient> ingredients;
    IntMap skills;
    std::string result;
};
std::optional<RecipeStats> recipe_stats(const GameData& g, const TemplateInfo& t);

// the layer's station names and the engine's gECraft numbers (src/game/lua_recipe.cpp)
const std::vector<std::string>& recipe_stations();

// names usable in the pickers
std::vector<std::string> skill_names(const GameData* g);           // Strength, Dexterity, ..., Smith, ...
std::vector<std::string> enum_names(const GameData* g, const std::string& enum_type);  // "gEItemUseType" -> {"gEItemUseType_1H", ...}
std::vector<std::string> known_icons(const GameData& g);           // every gCItem_PS.IconImage of the shipped templates

// "X.xmsh" -> "X._xmsh" (the compiled name the archives hold); other names are returned unchanged
std::string compiled_name(const std::string& raw);

}  // namespace mk
