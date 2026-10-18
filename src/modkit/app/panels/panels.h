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
std::unique_ptr<Panel> make_events_panel();
std::unique_ptr<Panel> make_layers_panel();
std::unique_ptr<Panel> make_world_panel();
std::unique_ptr<Panel> make_problems_panel();
std::unique_ptr<Panel> make_assets_panel();
std::unique_ptr<Panel> make_behaviors_panel();
std::unique_ptr<Panel> make_conversation_panel();
std::unique_ptr<Panel> make_spells_panel();
std::unique_ptr<Panel> make_npc_groups_panel();
std::unique_ptr<Panel> make_patches_panel();
std::unique_ptr<Panel> make_strings_panel();
std::unique_ptr<Panel> make_shipped_dialogs_panel();

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
inline constexpr const char* kPanelEvents = "Events";
inline constexpr const char* kPanelLayers = "Layers";
inline constexpr const char* kPanelWorld = "World";
inline constexpr const char* kPanelProblems = "Problems";
inline constexpr const char* kPanelAssets = "Assets";
inline constexpr const char* kPanelBehaviors = "Behaviours";
inline constexpr const char* kPanelConversation = "Conversation";
inline constexpr const char* kPanelSpells = "Spells";
inline constexpr const char* kPanelNpcGroups = "NPC groups";
inline constexpr const char* kPanelPatches = "Patches";
inline constexpr const char* kPanelStrings = "Strings";
inline constexpr const char* kPanelShippedDialogs = "Shipped dialogues";

}  // namespace mk
