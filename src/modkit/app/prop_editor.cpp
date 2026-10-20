#include "app/prop_editor.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>

#include "core/catalog.h"
#include "core/i18n.h"

namespace mk::ui {

namespace {

bool icontains(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
                          [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
    return it != hay.end();
}

// "bTPropertyContainer<enum gEItemUseType>" -> "gEItemUseType"
std::string enum_type_of(const std::string& type) {
    static const std::string pre = "bTPropertyContainer<enum ";
    if (type.rfind(pre, 0) != 0) return {};
    const auto e = type.find('>', pre.size());
    return e == std::string::npos ? std::string() : type.substr(pre.size(), e - pre.size());
}

}  // namespace

json val_to_json(const GameData& g, const p2m::Val& v, const std::string& type) {
    if (std::string e = g.enum_name(type, v); !e.empty()) return e;
    switch (v.k) {
        case p2m::Val::Int: return v.i;
        case p2m::Val::Bool: return v.i != 0;
        case p2m::Val::Float: return v.d;
        case p2m::Val::Str: return v.s;
        case p2m::Val::Vec: {
            json a = json::array();
            for (double x : v.v) a.push_back(x);
            return a;
        }
        default: return nullptr;
    }
}

bool prop_edits(const char* id, PropEdits& edits, const GameData* g, const TemplateInfo* t) {
    bool changed = false;
    ImGui::PushID(id);
    std::pair<std::string, std::string> remove;
    if (ImGui::BeginTable("##edits", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn(tr("property"), ImGuiTableColumnFlags_WidthStretch, 3);
        ImGui::TableSetupColumn(tr("value"), ImGuiTableColumnFlags_WidthStretch, 3);
        ImGui::TableSetupColumn("x", ImGuiTableColumnFlags_WidthFixed, 24);
        for (auto& [set, list] : edits) {
            for (auto& [name, v] : list) {
                const std::string key = set + "." + name;
                ImGui::PushID(key.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(key.c_str());
                if (t && g) {
                    if (const p2m::Val* sv = t->get(key)) {
                        if (ImGui::IsItemHovered()) {
                            const json sj = val_to_json(*g, *sv, *t->props.type_of(key));
                            ImGui::SetTooltip(tr("shipped: %s"), sj.dump().c_str());
                        }
                    }
                }
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                const std::string* type = t ? t->props.type_of(key) : nullptr;
                const std::string et = type ? enum_type_of(*type) : std::string();
                if (!et.empty() && v.is_string()) {
                    std::string s = v.get<std::string>();
                    if (ImGui::BeginCombo("##v", s.c_str(), ImGuiComboFlags_HeightLarge)) {
                        for (auto& n : enum_names(g, et))
                            if (ImGui::Selectable(n.c_str(), n == s)) {
                                v = n;
                                changed = true;
                            }
                        ImGui::EndCombo();
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Only the enum values the editor knows names for are listed"));
                } else if (v.is_boolean()) {
                    bool b = v.get<bool>();
                    if (ImGui::Checkbox("##v", &b)) {
                        v = b;
                        changed = true;
                    }
                } else if (v.is_number_integer()) {
                    int x = v.get<int>();
                    if (ImGui::InputInt("##v", &x)) {
                        v = x;
                        changed = true;
                    }
                } else if (v.is_number()) {
                    double x = v.get<double>();
                    if (ImGui::InputDouble("##v", &x, 0, 0, "%.6g")) {
                        v = x;
                        changed = true;
                    }
                } else if (v.is_array() && v.size() == 3) {
                    float f[3] = {v[0].get<float>(), v[1].get<float>(), v[2].get<float>()};
                    if (ImGui::InputFloat3("##v", f)) {
                        v = json::array({f[0], f[1], f[2]});
                        changed = true;
                    }
                } else {
                    std::string s = v.is_string() ? v.get<std::string>() : v.dump();
                    if (ImGui::InputText("##v", &s)) {
                        v = s;
                        changed = true;
                    }
                }
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("x")) remove = {set, name};
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    if (!remove.first.empty()) {
        edits[remove.first].erase(remove.second);
        if (edits[remove.first].empty()) edits.erase(remove.first);
        changed = true;
    }
    // add: the template's properties (scalars only: arrays have their own settings) or a free "Set.Name"
    static std::string filter, free_key;
    ImGui::SetNextItemWidth(260);
    if (ImGui::BeginCombo("##add", tr("add property..."), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::IsWindowAppearing()) {
            filter.clear();
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::InputTextWithHint("##f", tr("filter"), &filter);
        if (t && g) {
            for (size_t i = 0; i < t->props.size(); ++i) {
                const std::string& k = t->props.keys[i];
                if (k.find("[]") != std::string::npos || k.find('#') != std::string::npos || !icontains(k, filter)) continue;
                const auto dot = k.find('.');
                if (dot == std::string::npos) continue;
                const std::string set = k.substr(0, dot), name = k.substr(dot + 1);
                if (edits.count(set) && edits[set].count(name)) continue;
                json start = val_to_json(*g, t->props.vals[i], t->props.types[i]);
                if (start.is_null()) continue;
                if (ImGui::Selectable(k.c_str())) {
                    edits[set][name] = start;
                    changed = true;
                }
            }
        } else {
            ImGui::TextDisabled("%s", tr("no template: type Set.Name below"));
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##free", tr("or Set.Name"), &free_key);
    ImGui::SameLine();
    if (ImGui::Button(tr("Add")) && free_key.find('.') != std::string::npos) {
        const auto dot = free_key.find('.');
        edits[free_key.substr(0, dot)][free_key.substr(dot + 1)] = 0;
        free_key.clear();
        changed = true;
    }
    ImGui::PopID();
    return changed;
}

}  // namespace mk::ui
