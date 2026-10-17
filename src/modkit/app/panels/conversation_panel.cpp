// Conversation panel (DLG-5): plays a dialogue of the project like a player, inside the editor. Options, the lines the NPC and the player say, and a
// pretend world whose facts (quest states, items, variables, ...) can be changed at any moment to see which options appear. The rules are the
// layer's (core/dialog_sim.h); nothing is sent to the game.
#include <imgui.h>

#include <algorithm>
#include <memory>

#include "app/panels/panels.h"
#include "core/catalog.h"
#include "core/dialog.h"
#include "core/dialog_sim.h"
#include "core/graph.h"

namespace mk {

namespace {

const ImVec4 kPlayerCol(0.55f, 0.8f, 1.0f, 1), kNpcCol(0.95f, 0.95f, 0.95f, 1), kOtherCol(1.0f, 0.85f, 0.5f, 1), kFxCol(0.6f, 0.65f, 0.6f, 1);

class ConversationPanel : public Panel {
public:
    ConversationPanel() { open = false; }
    const char* title() const override { return kPanelConversation; }

    bool select(App& app, const std::string& key) override {
        for (auto& d : app.project.dialogs)
            if (d.uid == key || d.name == key) {
                sel_ = d.uid;
                sim_.reset();
                app.focus_panel(title());
                return true;
            }
        return false;
    }
    void on_project_changed(App&) override { sim_.reset(); }

