// A dialogue played through in the editor (DLG-5): the compiled infos of a dialogue (what the export writes, compile_dialog) are run against a
// pretend world (quest states, items, variables, ...) the author can change at any time. It follows the rules the layer documents for
// risen.dialog.create: options without a parent are listed while their `when` holds and they are not used up, the options below a parent appear after
// it was chosen, a sequence without End returns to the list, an Important option starts the conversation by itself.
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/project.h"

namespace mk {

// What the player has / what happened, as far as conditions can ask. Everything not listed is "no": no quest started, no item, no variable.
struct SimWorld {
    std::map<std::string, std::string> quests;  // quest -> Running / Success / Failed / Cancelled
    std::map<std::string, int> items;           // what the player carries
    std::map<std::string, double> vars;         // game variables (presence = exists)
    std::set<std::string> events;               // game events that happened
    std::map<std::string, int> skills;          // player skills
    std::set<std::string> dead;                 // NPCs (unique names) that are dead
    std::set<std::string> guilds;               // the player's guild
    // a condition leaf pinned to a result, whatever the world says: key = leaf_key(); lets the author switch a condition on or off
    std::map<std::string, bool> forced;
};

std::string leaf_key(const json& leaf);  // describe_condition(leaf)
// conditions are the data of core/graph.h ("all", "any", "not" and the leaves quest, item, var, event, skill, guild, dead, alive); null = true
bool eval_condition(const json& c, const SimWorld& w);
// the distinct leaves of a condition (for the switches), in order of appearance
void collect_leaves(const json& c, std::vector<json>& out);

struct SimLine {
    enum class Kind { Player, Npc, Other, Effect, End } kind = Kind::Effect;
    std::string who;   // speaker for Other
    std::string text;  // the spoken text, or what happened ("quest Q_X started", "command Give ...")
};

struct SimOption {
    std::string info;   // the info name
    std::string text;   // what the player says
    std::string type;   // Info, Important, ...
    bool permanent = false;
};

class DialogSim {
public:
    // `infos` and `actions`: compile_dialog() output of one dialogue and its quest actions; `language`: the language the texts are shown in
    // ("" = the first of the mod)
    DialogSim(const Project& p, json infos, json actions = json::array(), std::string language = {});

    SimWorld world;

    void restart();                       // a new conversation (the world and what was used up stay; use `reset_all` for a new game)
    void reset_all();                     // empty world, nothing used up
    bool choose(const std::string& info); // the player picks an option; false when it is not offered
    void leave();                         // the player walks away

    const std::vector<SimLine>& transcript() const { return transcript_; }
    // the options on offer now (empty after the conversation ended)
    std::vector<SimOption> options() const;
    bool ended() const { return ended_; }
    void keep_used(const std::set<std::string>& u) { used_ = u; }  // after a rebuild: what was used up before
    const std::set<std::string>& used() const { return used_; }
    // every distinct condition leaf of the dialogue: for the switches of the UI
    const std::vector<json>& leaves() const { return leaves_; }
    // options a conversation can never reach (a parent that is not an option of the dialogue)
    std::vector<std::string> unreachable() const;

private:
    const json* find(const std::string& info) const;
    bool available(const json& info) const;
    void run(const json& info);
    std::string text_of(const json& t) const;
    void auto_start();

    json infos_, actions_;
    std::string lang_code_;
    std::vector<json> leaves_;
    std::vector<SimLine> transcript_;
    std::set<std::string> used_;
    std::string current_;  // the option whose sub-options are on offer ("" = the top level)
    bool ended_ = false;
};

}  // namespace mk
