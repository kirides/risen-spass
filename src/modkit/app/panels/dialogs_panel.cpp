// Dialogues panel: one node graph per conversation of an NPC (options, lines, commands, quest actions, conditions) on a scrolling canvas, with the
// selected node's properties on the right. Exported as risen.dialog.create specs (core/dialog.cpp, data/dialogs.lua).
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>

#include "app/editor_util.h"
#include "app/node_canvas.h"
#include "app/panels/panels.h"
#include "app/pickers.h"
#include "app/widgets.h"
#include "core/catalog.h"
#include "core/dialog.h"
#include "core/lua_writer.h"
#include "core/validate.h"

namespace mk {

namespace {

std::string first_text(const Project& p, const json& t) {
    if (!t.is_object()) return {};
    for (auto& l : p.mod.languages)
        if (t.contains(l) && t[l].is_string() && !t[l].get<std::string>().empty()) return t[l].get<std::string>();
    return {};
}

std::string shorten(std::string s, size_t n = 90) {
    if (s.size() > n) s = s.substr(0, n) + "...";
    return s;
}

// a Text (language -> text) stored in a json object
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

class DialogsPanel : public Panel {
public:
    const char* title() const override { return kPanelDialogs; }

    void on_project_changed(App& app) override {
        if (!find(app.project, sel_)) sel_.clear();
    }
    bool select(App& app, const std::string& key) override {
        for (auto& d : app.project.dialogs)
            if (d.uid == key || d.name == key) {
                sel_ = d.uid;
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
        DialogDef* d = find(app.project, sel_);
        if (!d) {
            ImGui::TextDisabled("Create a dialogue: a conversation of one NPC (the owner) with the player.");
            ImGui::End();
            return;
        }
        ImGui::BeginGroup();
        draw_header(app, *d);
        const float inspector_w = 380;
        ImGui::BeginChild("##canvas", ImVec2(ImGui::GetContentRegionAvail().x - inspector_w - 8, 0), ImGuiChildFlags_Borders);
        // problems of this dialogue mark their nodes
        std::vector<DialogIssue> issues;
        compile_dialog(app.project, *d, issues);
        std::map<std::string, std::string> marked;
        for (auto& i : issues)
            if (i.error && !i.node.empty()) marked[i.node] = i.message;
        canvas_.set_marked(marked);
        const Project& pr = app.project;
        if (canvas_.draw(d->uid, d->graph, [&](const GraphNode& n) { return summary(pr, d->graph, n); })) app.touch();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##inspector", ImVec2(0, 0), ImGuiChildFlags_Borders);
        ImGui::PushItemWidth(-110);
        draw_inspector(app, *d, issues);
        ImGui::PopItemWidth();
        ImGui::EndChild();
        ImGui::EndGroup();
        ImGui::End();
    }

private:
    static DialogDef* find(Project& p, const std::string& uid) {
        for (auto& d : p.dialogs)
            if (d.uid == uid) return &d;
        return nullptr;
    }

    void add_dialog(App& app) {
        DialogDef d;
        d.uid = new_uid();
        d.name = "Dialogue_" + std::to_string(app.project.dialogs.size() + 1);
        d.owner = app.project.npcs.empty() ? std::string() : app.project.npcs.front().name;
        // a first option that ends the conversation
        GraphNode opt = make_node(GraphKind::Dialog, "option", 0, 0);
        opt.data["text"] = json::object({{app.project.mod.languages.empty() ? std::string("English") : app.project.mod.languages.front(), "Hello."}});
        opt.data["info"] = suggest_info(app.project, d, 1);
        GraphNode line = make_node(GraphKind::Dialog, "line", 320, 0);
        GraphNode end = make_node(GraphKind::Dialog, "end", 640, 0);
        d.graph.nodes = {opt, line, end};
        add_link(GraphKind::Dialog, d.graph, opt.uid, "then", line.uid, "in");
        add_link(GraphKind::Dialog, d.graph, line.uid, "next", end.uid, "in");
        app.project.dialogs.push_back(d);
        sel_ = d.uid;
        app.touch();
    }

    // <mod id>_<dialogue name>_<n>, the first n not used by any option of the project
    static std::string suggest_info(const Project& p, const DialogDef& d, int start) {
        auto taken = [&](const std::string& s) {
            for (auto& dd : p.dialogs)
                for (auto& n : dd.graph.nodes)
                    if (n.type == "option" && info_name(p, n) == s) return true;
            for (auto& n : d.graph.nodes)
                if (n.type == "option" && n.data.value("info", std::string()) == s) return true;
            return false;
        };
        std::string base = p.mod.id + "_" + d.name;
        for (char& ch : base)
            if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_' && ch != '-') ch = '_';
        for (int i = start;; ++i)
            if (!taken(base + "_" + std::to_string(i))) return base + "_" + std::to_string(i);
    }

    void draw_list(App& app) {
        if (ImGui::Button("+ New dialogue")) add_dialog(app);
        ImGui::Separator();
        std::string remove;
        for (auto& d : app.project.dialogs) {
            ImGui::PushID(d.uid.c_str());
            if (ImGui::Selectable(d.name.c_str(), sel_ == d.uid)) sel_ = d.uid;
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Delete")) remove = d.uid;
                ImGui::EndPopup();
            }
            ImGui::TextDisabled("  %s", d.owner.empty() ? "(no owner)" : d.owner.c_str());
            ImGui::PopID();
        }
        if (!remove.empty()) {
            auto& v = app.project.dialogs;
            v.erase(std::remove_if(v.begin(), v.end(), [&](const DialogDef& x) { return x.uid == remove; }), v.end());
            app.touch();
        }
    }

