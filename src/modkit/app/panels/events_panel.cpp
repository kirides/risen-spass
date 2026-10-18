// Events panel (stage E21): event graphs as node graphs. A trigger node (something happens in the game) walks along its links through conditions, flow
// nodes and actions. The node kinds and their fields come from one table (core/events.cpp), so this panel draws every inspector from it.
// Exported as data/events.lua, run by modkit/events.lua.
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>

#include "app/editor_util.h"
#include "app/node_canvas.h"
#include "app/panels/panels.h"
#include "app/pickers.h"
#include "app/widgets.h"
#include "core/catalog.h"
#include "core/events.h"
#include "core/lua_writer.h"
#include "core/validate.h"

namespace mk {

namespace {

class EventsPanel : public Panel {
public:
    const char* title() const override { return kPanelEvents; }

    void on_project_changed(App& app) override {
        if (!find(app.project, sel_)) sel_.clear();
    }
    bool select(App& app, const std::string& key) override {
        for (auto& e : app.project.events)
            if (e.uid == key || e.name == key) {
                sel_ = e.uid;
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
        if (sel_.empty() && !app.project.events.empty()) sel_ = app.project.events.front().uid;
        EventGraphDef* g = find(app.project, sel_);
        if (!g) {
            ImGui::TextWrapped("Create an event graph. A trigger node (\"On chapter\", \"On player moved\", \"On kill\", ...) starts it; conditions, flow nodes and actions "
                               "follow. Example: On chapter 3 -> Condition (intro = 0) -> Set variable (intro = 1) -> HUD message -> End.");
            ImGui::End();
            return;
        }
        ImGui::BeginGroup();
        draw_header(app, *g);
        const float inspector_w = 380;
        ImGui::BeginChild("##canvas", ImVec2(ImGui::GetContentRegionAvail().x - inspector_w - 8, 0), ImGuiChildFlags_Borders);
        std::vector<DialogIssue> issues;
        const json data = compile_events(app.project, *g, issues);
        std::map<std::string, std::string> marked;
        for (auto& i : issues)
            if (i.error && !i.node.empty()) marked[i.node] = i.message;
        canvas_.set_marked(marked);
        const Project& pr = app.project;
        if (canvas_.draw(g->uid, g->graph, [&](const GraphNode& n) { return event_summary(pr, n); })) app.touch();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##inspector", ImVec2(0, 0), ImGuiChildFlags_Borders);
        ImGui::PushItemWidth(-120);
        draw_inspector(app, *g, issues, data);
        ImGui::PopItemWidth();
        ImGui::EndChild();
        ImGui::EndGroup();
        ImGui::End();
    }

private:
    static EventGraphDef* find(Project& p, const std::string& uid) {
        for (auto& e : p.events)
            if (e.uid == uid) return &e;
        return nullptr;
    }

    void add_graph(App& app) {
        EventGraphDef g;
        g.uid = new_uid();
        for (int i = 1;; ++i) {
            const std::string name = "Event " + std::to_string(i);
            bool used = false;
            for (auto& e : app.project.events) used |= e.name == name;
            if (!used) {
                g.name = name;
                break;
            }
        }
        // the starter: on chapter 2 -> condition (intro_done = 0) -> set intro_done = 1 -> end
        GraphNode t = make_node(GraphKind::Event, "on_chapter", 0, 0);
        t.data["chapter"] = 2;
        GraphNode c = make_node(GraphKind::Event, "condition", 320, 0);
        c.data["tests"][0]["name"] = "intro_done";
        c.data["tests"][0]["value"] = 0;
        GraphNode s = make_node(GraphKind::Event, "set_variable", 680, 0);
        s.data["name"] = "intro_done";
        GraphNode e = make_node(GraphKind::Event, "end", 1000, 0);
        g.graph.nodes = {t, c, s, e};
        add_link(GraphKind::Event, g.graph, t.uid, "next", c.uid, "in");
        add_link(GraphKind::Event, g.graph, c.uid, "then", s.uid, "in");
        add_link(GraphKind::Event, g.graph, s.uid, "next", e.uid, "in");
        app.project.events.push_back(g);
        sel_ = g.uid;
        app.touch();
    }

    void draw_list(App& app) {
        if (ImGui::Button("+ New event graph")) add_graph(app);
        ImGui::Separator();
        std::string remove;
        for (auto& e : app.project.events) {
            ImGui::PushID(e.uid.c_str());
            std::string label = e.name.empty() ? "(unnamed)" : e.name;
            if (!e.enabled) label += "  (off)";
            if (ImGui::Selectable(label.c_str(), sel_ == e.uid)) sel_ = e.uid;
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Delete")) remove = e.uid;
                ImGui::EndPopup();
            }
            std::string triggers;
            for (auto& n : e.graph.nodes)
                if (const EvNodeSpec* s = event_spec(n.type); s && s->trigger) triggers += (triggers.empty() ? "" : ", ") + std::string(s->title);
            ImGui::TextDisabled("  %s", triggers.c_str());
            ImGui::PopID();
        }
        if (!remove.empty()) {
            auto& v = app.project.events;
            v.erase(std::remove_if(v.begin(), v.end(), [&](const EventGraphDef& x) { return x.uid == remove; }), v.end());
            app.touch();
        }
    }

