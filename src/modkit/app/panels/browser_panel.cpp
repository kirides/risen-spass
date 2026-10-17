// Browser panel: the game's templates by group, search by name and label, filter by kind, the property inspector and the actions the editors offer
// ("New item based on this", "Edit this item", ...).
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <map>

#include "app/panels/panels.h"
#include "app/widgets.h"

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
        filter_ = t->name;
        app.focus_panel(title());
        return true;
    }

    void draw(App& app) override {
        if (!ImGui::Begin(title(), &open)) {
            ImGui::End();
            return;
        }
        const GameData* g = app.game.get();
        if (!g) {
            ImGui::TextWrapped(app.game_loading() ? "Reading the game data..." : "No game data. Choose the game's data directory in Settings.");
            if (!app.game_loading() && ImGui::Button("Open Settings")) app.focus_panel(kPanelSettings);
            ImGui::End();
            return;
        }
        if (game_ != g) rebuild(*g);
        ImGui::SetNextItemWidth(-140);
        ImGui::InputTextWithHint("##filter", "search name or label", &filter_);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##kind", kind_filter_ < 0 ? "All kinds" : kind_name(static_cast<TplKind>(kind_filter_)))) {
            if (ImGui::Selectable("All kinds", kind_filter_ < 0)) kind_filter_ = -1;
            for (int k = 0; k < kTplKindCount; ++k)
                if (ImGui::Selectable(kind_name(static_cast<TplKind>(k)), kind_filter_ == k)) kind_filter_ = k;
            ImGui::EndCombo();
        }
        const float list_h = std::max(120.0f, ImGui::GetContentRegionAvail().y * 0.45f);
        ImGui::BeginChild("##tree", ImVec2(0, list_h), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
        if (!filter_.empty()) draw_flat(*g);
        else draw_tree(*g);
        ImGui::EndChild();
        draw_inspector(app, *g);
        ImGui::End();
    }

private:
    bool passes(const GameData& g, const TemplateInfo& t) const {
        if (kind_filter_ >= 0 && static_cast<int>(t.kind) != kind_filter_) return false;
        return icontains(t.name, filter_) || icontains(g.label(t.name), filter_) || icontains(t.group, filter_);
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
        if (ImGui::Selectable(t.name.c_str(), selected_ == &t)) {
            selected_ = &t;
            selected_name_ = t.name;
        }
        const std::string lbl = g.label(t.name);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.55f);
        ImGui::TextDisabled("%s", lbl.empty() ? kind_name(t.kind) : lbl.c_str());
    }
    void draw_flat(const GameData& g) {
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
    void draw_tree(const GameData& g) { draw_node(g, root_); }
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
            ImGui::TextDisabled("Select a template.");
            return;
        }
        ImGui::BeginChild("##inspector");
        ImGui::TextUnformatted(t->name.c_str());
        const std::string lbl = g.label(t->name);
        if (!lbl.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("\"%s\"", lbl.c_str());
        }
        ImGui::TextDisabled("%s   %s   %s.tple", kind_name(t->kind), t->group.c_str(), t->file.c_str());
        if (!t->mesh.empty()) ImGui::TextDisabled("mesh %s", t->mesh.c_str());
        if (!t->icon.empty()) ImGui::TextDisabled("icon %s", t->icon.c_str());
        const std::string desc = g.text("ITEMDESC_" + t->name);
        if (!desc.empty() && desc != "-") ImGui::TextWrapped("%s", desc.c_str());
        // actions of the editors
        bool first = true;
        for (auto& a : app.template_actions) {
            if (!a.kinds.empty() && std::find(a.kinds.begin(), a.kinds.end(), t->kind) == a.kinds.end()) continue;
            if (!first) ImGui::SameLine();
            first = false;
            if (ImGui::Button(a.label.c_str())) {
                const TemplateAction* act = &a;
                app.defer([&app, act, t] { act->run(app, *t); });
            }
        }
        ImGui::InputTextWithHint("##pfilter", "filter properties", &prop_filter_);
        if (ImGui::BeginTable("##props", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthStretch, 3);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 3);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 2);
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
        ImGui::EndChild();
    }

    const GameData* game_ = nullptr;
    Node root_;
    const TemplateInfo* selected_ = nullptr;
    std::string selected_name_, filter_, prop_filter_;
    int kind_filter_ = -1;
};

}  // namespace

std::unique_ptr<Panel> make_browser_panel() { return std::make_unique<BrowserPanel>(); }

}  // namespace mk
