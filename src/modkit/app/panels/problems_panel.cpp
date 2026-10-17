// Problems panel: the result of validate() (refreshed twice a second); a click selects the object in its panel.
#include <imgui.h>

#include <algorithm>

#include "app/panels/panels.h"
#include "core/validate.h"

namespace mk {

namespace {

class ProblemsPanel : public Panel {
public:
    const char* title() const override { return kPanelProblems; }
    void draw(App& app) override {
        const auto& problems = app.problems();
        const size_t errors = error_count(problems);
        char t[96];
        std::snprintf(t, sizeof t, "Problems (%zu)###%s", problems.size(), kPanelProblems);
        if (!ImGui::Begin(t, &open)) {
            ImGui::End();
            return;
        }
        const auto& game = app.game_problems();
        if (problems.empty()) {
            ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1), app.game ? "No problems." : "No problems (without the game data only the project itself is checked).");
        } else {
            ImGui::Text("%zu errors, %zu warnings%s", errors, problems.size() - errors, errors ? ": the export is refused while errors exist" : "");
            table("##problems", app, problems, game.empty() ? 0.0f : std::min(ImGui::GetContentRegionAvail().y * 0.45f, (static_cast<float>(problems.size()) + 1.6f) * ImGui::GetFrameHeightWithSpacing()));
        }
        if (!game.empty()) {
            ImGui::SeparatorText("From the game (spass.log)");
            ImGui::TextDisabled("Errors and warnings the running game logged for this mod; a click selects the object they name. Game > Forget the game's errors clears the list.");
            table("##gameproblems", app, game, 0.0f);
        }
        ImGui::End();
    }

private:
    static void table(const char* id, App& app, const std::vector<Problem>& problems, float height) {
        if (ImGui::BeginTable(id, 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, ImVec2(0, height))) {
            ImGui::TableSetupColumn("Where", ImGuiTableColumnFlags_WidthStretch, 1);
            ImGui::TableSetupColumn("What", ImGuiTableColumnFlags_WidthStretch, 1);
            ImGui::TableSetupColumn("Problem", ImGuiTableColumnFlags_WidthStretch, 4);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            int i = 0;
            for (auto& p : problems) {
                ImGui::PushID(i++);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const ImVec4 col = p.severity == Severity::Error ? ImVec4(1, 0.45f, 0.4f, 1) : ImVec4(1, 0.8f, 0.35f, 1);
                ImGui::TextColored(col, "%s", p.area.c_str());
                ImGui::TableNextColumn();
                if (ImGui::Selectable(p.subject.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                    const std::string uid = p.uid, area = p.area;
                    app.defer([&app, uid, area] { app.select_in(area, uid); });
                }
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", p.message.c_str());
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
};

}  // namespace

std::unique_ptr<Panel> make_problems_panel() { return std::make_unique<ProblemsPanel>(); }

}  // namespace mk
