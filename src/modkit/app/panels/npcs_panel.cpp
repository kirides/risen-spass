// NPCs panel: NPC and monster templates (new ones cloned from a base, changes of shipped ones: risen.balance npcs / new_npcs) and placed NPCs
// (risen.npc.persistent: name, template, position, label, guild, inventory with equipment, routine).
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>

#include "app/editor_util.h"
#include "app/panels/panels.h"
#include "app/pickers.h"
#include "app/prop_editor.h"
#include "app/widgets.h"
#include "core/catalog.h"
#include "core/validate.h"

namespace mk {

namespace {

const std::vector<std::string>& point_kinds() {
    static const std::vector<std::string> v = {"guard", "lookat", "spectator", "sleep", "dance", "stand", "sit", "smalltalk"};
    return v;
}

std::string opt_str(const std::optional<int>& v) { return v ? std::to_string(*v) : std::string(); }

// one routine point { dx, dy, dz, kind, yaw }
bool point_editor(const char* id, json& pt) {
    bool ch = false;
    ImGui::PushID(id);
    if (!pt.is_object()) pt = json::object({{"dx", 0}, {"dy", 0}, {"dz", 0}, {"kind", "guard"}});
    float d[3] = {pt.value("dx", 0.0f), pt.value("dy", 0.0f), pt.value("dz", 0.0f)};
    ImGui::SetNextItemWidth(240);
    if (ImGui::InputFloat3("dx dy dz", d, "%.0f")) {
        pt["dx"] = d[0];
        pt["dy"] = d[1];
        pt["dz"] = d[2];
        ch = true;
    }
    ImGui::SameLine();
    std::string kind = pt.value("kind", std::string("guard"));
    ImGui::SetNextItemWidth(110);
    if (ui::combo("kind", kind, point_kinds(), false)) {
        pt["kind"] = kind;
        ch = true;
    }
    ImGui::SameLine();
    bool has_yaw = pt.contains("yaw");
    if (ImGui::Checkbox("yaw", &has_yaw)) {
        if (has_yaw) pt["yaw"] = 0;
        else pt.erase("yaw");
        ch = true;
    }
    if (has_yaw) {
        ImGui::SameLine();
        float y = pt.value("yaw", 0.0f);
        ImGui::SetNextItemWidth(80);
        if (ImGui::DragFloat("##yaw", &y, 1, -360, 360, "%.0f deg")) {
            pt["yaw"] = y;
            ch = true;
        }
    }
    ImGui::PopID();
    return ch;
}

bool routine_editor(json& r) {
    bool ch = false;
    static const char* kModes[] = {"none", "one post", "day / evening / night", "patrol"};
    int mode = 0;
    if (r.is_object()) {
        if (r.contains("at")) mode = 1;
        else if (r.contains("patrol")) mode = 3;
        else if (r.contains("work")) mode = 2;
    }
    ImGui::SetNextItemWidth(220);
    if (ImGui::Combo("Routine", &mode, kModes, 4)) {
        switch (mode) {
            case 0: r = nullptr; break;
            case 1: r = json::object({{"at", json::object({{"dx", 0}, {"dy", 0}, {"dz", 0}, {"kind", "guard"}})}}); break;
            case 2:
                r = json::object({{"work", json::object({{"dx", 200}, {"dy", 0}, {"dz", 0}, {"kind", "guard"}})},
                                  {"relax", json::object({{"dx", 0}, {"dy", 0}, {"dz", 200}, {"kind", "lookat"}})},
                                  {"sleep", json::object({{"dx", -150}, {"dy", 0}, {"dz", 150}, {"kind", "sleep"}})}});
                break;
            case 3:
                r = json::object({{"patrol", json::object({{"points", json::array({json::object({{"dx", -300}, {"dy", 0}, {"dz", 0}}),
                                                                                    json::object({{"dx", 300}, {"dy", 0}, {"dz", 0}})})},
                                                           {"kind", "guard"}})}});
                break;
        }
        ch = true;
    }
    ui::help("Points are relative to the NPC's home (where he is made). Humans work 6:00-20:00, relax from 20:00, sleep before 6:00. A post the NPC "
             "cannot walk to leaves him standing: choose connected points near his home.");
    if (mode == 1) ch |= point_editor("at", r["at"]);
    if (mode == 2) {
        for (const char* k : {"work", "relax", "sleep"}) {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%-6s", k);
            ImGui::SameLine(70);
            ch |= point_editor(k, r[k]);
        }
    }
    if (mode == 3) {
        json& p = r["patrol"];
        json& pts = p["points"];
        int remove = -1;
        for (size_t i = 0; i < pts.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%zu", i + 1);
            ImGui::SameLine(40);
            ch |= point_editor("pt", pts[i]);
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) remove = static_cast<int>(i);
            ImGui::PopID();
        }
        if (remove >= 0) {
            pts.erase(static_cast<size_t>(remove));
            ch = true;
        }
        if (ImGui::SmallButton("+ waypoint")) {
            pts.push_back(json::object({{"dx", 0}, {"dy", 0}, {"dz", 0}}));
            ch = true;
        }
        std::string name = p.value("name", std::string());
        ImGui::SetNextItemWidth(200);
        if (ImGui::InputText("Patrol group name", &name)) {
            if (name.empty()) p.erase("name");
            else p["name"] = name;
            ch = true;
        }
    }
    return ch;
}

class NpcsPanel : public Panel {
public:
    const char* title() const override { return kPanelNpcs; }

