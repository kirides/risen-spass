// Spells panel: changes of the 21 shipped spells and new spells made of their pieces (a spell is a group of items and projectile templates,
// docs/SPELL_PROPERTIES.md). Exported as the spells / new_spells sections of risen.balance.install (data/balance.lua).
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>

#include "app/editor_util.h"
#include "app/icons.h"
#include "app/panels/panels.h"
#include "app/pickers.h"
#include "app/prop_editor.h"
#include "app/widgets.h"
#include "core/catalog.h"
#include "core/spells.h"
#include "core/validate.h"

namespace mk {

namespace {

// the number of a shipped template property ("gCDamage_PS.DamageAmount") as text, "" when absent
std::string shipped_num(const TemplateInfo* t, const char* key) {
    if (!t) return {};
    const p2m::Val* v = t->get(key);
    if (!v) return {};
    if (v->k == p2m::Val::Int) return std::to_string(v->i);
    if (v->k == p2m::Val::Float) {
        char b[32];
        std::snprintf(b, sizeof b, "%g", v->d);
        return b;
    }
    return v->s;
}

// "set" checkbox + a "yes" checkbox: an optional bool
bool opt_bool(const char* label, std::optional<bool>& v, const std::string& shipped) {
    bool changed = false;
    bool on = v.has_value();
    ImGui::PushID(label);
    if (ImGui::Checkbox("##set", &on)) {
        if (on) v = true;
        else v.reset();
        changed = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!v);
    bool b = v.value_or(false);
    if (ImGui::Checkbox(label, &b)) {
        v = b;
        changed = true;
    }
    ImGui::EndDisabled();
    if (!shipped.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", shipped.c_str());
    }
    ImGui::PopID();
    return changed;
}

class SpellsPanel : public Panel {
public:
    const char* title() const override { return kPanelSpells; }

