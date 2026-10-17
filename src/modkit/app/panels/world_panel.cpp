// World panel (docs/editor/REQUIREMENTS.md WLD-1, WLD-2, WLD-9): a game world read offline, every entity as its bounding box in a 3D view with a fly
// camera, layers and kinds to show, search, picking, and the inspector of the selected entity (template, GUID, position, properties, inventory).
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <future>
#include <map>
#include <optional>
#include <set>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <ImGuizmo.h>

#include "app/dragdrop.h"
#include "app/editor_util.h"
#include "common/pose.h"
#include "app/panels/panels.h"
#include "app/pickers.h"
#include "app/render/gl_util.h"
#include "app/render/mesh_renderer.h"
#include "app/render/resources.h"
#include "core/catalog.h"
#include "app/widgets.h"
#include "core/log.h"

namespace mk {

namespace {

using gfx::to_gl;

uint32_t kind_color(TplKind k) {
    switch (k) {
        case TplKind::Item: return IM_COL32(240, 220, 80, 255);
        case TplKind::Recipe: return IM_COL32(240, 190, 80, 255);
        case TplKind::Npc: return IM_COL32(90, 230, 110, 255);
        case TplKind::Monster: return IM_COL32(240, 80, 70, 255);
        case TplKind::Interactive: return IM_COL32(250, 150, 60, 255);
        case TplKind::Object: return IM_COL32(150, 150, 160, 255);
        case TplKind::Effect: return IM_COL32(170, 110, 220, 255);
        case TplKind::Navigation: return IM_COL32(80, 200, 220, 255);
        case TplKind::Sound: return IM_COL32(90, 120, 240, 255);
        case TplKind::Other: return IM_COL32(100, 100, 100, 255);
    }
    return IM_COL32_WHITE;
}

bool icontains(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
                          [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
    return it != hay.end();
}

// ray against an axis-aligned box (slabs); returns the entry distance or -1
float ray_box(const glm::vec3& o, const glm::vec3& d, const glm::vec3& mn, const glm::vec3& mx) {
    float t0 = 0, t1 = 1e30f;
    for (int i = 0; i < 3; ++i) {
        const float inv = 1.0f / (std::fabs(d[i]) < 1e-12f ? (d[i] < 0 ? -1e-12f : 1e-12f) : d[i]);
        float a = (mn[i] - o[i]) * inv, b = (mx[i] - o[i]) * inv;
        if (a > b) std::swap(a, b);
        t0 = std::max(t0, a);
        t1 = std::min(t1, b);
        if (t0 > t1) return -1;
    }
    return t0;
}

bool is_level_mesh(const WorldEntity& e) {
    if (!e.has_box()) return false;
    for (int i = 0; i < 3; ++i)
        if (e.box_max[i] - e.box_min[i] > 15000) return true;
    return p2m::starts_with(e.name, "Levelmesh");
}

std::string val_text(const p2m::Val& v) {
    switch (v.k) {
        case p2m::Val::Int: return std::to_string(v.i);
        case p2m::Val::Bool: return v.i ? "true" : "false";
        case p2m::Val::Float: return p2m::fmt_g(v.d);
        case p2m::Val::Str: return v.s.size() > 80 ? v.s.substr(0, 80) + "..." : v.s;
        case p2m::Val::Vec: {
            std::string s;
            for (size_t i = 0; i < v.v.size(); ++i) s += (i ? ", " : "") + p2m::fmt_g(v.v[i]);
            return s;
        }
        default: return "-";
    }
}

// what can be placed in the world (a drop on the view, the place modes)
const std::vector<TplKind>& place_kinds() {
    static const std::vector<TplKind> k = {TplKind::Npc,    TplKind::Monster,     TplKind::Item,  TplKind::Recipe,
                                           TplKind::Object, TplKind::Interactive, TplKind::Effect};
    return k;
}

class WorldPanel : public Panel {
public:
    const char* title() const override { return kPanelWorld; }
    bool busy() const override { return loading_.valid() || (res_ && res_->pending() > 0); }

    void on_project_changed(App& app) override {
        overlay_dirty_ = true;
        if (!app.game) {
            app.world.reset();
            world_.reset();
        } else if (!world_ && !loading_.valid() && !tried_) {
            // in the background as soon as the game data is there: the world is ready when the tab is opened
            start_load(app, app.settings.world.empty() ? std::string("World") : app.settings.world);
        }
    }
    bool select(App& app, const std::string& key) override {
        if (!world_) return false;
        if (project_position(app, key)) {
            proj_sel_ = key;
            overlay_dirty_ = true;
            const Vec3& p = *project_position(app, key);
            cam_.look_at(to_gl(static_cast<float>(p[0]), static_cast<float>(p[1]) + 100, static_cast<float>(p[2])), 900);
            app.focus_panel(title());
            return true;
        }
        for (size_t i = 0; i < world_->entities.size(); ++i)
            if (world_->entities[i].name == key || world_->entities[i].guid == key) {
                pick(i);
                fly_to(world_->entities[i]);
                app.focus_panel(title());
                return true;
            }
        return false;
    }

    void draw(App& app) override {
        poll_loader(app);
        if (!ImGui::Begin(title(), &open)) {
            ImGui::End();
            return;
        }
        if (!app.game) {
            ImGui::TextWrapped(app.game_loading() ? "Reading the game data..." : "No game data. Choose the game's data directory in Settings.");
            ImGui::End();
            return;
        }
        // load the configured world the first time the panel is shown
        if (!world_ && !loading_.valid() && !tried_) start_load(app, app.settings.world.empty() ? std::string("World") : app.settings.world);
        draw_toolbar(app);
        if (!world_) {
            ImGui::TextDisabled(loading_.valid() ? "Loading the world..." : "No world loaded.");
            ImGui::End();
            return;
        }
        ImGui::BeginChild("##layers", ImVec2(260, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
        draw_layers();
        ImGui::EndChild();
        ImGui::SameLine();
        const float inspector_w = 340;
        ImGui::BeginChild("##view", ImVec2(ImGui::GetContentRegionAvail().x - inspector_w - 8, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
        draw_view(app);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##inspector", ImVec2(0, 0), ImGuiChildFlags_Borders);
        draw_inspector(app);
        ImGui::EndChild();
        ImGui::End();
    }

private:
    void start_load(App& app, const std::string& name) {
        tried_ = true;
        std::shared_ptr<const GameData> g = app.game;
        loading_name_ = name;
        loading_ = std::async(std::launch::async, [g, name]() -> std::shared_ptr<World> {
            try {
                return load_world(*g, name);
            } catch (const std::exception& e) {
                log_error(std::string("world: ") + e.what());
                return nullptr;
            }
        });
    }
    void poll_loader(App& app) {
        if (!loading_.valid() || loading_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        world_ = loading_.get();
        app.world = world_;
        if (!world_) return;
        res_ = app.resources();
        mesh_names_.assign(world_->entities.size(), std::string());
        for (size_t i = 0; i < world_->entities.size(); ++i)
            if (!world_->entities[i].mesh.empty()) mesh_names_[i] = compiled_name(world_->entities[i].mesh);
        layer_on_.assign(world_->layers.size(), 1);
        // the system, navigation and sound layers are hidden at first: they are mostly helpers
        for (size_t i = 0; i < world_->layers.size(); ++i) {
            const std::string n = p2m::lower(world_->layers[i].name);
            if (p2m::contains(n, "_nav") || p2m::contains(n, "sfx") || p2m::contains(n, "_vfx") || p2m::contains(n, "occluder")) layer_on_[i] = 0;
        }
        selected_ = SIZE_MAX;
        boxes_dirty_ = overlay_dirty_ = true;
        // start at the player's start point (SP_Start in _System/_StartPoints) if the world has one, else at the first NPC
        const WorldEntity* start = world_->by_name("SP_Start");
        if (!start)
            for (auto& e : world_->entities)
                if (p2m::contains(p2m::lower(world_->layers[e.layer].name), "startpoint")) {
                    start = &e;
                    break;
                }
        if (!start)
            for (auto& e : world_->entities)
                if (e.npc) {
                    start = &e;
                    break;
                }
        if (start) fly_to(*start);
        // --camera x,y,z,yaw,pitch (game coordinates, degrees; yaw 0 = +z)
        float c[5] = {0, 0, 0, 0, 0};
        if (!app.options.camera.empty() && std::sscanf(app.options.camera.c_str(), "%f,%f,%f,%f,%f", &c[0], &c[1], &c[2], &c[3], &c[4]) == 5) {
            cam_.pos = to_gl(c[0], c[1], c[2]);
            cam_.yaw = glm::radians(c[3]);
            cam_.pitch = glm::radians(c[4]);
        }
        app.status("World " + world_->name + ": " + std::to_string(world_->entities.size()) + " entities");
    }

    void draw_toolbar(App& app) {
        ImGui::SetNextItemWidth(180);
        if (ImGui::BeginCombo("##world", world_ ? world_->name.c_str() : loading_name_.c_str())) {
            for (auto& w : app.game->worlds())
                if (ImGui::Selectable(w.c_str(), world_ && world_->name == w) && !loading_.valid()) {
                    app.settings.world = w;
                    app.settings.save();
                    start_load(app, w);
                }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (loading_.valid()) {
            ImGui::TextDisabled("loading...");
        } else if (world_) {
            ImGui::TextDisabled("%zu entities, %zu layers", world_->entities.size(), world_->layers.size());
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(220);
        if (ImGui::InputTextWithHint("##search", "find an entity by name", &search_)) search_hits_.clear();
        if (world_ && !search_.empty()) {
            if (search_hits_.empty() && search_.size() >= 2)
                for (size_t i = 0; i < world_->entities.size() && search_hits_.size() < 200; ++i)
                    if (icontains(world_->entities[i].name, search_) || icontains(world_->entities[i].template_name, search_)) search_hits_.push_back(i);
            ImGui::SameLine();
            if (ImGui::BeginCombo("##hits", (std::to_string(search_hits_.size()) + (search_hits_.size() >= 200 ? "+" : "") + " found").c_str(),
                                  ImGuiComboFlags_HeightLarge)) {
                for (size_t i : search_hits_) {
                    const WorldEntity& e = world_->entities[i];
                    if (ImGui::Selectable((e.name + "##" + std::to_string(i)).c_str(), i == selected_)) {
                        pick(i);
                        fly_to(e);
                    }
                    ImGui::SameLine(260);
                    ImGui::TextDisabled("%s", world_->layers[e.layer].name.c_str());
                }
                ImGui::EndCombo();
            }
        }
        // placing: what a click in the view does
        ImGui::SameLine(0, 24);
        ImGui::RadioButton("Select", &mode_, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Place NPC", &mode_, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Place item / object", &mode_, 2);
        if (mode_ == 1) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200);
            const PlacedNpc* cur = nullptr;
            for (auto& n : app.project.npcs)
                if (n.uid == place_npc_) cur = &n;
            if (ImGui::BeginCombo("##which", cur ? cur->name.c_str() : "a new NPC of the template")) {
                if (ImGui::Selectable("a new NPC of the template", !cur)) place_npc_.clear();
                for (auto& n : app.project.npcs)
                    if (ImGui::Selectable(n.name.c_str(), n.uid == place_npc_)) place_npc_ = n.uid;
                ImGui::EndCombo();
            }
            if (!cur) {
                ImGui::SameLine();
                std::vector<std::string> mine;
                for (auto& t : app.project.npc_templates)
                    if (t.mode == Mode::New) mine.push_back(t.name);
                ui::template_picker("##npct", place_template_, app.game.get(), {TplKind::Npc, TplKind::Monster}, mine, 200);
            }
        } else if (mode_ == 2) {
            ImGui::SameLine();
            ui::template_picker("##itemt", place_item_, app.game.get(), {TplKind::Item, TplKind::Recipe, TplKind::Object, TplKind::Interactive, TplKind::Effect},
                                ui::project_item_names(app.project), 200);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90);
            ImGui::InputInt("amount", &place_amount_);
            place_amount_ = std::max(1, place_amount_);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70);
            ImGui::DragFloat("yaw", &place_yaw_, 1.0f, 0, 360, "%.0f");
        }
        ImGui::SameLine(0, 24);
        ImGui::TextDisabled("gizmo:");
        ImGui::SameLine();
        ImGui::RadioButton("move", &gizmo_op_, 0);
        ImGui::SameLine();
        ImGui::RadioButton("turn", &gizmo_op_, 1);
        ImGui::TextDisabled("right mouse: look, WASD/QE: move, wheel: speed, click: %s, F: focus, End: align with mesh, gizmo: move / turn the selected project object",
                            mode_ == 0 ? "select" : "place on the ground");
    }

    void draw_layers() {
        ImGui::SeparatorText("Show");
        bool ch = false;
        for (int k = 0; k < kTplKindCount; ++k) {
            ImGui::PushID(k);
            const ImVec4 c = ImGui::ColorConvertU32ToFloat4(kind_color(static_cast<TplKind>(k)));
            ImGui::PushStyleColor(ImGuiCol_CheckMark, c);
            ch |= ImGui::Checkbox(kind_name(static_cast<TplKind>(k)), &kinds_[k]);
            ImGui::PopStyleColor();
            if (k % 2 == 0) ImGui::SameLine(130);
            ImGui::PopID();
        }
        ch |= ImGui::Checkbox("Level meshes (huge boxes)", &show_level_);
        ImGui::SeparatorText("Draw");
        ch |= ImGui::Checkbox("Meshes and textures", &draw_meshes_);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderFloat("##dist", &draw_distance_, 2000, 40000, "mesh distance %.0f");
        ch |= ImGui::Checkbox("Boxes of entities with a mesh", &boxes_for_meshes_);
        ch |= ImGui::Checkbox("Boxes of objects without a mesh", &boxes_plain_objects_);
        ui::help("Mostly the vegetation (SpeedTree), lights and helpers: the editor has no meshes for them.");
        if (res_) ImGui::TextDisabled("%zu meshes loaded, %zu waiting%s", res_->loaded_meshes(), res_->pending(), res_->s3tc() ? "" : ", CPU textures");
        ImGui::SeparatorText("Layers");
        if (ImGui::SmallButton("all")) {
            std::fill(layer_on_.begin(), layer_on_.end(), 1);
            ch = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("none")) {
            std::fill(layer_on_.begin(), layer_on_.end(), 0);
            ch = true;
        }
        std::map<std::string, std::vector<size_t>> groups;
        for (size_t i = 0; i < world_->layers.size(); ++i) groups[world_->layers[i].group].push_back(i);
        for (auto& [g, list] : groups) {
            int on = 0;
            for (size_t i : list) on += layer_on_[i];
            ImGui::PushID(g.c_str());
            bool all = on == static_cast<int>(list.size());
            ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, on > 0 && !all);
            const bool clicked = ImGui::Checkbox("##grp", &all);
            ImGui::PopItemFlag();
            if (clicked) {
                for (size_t i : list) layer_on_[i] = all;
                ch = true;
            }
            ImGui::SameLine();
            if (ImGui::TreeNode(g.empty() ? "(root)" : g.c_str())) {
                for (size_t i : list) {
                    bool b = layer_on_[i];
                    if (ImGui::Checkbox((world_->layers[i].name + "##" + std::to_string(i)).c_str(), &b)) {
                        layer_on_[i] = b;
                        ch = true;
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("%u", world_->layers[i].count);
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (ch) boxes_dirty_ = true;
    }

    bool visible(const WorldEntity& e) const {
        if (!layer_on_[e.layer] || !kinds_[static_cast<int>(e.kind)]) return false;
        if (!show_level_ && is_level_mesh(e)) return false;
        return true;
    }

    void rebuild_boxes() {
        boxes_.clear();
        box_drawn_.assign(world_->entities.size(), 0);
        for (size_t i = 0; i < world_->entities.size(); ++i) {
            const WorldEntity& e = world_->entities[i];
            if (!visible(e)) continue;
            // with meshes on, a box only for what has no mesh (or a mesh the editor cannot read)
            if (draw_meshes_ && !boxes_for_meshes_ && !mesh_names_[i].empty() && !(res_ && res_->failed(mesh_names_[i]))) continue;
            if (draw_meshes_ && !boxes_plain_objects_ && e.kind == TplKind::Object && mesh_names_[i].empty()) continue;
            box_drawn_[i] = 1;  // what the user sees can be clicked (pick_at)
            const int edit = i < edit_state_.size() ? edit_state_[i] : 0;
            if (edit == 1) {  // removed by the mod: a red box where it was
                if (e.has_box()) boxes_.box(to_gl(e.box_min[0], e.box_min[1], e.box_max[2]), to_gl(e.box_max[0], e.box_max[1], e.box_min[2]), IM_COL32(255, 50, 50, 255));
                else boxes_.cross(to_gl(e.x(), e.y(), e.z()), 40, IM_COL32(255, 50, 50, 255));
                continue;
            }
            if (edit == 2 && draw_meshes_ && !mesh_names_[i].empty()) continue;  // drawn as a mesh at its new place
            const uint32_t c = kind_color(e.kind);
            if (edit == 2) {
                const auto& m = moved_matrix_.at(i);
                const glm::vec3 d(m[12] - e.x(), m[13] - e.y(), m[14] - e.z());
                if (e.has_box())
                    boxes_.box(to_gl(e.box_min[0] + d.x, e.box_min[1] + d.y, e.box_max[2] + d.z), to_gl(e.box_max[0] + d.x, e.box_max[1] + d.y, e.box_min[2] + d.z),
                               IM_COL32(255, 160, 40, 255));
                else boxes_.cross(to_gl(m[12], m[13], m[14]), 25, IM_COL32(255, 160, 40, 255));
                continue;
            }
            if (e.has_box()) {
                const glm::vec3 a = to_gl(e.box_min[0], e.box_min[1], e.box_max[2]);
                const glm::vec3 b = to_gl(e.box_max[0], e.box_max[1], e.box_min[2]);
                boxes_.box(a, b, c);
            } else {
                boxes_.cross(to_gl(e.x(), e.y(), e.z()), 25, c);
            }
        }
        boxes_.upload();
        boxes_dirty_ = false;
    }

    void rebuild_overlay(App& app) {
        overlay_.clear();
        if (selected_ < world_->entities.size()) {
            const WorldEntity& e = world_->entities[selected_];
            const glm::vec3 p = to_gl(e.x(), e.y(), e.z());
            if (e.has_box()) {
                const glm::vec3 a = to_gl(e.box_min[0], e.box_min[1], e.box_max[2]) - glm::vec3(4), b = to_gl(e.box_max[0], e.box_max[1], e.box_min[2]) + glm::vec3(4);
                overlay_.box(a, b, IM_COL32_WHITE);
            }
            overlay_.cross(p, 60, IM_COL32_WHITE);
            // the facing
            const float yaw = glm::radians(e.yaw());
            overlay_.line(p + glm::vec3(0, 20, 0), p + glm::vec3(std::sin(yaw) * 150, 20, -std::cos(yaw) * 150), IM_COL32(255, 255, 255, 255));
        }
        // the project's placed NPCs with a world position (magenta) and the things it spawns (yellow); the selected one bright
        for (auto& n : app.project.npcs) {
            if (n.offset) continue;
            const glm::vec3 p = to_gl(static_cast<float>(n.position[0]), static_cast<float>(n.position[1]), static_cast<float>(n.position[2]));
            const bool sel = proj_sel_ == n.uid;
            overlay_.box(p + glm::vec3(-40, 0, -40), p + glm::vec3(40, 200, 40), sel ? IM_COL32(255, 200, 255, 255) : IM_COL32(255, 80, 255, 255));
        }
        for (auto& s : app.project.world.spawns) {
            const glm::vec3 p = to_gl(static_cast<float>(s.position[0]), static_cast<float>(s.position[1]), static_cast<float>(s.position[2]));
            const bool sel = proj_sel_ == s.uid;
            overlay_.box(p + glm::vec3(-25, 0, -25), p + glm::vec3(25, 50, 25), sel ? IM_COL32(255, 255, 200, 255) : IM_COL32(250, 220, 60, 255));
            const float yaw = glm::radians(static_cast<float>(s.rotation[0]));
            overlay_.line(p + glm::vec3(0, 25, 0), p + glm::vec3(std::sin(yaw) * 80, 25, -std::cos(yaw) * 80), IM_COL32(250, 220, 60, 255));
        }
        for (auto& [i, m] : moved_matrix_) {
            const WorldEntity& e = world_->entities[i];
            const glm::vec3 from = to_gl(e.x(), e.y(), e.z()), to = to_gl(m[12], m[13], m[14]);
            overlay_.line(from, to, IM_COL32(255, 160, 40, 255));
            overlay_.cross(to, 40, IM_COL32(255, 160, 40, 255));
        }
        overlay_.upload();
        overlay_dirty_ = false;
    }

    void pick(size_t i) {
        selected_ = i;
        overlay_dirty_ = true;
        props_.clear();
        inv_.clear();
        props_for_ = SIZE_MAX;
    }

    void fly_to(const WorldEntity& e) {
        float size = 300;
        if (e.has_box()) size = std::max({e.box_max[0] - e.box_min[0], e.box_max[1] - e.box_min[1], e.box_max[2] - e.box_min[2], 200.0f});
        cam_.look_at(to_gl(e.x(), e.y() + 100, e.z()), std::clamp(size * 2.5f, 900.0f, 20000.0f));
    }

    void draw_view(App& app) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const int w = std::max(16, static_cast<int>(avail.x)), h = std::max(16, static_cast<int>(avail.y));
        sync_edits(app);
        if (boxes_dirty_) rebuild_boxes();
        if (overlay_dirty_) rebuild_overlay(app);
        // input first, so the frame shows this frame's camera
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        // over the gizmo the view is no button: ImGuizmo only takes a click when no ImGui item is hovered (or was in the last frame)
        const bool on_gizmo = gizmo_over_ && !view_active_ && !ImGui::GetDragDropPayload() && !ImGui::IsMouseDown(ImGuiMouseButton_Right);
        if (on_gizmo) ImGui::Dummy(ImVec2(static_cast<float>(w), static_cast<float>(h)));
        else
            ImGui::InvisibleButton("##viewport", ImVec2(static_cast<float>(w), static_cast<float>(h)),
                                   ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const bool hovered = ImGui::IsItemHovered();
        view_active_ = !on_gizmo && ImGui::IsItemActive();
        ImGuiIO& io = ImGui::GetIO();
        const float aspect = static_cast<float>(w) / static_cast<float>(h);
        if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            cam_.yaw += io.MouseDelta.x * 0.004f;
            cam_.pitch = std::clamp(cam_.pitch - io.MouseDelta.y * 0.004f, -1.5f, 1.5f);
            glm::vec3 move(0);
            if (ImGui::IsKeyDown(ImGuiKey_W)) move += cam_.forward();
            if (ImGui::IsKeyDown(ImGuiKey_S)) move -= cam_.forward();
            if (ImGui::IsKeyDown(ImGuiKey_D)) move += cam_.right();
            if (ImGui::IsKeyDown(ImGuiKey_A)) move -= cam_.right();
            if (ImGui::IsKeyDown(ImGuiKey_E)) move.y += 1;
            if (ImGui::IsKeyDown(ImGuiKey_Q)) move.y -= 1;
            const float fast = io.KeyShift ? 4.0f : 1.0f;
            if (glm::length(move) > 0) cam_.pos += glm::normalize(move) * cam_.speed * fast * io.DeltaTime;
            if (io.MouseWheel != 0) cam_.speed = std::clamp(cam_.speed * (io.MouseWheel > 0 ? 1.25f : 0.8f), 100.0f, 100000.0f);
        } else if (hovered && io.MouseWheel != 0) {
            cam_.pos += cam_.forward() * io.MouseWheel * cam_.speed * 0.15f;
        }
        // a template or placed NPC dropped from the Browser / the NPCs panel: placed where it was dropped (after the frame is drawn, like a click)
        std::vector<std::string> placed;
        for (auto& n : app.project.npcs) placed.push_back(n.name);
        std::optional<ui::DragTemplate> dropped;
        if (const ui::DragTemplate* d = ui::drop_target(place_kinds(), placed)) dropped = *d;
        if (hovered && ui::dragging()) drop_preview(*ui::dragging());
        if (hovered && ImGui::IsKeyPressed(ImGuiKey_F) && selected_ < world_->entities.size()) fly_to(world_->entities[selected_]);
        if (hovered && ImGui::IsKeyPressed(ImGuiKey_End) && !proj_sel_.empty()) settle_req_ = proj_sel_;
        // align with mesh: asked for by the button / End, or after a placement (once the object's mesh is there, two seconds at most)
        if (!settle_req_.empty()) {
            settle(app, settle_req_, false);
            settle_req_.clear();
        }
        if (!settle_auto_.empty() && (settle_ready(app, settle_auto_) || ++settle_wait_ > 120)) {
            settle(app, settle_auto_, true);
            settle_auto_.clear();
        }
        bool clicked = false, pick_wanted = false;
        float cu = 0, cv = 0;
        if (dropped) {
            cu = (io.MousePos.x - origin.x) / static_cast<float>(w);
            cv = (io.MousePos.y - origin.y) / static_cast<float>(h);
        }
        if (ImGui::IsItemDeactivated() && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !gizmo_over_ &&
            glm::length(glm::vec2(ImGui::GetMouseDragDelta(0).x, ImGui::GetMouseDragDelta(0).y)) < 4) {
            cu = (io.MousePos.x - origin.x) / static_cast<float>(w);
            cv = (io.MousePos.y - origin.y) / static_cast<float>(h);
            if (mode_ == 0 && !pick_project(app, cu, cv, aspect)) pick_wanted = true;
            else if (mode_ != 0) clicked = true;  // placed after the frame is drawn: the depth buffer tells where the ground is
        }

        rt_.resize(w, h);
        if (pick_wanted) pick_at(cu, cv, aspect, w, h, cam_.proj(aspect) * cam_.view());
        rt_.bind();
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glClearColor(0.10f, 0.11f, 0.13f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        const glm::mat4 vp = cam_.proj(aspect) * cam_.view();
        if (draw_meshes_ && res_) draw_meshes(vp);
        if (draw_meshes_ && res_) draw_project_meshes(app, vp);
        boxes_.draw(vp);
        if (clicked) place_at(app, cu, cv, w, h, vp);
        if (dropped) drop_at(app, *dropped, cu, cv, w, h, vp);
        glDisable(GL_DEPTH_TEST);
        overlay_.draw(vp);
        rt_.unbind();
        ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(rt_.texture())), origin, ImVec2(origin.x + w, origin.y + h),
                                             ImVec2(0, 1), ImVec2(1, 0));
        draw_gizmo(app, origin, w, h, aspect);
        // camera position in game coordinates
        const glm::vec3 gp = gfx::from_gl(cam_.pos);
        char buf[96];
        std::snprintf(buf, sizeof buf, "camera %.0f %.0f %.0f   speed %.0f   %zu meshes drawn", gp.x, gp.y, gp.z, cam_.speed, drawn_);
        ImGui::GetWindowDrawList()->AddText(ImVec2(origin.x + 8, origin.y + h - 22), IM_COL32(200, 200, 200, 255), buf);
    }

    // `ids`: the picking pass (pick_at): the same meshes, each in the colour of its entity number; level meshes in black (they hide what is behind
    // them, but are only clickable when their boxes are shown)
    void draw_meshes(const glm::mat4& vp, bool ids = false, size_t skip = SIZE_MAX) {
        if (!ids) res_->update(6.0);
        const gfx::Frustum fr(vp);
        const glm::vec3 cam_game = gfx::from_gl(cam_.pos);
        const float d2 = draw_distance_ * draw_distance_;
        mesh_r_.begin(vp, glm::vec3(0.35f, -1.0f, 0.25f), ids);
        if (!ids) {
            drawn_ = 0;
            mesh_drawn_.assign(world_->entities.size(), 0);
        }
        size_t failed_before = failed_seen_;
        for (size_t i = 0; i < world_->entities.size(); ++i) {
            if (mesh_names_[i].empty() || i == skip) continue;
            const WorldEntity& e = world_->entities[i];
            if (!layer_on_[e.layer] || !kinds_[static_cast<int>(e.kind)]) continue;
            const int edit = i < edit_state_.size() ? edit_state_[i] : 0;
            if (edit == 1) continue;  // removed by the mod
            const bool level = is_level_mesh(e);
            if (!level) {
                const float dx = e.x() - cam_game.x, dy = e.y() - cam_game.y, dz = e.z() - cam_game.z;
                if (dx * dx + dy * dy + dz * dz > d2) continue;
            }
            if (edit == 0 && e.has_box() && !fr.sees(to_gl(e.box_min[0], e.box_min[1], e.box_max[2]), to_gl(e.box_max[0], e.box_max[1], e.box_min[2]))) continue;
            const gfx::GpuMesh* m = res_->mesh(mesh_names_[i]);
            if (!m) {
                if (!ids && res_->failed(mesh_names_[i])) ++failed_seen_;
                continue;
            }
            if (ids) {
                const glm::vec4 c = (!level || show_level_) ? gfx::MeshRenderer::id_color(static_cast<uint32_t>(i + 1)) : glm::vec4(0);
                mesh_r_.draw(*m, gfx::entity_model(edit == 2 ? moved_matrix_.at(i).data() : e.matrix.data()), *res_, c);
                continue;
            }
            glm::vec4 tint = i == selected_ ? glm::vec4(1.4f, 1.3f, 0.9f, 1) : glm::vec4(1);
            if (edit == 2) tint *= glm::vec4(1.3f, 1.05f, 0.75f, 1);
            mesh_r_.draw(*m, gfx::entity_model(edit == 2 ? moved_matrix_.at(i).data() : e.matrix.data()), *res_, tint);
            ++drawn_;
            // level meshes are always drawn, but only clickable when their boxes are shown (they would swallow every click)
            if (!level || show_level_) mesh_drawn_[i] = 1;
        }
        mesh_r_.end();
        if (ids) return;
        // meshes that turned out unreadable get their box back
        if (failed_seen_ != failed_before) boxes_dirty_ = true;
        failed_seen_ = 0;
    }

    // the project object (placed NPC, spawn) the ray hits, selected; false when none
    bool pick_project(App& app, float u, float v, float aspect) {
        glm::vec3 o, d;
        cam_.ray(u, v, aspect, o, d);
        std::string best;
        float best_t = 1e30f;
        auto test = [&](const std::string& uid, const Vec3& pos, float r, float hgt) {
            const glm::vec3 p = to_gl(static_cast<float>(pos[0]), static_cast<float>(pos[1]), static_cast<float>(pos[2]));
            const float t = ray_box(o, d, p - glm::vec3(r, 0, r), p + glm::vec3(r, hgt, r));
            if (t > 0 && t < best_t) {
                best_t = t;
                best = uid;
            }
        };
        for (auto& n : app.project.npcs)
            if (!n.offset) test(n.uid, n.position, 40, 200);
        for (auto& s : app.project.world.spawns) test(s.uid, s.position, 25, 50);
        for (auto& m : app.project.world.moves)
            if (m.position) test(m.uid, *m.position, 40, 120);
        if (best.empty()) return false;
        proj_sel_ = best;
        selected_ = SIZE_MAX;
        overlay_dirty_ = true;
        return true;
    }

    // the ground under the cursor from the depth buffer (bound render target), in game coordinates
    bool ground_at(float u, float v, int w, int h, const glm::mat4& vp, glm::vec3& out) {
        const int px = std::clamp(static_cast<int>(u * w), 0, w - 1), py = std::clamp(static_cast<int>((1.0f - v) * h), 0, h - 1);
        float depth = 1;
        glReadPixels(px, py, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
        if (depth >= 1.0f) return false;
        const glm::vec4 ndc(u * 2 - 1, (1 - v) * 2 - 1, depth * 2 - 1, 1);
        glm::vec4 p = glm::inverse(vp) * ndc;
        if (std::fabs(p.w) < 1e-9f) return false;
        out = gfx::from_gl(glm::vec3(p) / p.w);
        return true;
    }

    // the ground under the cursor rounded to a tenth, or nullopt (with a status line) when there is none
    std::optional<Vec3> ground_pos(App& app, float u, float v, int w, int h, const glm::mat4& vp) {
        glm::vec3 g;
        if (!ground_at(u, v, w, h, vp, g)) {
            app.status("Nothing under the cursor to place on (the sky?)");
            return std::nullopt;
        }
        return Vec3{std::round(g.x * 10) / 10, std::round(g.y * 10) / 10, std::round(g.z * 10) / 10};
    }

    void place_at(App& app, float u, float v, int w, int h, const glm::mat4& vp) {
        if (mode_ == 1 && !place_npc_.empty()) {
            if (auto pos = ground_pos(app, u, v, w, h, vp)) move_npc(app, place_npc_, *pos);
        } else if (mode_ == 1) {
            if (place_template_.empty()) return app.status("Choose the template of the new NPC first");
            if (auto pos = ground_pos(app, u, v, w, h, vp)) place_npc_ = new_npc(app, place_template_, *pos);
        } else {
            if (place_item_.empty()) return app.status("Choose the item to place first");
            if (auto pos = ground_pos(app, u, v, w, h, vp)) new_spawn(app, place_item_, *pos, place_amount_);
        }
    }

    // a drop on the view: an NPC or monster template becomes a new placed NPC, a placed NPC moves there, anything else is spawned
    void drop_at(App& app, const ui::DragTemplate& d, float u, float v, int w, int h, const glm::mat4& vp) {
        const auto pos = ground_pos(app, u, v, w, h, vp);
        if (!pos) return;
        if (d.entity) {
            for (auto& n : app.project.npcs)
                if (n.name == d.name) return move_npc(app, n.uid, *pos);
        } else if (d.kind == TplKind::Npc || d.kind == TplKind::Monster) {
            new_npc(app, d.name, *pos);
        } else {
            new_spawn(app, d.name, *pos, 1);
        }
    }

    // what a drop would do, as a tooltip while a payload is over the view
    static void drop_preview(const ui::DragTemplate& d) {
        const auto& k = place_kinds();
        const std::string name = d.name;
        if (d.entity) ImGui::SetTooltip("Move %s here", name.c_str());
        else if (d.kind == TplKind::Npc || d.kind == TplKind::Monster) ImGui::SetTooltip("A new NPC of %s here", name.c_str());
        else if (std::find(k.begin(), k.end(), d.kind) != k.end()) ImGui::SetTooltip("Spawn %s here", name.c_str());
        else ImGui::SetTooltip("%s cannot be placed in the world", name.c_str());
    }

    void after_place(App& app, const std::string& uid, const std::string& what) {
        proj_sel_ = uid;
        selected_ = SIZE_MAX;
        overlay_dirty_ = true;
        app.status(what);
        app.touch();
    }
    void move_npc(App& app, const std::string& uid, const Vec3& pos) {
        for (auto& n : app.project.npcs)
            if (n.uid == uid) {
                n.position = pos;
                n.offset = false;
                return after_place(app, n.uid, "Placed " + n.name);
            }
    }
    // a new placed NPC of `tpl` at `pos`; returns its uid
    std::string new_npc(App& app, const std::string& tpl, const Vec3& pos) {
        PlacedNpc nn;
        nn.uid = new_uid();
        nn.template_name = tpl;
        nn.name = ui::unique_template_name(app.project, app.game.get(), ui::name_prefix(app.project) + "_" + tpl);
        nn.position = pos;
        app.project.npcs.push_back(nn);
        after_place(app, nn.uid, "Placed " + nn.name + " (a new NPC: name, inventory and routine in the NPCs panel)");
        return nn.uid;
    }
    void new_spawn(App& app, const std::string& tpl, const Vec3& pos, int amount) {
        WorldSpawn sp;
        sp.uid = new_uid();
        sp.template_name = tpl;
        sp.position = pos;
        sp.rotation = {std::round(place_yaw_), 0, 0};
        sp.amount = amount;
        app.project.world.spawns.push_back(sp);
        after_place(app, sp.uid, "Placed " + tpl);
        settle_auto_ = sp.uid;  // onto the surface with its lowest point once its mesh is loaded
        settle_wait_ = 0;
    }

    Vec3* project_position(App& app, const std::string& uid) {
        for (auto& n : app.project.npcs)
            if (n.uid == uid && !n.offset) return &n.position;
        for (auto& s : app.project.world.spawns)
            if (s.uid == uid) return &s.position;
        for (auto& m : app.project.world.moves)
            if (m.uid == uid && m.position) return &*m.position;
        return nullptr;
    }
    Vec3* project_rotation(App& app, const std::string& uid) {
        for (auto& s : app.project.world.spawns)
            if (s.uid == uid) return &s.rotation;
        for (auto& m : app.project.world.moves)
            if (m.uid == uid && m.rotation) return &*m.rotation;
        return nullptr;
    }

    // which world entities the project removes (1) or moves (2), and the moved ones' new matrices; rebuilt when the edits change
    void sync_edits(App& app) {
        std::string sig;
        char buf[160];
        for (auto& r : app.project.world.removals) sig += "r" + r.guid;
        for (auto& m : app.project.world.moves) {
            const Vec3 p = m.position.value_or(Vec3{0, 0, 0}), r = m.rotation.value_or(Vec3{0, 0, 0});
            std::snprintf(buf, sizeof buf, "m%s%d%d%.2f,%.2f,%.2f,%.2f,%.2f,%.2f", m.guid.c_str(), m.position ? 1 : 0, m.rotation ? 1 : 0, p[0], p[1], p[2], r[0], r[1], r[2]);
            sig += buf;
        }
        if (sig == edits_sig_ && edit_state_.size() == world_->entities.size()) return;
        edits_sig_ = sig;
        edit_state_.assign(world_->entities.size(), 0);
        moved_matrix_.clear();
        auto index_of = [&](const std::string& guid) -> size_t {
            const WorldEntity* e = world_->by_guid(guid);
            return e ? static_cast<size_t>(e - world_->entities.data()) : SIZE_MAX;
        };
        for (auto& r : app.project.world.removals)
            if (size_t i = index_of(r.guid); i != SIZE_MAX) edit_state_[i] = 1;
        for (auto& m : app.project.world.moves) {
            const size_t i = index_of(m.guid);
            if (i == SIZE_MAX || edit_state_[i] == 1) continue;
            const WorldEntity& e = world_->entities[i];
            spass::pose::Matrix orig;
            std::copy(e.matrix.begin(), e.matrix.end(), orig.begin());
            spass::pose::Angles a = spass::pose::decompose(orig);
            if (m.rotation) a = {static_cast<float>((*m.rotation)[0]), static_cast<float>((*m.rotation)[1]), static_cast<float>((*m.rotation)[2])};
            const Vec3 p = m.position.value_or(Vec3{e.x(), e.y(), e.z()});
            moved_matrix_[i] = m.rotation ? spass::pose::compose(static_cast<float>(p[0]), static_cast<float>(p[1]), static_cast<float>(p[2]), a, spass::pose::scale_of(orig))
                                          : orig;
            moved_matrix_[i][12] = static_cast<float>(p[0]);
            moved_matrix_[i][13] = static_cast<float>(p[1]);
            moved_matrix_[i][14] = static_cast<float>(p[2]);
            edit_state_[i] = 2;
        }
        boxes_dirty_ = overlay_dirty_ = true;
    }

    void draw_gizmo(App& app, const ImVec2& origin, int w, int h, float aspect) {
        gizmo_over_ = false;
        Vec3* pos = proj_sel_.empty() ? nullptr : project_position(app, proj_sel_);
        if (!pos) return;
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
        ImGuizmo::SetRect(origin.x, origin.y, static_cast<float>(w), static_cast<float>(h));
        const glm::mat4 view = cam_.view(), proj = cam_.proj(aspect);
        Vec3* rot = project_rotation(app, proj_sel_);
        const bool turn = gizmo_op_ == 1 && rot;
        // a game yaw y (left-handed, forward = (sin y, 0, cos y)) is a turn by -y about +y in the view's mirrored space
        if (!gizmo_active_)
            gizmo_ = glm::translate(glm::mat4(1), to_gl(static_cast<float>((*pos)[0]), static_cast<float>((*pos)[1]), static_cast<float>((*pos)[2]))) *
                     glm::rotate(glm::mat4(1), glm::radians(rot ? -static_cast<float>((*rot)[0]) : 0.0f), glm::vec3(0, 1, 0));
        ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), turn ? ImGuizmo::ROTATE_Y : ImGuizmo::TRANSLATE, turn ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
                             glm::value_ptr(gizmo_));
        gizmo_over_ = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
        const bool using_now = ImGuizmo::IsUsing();
        // while dragging, the gizmo holds ImGui's active id: no window moves with the mouse, no other item reacts
        const ImGuiID gid = ImGui::GetID("##gizmo");
        if (using_now) ImGui::SetActiveID(gid, ImGui::GetCurrentWindow());
        else if (ImGui::GetActiveID() == gid) ImGui::ClearActiveID();
        if (using_now) {
            gizmo_active_ = true;
            overlay_dirty_ = true;
            if (turn) {
                const glm::vec4 f = gizmo_ * glm::vec4(0, 0, -1, 0);  // the game's +z
                float yaw = glm::degrees(std::atan2(f.x, -f.z));
                if (yaw < 0) yaw += 360;
                (*rot)[0] = std::round(yaw * 10) / 10;
            } else {
                const glm::vec3 g = gfx::from_gl(glm::vec3(gizmo_[3]));
                *pos = {std::round(g.x * 10) / 10, std::round(g.y * 10) / 10, std::round(g.z * 10) / 10};
            }
        } else if (gizmo_active_) {
            gizmo_active_ = false;  // one drag = one undo step: the model was written while dragging, the step is taken now
            app.touch();
        }
    }

    // the compiled mesh of a spawn's template ("" = none)
    static std::string spawn_mesh(const App& app, const WorldSpawn& s) {
        const TemplateInfo* t = app.game ? app.game->tpl(s.template_name) : nullptr;
        std::string mesh = t ? t->mesh : std::string();
        for (auto& it : app.project.items)
            if (it.name == s.template_name && it.mode == Mode::New) {
                if (it.mesh) mesh = *it.mesh;
                else if (const TemplateInfo* b = app.game ? app.game->tpl(it.base) : nullptr) mesh = b->mesh;
            }
        return mesh.empty() ? std::string() : compiled_name(p2m::bare_name(mesh));
    }
    static spass::pose::Matrix spawn_matrix(const WorldSpawn& s) {
        return spass::pose::compose(static_cast<float>(s.position[0]), static_cast<float>(s.position[1]), static_cast<float>(s.position[2]),
                                    {static_cast<float>(s.rotation[0]), static_cast<float>(s.rotation[1]), static_cast<float>(s.rotation[2])});
    }

    // the meshes of the things the project spawns, at their pose (`skip`: a spawn left out, the one being settled)
    void draw_project_meshes(App& app, const glm::mat4& vp, const std::string& skip = {}) {
        mesh_r_.begin(vp, glm::vec3(0.35f, -1.0f, 0.25f));
        for (auto& s : app.project.world.spawns) {
            if (s.uid == skip) continue;
            const std::string mesh = spawn_mesh(app, s);
            if (mesh.empty()) continue;
            const gfx::GpuMesh* m = res_->mesh(mesh);
            if (!m) continue;
            const spass::pose::Matrix pm = spawn_matrix(s);
            const glm::mat4 model = gfx::entity_model(pm.data());
            mesh_r_.draw(*m, model, *res_, proj_sel_ == s.uid ? glm::vec4(1.4f, 1.3f, 0.9f, 1) : glm::vec4(1));
        }
        mesh_r_.end();
    }

    // ---- align with mesh: put a project object (spawn, placed NPC, moved world entity) onto the surface below it. A small orthographic depth
    // pass looks straight down from just above the object over its footprint and draws everything but the object; the highest surface it
    // finds is where the object's lowest point goes.

    // the object's mesh is loaded (or it has none): the settle can use its bounds
    bool settle_ready(App& app, const std::string& uid) {
        if (!res_) return true;
        for (auto& s : app.project.world.spawns)
            if (s.uid == uid) {
                const std::string m = spawn_mesh(app, s);
                return m.empty() || res_->mesh(m) || res_->failed(m);
            }
        return true;
    }

    bool settle(App& app, const std::string& uid, bool quiet) {
        Vec3* pos = project_position(app, uid);
        if (!pos || !res_ || !world_) {
            if (!quiet) app.status("Select a placed NPC, a spawned object or a moved entity of the project to align it");
            return false;
        }
        const glm::vec3 p(static_cast<float>((*pos)[0]), static_cast<float>((*pos)[1]), static_cast<float>((*pos)[2]));
        // the object's bounds in game coordinates: a mesh's box at its pose, a world entity's box, or a point (NPCs stand on their position)
        glm::vec3 mn = p - glm::vec3(20, 0, 20), mx = p + glm::vec3(20, 0, 20);
        const gfx::GpuMesh* mesh = nullptr;
        spass::pose::Matrix pm{};
        size_t skip_entity = SIZE_MAX;
        std::string skip_spawn, what = "it";
        bool is_npc = false;
        for (auto& sp : app.project.world.spawns)
            if (sp.uid == uid) {
                const std::string m = spawn_mesh(app, sp);
                mesh = m.empty() ? nullptr : res_->mesh(m);
                pm = spawn_matrix(sp);
                skip_spawn = uid;
                what = sp.template_name;
            }
        for (auto& n : app.project.npcs)
            if (n.uid == uid) {
                is_npc = true;
                what = n.name;
                mx.y = p.y + 100;  // starts above his knees: a slightly sunken NPC comes up
            }
        for (auto& mv : app.project.world.moves)
            if (mv.uid == uid) {
                const WorldEntity* e = world_->by_guid(mv.guid);
                if (!e) break;
                skip_entity = static_cast<size_t>(e - world_->entities.data());
                what = mv.entity_name.empty() ? mv.guid : mv.entity_name;
                auto it = moved_matrix_.find(skip_entity);
                std::copy(e->matrix.begin(), e->matrix.end(), pm.begin());
                if (it != moved_matrix_.end()) pm = it->second;
                mesh = mesh_names_[skip_entity].empty() ? nullptr : res_->mesh(mesh_names_[skip_entity]);
                if (!mesh && e->has_box()) {
                    const glm::vec3 d = p - glm::vec3(e->x(), e->y(), e->z());
                    mn = glm::vec3(e->box_min[0], e->box_min[1], e->box_min[2]) + d;
                    mx = glm::vec3(e->box_max[0], e->box_max[1], e->box_max[2]) + d;
                }
            }
        if (mesh) {
            mn = glm::vec3(1e30f);
            mx = glm::vec3(-1e30f);
            for (int c = 0; c < 8; ++c) {
                const glm::vec3 l((c & 1) ? mesh->box_max.x : mesh->box_min.x, (c & 2) ? mesh->box_max.y : mesh->box_min.y,
                                  (c & 4) ? mesh->box_max.z : mesh->box_min.z);
                glm::vec3 wpt;  // row vectors: world = local * M
                for (int j = 0; j < 3; ++j) wpt[j] = l.x * pm[j] + l.y * pm[4 + j] + l.z * pm[8 + j] + pm[12 + j];
                mn = glm::min(mn, wpt);
                mx = glm::max(mx, wpt);
            }
        }
        const float top = mx.y + 20, depth_range = 20000;
        const float hw = std::max((mx.x - mn.x) * 0.5f, 2.0f), hd = std::max((mx.z - mn.z) * 0.5f, 2.0f);
        const float cx = (mn.x + mx.x) * 0.5f, cz = (mn.z + mx.z) * 0.5f;
        const glm::vec3 eye = gfx::to_gl(cx, top, cz);
        const glm::mat4 vp = glm::ortho(-hw, hw, -hd, hd, 0.0f, depth_range) * glm::lookAt(eye, eye - glm::vec3(0, 1, 0), glm::vec3(0, 0, -1));
        constexpr int kSize = 32;
        settle_rt_.resize(kSize, kSize);
        settle_rt_.bind();
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        draw_meshes(vp, true, skip_entity);
        draw_project_meshes(app, vp, skip_spawn);
        std::vector<float> depth(kSize * kSize, 1.0f);
        glReadPixels(0, 0, kSize, kSize, GL_DEPTH_COMPONENT, GL_FLOAT, depth.data());
        settle_rt_.unbind();
        const float nearest = *std::min_element(depth.begin(), depth.end());
        if (nearest >= 1.0f) {
            if (!quiet) app.status("Nothing below " + what + " to align it with (meshes further than the draw distance are not drawn)");
            return false;
        }
        const float surface = top - nearest * depth_range;
        const double y = std::round((surface + (p.y - (is_npc ? p.y : mn.y))) * 10) / 10;
        if (std::fabs(y - (*pos)[1]) < 0.05) {
            if (!quiet) app.status(what + " already rests on the mesh below");
            return true;
        }
        (*pos)[1] = y;
        boxes_dirty_ = overlay_dirty_ = true;
        app.touch();
        app.status((quiet ? "Placed " : "Aligned ") + what + " with the mesh below (height " + p2m::fmt_g(y) + ")");
        return true;
    }

    void align_button(const std::string& uid) {
        if (ImGui::Button("Align with mesh")) settle_req_ = uid;
        ui::help("Lets the object down (or up) onto the surface below it: the table under an apple, the ground under a crate. Its lowest point "
                 "goes onto the highest point of what is below its footprint. Key: End (in the view).");
    }

    void draw_project_inspector(App& app) {
        bool ch = false;
        for (auto& n : app.project.npcs) {
            if (n.uid != proj_sel_) continue;
            ImGui::TextUnformatted(n.name.c_str());
            ImGui::TextDisabled("placed NPC of the project (%s)", n.template_name.c_str());
            float p[3] = {static_cast<float>(n.position[0]), static_cast<float>(n.position[1]), static_cast<float>(n.position[2])};
            if (ImGui::DragFloat3("position", p, 5.0f, 0, 0, "%.1f")) {
                n.position = {p[0], p[1], p[2]};
                overlay_dirty_ = true;
                ch = true;
            }
            if (!n.offset) align_button(n.uid);
            if (ImGui::Button("Edit in NPCs")) {
                const std::string uid = n.uid;
                app.defer([&app, uid] { app.select_in(kPanelNpcs, uid); });
            }
        }
        std::string remove;
        for (auto& s : app.project.world.spawns) {
            if (s.uid != proj_sel_) continue;
            ImGui::TextUnformatted(s.template_name.c_str());
            ImGui::TextDisabled("spawned once per savegame by the mod");
            ch |= ui::template_picker("Template", s.template_name, app.game.get(), {TplKind::Item, TplKind::Recipe, TplKind::Object, TplKind::Interactive, TplKind::Effect},
                                      ui::project_item_names(app.project), 200);
            ImGui::SetNextItemWidth(120);
            if (ImGui::InputInt("Amount", &s.amount)) {
                s.amount = std::max(1, s.amount);
                ch = true;
            }
            float p[3] = {static_cast<float>(s.position[0]), static_cast<float>(s.position[1]), static_cast<float>(s.position[2])};
            if (ImGui::DragFloat3("position", p, 5.0f, 0, 0, "%.1f")) {
                s.position = {p[0], p[1], p[2]};
                overlay_dirty_ = true;
                ch = true;
            }
            float r[3] = {static_cast<float>(s.rotation[0]), static_cast<float>(s.rotation[1]), static_cast<float>(s.rotation[2])};
            if (ImGui::DragFloat3("yaw, pitch, roll", r, 1.0f, -360, 360, "%.1f")) {
                s.rotation = {r[0], r[1], r[2]};
                overlay_dirty_ = true;
                ch = true;
            }
            align_button(s.uid);
            if (ImGui::Button("Remove from the project")) remove = s.uid;
        }
        std::string drop;
        for (auto& m : app.project.world.moves) {
            if (m.uid != proj_sel_) continue;
            ImGui::TextUnformatted(m.entity_name.empty() ? m.guid.c_str() : m.entity_name.c_str());
            ImGui::TextDisabled("an entity of the world, moved by the mod on every world.ready");
            ImGui::TextDisabled("id %s", m.guid.c_str());
            if (m.position) {
                float p[3] = {static_cast<float>((*m.position)[0]), static_cast<float>((*m.position)[1]), static_cast<float>((*m.position)[2])};
                if (ImGui::DragFloat3("position", p, 5.0f, 0, 0, "%.1f")) {
                    m.position = Vec3{p[0], p[1], p[2]};
                    ch = true;
                }
            }
            if (m.rotation) {
                float r[3] = {static_cast<float>((*m.rotation)[0]), static_cast<float>((*m.rotation)[1]), static_cast<float>((*m.rotation)[2])};
                if (ImGui::DragFloat3("yaw, pitch, roll", r, 1.0f, -360, 360, "%.1f")) {
                    m.rotation = Vec3{r[0], r[1], r[2]};
                    ch = true;
                }
            }
            if (m.position) align_button(m.uid);
            if (ImGui::Button("Back to its place (drop the move)")) drop = m.uid;
        }
        if (!drop.empty()) {
            auto& v = app.project.world.moves;
            v.erase(std::remove_if(v.begin(), v.end(), [&](const EntityRef& x) { return x.uid == drop; }), v.end());
            proj_sel_.clear();
            ch = true;
        }
        if (!remove.empty()) {
            auto& v = app.project.world.spawns;
            v.erase(std::remove_if(v.begin(), v.end(), [&](const WorldSpawn& x) { return x.uid == remove; }), v.end());
            proj_sel_.clear();
            overlay_dirty_ = true;
            ch = true;
        }
        if (ch) app.touch();
    }

    // items the mod adds to a container of the world (a chest, a corpse ...)
    void draw_container_editor(App& app, const WorldEntity& e) {
        ImGui::SeparatorText("Items this mod adds");
        ContainerEdit* ce = nullptr;
        for (auto& c : app.project.world.containers)
            if (c.guid == e.guid) ce = &c;
        if (!ce) {
            if (ImGui::Button("Add items to this container")) {
                ContainerEdit c;
                c.uid = new_uid();
                c.guid = e.guid;
                c.entity_name = e.name;
                c.layer = world_->layers[e.layer].name;
                c.items.push_back({"It_Gold", 50, false});
                app.project.world.containers.push_back(c);
                app.touch();
            }
            ImGui::TextDisabled("Put once per savegame into this entity (risen.entity.by_id).");
            return;
        }
        bool ch = ui::inventory_list("ce", ce->items, app, ui::project_item_names(app.project), false);
        if (ImGui::SmallButton("Remove these additions")) {
            const std::string uid = ce->uid;
            auto& v = app.project.world.containers;
            v.erase(std::remove_if(v.begin(), v.end(), [&](const ContainerEdit& x) { return x.uid == uid; }), v.end());
            ch = true;
        }
        if (ch) app.touch();
    }

    // moving or removing an entity of the world (E12): the mod applies it by the entity's id on every world.ready
    void draw_world_edit(App& app, const WorldEntity& e) {
        ImGui::SeparatorText("Change it in the world");
        auto& rem = app.project.world.removals;
        auto& mov = app.project.world.moves;
        auto removed = std::find_if(rem.begin(), rem.end(), [&](const EntityRef& r) { return r.guid == e.guid; });
        auto moved = std::find_if(mov.begin(), mov.end(), [&](const EntityRef& r) { return r.guid == e.guid; });
        if (removed != rem.end()) {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Removed by this mod.");
            ImGui::SameLine();
            if (ImGui::SmallButton("Keep it")) {
                rem.erase(removed);
                app.touch();
            }
            return;
        }
        if (moved != mov.end()) {
            ImGui::TextColored(ImVec4(1, 0.65f, 0.25f, 1), "Moved by this mod.");
            ImGui::SameLine();
            if (ImGui::SmallButton("Edit the move")) {
                proj_sel_ = moved->uid;
                overlay_dirty_ = true;
            }
            return;
        }
        if (ImGui::SmallButton("Move or turn it")) {
            EntityRef r;
            r.uid = new_uid();
            r.guid = e.guid;
            r.entity_name = e.name;
            r.layer = world_->layers[e.layer].name;
            spass::pose::Matrix m;
            std::copy(e.matrix.begin(), e.matrix.end(), m.begin());
            const spass::pose::Angles a = spass::pose::decompose(m);
            r.position = Vec3{e.x(), e.y(), e.z()};
            r.rotation = Vec3{std::round(a.yaw * 10) / 10, std::round(a.pitch * 10) / 10, std::round(a.roll * 10) / 10};
            mov.push_back(r);
            proj_sel_ = r.uid;
            overlay_dirty_ = true;
            app.touch();
            app.status("Drag the gizmo (move / turn) or type the new pose");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove it from the world")) {
            EntityRef r;
            r.uid = new_uid();
            r.guid = e.guid;
            r.entity_name = e.name;
            r.layer = world_->layers[e.layer].name;
            rem.push_back(r);
            app.touch();
        }
        if (e.npc) ImGui::TextDisabled("An NPC: his routine moves him again, and removing him can break quests.");
    }

    // What is under the cursor: the meshes by an ID pass into the view's render target (exact: the mesh's pixels, holes of foliage included,
    // hidden by what is in front), the entities drawn as boxes by the ray; the nearer one wins. Undrawn entities (hidden boxes of vegetation,
    // helpers, meshes beyond the distance) and level meshes without their boxes are never selected.
    void pick_at(float u, float v, float aspect, int w, int h, const glm::mat4& vp) {
        glm::vec3 o, d;
        cam_.ray(u, v, aspect, o, d);
        const float dlen = std::max(glm::length(d), 1e-6f);
        size_t best = SIZE_MAX;
        float best_t = 1e30f;   // distance along the ray, in GL units
        float surface_t = 1e30f;  // the nearest drawn surface under the cursor
        if (draw_meshes_ && res_) {
            rt_.bind();
            glEnable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            draw_meshes(vp, true);
            const int px = std::clamp(static_cast<int>(u * w), 0, w - 1), py = std::clamp(static_cast<int>((1.0f - v) * h), 0, h - 1);
            unsigned char rgba[4] = {};
            float depth = 1;
            glReadPixels(px, py, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            glReadPixels(px, py, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
            rt_.unbind();
            if (depth < 1.0f) {
                const glm::vec4 ndc(u * 2 - 1, (1 - v) * 2 - 1, depth * 2 - 1, 1);
                const glm::vec4 p = glm::inverse(vp) * ndc;
                if (std::fabs(p.w) > 1e-9f) surface_t = glm::length(glm::vec3(p) / p.w - o);
            }
            const uint32_t id = gfx::MeshRenderer::id_of(rgba);
            if (id > 0 && id <= world_->entities.size()) {
                best = id - 1;
                best_t = surface_t;
            }
        }
        for (size_t i = 0; i < world_->entities.size(); ++i) {
            const WorldEntity& e = world_->entities[i];
            if (!visible(e)) continue;
            // boxes the user sees (a mesh that is drawn was tested above, by its pixels)
            if (i >= box_drawn_.size() || !box_drawn_[i]) continue;
            glm::vec3 mn, mx;
            // a moved entity is clicked where it is drawn now
            glm::vec3 off(0);
            if (auto mv = moved_matrix_.find(i); mv != moved_matrix_.end()) off = glm::vec3(mv->second[12] - e.x(), mv->second[13] - e.y(), mv->second[14] - e.z());
            if (e.has_box()) {
                mn = to_gl(e.box_min[0] + off.x, e.box_min[1] + off.y, e.box_max[2] + off.z);
                mx = to_gl(e.box_max[0] + off.x, e.box_max[1] + off.y, e.box_min[2] + off.z);
            } else {
                const glm::vec3 p = to_gl(e.x() + off.x, e.y() + off.y, e.z() + off.z);
                mn = p - glm::vec3(25, 0, 25);
                mx = p + glm::vec3(25, 50, 25);
            }
            const float t = ray_box(o, d, mn, mx) * dlen;
            if (t <= 0 || t >= best_t) continue;  // a box around the camera (t == 0) does not count
            if (t > surface_t + 50.0f) continue;  // behind a drawn surface (a hill, a wall)
            best_t = t;
            best = i;
        }
        if (best != SIZE_MAX) pick(best);
        if (best != SIZE_MAX) proj_sel_.clear();
    }

    void draw_inspector(App& app) {
        if (!proj_sel_.empty() && project_position(app, proj_sel_)) {
            draw_project_inspector(app);
            return;
        }
        if (selected_ >= world_->entities.size()) {
            ImGui::TextWrapped("Click an entity in the view, or find one by name.");
            ImGui::Spacing();
            ImGui::TextDisabled("Colours: the kind of the entity's template (see Show). Magenta: the project's placed NPCs.");
            return;
        }
        const WorldEntity& e = world_->entities[selected_];
        ImGui::TextUnformatted(e.name.c_str());
        ImGui::TextDisabled("%s  %s", kind_name(e.kind), e.npc ? "(NPC)" : "");
        if (ImGui::BeginTable("##info", 2, ImGuiTableFlags_SizingStretchProp)) {
            auto row = [](const char* k, const std::string& v) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", k);
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", v.c_str());
            };
            row("template", e.template_name.empty() ? "?" : e.template_name);
            row("id", e.guid);
            char pos[96];
            std::snprintf(pos, sizeof pos, "%.1f  %.1f  %.1f", e.x(), e.y(), e.z());
            row("position", pos);
            std::snprintf(pos, sizeof pos, "%.1f deg", e.yaw());
            row("yaw", pos);
            row("layer", world_->layers[e.layer].group + "/" + world_->layers[e.layer].name);
            if (!e.mesh.empty()) row("mesh", e.mesh);
            ImGui::EndTable();
        }
        if (ImGui::SmallButton("Copy id")) ImGui::SetClipboardText(e.guid.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy position")) {
            char buf[96];
            std::snprintf(buf, sizeof buf, "{ %.1f, %.1f, %.1f }", e.x(), e.y(), e.z());
            ImGui::SetClipboardText(buf);
        }
        ImGui::SameLine();
        if (!e.template_name.empty() && app.game && app.game->tpl(e.template_name) && ImGui::SmallButton("Template in Browser")) {
            const std::string t = e.template_name;
            app.defer([&app, t] { app.select_in(kPanelBrowser, t); });
        }
        draw_world_edit(app, e);
        if (props_for_ != selected_) {
            props_ = entity_properties(*app.game, *world_, e);
            inv_ = e.inventory ? entity_inventory(*app.game, *world_, e) : std::vector<std::pair<std::string, int>>{};
            props_for_ = selected_;
        }
        if (e.inventory && !e.npc) draw_container_editor(app, e);
        if (!inv_.empty()) {
            ImGui::SeparatorText("Inventory");
            for (auto& [item, n] : inv_) {
                const std::string lbl = app.game->label(item);
                ImGui::Text("%dx %s", n, item.c_str());
                if (!lbl.empty()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", lbl.c_str());
                }
            }
        }
        ImGui::SeparatorText("Properties");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##pf", "filter", &prop_filter_);
        if (ImGui::BeginTable("##props", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY)) {
            for (auto& p : props_) {
                const std::string k = p.pset + "." + p.name, v = val_text(p.value);
                if (!icontains(k, prop_filter_) && !icontains(v, prop_filter_)) continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(k.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(v.c_str());
            }
            ImGui::EndTable();
        }
    }

    std::shared_ptr<const World> world_;
    std::future<std::shared_ptr<World>> loading_;
    std::string loading_name_ = "World";
    bool tried_ = false;
    std::vector<char> layer_on_;
    bool kinds_[kTplKindCount] = {true, true, true, true, true, true, false, false, false, false};
    bool show_level_ = false;
    bool draw_meshes_ = true, boxes_for_meshes_ = false, boxes_plain_objects_ = false;
    float draw_distance_ = 9000;
    gfx::ResourceCache* res_ = nullptr;  // App::resources()
    gfx::MeshRenderer mesh_r_;
    std::vector<std::string> mesh_names_;  // per entity: the compiled mesh name ("" = none)
    std::vector<char> box_drawn_, mesh_drawn_;  // per entity: its box is in the batch / its mesh was drawn last frame (what a click may select)
    size_t drawn_ = 0, failed_seen_ = 0;
    // placing
    int mode_ = 0;  // 0 select, 1 place NPC, 2 place item or object
    int gizmo_op_ = 0;  // 0 move, 1 turn (yaw)
    float place_yaw_ = 0;
    std::vector<int8_t> edit_state_;  // per entity: 0, 1 removed, 2 moved (sync_edits)
    std::map<size_t, spass::pose::Matrix> moved_matrix_;
    std::string edits_sig_;
    std::string place_npc_, place_template_ = "Peasant_1_01", place_item_ = "It_Apple";
    int place_amount_ = 1;
    std::string proj_sel_;  // the selected project object (placed NPC or spawn uid)
    glm::mat4 gizmo_{1.0f};
    bool gizmo_active_ = false, gizmo_over_ = false, view_active_ = false;
    gfx::RenderTarget settle_rt_;
    std::string settle_req_, settle_auto_;  // align with mesh: asked for / after a placement
    int settle_wait_ = 0;
    bool boxes_dirty_ = true, overlay_dirty_ = true;
    gfx::LineBatch boxes_, overlay_;
    gfx::RenderTarget rt_;
    gfx::FlyCamera cam_;
    size_t selected_ = SIZE_MAX, props_for_ = SIZE_MAX;
    std::vector<p2m::Prop> props_;
    std::vector<std::pair<std::string, int>> inv_;
    std::string search_, prop_filter_;
    std::vector<size_t> search_hits_;
};

}  // namespace

std::unique_ptr<Panel> make_world_panel() { return std::make_unique<WorldPanel>(); }

}  // namespace mk
