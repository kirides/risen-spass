#include "app/widgets.h"

#include <imgui_stdlib.h>

#include <sstream>

namespace mk::ui {

bool input_text(const char* label, std::string& s, ImGuiInputTextFlags flags) { return ImGui::InputText(label, &s, flags); }

bool input_multiline(const char* label, std::string& s, float height_lines) {
    return ImGui::InputTextMultiline(label, &s, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * height_lines + 8));
}

namespace {
bool set_box(const char* label, bool is_set, bool& toggled) {
    ImGui::PushID(label);
    bool on = is_set;
    toggled = ImGui::Checkbox("##set", &on);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(on ? "Set: the mod writes this value" : "Not set: the template keeps its own value");
    ImGui::PopID();
    ImGui::SameLine();
    return on;
}
}  // namespace

bool opt_int(const char* label, std::optional<int>& v, const char* shipped_hint, int def) {
    bool toggled = false;
    const bool on = set_box(label, v.has_value(), toggled);
    bool changed = false;
    if (toggled) {
        if (on) v = def;
        else v.reset();
        changed = true;
    }
    if (v) {
        int x = *v;
        if (ImGui::InputInt(label, &x)) {
            v = x;
            changed = true;
        }
    } else {
        ImGui::BeginDisabled();
        int x = def;
        ImGui::InputInt(label, &x);
        ImGui::EndDisabled();
    }
    if (shipped_hint && *shipped_hint) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", shipped_hint);
    }
    return changed;
}

bool opt_double(const char* label, std::optional<double>& v, const char* shipped_hint, double def, const char* fmt) {
    bool toggled = false;
    const bool on = set_box(label, v.has_value(), toggled);
    bool changed = false;
    if (toggled) {
        if (on) v = def;
        else v.reset();
        changed = true;
    }
    if (v) {
        double x = *v;
        if (ImGui::InputDouble(label, &x, 0.1, 1.0, fmt)) {
            v = x;
            changed = true;
        }
    } else {
        ImGui::BeginDisabled();
        double x = def;
        ImGui::InputDouble(label, &x, 0.1, 1.0, fmt);
        ImGui::EndDisabled();
    }
    if (shipped_hint && *shipped_hint) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", shipped_hint);
    }
    return changed;
}

bool opt_text(const char* label, std::optional<std::string>& v, const char* shipped_hint) {
    bool toggled = false;
    const bool on = set_box(label, v.has_value(), toggled);
    bool changed = false;
    if (toggled) {
        if (on) v = shipped_hint ? std::string(shipped_hint) : std::string();
        else v.reset();
        changed = true;
    }
    if (v) {
        changed |= ImGui::InputText(label, &*v);
    } else {
        ImGui::BeginDisabled();
        std::string x = shipped_hint ? shipped_hint : "";
        ImGui::InputText(label, &x);
        ImGui::EndDisabled();
    }
    return changed;
}

bool lang_text(const char* label, Text& t, const std::vector<std::string>& languages, bool multiline) {
    bool changed = false;
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    for (size_t i = 0; i < languages.size(); ++i) {
        const std::string& lang = languages[i];
        std::string v;
        if (auto it = t.find(lang); it != t.end()) v = it->second;
        ImGui::PushID(static_cast<int>(i));
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%-8s", lang.c_str());
        ImGui::SameLine(90);
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool c = multiline ? ImGui::InputTextMultiline("##t", &v, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 3 + 8)) : ImGui::InputText("##t", &v);
        if (c) {
            if (v.empty()) t.erase(lang);
            else t[lang] = v;
            changed = true;
        }
        ImGui::PopID();
    }
    ImGui::PopID();
    return changed;
}

bool string_list(const char* label, std::vector<std::string>& v) {
    std::string joined;
    for (size_t i = 0; i < v.size(); ++i) joined += (i ? "\n" : "") + v[i];
    if (!ImGui::InputTextMultiline(label, &joined, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 3 + 8))) return false;
    v.clear();
    std::istringstream ss(joined);
    for (std::string line; std::getline(ss, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty()) v.push_back(line);
    }
    return true;
}

bool int_map(const char* id, IntMap& m, const std::vector<std::string>& choices, int def_value, int min_value, int max_value) {
    bool changed = false;
    ImGui::PushID(id);
    std::string remove;
    if (ImGui::BeginTable("##map", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 3);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 2);
        ImGui::TableSetupColumn("x", ImGuiTableColumnFlags_WidthFixed, 24);
        for (auto& [k, val] : m) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(k.c_str());
            ImGui::TableNextColumn();
            ImGui::PushID(k.c_str());
            ImGui::SetNextItemWidth(-FLT_MIN);
            int x = val;
            if (ImGui::InputInt("##v", &x)) {
                val = std::clamp(x, min_value, max_value);
                changed = true;
            }
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("x")) remove = k;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (!remove.empty()) {
        m.erase(remove);
        changed = true;
    }
    static std::string pending;  // the add field (one is edited at a time)
    ImGui::SetNextItemWidth(200);
    if (!choices.empty()) {
        if (ImGui::BeginCombo("##add", "add...", ImGuiComboFlags_HeightLarge)) {
            static ImGuiTextFilter filter;
            filter.Draw("##filter", 180);
            for (auto& c : choices) {
                if (m.count(c) || !filter.PassFilter(c.c_str())) continue;
                if (ImGui::Selectable(c.c_str())) {
                    m[c] = def_value;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
    } else {
        ImGui::InputTextWithHint("##addname", "name", &pending);
        ImGui::SameLine();
        if (ImGui::Button("Add") && !pending.empty()) {
            m[pending] = def_value;
            pending.clear();
            changed = true;
        }
    }
    ImGui::PopID();
    return changed;
}

void help(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void section(const char* title) {
    ImGui::Spacing();
    ImGui::SeparatorText(title);
}

bool combo(const char* label, std::string& v, const std::vector<std::string>& items, bool allow_empty) {
    bool changed = false;
    if (ImGui::BeginCombo(label, v.empty() ? "(none)" : v.c_str(), ImGuiComboFlags_HeightLarge)) {
        if (allow_empty && ImGui::Selectable("(none)", v.empty())) {
            v.clear();
            changed = true;
        }
        for (auto& it : items) {
            if (ImGui::Selectable(it.c_str(), it == v)) {
                v = it;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

void hint(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", text);
}

}  // namespace mk::ui