    void draw(App& app) override {
        if (!ImGui::Begin(title(), &open)) {
            ImGui::End();
            return;
        }
        const Project& p = app.project;
        if (p.dialogs.empty()) {
            ImGui::TextDisabled("There is no dialogue in the project yet (Dialogues panel).");
            ImGui::End();
            return;
        }
        const DialogDef* d = nullptr;
        for (auto& x : p.dialogs)
            if (x.uid == sel_) d = &x;
        if (!d) {
            d = &p.dialogs.front();
            sel_ = d->uid;
        }
        header(app, *d);
        refresh(app, *d);
        if (!sim_) {
            ImGui::End();
            return;
        }
        const float side_w = std::clamp(ImGui::GetContentRegionAvail().x * 0.4f, 260.0f, 520.0f);
        ImGui::BeginChild("##talk", ImVec2(ImGui::GetContentRegionAvail().x - side_w - 8, 0), ImGuiChildFlags_Borders);
        talk(app);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##world", ImVec2(0, 0), ImGuiChildFlags_Borders);
        world_editor(app);
        ImGui::EndChild();
        ImGui::End();
    }

private:
    void header(App& app, const DialogDef& d) {
        ImGui::SetNextItemWidth(220);
        if (ImGui::BeginCombo("##dlg", d.name.c_str())) {
            for (auto& x : app.project.dialogs)
                if (ImGui::Selectable((x.name + "##" + x.uid).c_str(), x.uid == sel_)) {
                    sel_ = x.uid;
                    sim_.reset();
                }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("owner: %s", d.owner.empty() ? "(none)" : d.owner.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Talk again")) {
            if (sim_) sim_->restart();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Start a new conversation. What was used up (options that disappear after use) and the world stay.");
        ImGui::SameLine();
        if (ImGui::Button("New game")) {
            if (sim_) sim_->reset_all();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Everything back to the start: nothing used up, no quests, no items.");
        ImGui::SameLine();
        if (ImGui::Button("Edit")) app.select_in(kPanelDialogs, d.uid);
    }

    // the compiled dialogue changed (an edit in the Dialogues panel): rebuild, keeping the pretend world and what was used up
    void refresh(App& app, const DialogDef& d) {
        std::vector<DialogIssue> issues;
        json actions = json::array();
        json infos = compile_dialog(app.project, d, issues, &actions);
        const std::string sig = infos.dump() + actions.dump() + std::to_string(app.project.mod.languages.size());
        if (sim_ && sig == sig_) return;
        sig_ = sig;
        auto fresh = std::make_unique<DialogSim>(app.project, infos, actions);
        if (sim_) {
            fresh->world = sim_->world;
            fresh->keep_used(sim_->used());
            fresh->restart();
        }
        sim_ = std::move(fresh);
        errors_ = 0;
        for (auto& i : issues) errors_ += i.error;
    }

    void talk(App& app) {
        if (errors_) ImGui::TextColored(ImVec4(1, 0.8f, 0.35f, 1), "The dialogue has %d errors (Dialogues panel): the export would be refused.", errors_);
        ImGui::BeginChild("##lines", ImVec2(0, -std::max(120.0f, ImGui::GetContentRegionAvail().y * 0.35f)));
        for (auto& l : sim_->transcript()) {
            switch (l.kind) {
                case SimLine::Kind::Player:
                    ImGui::PushStyleColor(ImGuiCol_Text, kPlayerCol);
                    ImGui::TextWrapped("You: %s", l.text.c_str());
                    break;
                case SimLine::Kind::Npc:
                    ImGui::PushStyleColor(ImGuiCol_Text, kNpcCol);
                    ImGui::TextWrapped("%s", l.text.c_str());
                    break;
                case SimLine::Kind::Other:
                    ImGui::PushStyleColor(ImGuiCol_Text, kOtherCol);
                    ImGui::TextWrapped("%s: %s", l.who.c_str(), l.text.c_str());
                    break;
                case SimLine::Kind::End:
                    ImGui::PushStyleColor(ImGuiCol_Text, kFxCol);
                    ImGui::TextWrapped("  %s", l.text.c_str());
                    break;
                default:
                    ImGui::PushStyleColor(ImGuiCol_Text, kFxCol);
                    ImGui::TextWrapped("  [%s]", l.text.c_str());
                    break;
            }
            ImGui::PopStyleColor();
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4 || follow_) ImGui::SetScrollHereY(1.0f);
        follow_ = false;
        ImGui::EndChild();
        ImGui::Separator();
        if (sim_->ended()) {
            ImGui::TextDisabled("The conversation is over.");
            if (ImGui::Button("Talk again##end")) sim_->restart();
            return;
        }
        const auto opts = sim_->options();
        if (opts.empty()) ImGui::TextDisabled("No option is on offer (every condition is 'no' or all options are used up).");
        int n = 0;
        std::string chosen;
        for (auto& o : opts) {
            ImGui::PushID(o.info.c_str());
            std::string label = std::to_string(++n) + ". " + (o.text.empty() ? "(no text)" : o.text);
            if (o.type != "Info") label += "  [" + o.type + "]";
            if (ImGui::Button(label.c_str(), ImVec2(-FLT_MIN, 0))) chosen = o.info;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s%s", o.info.c_str(), o.permanent ? " (permanent)" : "");
            ImGui::PopID();
        }
        if (ImGui::Button("Leave", ImVec2(-FLT_MIN, 0))) {
            sim_->leave();
            follow_ = true;
        }
        if (!chosen.empty()) {
            sim_->choose(chosen);
            follow_ = true;
        }
        (void)app;
    }

    // the facts the dialogue's conditions ask about, each with a control, and a switch that pins the condition's answer
    void world_editor(App& app) {
        SimWorld& w = sim_->world;
        const auto& leaves = sim_->leaves();
        ImGui::SeparatorText("Conditions of this dialogue");
        if (leaves.empty()) ImGui::TextDisabled("No option has a condition.");
        ImGui::TextDisabled("Change the world, or pin a condition to yes / no; the options update at once.");
        int i = 0;
        for (auto& leaf : leaves) {
            ImGui::PushID(i++);
            const std::string key = leaf_key(leaf);
            const std::string kind = leaf[0].get<std::string>();
            const bool value = eval_condition(leaf, w);
            ImGui::TextColored(value ? ImVec4(0.5f, 0.9f, 0.5f, 1) : ImVec4(0.9f, 0.5f, 0.45f, 1), "%s", value ? "yes" : "no ");
            ImGui::SameLine();
            ImGui::TextWrapped("%s", key.c_str());
            auto str = [&](size_t k) { return k < leaf.size() && leaf[k].is_string() ? leaf[k].get<std::string>() : std::string(); };
            ImGui::Indent(18);
            ImGui::SetNextItemWidth(130);
            if (kind == "quest") {
                const std::string q = str(1);
                const char* states[] = {"(not started)", "Running", "Success", "Failed", "Cancelled"};
                int cur = 0;
                auto it = w.quests.find(q);
                if (it != w.quests.end())
                    for (int s = 1; s < 5; ++s)
                        if (it->second == states[s]) cur = s;
                if (ImGui::Combo("quest state", &cur, states, 5)) {
                    if (cur == 0) w.quests.erase(q);
                    else w.quests[q] = states[cur];
                }
            } else if (kind == "item") {
                int c = w.items.count(str(1)) ? w.items[str(1)] : 0;
                if (ImGui::InputInt("carried", &c)) w.items[str(1)] = std::max(0, c);
            } else if (kind == "var") {
                const std::string n = str(1);
                bool ex = w.vars.count(n) > 0;
                if (ImGui::Checkbox("exists", &ex)) {
                    if (ex) w.vars[n] = 0;
                    else w.vars.erase(n);
                }
                if (ex) {
                    ImGui::SameLine();
                    double v = w.vars[n];
                    ImGui::SetNextItemWidth(100);
                    if (ImGui::InputDouble("value", &v, 1, 10, "%.0f")) w.vars[n] = v;
                }
            } else if (kind == "event") {
                bool ev = w.events.count(str(1)) > 0;
                if (ImGui::Checkbox("happened", &ev)) {
                    if (ev) w.events.insert(str(1));
                    else w.events.erase(str(1));
                }
            } else if (kind == "skill") {
                int s = w.skills.count(str(1)) ? w.skills[str(1)] : 0;
                if (ImGui::InputInt("level", &s)) w.skills[str(1)] = std::max(0, s);
            } else if (kind == "guild") {
                bool g = w.guilds.count(str(1)) > 0;
                if (ImGui::Checkbox("member", &g)) {
                    if (g) w.guilds.insert(str(1));
                    else w.guilds.erase(str(1));
                }
            } else if (kind == "dead" || kind == "alive") {
                bool dead = w.dead.count(str(1)) > 0;
                if (ImGui::Checkbox("is dead", &dead)) {
                    if (dead) w.dead.insert(str(1));
                    else w.dead.erase(str(1));
                }
            }
            // pin
            int pin = 0;
            if (auto f = w.forced.find(key); f != w.forced.end()) pin = f->second ? 1 : 2;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90);
            const char* pins[] = {"as world", "pin yes", "pin no"};
            if (ImGui::Combo("##pin", &pin, pins, 3)) {
                if (pin == 0) w.forced.erase(key);
                else w.forced[key] = pin == 1;
            }
            ImGui::Unindent(18);
            ImGui::PopID();
        }
        ImGui::SeparatorText("Quests now");
        if (w.quests.empty()) ImGui::TextDisabled("none started");
        for (auto& [q, s] : w.quests) ImGui::BulletText("%s: %s", q.c_str(), s.c_str());
        const auto un = sim_->unreachable();
        if (!un.empty()) {
            ImGui::SeparatorText("Never reachable");
            for (auto& u : un) ImGui::BulletText("%s (its parent is no option of this dialogue)", u.c_str());
        }
        (void)app;
    }

    std::string sel_, sig_;
    std::unique_ptr<DialogSim> sim_;
    int errors_ = 0;
    bool follow_ = false;
};

}  // namespace

std::unique_ptr<Panel> make_conversation_panel() { return std::make_unique<ConversationPanel>(); }

}  // namespace mk
