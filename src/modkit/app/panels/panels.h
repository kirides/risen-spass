// Factories of the panels (one file each).
#pragma once

#include <memory>

#include "app/app.h"
#include "core/i18n.h"

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
std::unique_ptr<Panel> make_lua_panel();
std::unique_ptr<Panel> make_conversation_panel();
std::unique_ptr<Panel> make_spells_panel();
std::unique_ptr<Panel> make_npc_groups_panel();
std::unique_ptr<Panel> make_patches_panel();
std::unique_ptr<Panel> make_strings_panel();
std::unique_ptr<Panel> make_shipped_dialogs_panel();

// window titles, used for the default docking layout
inline constexpr const char* kPanelMod = N_("Mod");
inline constexpr const char* kPanelLog = N_("Log");
inline constexpr const char* kPanelSettings = N_("Settings");
inline constexpr const char* kPanelBrowser = N_("Browser");
inline constexpr const char* kPanelItems = N_("Items");
inline constexpr const char* kPanelRecipes = N_("Recipes");
inline constexpr const char* kPanelNpcs = N_("NPCs");
inline constexpr const char* kPanelDialogs = N_("Dialogues");
inline constexpr const char* kPanelQuests = N_("Quests");
inline constexpr const char* kPanelEvents = N_("Events");
inline constexpr const char* kPanelLayers = N_("Layers");
inline constexpr const char* kPanelWorld = N_("World");
inline constexpr const char* kPanelProblems = N_("Problems");
inline constexpr const char* kPanelAssets = N_("Assets");
inline constexpr const char* kPanelBehaviors = N_("Behaviours");
inline constexpr const char* kPanelLua = N_("Lua");
inline constexpr const char* kPanelConversation = N_("Conversation");
inline constexpr const char* kPanelSpells = N_("Spells");
inline constexpr const char* kPanelNpcGroups = N_("NPC groups");
inline constexpr const char* kPanelPatches = N_("Patches");
inline constexpr const char* kPanelStrings = N_("Strings");
inline constexpr const char* kPanelShippedDialogs = N_("Shipped dialogues");

}  // namespace mk
