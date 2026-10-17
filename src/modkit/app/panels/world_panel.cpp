// World panel (docs/editor/REQUIREMENTS.md WLD-1, WLD-2, WLD-9): a game world read offline, every entity as its bounding box in a 3D view with a fly
// camera, layers and kinds to show, search, picking, and the inspector of the selected entity (template, GUID, position, properties, inventory).
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <future>
#include <map>
#include <set>

#include <glm/gtc/matrix_transform.hpp>

#include "app/panels/panels.h"
#include "app/render/gl_util.h"
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

class WorldPanel : public Panel {
public:
    const char* title() const override { return kPanelWorld; }
    bool busy() const override { return loading_.valid(); }

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
        ImGui::SameLine();
        ImGui::TextDisabled("  right mouse: look, WASD/QE: move, wheel: speed, click: select, F: focus");
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
        for (auto& e : world_->entities) {
            if (!visible(e)) continue;
            const uint32_t c = kind_color(e.kind);
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
        // the project's placed NPCs with a world position (magenta)
        for (auto& n : app.project.npcs) {
            if (n.offset) continue;
            const glm::vec3 p = to_gl(static_cast<float>(n.position[0]), static_cast<float>(n.position[1]), static_cast<float>(n.position[2]));
            overlay_.box(p + glm::vec3(-40, 0, -40), p + glm::vec3(40, 200, 40), IM_COL32(255, 80, 255, 255));
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
        cam_.look_at(to_gl(e.x(), e.y() + 100, e.z()), std::min(size * 2.5f, 20000.0f) + 400);
    }

    void draw_view(App& app) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const int w = std::max(16, static_cast<int>(avail.x)), h = std::max(16, static_cast<int>(avail.y));
        if (boxes_dirty_) rebuild_boxes();
        if (overlay_dirty_) rebuild_overlay(app);
        // input first, so the frame shows this frame's camera
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##viewport", ImVec2(static_cast<float>(w), static_cast<float>(h)),
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const bool hovered = ImGui::IsItemHovered();
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
        if (hovered && ImGui::IsKeyPressed(ImGuiKey_F) && selected_ < world_->entities.size()) fly_to(world_->entities[selected_]);
        if (ImGui::IsItemDeactivated() && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && glm::length(glm::vec2(ImGui::GetMouseDragDelta(0).x, ImGui::GetMouseDragDelta(0).y)) < 4) {
            const float u = (io.MousePos.x - origin.x) / static_cast<float>(w), v = (io.MousePos.y - origin.y) / static_cast<float>(h);
            pick_at(u, v, aspect);
        }

        rt_.resize(w, h);
        rt_.bind();
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glClearColor(0.10f, 0.11f, 0.13f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        const glm::mat4 vp = cam_.proj(aspect) * cam_.view();
        boxes_.draw(vp);
        glDisable(GL_DEPTH_TEST);
        overlay_.draw(vp);
        rt_.unbind();
        ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(rt_.texture())), origin, ImVec2(origin.x + w, origin.y + h),
                                             ImVec2(0, 1), ImVec2(1, 0));
        // camera position in game coordinates
        const glm::vec3 gp = gfx::from_gl(cam_.pos);
        char buf[96];
        std::snprintf(buf, sizeof buf, "camera %.0f %.0f %.0f   speed %.0f", gp.x, gp.y, gp.z, cam_.speed);
        ImGui::GetWindowDrawList()->AddText(ImVec2(origin.x + 8, origin.y + h - 22), IM_COL32(200, 200, 200, 255), buf);
    }

    void pick_at(float u, float v, float aspect) {
        glm::vec3 o, d;
        cam_.ray(u, v, aspect, o, d);
        size_t best = SIZE_MAX;
        float best_t = 1e30f;
        for (size_t i = 0; i < world_->entities.size(); ++i) {
            const WorldEntity& e = world_->entities[i];
            if (!visible(e)) continue;
            glm::vec3 mn, mx;
            if (e.has_box()) {
                mn = to_gl(e.box_min[0], e.box_min[1], e.box_max[2]);
                mx = to_gl(e.box_max[0], e.box_max[1], e.box_min[2]);
            } else {
                const glm::vec3 p = to_gl(e.x(), e.y(), e.z());
                mn = p - glm::vec3(25, 0, 25);
                mx = p + glm::vec3(25, 50, 25);
            }
            const float t = ray_box(o, d, mn, mx);
            if (t <= 0 || t >= best_t) continue;  // a box around the camera (t == 0) does not count
            best_t = t;
            best = i;
        }
        if (best != SIZE_MAX) pick(best);
    }

    void draw_inspector(App& app) {
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
        if (props_for_ != selected_) {
            props_ = entity_properties(*app.game, *world_, e);
            inv_ = e.inventory ? entity_inventory(*app.game, *world_, e) : std::vector<std::pair<std::string, int>>{};
            props_for_ = selected_;
        }
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
