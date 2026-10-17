#include "core/dialog.h"

#include <algorithm>
#include <map>
#include <set>

namespace mk {

namespace {

struct CommandRow {
    const char* name;
    std::vector<std::pair<const char*, const char*>> params;
};
#include "core/dialog_commands.inc"

std::string text_first(const Project& p, const json& t) {
    if (!t.is_object() || t.empty()) return {};
    for (auto& lang : p.mod.languages)
        if (t.contains(lang) && t[lang].is_string() && !t[lang].get<std::string>().empty()) return t[lang].get<std::string>();
    for (auto& [k, v] : t.items())
        if (v.is_string() && !v.get<std::string>().empty()) return v.get<std::string>();
    return {};
}

}  // namespace

const std::vector<CommandDef>& dialog_commands() {
    static const std::vector<CommandDef> v = [] {
        std::vector<CommandDef> out;
        for (const CommandRow& r : kCommandRows) {
            CommandDef d{r.name, {}};
            for (auto& [n, t] : r.params) d.params.push_back({n, t});
            out.push_back(std::move(d));
        }
        return out;
    }();
    return v;
}

const CommandDef* dialog_command(const std::string& name) {
    for (auto& c : dialog_commands())
        if (c.name == name) return &c;
    return nullptr;
}

std::string info_name(const Project& p, const GraphNode& option) {
    const std::string s = option.data.value("info", std::string());
    if (!s.empty()) return s;
    return p.mod.id + "_" + option.uid.substr(0, 8);
}

std::string quest_action_script(const std::string& quest, const std::string& action) { return "MK_" + quest + "_" + action; }

json line_text(const Project& p, const json& text) {
    json o = json::object();
    const std::string first = text_first(p, text);
    if (first.empty()) return nullptr;
    o["default"] = first;
    for (size_t i = 0; i < p.mod.languages.size(); ++i) {
        const std::string& lang = p.mod.languages[i];
        if (text.is_object() && text.contains(lang) && text[lang].is_string() && !text[lang].get<std::string>().empty() && i > 0)
            o[language_code(lang)] = text[lang];
    }
    return o.size() == 1 ? json(first) : o;
}

json compile_dialog(const Project& p, const DialogDef& d, std::vector<DialogIssue>& issues, json* actions) {
    const Graph& g = d.graph;
    auto err = [&](const std::string& node, const std::string& msg) { issues.push_back({true, node, msg}); };
    auto warn = [&](const std::string& node, const std::string& msg) { issues.push_back({false, node, msg}); };

    // options, parents first (the engine needs the parent info when it makes a child); otherwise in the order of the canvas (top to bottom)
    std::vector<const GraphNode*> options;
    for (auto& n : g.nodes)
        if (n.type == "option") options.push_back(&n);
    std::stable_sort(options.begin(), options.end(), [](const GraphNode* a, const GraphNode* b) { return a->y < b->y; });
    auto parent_of = [&](const GraphNode& o) -> const GraphNode* {
        auto in = links_into(g, o.uid, "parent");
        return in.empty() ? nullptr : g.find(in.front()->from_node);
    };
    std::vector<const GraphNode*> ordered;
    std::set<std::string> placed;
    for (size_t guard = 0; ordered.size() < options.size() && guard <= options.size(); ++guard) {
        for (const GraphNode* o : options) {
            if (placed.count(o->uid)) continue;
            const GraphNode* par = parent_of(*o);
            if (par && !placed.count(par->uid)) continue;
            ordered.push_back(o);
            placed.insert(o->uid);
        }
    }
    if (ordered.size() < options.size()) err("", "the options' parent links make a loop");

    if (d.owner.empty()) err("", "the dialogue has no owner NPC");
    json infos = json::array();
    for (const GraphNode* o : ordered) {
        json info = json::object();
        const std::string name = info_name(p, *o);
        info["name"] = name;
        info["owner"] = d.owner;
        if (const GraphNode* par = parent_of(*o)) info["parent"] = info_name(p, *par);
        const std::string type = o->data.value("type", std::string("Info"));
        if (type != "Info") info["type"] = type;
        if (o->data.value("permanent", false)) info["permanent"] = true;
        std::vector<std::string> cerr;
        json when = condition_on(g, o->uid, "when", cerr);
        for (auto& e : cerr) err(o->uid, e);
        if (!when.is_null()) info["when"] = when;

        json lines = json::array();
        json first = line_text(p, o->data.value("text", json::object()));
        if (first.is_null()) err(o->uid, "the option has no text (what the player says)");
        lines.push_back(json::object({{"speaker", "player"}, {"listener", "npc"}, {"text", first.is_null() ? json("...") : first}}));
        // the sequence on "then"
        std::set<std::string> seen;
        bool ended = false;
        auto out = links_from(g, o->uid, "then");
        const GraphNode* cur = out.empty() ? nullptr : g.find(out.front()->to_node);
        while (cur) {
            if (!seen.insert(cur->uid).second) {
                err(o->uid, "the sequence of this option runs in a circle");
                break;
            }
            const json& dd = cur->data;
            if (cur->type == "line") {
                json t = line_text(p, dd.value("text", json::object()));
                if (t.is_null()) err(cur->uid, "the line has no text");
                std::string speaker = dd.value("speaker", std::string("npc"));
                if (speaker.empty()) speaker = "npc";
                const std::string listener = speaker == "player" ? "npc" : "player";
                lines.push_back(json::object({{"speaker", speaker}, {"listener", listener}, {"text", t.is_null() ? json("...") : t}}));
            } else if (cur->type == "command") {
                const std::string cmd = dd.value("command", std::string());
                const CommandDef* def = dialog_command(cmd);
                if (!def) err(cur->uid, "unknown dialogue command '" + cmd + "'");
                json l = json::object({{"command", cmd}});
                if (dd.contains("params") && dd["params"].is_object()) {
                    for (auto& [k, v] : dd["params"].items()) {
                        if (def && std::none_of(def->params.begin(), def->params.end(), [&](const CommandParam& cp) { return cp.name == k; }))
                            err(cur->uid, cmd + " has no parameter '" + k + "'");
                        if (v.is_string() && v.get<std::string>().empty()) continue;
                        l[k] = v;
                    }
                }
                if (dd.contains("text")) {
                    json t = line_text(p, dd["text"]);
                    if (!t.is_null()) l["text"] = t;
                }
                lines.push_back(l);
                if (cmd == "End") {
                    ended = true;
                    break;
                }
            } else if (cur->type == "quest") {
                const std::string q = dd.value("quest", std::string()), a = dd.value("action", std::string("start"));
                if (q.empty()) err(cur->uid, "the Quest node names no quest");
                else if (!p.quest(q)) warn(cur->uid, "'" + q + "' is not a quest of the project");
                const std::string script = quest_action_script(q, a);
                lines.push_back(json::object({{"script", script}}));
                if (actions) actions->push_back(json::object({{"script", script}, {"quest", q}, {"action", a}}));
            } else if (cur->type == "log") {
                const std::string q = dd.value("quest", std::string());
                const std::string t = text_first(p, dd.value("text", json::object()));
                if (q.empty() || t.empty()) err(cur->uid, "a log entry needs a quest and a text");
                lines.push_back(json::object({{"log", json::object({{"quest", q}, {"text", t}})}}));
            } else if (cur->type == "script") {
                const std::string s = dd.value("script", std::string());
                if (s.empty()) err(cur->uid, "the Script node names no script");
                lines.push_back(json::object({{"script", s}}));
            } else if (cur->type == "end") {
                lines.push_back(json::object({{"command", "End"}}));
                ended = true;
                break;
            }
            auto nx = links_from(g, cur->uid, "next");
            cur = nx.empty() ? nullptr : g.find(nx.front()->to_node);
        }
        const bool has_children = !links_from(g, o->uid, "choices").empty();
        if (ended && has_children) warn(o->uid, "the sequence ends the conversation, so the sub-options are never shown");
        // an open conversation blocks saving: an option without sub-options ends it
        if (!ended && !has_children) lines.push_back(json::object({{"command", "End"}}));
        info["lines"] = lines;
        infos.push_back(info);
    }
    return infos;
}

json dialogs_data(const Project& p, json* actions) {
    json all = json::array();
    for (auto& d : p.dialogs) {
        std::vector<DialogIssue> issues;
        for (auto& i : compile_dialog(p, d, issues, actions)) all.push_back(i);
    }
    return all;
}

}  // namespace mk
