#include "app/pickers.h"

#include "app/audio.h"
#include "app/dragdrop.h"
#include "app/widgets.h"
#include "core/assets.h"
#include "pack2mod/util.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cstring>

namespace mk::ui {

namespace {

void visible_label(const char* label) {
    const char* end = std::strstr(label, "##");
    const size_t n = end ? static_cast<size_t>(end - label) : std::strlen(label);
    if (!n) return;
    ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::TextUnformatted(label, label + n);
}

bool icontains(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
                          [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
    return it != hay.end();
}

}  // namespace

std::string label_of(const App& app, const std::string& name) {
    for (auto& i : app.project.items)
        if (i.name == name && !i.label.empty()) return i.label.begin()->second;
    for (auto& n : app.project.npc_templates)
        if (n.name == name && !n.label.empty()) return n.label.begin()->second;
    for (auto& r : app.project.recipes)
        if (r.name == name && !r.label.empty()) return r.label.begin()->second;
    return app.game ? app.game->label(name) : std::string();
}

bool template_picker(const char* label, std::string& name, const GameData* game, std::initializer_list<TplKind> kinds, const std::vector<std::string>& extra,
                     float width) {
    bool changed = false;
    ImGui::PushID(label);
    const bool known = name.empty() || !game || game->tpl(name) || std::find(extra.begin(), extra.end(), name) != extra.end();
    if (!known) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.65f, 0.3f, 1));
    ImGui::SetNextItemWidth(width > 0 ? width : ImGui::CalcItemWidth() - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemInnerSpacing.x);
    changed |= ImGui::InputText("##name", &name);
    if (!known) {
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered() && !dragging()) ImGui::SetTooltip("No template with this name in the game data or the project");
    }
    if (const DragTemplate* d = drop_target(kinds, extra)) {
        name = d->name;
        changed = true;
    }
    ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
    static std::string filter;
    if (ImGui::ArrowButton("##open", ImGuiDir_Down)) {
        filter.clear();
        ImGui::OpenPopup("##pick");
    }
    visible_label(label);
    if (ImGui::BeginPopup("##pick")) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(360);
        ImGui::InputTextWithHint("##filter", "filter by name or label", &filter);
        ImGui::BeginChild("##list", ImVec2(520, 360));
        auto row = [&](const std::string& n, const std::string& lbl, const char* tag) {
            if (!icontains(n, filter) && !icontains(lbl, filter)) return;
            if (ImGui::Selectable((n + "##" + tag).c_str(), n == name, ImGuiSelectableFlags_None)) {
                name = n;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            if (!lbl.empty()) {
                ImGui::SameLine(300);
                ImGui::TextDisabled("%s", lbl.c_str());
            }
        };
        for (auto& e : extra) row(e, "(project)", "p");
        if (game) {
            ImGuiListClipper clip;
            std::vector<const TemplateInfo*> list;
            for (auto& t : game->templates()) {
                if (kinds.size() && std::find(kinds.begin(), kinds.end(), t->kind) == kinds.end()) continue;
                const std::string lbl = game->label(t->name);
                if (!icontains(t->name, filter) && !icontains(lbl, filter)) continue;
                list.push_back(t.get());
            }
            clip.Begin(static_cast<int>(list.size()));
            while (clip.Step())
                for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) row(list[i]->name, game->label(list[i]->name), "g");
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

bool asset_picker(const char* label, std::string& name, const GameData* game, const char* ext, bool strip_compiled, float width) {
    bool changed = false;
    ImGui::PushID(label);
    ImGui::SetNextItemWidth(width > 0 ? width : ImGui::CalcItemWidth() - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemInnerSpacing.x);
    changed |= ImGui::InputText("##name", &name);
    ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
    static std::string filter;
    if (ImGui::ArrowButton("##open", ImGuiDir_Down)) {
        filter.clear();
        ImGui::OpenPopup("##pick");
    }
    visible_label(label);
    if (ImGui::BeginPopup("##pick")) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(360);
        ImGui::InputTextWithHint("##filter", "filter", &filter);
        ImGui::BeginChild("##list", ImVec2(520, 360));
        if (game) {
            std::vector<std::string> list;
            for (auto& a : game->assets(ext)) {
                std::string n = a.name;
                if (strip_compiled) {
                    // "X._xmsh" -> "X.xmsh"
                    const auto dot = n.rfind("._");
                    if (dot != std::string::npos) n.erase(dot + 1, 1);
                }
                if (icontains(n, filter)) list.push_back(n);
            }
            ImGuiListClipper clip;
            clip.Begin(static_cast<int>(list.size()));
            while (clip.Step())
                for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i)
                    if (ImGui::Selectable(list[i].c_str(), list[i] == name)) {
                        name = list[i];
                        changed = true;
                        ImGui::CloseCurrentPopup();
                    }
        } else {
            ImGui::TextDisabled("load the game data first (Settings)");
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

bool sound_button(App& app, const std::string& ref, const char* id) {
    const bool on = !ref.empty() && app.audio().current() == ref;
    ImGui::BeginDisabled(ref.empty());
    const bool clicked = ImGui::SmallButton(((on ? std::string("Stop") : std::string("Play")) + id).c_str());
    ImGui::EndDisabled();
    if (clicked) app.toggle_sound(ref);
    if (on && ImGui::IsItemHovered()) ImGui::SetTooltip("%.1f / %.1f s", app.audio().position(), app.audio().length());
    return clicked;
}

bool sound_picker(const char* label, std::string& ref, App& app, bool raw_name, float width) {
    // a raw name is played as the compiled file of the same stem
    auto compiled_of = [&](const std::string& v) -> std::string {
        if (!raw_name || is_compiled_sound(v)) return v;
        const auto dot = v.rfind('.');
        return (dot == std::string::npos ? v : v.substr(0, dot)) + "._xsnd";
    };
    bool changed = false;
    ImGui::PushID(label);
    const float play_w = ImGui::CalcTextSize("Stop").x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemInnerSpacing.x;
    ImGui::SetNextItemWidth(width > 0 ? width : ImGui::CalcItemWidth() - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemInnerSpacing.x - play_w);
    changed |= ImGui::InputTextWithHint("##name", raw_name ? "X.wav" : "a sound", &ref);
    ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
    static std::string filter;
    static int group = 0;  // 0 all, 1.. a voice language, last = effects (no language)
    if (ImGui::ArrowButton("##open", ImGuiDir_Down)) {
        filter.clear();
        ImGui::OpenPopup("##pick");
    }
    ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
    sound_button(app, compiled_of(ref));
    visible_label(label);
    if (ImGui::BeginPopup("##pick")) {
        static const char* kGroups[] = {"all", "English", "German", "French", "Russian", "effects and other"};
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(360);
        ImGui::InputTextWithHint("##filter", "filter", &filter);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160);
        ImGui::Combo("##group", &group, kGroups, IM_ARRAYSIZE(kGroups));
        ImGui::BeginChild("##list", ImVec2(640, 380));
        struct Row {
            std::string value, play;  // what is stored, what is played
            bool project;
        };
        std::vector<Row> list;
        for (auto& a : app.project.assets) {
            const std::string n = p2m::bare_name(a.file);
            if (asset_kind(n) != "sound" || (raw_name && !is_compiled_sound(n)) || !icontains(n, filter)) continue;
            list.push_back({raw_name ? sound_raw_name(n) : n, n, true});
        }
        if (app.game) {
            for (auto& a : app.game->assets("_xsnd")) {
                if (!icontains(a.name, filter)) continue;
                if (group > 0) {
                    const std::string lang = sound_language(a.name);
                    if (group == IM_ARRAYSIZE(kGroups) - 1 ? !lang.empty() : lang != kGroups[group]) continue;
                }
                list.push_back({raw_name ? sound_raw_name(a.name) : a.name, a.name, false});
            }
        } else {
            ImGui::TextDisabled("load the game data (Settings) to see the game's sounds");
        }
        ImGuiListClipper clip;
        clip.Begin(static_cast<int>(list.size()));
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                ImGui::PushID(i);
                sound_button(app, list[i].play);
                ImGui::SameLine();
                if (list[i].project) {
                    ImGui::TextColored(ImVec4(0.55f, 0.8f, 1.0f, 1.0f), "project");
                    ImGui::SameLine();
                }
                if (ImGui::Selectable(list[i].value.c_str(), list[i].value == ref)) {
                    ref = list[i].value;
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
        ImGui::EndChild();
        ImGui::TextDisabled("%zu sounds", list.size());
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

bool sound_rows(const char* label, json& sound, App& app) {
    if (!sound.is_object()) sound = json::object();
    bool changed = false;
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    help("Spoken audio for the line: an audio file of the project (Assets panel: Ogg Vorbis, WAV, MP3) or a sound of the game. 'every' plays in "
         "every voice language; a language row replaces it for that voice language. The export writes the files into sounds/ of the mod.");
    std::vector<std::string> rows = {"default"};
    for (auto& l : app.project.mod.languages) rows.push_back(l);
    for (auto& r : rows) {
        ImGui::PushID(r.c_str());
        std::string v = sound.value(r, std::string());
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%-8s", r == "default" ? "every" : r.c_str());
        ImGui::SameLine(90);
        ImGui::PushItemWidth(-FLT_MIN);
        if (sound_picker("##snd", v, app)) {
            if (v.empty()) sound.erase(r);
            else sound[r] = v;
            changed = true;
        }
        ImGui::PopItemWidth();
        ImGui::PopID();
    }
    ImGui::PopID();
    return changed;
}

}  // namespace mk::ui
