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

struct NpcStats {
    std::optional<int> hp_max, hp, mana_max, level, xp, damage;
    IntMap skills;  // attributes, protection, combat and magic skills (non-zero values; HitPoints, Level, ... are the fields above)
    std::string guild, species, attitude, bravery, damage_calc, damage_type;  // the layer's short names ("Grd", "Wolf", ...)
    std::string combat_species, ai_group;
    std::string mesh;  // the actor (eCAnimation_PS.ResourceFilePath)
};
NpcStats npc_stats(const GameData& g, const TemplateInfo& t);

// the layer's names of the NPC enums (src/game/npc_balance_lua.h)
const std::vector<std::string>& guild_names();
const std::vector<std::string>& species_names();
const std::vector<std::string>& attitude_names();
const std::vector<std::string>& bravery_names();
const std::vector<std::string>& damage_calc_names();
const std::vector<std::string>& damage_type_names();
// the skills a balance spec may set on an NPC (`skills = {...}`): everything but the point pools and the level, which have their own settings
std::vector<std::string> npc_skill_names(const GameData* g);

// the quest types of risen.quest.define (QuestType of types/risen.d.lua)
const std::vector<std::string>& quest_types();
// the info types and conditions of risen.dialog.create (InfoType / InfoCondition)
const std::vector<std::string>& info_types();
// the quest states of risen.cond.quest
const std::vector<std::string>& quest_states();

// the layer's station names and the engine's gECraft numbers (src/game/lua_recipe.cpp)
const std::vector<std::string>& recipe_stations();

// names usable in the pickers
std::vector<std::string> skill_names(const GameData* g);           // Strength, Dexterity, ..., Smith, ...
std::vector<std::string> enum_names(const GameData* g, const std::string& enum_type);  // "gEItemUseType" -> {"gEItemUseType_1H", ...}
std::vector<std::string> known_icons(const GameData& g);           // every gCItem_PS.IconImage of the shipped templates

// "X.xmsh" -> "X._xmsh" (the compiled name the archives hold); other names are returned unchanged
std::string compiled_name(const std::string& raw);

}  // namespace mk
