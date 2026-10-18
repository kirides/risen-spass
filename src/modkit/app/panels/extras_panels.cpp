// Panels for what no other panel owns: "Patches" (free property edits of any template: risen.template.patch), "Strings" (text rows of the game's string tables
// that belong to no object of the project) and "Shipped dialogues" (XP rewards and trader refills added to shipped dialogue infos, teaching limits of placed
// NPCs). They are the editor's home for what a converted Risen mod (risen-pack2mod --project) changes besides items, recipes and NPCs.
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>

#include "app/editor_util.h"
#include "app/panels/panels.h"
#include "app/pickers.h"
#include "app/prop_editor.h"
#include "app/widgets.h"
#include "core/catalog.h"
#include "pack2mod/util.h"

namespace mk {

namespace {

template <class T>
T* find_by_uid(std::vector<T>& v, const std::string& uid) {
    for (auto& x : v)
        if (x.uid == uid) return &x;
    return nullptr;
}

template <class T>
void remove_by_uid(std::vector<T>& v, const std::string& uid) {
    v.erase(std::remove_if(v.begin(), v.end(), [&](const T& x) { return x.uid == uid; }), v.end());
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------
class PatchesPanel : public Panel {
public:
    const char* title() const override { return kPanelPatches; }
    void on_project_changed(App& app) override {
        if (!find_by_uid(app.project.patches, sel_)) sel_.clear();
    }
    bool select(App& app, const std::string& key) override {
        for (auto& x : app.project.patches)
            if (x.uid == key || x.template_name == key) {
                sel_ = x.uid;
                app.focus_panel(title());
                return true;
            }
        return false;
    }
    void attach(App& app) override {
        app.template_actions.push_back({"Patch this template's properties", {}, [this](App& a, const TemplateInfo& t) {
                                            TemplatePatch x;
                                            x.uid = new_uid();
                                            x.template_name = t.name;
                                            a.project.patches.push_back(x);
                                            sel_ = x.uid;
                                            a.touch();
                                            a.focus_panel(title());
                                        }});
    }

    void draw(App& app) override {
        if (!ImGui::Begin(title(), &open)) {
            ImGui::End();
            return;
        }
        Project& p = app.project;
        ImGui::BeginChild("##list", ImVec2(280, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
        if (ImGui::Button("+ Patch")) {
            TemplatePatch x;
            x.uid = new_uid();
            p.patches.push_back(x);
            sel_ = x.uid;
            app.touch();
        }
        ui::help("A patch writes properties of any template of the game (or of the project): the range of a light, the flight of a projectile, an NPC's voice... "
                 "It is applied on the first tick and again with the live entities on every session start, like the balance data.");
        ImGui::Separator();
        std::string remove;
        for (auto& x : p.patches) {
            ImGui::PushID(x.uid.c_str());
            const std::string label = x.template_name.empty() ? std::string("(choose a template)") : x.template_name;
            if (ImGui::Selectable(label.c_str(), sel_ == x.uid)) sel_ = x.uid;
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Delete")) remove = x.uid;
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        if (!remove.empty()) {
            remove_by_uid(p.patches, remove);
            app.touch();
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##editor");
        if (TemplatePatch* x = find_by_uid(p.patches, sel_)) {
            bool ch = false;
            const GameData* g = app.game.get();
            ImGui::PushItemWidth(std::min(480.0f, ImGui::GetContentRegionAvail().x * 0.6f));
            ch |= ui::template_picker("Template", x->template_name, g, {}, ui::project_item_names(p));
            ImGui::PopItemWidth();
            const TemplateInfo* t = g ? g->tpl(x->template_name) : nullptr;
            ui::section("Properties");
            ImGui::TextDisabled("Only the properties you list are written. A property set the template lacks cannot be added at run time.");
            ch |= ui::prop_edits("##props", x->properties, g, t);
            ui::section("Notes");
            ch |= ui::input_multiline("##notes", x->notes, 3);
            if (ch) app.touch();
        } else {
            ImGui::TextDisabled("Select a patch, or add one from the Browser (\"Patch this template's properties\").");
        }
        ImGui::EndChild();
        ImGui::End();
    }

private:
    std::string sel_;
};

// ---------------------------------------------------------------------------------------------------------------------------------------------------------
class StringsPanel : public Panel {
public:
    const char* title() const override { return kPanelStrings; }
    void on_project_changed(App& app) override {
        if (!find_by_uid(app.project.strings, sel_)) sel_.clear();
    }
    bool select(App& app, const std::string& key) override {
        for (auto& x : app.project.strings)
            if (x.uid == key || x.id == key) {
                sel_ = x.uid;
                app.focus_panel(title());
                return true;
            }
        return false;
    }
    void draw(App& app) override {
        if (!ImGui::Begin(title(), &open)) {
            ImGui::End();
            return;
        }
        Project& p = app.project;
        ImGui::BeginChild("##list", ImVec2(320, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
        if (ImGui::Button("+ Row")) {
            StringRow s;
            s.uid = new_uid();
            s.id = "NEW_TEXT";
            p.strings.push_back(s);
            sel_ = s.uid;
            app.touch();
        }
        ui::help("A text of the game's string tables (strings/*.csv of the exported mod) that no item, recipe or NPC of the project owns: a button label, a "
                 "document, a changed line. The id is not case sensitive.");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##filter", "filter", &filter_);
        ImGui::Separator();
        std::string remove;
        const std::string f = pack2mod_lower(filter_);
        for (auto& x : p.strings) {
            if (!f.empty() && pack2mod_lower(x.id).find(f) == std::string::npos) continue;
            ImGui::PushID(x.uid.c_str());
            if (ImGui::Selectable(x.id.empty() ? "(no id)" : x.id.c_str(), sel_ == x.uid)) sel_ = x.uid;
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Delete")) remove = x.uid;
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        if (!remove.empty()) {
            remove_by_uid(p.strings, remove);
            app.touch();
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##editor");
        if (StringRow* x = find_by_uid(p.strings, sel_)) {
            bool ch = false;
            ImGui::PushItemWidth(std::min(480.0f, ImGui::GetContentRegionAvail().x * 0.6f));
            ch |= ui::input_text("Id", x->id);
            ImGui::PopItemWidth();
            ui::section("Texts");
            ch |= ui::lang_text("Text", x->text, p.mod.languages, true);
            ui::section("Notes");
            ch |= ui::input_multiline("##notes", x->notes, 2);
            if (ch) app.touch();
        } else {
            ImGui::TextDisabled("%zu text rows. Select one to edit its texts.", p.strings.size());
        }
        ImGui::EndChild();
        ImGui::End();
    }

private:
    static std::string pack2mod_lower(const std::string& s) { return p2m::lower(s); }
    std::string sel_, filter_;
};

// ---------------------------------------------------------------------------------------------------------------------------------------------------------
class ShippedDialogsPanel : public Panel {
public:
    const char* title() const override { return kPanelShippedDialogs; }
    void on_project_changed(App& app) override {
        if (!find_by_uid(app.project.dialog_edits, sel_)) sel_.clear();
        if (!find_by_uid(app.project.teachers, tsel_)) tsel_.clear();
    }
    bool select(App& app, const std::string& key) override {
        for (auto& x : app.project.dialog_edits)
            if (x.uid == key || x.info == key) {
                sel_ = x.uid;
                app.focus_panel(title());
                return true;
            }
        for (auto& x : app.project.teachers)
            if (x.uid == key || x.entity_name == key) {
                tsel_ = x.uid;
                app.focus_panel(title());
                return true;
            }
        return false;
    }
    void draw(App& app) override {
        if (!ImGui::Begin(title(), &open)) {
            ImGui::End();
            return;
        }
        if (ImGui::BeginTabBar("##tabs")) {
            if (ImGui::BeginTabItem("Dialogue edits")) {
                draw_edits(app);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Teacher limits")) {
                draw_teachers(app);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }

private:
    void draw_edits(App& app) {
        Project& p = app.project;
        ImGui::BeginChild("##list", ImVec2(280, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
        if (ImGui::Button("+ Edit of a shipped info")) {
            DialogEdit x;
            x.uid = new_uid();
            p.dialog_edits.push_back(x);
            sel_ = x.uid;
            app.touch();
        }
        ui::help("Adds commands to a dialogue info of the game when the game starts (risen.dialog.get): an XP reward, or trader refills (the stock a trader "
                 "gets when the info runs, e.g. at a chapter change). The new dialogues of the Dialogues panel are separate.");
        ImGui::Separator();
        std::string remove;
        for (auto& x : p.dialog_edits) {
            ImGui::PushID(x.uid.c_str());
            if (ImGui::Selectable(x.info.empty() ? "(choose an info)" : x.info.c_str(), sel_ == x.uid)) sel_ = x.uid;
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Delete")) remove = x.uid;
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        if (!remove.empty()) {
            remove_by_uid(p.dialog_edits, remove);
            app.touch();
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##editor");
        if (DialogEdit* x = find_by_uid(p.dialog_edits, sel_)) {
            bool ch = false;
            ImGui::PushItemWidth(std::min(420.0f, ImGui::GetContentRegionAvail().x * 0.6f));
            ch |= ui::input_text("Info", x->info);
            ui::help("The name of a shipped dialogue info, as the game's dialogue data and risen.dialog.get know it (\"FILLERX2_00029\").");
            ImGui::PopItemWidth();
            ui::section("XP reward");
            ch |= ui::opt_int("Add a reward", x->xp_add, nullptr, 50);
            ui::help("Appends a GiveXP command with this amount at the end of the info.");
            {
                bool on = x->xp_old.has_value();
                if (ImGui::Checkbox("Change a reward", &on)) {
                    if (on) {
                        x->xp_old = 10;
                        x->xp_new = 20;
                    } else {
                        x->xp_old.reset();
                        x->xp_new.reset();
                    }
                    ch = true;
                }
                ui::help("Finds the info's GiveXP command that gives the old amount and gives the new one instead.");
                if (on) {
                    int o = x->xp_old.value_or(0), n = x->xp_new.value_or(0);
                    ImGui::SetNextItemWidth(140);
                    if (ImGui::InputInt("from##xp", &o)) ch = true, x->xp_old = o;
                    ImGui::SetNextItemWidth(140);
                    if (ImGui::InputInt("to##xp", &n)) ch = true, x->xp_new = n;
                }
            }
            ui::section("Property edits");
            ImGui::TextDisabled("Change a value of the info, one of its commands or conditions where it still has the old one; or append a condition. Applied before the XP edits.");
            {
                static const std::vector<std::string> kinds = {"info", "command", "condition", "add_condition"};
                int drop_op = -1;
                for (size_t i = 0; i < x->ops.size(); ++i) {
                    InfoOp& op = x->ops[i];
                    ImGui::PushID(1000 + static_cast<int>(i));
                    ImGui::SetNextItemWidth(130);
                    ch |= ui::combo("##kind", op.kind, kinds, false);
                    if (op.kind != "info") {
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(60);
                        if (ImGui::InputInt("##index", &op.index, 0, 0)) {
                            op.index = std::max(0, op.index);
                            ch = true;
                        }
                    }
                    if (op.kind == "add_condition") {
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(130);
                        ch |= ImGui::InputTextWithHint("##class", "class", &op.cls);
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(120);
                    ch |= ImGui::InputTextWithHint("##pname", "property", &op.name);
                    if (op.kind != "add_condition") {
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(110);
                        ch |= ImGui::InputTextWithHint("##old", "old value", &op.old_value);
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(130);
                    ch |= ImGui::InputTextWithHint("##new", "new value", &op.new_value);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("x")) drop_op = static_cast<int>(i);
                    ImGui::PopID();
                }
                if (drop_op >= 0) {
                    x->ops.erase(x->ops.begin() + drop_op);
                    ch = true;
                }
                if (ImGui::SmallButton("+ property edit")) {
                    InfoOp op;
                    op.kind = "command";
                    op.index = 1;
                    x->ops.push_back(op);
                    ch = true;
                }
            }
            ui::section("Trader refills");
            ch |= ui::opt_int("Chapter", x->chapter, nullptr, 1);
            ui::help("The chapter the info belongs to. Refills of chapter 1 are also handed out when a new game starts (the traders sell them from the beginning).");
            int drop = -1;
            for (size_t i = 0; i < x->refills.size(); ++i) {
                Refill& r = x->refills[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::SetNextItemWidth(130);
                ch |= ImGui::InputTextWithHint("##trader", "trader", &r.trader);
                ImGui::SameLine();
                ch |= ui::template_picker("##item", r.item, app.game.get(), {TplKind::Item, TplKind::Recipe}, ui::project_item_names(p), 260);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(90);
                if (ImGui::InputInt("##n", &r.count)) {
                    r.count = std::max(0, r.count);
                    ch = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) drop = static_cast<int>(i);
                ImGui::PopID();
            }
            if (drop >= 0) {
                x->refills.erase(x->refills.begin() + drop);
                ch = true;
            }
            if (ImGui::SmallButton("+ refill")) {
                x->refills.push_back({});
                ch = true;
            }
            ui::section("Notes");
            ch |= ui::input_multiline("##notes", x->notes, 2);
            if (ch) app.touch();
        } else {
            ImGui::TextDisabled("Select an edit, or add one.");
        }
        ImGui::EndChild();
    }

    void draw_teachers(App& app) {
        Project& p = app.project;
        ImGui::BeginChild("##tlist", ImVec2(280, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
        if (ImGui::Button("+ Teacher limit")) {
            TeacherLimit x;
            x.uid = new_uid();
            x.skill = "Strength";
            p.teachers.push_back(x);
            tsel_ = x.uid;
            app.touch();
        }
        ui::help("The highest value a placed NPC brings the player's skill to (risen.npc.set_teach_limit); applied on every world start. The NPC is found by his "
                 "entity ID (World panel: select the NPC).");
        ImGui::Separator();
        std::string remove;
        for (auto& x : p.teachers) {
            ImGui::PushID(x.uid.c_str());
            const std::string label = (x.entity_name.empty() ? x.guid : x.entity_name) + ": " + x.skill + " " + std::to_string(x.max);
            if (ImGui::Selectable(label.c_str(), tsel_ == x.uid)) tsel_ = x.uid;
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Delete")) remove = x.uid;
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        if (!remove.empty()) {
            remove_by_uid(p.teachers, remove);
            app.touch();
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##teditor");
        if (TeacherLimit* x = find_by_uid(p.teachers, tsel_)) {
            bool ch = false;
            ImGui::PushItemWidth(std::min(420.0f, ImGui::GetContentRegionAvail().x * 0.6f));
            ch |= ui::input_text("NPC", x->entity_name);
            ui::help("For display: the NPC's entity name.");
            ch |= ui::input_text("Entity ID", x->guid);
            ui::help("xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx: the ID of the placed NPC in the world (risen.entity.by_id).");
            ch |= ui::combo("Skill", x->skill, skill_names(app.game.get()), false);
            ImGui::SetNextItemWidth(140);
            if (ImGui::InputInt("Limit", &x->max)) {
                x->max = std::max(0, x->max);
                ch = true;
            }
            ui::help("The highest value he teaches the skill up to.");
            ch |= ui::world_combo("World", x->world, app);
            ImGui::PopItemWidth();
            if (ch) app.touch();
        } else {
            ImGui::TextDisabled("Select a limit, or add one.");
        }
        ImGui::EndChild();
    }

    std::string sel_, tsel_;
};

}  // namespace

std::unique_ptr<Panel> make_patches_panel() { return std::make_unique<PatchesPanel>(); }
std::unique_ptr<Panel> make_strings_panel() { return std::make_unique<StringsPanel>(); }
std::unique_ptr<Panel> make_shipped_dialogs_panel() { return std::make_unique<ShippedDialogsPanel>(); }

}  // namespace mk
