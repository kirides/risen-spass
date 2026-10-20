// Behaviours panel: the project's behaviour scripts (risen.behavior, like components in Unity / Unreal). Each is a Lua file next to the project that
// returns its definition; the panel creates one from a commented template, opens it in the system's editor, shows what the editor read from it
// (description, parameters with their defaults, handlers, errors) and where it is attached. A row is dragged onto an NPC's Behaviours section.
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>

#include "app/dragdrop.h"
#include "app/editor_util.h"
#include "app/panels/panels.h"
#include "app/widgets.h"
#include "core/behavior.h"
#include "core/i18n.h"
#include "core/paths.h"

namespace mk {

namespace {

class BehaviorsPanel : public Panel {
public:
    const char* title() const override { return kPanelBehaviors; }

    void on_project_changed(App&) override {
        sel_.clear();
        edit_uid_.clear();
    }
    bool select(App& app, const std::string& key) override {
        for (auto& b : app.project.behaviors)
            if (b.uid == key || b.name == key) {
                sel_ = b.uid;
                app.focus_panel(title());
                return true;
            }
        return false;
    }

    void draw(App& app) override {
        if (!ImGui::Begin(ui::panel_label(title()).c_str(), &open)) {
            ImGui::End();
            return;
        }
        Project& p = app.project;
        if (ImGui::Button(tr("+ New behaviour"))) app.defer([this, &app] { create(app); });
        ui::help(tr("Creates a Lua script next to the project (behaviors/<name>.lua) from a commented template and opens it in your editor. A behaviour "
                 "runs on every NPC it is attached to: start, update every few seconds, and handlers for death, damage, talk, items, any quest of "
                 "the game, the player coming near."));
        ImGui::SameLine();
        if (ImGui::Button(tr("Add a script..."))) {
            const std::string dir = app.project_path.empty() ? std::string() : u8str(u8(app.project_path).parent_path());
            for (auto& f : dialog_open_files("Lua scripts", "lua", dir)) add(app, f);
        }
        if (app.project_path.empty())
            ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "%s", tr("Save the project first: new scripts are written next to the project file."));

        const float list_w = 240;
        ImGui::BeginChild("##list", ImVec2(list_w, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
        std::string remove;
        for (auto& b : p.behaviors) {
            ImGui::PushID(b.uid.c_str());
            const BehaviorInfo info = behavior_info(app.project_file(b.file));
            if (!info.ok) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.5f, 0.4f, 1));
            if (ImGui::Selectable(b.name.empty() ? tr("(unnamed)") : b.name.c_str(), sel_ == b.uid)) sel_ = b.uid;
            if (!info.ok) ImGui::PopStyleColor();
            ui::drag_behavior_source(b.name);
            if (ImGui::IsItemHovered() && !ImGui::GetDragDropPayload())
                ImGui::SetTooltip("%s", info.ok ? (info.description.empty() ? tr("drag it onto an NPC") : info.description.c_str()) : info.error.c_str());
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem(tr("Remove from the project (the file stays)"))) remove = b.uid;
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        if (p.behaviors.empty()) ImGui::TextDisabled("%s", tr("No behaviours yet."));
        ImGui::EndChild();
        if (!remove.empty()) {
            p.behaviors.erase(std::remove_if(p.behaviors.begin(), p.behaviors.end(), [&](const BehaviorScript& b) { return b.uid == remove; }), p.behaviors.end());
            sel_.clear();
            app.touch();
        }
        ImGui::SameLine();
        ImGui::BeginChild("##detail");
        BehaviorScript* b = nullptr;
        for (auto& x : p.behaviors)
            if (x.uid == sel_) b = &x;
        if (b) draw_detail(app, *b);
        else ImGui::TextDisabled("%s", tr("Select a behaviour, or create one. Drag a behaviour onto a placed NPC or an NPC template (NPCs panel) to attach it."));
        ImGui::EndChild();
        ImGui::End();
    }

private:
    void create(App& app) {
        if (app.project_path.empty()) {
            app.status(tr("Save the project first: the script is written next to the project file"));
            return;
        }
        std::string name;
        for (int i = 1;; ++i) {
            name = "behavior_" + std::to_string(i);
            const bool taken = std::any_of(app.project.behaviors.begin(), app.project.behaviors.end(), [&](const BehaviorScript& b) { return b.name == name; });
            if (!taken && !fs::exists(u8(app.project_file("behaviors/" + name + ".lua")))) break;
        }
        const std::string rel = "behaviors/" + name + ".lua";
        try {
            write_text(u8(app.project_file(rel)), behavior_template(name));
        } catch (const std::exception& e) {
            app.status(trf("Cannot write the script: {0}", {e.what()}));
            return;
        }
        BehaviorScript s{new_uid(), name, rel};
        app.project.behaviors.push_back(s);
        sel_ = s.uid;
        app.touch();
        open_in_os(app.project_file(rel));
        app.status(trf("Created {0} and opened it in your editor", {rel}));
    }

