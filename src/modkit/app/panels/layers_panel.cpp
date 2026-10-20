// Layers panel (stage E21): mod layers, named groups of the project's placed NPCs, world spawns and container fills that are switched on and off as a whole,
// like the game's story sectors. A layer may switch itself on / off the first time a condition holds; event graphs switch it too (Layer node).
// A plan view (x / z of the members, one colour per layer) shows what belongs where. Exported as data/layers.lua (docs/editor/REQUIREMENTS.md 7.6).
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <map>

#include "app/app.h"
#include "app/editor_util.h"
#include "app/event_ui.h"
#include "app/panels/panels.h"
#include "app/widgets.h"
#include "core/i18n.h"
#include "core/layers.h"

namespace mk {

namespace {

ImU32 to_im(unsigned argb, float alpha = 1.0f) {
    const unsigned a = static_cast<unsigned>(static_cast<float>((argb >> 24) & 0xff) * alpha);
    return IM_COL32((argb >> 16) & 0xff, (argb >> 8) & 0xff, argb & 0xff, a);
}

template <class T>
bool contains(const std::vector<T>& v, const T& x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

class LayersPanel : public Panel {
public:
    const char* title() const override { return kPanelLayers; }

    void on_project_changed(App& app) override {
        if (!find(app.project, sel_)) sel_.clear();
    }
    bool select(App& app, const std::string& key) override {
        for (auto& l : app.project.layers)
            if (l.uid == key || l.name == key) {
                sel_ = l.uid;
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
        ImGui::BeginChild("##list", ImVec2(220, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
        draw_list(app);
        ImGui::EndChild();
        ImGui::SameLine();
        if (sel_.empty() && !app.project.layers.empty()) sel_ = app.project.layers.front().uid;
        LayerDef* l = find(app.project, sel_);
        ImGui::BeginGroup();
        if (!l) {
            ImGui::TextWrapped("%s", tr("Create a layer: a group of placed NPCs, world spawns and container fills that is switched on and off as a whole (like the game's story "
                               "sectors). It can switch itself when a condition first holds, and the event graphs switch it with a Layer node."));
        } else {
            ImGui::BeginChild("##edit", ImVec2(ImGui::GetContentRegionAvail().x * 0.55f, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
            ImGui::PushItemWidth(-110);
            draw_layer(app, *l);
            ImGui::PopItemWidth();
            ImGui::EndChild();
            ImGui::SameLine();
        }
        ImGui::BeginChild("##plan", ImVec2(0, 0), ImGuiChildFlags_Borders);
        draw_plan(app);
        ImGui::EndChild();
        ImGui::EndGroup();
        ImGui::End();
    }

private:
    static LayerDef* find(Project& p, const std::string& uid) {
        for (auto& l : p.layers)
            if (l.uid == uid) return &l;
        return nullptr;
    }

    void add_layer(App& app) {
        LayerDef l;
        l.uid = new_uid();
        for (int i = 1;; ++i) {
            const std::string name = "Layer_" + std::to_string(i);
            if (!find_layer(app.project, name)) {
                l.name = name;
                break;
            }
        }
        app.project.layers.push_back(l);
        sel_ = l.uid;
        app.touch();
    }

    void draw_list(App& app) {
        if (ImGui::Button(tr("+ New layer"))) add_layer(app);
        ImGui::Separator();
        std::string remove;
        size_t index = 0;
        for (auto& l : app.project.layers) {
            ImGui::PushID(l.uid.c_str());
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + 6, p.y + ImGui::GetTextLineHeight()), to_im(layer_color(index)));
            ImGui::SetCursorScreenPos(ImVec2(p.x + 10, p.y));
            if (ImGui::Selectable((l.name.empty() ? std::string("(unnamed)") : l.name).c_str(), sel_ == l.uid)) sel_ = l.uid;
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem(tr("Delete"))) remove = l.uid;
                ImGui::EndPopup();
            }
            ImGui::TextDisabled(tr("   %s, %zu NPCs, %zu spawns, %zu fills"), l.enabled ? tr("on at start") : tr("off at start"), l.npcs.size(), l.spawns.size(), l.containers.size());
            ImGui::PopID();
            ++index;
        }
        if (!remove.empty()) {
            auto& v = app.project.layers;
            v.erase(std::remove_if(v.begin(), v.end(), [&](const LayerDef& x) { return x.uid == remove; }), v.end());
            app.touch();
        }
    }

    // a condition set with an on / off switch ("never by itself" = null)
    bool condition_block(App& app, const char* title, const char* help, json& set) {
        bool ch = false;
        bool on = !set.is_null();
        ImGui::PushID(title);
        if (ImGui::Checkbox(title, &on)) {
            set = on ? event_default_condition_set() : json(nullptr);
            ch = true;
        }
        ui::help(help);
        if (on) {
            ImGui::Indent();
            ch |= ui::event_condition_set(app, set, true);
            ImGui::Unindent();
        }
        ImGui::PopID();
        return ch;
    }

    void draw_layer(App& app, LayerDef& l) {
        bool ch = false;
        ch |= ImGui::InputText(tr("Name"), &l.name);
        ui::help(tr("Letters, digits, _: the key of the layer's state in savegames. Keep it once released."));
        ch |= ImGui::Checkbox(tr("On when a game starts"), &l.enabled);
        ui::help(tr("Off: the members are not made until something switches the layer on (a condition below, or an event graph)."));
        ImGui::SeparatorText(tr("Switches by itself"));
        ch |= condition_block(app, tr("Switches on when"), tr("The first time this holds (looked at about twice a second; once per savegame) the layer goes on."), l.enable_when);
        ch |= condition_block(app, tr("Switches off when"), tr("The first time this holds the layer goes off (once per savegame)."), l.disable_when);
        ImGui::SeparatorText(tr("Members"));
        ImGui::TextDisabled("%s", tr("NPCs are made while the layer is on and removed when it goes off. World spawns are made when it is on and taken away when it goes off. "
                            "Container fills happen once, the first time the layer is on. An object belongs to one layer."));
        ch |= members(app, l);
        ImGui::SeparatorText(tr("Notes"));
        ch |= ImGui::InputTextMultiline("##notes", &l.notes, ImVec2(-FLT_MIN, 50));
        if (ch) app.touch();
    }

    bool members(App& app, LayerDef& l) {
        bool ch = false;
        const Project& p = app.project;
        // NPCs
        ImGui::TextUnformatted(tr("NPCs"));
        std::string drop;
        for (auto& n : l.npcs) {
            ImGui::PushID(n.c_str());
            if (ImGui::SmallButton("x")) drop = n;
            ImGui::SameLine();
            ImGui::TextUnformatted(n.c_str());
            ImGui::PopID();
        }
        if (!drop.empty()) {
            l.npcs.erase(std::remove(l.npcs.begin(), l.npcs.end(), drop), l.npcs.end());
            ch = true;
        }
        if (ImGui::BeginCombo("##add_npc", tr("+ Add a placed NPC..."))) {
            for (auto& n : p.npcs) {
                if (layer_of_npc(p, n.name)) continue;
                if (ImGui::Selectable(n.name.c_str())) {
                    l.npcs.push_back(n.name);
                    ch = true;
                }
            }
            ImGui::EndCombo();
        }
        // world spawns
        ImGui::TextUnformatted(tr("World spawns"));
        std::string drop_uid;
        for (auto& u : l.spawns) {
            ImGui::PushID(u.c_str());
            if (ImGui::SmallButton("x")) drop_uid = u;
            ImGui::SameLine();
            ImGui::TextUnformatted(spawn_label(p, u).c_str());
            ImGui::PopID();
        }
        if (!drop_uid.empty()) {
            l.spawns.erase(std::remove(l.spawns.begin(), l.spawns.end(), drop_uid), l.spawns.end());
            ch = true;
        }
        if (ImGui::BeginCombo("##add_spawn", tr("+ Add a world spawn..."))) {
            for (auto& s : p.world.spawns) {
                if (layer_of_spawn(p, s.uid)) continue;
                if (ImGui::Selectable(spawn_label(p, s.uid).c_str())) {
                    l.spawns.push_back(s.uid);
                    ch = true;
                }
            }
            ImGui::EndCombo();
        }
        // container fills
        ImGui::TextUnformatted(tr("Container fills"));
        drop_uid.clear();
        for (auto& u : l.containers) {
            ImGui::PushID(u.c_str());
            if (ImGui::SmallButton("x")) drop_uid = u;
            ImGui::SameLine();
            ImGui::TextUnformatted(container_label(p, u).c_str());
            ImGui::PopID();
        }
        if (!drop_uid.empty()) {
            l.containers.erase(std::remove(l.containers.begin(), l.containers.end(), drop_uid), l.containers.end());
            ch = true;
        }
        if (ImGui::BeginCombo("##add_container", tr("+ Add a container fill..."))) {
            for (auto& c : p.world.containers) {
                if (layer_of_container(p, c.uid)) continue;
                if (ImGui::Selectable(container_label(p, c.uid).c_str())) {
                    l.containers.push_back(c.uid);
                    ch = true;
                }
            }
            ImGui::EndCombo();
        }
        return ch;
    }

    static std::string spawn_label(const Project& p, const std::string& uid) {
        for (auto& s : p.world.spawns)
            if (s.uid == uid) {
                char buf[96];
                std::snprintf(buf, sizeof buf, "%s  (%.0f, %.0f)", s.template_name.c_str(), s.position[0], s.position[2]);
                return buf;
            }
        return tr("(missing spawn)");
    }
    static std::string container_label(const Project& p, const std::string& uid) {
        for (auto& c : p.world.containers)
            if (c.uid == uid) return trf("{0}  ({1} items)", {c.entity_name.empty() ? c.guid : c.entity_name, std::to_string(c.items.size())});
        return tr("(missing container)");
    }

    // the plan: x / z of every member that has a position, a colour per layer (grey: in no layer)
    void draw_plan(App& app) {
        const Project& p = app.project;
        struct Dot {
            double x, z;
            int layer;  // -1 = none
            std::string text;
        };
        std::vector<Dot> dots;
        auto layer_index = [&](const LayerDef* l) { return l ? static_cast<int>(l - p.layers.data()) : -1; };
        for (auto& n : p.npcs) {
            if (n.offset) continue;
            dots.push_back({n.position[0], n.position[2], layer_index(layer_of_npc(p, n.name)), "NPC " + n.name + " (" + n.template_name + ")"});
        }
        for (auto& s : p.world.spawns) dots.push_back({s.position[0], s.position[2], layer_index(layer_of_spawn(p, s.uid)), "spawn " + s.template_name});
        ImGui::TextUnformatted(tr("Plan (x / z)"));
        const ImVec2 size = ImGui::GetContentRegionAvail();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(20, 22, 26, 255));
        if (dots.empty()) {
            dl->AddText(ImVec2(origin.x + 8, origin.y + 8), IM_COL32(150, 150, 150, 255), tr("Placed NPCs and world spawns with a position appear here."));
            return;
        }
        double minx = dots[0].x, maxx = dots[0].x, minz = dots[0].z, maxz = dots[0].z;
        for (auto& d : dots) {
            minx = std::min(minx, d.x), maxx = std::max(maxx, d.x);
            minz = std::min(minz, d.z), maxz = std::max(maxz, d.z);
        }
        const double span = std::max({maxx - minx, maxz - minz, 500.0});
        const float margin = 24;
        const double scale = (std::min(size.x, size.y) - 2 * margin) / span;
        const double cx = (minx + maxx) / 2, cz = (minz + maxz) / 2;
        auto to_screen = [&](double x, double z) {
            return ImVec2(origin.x + size.x / 2 + static_cast<float>((x - cx) * scale), origin.y + size.y / 2 - static_cast<float>((z - cz) * scale));
        };
        int selected = -1;
        if (!sel_.empty())
            for (size_t i = 0; i < p.layers.size(); ++i)
                if (p.layers[i].uid == sel_) selected = static_cast<int>(i);
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const Dot* hover = nullptr;
        for (int pass = 0; pass < 2; ++pass)  // the selected layer on top
            for (auto& d : dots) {
                const bool is_sel = d.layer >= 0 && d.layer == selected;
                if ((pass == 1) != is_sel) continue;
                const ImVec2 sp = to_screen(d.x, d.z);
                const bool off = d.layer >= 0 && !p.layers[static_cast<size_t>(d.layer)].enabled;
                const unsigned col = d.layer >= 0 ? layer_color(static_cast<size_t>(d.layer)) : 0xff808080;
                const float r = is_sel ? 6.0f : 4.0f;
                if (off) dl->AddCircle(sp, r, to_im(col), 12, 2.0f);
                else dl->AddCircleFilled(sp, r, to_im(col, d.layer >= 0 && selected >= 0 && !is_sel ? 0.45f : 1.0f));
                if (std::fabs(mouse.x - sp.x) < 7 && std::fabs(mouse.y - sp.y) < 7) hover = &d;
            }
        if (hover && ImGui::IsWindowHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(hover->text.c_str());
            if (hover->layer >= 0) ImGui::Text(tr("layer %s (%s at start)"), p.layers[static_cast<size_t>(hover->layer)].name.c_str(), p.layers[static_cast<size_t>(hover->layer)].enabled ? "on" : "off");
            else ImGui::TextDisabled("%s", tr("in no layer"));
            ImGui::Text(tr("x %.0f  z %.0f"), hover->x, hover->z);
            ImGui::EndTooltip();
        }
        // legend: filled = on at the start, ring = off
        float y = origin.y + 6;
        for (size_t i = 0; i < p.layers.size(); ++i) {
            dl->AddCircleFilled(ImVec2(origin.x + 12, y + 7), 4, to_im(layer_color(i)));
            dl->AddText(ImVec2(origin.x + 22, y), IM_COL32(210, 210, 210, 255), (p.layers[i].name + (p.layers[i].enabled ? "" : "  (off)")).c_str());
            y += 16;
        }
        ImGui::Dummy(size);
    }

    std::string sel_;
};

}  // namespace

std::unique_ptr<Panel> make_layers_panel() { return std::make_unique<LayersPanel>(); }

}  // namespace mk