    void attach(App& app) override {
        app.template_actions.push_back({"New NPC template based on this", {TplKind::Npc, TplKind::Monster},
                                        [this](App& a, const TemplateInfo& t) { add_template(a, t.name, Mode::New); }});
        app.template_actions.push_back({"Change this NPC", {TplKind::Npc, TplKind::Monster}, [this](App& a, const TemplateInfo& t) { add_template(a, t.name, Mode::Edit); }});
        app.template_actions.push_back({"Place one", {TplKind::Npc, TplKind::Monster}, [this](App& a, const TemplateInfo& t) { add_placed(a, t.name); }});
    }
    void on_project_changed(App& app) override {
        if (!tpl(app.project, sel_) && !placed(app.project, sel_)) sel_.clear();
    }
    bool select(App& app, const std::string& key) override {
        for (auto& n : app.project.npc_templates)
            if (n.name == key || n.uid == key) {
                sel_ = n.uid;
                app.focus_panel(title());
                return true;
            }
        for (auto& n : app.project.npcs)
            if (n.name == key || n.uid == key) {
                sel_ = n.uid;
                app.focus_panel(title());
                return true;
            }
        return false;
    }

    void add_template(App& app, const std::string& name, Mode mode) {
        if (mode == Mode::Edit)
            for (auto& n : app.project.npc_templates)
                if (n.mode == Mode::Edit && n.name == name) {
                    sel_ = n.uid;
                    app.focus_panel(title());
                    return;
                }
        NpcTemplateDef d;
        d.uid = new_uid();
        d.mode = mode;
        if (mode == Mode::New) {
            d.base = name;
            d.name = ui::unique_template_name(app.project, app.game.get(), ui::name_prefix(app.project) + "_" + (name.empty() ? std::string("Npc") : name));
        } else {
            d.name = name;
        }
        app.project.npc_templates.push_back(d);
        sel_ = d.uid;
        app.touch();
        app.focus_panel(title());
    }
    void add_placed(App& app, const std::string& template_name) {
        PlacedNpc n;
        n.uid = new_uid();
        n.template_name = template_name;
        n.name = ui::unique_template_name(app.project, app.game.get(), ui::name_prefix(app.project) + "_" + (template_name.empty() ? std::string("Npc") : template_name));
        n.offset = true;
        n.position = {150, 20, 150};
        app.project.npcs.push_back(n);
        sel_ = n.uid;
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
        if (NpcTemplateDef* t = tpl(app.project, sel_)) draw_template(app, *t);
        else if (PlacedNpc* n = placed(app.project, sel_)) draw_placed(app, *n);
        else ImGui::TextDisabled("NPC templates are kinds of NPCs and monsters (numbers, guild, loot); placed NPCs are persons in the world (a name, a place, "
                                 "an inventory, a routine). Select an NPC or monster in the Browser for more actions.");
        ImGui::PopItemWidth();
        ImGui::EndChild();
        ImGui::End();
    }

private:
    static NpcTemplateDef* tpl(Project& p, const std::string& uid) {
        for (auto& n : p.npc_templates)
            if (n.uid == uid) return &n;
        return nullptr;
    }
    static PlacedNpc* placed(Project& p, const std::string& uid) {
        for (auto& n : p.npcs)
            if (n.uid == uid) return &n;
        return nullptr;
    }

