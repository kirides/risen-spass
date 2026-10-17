// Quest graphs -> the flow of a quest in data/quests.lua (docs/editor/REQUIREMENTS.md section 7.4): how it starts, the steps the runtime walks
// (objectives wait for events, log entries and rewards happen at once) and how it ends.
#pragma once

#include <string>
#include <vector>

#include "core/dialog.h"
#include "core/project.h"

namespace mk {

// the kinds the editor offers (and the runtime understands)
const std::vector<std::string>& quest_triggers();    // dialog, game_start, item, kill, var, quest
const std::vector<std::string>& objective_kinds();   // items, kill, talk, var, quest
const std::vector<std::string>& outcome_results();   // success, fail, cancel

// { trigger = { kind, ... }, steps = { { kind = "objective" | "log" | "reward" | "outcome", ... }, ... } }, or null when the graph has no Start
json compile_quest(const Project& p, const QuestDef& q, std::vector<DialogIssue>& issues);

// every info name the dialogues of the project make (talk objectives pick from them)
std::vector<std::string> project_info_names(const Project& p);

}  // namespace mk
