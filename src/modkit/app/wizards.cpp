#include "app/wizards.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>

#include "app/editor_util.h"
#include "app/pickers.h"
#include "app/widgets.h"
#include "core/catalog.h"
#include "core/log.h"
#include "core/validate.h"

namespace mk {

const char* WizardUi::title(WizardKind k) {
    switch (k) {
        case WizardKind::FetchQuest: return "Wizard: fetch quest";
        case WizardKind::Weapon: return "Wizard: new weapon with a recipe";
        case WizardKind::Trader: return "Wizard: trader with stock";
        default: return "Wizard";
    }
}

void WizardUi::start(App& app, WizardKind kind) {
    kind_ = kind;
    error_.clear();
    const Project& p = app.project;
    if (kind == WizardKind::FetchQuest) {
        fetch_ = FetchQuestParams{};
        if (!p.npcs.empty()) fetch_.npc = p.npcs.front().name;
        fetch_.item = "It_IronOre";
        fetch_.count = 2;
        fetch_.xp = 100;
        fetch_.quest = suggest_quest_id(p, "IronOre");
        quest_id_touched_ = false;
    } else if (kind == WizardKind::Weapon) {
        weapon_ = WeaponParams{};
        weapon_.base = "It_1H_Sword";
        weapon_.name = ui::unique_template_name(p, app.game.get(), ui::name_prefix(p) + "_Sword");
        weapon_.requirements = IntMap{{"Strength", 10}};
        weapon_.ingredients = {{"It_IronOre", 3}};
        weapon_.skills = IntMap{{"Smith", 1}};
        if (const TemplateInfo* t = app.game ? app.game->tpl(weapon_.base) : nullptr) {
            const ItemStats s = item_stats(*app.game, *t);
            weapon_.damage = s.damage;
            weapon_.value = s.value;
        }
    } else if (kind == WizardKind::Trader) {
        trader_ = TraderParams{};
        trader_.npc = ui::name_prefix(p) + "_Trader";
        for (int i = 2; p.npc(trader_.npc); ++i) trader_.npc = ui::name_prefix(p) + "_Trader_" + std::to_string(i);
        trader_.template_name = "Peasant_1_01";
        trader_.stock = {{"It_Apple", 5, false}, {"It_Bread", 5, false}};
    }
}

void WizardUi::finish(App& app, const WizardResult& r) {
    if (!r.ok()) {
        error_ = r.error;
        return;
    }
    app.touch();
    app.project_replaced();
    for (auto& n : r.notes) log_info("wizard: " + n);
    std::string msg = std::string(title(kind_)) + ": ";
    msg += r.created.empty() ? "done" : "created";
    if (!r.notes.empty()) msg += " (" + r.notes.front() + ")";
    app.status(msg);
    const auto first = r.created.empty() ? std::pair<std::string, std::string>() : r.created.front();
    kind_ = WizardKind::None;
    for (auto& c : r.created) app.defer([&app, c] { app.select_in(c.first, c.second); });
    // the first object last: its panel ends up in front
    if (!first.first.empty()) app.defer([&app, first] { app.select_in(first.first, first.second); });
}

void WizardUi::draw(App& app) {
    if (want_ != WizardKind::None) {
        start(app, want_);
        want_ = WizardKind::None;
        ImGui::OpenPopup("###wizard");
    }
    if (kind_ == WizardKind::None) return;
    ImGui::SetNextWindowSize(ImVec2(820, 700), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    bool open = true;
    const std::string name = std::string(title(kind_)) + "###wizard";
    if (!ImGui::BeginPopupModal(name.c_str(), &open)) {
        if (!open) kind_ = WizardKind::None;
        return;
    }
    ImGui::BeginChild("##form", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.2f));
    ImGui::PushItemWidth(380);
    bool go = false;
    switch (kind_) {
        case WizardKind::FetchQuest: go = fetch_form(app); break;
        case WizardKind::Weapon: go = weapon_form(app); break;
        case WizardKind::Trader: go = trader_form(app); break;
        default: break;
    }
    ImGui::PopItemWidth();
    ImGui::EndChild();
    if (!error_.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", error_.c_str());
    ImGui::BeginDisabled(!go);
    if (ImGui::Button("Create", ImVec2(140, 0))) {
        WizardResult r;
        if (kind_ == WizardKind::FetchQuest) r = wizard_fetch_quest(app.project, fetch_);
        else if (kind_ == WizardKind::Weapon) r = wizard_weapon(app.project, weapon_);
        else if (kind_ == WizardKind::Trader) r = wizard_trader(app.project, trader_);
        finish(app, r);
        if (kind_ == WizardKind::None) ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0))) {
        kind_ = WizardKind::None;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Everything the wizard makes can be edited afterwards, and Ctrl+Z takes it back.");
    ImGui::EndPopup();
}

// ---- forms: each returns whether the "Create" button may be used ----

bool WizardUi::fetch_form(App& app) {
    const Project& p = app.project;
    const auto& langs = p.mod.languages;
    ImGui::TextWrapped("An NPC asks the player for items. Makes the quest (start, bring the items, turn them in, reward, success) and two dialogue options for the NPC: one offers the job, one hands the items in.");
    ui::section("Who and what");
    // the NPC: a placed NPC of the project, or the unique name of one of the game's world
    if (ImGui::BeginCombo("##npc", fetch_.npc.empty() ? "(choose)" : fetch_.npc.c_str())) {
        for (auto& n : p.npcs)
            if (ImGui::Selectable(n.name.c_str(), n.name == fetch_.npc)) fetch_.npc = n.name;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220);
    ImGui::InputTextWithHint("NPC", "or a unique name", &fetch_.npc);
    ui::template_picker("Item", fetch_.item, app.game.get(), {TplKind::Item}, ui::project_item_names(p));
    ImGui::InputInt("How many", &fetch_.count);
    fetch_.count = std::clamp(fetch_.count, 1, 999);
    ImGui::Checkbox("The NPC takes the items", &fetch_.take_items);
    if (ImGui::InputText("Quest id", &fetch_.quest)) quest_id_touched_ = true;
    ui::section("Reward");
    ImGui::InputInt("Experience", &fetch_.xp);
    ImGui::InputInt("Gold", &fetch_.gold);
    {
        std::vector<Ingredient> rows;
        for (auto& [item, n] : fetch_.reward_items) rows.push_back({item, n});
        if (ui::ingredient_list("reward", rows, app, ui::project_item_names(p), 6)) {
            fetch_.reward_items.clear();
            for (auto& r : rows) fetch_.reward_items.push_back({r.item, r.count});
        }
    }
    ui::section("Texts");
    ImGui::TextDisabled("Left empty, a plain English text is used in the first language.");
    ui::lang_text("Quest title", fetch_.title, langs);
    ui::lang_text("Player asks", fetch_.ask_option, langs);
    ui::lang_text("NPC answers", fetch_.ask_line, langs, true);
    ui::lang_text("Quest log entry", fetch_.log_text, langs, true);
    ui::lang_text("Player hands in", fetch_.done_option, langs);
    ui::lang_text("NPC thanks", fetch_.done_line, langs, true);
    ui::lang_text("Log entry at the end", fetch_.success_log, langs);
    if (!quest_id_touched_ && !fetch_.item.empty()) {  // the quest id follows the item until it is edited
        std::string tail = fetch_.item;
        if (tail.rfind("It_", 0) == 0) tail = tail.substr(3);
        fetch_.quest = suggest_quest_id(p, tail);
    }
    return !fetch_.npc.empty() && !fetch_.item.empty() && !fetch_.quest.empty();
}

bool WizardUi::weapon_form(App& app) {
    const Project& p = app.project;
    const auto& langs = p.mod.languages;
    const GameData* g = app.game.get();
    ImGui::TextWrapped("A new weapon made of a shipped one, and (optionally) the recipe that crafts it.");
    ui::section("The weapon");
    ui::input_text("Name", weapon_.name);
    if (const std::string why = template_name_problem(weapon_.name); !why.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", why.c_str());
    const std::string old_base = weapon_.base;
    if (ui::template_picker("Made of", weapon_.base, g, {TplKind::Item}, {}) && weapon_.base != old_base && g)
        if (const TemplateInfo* t = g->tpl(weapon_.base)) {
            const ItemStats s = item_stats(*g, *t);
            weapon_.damage = s.damage;
            weapon_.value = s.value;
        }
    ui::lang_text("Label", weapon_.label, langs);
    ui::lang_text("Description", weapon_.description, langs, true);
    ui::opt_int("Damage", weapon_.damage, nullptr, 20);
    ui::opt_int("Value (gold)", weapon_.value, nullptr, 100);
    ImGui::TextUnformatted("Needs to equip");
    ui::int_map("requires", weapon_.requirements, skill_names(g), 10, 0, 200);
    ui::section("Recipe");
    ImGui::Checkbox("Make a recipe for it", &weapon_.recipe);
    if (weapon_.recipe) {
        ui::combo("Station", weapon_.station, recipe_stations(), false);
        ImGui::TextUnformatted("Ingredients");
        ui::ingredient_list("ingr", weapon_.ingredients, app, ui::project_item_names(p));
        ImGui::TextUnformatted("Skills to craft it");
        ui::int_map("skills", weapon_.skills, skill_names(g), 1, 0, 10);
        ImGui::Checkbox("The player knows the recipe from the start", &weapon_.learn);
    }
    return !weapon_.name.empty() && !weapon_.base.empty();
}

bool WizardUi::trader_form(App& app) {
    const Project& p = app.project;
    const auto& langs = p.mod.languages;
    ImGui::TextWrapped("A trader: a new NPC with his own template (trading switched on), his stock and a dialogue option that opens the trade. Not tried in the game yet: test him with Play.");
    ui::section("Who");
    ui::input_text("Unique name", trader_.npc);
    if (const std::string why = npc_name_problem(trader_.npc); !why.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", why.c_str());
    ui::template_picker("Made of", trader_.template_name, app.game.get(), {TplKind::Npc}, {});
    ui::lang_text("Label", trader_.label, langs);
    ImGui::Checkbox("In front of the player when first made", &trader_.near_player);
    if (!trader_.near_player) {
        ImGui::InputDouble("X", &trader_.position[0], 10, 100, "%.0f");
        ImGui::InputDouble("Y", &trader_.position[1], 10, 100, "%.0f");
        ImGui::InputDouble("Z", &trader_.position[2], 10, 100, "%.0f");
        ImGui::TextDisabled("Better: leave it and place him in the World view afterwards.");
    }
    ui::section("Stock");
    ui::inventory_list("stock", trader_.stock, app, ui::project_item_names(p), false);
    ui::section("Texts");
    ui::lang_text("Player says", trader_.option_text, langs);
    ui::lang_text("NPC says", trader_.line_text, langs);
    return !trader_.npc.empty() && !trader_.template_name.empty() && !trader_.stock.empty();
}

}  // namespace mk
