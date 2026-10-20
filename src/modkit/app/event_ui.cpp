#include "app/event_ui.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <map>

#include "app/app.h"
#include "app/editor_util.h"
#include "app/pickers.h"
#include "app/widgets.h"
#include "core/catalog.h"
#include "core/i18n.h"

namespace mk::ui {

namespace {

bool lang_json(const char* label, json& t, const std::vector<std::string>& langs) {
    Text tx;
    if (t.is_object())
        for (auto& [k, v] : t.items())
            if (v.is_string()) tx[k] = v.get<std::string>();
    if (!ui::lang_text(label, tx, langs, true)) return false;
    t = json::object();
    for (auto& [k, v] : tx) t[k] = v;
    return true;
}

std::vector<std::string> project_info_options(const Project& p) {
    std::vector<std::string> v;
    for (auto& d : p.dialogs)
        for (auto& n : d.graph.nodes)
            if (n.type == "option") v.push_back(info_name(p, n));
    return v;
}

bool field(App& app, const EvFieldDef& f, json& d) {
    bool ch = false;
    const char* label = tr(f.label);  // the tables in core/events.cpp hold the English texts
    const char* help = f.help ? tr(f.help) : nullptr;
    const std::string key = f.key;
    switch (f.kind) {
        case EvField::Int: {
            int v = d.contains(key) && d[key].is_number() ? static_cast<int>(d[key].get<double>()) : (f.def.is_number() ? f.def.get<int>() : 0);
            if (ImGui::InputInt(label, &v)) {
                d[key] = static_cast<int>(std::clamp<double>(v, f.min, f.max));
                ch = true;
            }
            break;
        }
        case EvField::Number: {
            double v = d.contains(key) && d[key].is_number() ? d[key].get<double>() : (f.def.is_number() ? f.def.get<double>() : 0);
            if (ImGui::InputDouble(label, &v, 0, 0, "%.2f")) {
                d[key] = std::isfinite(v) ? std::clamp(v, f.min, f.max) : 0.0;
                ch = true;
            }
            break;
        }
        case EvField::Bool: {
            bool v = d.value(key, f.def.is_boolean() ? f.def.get<bool>() : false);
            if (ImGui::Checkbox(label, &v)) {
                d[key] = v;
                ch = true;
            }
            break;
        }
        case EvField::Combo: {
            std::string v = d.value(key, std::string());
            if (ui::combo(label, v, f.options, false)) {
                d[key] = v;
                ch = true;
            }
            break;
        }
        case EvField::Text:
            ch |= lang_json(label, d[key], app.project.mod.languages);
            break;
        case EvField::Item: {
            std::string v = d.value(key, std::string());
            if (ui::template_picker(label, v, app.game.get(), {TplKind::Item, TplKind::Recipe}, ui::project_item_names(app.project))) {
                d[key] = v;
                ch = true;
            }
            break;
        }
        case EvField::Quest: {
            std::string v = d.value(key, std::string());
            if (ui::quest_picker(label, v, app)) {
                d[key] = v;
                ch = true;
            }
            break;
        }
        case EvField::Target:
        case EvField::Template: {
            std::string v = d.value(key, std::string());
            std::vector<std::string> extra;
            if (f.kind == EvField::Target)
                for (auto& x : app.project.npcs) extra.push_back(x.name);
            for (auto& x : app.project.npc_templates)
                if (x.mode == Mode::New) extra.push_back(x.name);
            if (ui::template_picker(label, v, app.game.get(), {TplKind::Npc, TplKind::Monster}, extra)) {
                d[key] = v;
                ch = true;
            }
            if (f.kind == EvField::Target) ui::help(tr("A template (every wolf) or the unique name of one NPC."));
            break;
        }
        case EvField::Info: {
            std::string v = d.value(key, std::string());
            bool c = ui::combo(label, v, project_info_options(app.project), false);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120);
            c |= ImGui::InputText("##free", &v);
            if (c) {
                d[key] = v;
                ch = true;
            }
            break;
        }
        case EvField::Layer: {
            std::string v = d.value(key, std::string());
            std::vector<std::string> names;
            for (auto& l : app.project.layers) names.push_back(l.name);
            if (ui::combo(label, v, names, true)) {
                d[key] = v;
                ch = true;
            }
            if (names.empty()) ImGui::TextDisabled("%s", tr("No layers yet: make one in the Layers panel."));
            break;
        }
        case EvField::Sector: {
            std::string v = d.value(key, std::string());
            if (ImGui::BeginCombo(label, v.empty() ? tr("(choose)") : v.c_str())) {
                for (auto& s : event_stock_sectors(app)) {
                    const std::string row = s.name + (s.enabled_on_startup ? "" : tr("   (starts off)"));
                    if (ImGui::Selectable(row.c_str(), s.name == v)) {
                        d[key] = s.name;
                        ch = true;
                    }
                }
                ImGui::EndCombo();
            }
            std::string free = v;
            if (ImGui::InputText("##sector_free", &free)) {
                d[key] = free;
                ch = true;
            }
            ui::help(tr("A sector of the game's world by file base name; the ones that start off are switched by the story (docs/RE.md section 55)."));
            break;
        }
        case EvField::String: {
            std::string v = d.value(key, std::string());
            const bool hint = help && std::string(f.help).size() < 40;
            if (hint ? ImGui::InputTextWithHint(label, help, &v) : ImGui::InputText(label, &v)) {
                d[key] = v;
                ch = true;
            }
            break;
        }
    }
    if (help && f.kind != EvField::Target && f.kind != EvField::Sector && !(f.kind == EvField::String && std::string(f.help).size() < 40)) ui::help(help);
    return ch;
}

}  // namespace

