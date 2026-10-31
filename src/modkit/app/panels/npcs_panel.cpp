// NPCs panel: NPC and monster templates (new ones cloned from a base, changes of shipped ones: risen.balance npcs / new_npcs) and placed NPCs
// (risen.npc.persistent: name, template, position, label, guild, inventory with equipment, routine).
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
#include "core/navzones.h"
#include "core/routine.h"
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
    if (ImGui::InputFloat3(tr("dx dy dz"), d, "%.0f")) {
        pt["dx"] = d[0];
        pt["dy"] = d[1];
        pt["dz"] = d[2];
        ch = true;
    }
    ImGui::SameLine();
    std::string kind = pt.value("kind", std::string("guard"));
    ImGui::SetNextItemWidth(110);
    if (ui::combo(tr("kind"), kind, point_kinds(), false)) {
        pt["kind"] = kind;
        ch = true;
    }
    ImGui::SameLine();
    bool has_yaw = pt.contains("yaw");
    if (ImGui::Checkbox(tr("yaw"), &has_yaw)) {
        if (has_yaw) pt["yaw"] = 0;
        else pt.erase("yaw");
        ch = true;
    }
    if (has_yaw) {
        ImGui::SameLine();
        float y = pt.value("yaw", 0.0f);
        ImGui::SetNextItemWidth(80);
        if (ImGui::DragFloat("##yaw", &y, 1, -360, 360, tr("%.0f deg"))) {
            pt["yaw"] = y;
            ch = true;
        }
    }
    ImGui::PopID();
    return ch;
}

// what an NPC does at a point of this kind (RE.md section 35)
const char* kind_hint(const std::string& k) {
    if (k == "guard") return N_("stands with his hands on his hips and looks the way the yaw says");
    if (k == "lookat") return N_("stands and thinks");
    if (k == "spectator") return N_("stands with his arms crossed and watches");
    if (k == "sleep") return N_("lies down and sleeps on the ground");
    if (k == "dance") return N_("dances");
    if (k == "stand" || k == "sit" || k == "smalltalk") return N_("hangs around where he is: he does NOT walk to this point (he may sit down)");
    return "";
}

// the three fixed parts of a day as a strip: sleep 0-6, work 6-20, relax 20-24 (the clock times are the game's, they cannot be changed); a click picks the part to edit
void day_strip(const json& r, std::string& sel) {
    struct Seg {
        const char* key;
        const char* label;
        ImU32 col;
    };
    const Seg segs[3] = {{"sleep", N_("sleep"), IM_COL32(90, 130, 235, 255)}, {"work", N_("work"), IM_COL32(100, 210, 80, 255)}, {"relax", N_("relax"), IM_COL32(235, 200, 50, 255)}};
    const float w = std::max(200.0f, ImGui::GetContentRegionAvail().x - 12), h = 30;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (const Seg& s : segs) {
        int from = 0, to = 0;
        routine_slot_hours(s.key, from, to);
        const float x0 = p0.x + w * static_cast<float>(from) / 24.0f, x1 = p0.x + w * static_cast<float>(to) / 24.0f;
        ImGui::SetCursorScreenPos(ImVec2(x0, p0.y));
        ImGui::PushID(s.key);
        if (ImGui::InvisibleButton("##seg", ImVec2(x1 - x0 - 2, h))) sel = s.key;
        ImGui::PopID();
        const bool own = r.is_object() && r.contains(s.key), picked = sel == s.key;
        ImU32 fill = (s.col & 0x00ffffffu) | ((own ? (picked ? 235u : 150u) : 55u) << 24);
        dl->AddRectFilled(ImVec2(x0, p0.y), ImVec2(x1 - 2, p0.y + h), fill, 3.0f);
        if (picked) dl->AddRect(ImVec2(x0, p0.y), ImVec2(x1 - 2, p0.y + h), IM_COL32(255, 255, 255, 255), 3.0f, 0, 2.0f);
        std::string text = tr(s.label);
        if (!own && std::string(s.key) != "work") text += " (= " + std::string(tr("work")) + ")";
        const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
        if (ts.x < x1 - x0 - 6) dl->AddText(ImVec2((x0 + x1 - ts.x) * 0.5f - 1, p0.y + (h - ts.y) * 0.5f), IM_COL32(255, 255, 255, 255), text.c_str());
    }
    for (int hr : {0, 6, 12, 18, 24}) {
        char b[8];
        std::snprintf(b, sizeof b, "%d:00", hr);
        const float x = p0.x + w * static_cast<float>(hr) / 24.0f - (hr == 24 ? ImGui::CalcTextSize(b).x : hr == 0 ? 0.0f : ImGui::CalcTextSize(b).x * 0.5f);
        dl->AddText(ImVec2(x, p0.y + h + 2), IM_COL32(170, 170, 170, 255), b);
    }
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + h + ImGui::GetTextLineHeight() + 6));
    ImGui::Dummy(ImVec2(w, 1));
}

