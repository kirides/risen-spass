// Lua panel: the project's hand-written Lua files (core/lua_files.h). Each is a file next to the project that the export copies into lua/ of the mod;
// main.lua loads them in this order (protected) before the content is installed and calls their hooks before_install(data) / after_install(runtime).
// The panel creates one from a commented template, opens it in the system's editor and shows what the editor read from it (syntax, hooks, scripts).
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>

#include "app/editor_util.h"
#include "app/panels/panels.h"
#include "app/widgets.h"
#include "core/i18n.h"
#include "core/lua_files.h"
#include "core/paths.h"

namespace mk {

namespace {

class LuaPanel : public Panel {
public:
    const char* title() const override { return kPanelLua; }

    void on_project_changed(App&) override {
        sel_.clear();
        edit_uid_.clear();
    }
    bool select(App& app, const std::string& key) override {
        for (auto& l : app.project.lua_files)
            if (l.uid == key || l.name == key) {
                sel_ = l.uid;
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
        if (ImGui::Button(tr("+ New Lua file"))) app.defer([this, &app] { create(app); });
        ui::help(tr("Creates a Lua file next to the project (lua/<name>.lua) from a commented template and opens it in your editor. The export copies it "
                 "into the mod; the mod loads it before the editor's content and calls its hooks. An error in it is logged and the rest of the mod keeps working."));
        ImGui::SameLine();
        if (ImGui::Button(tr("Add a file..."))) {
            const std::string dir = app.project_path.empty() ? std::string() : u8str(u8(app.project_path).parent_path());
            for (auto& f : dialog_open_files("Lua scripts", "lua", dir)) add(app, f);
        }
        if (app.project_path.empty())
            ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "%s", tr("Save the project first: new files are written next to the project file."));

        const float list_w = 240;
        ImGui::BeginChild("##list", ImVec2(list_w, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
        std::string remove;
        int move = 0;  // -1 up, +1 down (the selected one)
        for (auto& l : p.lua_files) {
            ImGui::PushID(l.uid.c_str());
            const LuaFileInfo info = lua_file_info(app.project_file(l.file));
            if (!info.ok) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.5f, 0.4f, 1));
            if (ImGui::Selectable(l.name.empty() ? tr("(unnamed)") : l.name.c_str(), sel_ == l.uid)) sel_ = l.uid;
            if (!info.ok) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered() && !info.ok) ImGui::SetTooltip("%s", info.error.c_str());
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem(tr("Remove from the project (the file stays)"))) remove = l.uid;
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        if (p.lua_files.empty()) ImGui::TextDisabled("%s", tr("No Lua files yet."));
        else {
            ImGui::Separator();
            ImGui::BeginDisabled(sel_.empty());
            if (ImGui::ArrowButton("##up", ImGuiDir_Up)) move = -1;
            ImGui::SameLine();
            if (ImGui::ArrowButton("##down", ImGuiDir_Down)) move = 1;
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("%s", tr("load order"));
        }
        ImGui::EndChild();
        if (!remove.empty()) {
            p.lua_files.erase(std::remove_if(p.lua_files.begin(), p.lua_files.end(), [&](const LuaFile& l) { return l.uid == remove; }), p.lua_files.end());
            sel_.clear();
            app.touch();
        }
        if (move) {
            auto it = std::find_if(p.lua_files.begin(), p.lua_files.end(), [&](const LuaFile& l) { return l.uid == sel_; });
            if (it != p.lua_files.end()) {
                const auto i = it - p.lua_files.begin();
                const auto j = i + move;
                if (j >= 0 && j < static_cast<decltype(j)>(p.lua_files.size())) {
                    std::swap(p.lua_files[i], p.lua_files[j]);
                    app.touch();
                }
            }
        }
        ImGui::SameLine();
        ImGui::BeginChild("##detail");
        LuaFile* l = nullptr;
        for (auto& x : p.lua_files)
            if (x.uid == sel_) l = &x;
        if (l) draw_detail(app, *l);
        else draw_overview(app);
        ImGui::EndChild();
        ImGui::End();
    }

private:
    void create(App& app) {
        if (app.project_path.empty()) {
            app.status(tr("Save the project first: the file is written next to the project file"));
            return;
        }
        std::string name;
        for (int i = 1;; ++i) {
            name = "extension_" + std::to_string(i);
            const bool taken = std::any_of(app.project.lua_files.begin(), app.project.lua_files.end(), [&](const LuaFile& l) { return l.name == name; });
            if (!taken && !fs::exists(u8(app.project_file("lua/" + name + ".lua")))) break;
        }
        const std::string rel = "lua/" + name + ".lua";
        try {
            write_text(u8(app.project_file(rel)), lua_file_template(name, app.project.mod.id));
        } catch (const std::exception& e) {
            app.status(trf("Cannot write the file: {0}", {e.what()}));
            return;
        }
        LuaFile f{new_uid(), name, rel};
        app.project.lua_files.push_back(f);
        sel_ = f.uid;
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
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) c = '_';
        if (!name.empty() && std::isdigit(static_cast<unsigned char>(name[0]))) name = "_" + name;
        if (name == "custom") name = "custom_lua";
        LuaFile l{new_uid(), name, stored};
        app.project.lua_files.push_back(l);
        sel_ = l.uid;
        app.touch();
    }