const std::vector<StockSector>& event_stock_sectors(App& app) {
    static const GameData* cached_for = nullptr;
    static std::vector<StockSector> cache;
    if (cached_for != app.game.get()) {
        cached_for = app.game.get();
        cache = app.game ? stock_sectors(*app.game) : std::vector<StockSector>{};
    }
    return cache;
}

bool event_fields(App& app, const std::vector<EvFieldDef>& fields, json& d) {
    bool ch = false;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (!event_field_visible(fields[i], d)) continue;
        ImGui::PushID(static_cast<int>(i));
        ch |= field(app, fields[i], d);
        ImGui::PopID();
    }
    return ch;
}

bool event_condition_set(App& app, json& d, bool show_mode) {
    bool ch = false;
    if (!d.is_object()) d = event_default_condition_set();
    if (show_mode) {
        std::string mode = d.value("mode", std::string("all"));
        if (ui::combo(tr("Combine"), mode, {"all", "any"}, false)) {
            d["mode"] = mode;
            ch = true;
        }
        bool neg = d.value("negate", false);
        if (ImGui::Checkbox(tr("Negate the result"), &neg)) {
            d["negate"] = neg;
            ch = true;
        }
    }
    if (!d.contains("tests") || !d["tests"].is_array()) d["tests"] = json::array();
    json& tests = d["tests"];
    int remove = -1;
    for (size_t i = 0; i < tests.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        json& t = tests[i];
        const EvTestSpec* spec = event_test(t.value("kind", std::string()));
        ImGui::Separator();
        ImGui::TextUnformatted(spec ? tr(spec->title) : "?");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20);
        if (ImGui::SmallButton("x")) remove = static_cast<int>(i);
        if (spec) ch |= event_fields(app, spec->fields, t);
        ImGui::PopID();
    }
    if (remove >= 0) {
        tests.erase(tests.begin() + remove);
        ch = true;
    }
    ImGui::Separator();
    if (ImGui::BeginCombo("##add_check", tr("+ Add a check..."))) {
        for (auto& s : event_tests())
            if (ImGui::Selectable(tr(s.title))) {
                tests.push_back(event_default_test(s.kind));
                ch = true;
            }
        ImGui::EndCombo();
    }
    return ch;
}

}  // namespace mk::ui