    void draw_list(App& app) {
        Project& p = app.project;
        if (ImGui::Button("+ Template")) add_template(app, "Peasant_1_01", Mode::New);
        ImGui::SameLine();
        if (ImGui::Button("+ Change shipped")) ImGui::OpenPopup("##pick_edit");
        ImGui::SameLine();
        if (ImGui::Button("+ Place NPC")) add_placed(app, p.npc_templates.empty() ? std::string("Peasant_1_01") : p.npc_templates.front().name);
        if (ImGui::BeginPopup("##pick_edit")) {
            static std::string name;
            ImGui::TextUnformatted("Which shipped NPC or monster?");
            ui::template_picker("NPC", name, app.game.get(), {TplKind::Npc, TplKind::Monster}, {}, 280);
            if (ImGui::Button("Add") && !name.empty()) {
                add_template(app, name, Mode::Edit);
                name.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        std::string remove;
        ImGui::SeparatorText("Templates");
        for (auto& n : p.npc_templates) {
            ImGui::PushID(n.uid.c_str());
            if (ImGui::Selectable(n.name.empty() ? "(unnamed)" : n.name.c_str(), sel_ == n.uid)) sel_ = n.uid;
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Place one")) {
                    const std::string t = n.name;
                    app.defer([this, &app, t] { add_placed(app, t); });
                }
                if (ImGui::MenuItem("Delete")) remove = n.uid;
                ImGui::EndPopup();
            }
            ImGui::SameLine(190);
            ImGui::TextDisabled("%s", n.mode == Mode::New ? "new" : "change");
            ImGui::PopID();
        }
        ImGui::SeparatorText("Placed NPCs");
        for (auto& n : p.npcs) {
            ImGui::PushID(n.uid.c_str());
            if (ImGui::Selectable(n.name.empty() ? "(unnamed)" : n.name.c_str(), sel_ == n.uid)) sel_ = n.uid;
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Duplicate")) {
                    PlacedNpc d = n;
                    d.uid = new_uid();
                    d.name = ui::unique_template_name(p, app.game.get(), d.name);
                    app.defer([&app, d] {
                        app.project.npcs.push_back(d);
                        app.touch();
                    });
                }
                if (ImGui::MenuItem("Delete")) remove = n.uid;
                ImGui::EndPopup();
            }
            ImGui::SameLine(190);
            ImGui::TextDisabled("%s", n.template_name.c_str());
            ImGui::PopID();
        }
        if (!remove.empty()) {
            p.npc_templates.erase(std::remove_if(p.npc_templates.begin(), p.npc_templates.end(), [&](const NpcTemplateDef& n) { return n.uid == remove; }),
                                  p.npc_templates.end());
            p.npcs.erase(std::remove_if(p.npcs.begin(), p.npcs.end(), [&](const PlacedNpc& n) { return n.uid == remove; }), p.npcs.end());
            app.touch();
        }
    }

    void draw_template(App& app, NpcTemplateDef& n) {
        const GameData* g = app.game.get();
        bool ch = false;
        const TemplateInfo* target = g ? g->tpl(n.mode == Mode::New ? n.base : n.name) : nullptr;
        NpcStats s;
        if (g && target) s = npc_stats(*g, *target);
        if (n.mode == Mode::New) {
            ImGui::TextDisabled("NEW NPC TEMPLATE: a clone of the base (a human looks like his base: heads and body parts are separate items)");
            ch |= ui::input_text("Name", n.name);
            if (std::string why = template_name_problem(n.name); !why.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", why.c_str());
            ch |= ui::template_picker("Base", n.base, g, {TplKind::Npc, TplKind::Monster});
        } else {
            ImGui::TextDisabled("CHANGE OF A SHIPPED NPC OR MONSTER: new spawns follow at once, the ones in the world are rewritten at the session start");
            ch |= ui::template_picker("NPC", n.name, g, {TplKind::Npc, TplKind::Monster});
        }
        if (g && target) {
            ImGui::SameLine();
            ImGui::TextDisabled("\"%s\" %s %s", g->label(target->name).c_str(), s.species.c_str(), s.guild.c_str());
        }
        ch |= ui::lang_text("Label", n.label, app.project.mod.languages);

        ui::section("Numbers");
        ch |= ui::opt_int("Hit points", n.hp_max, opt_str(s.hp_max).c_str(), s.hp_max.value_or(100));
        ch |= ui::opt_double("Hit point factor", n.hp_mult, nullptr, 1.0);
        ch |= ui::opt_int("Level", n.level, opt_str(s.level).c_str(), s.level.value_or(1));
        ch |= ui::opt_int("Level added", n.level_add, nullptr, 1);
        ch |= ui::opt_int("Experience for a kill", n.xp, opt_str(s.xp).c_str(), s.xp.value_or(50));
        ui::help("A kill pays 10 * level, at least 50: setting the experience sets the level.");
        ch |= ui::opt_int("Mana", n.mana_max, opt_str(s.mana_max).c_str(), s.mana_max.value_or(0));
        ch |= ui::opt_double("Damage factor", n.damage_mult, nullptr, 1.0);
        ui::help("Scales Strength (monsters bite with their Strength) and the NPC's own damage.");
        ch |= ui::opt_skills("Replace skills", n.skills, s.skills, npc_skill_names(g),
                             "Attributes (Strength, Dexterity, Intelligence 0..200), protection (ProtEdge ...), combat skills (Sword ... 0..10), magic.");

        ui::section("Identity");
        ch |= ui::opt_choice("Guild", n.guild, guild_names(), s.guild);
        ch |= ui::opt_choice("Species", n.species, species_names(), s.species);
        ch |= ui::opt_choice("Attitude", n.attitude, attitude_names(), s.attitude);
        ch |= ui::opt_choice("Bravery", n.bravery, bravery_names(), s.bravery);
        ch |= ui::opt_choice("Damage calculation", n.damage_calc, damage_calc_names(), s.damage_calc);
        ch |= ui::opt_text("Combat species", n.combat_species, s.combat_species.c_str());
        ch |= ui::opt_text("AI group", n.ai_group, s.ai_group.c_str());
        {
            bool on = n.mesh.has_value();
            if (ImGui::Checkbox("##mesh_on", &on)) {
                if (on) n.mesh = s.mesh;
                else n.mesh.reset();
                ch = true;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!n.mesh);
            std::string m = n.mesh ? *n.mesh : s.mesh;
            if (ui::asset_picker("Actor (monsters)", m, g, "_xmac", false) && n.mesh) {
                // the archives hold "X._xmac", templates name the source "X.xac"
                const auto dot = m.rfind("._xmac");
                n.mesh = dot != std::string::npos ? m.substr(0, dot) + ".xac" : m;
                ch = true;
            }
            ImGui::EndDisabled();
        }

        ui::section("Loot");
        bool loot_on = n.loot.has_value();
        if (ImGui::Checkbox("Set loot amounts", &loot_on)) {
            if (loot_on) n.loot = IntMap{};
            else n.loot.reset();
            ch = true;
        }
        ui::help("Target amounts the template carries ({ It_Apple = 3 }; 0 removes). Reaches NPCs spawned afterwards, not existing ones.");
        if (n.loot) {
            std::vector<Ingredient> rows;
            for (auto& [k, v] : *n.loot) rows.push_back({k, v});
            if (ui::ingredient_list("loot", rows, app, ui::project_item_names(app.project), 64)) {
                IntMap m;
                for (auto& r : rows)
                    if (!r.item.empty()) m[r.item] = r.count;
                    else m[""] = r.count;
                n.loot = m;
                ch = true;
            }
        }
        ui::section("Other properties");
        ch |= ui::prop_edits("props", n.properties, g, target);
        ui::section("Notes");
        ch |= ui::input_multiline("##notes", n.notes, 3);
        if (ch) app.touch();
    }

    void draw_placed(App& app, PlacedNpc& n) {
        const GameData* g = app.game.get();
        bool ch = false;
        ImGui::TextDisabled("PLACED NPC: made in a new game, restored after a load, never twice (risen.npc.persistent)");
        ch |= ui::input_text("Unique name", n.name);
        ui::help("The entity name (letters, digits, _ and -): dialogues name their owner by it; it is the key of the NPC in savegames.");
        if (std::string why = npc_name_problem(n.name); !why.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", why.c_str());
        std::vector<std::string> mine;
        for (auto& t : app.project.npc_templates)
            if (t.mode == Mode::New) mine.push_back(t.name);
        ch |= ui::template_picker("Template", n.template_name, g, {TplKind::Npc, TplKind::Monster}, mine);
        ch |= ui::lang_text("Label", n.label, app.project.mod.languages);

        ui::section("Where");
        ch |= ImGui::Checkbox("Offset from the player (when first made in a savegame)", &n.offset);
        float pos[3] = {static_cast<float>(n.position[0]), static_cast<float>(n.position[1]), static_cast<float>(n.position[2])};
        if (ImGui::InputFloat3(n.offset ? "dx dy dz" : "x y z", pos, "%.1f")) {
            n.position = {pos[0], pos[1], pos[2]};
            ch = true;
        }
        ui::help("+y is up. The World view (a later stage) places NPCs with the mouse; until then type a position or use an offset from the player.");

        ui::section("Person");
        ch |= ImGui::Checkbox("Peaceful", &n.peaceful);
        ui::help("A citizen unless a guild is given: pirates, guards and monsters leave him alone (a shopkeeper, a quest giver).");
        ch |= ui::opt_choice("Guild", n.guild, guild_names(), "");
        ch |= ui::opt_int("Level", n.level, nullptr, 1);
        ch |= ui::opt_int("Hit points", n.hp_max, nullptr, 100);
        ui::section("Inventory");
        ImGui::TextDisabled("Items he carries; \"equip\" wears / holds the item when he is made.");
        ch |= ui::inventory_list("inv", n.items, app, ui::project_item_names(app.project), true);
        ui::section("Routine");
        ch |= routine_editor(n.routine);
        ui::section("Lifetime");
        ch |= ImGui::Checkbox("Respawn when dead (at the next start or load)", &n.respawn);
        ImGui::SetNextItemWidth(120);
        ch |= ImGui::InputInt("Version", &n.version);
        ui::help("Raise it to replace the NPC saved in existing savegames by one made from this declaration.");
        ui::section("Notes");
        ch |= ui::input_multiline("##notes", n.notes, 3);
        if (ch) app.touch();
    }

    std::string sel_;
};

}  // namespace

std::unique_ptr<Panel> make_npcs_panel() { return std::make_unique<NpcsPanel>(); }

}  // namespace mk