    void on_project_changed(App& app) override {
        if (!find(app.project, sel_)) sel_.clear();
    }
    bool select(App& app, const std::string& key) override {
        for (auto& s : app.project.spells)
            if (s.uid == key || s.name == key) {
                sel_ = s.uid;
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
        ImGui::BeginChild("##list", ImVec2(260, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
        draw_list(app);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##editor");
        ImGui::PushItemWidth(std::min(480.0f, ImGui::GetContentRegionAvail().x * 0.6f));
        if (SpellDef* s = find(app.project, sel_)) draw_spell(app, *s);
        else ImGui::TextDisabled("Change a shipped spell (mana cost, requirements, projectile damage) or make a new one from the pieces of a shipped one.");
        ImGui::PopItemWidth();
        ImGui::EndChild();
        ImGui::End();
    }

private:
    static SpellDef* find(Project& p, const std::string& uid) {
        for (auto& s : p.spells)
            if (s.uid == uid) return &s;
        return nullptr;
    }

    void add(App& app, Mode mode, const std::string& shipped) {
        if (mode == Mode::Edit)
            for (auto& s : app.project.spells)
                if (s.mode == Mode::Edit && s.name == shipped) {
                    sel_ = s.uid;
                    return;
                }
        SpellDef d;
        d.uid = new_uid();
        d.mode = mode;
        if (mode == Mode::Edit) {
            d.name = shipped;
        } else {
            d.base = shipped;
            d.name = ui::unique_template_name(app.project, app.game.get(), ui::name_prefix(app.project) + "_" + shipped);
            const ShippedSpell* sp = shipped_spell(shipped);
            if (sp) d.forms = {sp->forms.front()};
        }
        app.project.spells.push_back(d);
        sel_ = d.uid;
        app.touch();
    }

    void draw_list(App& app) {
        Project& p = app.project;
        if (ImGui::Button("+ Change shipped")) ImGui::OpenPopup("##pick_edit");
        ImGui::SameLine();
        if (ImGui::Button("+ New spell")) ImGui::OpenPopup("##pick_new");
        for (const char* pop : {"##pick_edit", "##pick_new"}) {
            if (!ImGui::BeginPopup(pop)) continue;
            ImGui::TextUnformatted(pop[6] == 'e' ? "Which shipped spell?" : "Made from which shipped spell?");
            for (auto& s : shipped_spells()) {
                if (ImGui::Selectable((s.name + (s.projectile ? "   (crystal, projectile)" : "   (rune, scroll, orb)")).c_str())) {
                    const std::string name = s.name;
                    const Mode m = pop[6] == 'e' ? Mode::Edit : Mode::New;
                    app.defer([this, &app, m, name] { add(app, m, name); });
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
        ImGui::Separator();
        std::string remove;
        for (auto& s : p.spells) {
            ImGui::PushID(s.uid.c_str());
            if (ImGui::Selectable(s.name.empty() ? "(unnamed)" : s.name.c_str(), sel_ == s.uid)) sel_ = s.uid;
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Delete")) remove = s.uid;
                ImGui::EndPopup();
            }
            ImGui::SameLine(170);
            ImGui::TextDisabled("%s", s.mode == Mode::New ? "new" : "change");
            ImGui::PopID();
        }
        if (!remove.empty()) {
            auto& v = p.spells;
            v.erase(std::remove_if(v.begin(), v.end(), [&](const SpellDef& x) { return x.uid == remove; }), v.end());
            app.touch();
        }
    }

    void draw_spell(App& app, SpellDef& s) {
        const GameData* g = app.game.get();
        bool ch = false;
        const ShippedSpell* base = shipped_spell(s.mode == Mode::New ? s.base : s.name);
        if (s.mode == Mode::New) {
            ImGui::TextDisabled("NEW SPELL: made of the pieces of a shipped spell (new item templates, risen.balance new_spells)");
            ch |= ui::input_text("Name", s.name);
            ui::help("The template name of the crystal / rune; scroll and orb get _Scroll and _Orb. Letters, digits and _, unique, at most 63 characters.");
            if (std::string why = template_name_problem(s.name); !why.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", why.c_str());
            std::string b = s.base;
            std::vector<std::string> names;
            for (auto& sp : shipped_spells()) names.push_back(sp.name);
            if (ui::combo("Base spell", b, names, false) && b != s.base) {
                s.base = b;
                const ShippedSpell* nb = shipped_spell(b);
                s.forms.clear();
                if (nb) s.forms = {nb->forms.front()};
                if (nb && nb->projectile != (base && base->projectile)) {  // settings of the other kind do not apply
                    s.damage = nullptr;
                    s.damage_mult.reset();
                    s.speed.reset();
                    s.speed_mult.reset();
                    s.radius.reset();
                    s.radius_mult.reset();
                    s.damage_type.reset();
                    s.effect.reset();
                    s.scroll_mana_cost.reset();
                    s.orb_mana_cost.reset();
                }
                ch = true;
                base = nb;
            }
            if (base) {
                ImGui::TextUnformatted("Forms to make");
                ImGui::SameLine();
                for (auto& f : base->forms) {
                    bool on = std::find(s.forms.begin(), s.forms.end(), f) != s.forms.end();
                    if (ImGui::Checkbox((f + "##form").c_str(), &on)) {
                        if (on) s.forms.push_back(f);
                        else s.forms.erase(std::remove(s.forms.begin(), s.forms.end(), f), s.forms.end());
                        ch = true;
                    }
                    ImGui::SameLine();
                }
                ImGui::NewLine();
                std::string made;
                for (auto& f : s.forms) made += (made.empty() ? "" : ", ") + new_spell_template(s.name, f);
                if (!made.empty()) ImGui::TextDisabled("makes: %s", made.c_str());
            }
            ch |= ui::lang_text("Label", s.label, app.project.mod.languages);
            ch |= ui::lang_text("Description", s.description, app.project.mod.languages, true);
        } else {
            ImGui::TextDisabled("CHANGE OF A SHIPPED SPELL: the shipped items and projectiles are edited (every game, savegames keep the numbers they were saved with)");
            std::string n = s.name;
            std::vector<std::string> names;
            for (auto& sp : shipped_spells()) names.push_back(sp.name);
            if (ui::combo("Spell", n, names, false)) {
                s.name = n;
                ch = true;
                base = shipped_spell(n);
            }
        }
        if (!base) {
            if (ch) app.touch();
            return;
        }
        // what the spell is made of
        {
            std::string parts;
            for (auto& f : base->forms) {
                const std::string t = spell_form_template(*base, f);
                if (!t.empty()) parts += (parts.empty() ? "" : "  ") + f + " " + t;
            }
            if (base->projectile) parts += "   projectiles " + std::to_string(base->projectiles.size());
            ImGui::TextDisabled("%s: %s", base->projectile ? "projectile spell" : "rune spell", parts.c_str());
        }
        const std::string main_form = base->projectile ? "crystal" : "rune";
        const TemplateInfo* main = g ? g->tpl(spell_form_template(*base, main_form)) : nullptr;
        const TemplateInfo* scroll = g ? g->tpl(spell_form_template(*base, "scroll")) : nullptr;
        const TemplateInfo* orb = g ? g->tpl(spell_form_template(*base, "orb")) : nullptr;
        const TemplateInfo* proj0 = g && base->projectile ? g->tpl(base->projectiles.front()) : nullptr;
        ItemStats shipped;
        if (g && main) shipped = item_stats(*g, *main);
        auto hint_int = [](const std::optional<int>& v) { return v ? std::to_string(*v) : std::string(); };

        if (s.mode == Mode::New) {
            ui::section("Item");
            std::string ic = s.icon ? *s.icon : shipped.icon;
            ui::icon(ic, 32.0f);
            ImGui::SameLine();
            ch |= ui::opt_text("Icon", s.icon, shipped.icon.c_str());
            ch |= ui::opt_int("Value (gold)", s.value, hint_int(shipped.value).c_str(), shipped.value.value_or(500));
        }
        ui::section("Costs and requirements");
        ch |= ui::opt_int("Mana cost", s.mana_cost, hint_int(shipped.mana_cost).c_str(), shipped.mana_cost.value_or(10));
        ui::help("gCCastInfo_PS.ManaCost of the crystal / rune; the cast takes exactly this from the base mana.");
        ch |= ui::opt_double("Mana factor", s.mana_mult, nullptr, 1.0);
        ui::help("Crystal, rune and scroll cost = shipped * factor (an orb stays free). Excludes an absolute mana cost.");
        if (s.mana_cost && s.mana_mult) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "a mana cost and a factor exclude each other");
        if (!base->projectile) {
            ch |= ui::opt_int("Scroll mana cost", s.scroll_mana_cost, scroll ? shipped_num(scroll, "gCCastInfo_PS.ManaCost").c_str() : "", 10);
            ch |= ui::opt_int("Orb mana cost", s.orb_mana_cost, orb ? shipped_num(orb, "gCCastInfo_PS.ManaCost").c_str() : "", 0);
        }
        ch |= ui::opt_skills("Replace requirements", s.requirements, shipped.requirements, skill_names(g),
                             base->projectile ? "The skill level to equip the crystal ({ Fireball = 2 })." : "The Circle level to equip the rune ({ Circle = 3 }); scrolls and orbs need nothing.");
        ch |= opt_bool("Used up by a cast", s.consume, shipped_num(main, "gCCastInfo_PS.ConsumeItem"));

        if (base->projectile) {
            ui::section("Projectile");
            ImGui::TextDisabled("The projectile templates (%zu level%s) carry the damage, speed and size.", base->projectiles.size(), base->projectiles.size() == 1 ? "" : "s");
            // damage: unchanged / one value / one per level
            int mode = s.damage.is_null() ? 0 : s.damage.is_array() ? 2 : 1;
            const int old_mode = mode;
            const char* modes[] = {"unchanged", "same for every level", "one value per level"};
            ImGui::SetNextItemWidth(220);
            if (ImGui::Combo("Damage", &mode, modes, base->projectiles.size() > 1 ? 3 : 2) && mode != old_mode) {
                const double shipped_d = proj0 ? std::atof(shipped_num(proj0, "gCDamage_PS.DamageAmount").c_str()) : 100.0;
                if (mode == 0) {
                    s.damage = nullptr;
                } else if (mode == 1) {
                    s.damage = shipped_d;
                } else {
                    json a = json::array();
                    for (size_t i = 0; i < base->projectiles.size(); ++i) {
                        const TemplateInfo* pt = g ? g->tpl(base->projectiles[i]) : nullptr;
                        const std::string v = shipped_num(pt, "gCDamage_PS.DamageAmount");
                        a.push_back(v.empty() ? shipped_d : std::atof(v.c_str()));
                    }
                    s.damage = a;
                }
                ch = true;
            }
            if (proj0) {
                ImGui::SameLine();
                ImGui::TextDisabled("(shipped %s)", shipped_num(proj0, "gCDamage_PS.DamageAmount").c_str());
            }
            if (s.damage.is_number()) {
                double v = s.damage.get<double>();
                if (ImGui::InputDouble("  damage", &v, 1, 10, "%.0f")) {
                    s.damage = v;
                    ch = true;
                }
            } else if (s.damage.is_array()) {
                for (size_t i = 0; i < s.damage.size(); ++i) {
                    double v = s.damage[i].is_number() ? s.damage[i].get<double>() : 0;
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::InputDouble(("  level " + std::to_string(i)).c_str(), &v, 1, 10, "%.0f")) {
                        s.damage[i] = v;
                        ch = true;
                    }
                    ImGui::PopID();
                }
            }
            ch |= ui::opt_double("Damage factor", s.damage_mult, nullptr, 1.0);
            ch |= ui::opt_choice("Damage type", s.damage_type, spell_damage_types(), shipped_num(proj0, "gCDamage_PS.DamageType"));
            ui::help("Picks the protection skill that is subtracted: Magic (ProtMagic), Ice, Fire, ...");
            ch |= ui::opt_double("Speed", s.speed, shipped_num(proj0, "gCProjectile2_PS.StartVelocity").c_str(), 15.0);
            ch |= ui::opt_double("Speed factor", s.speed_mult, nullptr, 1.0);
            ch |= ui::opt_double("Radius", s.radius, shipped_num(proj0, "gCProjectile2_PS.SweepTestRadius").c_str(), 10.0);
            ch |= ui::opt_double("Radius factor", s.radius_mult, nullptr, 1.0);
            ch |= ui::opt_text("Flying effect", s.effect, shipped_num(proj0, "gCEffect_PS.Effect").c_str());
        }
        ui::section(("Other properties of the " + main_form).c_str());
        ImGui::TextDisabled("Any property of the item by name (gCCastInfo_PS.StartTask re-uses another spell's effect), see docs/SPELL_PROPERTIES.md.");
        ch |= ui::prop_edits("props", s.properties, g, main);
        if (s.extra.is_object() && !s.extra.empty()) {
            ui::section("Kept as imported");
            std::string keys;
            for (auto& [k, v] : s.extra.items()) keys += (keys.empty() ? "" : ", ") + k;
            ImGui::TextDisabled("Settings the editor has no field for: %s (exported unchanged)", keys.c_str());
        }
        ui::section("Notes");
        ch |= ui::input_multiline("##notes", s.notes, 3);
        if (ch) app.touch();
    }

    std::string sel_;
};

}  // namespace

std::unique_ptr<Panel> make_spells_panel() { return std::make_unique<SpellsPanel>(); }

}  // namespace mk