    void draw_header(App& app, EventGraphDef& g) {
        bool ch = false;
        ImGui::SetNextItemWidth(220);
        ch |= ImGui::InputText("Name", &g.name);
        ImGui::SameLine();
        ch |= ImGui::Checkbox("Enabled", &g.enabled);
        ui::help("A disabled graph is exported but never starts (custom.lua can turn it on with require(\"modkit.events\").set_enabled(name, true)).");
        if (ch) app.touch();
    }

    static bool lang_json(const char* label, json& t, const std::vector<std::string>& langs) {
        Text tx;
        if (t.is_object())
            for (auto& [k, v] : t.items())
                if (v.is_string()) tx[k] = v.get<std::string>();
        if (!ui::lang_text(label, tx, langs, true)) return false;
        t = json::object();
        for (auto& [k, v] : tx) t[k] = v;
        return true;
    }

    // one field of a node or check; returns true when it changed
    bool draw_field(App& app, const EvFieldDef& f, json& d) {
        bool ch = false;
        const std::string key = f.key;
        switch (f.kind) {
            case EvField::Int: {
                int v = d.contains(key) && d[key].is_number() ? static_cast<int>(d[key].get<double>()) : (f.def.is_number() ? f.def.get<int>() : 0);
                if (ImGui::InputInt(f.label, &v)) {
                    d[key] = static_cast<int>(std::clamp<double>(v, f.min, f.max));
                    ch = true;
                }
                break;
            }
            case EvField::Number: {
                double v = d.contains(key) && d[key].is_number() ? d[key].get<double>() : (f.def.is_number() ? f.def.get<double>() : 0);
                if (ImGui::InputDouble(f.label, &v, 0, 0, "%.2f")) {
                    d[key] = std::isfinite(v) ? std::clamp(v, f.min, f.max) : 0.0;
                    ch = true;
                }
                break;
            }
            case EvField::Bool: {
                bool v = d.value(key, f.def.is_boolean() ? f.def.get<bool>() : false);
                if (ImGui::Checkbox(f.label, &v)) {
                    d[key] = v;
                    ch = true;
                }
                break;
            }
            case EvField::Combo: {
                std::string v = d.value(key, std::string());
                if (ui::combo(f.label, v, f.options, false)) {
                    d[key] = v;
                    ch = true;
                }
                break;
            }
            case EvField::Text:
                ch |= lang_json(f.label, d[key], app.project.mod.languages);
                break;
            case EvField::Item: {
                std::string v = d.value(key, std::string());
                if (ui::template_picker(f.label, v, app.game.get(), {TplKind::Item, TplKind::Recipe}, ui::project_item_names(app.project))) {
                    d[key] = v;
                    ch = true;
                }
                break;
            }
            case EvField::Quest: {
                std::string v = d.value(key, std::string());
                if (ui::quest_picker(f.label, v, app)) {
                    d[key] = v;
                    ch = true;
                }
                break;
            }
            case EvField::Target:
            case EvField::Template: {
                std::string v = d.value(key, std::string());
                std::vector<std::string> extra;
                if (f.kind == EvField::Target)
                    for (auto& x : app.project.npcs) extra.push_back(x.name);
                for (auto& x : app.project.npc_templates)
                    if (x.mode == Mode::New) extra.push_back(x.name);
                if (ui::template_picker(f.label, v, app.game.get(), {TplKind::Npc, TplKind::Monster}, extra)) {
                    d[key] = v;
                    ch = true;
                }
                if (f.kind == EvField::Target) ui::help("A template (every wolf) or the unique name of one NPC.");
                break;
            }
            case EvField::Info: {
                std::string v = d.value(key, std::string());
                bool c = ui::combo(f.label, v, project_info_options(app.project), false);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(120);
                c |= ImGui::InputText("##free", &v);
                if (c) {
                    d[key] = v;
                    ch = true;
                }
                break;
            }
            case EvField::String: {
                std::string v = d.value(key, std::string());
                const bool hint = f.help && f.kind == EvField::String && std::string(f.help).size() < 40;
                if (hint ? ImGui::InputTextWithHint(f.label, f.help, &v) : ImGui::InputText(f.label, &v)) {
                    d[key] = v;
                    ch = true;
                }
                break;
            }
        }
        if (f.help && f.kind != EvField::Target && !(f.kind == EvField::String && std::string(f.help).size() < 40)) ui::help(f.help);
        return ch;
    }