// the places of his routine against the walkable ground of the world (core/navzones.h): a place outside it is one he will not walk to
void ground_report(App& app, const PlacedNpc& n) {
    if (n.offset || n.routine.is_null() || routine_points(n.routine).empty()) return;
    if (!app.world || world_of(app.project, n.world) != app.world->name) return;
    if (!app.navmap) {
        ImGui::TextDisabled("%s", tr("Open the World view once to check these places against the walkable ground."));
        return;
    }
    bool any = false;
    for (const RoutinePoint& p : routine_points(n.routine)) {
        if (!routine_kind_walks(p.kind.empty() ? std::string("guard") : p.kind)) continue;  // he does not walk to a place of these kinds
        const Vec3 w = routine_world(n.position, p);
        const double d = app.navmap->distance_to_ground(w[0], w[1], w[2]);
        if (d == 0) continue;
        any = true;
        if (d < 0) ImGui::TextColored(ImVec4(1, 0.8f, 0.35f, 1), tr("%s: no walkable ground near this place: he will probably stay where he is."), p.label.c_str());
        else ImGui::TextColored(ImVec4(1, 0.8f, 0.35f, 1), tr("%s: %.0f units from walkable ground: he will probably not walk there."), p.label.c_str(), d);
    }
    if (!any) ImGui::TextDisabled("%s", tr("All places are on walkable ground."));
}

bool day_editor(App& app, PlacedNpc& n) {
    bool ch = false;
    json& r = n.routine;
    static std::string sel = "work";
    if (sel != "work" && sel != "relax" && sel != "sleep") sel = "work";
    ImGui::TextDisabled("%s", tr("A day has three parts at fixed hours. Click one to edit its place."));
    day_strip(r, sel);
    ImGui::PushID(sel.c_str());
    const bool own = r.is_object() && r.contains(sel);
    if (sel != "work") {
        bool has = own;
        if (ImGui::Checkbox(tr("He has his own place for this part"), &has)) {
            if (has) {
                // starts beside the work place, so a new place is never on top of the first
                Vec3 d{100, 0, 100};
                if (r.is_object() && r.contains("work") && r["work"].is_object())
                    d = {r["work"].value("dx", 0.0) + 100, r["work"].value("dy", 0.0), r["work"].value("dz", 0.0) + 100};
                routine_add_point(r, sel, d, sel == "sleep" ? "sleep" : "lookat");
            } else {
                routine_remove_point(r, sel);
            }
            ch = true;
        }
        ui::help(tr("Without one he stays at the work place at these hours (the game's own rule)."));
    }
    if (r.is_object() && r.contains(sel)) {
        ch |= point_editor(sel.c_str(), r[sel]);
        const std::string kind = r[sel].value("kind", std::string("guard"));
        if (const char* hint = kind_hint(kind); hint && *hint) ImGui::TextDisabled("%s: %s", kind.c_str(), tr(hint));
        if (!routine_kind_walks(kind)) ImGui::TextColored(ImVec4(1, 0.8f, 0.35f, 1), "%s", tr("This kind makes him hang around where he is: choose guard, lookat, spectator, sleep or dance for a place he walks to."));
        if (sel != "work" && r.contains("work") && ImGui::SmallButton(tr("Copy the work place"))) {
            r[sel] = r["work"];
            ch = true;
        }
        if (!n.offset) {
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("Place it in the World view"))) {
                const std::string target = n.uid + "#" + sel;
                app.defer([&app, target] { app.select_in(kPanelWorld, target); });
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Shows the NPC with his places and moves the chosen one with the gizmo."));
        }
    } else if (sel == "work") {
        if (ImGui::Button(tr("Give him a work place"))) {
            routine_add_point(r, "work", {200, 0, 0}, "guard");
            ch = true;
        }
    }
    ImGui::PopID();
    ui::help(tr("Points are relative to the NPC's home (where he is made). He works from 6:00 to 20:00, relaxes from 20:00 to midnight and sleeps from midnight to 6:00. "
                "He notices a change of the hour when his current task ends, which can take a minute or two. A place he cannot walk to leaves him standing: choose connected points near his home."));
    return ch;
}

