// Browser panel: the game's templates by group, search by name and label, filter by kind, the property inspector and the actions the editors offer
// ("New item based on this", "Edit this item", ...). "Include mod" lists the project's new items, recipes, NPC templates and placed NPCs too. Every
// row is a drag source (app/dragdrop.h): into template fields, inventories and the World view.
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cstring>
#include <map>

#include "app/dragdrop.h"
#include "app/editor_util.h"
#include "app/icons.h"
#include "app/panels/panels.h"
#include "app/pickers.h"
#include "app/render/mesh_preview.h"
#include "app/widgets.h"
#include "core/i18n.h"

namespace mk {

namespace {

bool icontains(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
                          [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
    return it != hay.end();
}

std::string show_value(const GameData& g, const p2m::Val& v, const std::string& type) {
    std::string e = g.enum_name(type, v);
    if (!e.empty()) return e;
    switch (v.k) {
        case p2m::Val::None: return "-";
        case p2m::Val::Int: return std::to_string(v.i);
        case p2m::Val::Bool: return v.i ? "true" : "false";
        case p2m::Val::Float: return p2m::fmt_g(v.d);
        case p2m::Val::Str: return v.s.size() > 80 ? v.s.substr(0, 80) + "..." : v.s;
        case p2m::Val::Vec: {
            std::string s;
            for (size_t i = 0; i < v.v.size(); ++i) s += (i ? ", " : "") + p2m::fmt_g(v.v[i]);
            return s;
        }
    }
    return "?";
}

struct Node {
    std::map<std::string, Node> kids;
    std::vector<const TemplateInfo*> items;
    size_t total = 0;
};

// an object the project makes, as the Browser lists it
struct ModEntry {
    std::string uid, name, label, base, mesh;
    const char* group;  // "Items", "Recipes", "NPC templates", "Placed NPCs"
    const char* panel;  // the editor panel
    TplKind kind;
    bool entity = false;  // a placed NPC
};

std::vector<ModEntry> mod_entries(const App& app) {
    std::vector<ModEntry> v;
    const Project& p = app.project;
    auto first = [](const Text& t) { return t.empty() ? std::string() : t.begin()->second; };
    for (auto& i : p.items)
        if (i.mode == Mode::New && !i.name.empty()) v.push_back({i.uid, i.name, first(i.label), i.base, i.mesh.value_or(""), N_("Items"), kPanelItems, TplKind::Item});
    for (auto& r : p.recipes)
        if (r.mode == Mode::New && !r.name.empty())
            v.push_back({r.uid, r.name, first(r.label), r.base, r.mesh.value_or(""), N_("Recipes"), kPanelRecipes, TplKind::Recipe});
    for (auto& t : p.npc_templates)
        if (t.mode == Mode::New && !t.name.empty())
            v.push_back({t.uid, t.name, first(t.label), t.base, "", N_("NPC templates"), kPanelNpcs, ui::kind_of(app, t.name)});
    for (auto& n : p.npcs)
        if (!n.name.empty()) v.push_back({n.uid, n.name, first(n.label), n.template_name, "", N_("Placed NPCs"), kPanelNpcs, TplKind::Npc, true});
    return v;
}

class BrowserPanel : public Panel {
public:
    const char* title() const override { return kPanelBrowser; }
    void on_project_changed(App& app) override {
        if (!app.game || (selected_ && !app.game->tpl(selected_name_))) selected_ = nullptr;
        if (app.game && !selected_name_.empty()) selected_ = app.game->tpl(selected_name_);
        root_ = Node{};
        game_ = nullptr;
    }
    bool select(App& app, const std::string& name) override {
        if (!app.game) return false;
        const TemplateInfo* t = app.game->tpl(name);
        if (!t) return false;
        selected_ = t;
        selected_name_ = t->name;
        sel_mod_.clear();
        filter_ = t->name;
        app.focus_panel(title());
        return true;
    }

    void draw(App& app) override {
        if (!ImGui::Begin(ui::panel_label(title()).c_str(), &open)) {
            ImGui::End();
            return;
        }
        const GameData* g = app.game.get();
        if (!g) {
            ImGui::TextWrapped("%s", app.game_loading() ? tr("Reading the game data...") : tr("No game data. Choose the game's data directory in Settings."));
            if (!app.game_loading() && ImGui::Button(tr("Open Settings"))) app.focus_panel(kPanelSettings);
            ImGui::End();
            return;
        }
        if (game_ != g) rebuild(*g);
        mods_ = include_mod_ ? mod_entries(app) : std::vector<ModEntry>{};
        if (!sel_mod_.empty() && std::none_of(mods_.begin(), mods_.end(), [&](const ModEntry& m) { return m.uid == sel_mod_; })) sel_mod_.clear();
        ImGui::Checkbox(tr("Include mod"), &include_mod_);
        ui::help(tr("Also list what the mod makes: its new items, recipes, NPC templates and placed NPCs (group Mod). Drag any row into a template field, "
                 "an inventory or loot list, or the World view."));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-140);
        ImGui::InputTextWithHint("##filter", tr("search name or label"), &filter_);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##kind", kind_filter_ < 0 ? tr("All kinds") : kind_label(static_cast<TplKind>(kind_filter_)))) {
            if (ImGui::Selectable(tr("All kinds"), kind_filter_ < 0)) kind_filter_ = -1;
            for (int k = 0; k < kTplKindCount; ++k)
                if (ImGui::Selectable(kind_label(static_cast<TplKind>(k)), kind_filter_ == k)) kind_filter_ = k;
            ImGui::EndCombo();
        }
        const float list_h = std::max(120.0f, ImGui::GetContentRegionAvail().y * 0.45f);
        ImGui::BeginChild("##tree", ImVec2(0, list_h), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
        if (!filter_.empty()) draw_flat(app, *g);
        else draw_tree(app, *g);
        ImGui::EndChild();
        if (const ModEntry* m = selected_mod()) draw_mod_inspector(app, *g, *m);
        else draw_inspector(app, *g);
        ImGui::End();
    }

private:
    bool passes(const GameData& g, const TemplateInfo& t) const {
        if (kind_filter_ >= 0 && static_cast<int>(t.kind) != kind_filter_) return false;
        return icontains(t.name, filter_) || icontains(g.label(t.name), filter_) || icontains(t.group, filter_);
    }
    bool passes(const ModEntry& m) const {
        if (kind_filter_ >= 0 && static_cast<int>(m.kind) != kind_filter_) return false;
        return icontains(m.name, filter_) || icontains(m.label, filter_) || icontains(std::string("Mod/") + m.group, filter_);
    }
    const ModEntry* selected_mod() const {
        for (auto& m : mods_)
            if (m.uid == sel_mod_) return &m;
        return nullptr;
    }
    void rebuild(const GameData& g) {
        game_ = &g;
        root_ = Node{};
        for (auto& t : g.templates()) {
            Node* n = &root_;
            ++n->total;
            for (size_t a = 0, b; a < t->group.size(); a = b + 1) {
                b = t->group.find('/', a);
                if (b == std::string::npos) b = t->group.size();
                n = &n->kids[t->group.substr(a, b - a)];
                ++n->total;
            }
            n->items.push_back(t.get());
        }
    }
    void row(const GameData& g, const TemplateInfo& t) {
        if (t.kind == TplKind::Item) {
            ui::icon(t.icon, 20.0f);
            ImGui::SameLine();
        }
        if (ImGui::Selectable(t.name.c_str(), selected_ == &t && sel_mod_.empty())) {
            selected_ = &t;
            selected_name_ = t.name;
            sel_mod_.clear();
        }
        const std::string lbl = g.label(t.name);
        ui::drag_source(t.name, t.kind, false, false, lbl);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.55f);
        ImGui::TextDisabled("%s", lbl.empty() ? kind_label(t.kind) : lbl.c_str());
    }
    void mod_row(const ModEntry& m) {
        ImGui::PushID(m.uid.c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.8f, 1.0f, 1.0f));
        if (ImGui::Selectable(m.name.c_str(), sel_mod_ == m.uid)) sel_mod_ = m.uid;
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) open_in_editor_ = m.uid;
        ui::drag_source(m.name, m.kind, m.entity, true, m.label);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.55f);
        ImGui::TextDisabled("%s", !m.label.empty() ? m.label.c_str() : m.entity ? tr("placed NPC") : kind_label(m.kind));
        ImGui::PopID();
    }
    void draw_mod_node() {
        size_t c = 0;
        for (auto& m : mods_) c += passes(m);
        if (!c) return;
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        if (!ImGui::TreeNodeEx("##mod", ImGuiTreeNodeFlags_SpanAvailWidth, tr("Mod  (%zu)"), c)) return;
        for (const char* grp : {N_("Items"), N_("Recipes"), N_("NPC templates"), N_("Placed NPCs")}) {
            size_t n = 0;
            for (auto& m : mods_) n += std::strcmp(m.group, grp) == 0 && passes(m);
            if (!n) continue;
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::TreeNodeEx(grp, ImGuiTreeNodeFlags_SpanAvailWidth, "%s  (%zu)", tr(grp), n)) {
                for (auto& m : mods_)
                    if (std::strcmp(m.group, grp) == 0 && passes(m)) mod_row(m);
                ImGui::TreePop();
            }
        }
        ImGui::TreePop();
    }
    void draw_flat(App& app, const GameData& g) {
        for (auto& m : mods_)
            if (passes(m)) mod_row(m);
        flush_open(app);
        std::vector<const TemplateInfo*> list;
        for (auto& t : g.templates())
            if (passes(g, *t)) list.push_back(t.get());
        ImGuiListClipper clip;
        clip.Begin(static_cast<int>(list.size()));
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                ImGui::PushID(list[i]);
                row(g, *list[i]);
                ImGui::PopID();
            }
    }
    void draw_tree(App& app, const GameData& g) {
        draw_mod_node();
        flush_open(app);
        draw_node(g, root_);
    }
    // a double click on a mod row opens it in its editor
    void flush_open(App& app) {
        if (open_in_editor_.empty()) return;
        for (auto& m : mods_)
            if (m.uid == open_in_editor_) {
                const std::string panel = m.panel, uid = m.uid;
                app.defer([&app, panel, uid] { app.select_in(panel, uid); });
            }
        open_in_editor_.clear();
    }
    size_t count(const GameData& g, const Node& n) const {
        size_t c = 0;
        for (auto* t : n.items) c += passes(g, *t);
        for (auto& [name, kid] : n.kids) c += count(g, kid);
        return c;
    }
    void draw_node(const GameData& g, const Node& n) {
        for (auto& [name, kid] : n.kids) {
            const size_t c = kind_filter_ >= 0 ? count(g, kid) : kid.total;
            if (!c) continue;
            if (ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth, "%s  (%zu)", name.c_str(), c)) {
                draw_node(g, kid);
                ImGui::TreePop();
            }
        }
        for (auto* t : n.items) {
            if (!passes(g, *t)) continue;
            ImGui::PushID(t);
            row(g, *t);
            ImGui::PopID();
        }
    }

    void draw_inspector(App& app, const GameData& g) {
        const TemplateInfo* t = selected_;
        if (!t) {
            ImGui::TextDisabled("%s", tr("Select a template."));
            return;
        }
        ImGui::BeginChild("##inspector");
        ImGui::TextUnformatted(t->name.c_str());
        const std::string lbl = g.label(t->name);
        if (!lbl.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("\"%s\"", lbl.c_str());
        }
        ImGui::TextDisabled(tr("%s   %s   %s.tple"), kind_label(t->kind), t->group.c_str(), t->file.c_str());
        if (!t->mesh.empty()) ImGui::TextDisabled(tr("mesh %s"), t->mesh.c_str());
        if (!t->icon.empty()) {
            ui::icon(t->icon, 48.0f);
            ImGui::SameLine();
            ImGui::TextDisabled(tr("icon %s"), t->icon.c_str());
        }
        const std::string desc = g.text("ITEMDESC_" + t->name);
        if (!desc.empty() && desc != "-") ImGui::TextWrapped("%s", desc.c_str());
        if (!t->mesh.empty()) preview_.draw(app, t->mesh, ImVec2(std::min(ImGui::GetContentRegionAvail().x, 300.0f), 160));
        // actions of the editors
        bool first = true;
        for (auto& a : app.template_actions) {
            if (!a.kinds.empty() && std::find(a.kinds.begin(), a.kinds.end(), t->kind) == a.kinds.end()) continue;
            if (!first) ImGui::SameLine();
            first = false;
            if (ImGui::Button(tr(a.label.c_str()))) {
                const TemplateAction* act = &a;
                app.defer([&app, act, t] { act->run(app, *t); });
            }
        }
        draw_props(g, *t);
        ImGui::EndChild();
    }

    void draw_mod_inspector(App& app, const GameData& g, const ModEntry& m) {
        ImGui::BeginChild("##inspector");
        ImGui::TextColored(ImVec4(0.55f, 0.8f, 1.0f, 1.0f), "%s", m.name.c_str());
        if (!m.label.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("\"%s\"", m.label.c_str());
        }
        const TemplateInfo* base = m.base.empty() ? nullptr : g.tpl(m.base);
        if (m.entity) ImGui::TextDisabled(tr("placed NPC of the mod, template %s"), m.base.c_str());
        else ImGui::TextDisabled(tr("%s new in the mod%s%s"), kind_label(m.kind), m.base.empty() ? "" : tr(", based on "), m.base.c_str());
        if (ImGui::Button(trf("Open in {0}", {tr(m.panel)}).c_str())) {
            const std::string panel = m.panel, uid = m.uid;
            app.defer([&app, panel, uid] { app.select_in(panel, uid); });
        }
        if (base) {
            ImGui::SameLine();
            if (ImGui::Button(m.entity ? tr("Template in Browser") : tr("Base in Browser"))) {
                selected_ = base;
                selected_name_ = base->name;
                sel_mod_.clear();
            }
        }
        ImGui::TextDisabled("%s", tr("Drag the name from the list into a template field, an inventory or the World view."));
        const std::string mesh = !m.mesh.empty() ? m.mesh : base ? base->mesh : std::string();
        if (!mesh.empty()) preview_.draw(app, mesh, ImVec2(std::min(ImGui::GetContentRegionAvail().x, 300.0f), 160));
        if (base) {
            ImGui::SeparatorText(m.entity ? tr("Properties of the template") : tr("Properties of the base (the mod's changes are in its editor)"));
            draw_props(g, *base);
        }
        ImGui::EndChild();
    }

    void draw_props(const GameData& g, const TemplateInfo& tpl) {
        const TemplateInfo* t = &tpl;
        ImGui::InputTextWithHint("##pfilter", tr("filter properties"), &prop_filter_);
        if (ImGui::BeginTable("##props", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn(tr("Property"), ImGuiTableColumnFlags_WidthStretch, 3);
            ImGui::TableSetupColumn(tr("Value"), ImGuiTableColumnFlags_WidthStretch, 3);
            ImGui::TableSetupColumn(tr("Type"), ImGuiTableColumnFlags_WidthStretch, 2);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < t->props.size(); ++i) {
                const std::string& k = t->props.keys[i];
                const std::string v = show_value(g, t->props.vals[i], t->props.types[i]);
                if (!icontains(k, prop_filter_) && !icontains(v, prop_filter_)) continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(k.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(v.c_str());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", t->props.types[i].c_str());
            }
            ImGui::EndTable();
        }
    }

    const GameData* game_ = nullptr;
    Node root_;
    const TemplateInfo* selected_ = nullptr;
    MeshPreview preview_;
    std::string selected_name_, filter_, prop_filter_;
    int kind_filter_ = -1;
    bool include_mod_ = true;
    std::vector<ModEntry> mods_;  // rebuilt every frame (the project changes under the panel)
    std::string sel_mod_, open_in_editor_;
};

}  // namespace

std::unique_ptr<Panel> make_browser_panel() { return std::make_unique<BrowserPanel>(); }

}  // namespace mk