    static std::vector<std::string> project_info_options(const Project& p) {
        std::vector<std::string> v;
        for (auto& d : p.dialogs)
            for (auto& n : d.graph.nodes)
                if (n.type == "option") v.push_back(info_name(p, n));
        return v;
    }

    bool draw_fields(App& app, const std::vector<EvFieldDef>& fields, json& d) {
        bool ch = false;
        for (size_t i = 0; i < fields.size(); ++i) {
            if (!event_field_visible(fields[i], d)) continue;
            ImGui::PushID(static_cast<int>(i));
            ch |= draw_field(app, fields[i], d);
            ImGui::PopID();
        }
        return ch;
    }

    bool draw_tests(App& app, json& d) {
        bool ch = false;
        if (!d.contains("tests") || !d["tests"].is_array()) d["tests"] = json::array();
        json& tests = d["tests"];
        int remove = -1;
        for (size_t i = 0; i < tests.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            json& t = tests[i];
            const EvTestSpec* spec = event_test(t.value("kind", std::string()));
            ImGui::Separator();
            ImGui::TextUnformatted(spec ? spec->title : "?");
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20);
            if (ImGui::SmallButton("x")) remove = static_cast<int>(i);
            if (spec) ch |= draw_fields(app, spec->fields, t);
            ImGui::PopID();
        }
        if (remove >= 0) {
            tests.erase(tests.begin() + remove);
            ch = true;
        }
        ImGui::Separator();
        if (ImGui::BeginCombo("##add_check", "+ Add a check...")) {
            for (auto& s : event_tests())
                if (ImGui::Selectable(s.title)) {
                    tests.push_back(event_default_test(s.kind));
                    ch = true;
                }
            ImGui::EndCombo();
        }
        return ch;
    }

    void draw_inspector(App& app, EventGraphDef& g, const std::vector<DialogIssue>& issues, const json& data) {
        GraphNode* n = g.graph.find(canvas_.selected());
        if (!n) {
            ImGui::TextWrapped("Select a node to edit it. Right-click the canvas to add nodes; drag from an output pin into empty space to add a linked node.");
            ImGui::Spacing();
            ImGui::TextWrapped("A graph starts at a trigger (green) and runs along its links. A Condition goes on at 'then' or 'else'. An output linked to several nodes runs "
                               "them all, top to bottom. Once, After N passes and Cooldown keep their state in the savegame; a Delay too. Variables are game variables "
                               "(dialogues and quests can read them) or mod variables kept in the savegame.");
            ImGui::SeparatorText("Notes");
            ImGui::InputTextMultiline("##notes", &g.notes, ImVec2(-FLT_MIN, 60));
            ImGui::SeparatorText("Exported as");
            std::string lua = lua_value(data, 0, 60);
            ImGui::InputTextMultiline("##lua", &lua, ImVec2(-FLT_MIN, 200), ImGuiInputTextFlags_ReadOnly);
            draw_issues(issues, "");
            return;
        }
        const EvNodeSpec* spec = event_spec(n->type);
        ImGui::SeparatorText(spec ? spec->title : n->type.c_str());
        if (spec && spec->help[0]) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", spec->help);
            ImGui::PopStyleColor();
        }
        bool ch = false;
        if (spec) ch |= draw_fields(app, spec->fields, n->data);
        if (n->type == "condition") ch |= draw_tests(app, n->data);
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
    NodeCanvas canvas_{GraphKind::Event};
};

}  // namespace

std::unique_ptr<Panel> make_events_panel() { return std::make_unique<EventsPanel>(); }

}  // namespace mk