    void draw_header(App& app, DialogDef& d) {
        bool ch = false;
        ImGui::SetNextItemWidth(200);
        ch |= ImGui::InputText("Name", &d.name);
        ImGui::SameLine();
        std::vector<std::string> npcs;
        for (auto& n : app.project.npcs) npcs.push_back(n.name);
        ImGui::SetNextItemWidth(200);
        if (ImGui::BeginCombo("##owner", d.owner.empty() ? "(owner)" : d.owner.c_str(), ImGuiComboFlags_HeightLarge)) {
            for (auto& n : npcs)
                if (ImGui::Selectable(n.c_str(), n == d.owner)) {
                    d.owner = n;
                    ch = true;
                }
            // the NPCs placed in the game world (their unique names)
            if (app.world) {
                ImGui::SeparatorText(("NPCs of " + app.world->name).c_str());
                static std::string filter;
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputTextWithHint("##of", "filter", &filter);
                for (auto& e : app.world->entities) {
                    if (!e.npc || e.kind != TplKind::Npc) continue;
                    if (!filter.empty() && e.name.find(filter) == std::string::npos) continue;
                    if (ImGui::Selectable((e.name + "##w" + e.guid).c_str(), e.name == d.owner)) {
                        d.owner = e.name;
                        ch = true;
                    }
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160);
        ch |= ImGui::InputTextWithHint("Owner NPC", "or a unique name", &d.owner);
        ui::help("The NPC who owns the options: a placed NPC of the project, or the unique entity name of an NPC in the world.");
        ImGui::SameLine();
        ImGui::TextDisabled("  right-click: new node · drag from a pin into space: new linked node · Del: delete");
        if (ch) app.touch();
    }

    std::string summary(const Project& p, const Graph& g, const GraphNode& n) {
        const json& d = n.data;
        if (n.type == "option") {
            std::string s = "\"" + shorten(first_text(p, d.value("text", json::object()))) + "\"";
            const std::string type = d.value("type", std::string("Info"));
            if (type != "Info") s += "\n[" + type + "]";
            if (d.value("permanent", false)) s += "\n[permanent]";
            s += "\n" + info_name(p, n);
            return s;
        }
        if (n.type == "line") return d.value("speaker", std::string("npc")) + ": " + shorten(first_text(p, d.value("text", json::object())));
        if (n.type == "command") {
            std::string s = d.value("command", std::string());
            if (d.contains("params") && d["params"].is_object())
                for (auto& [k, v] : d["params"].items()) s += "\n" + k + " = " + (v.is_string() ? v.get<std::string>() : v.dump());
            return s;
        }
        if (n.type == "quest") return d.value("action", std::string()) + " " + d.value("quest", std::string());
        if (n.type == "log") return d.value("quest", std::string()) + ": " + shorten(first_text(p, d.value("text", json::object())), 60);
        if (n.type == "script") return d.value("script", std::string());
        if (n.type == "cond") {
            std::vector<std::string> errs;
            return describe_condition(compile_condition(g, n.uid, errs));
        }
        if (n.type == "comment") return d.value("text", std::string());
        return {};
    }

    void draw_inspector(App& app, DialogDef& d, const std::vector<DialogIssue>& issues) {
        GraphNode* n = d.graph.find(canvas_.selected());
        if (!n) {
            ImGui::TextWrapped("Select a node to edit it.");
            ImGui::Spacing();
            ImGui::TextWrapped("An Option is what the player says; its 'then' pin leads to the lines and commands that follow, its 'choices' pin to "
                               "sub-options, 'when' takes conditions. A sequence without an End returns to the list of options (an option without "
                               "sub-options gets an End by itself).");
            draw_issues(issues, "");
            return;
        }
        const NodeTypeDef* t = node_type(GraphKind::Dialog, n->type);
        ImGui::SeparatorText(t ? t->title : n->type.c_str());
        if (t && t->help) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", t->help);
            ImGui::PopStyleColor();
        }
        bool ch = false;
        json& dd = n->data;
        const auto& langs = app.project.mod.languages;
        if (n->type == "option") {
            ch |= json_text("Player says", dd["text"], langs, true);
            ch |= json_combo("Type", dd, "type", info_types());
            ui::help("Info: offered in the list. Important: the NPC starts with it. Comment, News, Refuse, Parent: special kinds of the engine.");
            bool perm = dd.value("permanent", false);
            if (ImGui::Checkbox("Permanent (stays after use)", &perm)) {
                dd["permanent"] = perm;
                ch = true;
            }
            std::string info = dd.value("info", std::string());
            if (ImGui::InputTextWithHint("Info name", info_name(app.project, *n).c_str(), &info)) {
                dd["info"] = info;
                ch = true;
            }
            ui::help("The option's unique name: the engine saves 'option used' under it. Keep it once released.");
            if (ImGui::SmallButton("Add a sub-option")) {
                GraphNode c = make_node(GraphKind::Dialog, "option", n->x + 60, n->y + 220);
                c.data["info"] = suggest_info(app.project, d, 1);
                const std::string parent = n->uid;
                d.graph.nodes.push_back(c);
                add_link(GraphKind::Dialog, d.graph, parent, "choices", c.uid, "parent");
                canvas_.select(d.uid, c.uid);
                ch = true;
                n = d.graph.find(parent);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Add a condition")) {
                GraphNode c = make_node(GraphKind::Dialog, "cond", n->x - 300, n->y + 60);
                const std::string target = n->uid;
                d.graph.nodes.push_back(c);
                add_link(GraphKind::Dialog, d.graph, c.uid, "out", target, "when");
                ch = true;
                n = d.graph.find(target);
            }
            if (n) {
                // the compiled option, as the export writes it
                std::vector<DialogIssue> tmp;
                for (auto& info_json : compile_dialog(app.project, d, tmp))
                    if (info_json["name"] == info_name(app.project, *n)) {
                        ImGui::SeparatorText("Exported as");
                        std::string lua = lua_value(info_json, 0, 60);
                        ImGui::InputTextMultiline("##lua", &lua, ImVec2(-FLT_MIN, 160), ImGuiInputTextFlags_ReadOnly);
                    }
            }
        } else if (n->type == "line") {
            std::string sp = dd.value("speaker", std::string("npc"));
            static const std::vector<std::string> kSpeakers = {"npc", "player"};
            if (ui::combo("Speaker", sp, kSpeakers, false)) {
                dd["speaker"] = sp;
                ch = true;
            }
            ch |= json_str("Other NPC", dd, "speaker", "or a unique name");
            ch |= json_text("Text", dd["text"], langs, true);
        } else if (n->type == "command") {
            std::vector<std::string> names;
            for (auto& c : dialog_commands()) names.push_back(c.name);
            std::string cmd = dd.value("command", std::string());
            if (ui::combo("Command", cmd, names, false)) {
                dd["command"] = cmd;
                dd["params"] = json::object();
                ch = true;
            }
            if (const CommandDef* def = dialog_command(cmd)) {
                if (!dd.contains("params") || !dd["params"].is_object()) dd["params"] = json::object();
                json& ps = dd["params"];
                for (auto& par : def->params) {
                    if (par.name == "Speaker" || par.name == "Listener" || par.name == "Gesture") continue;  // the base class's
                    ImGui::PushID(par.name.c_str());
                    if (par.type == "gCInfoLocString") {
                        ch |= json_text(par.name.c_str(), dd["text"], langs, false);
                    } else if (par.type == "long") {
                        int v = ps.value(par.name, 0);
                        if (ImGui::InputInt(par.name.c_str(), &v)) {
                            ps[par.name] = v;
                            ch = true;
                        }
                    } else if (par.type == "bool") {
                        bool v = ps.value(par.name, false);
                        if (ImGui::Checkbox(par.name.c_str(), &v)) {
                            ps[par.name] = v;
                            ch = true;
                        }
                    } else if (par.type == "eCTemplateEntityProxy") {
                        std::string v = ps.value(par.name, std::string());
                        if (ui::template_picker(par.name.c_str(), v, app.game.get(), {TplKind::Item, TplKind::Recipe}, ui::project_item_names(app.project))) {
                            ps[par.name] = v;
                            ch = true;
                        }
                    } else {
                        const bool entity = par.name.rfind("Entity", 0) == 0;
                        ch |= json_str(par.name.c_str(), ps, par.name.c_str(), entity ? "player, npc or a unique name" : nullptr);
                    }
                    ImGui::PopID();
                }
            }
        } else if (n->type == "quest") {
            std::vector<std::string> qs;
            for (auto& q : app.project.quests) qs.push_back(q.name);
            ch |= json_combo("Quest", dd, "quest", qs, true);
            static const std::vector<std::string> kActions = {"start", "succeed", "fail", "cancel"};
            ch |= json_combo("Action", dd, "action", kActions);
        } else if (n->type == "log") {
            std::vector<std::string> qs;
            for (auto& q : app.project.quests) qs.push_back(q.name);
            ch |= json_combo("Quest", dd, "quest", qs, true);
            ch |= json_text("Text", dd["text"], langs, true);
        } else if (n->type == "script") {
            ch |= json_str("Script", dd, "script", "a script name");
        } else if (n->type == "cond") {
            ch |= condition_editor(app, dd);
        } else if (n->type == "comment") {
            std::string s = dd.value("text", std::string());
            if (ImGui::InputTextMultiline("##c", &s, ImVec2(-FLT_MIN, 120))) {
                dd["text"] = s;
                ch = true;
            }
        }
        if (n) draw_issues(issues, n->uid);
        if (ch) app.touch();
    }

    bool condition_editor(App& app, json& dd) {
        bool ch = false;
        static const std::vector<std::string> kKinds = {"quest", "item", "var", "event", "skill", "guild", "dead", "alive"};
        std::string kind = dd.value("kind", std::string("quest"));
        if (ui::combo("Kind", kind, kKinds, false)) {
            dd["kind"] = kind;
            dd["args"] = json::object();
            ch = true;
        }
        if (!dd.contains("args") || !dd["args"].is_object()) dd["args"] = json::object();
        json& a = dd["args"];
        static const std::vector<std::string> kOps = {">=", "==", "~=", "<=", ">", "<"};
        std::vector<std::string> qs, npcs;
        for (auto& q : app.project.quests) qs.push_back(q.name);
        for (auto& n : app.project.npcs) npcs.push_back(n.name);
        auto who = [&](const char* key, const char* label) {
            ch |= json_str(label, a, key, "player (empty), npc or a unique name");
        };
        if (kind == "quest") {
            ch |= json_combo("Quest", a, "quest", qs, true);
            ch |= json_combo("Status", a, "status", quest_states());
        } else if (kind == "item") {
            std::string it = a.value("item", std::string());
            if (ui::template_picker("Item", it, app.game.get(), {TplKind::Item, TplKind::Recipe}, ui::project_item_names(app.project))) {
                a["item"] = it;
                ch = true;
            }
            int cnt = a.value("count", 1);
            if (ImGui::InputInt("Count", &cnt)) {
                a["count"] = std::max(1, cnt);
                ch = true;
            }
            who("holder", "Holder");
        } else if (kind == "var") {
            ch |= json_str("Variable", a, "name");
            bool has = a.contains("value") && !a["value"].is_null();
            if (ImGui::Checkbox("Compare a value", &has)) {
                if (has) a["value"] = 1;
                else {
                    a.erase("value");
                    a.erase("op");
                }
                ch = true;
            }
            if (has) {
                int v = a.value("value", 1);
                if (ImGui::InputInt("Value", &v)) {
                    a["value"] = v;
                    ch = true;
                }
                ch |= json_combo("Compare", a, "op", kOps);
            }
        } else if (kind == "event") {
            ch |= json_str("Event", a, "name");
            ui::help("A game event: set by the dialogue command SetGameEvent (a game variable with the value 0).");
        } else if (kind == "skill") {
            ch |= json_combo("Skill", a, "skill", skill_names(app.game.get()));
            int v = a.value("value", 1);
            if (ImGui::InputInt("Value", &v)) {
                a["value"] = v;
                ch = true;
            }
            ch |= json_combo("Compare", a, "op", kOps);
            who("who", "Who");
        } else if (kind == "guild") {
            ch |= json_combo("Guild", a, "guild", guild_names());
            who("who", "Who");
        } else {
            ch |= json_combo("NPC", a, "who", npcs, true);
        }
        return ch;
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
    NodeCanvas canvas_{GraphKind::Dialog};
};

}  // namespace

std::unique_ptr<Panel> make_dialogs_panel() { return std::make_unique<DialogsPanel>(); }

}  // namespace mk
