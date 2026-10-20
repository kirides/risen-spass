// NPC groups panel: relative changes for every NPC and monster template a selector matches ("all wolves 1.5 times as strong", "everything in
// Monster/Kapitel 2 one level higher"). Exported as the npc_groups section of risen.balance.install (data/balance.lua).
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <set>

#include "app/editor_util.h"
#include "app/panels/panels.h"
#include "app/widgets.h"
#include "core/catalog.h"
#include "core/i18n.h"
#include "core/spells.h"
#include "core/validate.h"

namespace mk {

namespace {

class NpcGroupsPanel : public Panel {
public:
    const char* title() const override { return kPanelNpcGroups; }

    void on_project_changed(App& app) override {
        if (!find(app.project, sel_)) sel_.clear();
        match_sig_.clear();
    }
    bool select(App& app, const std::string& key) override {
        for (auto& g : app.project.npc_groups)
            if (g.uid == key) {
                sel_ = g.uid;
                app.focus_panel(title());
                return true;
            }
        return false;
    }

    void draw(App& app) override {
        if (!ImGui::Begin(ui::panel_label(title()).c_str(), &open)) {
            ImGui::End();
            return;
        }
        ImGui::BeginChild("##list", ImVec2(260, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
        draw_list(app);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##editor");
        ImGui::PushItemWidth(std::min(480.0f, ImGui::GetContentRegionAvail().x * 0.6f));
        if (NpcGroup* g = find(app.project, sel_)) draw_group(app, *g);
        else ImGui::TextDisabled("%s", tr("A group changes many NPC and monster templates at once, relative to their shipped values: a pattern on the name (\"^Wolf\"), a template group path (\"Monster/Kapitel 2\") or a list of names."));
        ImGui::PopItemWidth();
        ImGui::EndChild();
        ImGui::End();
    }

private:
    static NpcGroup* find(Project& p, const std::string& uid) {
        for (auto& g : p.npc_groups)
            if (g.uid == uid) return &g;
        return nullptr;
    }
    static std::string label_of(const NpcGroup& g, size_t i) {
        if (!g.group.empty()) return g.group;
        if (!g.match.empty()) return trf("match {0}", {g.match});
        if (!g.names.empty()) return g.names.front() + (g.names.size() > 1 ? ", ..." : "");
        return trf("group {0}", {std::to_string(i + 1)});
    }

    void draw_list(App& app) {
        Project& p = app.project;
        if (ImGui::Button(tr("+ Group"))) {
            NpcGroup g;
            g.uid = new_uid();
            g.match = "^Wolf";
            g.hp_mult = 1.5;
            p.npc_groups.push_back(g);
            sel_ = g.uid;
            app.touch();
        }
        ImGui::Separator();
        std::string remove;
        for (size_t i = 0; i < p.npc_groups.size(); ++i) {
            auto& g = p.npc_groups[i];
            ImGui::PushID(g.uid.c_str());
            if (ImGui::Selectable(label_of(g, i).c_str(), sel_ == g.uid)) sel_ = g.uid;
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem(tr("Duplicate"))) {
                    NpcGroup d = g;
                    d.uid = new_uid();
                    p.npc_groups.insert(p.npc_groups.begin() + static_cast<std::ptrdiff_t>(i) + 1, d);
                    sel_ = d.uid;
                    app.touch();
                    ImGui::EndPopup();
                    ImGui::PopID();
                    break;
                }
                if (ImGui::MenuItem(tr("Delete"))) remove = g.uid;
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        if (!remove.empty()) {
            auto& v = p.npc_groups;
            v.erase(std::remove_if(v.begin(), v.end(), [&](const NpcGroup& x) { return x.uid == remove; }), v.end());
            app.touch();
        }
    }

    // the template groups below NPC and Monster ("Monster/Kapitel 2"), for the picker
    const std::vector<std::string>& group_paths(const GameData& g) {
        if (paths_for_ != &g) {
            paths_for_ = &g;
            std::set<std::string> s;
            for (auto& t : g.templates()) {
                if (t->kind != TplKind::Npc && t->kind != TplKind::Monster) continue;
                std::string path;
                size_t from = 0;
                while (from <= t->group.size()) {
                    const size_t slash = t->group.find('/', from);
                    path = t->group.substr(0, slash);
                    if (path.rfind("NPC", 0) == 0 || path.rfind("Monster", 0) == 0) s.insert(path);
                    if (slash == std::string::npos) break;
                    from = slash + 1;
                }
            }
            paths_.assign(s.begin(), s.end());
        }
        return paths_;
    }

    void draw_group(App& app, NpcGroup& g) {
        const GameData* gd = app.game.get();
        bool ch = false;
        ui::section(tr("Which templates"));
        ImGui::TextDisabled("%s", tr("Leave everything empty for every NPC and monster. The selectors combine: the group's templates, filtered by the patterns, plus the names."));
        if (gd) {
            std::string path = g.group;
            if (ui::combo(tr("Template group"), path, group_paths(*gd), true)) {
                g.group = path;
                ch = true;
            }
        } else {
            ch |= ui::input_text(tr("Template group"), g.group);
        }
        ch |= ui::input_text(tr("Pattern (Lua)"), g.match);
        ui::help(tr("A Lua pattern on the template name: \"^Wolf\" (starts with Wolf), \"Dead$\" (ends with Dead). ^ and $ anchor, . any character, % escapes."));
        ch |= ui::input_text(tr("Leave out (Lua)"), g.exclude);
        ImGui::TextUnformatted(tr("Also these names (one per line)"));
        ch |= ui::string_list("##names", g.names);
        // what the selector matches (recomputed when it changes)
        const std::string sig = g.group + "|" + g.match + "|" + g.exclude + "|" + [&] {
            std::string n;
            for (auto& x : g.names) n += x + ",";
            return n;
        }();
        if (gd && sig != match_sig_) {
            match_sig_ = sig;
            match_error_.clear();
            matched_ = npc_group_templates(g, *gd, match_error_);
        }
        if (gd) {
            if (!match_error_.empty()) {
                ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", match_error_.c_str());
            } else if (matched_.empty()) {
                ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", tr("matches nothing: the layer refuses a selector that matches nothing"));
            } else {
                std::string list;
                for (size_t i = 0; i < matched_.size() && i < 14; ++i) list += (i ? ", " : "") + matched_[i];
                if (matched_.size() > 14) list += ", ...";
                ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1), tr("%zu templates:"), matched_.size());
                ImGui::SameLine();
                ImGui::TextWrapped("%s", list.c_str());
            }
        }

        ui::section(tr("Relative changes"));
        ImGui::TextDisabled("%s", tr("Factors and additions work on the SHIPPED values: applying them twice never compounds."));
        ch |= ui::opt_double(tr("Hit point factor"), g.hp_mult, nullptr, 1.5);
        ch |= ui::opt_double(tr("Mana factor"), g.mana_mult, nullptr, 1.0);
        ch |= ui::opt_double(tr("Level factor"), g.level_mult, nullptr, 1.0);
        ch |= ui::opt_int(tr("Level added"), g.level_add, nullptr, 1);
        ch |= ui::opt_double(tr("Experience factor"), g.xp_mult, nullptr, 1.0);
        ch |= ui::opt_double(tr("Damage factor"), g.damage_mult, nullptr, 1.0);
        ui::help(tr("Scales Strength (monsters bite with their Strength) and the template's own damage."));
        ch |= ui::opt_skills(tr("Add to skills"), g.skills_add, IntMap{}, npc_skill_names(gd), tr("Added to the shipped value: { Strength = 10 }, negative values lower it. Results are clamped to the skill's range."));
        {
            bool on = g.skills_mult.has_value();
            if (ImGui::Checkbox(tr("Skill factors"), &on)) {
                if (on) g.skills_mult = std::map<std::string, double>{};
                else g.skills_mult.reset();
                ch = true;
            }
            ui::help(tr("{ ProtEdge = 1.1 }: the shipped value times the factor."));
            if (g.skills_mult) {
                std::string remove;
                for (auto& [k, v] : *g.skills_mult) {
                    ImGui::PushID(k.c_str());
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(k.c_str());
                    ImGui::SameLine(160);
                    ImGui::SetNextItemWidth(120);
                    if (ImGui::InputDouble("##f", &v, 0.1, 1.0, "%.2f")) ch = true;
                    ImGui::SameLine();
                    if (ImGui::SmallButton("x")) remove = k;
                    ImGui::PopID();
                }
                if (!remove.empty()) {
                    g.skills_mult->erase(remove);
                    ch = true;
                }
                static std::string add;
                ImGui::SetNextItemWidth(160);
                if (ui::combo("##addskill", add, npc_skill_names(gd), true) && !add.empty()) {
                    (*g.skills_mult)[add] = 1.0;
                    add.clear();
                    ch = true;
                }
            }
        }

        ui::section(tr("Identity"));
        ch |= ui::opt_choice(tr("Guild"), g.guild, guild_names(), "");
        ch |= ui::opt_choice(tr("Species"), g.species, species_names(), "");
        ch |= ui::opt_choice(tr("Attitude"), g.attitude, attitude_names(), "");
        ch |= ui::opt_choice(tr("Bravery"), g.bravery, bravery_names(), "");
        ch |= ui::opt_choice(tr("Damage calculation"), g.damage_calc, damage_calc_names(), "");
        ch |= ui::opt_text(tr("AI group"), g.ai_group, nullptr);
        if (g.extra.is_object() && !g.extra.empty()) {
            ui::section(tr("Kept as imported"));
            std::string keys;
            for (auto& [k, v] : g.extra.items()) keys += (keys.empty() ? "" : ", ") + k;
            ImGui::TextDisabled(tr("Settings the editor has no field for: %s (exported unchanged)"), keys.c_str());
        }
        ui::section(tr("Notes"));
        ch |= ui::input_multiline("##notes", g.notes, 3);
        if (ch) {
            match_sig_.clear();
            app.touch();
        }
    }

    std::string sel_, match_sig_, match_error_;
    std::vector<std::string> matched_;
    const GameData* paths_for_ = nullptr;
    std::vector<std::string> paths_;
};

}  // namespace

std::unique_ptr<Panel> make_npc_groups_panel() { return std::make_unique<NpcGroupsPanel>(); }

}  // namespace mk
