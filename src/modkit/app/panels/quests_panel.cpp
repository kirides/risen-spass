// Quests panel: a quest's identity (name, title, type, experience) and its flow as a node graph: Start (a dialogue or a trigger) -> objectives,
// log entries, rewards -> Outcome. Exported as data/quests.lua; the runtime walks the flow from the engine's events (core/quest.cpp).
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>

#include "app/editor_util.h"
#include "app/node_canvas.h"
#include "app/panels/panels.h"
#include "app/pickers.h"
#include "app/widgets.h"
#include "core/catalog.h"
#include "core/lua_writer.h"
#include "core/quest.h"
#include "core/validate.h"

namespace mk {

namespace {

std::string first_text(const Project& p, const json& t) {
    if (!t.is_object()) return {};
    for (auto& l : p.mod.languages)
        if (t.contains(l) && t[l].is_string() && !t[l].get<std::string>().empty()) return t[l].get<std::string>();
    return {};
}

bool json_text(const char* label, json& t, const std::vector<std::string>& langs, bool multiline) {
    Text tx;
    if (t.is_object())
        for (auto& [k, v] : t.items())
            if (v.is_string()) tx[k] = v.get<std::string>();
    if (!ui::lang_text(label, tx, langs, multiline)) return false;
    t = json::object();
    for (auto& [k, v] : tx) t[k] = v;
    return true;
}

bool json_str(const char* label, json& obj, const char* key, const char* hint = nullptr) {
    std::string s = obj.value(key, std::string());
    if (!(hint ? ImGui::InputTextWithHint(label, hint, &s) : ImGui::InputText(label, &s))) return false;
    obj[key] = s;
    return true;
}

bool json_int(const char* label, json& obj, const char* key, int def, int min_value = 0) {
    int v = obj.value(key, def);
    if (!ImGui::InputInt(label, &v)) return false;
    obj[key] = std::max(min_value, v);
    return true;
}

bool json_combo(const char* label, json& obj, const char* key, const std::vector<std::string>& items, bool allow_free = false) {
    std::string s = obj.value(key, std::string());
    bool ch = ui::combo(label, s, items, false);
    if (allow_free) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150);
        ch |= ImGui::InputText((std::string("##free") + key).c_str(), &s);
    }
    if (ch) obj[key] = s;
    return ch;
}

class QuestsPanel : public Panel {
public:
    const char* title() const override { return kPanelQuests; }

    void on_project_changed(App& app) override {
        if (!find(app.project, sel_)) sel_.clear();
    }
    bool select(App& app, const std::string& key) override {
        for (auto& q : app.project.quests)
            if (q.uid == key || q.name == key) {
                sel_ = q.uid;
                app.focus_panel(title());
                return true;
            }
        return false;
    }

