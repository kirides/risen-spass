#include "core/graph.h"

#include <algorithm>
#include <map>
#include <set>

namespace mk {

const PinDef* NodeTypeDef::pin(const std::string& name) const {
    for (auto& p : pins)
        if (name == p.name) return &p;
    return nullptr;
}

namespace {

// pins shared by the condition nodes of both graphs
const PinDef kCondOut{"out", "", false, PinKind::Cond, true};

std::vector<NodeTypeDef> dialog_types() {
    return {
        {"option", "Option", "A choice in the conversation: the player says the text, then the sequence on 'then' plays. 'choices' are the sub-options "
                             "shown after it (when the sequence does not end the conversation); 'when' takes conditions (several = all must hold).",
         0xff3a6ea5,
         {{"parent", "parent", true, PinKind::Choice, false},
          {"when", "when", true, PinKind::Cond, true},
          {"then", "then", false, PinKind::Flow, false},
          {"choices", "choices", false, PinKind::Choice, true}}},
        {"line", "Line", "A spoken line: the NPC, the player or another NPC (by unique name) says the text.", 0xff4a7a4a,
         {{"in", "", true, PinKind::Flow, true}, {"next", "", false, PinKind::Flow, false}}},
        {"command", "Command", "An engine dialogue command by name with its parameters (Give, GiveXP, SetGameEvent, DoTrade, ...).", 0xff8a6a2a,
         {{"in", "", true, PinKind::Flow, true}, {"next", "", false, PinKind::Flow, false}}},
        {"quest", "Quest", "Starts, succeeds, fails or cancels a quest of the project.", 0xff8a3a8a,
         {{"in", "", true, PinKind::Flow, true}, {"next", "", false, PinKind::Flow, false}}},
        {"log", "Log entry", "Adds a line to a quest's log.", 0xff6a4a8a, {{"in", "", true, PinKind::Flow, true}, {"next", "", false, PinKind::Flow, false}}},
        {"script", "Script", "Calls a script (risen.script.define in custom.lua, or a game script).", 0xff5a5a5a,
         {{"in", "", true, PinKind::Flow, true}, {"next", "", false, PinKind::Flow, false}}},
        {"end", "End", "Ends the conversation.", 0xffa53a3a, {{"in", "", true, PinKind::Flow, true}}},
        {"cond", "Condition", "A condition: quest state, items carried, a game variable or event, a skill, a guild, an NPC dead or alive.", 0xff2a7a7a,
         {kCondOut}},
        {"all", "All of", "Holds when every linked condition holds.", 0xff2a6a6a, {{"in", "", true, PinKind::Cond, true}, kCondOut}},
        {"any", "Any of", "Holds when one of the linked conditions holds.", 0xff2a6a6a, {{"in", "", true, PinKind::Cond, true}, kCondOut}},
        {"not", "Not", "Holds when the linked condition does not.", 0xff2a6a6a, {{"in", "", true, PinKind::Cond, false}, kCondOut}},
        {"comment", "Comment", "A note on the canvas; not exported.", 0xff404040, {}},
    };
}

std::vector<NodeTypeDef> quest_types() {
    return {
        {"start", "Start", "Where the quest begins: started by a dialogue's Quest node, or by a trigger (an item picked up, an NPC killed, a game "
                           "variable, the start of a game).",
         0xff3a8a3a,
         {{"next", "", false, PinKind::Flow, false}}},
        {"objective", "Objective", "What the player has to do: bring items, kill, talk to someone (a dialogue option finished), a game variable.",
         0xff3a6ea5,
         {{"in", "", true, PinKind::Flow, true}, {"next", "", false, PinKind::Flow, false}}},
        {"log", "Log entry", "Adds a line to the quest's log when the flow passes.", 0xff6a4a8a,
         {{"in", "", true, PinKind::Flow, true}, {"next", "", false, PinKind::Flow, false}}},
        {"reward", "Reward", "Gives experience, items or gold, sets a game variable.", 0xff8a6a2a,
         {{"in", "", true, PinKind::Flow, true}, {"next", "", false, PinKind::Flow, false}}},
        {"branch", "Either / or",
         "Several ways to go on. Link each way's first Objective to the output: the quest waits for all of them at once, the first one the player completes "
         "decides the path (the others are dropped) and the flow continues behind it. Paths do not join: each ends with its own Outcome.",
         0xff7a5a9a,
         {{"in", "", true, PinKind::Flow, true}, {"next", "paths", false, PinKind::Flow, true}}},
        {"outcome", "Outcome", "Ends the quest: success, failure or cancelled; may start a follow-up quest.", 0xffa53a3a,
         {{"in", "", true, PinKind::Flow, true}}},
        {"comment", "Comment", "A note on the canvas; not exported.", 0xff404040, {}},
    };
}

}  // namespace

const std::vector<NodeTypeDef>& node_types(GraphKind k) {
    static const std::vector<NodeTypeDef> d = dialog_types(), q = quest_types();
    return k == GraphKind::Dialog ? d : q;
}

const NodeTypeDef* node_type(GraphKind k, const std::string& type) {
    for (auto& t : node_types(k))
        if (type == t.type) return &t;
    return nullptr;
}

std::vector<const GraphLink*> links_into(const Graph& g, const std::string& node, const std::string& pin) {
    std::vector<const GraphLink*> v;
    for (auto& l : g.links)
        if (l.to_node == node && l.to_pin == pin) v.push_back(&l);
    return v;
}

std::vector<const GraphLink*> links_from(const Graph& g, const std::string& node, const std::string& pin) {
    std::vector<const GraphLink*> v;
    for (auto& l : g.links)
        if (l.from_node == node && l.from_pin == pin) v.push_back(&l);
    return v;
}

bool link_allowed(GraphKind k, const Graph& g, const std::string& a, const std::string& pin_a, const std::string& b, const std::string& pin_b, std::string* why) {
    auto fail = [&](const char* w) {
        if (why) *why = w;
        return false;
    };
    const GraphNode* na = g.find(a);
    const GraphNode* nb = g.find(b);
    if (!na || !nb) return fail("unknown node");
    if (a == b) return fail("a node cannot link to itself");
    const NodeTypeDef* ta = node_type(k, na->type);
    const NodeTypeDef* tb = node_type(k, nb->type);
    if (!ta || !tb) return fail("unknown node type");
    const PinDef* pa = ta->pin(pin_a);
    const PinDef* pb = tb->pin(pin_b);
    if (!pa || !pb) return fail("unknown pin");
    if (pa->input || !pb->input) return fail("links go from an output to an input");
    if (pa->kind != pb->kind) return fail("these pins do not fit together");
    for (auto& l : g.links)
        if (l.from_node == a && l.from_pin == pin_a && l.to_node == b && l.to_pin == pin_b) return fail("already linked");
    // the choice tree and the condition graph must stay acyclic: b must not reach a along the same kind of links
    if (pa->kind != PinKind::Flow) {
        std::set<std::string> seen;
        std::vector<std::string> todo{b};
        while (!todo.empty()) {
            const std::string n = todo.back();
            todo.pop_back();
            if (n == a) return fail("that would make a loop");
            if (!seen.insert(n).second) continue;
            for (auto& l : g.links) {
                if (l.from_node != n) continue;
                const GraphNode* fn = g.find(l.from_node);
                const NodeTypeDef* ft = fn ? node_type(k, fn->type) : nullptr;
                const PinDef* fp = ft ? ft->pin(l.from_pin) : nullptr;
                if (fp && fp->kind == pa->kind) todo.push_back(l.to_node);
            }
        }
    }
    return true;
}

std::string add_link(GraphKind k, Graph& g, const std::string& a, const std::string& pin_a, const std::string& b, const std::string& pin_b, std::string* why) {
    if (!link_allowed(k, g, a, pin_a, b, pin_b, why)) return {};
    const PinDef* pa = node_type(k, g.find(a)->type)->pin(pin_a);
    const PinDef* pb = node_type(k, g.find(b)->type)->pin(pin_b);
    // a single-link pin gives up its old link
    g.links.erase(std::remove_if(g.links.begin(), g.links.end(),
                                 [&](const GraphLink& l) {
                                     return (!pa->multi && l.from_node == a && l.from_pin == pin_a) || (!pb->multi && l.to_node == b && l.to_pin == pin_b);
                                 }),
                  g.links.end());
    GraphLink l{new_uid(), a, pin_a, b, pin_b};
    g.links.push_back(l);
    return l.uid;
}

void remove_node(Graph& g, const std::string& uid) {
    g.links.erase(std::remove_if(g.links.begin(), g.links.end(), [&](const GraphLink& l) { return l.from_node == uid || l.to_node == uid; }), g.links.end());
    g.nodes.erase(std::remove_if(g.nodes.begin(), g.nodes.end(), [&](const GraphNode& n) { return n.uid == uid; }), g.nodes.end());
}

GraphNode make_node(GraphKind k, const std::string& type, double x, double y) {
    GraphNode n;
    n.uid = new_uid();
    n.type = type;
    n.x = x;
    n.y = y;
    json& d = n.data;
    if (k == GraphKind::Dialog) {
        if (type == "option") {
            d["text"] = json::object();
            d["type"] = "Info";
            d["permanent"] = false;
        } else if (type == "line") {
            d["speaker"] = "npc";
            d["text"] = json::object();
        } else if (type == "command") {
            d["command"] = "GiveXP";
            d["params"] = json::object({{"Amount", 50}});
        } else if (type == "quest") {
            d["quest"] = "";
            d["action"] = "start";
        } else if (type == "log") {
            d["quest"] = "";
            d["text"] = json::object();
        } else if (type == "script") {
            d["script"] = "";
        } else if (type == "cond") {
            d["kind"] = "quest";
            d["args"] = json::object({{"quest", ""}, {"status", "Running"}});
        } else if (type == "comment") {
            d["text"] = "";
        }
    } else {
        if (type == "start") {
            d["trigger"] = "dialog";
        } else if (type == "objective") {
            d["kind"] = "items";
            d["item"] = "";
            d["count"] = 1;
            d["text"] = json::object();
        } else if (type == "log") {
            d["text"] = json::object();
        } else if (type == "reward") {
            d["xp"] = 0;
            d["items"] = json::array();
        } else if (type == "outcome") {
            d["result"] = "success";
            d["text"] = json::object();
        } else if (type == "comment") {
            d["text"] = "";
        }
    }
    return n;
}

namespace {

std::string arg_str(const json& a, const char* k) { return a.contains(k) && a[k].is_string() ? a[k].get<std::string>() : std::string(); }

}  // namespace

json leaf_condition(const std::string& kind, const json& a, std::vector<std::string>& errors) {
    // risen.cond.<kind>(args...) in the order of its parameters; trailing optional arguments are left out
    json c = json::array({kind});
    auto need = [&](const char* k) {
        const std::string v = arg_str(a, k);
        if (v.empty()) errors.push_back("condition '" + kind + "': " + k + " is empty");
        c.push_back(v);
    };
    auto opt_tail = [&](std::initializer_list<const char*> keys) {
        // optional arguments: written up to the last one that is set
        std::vector<json> vals;
        for (const char* k : keys) vals.push_back(a.contains(k) && !a[k].is_null() && !(a[k].is_string() && a[k].get<std::string>().empty()) ? a[k] : json(nullptr));
        while (!vals.empty() && vals.back().is_null()) vals.pop_back();
        for (auto& v : vals) c.push_back(v);
    };
    if (kind == "quest") {
        need("quest");
        opt_tail({"status"});
    } else if (kind == "item") {
        need("item");
        opt_tail({"count", "holder"});
    } else if (kind == "var") {
        need("name");
        opt_tail({"value", "op"});
    } else if (kind == "event") {
        need("name");
    } else if (kind == "skill") {
        need("skill");
        c.push_back(a.value("value", 1));
        opt_tail({"op", "who"});
    } else if (kind == "guild") {
        need("guild");
        opt_tail({"who"});
    } else if (kind == "dead" || kind == "alive") {
        need("who");
    } else {
        errors.push_back("unknown condition kind '" + kind + "'");
        return nullptr;
    }
    return c;
}

bool leaf_args(const json& c, std::string& kind, json& args) {
    if (!c.is_array() || c.empty() || !c[0].is_string()) return false;
    static const std::map<std::string, std::vector<const char*>> kParams = {
        {"quest", {"quest", "status"}}, {"item", {"item", "count", "holder"}}, {"var", {"name", "value", "op"}}, {"event", {"name"}},
        {"skill", {"skill", "value", "op", "who"}}, {"guild", {"guild", "who"}}, {"dead", {"who"}}, {"alive", {"who"}}};
    kind = c[0].get<std::string>();
    auto it = kParams.find(kind);
    if (it == kParams.end()) return false;
    args = json::object();
    for (size_t i = 0; i < it->second.size() && i + 1 < c.size(); ++i)
        if (!c[i + 1].is_null()) args[it->second[i]] = c[i + 1];
    return true;
}

json compile_condition(const Graph& g, const std::string& node_uid, std::vector<std::string>& errors, int depth) {
    const GraphNode* n = g.find(node_uid);
    if (!n) return nullptr;
    if (depth > 32) {
        errors.push_back("conditions nested too deep");
        return nullptr;
    }
    if (n->type == "all" || n->type == "any") {
        json list = json::array();
        for (auto* l : links_into(g, n->uid, "in")) {
            json c = compile_condition(g, l->from_node, errors, depth + 1);
            if (!c.is_null()) list.push_back(c);
        }
        if (list.empty()) {
            errors.push_back(std::string("an '") + (n->type == "all" ? "All of" : "Any of") + "' node has no conditions linked");
            return nullptr;
        }
        return json::array({n->type, list});
    }
    if (n->type == "not") {
        auto in = links_into(g, n->uid, "in");
        if (in.empty()) {
            errors.push_back("a 'Not' node has no condition linked");
            return nullptr;
        }
        json c = compile_condition(g, in.front()->from_node, errors, depth + 1);
        return c.is_null() ? json(nullptr) : json::array({"not", c});
    }
    if (n->type != "cond") return nullptr;
    return leaf_condition(n->data.value("kind", std::string()), n->data.value("args", json::object()), errors);
}

json condition_on(const Graph& g, const std::string& node_uid, const std::string& pin, std::vector<std::string>& errors) {
    json list = json::array();
    for (auto* l : links_into(g, node_uid, pin)) {
        json c = compile_condition(g, l->from_node, errors);
        if (!c.is_null()) list.push_back(c);
    }
    if (list.empty()) return nullptr;
    if (list.size() == 1) return list[0];
    return json::array({"all", list});
}

std::string describe_condition(const json& c) {
    if (!c.is_array() || c.empty() || !c[0].is_string()) return "?";
    const std::string k = c[0].get<std::string>();
    auto s = [&](size_t i) { return i < c.size() ? (c[i].is_string() ? c[i].get<std::string>() : c[i].dump()) : std::string(); };
    if (k == "all" || k == "any") {
        std::string out;
        const json& list = c.size() > 1 ? c[1] : json::array();
        for (size_t i = 0; i < list.size(); ++i) out += (i ? (k == "all" ? " and " : " or ") : "") + describe_condition(list[i]);
        return list.size() > 1 ? "(" + out + ")" : out;
    }
    if (k == "not") return "not " + describe_condition(c.size() > 1 ? c[1] : json());
    if (k == "quest") return "quest " + s(1) + " is " + (c.size() > 2 ? s(2) : std::string("Success"));  // risen.cond.quest's default
    if (k == "item") return (c.size() > 3 ? s(3) : std::string("player")) + " has " + (c.size() > 2 ? s(2) : std::string("1")) + " " + s(1);
    if (k == "var") return "variable " + s(1) + (c.size() > 2 ? " " + (c.size() > 3 ? s(3) : std::string("==")) + " " + s(2) : std::string(" exists"));
    if (k == "event") return "event " + s(1) + " happened";
    if (k == "skill") return (c.size() > 4 ? s(4) : std::string("player")) + " " + s(1) + " " + (c.size() > 3 ? s(3) : std::string(">=")) + " " + s(2);
    if (k == "guild") return (c.size() > 2 ? s(2) : std::string("player")) + " in guild " + s(1);
    if (k == "dead") return s(1) + " is dead";
    if (k == "alive") return s(1) + " is alive";
    return k;
}

std::string language_code(const std::string& language) {
    static const std::pair<const char*, const char*> kCodes[] = {{"English", "en"}, {"German", "de"}, {"French", "fr"}, {"Italian", "it"},
                                                                  {"Spanish", "es"}, {"Polish", "pl"}, {"Russian", "ru"}};
    for (auto& [n, c] : kCodes)
        if (language == n) return c;
    return language;
}

}  // namespace mk
