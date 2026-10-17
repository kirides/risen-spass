#include "app/pickers.h"

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
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("No template with this name in the game data or the project");
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

}  // namespace mk::ui