    void draw(App& app) override {
        if (!ImGui::Begin(title(), &open)) {
            ImGui::End();
            return;
        }
        ImGui::BeginChild("##list", ImVec2(220, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
        draw_list(app);
        ImGui::EndChild();
        ImGui::SameLine();
        QuestDef* q = find(app.project, sel_);
        if (!q) {
            ImGui::TextDisabled("Create a quest. Dialogues start, succeed or fail quests with their Quest node; a quest with a flow (Start -> objectives "
                                "-> Outcome) follows the player's progress by itself.");
            ImGui::End();
            return;
        }
        ImGui::BeginGroup();
        draw_header(app, *q);
        const float inspector_w = 380;
        ImGui::BeginChild("##canvas", ImVec2(ImGui::GetContentRegionAvail().x - inspector_w - 8, 0), ImGuiChildFlags_Borders);
        std::vector<DialogIssue> issues;
        const json flow = compile_quest(app.project, *q, issues);
        std::map<std::string, std::string> marked;
        for (auto& i : issues)
            if (i.error && !i.node.empty()) marked[i.node] = i.message;
        canvas_.set_marked(marked);
        const Project& pr = app.project;
        if (canvas_.draw(q->uid, q->graph, [&](const GraphNode& n) { return summary(pr, n); })) app.touch();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##inspector", ImVec2(0, 0), ImGuiChildFlags_Borders);
        ImGui::PushItemWidth(-110);
        draw_inspector(app, *q, issues, flow);
        ImGui::PopItemWidth();
        ImGui::EndChild();
        ImGui::EndGroup();
        ImGui::End();
    }

private:
    static QuestDef* find(Project& p, const std::string& uid) {
        for (auto& q : p.quests)
            if (q.uid == uid) return &q;
        return nullptr;
    }

    void add_quest(App& app) {
        QuestDef q;
        q.uid = new_uid();
        std::string base = "Q_" + ui::name_prefix(app.project) + "_";
        for (int i = 1;; ++i)
            if (!app.project.quest(base + std::to_string(i))) {
                q.name = base + std::to_string(i);
                break;
            }
        q.title[app.project.mod.languages.empty() ? std::string("English") : app.project.mod.languages.front()] = "New quest";
        GraphNode s = make_node(GraphKind::Quest, "start", 0, 0);
        GraphNode o = make_node(GraphKind::Quest, "objective", 300, 0);
        GraphNode e = make_node(GraphKind::Quest, "outcome", 650, 0);
        q.graph.nodes = {s, o, e};
        add_link(GraphKind::Quest, q.graph, s.uid, "next", o.uid, "in");
        add_link(GraphKind::Quest, q.graph, o.uid, "next", e.uid, "in");
        app.project.quests.push_back(q);
        sel_ = q.uid;
        app.touch();
    }

    void draw_list(App& app) {
        if (ImGui::Button("+ New quest")) add_quest(app);
        ImGui::Separator();
        std::string remove;
        for (auto& q : app.project.quests) {
            ImGui::PushID(q.uid.c_str());
            if (ImGui::Selectable(q.name.c_str(), sel_ == q.uid)) sel_ = q.uid;
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Delete")) remove = q.uid;
                ImGui::EndPopup();
            }
            const std::string t = q.title.empty() ? std::string() : q.title.begin()->second;
            ImGui::TextDisabled("  %s", t.c_str());
            ImGui::PopID();
        }
        if (!remove.empty()) {
            auto& v = app.project.quests;
            v.erase(std::remove_if(v.begin(), v.end(), [&](const QuestDef& x) { return x.uid == remove; }), v.end());
            app.touch();
        }
    }

    void draw_header(App& app, QuestDef& q) {
        bool ch = false;
        ImGui::SetNextItemWidth(180);
        ch |= ImGui::InputText("Name", &q.name);
        ui::help("The quest's id (letters, digits, _): the key of its state in savegames. Keep it once released.");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        ch |= ui::combo("Type", q.type, quest_types(), false);
        ImGui::SameLine();
        ImGui::PushItemWidth(110);
        ch |= ui::opt_int("XP", q.xp, nullptr, 100);
        ImGui::PopItemWidth();
        ImGui::SameLine();
        ch |= ImGui::Checkbox("Silent", &q.silent);
        ui::help("No \"quest started\" notification.");
        if (ImGui::TreeNode("Title")) {
            ch |= ui::lang_text("##title", q.title, app.project.mod.languages);
            ImGui::TreePop();
        }
        if (ch) app.touch();
    }

    std::string summary(const Project& p, const GraphNode& n) {
        const json& d = n.data;
        auto s = [&](const char* k) { return d.value(k, std::string()); };
        auto i = [&](const char* k, int def) { return std::to_string(d.value(k, def)); };
        if (n.type == "start") {
            const std::string t = s("trigger").empty() ? std::string("dialog") : s("trigger");
            if (t == "dialog") return "started by a dialogue";
            if (t == "game_start") return "at the start of a new game";
            if (t == "item") return "when the player gets " + s("item");
            if (t == "kill") return "when " + s("target") + " dies";
            if (t == "var") return "when " + s("name") + " >= " + i("value", 1);
            if (t == "quest") return "when " + s("quest") + " is " + (s("status").empty() ? std::string("Success") : s("status"));
            return t;
        }
        if (n.type == "objective") {
            const std::string k = s("kind");
            std::string out;
            if (k == "items") out = "carry " + i("count", 1) + " " + s("item");
            else if (k == "kill") out = "kill " + i("count", 1) + " " + s("target");
            else if (k == "talk") out = "talk: " + s("info");
            else if (k == "var") out = s("name") + " >= " + i("value", 1);
            else if (k == "quest") out = s("quest") + " is " + (s("status").empty() ? std::string("Success") : s("status"));
            const std::string t = first_text(p, d.value("text", json::object()));
            if (!t.empty()) out += "\nlog: " + t;
            return out;
        }
        if (n.type == "log") return first_text(p, d.value("text", json::object()));
        if (n.type == "reward") {
            std::string out;
            if (d.value("xp", 0) > 0) out += i("xp", 0) + " XP\n";
            if (d.value("gold", 0) > 0) out += i("gold", 0) + " gold\n";
            if (d.contains("items") && d["items"].is_array())
                for (auto& it : d["items"])
                    if (it.is_array() && it.size() >= 2) out += std::to_string(it[1].get<int>()) + "x " + it[0].get<std::string>() + "\n";
            if (!s("var").empty()) out += s("var") + " = " + i("var_value", 1) + "\n";
            if (!out.empty()) out.pop_back();
            return out;
        }
        if (n.type == "outcome") {
            std::string out = s("result").empty() ? std::string("success") : s("result");
            if (!s("next_quest").empty()) out += "\nthen " + s("next_quest");
            return out;
        }
        if (n.type == "comment") return s("text");
        return {};
    }

    void draw_inspector(App& app, QuestDef& q, const std::vector<DialogIssue>& issues, const json& flow) {
        GraphNode* n = q.graph.find(canvas_.selected());
        if (!n) {
            ImGui::TextWrapped("Select a node to edit it.");
            ImGui::Spacing();
            ImGui::TextWrapped("The flow starts at Start and walks the chain: an Objective waits until the player did it, a Log entry and a Reward "
                               "happen at once, an Outcome ends the quest (success, failure, cancelled) and may start a follow-up quest. A quest without "
                               "a Start is ended by its dialogues alone.");
            if (!flow.is_null()) {
                ImGui::SeparatorText("Exported as");
                std::string lua = lua_value(flow, 0, 60);
                ImGui::InputTextMultiline("##lua", &lua, ImVec2(-FLT_MIN, 220), ImGuiInputTextFlags_ReadOnly);
            }
            draw_issues(issues, "");
            return;
        }
        const NodeTypeDef* t = node_type(GraphKind::Quest, n->type);
        ImGui::SeparatorText(t ? t->title : n->type.c_str());
        if (t && t->help) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", t->help);
            ImGui::PopStyleColor();
        }
        bool ch = false;
        json& d = n->data;
        const auto& langs = app.project.mod.languages;
        std::vector<std::string> quests, npcs_and_monsters;
        for (auto& x : app.project.quests)
            if (x.name != q.name) quests.push_back(x.name);
        for (auto& x : app.project.npcs) npcs_and_monsters.push_back(x.name);
        for (auto& x : app.project.npc_templates)
            if (x.mode == Mode::New) npcs_and_monsters.push_back(x.name);
        auto item_pick = [&](const char* label, const char* key) {
            std::string v = d.value(key, std::string());
            if (ui::template_picker(label, v, app.game.get(), {TplKind::Item, TplKind::Recipe}, ui::project_item_names(app.project))) {
                d[key] = v;
                ch = true;
            }
        };
        auto target_pick = [&](const char* label, const char* key) {
            std::string v = d.value(key, std::string());
            if (ui::template_picker(label, v, app.game.get(), {TplKind::Npc, TplKind::Monster}, npcs_and_monsters)) {
                d[key] = v;
                ch = true;
            }
            ui::help("A template (every wolf counts) or the unique name of one NPC.");
        };
        if (n->type == "start") {
            ch |= json_combo("Trigger", d, "trigger", quest_triggers());
            const std::string tr = d.value("trigger", std::string("dialog"));
            if (tr == "dialog") ImGui::TextDisabled("A dialogue's Quest node with the action 'start' starts it.");
            if (tr == "item") item_pick("Item", "item");
            if (tr == "kill") target_pick("Killed", "target");
            if (tr == "var") {
                ch |= json_str("Variable", d, "name");
                ch |= json_int("Value", d, "value", 1, -1000000);
            }
            if (tr == "quest") {
                ch |= json_combo("Quest", d, "quest", quests, true);
                ch |= json_combo("Status", d, "status", quest_states());
            }
        } else if (n->type == "objective") {
            ch |= json_combo("Kind", d, "kind", objective_kinds());
            const std::string k = d.value("kind", std::string("items"));
            if (k == "items") {
                item_pick("Item", "item");
                ch |= json_int("Count", d, "count", 1, 1);
                ImGui::TextDisabled("Done while the player carries that many.");
            } else if (k == "kill") {
                target_pick("Kill", "target");
                ch |= json_int("Count", d, "count", 1, 1);
                ImGui::TextDisabled("Deaths counted from the moment the objective begins.");
            } else if (k == "talk") {
                ch |= json_combo("Option", d, "info", project_info_names(app.project), true);
                ImGui::TextDisabled("Done when that dialogue option finished playing.");
            } else if (k == "var") {
                ch |= json_str("Variable", d, "name");
                ch |= json_int("At least", d, "value", 1, -1000000);
            } else if (k == "quest") {
                ch |= json_combo("Quest", d, "quest", quests, true);
                ch |= json_combo("Status", d, "status", quest_states());
            }
            ch |= json_text("Log entry when it begins", d["text"], langs, true);
        } else if (n->type == "log") {
            ch |= json_text("Text", d["text"], langs, true);
        } else if (n->type == "reward") {
            ch |= json_int("Experience", d, "xp", 0);
            ch |= json_int("Gold", d, "gold", 0);
            std::vector<InvItem> items;
            if (d.contains("items") && d["items"].is_array())
                for (auto& it : d["items"])
                    if (it.is_array() && it.size() >= 2) items.push_back({it[0].get<std::string>(), it[1].get<int>(), false});
            ImGui::TextUnformatted("Items");
            if (ui::inventory_list("items", items, app, ui::project_item_names(app.project), false)) {
                json a = json::array();
                for (auto& it : items) a.push_back(json::array({it.item, it.amount}));
                d["items"] = a;
                ch = true;
            }
            ch |= json_str("Set variable", d, "var", "a game variable (optional)");
            if (!d.value("var", std::string()).empty()) ch |= json_int("to", d, "var_value", 1, -1000000);
        } else if (n->type == "outcome") {
            ch |= json_combo("Result", d, "result", outcome_results());
            ch |= json_text("Log entry", d["text"], langs, true);
            ch |= json_combo("Then start", d, "next_quest", quests, true);
        } else if (n->type == "comment") {
            std::string s = d.value("text", std::string());
            if (ImGui::InputTextMultiline("##c", &s, ImVec2(-FLT_MIN, 120))) {
                d["text"] = s;
                ch = true;
            }
        }
        draw_issues(issues, n->uid);
        if (ch) app.touch();
    }

    static void draw_issues(const std::vector<DialogIssue>& issues, const std::string& node) {
        bool header = false;
        for (auto& i : issues) {
            if (i.node != node) continue;
            if (!header) {
                ImGui::SeparatorText("Problems");
                header = true;
            }
            ImGui::TextColored(i.error ? ImVec4(1, 0.45f, 0.4f, 1) : ImVec4(1, 0.8f, 0.35f, 1), "%s", i.message.c_str());
        }
    }

    std::string sel_;
    NodeCanvas canvas_{GraphKind::Quest};
};

}  // namespace

std::unique_ptr<Panel> make_quests_panel() { return std::make_unique<QuestsPanel>(); }

}  // namespace mk
