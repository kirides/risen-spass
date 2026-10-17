// Wizards: common jobs as a form that generates ordinary project objects (a quest with its dialogue, an item with its recipe, a trader with his
// stock), which the author can then edit like anything else. Pure functions on the project: the panels' forms live in app/wizards.cpp.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "core/project.h"

namespace mk {

struct WizardResult {
    std::string error;                                   // not empty: nothing was changed
    std::vector<std::pair<std::string, std::string>> created;  // (panel title, uid) of what was made or extended, first = what to show
    std::vector<std::string> notes;                      // things the author should still do or know
    bool ok() const { return error.empty(); }
};

// ---- fetch quest: an NPC asks for items, the player brings them ----
struct FetchQuestParams {
    std::string npc;          // the unique name of the NPC who gives the job (a placed NPC of the project, or an NPC of the game world)
    std::string quest;        // the quest id ("Q_<Mod>_Ore")
    Text title;               // the quest log's title
    std::string item;         // what has to be brought
    int count = 1;
    bool take_items = true;   // the NPC takes them (Give player -> NPC)
    int xp = 100, gold = 0;
    std::vector<std::pair<std::string, int>> reward_items;
    Text ask_option, ask_line;      // "Do you have work for me?" / "Bring me two lumps of iron ore."
    Text log_text;                  // the quest log entry while it runs
    Text done_option, done_line;    // "Here is the ore." / "Thank you!"
    Text success_log;               // the log entry at the end (optional)
};
// Adds the quest (Start -> Objective items -> Objective talk -> Reward -> Outcome) and two options to the NPC's dialogue: the first offers the job (once), the
// second turns the items in while the quest runs and the player carries them. Creates the dialogue when the NPC has none in the project.
WizardResult wizard_fetch_quest(Project& p, const FetchQuestParams& w);

// ---- a new weapon and the recipe that makes it ----
struct WeaponParams {
    std::string name;         // the new item's template name
    std::string base;         // a shipped item to clone ("It_1H_Sword")
    Text label, description;
    std::optional<int> damage, value;
    IntMap requirements;
    bool recipe = true;
    std::string station = "anvil";
    std::vector<Ingredient> ingredients;
    IntMap skills;            // what the player needs to craft it ({ Smith = 1 })
    bool learn = true;        // the player knows the recipe from the start
};
WizardResult wizard_weapon(Project& p, const WeaponParams& w);

// ---- a trader with stock ----
struct TraderParams {
    std::string npc;               // unique name of the new placed NPC
    std::string template_name;     // NPC template he is made of (a shipped one, or a template of the project)
    Text label;
    std::vector<InvItem> stock;    // what he sells
    bool near_player = true;       // made 150 units in front of the player when the game first makes him (an offset); else at `position`
    Vec3 position{0, 0, 0};
    Text option_text, line_text;   // "Let's trade." / "Take a look."
};
// A placed NPC with the stock and a dialogue option running the engine's DoTrade command. The DoTrade command was not tried in a real trade yet
// (docs/MODDING_GUIDE.md): the result says so.
WizardResult wizard_trader(Project& p, const TraderParams& w);

// suggestions the forms start from (unique among the project's names)
std::string suggest_quest_id(const Project& p, const std::string& what);
std::string suggest_info_name(const Project& p, const std::string& base);

}  // namespace mk
