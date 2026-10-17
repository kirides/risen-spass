// Items panel: changes of shipped items, new items (clones of a base item) and item groups (relative settings for many items).
// Exported as the items / new_items / item_groups sections of risen.balance.install (data/balance.lua).
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>

#include "app/panels/panels.h"
#include "app/editor_util.h"
#include "app/pickers.h"
#include "app/prop_editor.h"
#include "app/render/mesh_preview.h"
#include "app/widgets.h"
#include "core/catalog.h"
#include "core/image.h"
#include "pack2mod/util.h"
#include "core/validate.h"

namespace mk {

namespace {

class ItemsPanel : public Panel {
public:
    const char* title() const override { return kPanelItems; }

    void attach(App& app) override {
        app.template_actions.push_back({"New item based on this", {TplKind::Item, TplKind::Recipe}, [this](App& a, const TemplateInfo& t) { add_new(a, t.name); }});
        app.template_actions.push_back({"Change this item", {TplKind::Item, TplKind::Recipe}, [this](App& a, const TemplateInfo& t) { add_edit(a, t.name); }});
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
        if (!ImGui::Begin(title(), &open)) {
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
        else ImGui::TextDisabled("Create an item, change a shipped one (also from the Browser: select an item there), or add an item group.");
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
        if (ImGui::Button("+ New item")) add_new(app, "It_1H_Sword");
        ImGui::SameLine();
        if (ImGui::Button("+ Change shipped")) ImGui::OpenPopup("##pick_edit");
        ImGui::SameLine();
        if (ImGui::Button("+ Group")) {
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
            ImGui::TextUnformatted("Which shipped item?");
            ui::template_picker("Item", name, app.game.get(), {TplKind::Item, TplKind::Recipe}, {}, 280);
            if (ImGui::Button("Add") && !name.empty()) {
                add_edit(app, name);
                name.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##filter", "filter", &filter_);
        ImGui::Separator();
        std::string remove;
        for (auto& i : p.items) {
            const std::string lbl = ui::label_of(app, i.name);
            if (!filter_.empty() && i.name.find(filter_) == std::string::npos && lbl.find(filter_) == std::string::npos) continue;
            ImGui::PushID(i.uid.c_str());
            if (ImGui::Selectable(i.name.empty() ? "(unnamed)" : i.name.c_str(), sel_ == i.uid)) sel_ = i.uid;
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Duplicate")) {
                    ItemDef d = i;
                    d.uid = new_uid();
                    if (d.mode == Mode::New) d.name = ui::unique_template_name(p, app.game.get(), d.name);
                    app.defer([&app, d] {
                        app.project.items.push_back(d);
                        app.touch();
                    });
                }
                if (ImGui::MenuItem("Delete")) remove = i.uid;
                ImGui::EndPopup();
            }
            ImGui::SameLine(190);
            ImGui::TextDisabled("%s", i.mode == Mode::New ? "new" : "change");
            ImGui::PopID();
        }
        if (!p.item_groups.empty()) ImGui::SeparatorText("Item groups");
        for (auto& g : p.item_groups) {
            ImGui::PushID(g.uid.c_str());
            const std::string t = !g.group.empty() ? g.group : !g.match.empty() ? g.match : "(group)";
            if (ImGui::Selectable(t.c_str(), sel_ == g.uid)) sel_ = g.uid;
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Delete")) remove = g.uid;
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
            ImGui::TextDisabled("NEW ITEM: a clone of the base with its own name, texts and numbers (risen.template.create)");
            ch |= ui::input_text("Name", it.name);
            ui::help("The template name: letters, digits and _, unique, at most 63 characters. It is the key of the item in savegames: keep it once released.");
            if (std::string why = template_name_problem(it.name); !why.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", why.c_str());
            else if (g && g->tpl(it.name)) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "a game template has this name");
            ch |= ui::template_picker("Base item", it.base, g, {TplKind::Item, TplKind::Recipe});
            if (g && target) {
                ImGui::SameLine();
                ImGui::TextDisabled("\"%s\"", g->label(target->name).c_str());
            }
        } else {
            ImGui::TextDisabled("CHANGE OF A SHIPPED ITEM: only the values you set are changed");
            ch |= ui::template_picker("Item", it.name, g, {TplKind::Item, TplKind::Recipe});
            if (g && target) {
                ImGui::SameLine();
                ImGui::TextDisabled("\"%s\"", g->label(target->name).c_str());
            }
        }

        ui::section("Texts");
        if (it.mode == Mode::Edit) ImGui::TextDisabled("Leave empty to keep the game's texts.");
        ch |= ui::lang_text("Label", it.label, app.project.mod.languages);
        ch |= ui::lang_text("Description", it.description, app.project.mod.languages, true);

        ui::section("Numbers");
        ch |= ui::opt_int("Value (gold)", it.value, hint_int(shipped.value).c_str(), shipped.value.value_or(10));
        ch |= ui::opt_int("Damage", it.damage, hint_int(shipped.damage).c_str(), shipped.damage.value_or(10));
        ui::help("gCDamage_PS.DamageAmount. Melee damage = multiplier * (DamageAmount + the wielder's Strength).");
        ch |= ui::opt_int("Mana cost", it.mana_cost, hint_int(shipped.mana_cost).c_str(), shipped.mana_cost.value_or(10));
        ui::help("Runes and scrolls only (gCCastInfo_PS.ManaCost).");

        ui::section("Requirements and effects");
        const auto skills = skill_names(g);
        ch |= ui::opt_skills("Replace requirements", it.requirements, shipped.requirements, skills,
                         "What the player needs to equip or use the item (attributes, combat skills, ...). Only the player is held to them.");
        ch |= ui::opt_skills("Replace modifiers", it.modifies, shipped.modifies, skills,
                         "What the item adds while worn or when used: HitPoints for a potion, ProtEdge for armour, ...");

        ui::section("Kind");
        ch |= ui::opt_enum("Use type", it.use_type, enum_names(g, "gEItemUseType"), shipped.use_type);
        ch |= ui::opt_enum("Category", it.category, enum_names(g, "gEItemCategory"), shipped.category);

        ui::section("Appearance");
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
            if (ui::asset_picker("Mesh", m, g, "_xmsh", true) && it.mesh) {
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
            if (ImGui::BeginCombo("Icon", ic.c_str(), ImGuiComboFlags_HeightLarge)) {
                // the project's pictures first: they become cells of the mod's icon atlas
                for (auto& a : app.project.assets) {
                    const std::string n = p2m::bare_name(a.file);
                    if (!is_picture(n)) continue;
                    if (ImGui::Selectable((n + "  (picture of the project)").c_str(), n == ic)) {
                        it.icon = n;
                        ch = true;
                    }
                }
                if (g) {
                    if (icons_.empty()) icons_ = known_icons(*g);
                    for (auto& n : icons_)
                        if (ImGui::Selectable(n.c_str(), n == ic)) {
                            it.icon = n;
                            ch = true;
                        }
                }
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
        }
        ch |= ui::opt_int("Material switch", it.material, hint_int(shipped.material).c_str(), shipped.material.value_or(0));
        preview_.draw(app, it.mesh ? *it.mesh : shipped.mesh, ImVec2(260, 180));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag to turn");

        ui::section("Other properties");
        ImGui::TextDisabled("Any property of the template by name (see docs/ITEM_PROPERTIES.md).");
        ch |= ui::prop_edits("props", it.properties, g, target);

        ui::section("Notes");
        ch |= ui::input_multiline("##notes", it.notes, 3);
        if (ch) app.touch();
    }

    void draw_group(App& app, ItemGroup& gr) {
        bool ch = false;
        ImGui::TextDisabled("ITEM GROUP: relative settings for every item a selector matches (computed from the shipped values)");
        ch |= ui::input_text("Group path", gr.group);
        ui::help("A template group: Items/Potions, Items/Weapons_Swords, ... (the Browser shows the groups).");
        ch |= ui::input_text("Name pattern", gr.match);
        ui::help("A Lua pattern on the template name, e.g. ^It_Po_");
        ch |= ui::input_text("Exclude pattern", gr.exclude);
        ImGui::TextUnformatted("Also these items (one per line)");
        ch |= ui::string_list("##names", gr.names);
        ui::section("Settings");
        ch |= ui::opt_double("Value factor", gr.value_mult, nullptr, 1.0);
        ch |= ui::opt_double("Damage factor", gr.damage_mult, nullptr, 1.0);
        if (app.game) {
            size_t n = 0;
            for (auto& t : app.game->templates()) {
                if (t->kind != TplKind::Item && t->kind != TplKind::Recipe) continue;
                if (!gr.group.empty() && (t->group.size() < gr.group.size() || p2m::lower(t->group.substr(0, gr.group.size())) != p2m::lower(gr.group))) continue;
                ++n;
            }
            ImGui::TextDisabled("The group path selects %zu items (patterns are evaluated by the layer).", n);
        }
        if (ch) app.touch();
    }

    std::string sel_, filter_;
    std::vector<std::string> icons_;
    MeshPreview preview_;
};

}  // namespace

std::unique_ptr<Panel> make_items_panel() { return std::make_unique<ItemsPanel>(); }

}  // namespace mk
