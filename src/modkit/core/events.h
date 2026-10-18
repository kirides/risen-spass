// Event graphs (docs/editor/REQUIREMENTS.md section 3.11 / 7.5): a trigger (something happens in the game) walks along its links through
// conditions, flow nodes and actions. The node kinds are table-driven: one field list per kind serves the defaults of a new node, the
// inspector, the validation and the summary on the canvas. The graph compiles to the plain data of data/events.lua that modkit/events.lua runs.
#pragma once

#include <string>
#include <vector>

#include "core/dialog.h"
#include "core/graph.h"
#include "core/project.h"

namespace mk {

enum class EvField {
    Int,
    Number,
    String,
    Bool,
    Combo,   // one of `options`
    Text,    // text per language ({ English = ..., German = ... })
    Item,    // an item template of the game or the project
    Quest,   // a quest of the project or the game
    Target,  // an NPC or monster template, or the unique name of one NPC
    Template,  // an NPC or monster template (what spawn_npc makes)
    Info,    // a dialogue option name (of the project's dialogues, or free)
};

struct EvFieldDef {
    const char* key = "";
    const char* label = "";
    EvField kind = EvField::String;
    json def = nullptr;                  // the value of a new node (null = leave the key out)
    std::vector<std::string> options;    // Combo
    bool required = false;               // must be filled in (a Text: in some language; an Int / Number is always set)
    const char* when = nullptr;          // shown only while another field has one of these values: "key=a|b" ("key!=a" for the opposite)
    const char* help = nullptr;
    double min = -1e9, max = 1e9;        // Int / Number
};

struct EvNodeSpec {
    const char* type = "";
    const char* group = "";  // Triggers, Conditions, Flow, Actions, Notes
    const char* title = "";
    const char* help = "";
    unsigned color = 0xff404040;
    bool trigger = false;                // a start node: no input
    std::vector<const char*> outs;       // output pins (flow); the first one is called "next" for most kinds
    std::vector<EvFieldDef> fields;
};

const std::vector<EvNodeSpec>& event_specs();
const EvNodeSpec* event_spec(const std::string& type);

// the checks of a Condition node ({ kind, ... } entries of its `tests` list)
struct EvTestSpec {
    const char* kind;
    const char* title;
    std::vector<EvFieldDef> fields;
};
const std::vector<EvTestSpec>& event_tests();
const EvTestSpec* event_test(const std::string& kind);

// the data of a new node / a new check
json event_default_data(const std::string& type);
json event_default_test(const std::string& kind);

// whether a field is shown / used given the node's other values (EvFieldDef::when)
bool event_field_visible(const EvFieldDef& f, const json& data);

// the pin list a node type has on the canvas (core/graph.cpp builds its NodeTypeDef from it)
const std::vector<std::string>& event_groups();

// one line (or a few) of what a node does, for the canvas
std::string event_summary(const Project& p, const GraphNode& n);

// the whole graph as the data of one data/events.lua entry: { name, key, enabled, nodes = [ { id, type, x, y, <fields>, outs = { pin = [ids] } } ] }.
// Problems of the graph (a DialogIssue's node is the node uid, "" for the whole graph) are appended to `issues`.
json compile_events(const Project& p, const EventGraphDef& g, std::vector<DialogIssue>& issues);
// every graph of the project (an array)
json events_data(const Project& p);
// the id a node has in the data (first 10 hex digits of its uid), and the key of a graph
std::string event_node_id(const GraphNode& n);
std::string event_graph_key(const EventGraphDef& g);

// what a graph names outside itself, for the validation: kind is item, quest, skill, npc (a template or a unique name), template (an NPC template)
struct EventRef {
    std::string node, kind, name;
};
std::vector<EventRef> event_refs(const EventGraphDef& g);

// the graph rebuilt from one data entry (import from the data files): nodes with their positions and data, links from `outs`
EventGraphDef event_graph_from_data(const json& entry);

// the names of the signals the project's graphs send / wait for
std::vector<std::string> event_signal_names(const Project& p);

// the movement filter of the runtime, as a C++ twin used by the tests next to the Lua one: true when the position is a sample (the player moved at
// least `min_units` from the anchor); the first call only sets the anchor
struct MovementState {
    bool has_anchor = false;
    double ax = 0, ay = 0, az = 0;
};
bool movement_step(MovementState& s, double x, double y, double z, double min_units = 100.0);

}  // namespace mk
