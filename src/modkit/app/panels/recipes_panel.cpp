// Recipes panel: new recipes (a recipe item the player carries to know it: station, ingredients, skills, result) and changes of shipped recipes.
// Exported as the new_recipes / recipes sections of risen.balance.install (data/balance.lua).
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>

#include "app/dragdrop.h"
#include "app/editor_util.h"
#include "app/icons.h"
#include "app/panels/panels.h"
#include "app/pickers.h"
#include "app/prop_editor.h"
#include "app/widgets.h"
#include "core/catalog.h"
#include "core/i18n.h"
#include "core/validate.h"

namespace mk {

namespace {

class RecipesPanel : public Panel {
public:
    const char* title() const override { return kPanelRecipes; }

    void attach(App& app) override {
        app.template_actions.push_back({N_("Change this recipe"), {TplKind::Recipe}, [this](App& a, const TemplateInfo& t) { add_edit(a, t.name); }});
        app.template_actions.push_back({N_("New recipe for this item"), {TplKind::Item}, [this](App& a, const TemplateInfo& t) { add_new(a, t.name); }});
    }
    void on_project_changed(App& app) override {
        if (!find(app.project, sel_)) sel_.clear();
    }
    bool select(App& app, const std::string& key) override {
        for (auto& r : app.project.recipes)
            if (r.name == key || r.uid == key) {
                sel_ = r.uid;
                app.focus_panel(title());
                return true;
            }
        return false;
    }

    void add_new(App& app, const std::string& result) {
        RecipeDef r;
        r.uid = new_uid();
        r.mode = Mode::New;
        r.station = "anvil";
        r.result = result;
        r.ingredients = std::vector<Ingredient>{{"It_IronOre", 1}};
        r.learn = true;
        r.name = ui::unique_template_name(app.project, app.game.get(), ui::name_prefix(app.project) + "_Recipe_" + (result.empty() ? std::string("Item") : result));
        app.project.recipes.push_back(r);
        sel_ = r.uid;
        app.touch();
        app.focus_panel(title());
    }
    void add_edit(App& app, const std::string& name) {
        for (auto& r : app.project.recipes)
            if (r.mode == Mode::Edit && r.name == name) {
                sel_ = r.uid;
                app.focus_panel(title());
                return;
            }
        RecipeDef r;
        r.uid = new_uid();
        r.mode = Mode::Edit;
        r.name = name;
        app.project.recipes.push_back(r);
        sel_ = r.uid;
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
        if (RecipeDef* r = find(app.project, sel_)) draw_recipe(app, *r);
        else ImGui::TextDisabled("%s", tr("Create a recipe or change a shipped one (the Browser offers both for a selected item / recipe)."));
        ImGui::PopItemWidth();
        ImGui::EndChild();
        ImGui::End();
    }

private:
    static RecipeDef* find(Project& p, const std::string& uid) {
        for (auto& r : p.recipes)
            if (r.uid == uid) return &r;
        return nullptr;
    }

    void draw_list(App& app) {
        Project& p = app.project;
        if (ImGui::Button(tr("+ New recipe"))) add_new(app, "");
        ImGui::SameLine();
        if (ImGui::Button(tr("+ Change shipped"))) ImGui::OpenPopup("##pick_edit");
        if (ImGui::BeginPopup("##pick_edit")) {
            static std::string name;
            ImGui::TextUnformatted(tr("Which shipped recipe?"));
            ui::template_picker(tr("Recipe"), name, app.game.get(), {TplKind::Recipe}, {}, 280);
            if (ImGui::Button(tr("Add")) && !name.empty()) {
                add_edit(app, name);
                name.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::Separator();
        std::string remove;
        for (auto& r : p.recipes) {
            ImGui::PushID(r.uid.c_str());
            ui::item_icon(r.result.value_or(r.name), 20.0f);
            ImGui::SameLine();
            if (ImGui::Selectable(r.name.empty() ? tr("(unnamed)") : r.name.c_str(), sel_ == r.uid)) sel_ = r.uid;
            ui::drag_source(r.name, TplKind::Recipe, false, r.mode == Mode::New, ui::label_of(app, r.name));
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem(tr("Duplicate"))) {
                    RecipeDef d = r;
                    d.uid = new_uid();
                    if (d.mode == Mode::New) d.name = ui::unique_template_name(p, app.game.get(), d.name);
                    app.defer([&app, d] {
                        app.project.recipes.push_back(d);
                        app.touch();
                    });
                }
                if (ImGui::MenuItem(tr("Delete"))) remove = r.uid;
                ImGui::EndPopup();
            }
            ImGui::SameLine(190);
            ImGui::TextDisabled("%s", r.mode == Mode::New ? (r.station.empty() ? "new" : r.station.c_str()) : "change");
            ImGui::PopID();
        }
        if (!remove.empty()) {
            p.recipes.erase(std::remove_if(p.recipes.begin(), p.recipes.end(), [&](const RecipeDef& r) { return r.uid == remove; }), p.recipes.end());
            app.touch();
        }
    }

