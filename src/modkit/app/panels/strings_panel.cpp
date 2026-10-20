// "Strings": every text of the mod and of the game in one filterable table (core/voice.h), with the voice recording workflow on top: the export
// of the shown lines for a studio (CSV, JSON, one script per speaker) and the import of a folder of recordings onto them (matched by name or by the
// filled CSV, converted with ffmpeg where the game cannot play a file, docs/editor/USER_GUIDE.md "Voice recording"). Rows of the project are edited
// in place; game rows are read-only, "Override in the mod" makes a free string row of one (the old Strings panel's rows are the "string" rows here).
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <future>
#include <mutex>
#include <thread>

#include "app/panels/panels.h"
#include "app/pickers.h"
#include "app/widgets.h"
#include "core/audio_convert.h"
#include "core/i18n.h"
#include "core/paths.h"
#include "core/voice.h"
#include "pack2mod/util.h"

namespace mk {

namespace {

const ImVec4 kGreen(0.45f, 0.85f, 0.45f, 1), kRed(1.0f, 0.45f, 0.4f, 1), kGrey(0.6f, 0.6f, 0.6f, 1), kOrange(1, 0.7f, 0.3f, 1);

const char* panel_of(const std::string& kind) {
    if (kind == "dialogue") return kPanelDialogs;
    if (kind == "quest") return kPanelQuests;
    if (kind == "event") return kPanelEvents;
    if (kind == "item") return kPanelItems;
    if (kind == "recipe") return kPanelRecipes;
    if (kind == "spell") return kPanelSpells;
    if (kind == "npc_template" || kind == "npc") return kPanelNpcs;
    if (kind == "layer") return kPanelLayers;
    return nullptr;
}

// the background import: copies / converts on a worker thread, the panel applies the result to the project when it is done
struct ImportJob {
    std::thread thread;
    std::atomic<bool> cancel{false}, done{false};
    std::mutex mu;
    size_t index = 0, total = 0;
    std::string current;
    std::vector<VoiceImportStep> steps;
    std::vector<ImportedVoice> result;
    ~ImportJob() {
        cancel = true;
        if (thread.joinable()) thread.join();
    }
};

class StringsPanel : public Panel {
public:
    const char* title() const override { return kPanelStrings; }
    bool busy() const override { return game_future_.valid() || preview_future_.valid() || ff_future_.valid() || want_preview_ || (job_ && !job_->done); }
    void on_project_changed(App&) override {
        mod_rev_ = static_cast<size_t>(-1);
        sel_.clear();
    }
    bool select(App& app, const std::string& key) override {
        rebuild(app);
        for (size_t i = 0; i < mod_.size(); ++i)
            if (mod_[i].object_uid == key || mod_[i].id == key || mod_[i].node_uid == key || (mod_[i].kind == "string" && mod_[i].object == key)) {
                sel_ = "m:" + mod_[i].id;
                scroll_to_sel_ = true;
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
        rebuild(app);
        draw_toolbar(app);
        const float detail_h = std::max(150.0f, ImGui::GetContentRegionAvail().y * 0.36f);
        ImGui::BeginChild("##table", ImVec2(0, std::max(80.0f, ImGui::GetContentRegionAvail().y - detail_h)));
        draw_table(app);
        ImGui::EndChild();
        ImGui::BeginChild("##detail", ImVec2(0, 0), ImGuiChildFlags_Borders);
        draw_detail(app);
        ImGui::EndChild();
        draw_export_modal(app);
        draw_import_modal(app);
        ImGui::End();
    }

private:
    // ---- the index ---------------------------------------------------------------------------------------------------------------------------------
    void rebuild(App& app) {
        bool changed = false;
        if (app.history.revision() != mod_rev_ || app.game.get() != mod_game_) {
            mod_ = project_lines(app.project, app.game.get());
            mod_rev_ = app.history.revision();
            mod_game_ = app.game.get();
            changed = true;
        }
        // the game's rows are made once per game data, on a worker thread (tens of thousands of rows)
        if (app.game.get() != game_src_ && !game_future_.valid()) {
            game_src_ = app.game.get();
            game_.clear();
            if (app.game) {
                std::shared_ptr<const GameData> g = app.game;
                game_future_ = std::async(std::launch::async, [g] { return game_lines(*g); });
            }
            changed = true;
        }
        if (game_future_.valid() && game_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            game_ = game_future_.get();
            changed = true;
        }
        if (changed) {
            mark_overridden(game_, mod_);
            filter_dirty_ = true;
            languages_ = app.project.mod.languages;
            if (app.game)
                for (auto& l : app.game->languages())
                    if (std::find(languages_.begin(), languages_.end(), l) == languages_.end()) languages_.push_back(l);
            if (std::find(languages_.begin(), languages_.end(), lang_) == languages_.end())
                lang_ = app.project.mod.languages.empty() ? std::string("English") : app.project.mod.languages.front();
        }
        if (filter_dirty_) refilter(app);
    }

    const StringLine* row(size_t ix) const { return ix < mod_.size() ? &mod_[ix] : ix - mod_.size() < game_.size() ? &game_[ix - mod_.size()] : nullptr; }
    static std::string key_of(const StringLine& l) { return (l.game ? "g:" + l.kind + ":" : "m:") + l.id; }

    void refilter(App& app) {
        filter_dirty_ = false;
        shown_.clear();
        const std::string f = p2m::lower(filter_), sp = p2m::lower(speaker_), ctx = p2m::lower(context_);
        auto pass = [&](const StringLine& l) {
            if (source_ == 1 && l.game) return false;
            if (source_ == 2 && !l.game) return false;
            if (l.game && l.overridden && !show_overridden_) return false;
            if (spoken_only_ && !l.spoken) return false;
            if (!kind_.empty() && l.kind != kind_) return false;
            if (!f.empty() && l.search.find(f) == std::string::npos) return false;
            if (!sp.empty() && p2m::lower(l.speaker + " " + l.voice).find(sp) == std::string::npos) return false;
            if (!ctx.empty() && p2m::lower(l.object + " " + l.quest + " " + l.context).find(ctx) == std::string::npos) return false;
            if (missing_only_ && audio_status(app.project, l, lang_) != AudioStatus::Missing) return false;
            return true;
        };
        shown_spoken_ = shown_missing_ = 0;
        for (size_t i = 0; i < mod_.size() + game_.size(); ++i) {
            const StringLine& l = *row(i);
            if (!pass(l)) continue;
            shown_.push_back(i);
            if (!l.spoken) continue;
            ++shown_spoken_;
            if (audio_status(app.project, l, lang_) == AudioStatus::Missing) ++shown_missing_;
        }
        sel_cache_.clear();
        kinds_.clear();
        for (auto* v : {&mod_, &game_})
            for (auto& l : *v)
                if (std::find(kinds_.begin(), kinds_.end(), l.kind) == kinds_.end()) kinds_.push_back(l.kind);
    }

    // ---- toolbar -----------------------------------------------------------------------------------------------------------------------------------
    void draw_toolbar(App& app) {
        bool ch = false;
        ImGui::SetNextItemWidth(260);
        ch |= ImGui::InputTextWithHint("##filter", tr("filter: text, id, speaker ..."), &filter_);
        ImGui::SameLine();
        const char* sources[] = {tr("Mod and game"), tr("Mod"), tr("Game")};
        ImGui::SetNextItemWidth(140);
        ch |= ImGui::Combo("##source", &source_, sources, 3);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130);
        if (ImGui::BeginCombo("##kind", kind_.empty() ? tr("every kind") : kind_.c_str())) {
            if (ImGui::Selectable(tr("every kind"), kind_.empty())) {
                kind_.clear();
                ch = true;
            }
            for (auto& k : kinds_)
                if (ImGui::Selectable(k.c_str(), kind_ == k)) {
                    kind_ = k;
                    ch = true;
                }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        ch |= ImGui::InputTextWithHint("##speaker", tr("speaker / voice"), &speaker_);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150);
        ch |= ImGui::InputTextWithHint("##ctx", tr("dialogue / quest / info"), &context_);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        if (ImGui::BeginCombo("##lang", lang_.c_str())) {
            for (auto& l : languages_)
                if (ImGui::Selectable(l.c_str(), lang_ == l)) {
                    lang_ = l;
                    ch = true;
                }
            ImGui::EndCombo();
        }
        ui::help(tr("The language of the Text column and of the 'missing audio' filter."));
        ch |= ImGui::Checkbox(tr("Spoken"), &spoken_only_);
        ImGui::SameLine();
        ch |= ImGui::Checkbox(tr("Missing audio"), &missing_only_);
        ImGui::SameLine();
        ch |= ImGui::Checkbox(tr("Overridden"), &show_overridden_);
        ui::help(tr("Also list the game rows that a row of the mod replaces (they are hidden otherwise)."));
        if (ch) filter_dirty_ = true;
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (ImGui::Button(tr("+ String row"))) {
            StringRow s;
            s.uid = new_uid();
            s.id = "NEW_TEXT";
            app.project.strings.push_back(s);
            sel_ = "m:NEW_TEXT";
            scroll_to_sel_ = true;
            app.touch();
        }
        ui::help(tr("A text of the game's string tables (strings/*.csv of the exported mod) that no item, recipe or NPC of the project owns: a button label, a "
                 "document, a changed line. The id is not case sensitive."));
        ImGui::SameLine();
        if (ImGui::Button(tr("Export for voice recording..."))) {
            export_open_ = true;
            export_report_.clear();
            if (export_dir_.empty() && !app.project_path.empty()) export_dir_ = u8str(u8(app.project_path).parent_path() / "voice_export");
        }
        ui::help(tr("Writes the shown lines (the filter above) as voice_lines.csv (UTF-8, opens in Excel), voice_lines.json and one script per speaker and language."));
        ImGui::SameLine();
        if (ImGui::Button(tr("Import recordings..."))) {
            import_open_ = true;
            preview_.reset();
            start_ffmpeg_probe(app);
            // a folder of the project named like the usual ones is the default, with the export's CSV in it
            if (imp_folder_.empty() && !app.project_path.empty()) {
                std::error_code ec;
                for (const char* d : {"voice_recordings", "recordings"}) {
                    const fs::path f = u8(app.project_path).parent_path() / d;
                    if (!fs::is_directory(f, ec)) continue;
                    imp_folder_ = u8str(f);
                    if (fs::exists(f / "voice_lines.csv", ec)) imp_csv_ = u8str(f / "voice_lines.csv");
                    want_preview_ = true;
                    break;
                }
            }
        }
        ui::help(tr("Assigns a folder of recordings to the shown dialogue lines: by file name (<line id>_<Language>.ogg, as the export names them) or by the "
                 "'file' column of a filled CSV. Files the game cannot play are converted with ffmpeg."));
        ImGui::SameLine();
        ImGui::TextDisabled(tr("%zu of %zu rows  |  %zu spoken, %zu without %s audio%s"), shown_.size(), mod_.size() + game_.size(), shown_spoken_, shown_missing_, lang_.c_str(),
                            game_future_.valid() ? "  |  reading the game's strings..." : !app.game ? "  |  no game data (Settings)" : "");
    }