    void draw_overview(App& app) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(tr("Hand-written Lua for what the editor does not make. The exported main.lua runs, in this order:"));
        ImGui::BulletText("%s", tr("the Lua files of this list, top to bottom (their top-level code runs before the content exists)"));
        ImGui::BulletText("%s", tr("before_install(data) of every file that returns one: the generated tables (data.balance, data.npcs, data.quests, ...) can still be changed"));
        ImGui::BulletText("%s", tr("the editor's content is installed"));
        ImGui::BulletText("%s", tr("after_install(runtime) of every file that returns one"));
        ImGui::BulletText("%s", tr("custom.lua of the mod folder (created once by the export, never overwritten, not part of the project)"));
        ImGui::Spacing();
        ImGui::TextDisabled("%s", trf("Every file is loaded protected: an error is logged as [{0}] (Problems panel, From the game) and the rest of the mod keeps working.",
                                      {app.project.mod.id}).c_str());
        ImGui::TextDisabled("%s", tr("Run script nodes (Events) and dialogue Script commands call scripts that a Lua file defines with risen.script.define."));
        ImGui::PopTextWrapPos();
    }

    void draw_detail(App& app, LuaFile& l) {
        bool ch = false;
        if (edit_uid_ != l.uid) {
            edit_uid_ = l.uid;
            edit_name_ = l.name;
        }
        ImGui::SetNextItemWidth(260);
        ImGui::InputText(tr("Name"), &edit_name_);
        if ((ImGui::IsItemDeactivatedAfterEdit() || (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Enter))) && edit_name_ != l.name) {
            l.name = edit_name_;
            ch = true;
        }
        ui::help(tr("The module name: exported as lua/<name>.lua and loaded with require(\"lua.<name>\")."));
        if (const std::string why = lua_file_name_problem(l.name); !why.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", why.c_str());
        const std::string path = app.project_file(l.file);
        ImGui::TextDisabled("%s", l.file.c_str());
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
                l.file = stored;
                ch = true;
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", tr("(changes on disk are read again at once)"));

        const LuaFileInfo info = lua_file_info(path);
        ui::section(tr("What the editor read from it"));
        ImGui::PushTextWrapPos(0);
        if (!info.ok) {
            ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", info.error.c_str());
            ImGui::TextDisabled("%s", tr("The export refuses a file that does not compile."));
        } else {
            ImGui::TextUnformatted(tr("compiles"));
            std::string hs;
            for (auto& h : info.hooks) hs += (hs.empty() ? "" : ", ") + h;
            ImGui::TextDisabled(tr("hooks: %s"), hs.empty() ? tr("none") : hs.c_str());
            std::string ss;
            for (auto& s : info.scripts) ss += (ss.empty() ? "" : ", ") + s;
            ImGui::TextDisabled(tr("scripts it defines: %s"), ss.empty() ? tr("none") : ss.c_str());
            ImGui::TextDisabled("%s", tr("(read from the text; the file is not run by the editor)"));
        }
        ImGui::PopTextWrapPos();
        if (ch) app.touch();
    }

    std::string sel_, edit_uid_, edit_name_;
};

}  // namespace

std::unique_ptr<Panel> make_lua_panel() { return std::make_unique<LuaPanel>(); }

}  // namespace mk
