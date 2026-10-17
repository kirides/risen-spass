// Factories of the panels (one file each).
#pragma once

#include <memory>

#include "app/app.h"

namespace mk {

std::unique_ptr<Panel> make_project_panel();
std::unique_ptr<Panel> make_log_panel();
std::unique_ptr<Panel> make_settings_panel();
std::unique_ptr<Panel> make_browser_panel();
std::unique_ptr<Panel> make_items_panel();
std::unique_ptr<Panel> make_recipes_panel();
std::unique_ptr<Panel> make_npcs_panel();
std::unique_ptr<Panel> make_dialogs_panel();
std::unique_ptr<Panel> make_quests_panel();
std::unique_ptr<Panel> make_world_panel();
std::unique_ptr<Panel> make_problems_panel();

// window titles, used for the default docking layout
inline constexpr const char* kPanelMod = "Mod";
inline constexpr const char* kPanelLog = "Log";
inline constexpr const char* kPanelSettings = "Settings";
inline constexpr const char* kPanelBrowser = "Browser";
inline constexpr const char* kPanelItems = "Items";
inline constexpr const char* kPanelRecipes = "Recipes";
inline constexpr const char* kPanelNpcs = "NPCs";
inline constexpr const char* kPanelDialogs = "Dialogues";
inline constexpr const char* kPanelQuests = "Quests";
inline constexpr const char* kPanelWorld = "World";
inline constexpr const char* kPanelProblems = "Problems";

}  // namespace mk
