// Node types of the graph editors (dialogues, quests): which pins a node has, what may be linked to what. Shared by the canvas (app/node_canvas)
// and the compilers (core/dialog, core/quest), so the rules live in one place.
#pragma once

#include <string>
#include <vector>

#include "core/project.h"

namespace mk {

// What travels along a link. Links only join pins of the same kind, from an output to an input.
enum class PinKind {
    Flow,    // the sequence of a dialogue option (line -> command -> ... -> end) or of a quest (start -> objective -> outcome)
    Choice,  // an option's sub-options (the engine's parent / child infos)
    Cond,    // a condition into an option's "when" (several links = all of them)
};

struct PinDef {
    const char* name;   // stored in GraphLink::from_pin / to_pin
    const char* label;  // shown next to the pin
    bool input;
    PinKind kind;
    bool multi;         // an input that takes several links / an output that may lead to several nodes
};

struct NodeTypeDef {
    const char* type;
    const char* title;
    const char* help;
    unsigned color;  // header colour, 0xAARRGGBB
    std::vector<PinDef> pins;
    const PinDef* pin(const std::string& name) const;
    const char* group = "";  // the create menu's submenu (event graphs)
};

enum class GraphKind { Dialog, Quest, Event };

const std::vector<NodeTypeDef>& node_types(GraphKind k);
const NodeTypeDef* node_type(GraphKind k, const std::string& type);

// links of a node's pin
std::vector<const GraphLink*> links_into(const Graph& g, const std::string& node, const std::string& pin);
std::vector<const GraphLink*> links_from(const Graph& g, const std::string& node, const std::string& pin);

// whether a link from (a, pin_a) to (b, pin_b) is allowed by the pin kinds and directions (`why` says why not)
bool link_allowed(GraphKind k, const Graph& g, const std::string& a, const std::string& pin_a, const std::string& b, const std::string& pin_b, std::string* why);
// adds the link; an output or input that takes one link loses its old one; returns the new link's uid ("" when refused)
std::string add_link(GraphKind k, Graph& g, const std::string& a, const std::string& pin_a, const std::string& b, const std::string& pin_b, std::string* why = nullptr);
void remove_node(Graph& g, const std::string& uid);  // with its links

// a new node with the type's default data
GraphNode make_node(GraphKind k, const std::string& type, double x, double y);

// Condition data (section 7.2 of REQUIREMENTS.md) of a condition sub-graph ending in the given node: ["quest", "Q", "Running"], ["all", [...]], ...
// Problems are appended to `errors`; null when the node is no condition.
json compile_condition(const Graph& g, const std::string& node_uid, std::vector<std::string>& errors, int depth = 0);
// one condition from its kind and named arguments ({ quest, status }, { item, count, holder }, ...: the dialogue editor's node data): ["quest", "Q", "Success"]
json leaf_condition(const std::string& kind, const json& args, std::vector<std::string>& errors);
// the other way: kind and named arguments of a one-condition array; false for all / any / not and unknown kinds
bool leaf_args(const json& c, std::string& kind, json& args);
// the condition on an input pin: several links are "all"; null when nothing is linked
json condition_on(const Graph& g, const std::string& node_uid, const std::string& pin, std::vector<std::string>& errors);
// a one-line text of condition data for the canvas ("quest Q_X is Running and has 3 It_IronOre")
std::string describe_condition(const json& c);

// the language codes of dialogue texts ({ default = ..., de = ... }); unknown languages keep their name
std::string language_code(const std::string& language);

}  // namespace mk
