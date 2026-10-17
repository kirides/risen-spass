#include "app/editor_util.h"

#include <imgui.h>

#include <algorithm>

#include "app/dragdrop.h"
#include "app/pickers.h"
#include "app/widgets.h"
#include "core/catalog.h"
#include "core/graph.h"

#include <imgui_stdlib.h>

namespace mk::ui {

namespace {

bool json_str(const char* label, json& obj, const char* key, const char* hint = nullptr) {
    std::string s = obj.value(key, std::string());
    if (!(hint ? ImGui::InputTextWithHint(label, hint, &s) : ImGui::InputText(label, &s))) return false;
    obj[key] = s;
    return true;
}

bool json_choice(const char* label, json& obj, const char* key, const std::vector<std::string>& items) {
    std::string s = obj.value(key, std::string());
    if (!combo(label, s, items, false)) return false;
    obj[key] = s;
    return true;
}

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

bool condition_args(App& app, json& dd) {
    bool ch = false;
    static const std::vector<std::string> kKinds = {"quest", "item", "var", "event", "skill", "guild", "dead", "alive"};
    std::string kind = dd.value("kind", std::string("quest"));
    if (combo("Kind", kind, kKinds, false)) {
        dd["kind"] = kind;
        dd["args"] = json::object();
        ch = true;
    }
    if (!dd.contains("args") || !dd["args"].is_object()) dd["args"] = json::object();
    json& a = dd["args"];
    static const std::vector<std::string> kOps = {">=", "==", "~=", "<=", ">", "<"};
    std::vector<std::string> npcs;
    for (auto& n : app.project.npcs) npcs.push_back(n.name);
    auto who = [&](const char* key, const char* label) { ch |= json_str(label, a, key, "player (empty), npc or a unique name"); };
    if (kind == "quest") {
        std::string q = a.value("quest", std::string());
        if (quest_picker("Quest", q, app)) {
            a["quest"] = q;
            ch = true;
        }
        help("A quest of this mod or ANY quest of the game (the list shows the game's 370 quests with their titles). The condition holds while the quest has "
             "the status (Success: it was solved).");
        ch |= json_choice("Status", a, "status", quest_states());
    } else if (kind == "item") {
        std::string it = a.value("item", std::string());
        if (template_picker("Item", it, app.game.get(), {TplKind::Item, TplKind::Recipe}, project_item_names(app.project))) {
            a["item"] = it;
            ch = true;
        }
        int cnt = a.value("count", 1);
        if (ImGui::InputInt("Count", &cnt)) {
            a["count"] = std::max(1, cnt);
            ch = true;
        }
        who("holder", "Holder");
    } else if (kind == "var") {
        ch |= json_str("Variable", a, "name");
        bool has = a.contains("value") && !a["value"].is_null();
        if (ImGui::Checkbox("Compare a value", &has)) {
            if (has) a["value"] = 1;
            else {
                a.erase("value");
                a.erase("op");
            }
            ch = true;
        }
        if (has) {
            int v = a.value("value", 1);
            if (ImGui::InputInt("Value", &v)) {
                a["value"] = v;
                ch = true;
            }
            ch |= json_choice("Compare", a, "op", kOps);
        }
    } else if (kind == "event") {
        ch |= json_str("Event", a, "name");
        help("A game event: set by the dialogue command SetGameEvent (a game variable with the value 0).");
    } else if (kind == "skill") {
        ch |= json_choice("Skill", a, "skill", skill_names(app.game.get()));
        int v = a.value("value", 1);
        if (ImGui::InputInt("Value", &v)) {
            a["value"] = v;
            ch = true;
        }
        ch |= json_choice("Compare", a, "op", kOps);
        who("who", "Who");
    } else if (kind == "guild") {
        ch |= json_choice("Guild", a, "guild", guild_names());
        who("who", "Who");
    } else {
        std::string w = a.value("who", std::string());
        if (combo("NPC", w, npcs, false)) {
            a["who"] = w;
            ch = true;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150);
        if (ImGui::InputTextWithHint("##who", "or a unique name", &w)) {
            a["who"] = w;
            ch = true;
        }
    }
    return ch;
}

bool condition_list(const char* id, json& cond, App& app, const char* none_label) {
    bool ch = false;
    ImGui::PushID(id);
    // the editable form: a mode and a list of single conditions
    int mode = 0;  // 0 none, 1 all, 2 any
    std::vector<json> items;  // { kind, args }
    bool nested = false;
    auto add_leaf = [&](const json& c) {
        std::string kind;
        json args;
        if (leaf_args(c, kind, args)) items.push_back(json{{"kind", kind}, {"args", args}});
        else nested = true;
    };
    if (cond.is_array() && !cond.empty() && cond[0].is_string()) {
        const std::string k = cond[0].get<std::string>();
        if ((k == "all" || k == "any") && cond.size() > 1 && cond[1].is_array()) {
            mode = k == "all" ? 1 : 2;
            for (auto& c : cond[1]) add_leaf(c);
        } else {
            mode = 1;
            add_leaf(cond);
        }
    }
    if (nested) {
        ImGui::TextWrapped("%s", describe_condition(cond).c_str());
        const bool replace = ImGui::SmallButton("Replace by a simple list (drops this condition)");
        if (replace) cond = nullptr;
        ImGui::PopID();
        return replace;
    }
    static const char* kModes[] = {nullptr, "all of these hold", "any of these holds"};
    const char* cur = mode == 0 ? none_label : kModes[mode];
    bool rebuild = false;
    ImGui::SetNextItemWidth(220);
    if (ImGui::BeginCombo("##mode", cur)) {
        if (ImGui::Selectable(none_label, mode == 0)) {
            mode = 0;
            rebuild = true;
        }
        for (int m = 1; m <= 2; ++m)
            if (ImGui::Selectable(kModes[m], mode == m)) {
                if (mode == 0 && items.empty()) items.push_back(json{{"kind", "quest"}, {"args", json{{"status", "Success"}}}});
                mode = m;
                rebuild = true;
            }
        ImGui::EndCombo();
    }
    if (mode != 0) {
        int remove = -1;
        for (size_t i = 0; i < items.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            std::vector<std::string> errs;
            const json leaf = leaf_condition(items[i].value("kind", std::string()), items[i].value("args", json::object()), errs);
            const std::string text = leaf.is_null() ? std::string("?") : describe_condition(leaf);
            const bool open = ImGui::TreeNodeEx("##c", ImGuiTreeNodeFlags_SpanAvailWidth | (items.size() == 1 ? ImGuiTreeNodeFlags_DefaultOpen : 0), "%s%s", text.c_str(),
                                                errs.empty() ? "" : "  (incomplete)");
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10);
            if (ImGui::SmallButton("x")) remove = static_cast<int>(i);
            if (open) {
                if (condition_args(app, items[i])) rebuild = true;
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (remove >= 0) {
            items.erase(items.begin() + remove);
            if (items.empty()) mode = 0;
            rebuild = true;
        }
        if (ImGui::SmallButton("+ condition")) {
            items.push_back(json{{"kind", "quest"}, {"args", json{{"status", "Success"}}}});
            rebuild = true;
        }
    }
    if (rebuild) {
        std::vector<std::string> errs;
        json list = json::array();
        for (auto& it : items) {
            json leaf = leaf_condition(it.value("kind", std::string()), it.value("args", json::object()), errs);
            if (!leaf.is_null()) list.push_back(leaf);
        }
        if (mode == 0 || list.empty()) cond = nullptr;
        else if (list.size() == 1) cond = list[0];
        else cond = json::array({mode == 1 ? "all" : "any", list});
        ch = true;
    }
    ImGui::PopID();
    return ch;
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