    // ---- table -------------------------------------------------------------------------------------------------------------------------------------
    void draw_table(App& app) {
        const auto& audio_langs = app.project.mod.languages;
        const int cols = 6 + static_cast<int>(audio_langs.size());
        if (!ImGui::BeginTable("##strings", cols, ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX |
                                                      ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit))
            return;
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn(tr("Id"), ImGuiTableColumnFlags_WidthFixed, 210);
        ImGui::TableSetupColumn(tr("Source"), ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableSetupColumn(tr("Speaker"), ImGuiTableColumnFlags_WidthFixed, 110);
        for (auto& l : audio_langs) ImGui::TableSetupColumn(trf("Audio {0}", {l}).c_str(), ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn(trf("Text ({0})", {lang_}).c_str(), ImGuiTableColumnFlags_WidthFixed, 420);
        ImGui::TableSetupColumn(tr("Stage direction"), ImGuiTableColumnFlags_WidthFixed, 150);
        ImGui::TableSetupColumn(tr("Context"), ImGuiTableColumnFlags_WidthFixed, 300);
        ImGui::TableHeadersRow();
        ImGuiListClipper clip;
        clip.Begin(static_cast<int>(shown_.size()));
        if (scroll_to_sel_) {
            for (size_t i = 0; i < shown_.size(); ++i)
                if (key_of(*row(shown_[i])) == sel_) {
                    clip.IncludeItemByIndex(static_cast<int>(i));
                    break;
                }
        }
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                const StringLine& l = *row(shown_[static_cast<size_t>(i)]);
                const std::string key = key_of(l);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(i);
                if (l.game) ImGui::PushStyleColor(ImGuiCol_Text, l.overridden ? kOrange : kGrey);
                if (ImGui::Selectable(l.id.c_str(), sel_ == key, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) sel_ = key;
                if (l.game) ImGui::PopStyleColor();
                if (scroll_to_sel_ && sel_ == key) {
                    ImGui::SetScrollHereY(0.3f);
                    scroll_to_sel_ = false;
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(l.game ? trf("game: {0}", {l.kind}).c_str() : (l.kind + (l.field.empty() ? "" : " " + l.field)).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(l.speaker.c_str());
                for (auto& lang : audio_langs) {
                    ImGui::TableNextColumn();
                    switch (audio_status(app.project, l, lang)) {
                        case AudioStatus::Recorded: ImGui::TextColored(kGreen, "%s", tr("recorded")); break;
                        case AudioStatus::Missing: ImGui::TextColored(kRed, "%s", tr("missing")); break;
                        default: ImGui::TextDisabled("-"); break;
                    }
                }
                ImGui::TableNextColumn();
                std::string t = text_of(l.text, lang_, app.project.mod.languages);
                std::replace(t.begin(), t.end(), '\n', ' ');
                ImGui::TextUnformatted(t.c_str());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", text_of(l.stage, lang_).c_str());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", l.context.c_str());
                ImGui::PopID();
            }
        ImGui::EndTable();
    }

    // ---- detail ------------------------------------------------------------------------------------------------------------------------------------
    // the selected row; looked up again only when the selection or the index changed (not every frame)
    StringLine* selected() {
        if (sel_cache_ == sel_ && !sel_.empty()) return sel_row_;
        sel_cache_ = sel_;
        sel_row_ = nullptr;
        for (auto* v : {&mod_, &game_})
            for (auto& l : *v)
                if (key_of(l) == sel_) {
                    sel_row_ = &l;
                    return sel_row_;
                }
        return nullptr;
    }

    void draw_detail(App& app) {
        StringLine* l = selected();
        if (!l) {
            ImGui::TextDisabled("%s", tr("Select a row. Rows of the mod are edited here; game rows are read-only (Override in the mod copies one into the project)."));
            return;
        }
        Project& p = app.project;
        ImGui::Text("%s", l->id.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s%s", l->game ? tr("game table ") : "", l->context.c_str());
        if (!l->speaker.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled(tr("| speaker %s%s%s"), l->speaker.c_str(), l->voice.empty() ? "" : tr(", voice "), l->voice.c_str());
        }
        if (!l->game) {
            if (const char* panel = panel_of(l->kind)) {
                ImGui::SameLine();
                if (ImGui::SmallButton(tr("Open"))) app.select_in(panel, l->object_uid);
            }
        } else {
            ImGui::SameLine();
            if (l->overridden) ImGui::TextColored(kOrange, "%s", tr("overridden by the mod"));
            else if (ImGui::SmallButton(tr("Override in the mod"))) {
                const std::string uid = override_game_line(p, *l, p.mod.languages);
                if (!uid.empty()) {
                    sel_ = "m:" + l->id;
                    scroll_to_sel_ = true;
                    app.touch();
                    app.status(trf("the text {0} is now a string row of the mod", {l->id}));
                    return;
                }
            }
        }
        if (l->kind == "string" && !l->game) {
            for (auto& s : p.strings)
                if (s.uid == l->object_uid) {
                    ImGui::SetNextItemWidth(320);
                    if (ui::input_text(tr("Id"), s.id)) {
                        app.touch();
                        sel_ = "m:" + s.id;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton(tr("Delete row"))) {
                        const std::string uid = s.uid;
                        p.strings.erase(std::remove_if(p.strings.begin(), p.strings.end(), [&](const StringRow& r) { return r.uid == uid; }), p.strings.end());
                        app.touch();
                        sel_.clear();
                        return;
                    }
                    break;
                }
        }
        const std::vector<std::string>& langs = l->game ? languages_ : p.mod.languages;
        const bool can_stage = !l->game && !l->node_uid.empty() && l->spoken;
        if (ImGui::BeginTable("##texts", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupColumn(tr("Language"), ImGuiTableColumnFlags_WidthFixed, 70);
            ImGui::TableSetupColumn(tr("Text"), ImGuiTableColumnFlags_WidthStretch, 3);
            ImGui::TableSetupColumn(tr("Stage direction"), ImGuiTableColumnFlags_WidthStretch, 1.2f);
            ImGui::TableSetupColumn(tr("Audio"), ImGuiTableColumnFlags_WidthStretch, 1.6f);
            ImGui::TableHeadersRow();
            for (const std::string& lang : langs) {
                ImGui::PushID(lang.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(lang.c_str());
                ImGui::TableNextColumn();
                std::string t = l->text.count(lang) ? l->text.at(lang) : std::string();
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (l->game) {
                    ImGui::TextWrapped("%s", t.c_str());
                } else if (ImGui::InputText("##t", &t)) {
                    if (set_line_text(p, *l, lang, t)) {
                        if (t.empty()) l->text.erase(lang);
                        else l->text[lang] = t;
                        app.touch();
                    }
                }
                ImGui::TableNextColumn();
                std::string sd = l->stage.count(lang) ? l->stage.at(lang) : std::string();
                if (can_stage) {
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::InputTextWithHint("##s", tr("(whispering)"), &sd) && set_line_stage(p, *l, lang, sd)) {
                        if (sd.empty()) l->stage.erase(lang);
                        else l->stage[lang] = sd;
                        app.touch();
                    }
                } else {
                    ImGui::TextDisabled("%s", sd.c_str());
                }
                ImGui::TableNextColumn();
                const AudioStatus st = audio_status(p, *l, lang);
                if (st != AudioStatus::None) {
                    std::string ref;
                    if (l->game) {
                        ref = l->audio.count(lang) ? l->audio.at(lang) : std::string();
                        if (const auto dot = ref.rfind('.'); dot != std::string::npos) ref = ref.substr(0, dot) + "._xsnd";
                    } else {
                        ref = l->audio.count(lang) ? l->audio.at(lang) : l->audio.count("default") ? l->audio.at("default") : std::string();
                    }
                    if (st == AudioStatus::Recorded) {
                        ui::sound_button(app, ref);
                        ImGui::SameLine();
                        ImGui::TextColored(kGreen, "%s", p2m::bare_name(ref).c_str());
                    } else {
                        ImGui::TextColored(kRed, "%s", tr("missing"));
                        ImGui::SameLine();
                        ImGui::TextDisabled("%s", expected_audio_file(*l, lang).c_str());
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (!l->game && l->spoken && !l->runtime_key.empty())
            ImGui::TextDisabled(tr("Plays as %s (key %s)"), runtime_sound(p, *l, lang_).c_str(), l->runtime_key.c_str());
    }

    // ---- export ------------------------------------------------------------------------------------------------------------------------------------
    void draw_export_modal(App& app) {
        if (export_open_) {
            ImGui::OpenPopup(tr("Export for voice recording"));
            export_open_ = false;
            if (export_langs_.empty()) export_langs_ = app.project.mod.languages;
        }
        ImGui::SetNextWindowSize(ImVec2(720, 0), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal(tr("Export for voice recording"), nullptr)) return;
        ImGui::TextWrapped(tr("Writes the %zu rows the Strings table shows now (its filter) for a recording studio: voice_lines.csv (UTF-8 with BOM: Excel opens it), "
                           "voice_lines.json and scripts/<speaker>_<Language>.txt. Every row has a stable line id and the file name the import expects."),
                           shown_.size());
        ImGui::SetNextItemWidth(-90);
        ImGui::InputText("##dir", &export_dir_);
        ImGui::SameLine();
        if (ImGui::Button(tr("Browse"))) {
            const std::string d = dialog_pick_folder(export_dir_);
            if (!d.empty()) export_dir_ = d;
        }
        ImGui::Text("%s", tr("Languages:"));
        for (auto& l : languages_) {
            ImGui::SameLine();
            bool on = std::find(export_langs_.begin(), export_langs_.end(), l) != export_langs_.end();
            if (ImGui::Checkbox(l.c_str(), &on)) {
                if (on) export_langs_.push_back(l);
                else export_langs_.erase(std::remove(export_langs_.begin(), export_langs_.end(), l), export_langs_.end());
            }
        }
        ImGui::Checkbox(tr("Only lines without a recording"), &exp_missing_);
        ImGui::SameLine();
        ImGui::Checkbox(tr("Only spoken lines"), &exp_spoken_);
        ui::help(tr("Off: every text of the selection (item names, descriptions, quest texts, HUD messages) for translators; their status is empty."));
        ImGui::SameLine();
        ImGui::Checkbox(tr("Scripts per speaker"), &exp_scripts_);
        if (ImGui::Button(tr("Export"), ImVec2(120, 0))) {
            VoiceExportOptions o;
            o.languages = export_langs_;
            o.missing_only = exp_missing_;
            o.spoken_only = exp_spoken_;
            o.scripts = exp_scripts_;
            std::vector<const StringLine*> lines;
            for (size_t ix : shown_) lines.push_back(row(ix));
            if (export_dir_.empty()) {
                export_report_ = tr("choose a folder");
            } else {
                const VoiceExportResult r = write_voice_export(app.project, lines, o, export_dir_);
                export_report_ = r.ok ? trf("{0} rows, {1} files written to {2}", {std::to_string(r.rows), std::to_string(r.written.size()), export_dir_}) : r.error;
                if (r.ok) app.status(trf("voice export: {0}", {export_report_}));
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Open folder")) && !export_dir_.empty()) open_in_os(export_dir_);
        ImGui::SameLine();
        if (ImGui::Button(tr("Close"), ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        if (!export_report_.empty()) ImGui::TextWrapped("%s", export_report_.c_str());
        ImGui::EndPopup();
    }

    // ---- import ------------------------------------------------------------------------------------------------------------------------------------
    void start_ffmpeg_probe(App& app) {
        if (ff_future_.valid() || (ff_ && ff_path_setting_ == app.settings.ffmpeg)) return;
        ff_path_setting_ = app.settings.ffmpeg;
        const std::string configured = app.settings.ffmpeg;
        ff_future_ = std::async(std::launch::async, [configured] {
            std::string err;
            const std::string exe = locate_ffmpeg(configured, &err);
            FfmpegInfo f = probe_ffmpeg(exe, run_process);
            if (!err.empty()) f.error = err;
            return f;
        });
    }

    VoiceImportOptions import_options(const App& app) const {
        VoiceImportOptions o;
        o.convert.mono = app.settings.voice_mono;
        o.convert.sample_rate = app.settings.voice_rate;
        o.convert.normalize = app.settings.voice_normalize;
        o.convert_all = imp_convert_all_;
        return o;
    }

    void draw_import_modal(App& app) {
        if (ff_future_.valid() && ff_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) ff_ = std::make_unique<FfmpegInfo>(ff_future_.get());
        if (preview_future_.valid() && preview_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            preview_ = std::make_unique<VoicePreview>(preview_future_.get());
        poll_job(app);
        if (import_open_) {
            ImGui::OpenPopup(tr("Import recordings"));
            import_open_ = false;
            if (imp_lang_.empty()) imp_lang_ = app.project.mod.languages.empty() ? std::string("English") : app.project.mod.languages.front();
        }
        ImGui::SetNextWindowSize(ImVec2(900, 640), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal(tr("Import recordings"), nullptr)) return;
        Project& p = app.project;
        const bool running = job_ && !job_->done;
        if (app.project_path.empty()) ImGui::TextColored(kOrange, "%s", tr("Save the project first: the recordings are copied into its folder (voice/)."));
        ImGui::BeginDisabled(running);
        ImGui::TextUnformatted(tr("Folder with the recordings"));
        ImGui::SetNextItemWidth(-90);
        bool ch = ImGui::InputText("##folder", &imp_folder_);
        ImGui::SameLine();
        if (ImGui::Button(tr("Browse##f"))) {
            const std::string d = dialog_pick_folder(imp_folder_);
            if (!d.empty()) {
                imp_folder_ = d;
                ch = true;
            }
        }
        ImGui::TextUnformatted(tr("Filled CSV (optional: its 'file' column names the recording of each line)"));
        ImGui::SetNextItemWidth(-90);
        ch |= ImGui::InputText("##csv", &imp_csv_);
        ImGui::SameLine();
        if (ImGui::Button(tr("Browse##c"))) {
            const std::string f = dialog_open_file("CSV", "csv", imp_folder_);
            if (!f.empty()) {
                imp_csv_ = f;
                ch = true;
            }
        }
        ImGui::SetNextItemWidth(140);
        if (ImGui::BeginCombo(tr("Language of files without one"), imp_lang_.c_str())) {
            for (auto& l : p.mod.languages)
                if (ImGui::Selectable(l.c_str(), imp_lang_ == l)) {
                    imp_lang_ = l;
                    ch = true;
                }
            ImGui::EndCombo();
        }
        ui::help(tr("A file named after the line id alone (<line id>.wav) is for this language; <line id>_<Language>.ogg names its own."));
        ImGui::SameLine();
        ch |= ImGui::Checkbox(tr("Only the shown lines"), &imp_shown_only_);
        ch |= ImGui::Checkbox(tr("Normalise loudness (EBU R128, -16 LUFS)"), &app.settings.voice_normalize);
        ImGui::SameLine();
        ch |= ImGui::Checkbox(tr("Mono 44.1 kHz like the game's speech"), &app.settings.voice_mono);
        if (ch) app.settings.voice_rate = app.settings.voice_mono ? 44100 : 0;
        ImGui::SameLine();
        ch |= ImGui::Checkbox(tr("Convert every file"), &imp_convert_all_);
        ui::help(tr("Off: files the game plays as they are (Ogg Vorbis, WAV, MP3) are copied unchanged and only the others go through ffmpeg."));
        if (ch) {
            preview_.reset();
            app.save_settings();
        }
        ImGui::EndDisabled();
        // ffmpeg
        if (ff_future_.valid()) ImGui::TextDisabled("%s", tr("looking for ffmpeg..."));
        else if (ff_ && ff_->ok)
            ImGui::TextColored(kGreen, tr("ffmpeg %s (%s)%s"), ff_->version.c_str(), ff_->path.c_str(), ff_->libvorbis ? "" : tr(" - no libvorbis: conversions write 16-bit WAV"));
        else if (ff_) {
            ImGui::TextColored(kOrange, tr("No ffmpeg: %s. Files the game plays are imported, the others are listed."), ff_->error.c_str());
            ImGui::TextWrapped("%s", ffmpeg_help().c_str());
        }
        ImGui::BeginDisabled(running || preview_future_.valid() || imp_folder_.empty());
        if (ImGui::Button(tr("Preview"), ImVec2(120, 0))) start_preview(app);
        ImGui::EndDisabled();
        ImGui::SameLine();
        const size_t doable = preview_ ? std::count_if(preview_->steps.begin(), preview_->steps.end(), [](const VoiceImportStep& s) { return !s.skip; }) : 0;
        ImGui::BeginDisabled(running || !preview_ || doable == 0 || app.project_path.empty());
        if (ImGui::Button(trf("Import {0} file(s)", {std::to_string(doable)}).c_str(), ImVec2(160, 0))) start_job(app);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (running) {
            size_t i, n;
            std::string cur;
            {
                std::lock_guard lk(job_->mu);
                i = job_->index;
                n = job_->total;
                cur = job_->current;
            }
            ImGui::ProgressBar(n ? static_cast<float>(i) / static_cast<float>(n) : 0.0f, ImVec2(220, 0), (std::to_string(i) + " / " + std::to_string(n)).c_str());
            ImGui::SameLine();
            if (ImGui::Button(tr("Cancel"))) job_->cancel = true;
            ImGui::TextDisabled("%s", p2m::bare_name(cur).c_str());
        } else if (ImGui::Button(tr("Close"), ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        if (want_preview_ && !ff_future_.valid() && !preview_future_.valid() && !running && !imp_folder_.empty()) {
            want_preview_ = false;
            start_preview(app);
        }
        if (preview_future_.valid()) ImGui::TextDisabled("%s", tr("reading the folder..."));
        if (!job_report_.empty()) ImGui::TextColored(kGreen, "%s", job_report_.c_str());
        if (preview_) draw_preview(*preview_);
        ImGui::EndPopup();
    }

    void draw_preview(const VoicePreview& pv) {
        if (!pv.error.empty()) {
            ImGui::TextColored(kRed, "%s", pv.error.c_str());
            return;
        }
        ImGui::Text(tr("%zu audio files: %zu matched, %zu unmatched, %zu ambiguous, %zu duplicate"), pv.files, pv.rep.matched.size(), pv.rep.unmatched.size(),
                    pv.rep.ambiguous.size(), pv.rep.duplicates.size());
        if (!ImGui::BeginTable("##preview", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV,
                               ImVec2(0, ImGui::GetContentRegionAvail().y)))
            return;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(tr("File"), ImGuiTableColumnFlags_WidthStretch, 2);
        ImGui::TableSetupColumn(tr("Result"), ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn(tr("Line / language"), ImGuiTableColumnFlags_WidthStretch, 2);
        ImGui::TableSetupColumn(tr("Note"), ImGuiTableColumnFlags_WidthStretch, 3);
        ImGui::TableHeadersRow();
        auto rowf = [](const std::string& file, const ImVec4& c, const char* res, const std::string& line, const std::string& note) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(p2m::bare_name(file).c_str());
            ImGui::TableNextColumn();
            ImGui::TextColored(c, "%s", res);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(line.c_str());
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", note.c_str());
        };
        for (auto& s : pv.steps) {
            std::string note = s.why + (s.dest_rel.empty() ? "" : " -> " + s.dest_rel);
            if (s.done) note = trf("imported: {0}", {s.dest_rel});
            if (!s.error.empty()) note = trf("error: {0}", {s.error});
            rowf(s.match.file, s.skip || !s.error.empty() ? kRed : s.done ? kGreen : ImVec4(0.8f, 0.9f, 1, 1),
                 s.skip ? "cannot" : s.done ? "done" : s.convert ? "convert" : "copy", s.match.line_id + " / " + s.match.language, note);
        }
        for (auto& [f, why] : pv.rep.unmatched) rowf(f, kOrange, "unmatched", "", why);
        for (auto& [f, c] : pv.rep.ambiguous) {
            std::string all;
            for (auto& x : c) all += (all.empty() ? "" : ", ") + x;
            rowf(f, kOrange, "ambiguous", all, "rename the file or name it in the CSV");
        }
        for (auto& [k, files] : pv.rep.duplicates)
            for (auto& f : files) rowf(f, kOrange, "duplicate", k, "more than one file for this line and language: none is taken");
        ImGui::EndTable();
    }

    std::vector<StringLine> import_targets() const {
        std::vector<StringLine> t;
        if (imp_shown_only_) {
            for (size_t ix : shown_)
                if (const StringLine* l = row(ix); !l->game && l->spoken) t.push_back(*l);
        } else {
            for (auto& l : mod_)
                if (l.spoken) t.push_back(l);
        }
        return t;
    }

    void start_preview(App& app) {
        preview_.reset();
        job_report_.clear();
        const Project p = app.project;
        const auto targets = import_targets();
        const std::string folder = imp_folder_, csv = imp_csv_, lang = imp_lang_;
        const VoiceImportOptions o = import_options(app);
        const std::shared_ptr<FfmpegInfo> ff = ff_ ? std::make_shared<FfmpegInfo>(*ff_) : nullptr;
        preview_future_ = std::async(std::launch::async, [p, targets, folder, csv, lang, o, ff] {
            return preview_voice_import(p, targets, folder, csv, lang, o, ff.get());
        });
    }

    void start_job(App& app) {
        job_ = std::make_unique<ImportJob>();
        job_->steps = preview_->steps;
        job_->total = job_->steps.size();
        const std::string dir = u8str(u8(app.project_path).parent_path());
        const VoiceImportOptions o = import_options(app);
        const std::shared_ptr<FfmpegInfo> ff = ff_ ? std::make_shared<FfmpegInfo>(*ff_) : nullptr;
        ImportJob* j = job_.get();
        j->thread = std::thread([j, dir, o, ff] {
            auto res = run_voice_import(j->steps, dir, o, ff.get(), run_process, [j](size_t i, size_t n, const std::string& f) {
                std::lock_guard lk(j->mu);
                j->index = i;
                j->total = n;
                j->current = f;
            }, &j->cancel);
            std::lock_guard lk(j->mu);
            j->result = std::move(res);
            j->done = true;
        });
    }

    void poll_job(App& app) {
        if (!job_ || !job_->done || !job_->thread.joinable()) return;
        job_->thread.join();
        const int n = apply_voice_import(app.project, job_->result);
        size_t errors = 0;
        for (auto& s : job_->steps) errors += !s.error.empty();
        if (preview_) preview_->steps = job_->steps;
        job_report_ = std::to_string(job_->result.size()) + " recording(s) imported, " + std::to_string(n) + " line(s) changed" +
                      (errors ? ", " + std::to_string(errors) + " error(s)" : std::string()) + (job_->cancel ? " (cancelled)" : "") + ". Undo with Ctrl+Z.";
        if (!job_->result.empty()) {
            app.touch();
            app.refresh_assets(true);
        }
        app.status(trf("voice import: {0}", {job_report_}));
    }

    // index
    std::vector<StringLine> mod_, game_;
    size_t mod_rev_ = static_cast<size_t>(-1);
    const GameData* mod_game_ = nullptr;
    const GameData* game_src_ = nullptr;
    std::future<std::vector<StringLine>> game_future_;
    std::vector<size_t> shown_;
    size_t shown_spoken_ = 0, shown_missing_ = 0;
    std::string sel_cache_;
    StringLine* sel_row_ = nullptr;
    std::vector<std::string> kinds_, languages_;
    bool filter_dirty_ = true, scroll_to_sel_ = false;
    // filter
    std::string filter_, kind_, speaker_, context_, lang_ = "English";
    int source_ = 0;
    bool spoken_only_ = false, missing_only_ = false, show_overridden_ = false;
    std::string sel_;
    // export
    bool export_open_ = false, exp_missing_ = false, exp_spoken_ = true, exp_scripts_ = true;
    std::string export_dir_, export_report_;
    std::vector<std::string> export_langs_;
    // import
    bool import_open_ = false, imp_shown_only_ = true, imp_convert_all_ = false, want_preview_ = false;
    std::string imp_folder_, imp_csv_, imp_lang_, job_report_;
    std::future<FfmpegInfo> ff_future_;
    std::unique_ptr<FfmpegInfo> ff_;
    std::string ff_path_setting_;
    std::future<VoicePreview> preview_future_;
    std::unique_ptr<VoicePreview> preview_;
    std::unique_ptr<ImportJob> job_;
};

}  // namespace

std::unique_ptr<Panel> make_strings_panel() { return std::make_unique<StringsPanel>(); }

}  // namespace mk