    void draw_recipe(App& app, RecipeDef& r) {
        const GameData* g = app.game.get();
        bool ch = false;
        const auto items = ui::project_item_names(app.project);
        std::optional<RecipeStats> shipped;
        const TemplateInfo* target = nullptr;
        if (g) {
            target = g->tpl(r.mode == Mode::New ? (r.base.empty() ? std::string("It_Recipe_Health_01") : r.base) : r.name);
            if (target) shipped = recipe_stats(*g, *target);
        }
        if (r.mode == Mode::New) {
            ImGui::TextDisabled("%s", tr("NEW RECIPE: a recipe item; the player knows the recipe while he carries it"));
            ch |= ui::input_text(tr("Name"), r.name);
            ui::help(tr("The template name of the recipe item (the plan): letters, digits and _, unique. It is the key in savegames."));
            if (std::string why = template_name_problem(r.name); !why.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", why.c_str());
            ui::section(tr("Recipe"));
            ui::combo(tr("Station"), r.station, recipe_stations(), false) && (ch = true);
            std::string res = r.result.value_or("");
            if (ui::template_picker(tr("Result item"), res, g, {TplKind::Item, TplKind::Recipe}, items)) {
                r.result = res;
                ch = true;
            }
            ImGui::TextUnformatted(tr("Ingredients"));
            if (!r.ingredients) r.ingredients = std::vector<Ingredient>{};
            ch |= ui::ingredient_list("ingr", *r.ingredients, app, items);
            const auto skills = skill_names(g);
            if (!r.skills) {
                if (ImGui::SmallButton(tr("+ required skills"))) {
                    r.skills = IntMap{{"Smith", 1}};
                    ch = true;
                }
            } else {
                ImGui::TextUnformatted(tr("Skills of the crafter"));
                ch |= ui::int_map("skills", *r.skills, skills, 1, 0, 10);
            }
            ch |= ImGui::Checkbox(tr("Give the plan to the player once per savegame (learn)"), &r.learn);
            ui::section(tr("The recipe item"));
            ch |= ui::lang_text(tr("Label"), r.label, app.project.mod.languages);
            ImGui::TextDisabled("%s", tr("Empty: \"Recipe: <result label>\""));
            ch |= ui::lang_text(tr("Description"), r.description, app.project.mod.languages, true);
            ch |= ui::template_picker(tr("Base recipe item"), r.base, g, {TplKind::Recipe});
            ui::help(tr("The shipped recipe item that is cloned (default It_Recipe_Health_01: a written recipe with the paper icon)."));
            ch |= ui::opt_int(tr("Value"), r.value, nullptr, 50);
        } else {
            ImGui::TextDisabled("%s", tr("CHANGE OF A SHIPPED RECIPE: only what you set is changed"));
            ch |= ui::template_picker(tr("Recipe"), r.name, g, {TplKind::Recipe});
            if (shipped) {
                std::string ing;
                for (auto& i : shipped->ingredients) ing += (ing.empty() ? "" : ", ") + std::to_string(i.count) + "x " + i.item;
                ImGui::TextDisabled(tr("shipped: %s at %s -> %s"), ing.c_str(), shipped->station.c_str(), shipped->result.c_str());
            }
            ui::section(tr("Recipe"));
            std::optional<std::string> st = r.station.empty() ? std::nullopt : std::optional<std::string>(r.station);
            if (ui::opt_choice(tr("Station"), st, recipe_stations(), shipped ? shipped->station : std::string())) {
                r.station = st.value_or("");
                ch = true;
            }
            bool on = r.ingredients.has_value();
            if (ImGui::Checkbox(tr("Replace ingredients"), &on)) {
                if (on) r.ingredients = shipped ? shipped->ingredients : std::vector<Ingredient>{};
                else r.ingredients.reset();
                ch = true;
            }
            if (r.ingredients) ch |= ui::ingredient_list("ingr", *r.ingredients, app, items);
            ch |= ui::opt_skills(tr("Replace skills"), r.skills, shipped ? shipped->skills : IntMap{}, skill_names(g), tr("The skills the crafter needs."));
            bool res_on = r.result.has_value();
            if (ImGui::Checkbox("##res_on", &res_on)) {
                if (res_on) r.result = shipped ? shipped->result : std::string();
                else r.result.reset();
                ch = true;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!r.result);
            std::string res = r.result.value_or(shipped ? shipped->result : std::string());
            if (ui::template_picker(tr("Result item"), res, g, {TplKind::Item, TplKind::Recipe}, items) && r.result) {
                r.result = res;
                ch = true;
            }
            ImGui::EndDisabled();
            ui::section(tr("Texts of the recipe item"));
            ch |= ui::lang_text(tr("Label"), r.label, app.project.mod.languages);
            ch |= ui::lang_text(tr("Description"), r.description, app.project.mod.languages, true);
        }
        ui::section(tr("Other properties of the recipe item"));
        ch |= ui::prop_edits("props", r.properties, g, target);
        ui::section(tr("Notes"));
        ch |= ui::input_multiline("##notes", r.notes, 3);
        if (ch) app.touch();
    }

    std::string sel_;
};

}  // namespace

std::unique_ptr<Panel> make_recipes_panel() { return std::make_unique<RecipesPanel>(); }

}  // namespace mk
