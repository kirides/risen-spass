// Settings panel: the game data directory, the export directory, the UI scale (stored per user, core/settings.h).
#include <imgui.h>
#include <imgui_stdlib.h>

#include "app/panels/panels.h"
#include "app/widgets.h"
#include "core/paths.h"

namespace mk {

namespace {

class SettingsPanel : public Panel {
public:
    SettingsPanel() { open = false; }
    const char* title() const override { return kPanelSettings; }
    void draw(App& app) override {
        if (!ImGui::Begin(title(), &open)) {
            ImGui::End();
            return;
        }
        Settings& s = app.settings;
        ui::section("Game data");
        ImGui::TextWrapped("The game's data directory: <Risen>\\data, the folder with common\\*.pak and compiled\\*.pak. It is only read.");
        ImGui::SetNextItemWidth(-90);
        bool changed = ImGui::InputText("##data", &s.data_dir);
        ImGui::SameLine();
        if (ImGui::Button("Browse##data")) {
            const std::string d = dialog_pick_folder(s.data_dir);
            if (!d.empty()) {
                s.data_dir = d;
                changed = true;
            }
        }
        std::error_code ec;
        if (!s.data_dir.empty()) {
            const bool ok = fs::exists(u8(s.data_dir) / "common", ec) && fs::exists(u8(s.data_dir) / "compiled", ec);
            if (ok) ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1), "common and compiled found");
            else ImGui::TextColored(ImVec4(1, 0.6f, 0.4f, 1), "this folder has no common / compiled sub-folders");
        }
        ui::section("Export");
        ImGui::TextWrapped("Default folder for exported mods, usually <Risen>\\bin\\spass\\mods.");
        ImGui::SetNextItemWidth(-90);
        changed |= ImGui::InputText("##export", &s.export_dir);
        ImGui::SameLine();
        if (ImGui::Button("Browse##export")) {
            const std::string d = dialog_pick_folder(s.export_dir);
            if (!d.empty()) {
                s.export_dir = d;
                changed = true;
            }
        }
        ui::section("Interface");
        changed |= ImGui::SliderFloat("UI scale", &s.ui_scale, 0.6f, 2.5f, "%.2f");
        if (changed && !ImGui::IsAnyItemActive()) s.save();
        if (ImGui::IsItemDeactivatedAfterEdit()) s.save();
        ImGui::End();
    }
};

}  // namespace

std::unique_ptr<Panel> make_settings_panel() { return std::make_unique<SettingsPanel>(); }

}  // namespace mk