bool routine_editor(App& app, PlacedNpc& n) {
    json& r = n.routine;
    bool ch = false;
    static const char* kModes[] = {N_("none"), N_("one post"), N_("day / evening / night"), N_("patrol")};
    int mode = 0;
    if (r.is_object()) {
        if (r.contains("at")) mode = 1;
        else if (r.contains("patrol")) mode = 3;
        else if (r.contains("work")) mode = 2;
    }
    ImGui::SetNextItemWidth(220);
    if (ui::combo_items(tr("Routine"), &mode, kModes, 4)) {
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
    if (mode != 2)
        ui::help(tr("Points are relative to the NPC's home (where he is made). A post the NPC cannot walk to leaves him standing: choose connected points near his home."));
    if (mode == 1) ch |= point_editor("at", r["at"]);
    if (mode == 2) ch |= day_editor(app, n);
    if (mode != 0) ground_report(app, n);
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
        if (ImGui::SmallButton(tr("+ waypoint"))) {
            pts.push_back(json::object({{"dx", 0}, {"dy", 0}, {"dz", 0}}));
            ch = true;
        }
        std::string name = p.value("name", std::string());
        ImGui::SetNextItemWidth(200);
        if (ImGui::InputText(tr("Patrol group name"), &name)) {
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
        app.template_actions.push_back({N_("New NPC template based on this"), {TplKind::Npc, TplKind::Monster},
                                        [this](App& a, const TemplateInfo& t) { add_template(a, t.name, Mode::New); }});
        app.template_actions.push_back({N_("Change this NPC"), {TplKind::Npc, TplKind::Monster}, [this](App& a, const TemplateInfo& t) { add_template(a, t.name, Mode::Edit); }});
        app.template_actions.push_back({N_("Place one"), {TplKind::Npc, TplKind::Monster}, [this](App& a, const TemplateInfo& t) { add_placed(a, t.name); }});
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
        if (NpcTemplateDef* t = tpl(app.project, sel_)) draw_template(app, *t);
        else if (PlacedNpc* n = placed(app.project, sel_)) draw_placed(app, *n);
        else ImGui::TextDisabled("%s", tr("NPC templates are kinds of NPCs and monsters (numbers, guild, loot); placed NPCs are persons in the world (a name, a place, "
                                 "an inventory, a routine). Select an NPC or monster in the Browser for more actions."));
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
        if (ImGui::Button(tr("+ Template"))) add_template(app, "Peasant_1_01", Mode::New);
        ImGui::SameLine();
        if (ImGui::Button(tr("+ Change shipped"))) ImGui::OpenPopup("##pick_edit");
        ImGui::SameLine();
        if (ImGui::Button(tr("+ Place NPC"))) add_placed(app, p.npc_templates.empty() ? std::string("Peasant_1_01") : p.npc_templates.front().name);
        if (ImGui::BeginPopup("##pick_edit")) {
            static std::string name;
            ImGui::TextUnformatted(tr("Which shipped NPC or monster?"));
            ui::template_picker(tr("NPC"), name, app.game.get(), {TplKind::Npc, TplKind::Monster}, {}, 280);
            if (ImGui::Button(tr("Add")) && !name.empty()) {
                add_template(app, name, Mode::Edit);
                name.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        std::string remove;
        ImGui::SeparatorText(tr("Templates"));
        for (auto& n : p.npc_templates) {
            ImGui::PushID(n.uid.c_str());
            if (ImGui::Selectable(n.name.empty() ? tr("(unnamed)") : n.name.c_str(), sel_ == n.uid)) sel_ = n.uid;
            ui::drag_source(n.name, ui::kind_of(app, n.name), false, n.mode == Mode::New, ui::label_of(app, n.name));
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem(tr("Place one"))) {
                    const std::string t = n.name;
                    app.defer([this, &app, t] { add_placed(app, t); });
                }
                if (ImGui::MenuItem(tr("Delete"))) remove = n.uid;
                ImGui::EndPopup();
            }
            ImGui::SameLine(190);
            ImGui::TextDisabled("%s", n.mode == Mode::New ? "new" : "change");
            ImGui::PopID();
        }
        ImGui::SeparatorText(tr("Placed NPCs"));
        for (auto& n : p.npcs) {
            ImGui::PushID(n.uid.c_str());
            if (ImGui::Selectable(n.name.empty() ? tr("(unnamed)") : n.name.c_str(), sel_ == n.uid)) sel_ = n.uid;
            ui::drag_source(n.name, TplKind::Npc, true, true, n.template_name);
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem(tr("Duplicate"))) {
                    PlacedNpc d = n;
                    d.uid = new_uid();
                    d.name = ui::unique_template_name(p, app.game.get(), d.name);
                    app.defer([&app, d] {
                        app.project.npcs.push_back(d);
                        app.touch();
                    });
                }
                if (ImGui::MenuItem(tr("Delete"))) remove = n.uid;
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
            ImGui::TextDisabled("%s", tr("NEW NPC TEMPLATE: a clone of the base (a human looks like his base: heads and body parts are separate items)"));
            ch |= ui::input_text(tr("Name"), n.name);
            if (std::string why = template_name_problem(n.name); !why.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", why.c_str());
            ch |= ui::template_picker(tr("Base"), n.base, g, {TplKind::Npc, TplKind::Monster});
        } else {
            ImGui::TextDisabled("%s", tr("CHANGE OF A SHIPPED NPC OR MONSTER: new spawns follow at once, the ones in the world are rewritten at the session start"));
            ch |= ui::template_picker(tr("NPC"), n.name, g, {TplKind::Npc, TplKind::Monster});
        }
        if (g && target) {
            ImGui::SameLine();
            ImGui::TextDisabled("\"%s\" %s %s", g->label(target->name).c_str(), s.species.c_str(), s.guild.c_str());
        }
        ch |= ui::lang_text(tr("Label"), n.label, app.project.mod.languages);

        ui::section(tr("Numbers"));
        ch |= ui::opt_int(tr("Hit points"), n.hp_max, opt_str(s.hp_max).c_str(), s.hp_max.value_or(100));
        ch |= ui::opt_double(tr("Hit point factor"), n.hp_mult, nullptr, 1.0);
        ch |= ui::opt_int(tr("Level"), n.level, opt_str(s.level).c_str(), s.level.value_or(1));
        ch |= ui::opt_int(tr("Level added"), n.level_add, nullptr, 1);
        ch |= ui::opt_int(tr("Experience for a kill"), n.xp, opt_str(s.xp).c_str(), s.xp.value_or(50));
        ui::help(tr("A kill pays 10 * level, at least 50: setting the experience sets the level."));
        ch |= ui::opt_int(tr("Mana"), n.mana_max, opt_str(s.mana_max).c_str(), s.mana_max.value_or(0));
        ch |= ui::opt_double(tr("Damage factor"), n.damage_mult, nullptr, 1.0);
        ui::help(tr("Scales Strength (monsters bite with their Strength) and the NPC's own damage."));
        ch |= ui::opt_skills(tr("Replace skills"), n.skills, s.skills, npc_skill_names(g),
                             tr("Attributes (Strength, Dexterity, Intelligence 0..200), protection (ProtEdge ...), combat skills (Sword ... 0..10), magic."));

        ui::section(tr("Identity"));
        ch |= ui::opt_choice(tr("Guild"), n.guild, guild_names(), s.guild);
        ch |= ui::opt_choice(tr("Species"), n.species, species_names(), s.species);
        ch |= ui::opt_choice(tr("Attitude"), n.attitude, attitude_names(), s.attitude);
        ch |= ui::opt_choice(tr("Bravery"), n.bravery, bravery_names(), s.bravery);
        ch |= ui::opt_choice(tr("Damage calculation"), n.damage_calc, damage_calc_names(), s.damage_calc);
        ch |= ui::opt_text(tr("Combat species"), n.combat_species, s.combat_species.c_str());
        ch |= ui::opt_text(tr("AI group"), n.ai_group, s.ai_group.c_str());
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
            if (ui::asset_picker(tr("Actor (monsters)"), m, g, "_xmac", false) && n.mesh) {
                // the archives hold "X._xmac", templates name the source "X.xac"
                const auto dot = m.rfind("._xmac");
                n.mesh = dot != std::string::npos ? m.substr(0, dot) + ".xac" : m;
                ch = true;
            }
            ImGui::EndDisabled();
        }

        ui::section(tr("Loot"));
        // what the template carries as shipped (the base's for a new template), read from the game's files
        std::map<std::string, int> shipped_loot;
        if (target && target->inventory_known) {
            for (auto& st : target->inventory)
                if (st.resolved) shipped_loot[st.item] += st.amount;
            if (shipped_loot.empty()) {
                ImGui::TextDisabled("%s", tr("Shipped: carries nothing."));
            } else {
                ImGui::TextDisabled("%s", tr("Shipped:"));
                for (auto& [item, amount] : shipped_loot) {
                    ImGui::SameLine();
                    ui::item_icon(item, ImGui::GetFrameHeight());
                    ImGui::SameLine(0, 2);
                    ImGui::TextDisabled("%dx %s", amount, item.c_str());
                }
            }
            if (!target->worn.empty()) {
                std::string worn;
                for (auto& [slot, item] : target->worn) worn += (worn.empty() ? "" : ", ") + item;
                ImGui::TextDisabled(tr("Wears: %s"), worn.c_str());
            }
        } else if (target) {
            ImGui::TextDisabled("%s", tr("Shipped: not readable for this template."));
        }
        bool loot_on = n.loot.has_value();
        if (ImGui::Checkbox(tr("Set loot amounts"), &loot_on)) {
            if (loot_on) n.loot = IntMap{};
            else n.loot.reset();
            ch = true;
        }
        ui::help(tr("Target amounts the template carries ({ It_Apple = 3 }; 0 removes). Reaches NPCs spawned afterwards, not existing ones."));
        if (n.loot) {
            if (!shipped_loot.empty() && ImGui::SmallButton(tr("Start from the shipped loot"))) {
                IntMap m = *n.loot;
                for (auto& [item, amount] : shipped_loot) m[item] = amount;
                n.loot = m;
                ch = true;
            }
            ImGui::SameLine();
            ui::hint(tr("an amount of 0 takes the item away, an item the template does not carry is added"));
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
        ui::section(tr("Other properties"));
        ch |= ui::prop_edits("props", n.properties, g, target);
        ui::section(tr("Behaviours (every NPC of this template)"));
        ch |= ui::behavior_uses("bhv", n.behaviors, app);
        ui::help(tr("Scripts that run on every NPC of this template: the ones in the world and every later spawn (risen.behavior.attach_template). "
                 "Works on a shipped template too (Change shipped): every wolf of the game, ..."));
        ui::section(tr("Notes"));
        ch |= ui::input_multiline("##notes", n.notes, 3);
        if (ch) app.touch();
    }

    void draw_placed(App& app, PlacedNpc& n) {
        const GameData* g = app.game.get();
        bool ch = false;
        ImGui::TextDisabled("%s", tr("PLACED NPC: made in a new game, restored after a load, never twice (risen.npc.persistent)"));
        ch |= ui::input_text(tr("Unique name"), n.name);
        ui::help(tr("The entity name (letters, digits, _ and -): dialogues name their owner by it; it is the key of the NPC in savegames."));
        if (std::string why = npc_name_problem(n.name); !why.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", why.c_str());
        std::vector<std::string> mine;
        for (auto& t : app.project.npc_templates)
            if (t.mode == Mode::New) mine.push_back(t.name);
        ch |= ui::template_picker(tr("Template"), n.template_name, g, {TplKind::Npc, TplKind::Monster}, mine);
        ch |= ui::lang_text(tr("Label"), n.label, app.project.mod.languages);

        ui::section(tr("Where"));
        ch |= ui::world_combo(tr("World"), n.world, app);
        ch |= ImGui::Checkbox(tr("Offset from the player (when first made in a savegame)"), &n.offset);
        float pos[3] = {static_cast<float>(n.position[0]), static_cast<float>(n.position[1]), static_cast<float>(n.position[2])};
        if (ImGui::InputFloat3(n.offset ? tr("dx dy dz") : tr("x y z"), pos, "%.1f")) {
            n.position = {pos[0], pos[1], pos[2]};
            ch = true;
        }
        ui::help(tr("+y is up. The World view (a later stage) places NPCs with the mouse; until then type a position or use an offset from the player."));

        ui::section(tr("Person"));
        ch |= ImGui::Checkbox(tr("Peaceful"), &n.peaceful);
        ui::help(tr("A citizen unless a guild is given: pirates, guards and monsters leave him alone (a shopkeeper, a quest giver)."));
        ch |= ui::opt_choice(tr("Guild"), n.guild, guild_names(), "");
        ch |= ui::opt_int(tr("Level"), n.level, nullptr, 1);
        ch |= ui::opt_int(tr("Hit points"), n.hp_max, nullptr, 100);
        ui::section(tr("Inventory"));
        ImGui::TextDisabled("%s", tr("Items he carries; \"equip\" wears / holds the item when he is made."));
        ch |= ui::inventory_list("inv", n.items, app, ui::project_item_names(app.project), true);
        ui::section(tr("Routine"));
        ch |= routine_editor(app, n);
        ui::section(tr("Behaviours"));
        ch |= ui::behavior_uses("bhv", n.behaviors, app);
        ui::help(tr("Scripts that run on him while he is in the world (Behaviours panel): greet the player, guard a spot, react to a quest of the game, ... "
                 "Their parameters can be set per NPC."));
        ui::section(tr("Appears when"));
        ch |= ui::condition_list("appear", n.appear_when, app, tr("from the start of a game"));
        ui::help(tr("He comes into the world only once this holds, e.g. after a quest of the game was solved (quest Rufo_GoToDruidHut is Success): enemies "
                 "that ambush the player afterwards, a messenger, a new trader. Nothing of him exists before; checked again after every load."));
        ui::section(tr("Leaves when"));
        ch |= ui::condition_list("leave", n.leave_when, app, "never");
        ui::help(tr("He is removed from the world once this holds (a quest is over, a variable is set)."));
        ui::section(tr("Lifetime"));
        ch |= ImGui::Checkbox(tr("Respawn when dead (at the next start or load)"), &n.respawn);
        ImGui::SetNextItemWidth(120);
        ch |= ImGui::InputInt(tr("Version"), &n.version);
        ui::help(tr("Raise it to replace the NPC saved in existing savegames by one made from this declaration."));
        ui::section(tr("Notes"));
        ch |= ui::input_multiline("##notes", n.notes, 3);
        if (ch) app.touch();
    }

    std::string sel_;
};

}  // namespace

std::unique_ptr<Panel> make_npcs_panel() { return std::make_unique<NpcsPanel>(); }

}  // namespace mk
