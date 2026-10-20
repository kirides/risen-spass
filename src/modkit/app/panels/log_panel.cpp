// Log panel: everything the editor did, parse problems of the game data, export results.
#include <imgui.h>

#include <vector>

#include "app/panels/panels.h"
#include "app/widgets.h"
#include "core/i18n.h"
#include "core/log.h"

namespace mk {

namespace {

class LogPanel : public Panel {
public:
    const char* title() const override { return kPanelLog; }
    void draw(App&) override {
        if (!ImGui::Begin(ui::panel_label(title()).c_str(), &open)) {
            ImGui::End();
            return;
        }
        next_ = Log::get().copy_since(next_, lines_);
        if (lines_.size() > 20000) lines_.erase(lines_.begin(), lines_.begin() + static_cast<std::ptrdiff_t>(lines_.size() - 20000));
        if (ImGui::Button(tr("Clear"))) lines_.clear();
        ImGui::SameLine();
        ImGui::Checkbox(tr("Debug"), &show_debug_);
        ImGui::SameLine();
        filter_.Draw("Filter", 220);
        ImGui::Separator();
        ImGui::BeginChild("##lines", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
        const bool at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2;
        for (auto& l : lines_) {
            if (l.level == Level::Debug && !show_debug_) continue;
            if (!filter_.PassFilter(l.text.c_str())) continue;
            ImVec4 col = l.level == Level::Error ? ImVec4(1.0f, 0.45f, 0.4f, 1) : l.level == Level::Warn ? ImVec4(1.0f, 0.8f, 0.35f, 1)
                         : l.level == Level::Debug ? ImVec4(0.6f, 0.6f, 0.6f, 1) : ImVec4(0.9f, 0.9f, 0.9f, 1);
            ImGui::TextDisabled("%8.2f", l.time);
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextUnformatted(l.text.c_str());
            ImGui::PopStyleColor();
        }
        if (at_bottom) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
        ImGui::End();
    }

private:
    std::vector<LogLine> lines_;
    size_t next_ = 0;
    bool show_debug_ = false;
    ImGuiTextFilter filter_;
};

}  // namespace

std::unique_ptr<Panel> make_log_panel() { return std::make_unique<LogPanel>(); }

}  // namespace mk
