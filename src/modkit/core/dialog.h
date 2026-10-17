// Dialogue graphs -> the infos of risen.dialog.create (docs/editor/REQUIREMENTS.md section 7.3) and the engine's dialogue commands.
#pragma once

#include <string>
#include <vector>

#include "core/graph.h"
#include "core/project.h"

namespace mk {

struct CommandParam {
    std::string name, type;  // engine type: bCString, long, bool, gCInfoLocString, eCTemplateEntityProxy, bTPropertyContainer<enum ...>
};
struct CommandDef {
    std::string name;
    std::vector<CommandParam> params;
};
const std::vector<CommandDef>& dialog_commands();  // src/modkit/core/dialog_commands.inc (from docs/PROPERTIES.md)
const CommandDef* dialog_command(const std::string& name);

struct DialogIssue {
    bool error = true;
    std::string node;  // the node the problem is about ("" = the whole dialogue)
    std::string message;
};

// the infos of one dialogue graph in creation order (parents before children), as the JSON of data/dialogs.lua entries;
// `actions` collects the quest actions the lines call ({ script, quest, action })
json compile_dialog(const Project& p, const DialogDef& d, std::vector<DialogIssue>& issues, json* actions = nullptr);
// every dialogue of the project
json dialogs_data(const Project& p, json* actions = nullptr);

// the info name of an option node: its "info" field, else <mod id>_<first 8 hex of the node uid>
std::string info_name(const Project& p, const GraphNode& option);
// the script a quest action of the dialogues calls: MK_<quest>_<action> (risen.script.define in the runtime)
std::string quest_action_script(const std::string& quest, const std::string& action);
// a text per language for a dialogue line: { default = <first language>, de = ..., ... }
json line_text(const Project& p, const json& text);

}  // namespace mk