    void add(App& app, const std::string& file) {
        const fs::path f = u8(file);
        std::string stored = u8str(f);
        if (!app.project_path.empty()) {
            const fs::path base = u8(app.project_path).parent_path();
            if (path_within(f, base)) stored = u8str(fs::relative(f, base));
        }
        std::replace(stored.begin(), stored.end(), '\\', '/');
        std::string name = u8str(f.stem());
        for (char& c : name)
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '-')) c = '_';
        BehaviorScript s{new_uid(), name, stored};
        app.project.behaviors.push_back(s);
        sel_ = s.uid;
        app.touch();
    }

    void draw_detail(App& app, BehaviorScript& b) {
        bool ch = false;
        // the name is edited in a buffer and taken on Enter / leaving the field: the attachments follow the new name
        if (edit_uid_ != b.uid) {
            edit_uid_ = b.uid;
            edit_name_ = b.name;
        }
        ImGui::SetNextItemWidth(260);
        ImGui::InputText(tr("Name"), &edit_name_);
        if ((ImGui::IsItemDeactivatedAfterEdit() || (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Enter))) && edit_name_ != b.name) {
            const std::string old_name = b.name;
            b.name = edit_name_;
            for (auto& n : app.project.npcs)
                for (auto& u : n.behaviors)
                    if (u.behavior == old_name) u.behavior = b.name;
            for (auto& t : app.project.npc_templates)
                for (auto& u : t.behaviors)
                    if (u.behavior == old_name) u.behavior = b.name;
            ch = true;
        }
        if (const std::string why = behavior_name_problem(b.name); !why.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", why.c_str());
        const std::string path = app.project_file(b.file);
        ImGui::TextDisabled("%s", b.file.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", path.c_str());
        if (ImGui::Button(tr("Open in your editor"))) {
            if (!open_in_os(path)) app.status(trf("Cannot open {0}", {path}));
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Change file..."))) {
            const std::string dir = app.project_path.empty() ? std::string() : u8str(u8(app.project_path).parent_path());
            const std::string f = dialog_open_file("Lua scripts", "lua", dir);
            if (!f.empty()) {
                std::string stored = f;
                if (!app.project_path.empty() && path_within(u8(f), u8(app.project_path).parent_path()))
                    stored = u8str(fs::relative(u8(f), u8(app.project_path).parent_path()));
                std::replace(stored.begin(), stored.end(), '\\', '/');
                b.file = stored;
                ch = true;
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", tr("(changes on disk are read again at once)"));

        const BehaviorInfo info = behavior_info(path);
        ui::section(tr("What the editor read from it"));
        if (!info.ok) {
            ImGui::PushTextWrapPos(0);
            ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", info.error.c_str());
            ImGui::PopTextWrapPos();
        } else {
            if (!info.description.empty()) ImGui::TextWrapped("%s", info.description.c_str());
            std::string hs;
            for (auto& h : info.handlers) hs += (hs.empty() ? "" : ", ") + h;
            for (auto& e : info.events) hs += (hs.empty() ? "" : ", ") + ("events[" + e + "]");
            ImGui::TextDisabled(tr("handlers: %s"), hs.empty() ? "none" : hs.c_str());
            if (info.interval >= 0) ImGui::TextDisabled(tr("update every %.2f s"), info.interval);
            if (info.near >= 0) ImGui::TextDisabled(tr("player near at %.0f units"), info.near);
            if (info.params.empty()) ImGui::TextDisabled("%s", tr("no parameters"));
            else if (ImGui::BeginTable("##params", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupColumn(tr("Parameter"));
                ImGui::TableSetupColumn(tr("Default"));
                ImGui::TableHeadersRow();
                for (auto& [k, v] : info.params.items()) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(k.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(v.dump().c_str());
                }
                ImGui::EndTable();
            }
        }
        ui::section(tr("Attached to"));
        int n = 0;
        auto row = [&](const std::string& who, const std::string& uid, const char* what) {
            ImGui::PushID(uid.c_str());
            if (ImGui::SmallButton(who.c_str())) {
                const std::string u = uid;
                app.defer([&app, u] { app.select_in(kPanelNpcs, u); });
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", what);
            ImGui::PopID();
            ++n;
        };
        for (auto& x : app.project.npcs)
            for (auto& u : x.behaviors)
                if (u.behavior == b.name) row(x.name, x.uid, tr("placed NPC"));
        for (auto& x : app.project.npc_templates)
            for (auto& u : x.behaviors)
                if (u.behavior == b.name) row(x.name, x.uid, tr("every NPC of this template"));
        if (!n) ImGui::TextDisabled("%s", tr("Nothing yet: drag the behaviour from the list onto an NPC's Behaviours section (NPCs panel)."));
        if (ch) app.touch();
    }

    std::string sel_, edit_uid_, edit_name_;
};

}  // namespace

std::unique_ptr<Panel> make_behaviors_panel() { return std::make_unique<BehaviorsPanel>(); }

}  // namespace mk
