#include "app/editor_util.h"

#include <imgui.h>

#include <algorithm>

#include "app/dragdrop.h"
#include "app/pickers.h"
#include "app/widgets.h"

namespace mk::ui {

namespace {

// tells that the list takes dropped items (next to its add button)
void drop_hint() {
    ImGui::SameLine();
    ImGui::TextDisabled("or drop items here");
}

}  // namespace

std::string name_prefix(const Project& p) {
    std::string s = p.mod.id;
    if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s.empty() ? "My" : s;
}

std::string unique_template_name(const Project& p, const GameData* g, std::string want) {
    auto taken = [&](const std::string& n) {
        if (g && g->tpl(n)) return true;
        for (auto& i : p.items)
            if (i.name == n) return true;
        for (auto& r : p.recipes)
            if (r.name == n) return true;
        for (auto& t : p.npc_templates)
            if (t.name == n) return true;
        for (auto& n2 : p.npcs)
            if (n2.name == n) return true;
        return false;
    };
    if (want.size() > 58) want.resize(58);
    if (!taken(want)) return want;
    for (int i = 2;; ++i)
        if (!taken(want + "_" + std::to_string(i))) return want + "_" + std::to_string(i);
}

bool opt_enum(const char* label, std::optional<std::string>& v, const std::vector<std::string>& names, const std::string& shipped) {
    bool changed = false;
    ImGui::PushID(label);
    bool on = v.has_value();
    if (ImGui::Checkbox("##set", &on)) {
        if (on) v = shipped.empty() ? (names.empty() ? std::string() : names.front()) : shipped;
        else v.reset();
        changed = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!v.has_value());
    const std::string cur = v ? *v : shipped;
    if (ImGui::BeginCombo(label, cur.empty() ? "(none)" : cur.c_str(), ImGuiComboFlags_HeightLarge)) {
        for (auto& n : names)
            if (ImGui::Selectable(n.c_str(), n == cur)) {
                v = n;
                changed = true;
            }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    ImGui::PopID();
    return changed;
}

bool opt_choice(const char* label, std::optional<std::string>& v, const std::vector<std::string>& names, const std::string& shipped) {
    return opt_enum(label, v, names, shipped);
}

bool opt_skills(const char* label, std::optional<IntMap>& m, const IntMap& shipped, const std::vector<std::string>& skills, const char* help_text) {
    bool changed = false;
    ImGui::PushID(label);
    bool on = m.has_value();
    if (ImGui::Checkbox(label, &on)) {
        if (on) m = shipped;
        else m.reset();
        changed = true;
    }
    help(help_text);
    if (m) {
        ImGui::Indent();
        changed |= int_map("##map", *m, skills, 1, -1000, 1000);
        ImGui::Unindent();
    } else {
        std::string s;
        for (auto& [k, v] : shipped) s += (s.empty() ? "" : ", ") + k + " " + std::to_string(v);
        ImGui::SameLine();
        ImGui::TextDisabled("shipped: %s", s.empty() ? "none" : s.c_str());
    }
    ImGui::PopID();
    return changed;
}

bool ingredient_list(const char* id, std::vector<Ingredient>& v, App& app, const std::vector<std::string>& extra, size_t max_rows) {
    bool changed = false;
    ImGui::PushID(id);
    ImGui::BeginGroup();
    int remove = -1;
    for (size_t i = 0; i < v.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        changed |= template_picker("##item", v[i].item, app.game.get(), {TplKind::Item, TplKind::Recipe}, extra, 300);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        int n = v[i].count;
        if (ImGui::InputInt("##n", &n)) {
            v[i].count = std::clamp(n, 1, 999);
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) remove = static_cast<int>(i);
        const std::string lbl = label_of(app, v[i].item);
        if (!lbl.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", lbl.c_str());
        }
        ImGui::PopID();
    }
    if (remove >= 0) {
        v.erase(v.begin() + remove);
        changed = true;
    }
    if (v.size() < max_rows && ImGui::SmallButton("+ ingredient")) {
        v.push_back({"", 1});
        changed = true;
    }
    drop_hint();
    ImGui::EndGroup();
    // a drop on a row's field replaces its item (template_picker), anywhere else on the list adds one
    if (const DragTemplate* d = drop_target({TplKind::Item, TplKind::Recipe}, extra)) {
        auto it = std::find_if(v.begin(), v.end(), [&](const Ingredient& x) { return x.item == d->name; });
        if (it != v.end()) it->count = std::min(it->count + 1, 999);
        else if (v.size() < max_rows) v.push_back({d->name, 1});
        changed = true;
    }
    ImGui::PopID();
    return changed;
}

bool inventory_list(const char* id, std::vector<InvItem>& v, App& app, const std::vector<std::string>& extra, bool with_equip) {
    bool changed = false;
    ImGui::PushID(id);
    ImGui::BeginGroup();
    int remove = -1;
    for (size_t i = 0; i < v.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        changed |= template_picker("##item", v[i].item, app.game.get(), {TplKind::Item, TplKind::Recipe}, extra, 300);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        int n = v[i].amount;
        if (ImGui::InputInt("##n", &n)) {
            v[i].amount = std::clamp(n, 1, 100000);
            changed = true;
        }
        if (with_equip) {
            ImGui::SameLine();
            changed |= ImGui::Checkbox("equip", &v[i].equip);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) remove = static_cast<int>(i);
        const std::string lbl = label_of(app, v[i].item);
        if (!lbl.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", lbl.c_str());
        }
        ImGui::PopID();
    }
    if (remove >= 0) {
        v.erase(v.begin() + remove);
        changed = true;
    }
    if (ImGui::SmallButton("+ item")) {
        v.push_back({"", 1, false});
        changed = true;
    }
    drop_hint();
    ImGui::EndGroup();
    if (const DragTemplate* d = drop_target({TplKind::Item, TplKind::Recipe}, extra)) {
        auto it = std::find_if(v.begin(), v.end(), [&](const InvItem& x) { return x.item == d->name; });
        if (it != v.end()) it->amount = std::min(it->amount + 1, 100000);
        else v.push_back({d->name, 1, false});
        changed = true;
    }
    ImGui::PopID();
    return changed;
}

TplKind kind_of(const App& app, const std::string& name) {
    std::string n = name;
    for (int depth = 0; depth < 8 && !n.empty(); ++depth) {
        if (app.game)
            if (const TemplateInfo* t = app.game->tpl(n)) return t->kind;
        const auto& p = app.project;
        if (std::any_of(p.items.begin(), p.items.end(), [&](const ItemDef& i) { return i.name == n; })) return TplKind::Item;
        if (std::any_of(p.recipes.begin(), p.recipes.end(), [&](const RecipeDef& r) { return r.name == n; })) return TplKind::Recipe;
        auto it = std::find_if(p.npc_templates.begin(), p.npc_templates.end(), [&](const NpcTemplateDef& t) { return t.name == n; });
        if (it == p.npc_templates.end()) return TplKind::Other;
        if (it->base.empty() || it->base == n) return TplKind::Npc;
        n = it->base;  // a clone of a clone
    }
    return TplKind::Npc;
}

std::vector<std::string> project_item_names(const Project& p) {
    std::vector<std::string> v;
    for (auto& i : p.items)
        if (i.mode == Mode::New && !i.name.empty()) v.push_back(i.name);
    for (auto& r : p.recipes)
        if (r.mode == Mode::New && !r.name.empty()) v.push_back(r.name);
    return v;
}

}  // namespace mk::ui
