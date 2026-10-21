// Items panel: changes of shipped items, new items (clones of a base item) and item groups (relative settings for many items).
// Exported as the items / new_items / item_groups sections of risen.balance.install (data/balance.lua).
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>

#include "app/panels/panels.h"
#include "app/dragdrop.h"
#include "app/editor_util.h"
#include "app/icons.h"
#include "app/pickers.h"
#include "app/prop_editor.h"
#include "app/render/mesh_preview.h"
#include "app/widgets.h"
#include "core/catalog.h"
#include "core/i18n.h"
#include "core/image.h"
#include "pack2mod/util.h"
#include "core/validate.h"

namespace mk {

namespace {

// the requirement / modifier list of an item: "replace the list" + rows in the engine's order (up / down), the modifier's mode (add, set, max, percent), remove,
// and an add combo. The list is stored as maps plus the order and the modes (ItemDef), so a plain list stays a plain { Skill = amount } table in the export.
bool skill_list(const char* label, ItemDef& it, bool modifiers, const IntMap& shipped, const std::vector<std::string>& skills, const char* help_text) {
    std::optional<IntMap>& m = modifiers ? it.modifies : it.requirements;
    bool changed = false;
    ImGui::PushID(label);
    bool on = m.has_value();
    if (ImGui::Checkbox(label, &on)) {
        if (on) {
            m = shipped;
            (modifiers ? it.modifier_order : it.requirement_order).clear();
            if (modifiers) it.modifier_modes.clear();
        } else {
            m.reset();
            (modifiers ? it.modifier_order : it.requirement_order).clear();
            if (modifiers) it.modifier_modes.clear();
        }
        changed = true;
    }
    ui::help(help_text);
    if (m) {
        std::vector<SkillEntry> list = modifiers ? modifier_entries(it) : requirement_entries(it);
        static const std::vector<std::string> modes = {"add", "set", "max", "percent"};
        int remove = -1, up = -1;
        bool edited = false;
        ImGui::Indent();
        if (ImGui::BeginTable("##skills", modifiers ? 5 : 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupColumn(tr("skill"), ImGuiTableColumnFlags_WidthStretch, 3);
            ImGui::TableSetupColumn(tr("amount"), ImGuiTableColumnFlags_WidthStretch, 2);
            if (modifiers) ImGui::TableSetupColumn(tr("mode"), ImGuiTableColumnFlags_WidthStretch, 2);
            ImGui::TableSetupColumn(tr("order"), ImGuiTableColumnFlags_WidthFixed, 22);
            ImGui::TableSetupColumn("x", ImGuiTableColumnFlags_WidthFixed, 24);
            for (size_t i = 0; i < list.size(); ++i) {
                SkillEntry& e = list[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(e.skill.c_str());
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::InputInt("##v", &e.amount)) edited = true;
                if (modifiers) {
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ui::combo("##mode", e.mode, modes, false)) edited = true;
                }
                ImGui::TableNextColumn();
                if (i > 0 && ImGui::SmallButton("^")) up = static_cast<int>(i);
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("x")) remove = static_cast<int>(i);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (up > 0) {
            std::swap(list[static_cast<size_t>(up)], list[static_cast<size_t>(up) - 1]);
            edited = true;
        }
        if (remove >= 0) {
            list.erase(list.begin() + remove);
            edited = true;
        }
        ImGui::SetNextItemWidth(200);
        if (ImGui::BeginCombo("##add", "add...", ImGuiComboFlags_HeightLarge)) {
            for (auto& s : skills) {
                if (std::any_of(list.begin(), list.end(), [&](const SkillEntry& e) { return e.skill == s; })) continue;
                if (ImGui::Selectable(s.c_str())) {
                    list.push_back({s, 1, "add"});
                    edited = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::Unindent();
        if (edited) {
            if (modifiers) set_modifier_entries(it, list);
            else set_requirement_entries(it, list);
            changed = true;
        }
    } else {
        std::string s;
        for (auto& [k, v] : shipped) s += (s.empty() ? "" : ", ") + k + " " + std::to_string(v);
        ImGui::SameLine();
        ImGui::TextDisabled(tr("shipped: %s"), s.empty() ? "none" : s.c_str());
    }
    ImGui::PopID();
    return changed;
}

}  // namespace

namespace {

class ItemsPanel : public Panel {
public:
    const char* title() const override { return kPanelItems; }

    void attach(App& app) override {
        app.template_actions.push_back({N_("New item based on this"), {TplKind::Item, TplKind::Recipe}, [this](App& a, const TemplateInfo& t) { add_new(a, t.name); }});
        app.template_actions.push_back({N_("Change this item"), {TplKind::Item, TplKind::Recipe}, [this](App& a, const TemplateInfo& t) { add_edit(a, t.name); }});
    }
    void on_project_changed(App& app) override {
        if (!find(app.project, sel_) && !group(app.project, sel_)) sel_.clear();
    }
    bool select(App& app, const std::string& key) override {
        for (auto& i : app.project.items)
            if (i.name == key || i.uid == key) {
                sel_ = i.uid;
                app.focus_panel(title());
                return true;
            }
        for (auto& g : app.project.item_groups)
            if (g.uid == key) {
                sel_ = g.uid;
                app.focus_panel(title());
                return true;
            }
        return false;
    }

    void add_new(App& app, const std::string& base) {
        ItemDef d;
        d.uid = new_uid();
        d.mode = Mode::New;
        d.base = base;
        d.name = ui::unique_template_name(app.project, app.game.get(), ui::name_prefix(app.project) + "_" + (base.empty() ? std::string("Item") : base));
        app.project.items.push_back(d);
        sel_ = d.uid;
        app.touch();
        app.focus_panel(title());
    }
    void add_edit(App& app, const std::string& name) {
        for (auto& i : app.project.items)
            if (i.mode == Mode::Edit && i.name == name) {
                sel_ = i.uid;
                app.focus_panel(title());
                return;
            }
        ItemDef d;
        d.uid = new_uid();
        d.mode = Mode::Edit;
        d.name = name;
        app.project.items.push_back(d);
        sel_ = d.uid;
        app.touch();
        app.focus_panel(title());
    }

    void draw(App& app) override {
        if (!ImGui::Begin(ui::panel_label(title()).c_str(), &open)) {
            ImGui::End();
            return;
        }
        ImGui::BeginChild("##list", ImVec2(300, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
        draw_list(app);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##editor");
        ImGui::PushItemWidth(std::min(520.0f, ImGui::GetContentRegionAvail().x * 0.6f));
        if (ItemDef* it = find(app.project, sel_)) draw_item(app, *it);
        else if (ItemGroup* g = group(app.project, sel_)) draw_group(app, *g);
        else ImGui::TextDisabled("%s", tr("Create an item, change a shipped one (also from the Browser: select an item there), or add an item group."));
        ImGui::PopItemWidth();
        ImGui::EndChild();
        ImGui::End();
    }

private:
    static ItemDef* find(Project& p, const std::string& uid) {
        for (auto& i : p.items)
            if (i.uid == uid) return &i;
        return nullptr;
    }
    static ItemGroup* group(Project& p, const std::string& uid) {
        for (auto& g : p.item_groups)
            if (g.uid == uid) return &g;
        return nullptr;
    }

    void draw_list(App& app) {
        Project& p = app.project;
        if (ImGui::Button(tr("+ New item"))) add_new(app, "It_1H_Sword");
        ImGui::SameLine();
        if (ImGui::Button(tr("+ Change shipped"))) ImGui::OpenPopup("##pick_edit");
        ImGui::SameLine();
        if (ImGui::Button(tr("+ Group"))) {
            ItemGroup g;
            g.uid = new_uid();
            g.group = "Items/Potions";
            g.value_mult = 1.0;
            p.item_groups.push_back(g);
            sel_ = g.uid;
            app.touch();
        }
        if (ImGui::BeginPopup("##pick_edit")) {
            static std::string name;
            ImGui::TextUnformatted(tr("Which shipped item?"));
            ui::template_picker(tr("Item"), name, app.game.get(), {TplKind::Item, TplKind::Recipe}, {}, 280);
            if (ImGui::Button(tr("Add")) && !name.empty()) {
                add_edit(app, name);
                name.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##filter", tr("filter"), &filter_);
        ImGui::Separator();
        std::string remove;
        for (auto& i : p.items) {
            const std::string lbl = ui::label_of(app, i.name);
            if (!filter_.empty() && i.name.find(filter_) == std::string::npos && lbl.find(filter_) == std::string::npos) continue;
            ImGui::PushID(i.uid.c_str());
            ui::item_icon(i.name, 20.0f);
            ImGui::SameLine();
            if (ImGui::Selectable(i.name.empty() ? tr("(unnamed)") : i.name.c_str(), sel_ == i.uid)) sel_ = i.uid;
            ui::drag_source(i.name, TplKind::Item, false, i.mode == Mode::New, lbl);
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem(tr("Duplicate"))) {
                    ItemDef d = i;
                    d.uid = new_uid();
                    if (d.mode == Mode::New) d.name = ui::unique_template_name(p, app.game.get(), d.name);
                    app.defer([&app, d] {
                        app.project.items.push_back(d);
                        app.touch();
                    });
                }
                if (ImGui::MenuItem(tr("Delete"))) remove = i.uid;
                ImGui::EndPopup();
            }
            ImGui::SameLine(190);
            ImGui::TextDisabled("%s", i.mode == Mode::New ? "new" : "change");
            ImGui::PopID();
        }
        if (!p.item_groups.empty()) ImGui::SeparatorText(tr("Item groups"));
        for (auto& g : p.item_groups) {
            ImGui::PushID(g.uid.c_str());
            const std::string t = !g.group.empty() ? g.group : !g.match.empty() ? g.match : "(group)";
            if (ImGui::Selectable(t.c_str(), sel_ == g.uid)) sel_ = g.uid;
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem(tr("Delete"))) remove = g.uid;
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        if (!remove.empty()) {
            p.items.erase(std::remove_if(p.items.begin(), p.items.end(), [&](const ItemDef& i) { return i.uid == remove; }), p.items.end());
            p.item_groups.erase(std::remove_if(p.item_groups.begin(), p.item_groups.end(), [&](const ItemGroup& g) { return g.uid == remove; }),
                                p.item_groups.end());
            app.touch();
        }
    }

    void draw_item(App& app, ItemDef& it) {
        const GameData* g = app.game.get();
        bool ch = false;
        const TemplateInfo* target = g ? g->tpl(it.mode == Mode::New ? it.base : it.name) : nullptr;
        ItemStats shipped;
        if (g && target) shipped = item_stats(*g, *target);
        auto hint_int = [](const std::optional<int>& v) { return v ? std::to_string(*v) : std::string(); };

        if (it.mode == Mode::New) {
            ImGui::TextDisabled("%s", tr("NEW ITEM: a clone of the base with its own name, texts and numbers (risen.template.create)"));
            ch |= ui::input_text(tr("Name"), it.name);
            ui::help(tr("The template name: letters, digits and _, unique, at most 63 characters. It is the key of the item in savegames: keep it once released."));
            if (std::string why = template_name_problem(it.name); !why.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", why.c_str());
            else if (g && g->tpl(it.name)) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", tr("a game template has this name"));
            ch |= ui::template_picker(tr("Base item"), it.base, g, {TplKind::Item, TplKind::Recipe});
            if (g && target) {
                ImGui::SameLine();
                ImGui::TextDisabled("\"%s\"", g->label(target->name).c_str());
            }
        } else {
            ImGui::TextDisabled("%s", tr("CHANGE OF A SHIPPED ITEM: only the values you set are changed"));
            ch |= ui::template_picker(tr("Item"), it.name, g, {TplKind::Item, TplKind::Recipe});
            if (g && target) {
                ImGui::SameLine();
                ImGui::TextDisabled("\"%s\"", g->label(target->name).c_str());
            }
        }

        ui::section(tr("Texts"));
        if (it.mode == Mode::Edit) ImGui::TextDisabled("%s", tr("Leave empty to keep the game's texts."));
        ch |= ui::lang_text(tr("Label"), it.label, app.project.mod.languages);
        ch |= ui::lang_text(tr("Description"), it.description, app.project.mod.languages, true);

        ui::section(tr("Numbers"));
        ch |= ui::opt_int(tr("Value (gold)"), it.value, hint_int(shipped.value).c_str(), shipped.value.value_or(10));
        ch |= ui::opt_int(tr("Damage"), it.damage, hint_int(shipped.damage).c_str(), shipped.damage.value_or(10));
        ui::help(tr("gCDamage_PS.DamageAmount. Melee damage = multiplier * (DamageAmount + the wielder's Strength)."));
        ch |= ui::opt_int(tr("Mana cost"), it.mana_cost, hint_int(shipped.mana_cost).c_str(), shipped.mana_cost.value_or(10));
        ui::help(tr("Runes and scrolls only (gCCastInfo_PS.ManaCost)."));

        ui::section(tr("Requirements and effects"));
        const auto skills = skill_names(g);
        ch |= skill_list(tr("Replace requirements"), it, false, shipped.requirements, skills,
                         tr("What the player needs to equip or use the item (attributes, combat skills, ...). Only the player is held to them. The order of the list is kept (^ moves a row up)."));
        ch |= skill_list(tr("Replace modifiers"), it, true, shipped.modifies, skills,
                         tr("What the item adds while worn or when used: HitPoints for a potion, ProtEdge for armour, ... Modes: add (default), set (to the value), max (to the maximum), percent (of the maximum)."));

        ui::section(tr("Kind"));
        ch |= ui::opt_enum(tr("Use type"), it.use_type, enum_names(g, "gEItemUseType"), shipped.use_type);
        ch |= ui::opt_enum(tr("Category"), it.category, enum_names(g, "gEItemCategory"), shipped.category);

        ui::section(tr("Appearance"));
        {
            bool on = it.mesh.has_value();
            if (ImGui::Checkbox("##mesh_on", &on)) {
                if (on) it.mesh = shipped.mesh;
                else it.mesh.reset();
                ch = true;
            }
            ImGui::SameLine();
            std::string m = it.mesh ? *it.mesh : shipped.mesh;
            ImGui::BeginDisabled(!it.mesh);
            if (ui::asset_picker(tr("Mesh"), m, g, "_xmsh", true) && it.mesh) {
                it.mesh = m;
                ch = true;
            }
            ImGui::EndDisabled();
        }
        {
            bool on = it.icon.has_value();
            if (ImGui::Checkbox("##icon_on", &on)) {
                if (on) it.icon = shipped.icon;
                else it.icon.reset();
                ch = true;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!it.icon);
            std::string ic = it.icon ? *it.icon : shipped.icon;
            if (ui::icon_picker(tr("Icon"), ic, app) && it.icon) {
                it.icon = ic;
                ch = true;
            }
            ImGui::EndDisabled();
        }
        ch |= ui::opt_int(tr("Material switch"), it.material, hint_int(shipped.material).c_str(), shipped.material.value_or(0));
        preview_.draw(app, it.mesh ? *it.mesh : shipped.mesh, ImVec2(260, 180));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Drag to turn"));

        ui::section(tr("Other properties"));
        ImGui::TextDisabled("%s", tr("Any property of the template by name (see docs/ITEM_PROPERTIES.md)."));
        ch |= ui::prop_edits("props", it.properties, g, target);

        ui::section(tr("Notes"));
        ch |= ui::input_multiline("##notes", it.notes, 3);
        if (ch) app.touch();
    }

    void draw_group(App& app, ItemGroup& gr) {
        bool ch = false;
        ImGui::TextDisabled("%s", tr("ITEM GROUP: relative settings for every item a selector matches (computed from the shipped values)"));
        ch |= ui::input_text(tr("Group path"), gr.group);
        ui::help(tr("A template group: Items/Potions, Items/Weapons_Swords, ... (the Browser shows the groups)."));
        ch |= ui::input_text(tr("Name pattern"), gr.match);
        ui::help(tr("A Lua pattern on the template name, e.g. ^It_Po_"));
        ch |= ui::input_text(tr("Exclude pattern"), gr.exclude);
        ImGui::TextUnformatted(tr("Also these items (one per line)"));
        ui::help(tr("Type the names or drag items here from the Browser."));
        ch |= ui::string_list("##names", gr.names);
        if (const ui::DragTemplate* d = ui::drop_target({TplKind::Item, TplKind::Recipe})) {
            if (std::find(gr.names.begin(), gr.names.end(), d->name) == gr.names.end()) gr.names.push_back(d->name);
            ch = true;
        }
        ui::section(tr("Settings"));
        ch |= ui::opt_double(tr("Value factor"), gr.value_mult, nullptr, 1.0);
        ch |= ui::opt_double(tr("Damage factor"), gr.damage_mult, nullptr, 1.0);
        if (app.game) {
            size_t n = 0;
            for (auto& t : app.game->templates()) {
                if (t->kind != TplKind::Item && t->kind != TplKind::Recipe) continue;
                if (!gr.group.empty() && (t->group.size() < gr.group.size() || p2m::lower(t->group.substr(0, gr.group.size())) != p2m::lower(gr.group))) continue;
                ++n;
            }
            ImGui::TextDisabled(tr("The group path selects %zu items (patterns are evaluated by the layer)."), n);
        }
        if (ch) app.touch();
    }

    std::string sel_, filter_;
    MeshPreview preview_;
};

}  // namespace

std::unique_ptr<Panel> make_items_panel() { return std::make_unique<ItemsPanel>(); }

}  // namespace mk
