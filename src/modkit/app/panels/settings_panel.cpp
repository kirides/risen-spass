// Settings panel: the game data directory, the export directory, the UI scale (stored per user, core/settings.h).
#include <imgui.h>
#include <imgui_stdlib.h>

#include <future>

#include "app/panels/panels.h"
#include "app/widgets.h"
#include "core/audio_convert.h"
#include "core/play.h"
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
        if (ImGui::Button(app.game ? "Reload game data" : "Load game data")) {
            s.save();
            app.load_game_data();
        }
        std::error_code ec;
        if (!s.data_dir.empty()) {
            ImGui::SameLine();
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
        ui::section("Play");
        ImGui::TextWrapped("The Risen folder (with bin\\Risen.exe and the mod layer) that Play starts. Play exports the mod into its bin\\spass\\mods and starts the game, "
                           "or, when the game runs with hot reload, lets it reload the mod. The game's errors for this mod appear in the Problems panel.");
        ImGui::SetNextItemWidth(-90);
        changed |= ImGui::InputText("##game", &s.game_dir);
        ImGui::SameLine();
        if (ImGui::Button("Browse##game")) {
            const std::string d = dialog_pick_folder(s.game_dir);
            if (!d.empty()) {
                s.game_dir = d;
                changed = true;
            }
        }
        if (!s.game_dir.empty()) {
            const PlayPlan plan = play_plan(s.game_dir);
            if (plan.problem.empty()) ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1), "Risen.exe found");
            else ImGui::TextColored(ImVec4(1, 0.6f, 0.4f, 1), "%s", plan.problem.c_str());
        }
        const char* starts[] = {"The main menu", "A new game", "The last quick save"};
        ImGui::SetNextItemWidth(220);
        changed |= ImGui::Combo("Start with", &s.play_start, starts, 3);
        changed |= ImGui::Checkbox("Windowed (sets FullScreen=\"false\" in data\\ini\\ConfigLocal.xml, a backup is kept)", &s.play_windowed);
        ui::section("Audio");
        ImGui::TextWrapped("ffmpeg converts voice recordings the game cannot play (FLAC, Opus, M4A, AIFF, ...) to Ogg Vorbis (Strings > Import recordings). "
                           "Leave the path empty to use the ffmpeg on PATH. It is not bundled.");
        ImGui::SetNextItemWidth(-200);
        changed |= ImGui::InputTextWithHint("##ffmpeg", "ffmpeg on PATH", &s.ffmpeg);
        ImGui::SameLine();
        if (ImGui::Button("Browse##ffmpeg")) {
            const std::string f = dialog_open_file("ffmpeg", nullptr, "");
            if (!f.empty()) {
                s.ffmpeg = f;
                changed = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Test") && !probe_.valid()) {
            const std::string configured = s.ffmpeg;
            probe_ = std::async(std::launch::async, [configured] {
                std::string err;
                FfmpegInfo f = probe_ffmpeg(locate_ffmpeg(configured, &err), run_process);
                if (!err.empty()) f.error = err;
                return f;
            });
        }
        if (probe_.valid() && probe_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            const FfmpegInfo f = probe_.get();
            probe_text_ = f.ok ? "ffmpeg " + f.version + " at " + f.path + (f.libvorbis ? " (libvorbis: Ogg Vorbis)" : " (no libvorbis: conversions write WAV)")
                               : f.error + ". " + ffmpeg_help();
            probe_ok_ = f.ok;
        }
        if (!probe_text_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, probe_ok_ ? ImVec4(0.5f, 0.9f, 0.5f, 1) : ImVec4(1, 0.6f, 0.4f, 1));
            ImGui::TextWrapped("%s", probe_text_.c_str());
            ImGui::PopStyleColor();
        }
        if (ImGui::Checkbox("Convert to mono 44.1 kHz (like the game's speech)", &s.voice_mono)) {
            s.voice_rate = s.voice_mono ? 44100 : 0;
            changed = true;
        }
        changed |= ImGui::Checkbox("Normalise the loudness of converted files (EBU R128, -16 LUFS)", &s.voice_normalize);
        ui::section("Interface");
        changed |= ImGui::SliderFloat("UI scale", &s.ui_scale, 0.6f, 2.5f, "%.2f");
        if (changed && !ImGui::IsAnyItemActive()) s.save();
        if (ImGui::IsItemDeactivatedAfterEdit()) s.save();
        ImGui::End();
    }

private:
    std::future<FfmpegInfo> probe_;
    std::string probe_text_;
    bool probe_ok_ = false;
};

}  // namespace

std::unique_ptr<Panel> make_settings_panel() { return std::make_unique<SettingsPanel>(); }

}  // namespace mk
