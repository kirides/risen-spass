#include "core/quest.h"

#include <set>

namespace mk {

const std::vector<std::string>& quest_triggers() {
    static const std::vector<std::string> v = {"dialog", "game_start", "item", "kill", "var", "quest"};
    return v;
}
const std::vector<std::string>& objective_kinds() {
    static const std::vector<std::string> v = {"items", "kill", "talk", "var", "quest"};
    return v;
}
const std::vector<std::string>& outcome_results() {
    static const std::vector<std::string> v = {"success", "fail", "cancel"};
    return v;
}

std::vector<std::string> project_info_names(const Project& p) {
    std::vector<std::string> v;
    for (auto& d : p.dialogs)
        for (auto& n : d.graph.nodes)
            if (n.type == "option") v.push_back(info_name(p, n));
    return v;
}

namespace {

std::string str(const json& d, const char* k) { return d.contains(k) && d[k].is_string() ? d[k].get<std::string>() : std::string(); }

std::string first_text(const Project& p, const json& t) {
    if (!t.is_object()) return {};
    for (auto& l : p.mod.languages)
        if (t.contains(l) && t[l].is_string() && !t[l].get<std::string>().empty()) return t[l].get<std::string>();
    return {};
}

}  // namespace

json compile_quest(const Project& p, const QuestDef& q, std::vector<DialogIssue>& issues) {
    const Graph& g = q.graph;
    auto err = [&](const std::string& node, const std::string& msg) { issues.push_back({true, node, msg}); };
    auto warn = [&](const std::string& node, const std::string& msg) { issues.push_back({false, node, msg}); };
    std::vector<const GraphNode*> starts;
    for (auto& n : g.nodes)
        if (n.type == "start") starts.push_back(&n);
    if (starts.empty()) {
        // a quest without a flow: started, succeeded and failed by the dialogues alone
        for (auto& n : g.nodes)
            if (n.type != "comment") {
                warn(n.uid, "no Start node: this node is not part of the quest's flow");
                break;
            }
        return nullptr;
    }
    if (starts.size() > 1) err(starts[1]->uid, "a quest has one Start node");
    const GraphNode& s = *starts.front();
    json out = json::object();
    json trig = json::object();
    const std::string tk = str(s.data, "trigger").empty() ? std::string("dialog") : str(s.data, "trigger");
    trig["kind"] = tk;
    if (tk == "item") {
        trig["item"] = str(s.data, "item");
        if (trig["item"] == "") err(s.uid, "the trigger needs an item");
    } else if (tk == "kill") {
        trig["target"] = str(s.data, "target");
        if (trig["target"] == "") err(s.uid, "the trigger needs a template or NPC name");
    } else if (tk == "var") {
        trig["name"] = str(s.data, "name");
        trig["value"] = s.data.value("value", 1);
        if (trig["name"] == "") err(s.uid, "the trigger needs a game variable");
    } else if (tk == "quest") {
        trig["quest"] = str(s.data, "quest");
        trig["status"] = str(s.data, "status").empty() ? std::string("Success") : str(s.data, "status");
        if (trig["quest"] == "") err(s.uid, "the trigger needs a quest");
        else if (trig["quest"] == q.name) err(s.uid, "a quest cannot start itself");
    } else if (tk != "dialog" && tk != "game_start") {
        err(s.uid, "unknown trigger '" + tk + "'");
    }
    out["trigger"] = trig;

    json steps = json::array();
    std::set<std::string> seen;
    bool ended = false;
    auto nx = links_from(g, s.uid, "next");
    const GraphNode* cur = nx.empty() ? nullptr : g.find(nx.front()->to_node);
    if (!cur) err(s.uid, "the Start node leads nowhere");
    while (cur) {
        if (!seen.insert(cur->uid).second) {
            err(cur->uid, "the quest's flow runs in a circle");
            break;
        }
        const json& d = cur->data;
        json st = json::object();
        st["kind"] = cur->type;
        if (cur->type == "objective") {
            const std::string k = str(d, "kind");
            st["objective"] = k;
            if (k == "items") {
                st["item"] = str(d, "item");
                st["count"] = std::max(1, d.value("count", 1));
                if (st["item"] == "") err(cur->uid, "the objective needs an item");
            } else if (k == "kill") {
                st["target"] = str(d, "target");
                st["count"] = std::max(1, d.value("count", 1));
                if (st["target"] == "") err(cur->uid, "the objective needs a template or NPC name");
            } else if (k == "talk") {
                st["info"] = str(d, "info");
                if (st["info"] == "") err(cur->uid, "the objective needs a dialogue option");
            } else if (k == "var") {
                st["name"] = str(d, "name");
                st["value"] = d.value("value", 1);
                if (st["name"] == "") err(cur->uid, "the objective needs a game variable");
            } else if (k == "quest") {
                st["quest"] = str(d, "quest");
                st["status"] = str(d, "status").empty() ? std::string("Success") : str(d, "status");
                if (st["quest"] == "") err(cur->uid, "the objective needs a quest");
            } else {
                err(cur->uid, "unknown objective '" + k + "'");
            }
            const std::string t = first_text(p, d.value("text", json::object()));
            if (!t.empty()) st["log"] = t;
        } else if (cur->type == "log") {
            st["text"] = first_text(p, d.value("text", json::object()));
            if (st["text"] == "") err(cur->uid, "the log entry has no text");
        } else if (cur->type == "reward") {
            const int xp = d.value("xp", 0), gold = d.value("gold", 0);
            if (xp > 0) st["xp"] = xp;
            if (gold > 0) st["gold"] = gold;
            json items = json::array();
            if (d.contains("items") && d["items"].is_array())
                for (auto& it : d["items"])
                    if (it.is_array() && it.size() >= 2 && it[0].is_string() && !it[0].get<std::string>().empty()) items.push_back(json::array({it[0], it[1]}));
            if (!items.empty()) st["items"] = items;
            if (!str(d, "var").empty()) {
                st["var"] = str(d, "var");
                st["var_value"] = d.value("var_value", 1);
            }
            if (st.size() == 1) warn(cur->uid, "the reward gives nothing");
        } else if (cur->type == "outcome") {
            const std::string r = str(d, "result").empty() ? std::string("success") : str(d, "result");
            st["result"] = r;
            const std::string t = first_text(p, d.value("text", json::object()));
            if (!t.empty()) st["log"] = t;
            if (!str(d, "next_quest").empty()) {
                st["next_quest"] = str(d, "next_quest");
                if (!p.quest(str(d, "next_quest"))) err(cur->uid, "the follow-up quest '" + str(d, "next_quest") + "' is not in the project");
            }
            ended = true;
        } else {
            err(cur->uid, "a " + cur->type + " node cannot be part of a quest's flow");
        }
        steps.push_back(st);
        if (ended) break;
        auto n2 = links_from(g, cur->uid, "next");
        cur = n2.empty() ? nullptr : g.find(n2.front()->to_node);
    }
    if (!ended) warn(s.uid, "the flow has no Outcome: the quest is only ended by a dialogue");
    out["steps"] = steps;
    return out;
}

}  // namespace mk
