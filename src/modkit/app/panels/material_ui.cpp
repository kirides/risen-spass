#include "app/panels/material_ui.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <filesystem>

#include "app/app.h"
#include "app/render/resources.h"
#include "core/i18n.h"
#include "core/image.h"
#include "core/mesh.h"

namespace mk {

namespace {

const char* const kFlags[] = {"EnableSpecular",         "CompressedNormalMap",    "DisableLighting",   "DisableFog",   "DisableDirLightShadows",
                              "DisablePntLightShadows", "DisableAmbientOcclusion", "EnableAlphaZWrite", "UseDepthBias", "TradeAlphaAsSolid"};
const char* const kComponents[] = {"", ".rgb", ".r", ".g", ".b", ".a"};
const ImVec4 kChanged(1.0f, 0.8f, 0.4f, 1);

std::string stem_of(const std::string& name) {
    const auto dot = name.rfind('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}
// "X.tga" / "X" -> "X._ximg"
std::string compiled_image(const std::string& raw) { return stem_of(raw) + "._ximg"; }

MaterialEdit* find_edit(Project& p, const std::string& material) {
    for (auto& e : p.materials)
        if (p2m::lower(e.material) == p2m::lower(material)) return &e;
    return nullptr;
}

const MatObject* shader_of(const Material& m) {
    for (auto& o : m.objects)
        if (o.cls == "eCShaderDefault") return &o;
    return nullptr;
}

}  // namespace

const MaterialInspector::Original& MaterialInspector::original(App& app, const std::string& material) {
    auto& o = originals_[p2m::lower(material)];
    if (!o.ok && o.error.empty()) {
        try {
            o.m = Material::parse(app.game->read(material));
            o.ok = true;
        } catch (const std::exception& e) {
            o.error = e.what();
        }
    }
    return o;
}

const std::vector<std::string>& MaterialInspector::materials_of(App& app, const std::string& mesh) {
    auto it = mesh_materials_.find(mesh);
    if (it != mesh_materials_.end()) return it->second;
    std::vector<std::string> r;
    try {
        const MeshData d = decode_mesh(app.game->read(mesh));
        for (auto& s : d.submeshes)
            if (!s.material.empty() && std::find(r.begin(), r.end(), s.material) == r.end()) r.push_back(s.material);
    } catch (const std::exception&) {
        // an actor or a mesh the editor cannot read: no materials to edit
    }
    return mesh_materials_[mesh] = std::move(r);
}

const std::vector<std::string>& MaterialInspector::images(App& app) {
    if (images_assets_ != app.project.assets.size() || images_.empty()) {
        images_.clear();
        for (auto& a : app.project.assets) {
            const std::string name = std::filesystem::path(a.file).filename().string();
            if (is_picture(name)) images_.push_back(stem_of(name));
        }
        const auto game_from = static_cast<std::ptrdiff_t>(images_.size());
        for (auto& a : app.game->assets("_ximg")) images_.push_back(stem_of(a.name));
        std::sort(images_.begin() + game_from, images_.end(), [](const std::string& x, const std::string& y) { return p2m::lower(x) < p2m::lower(y); });
        images_assets_ = app.project.assets.size();
    }
    return images_;
}

bool MaterialInspector::pick_name(const char* id, const std::vector<std::string>& all, std::string* picked, const char* suffix) {
    bool done = false;
    if (ImGui::BeginPopup(id)) {
        ImGui::SetNextItemWidth(320);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::InputTextWithHint("##f", tr("filter"), &filter_);
        std::vector<const std::string*> hits;
        const std::string f = p2m::lower(filter_);
        for (auto& s : all)
            if (f.empty() || p2m::lower(s).find(f) != std::string::npos) hits.push_back(&s);
        if (ImGui::BeginChild("##list", ImVec2(320, 260))) {
            ImGuiListClipper clip;
            clip.Begin(static_cast<int>(hits.size()));
            while (clip.Step())
                for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i)
                    if (ImGui::Selectable(hits[static_cast<size_t>(i)]->c_str())) {
                        *picked = *hits[static_cast<size_t>(i)] + suffix;
                        done = true;
                        ImGui::CloseCurrentPopup();
                    }
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    return done;
}

bool MaterialInspector::pick_image(App& app, const char* id, std::string* picked) { return pick_name(id, images(app), picked, ".tga"); }

const std::vector<std::string>& MaterialInspector::material_names(App& app) {
    if (material_names_.empty()) {
        for (auto& a : app.game->assets("_xmat")) material_names_.push_back(stem_of(a.name));
        std::sort(material_names_.begin(), material_names_.end(), [](const std::string& x, const std::string& y) { return p2m::lower(x) < p2m::lower(y); });
    }
    return material_names_;
}

const MeshInfo* MaterialInspector::info_of(App& app, const std::string& mesh) {
    auto it = mesh_infos_.find(mesh);
    if (it == mesh_infos_.end()) {
        std::pair<bool, MeshInfo> v;
        try {
            v.second = mesh_info(app.game->read(mesh));
            v.first = true;
        } catch (const std::exception&) {
        }
        it = mesh_infos_.emplace(mesh, std::move(v)).first;
    }
    return it->second.first ? &it->second.second : nullptr;
}

void MaterialInspector::draw_mesh(App& app, const std::string& mesh) {
    const MeshInfo* info = info_of(app, mesh);
    if (!info) return;
    uint32_t tris = 0, verts = 0;
    for (auto& s : info->subs) {
        tris += s.triangles;
        verts += s.vertices;
    }
    ImGui::TextDisabled(tr("%zu sub-mesh(es), %u vertices, %u triangles"), info->subs.size(), verts, tris);
    ImGui::TextDisabled(tr("size %.0f x %.0f x %.0f"), info->box_max[0] - info->box_min[0], info->box_max[1] - info->box_min[1], info->box_max[2] - info->box_min[2]);
    if (!info->writable) {
        ImGui::TextDisabled("%s", tr("This mesh cannot be changed by the editor."));
        ImGui::TextDisabled("%s", info->why_not.c_str());
        return;
    }
    auto find = [&]() -> MeshEdit* {
        for (auto& e : app.project.meshes)
            if (p2m::lower(e.mesh) == p2m::lower(mesh)) return &e;
        return nullptr;
    };
    auto edit = [&]() -> MeshEdit& {
        if (MeshEdit* e = find()) return *e;
        MeshEdit e;
        e.uid = new_uid();
        e.mesh = mesh;
        app.project.meshes.push_back(e);
        return app.project.meshes.back();
    };
    auto prune = [&] {
        auto& v = app.project.meshes;
        v.erase(std::remove_if(v.begin(), v.end(), [](const MeshEdit& e) { return e.changes_nothing() && e.notes.empty(); }), v.end());
    };
    for (size_t i = 0; i < info->subs.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const MeshEdit* e = find();
        const bool changed = e && e->materials.count(static_cast<int>(i)) != 0;
        const std::string shown = changed ? e->materials.at(static_cast<int>(i)) : info->subs[i].material;
        ImGui::TextDisabled("#%zu  %u tri", i, info->subs[i].triangles);
        ImGui::SameLine();
        if (changed) ImGui::PushStyleColor(ImGuiCol_Text, kChanged);
        if (ImGui::Button(shown.c_str(), ImVec2(changed ? -60.0f : -1.0f, 0))) ImGui::OpenPopup("mat");
        if (changed) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered() && changed) ImGui::SetTooltip("%s", trf("shipped: {0}", {info->subs[i].material}).c_str());
        std::string picked;
        if (pick_name("mat", material_names(app), &picked, "._xmat")) {
            MeshEdit& m = edit();
            if (p2m::lower(picked) == p2m::lower(info->subs[i].material)) m.materials.erase(static_cast<int>(i));
            else m.materials[static_cast<int>(i)] = picked;
            prune();
            app.touch();
        }
        if (changed) {
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("Reset"))) {
                find()->materials.erase(static_cast<int>(i));
                prune();
                app.touch();
            }
        }
        ImGui::PopID();
    }
    const MeshEdit* cur = find();
    float sc[3] = {cur ? cur->scale[0] : 1.0f, cur ? cur->scale[1] : 1.0f, cur ? cur->scale[2] : 1.0f};
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::DragFloat3("##scale", sc, 0.01f, 0.01f, 100.0f, "%.2f")) {
        MeshEdit& m = edit();
        for (int k = 0; k < 3; ++k) m.scale[static_cast<size_t>(k)] = std::max(0.01f, sc[k]);
        prune();
        app.touch();
    }
    ImGui::TextDisabled("%s", tr("scale x, y, z (the mesh itself: every object that uses it changes)"));
    if (find()) {
        if (ImGui::Button(tr("Reset the whole mesh"))) {
            auto& v = app.project.meshes;
            v.erase(std::remove_if(v.begin(), v.end(), [&](const MeshEdit& e) { return p2m::lower(e.mesh) == p2m::lower(mesh); }), v.end());
            app.touch();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", tr("exported as a loose copy of the mesh"));
    }
}

void MaterialInspector::draw(App& app, const std::string& mesh) {
    if (!app.game || mesh.empty()) return;
    ImGui::SeparatorText(tr("Mesh"));
    draw_mesh(app, mesh);
    ImGui::SeparatorText(tr("Materials"));
    const auto& mats = materials_of(app, mesh);
    if (mats.empty()) {
        ImGui::TextDisabled("%s", tr("This mesh has no materials the editor can change."));
        return;
    }
    const auto same = [&](const std::string& m) { return p2m::lower(m) == p2m::lower(selected_); };
    if (std::none_of(mats.begin(), mats.end(), same)) selected_ = mats.front();
    for (const std::string& m : mats) {
        const bool edited = find_edit(app.project, m) != nullptr;
        const std::string label = m + (edited ? "  *" : "");
        if (ImGui::Selectable(label.c_str(), same(m))) selected_ = m;
    }
    MatTarget t;
    const std::string material = selected_;
    t.find = [&app, material]() -> MaterialEdit* { return find_edit(app.project, material); };
    t.create = [&app, material]() -> MaterialEdit& {
        if (MaterialEdit* e = find_edit(app.project, material)) return *e;
        MaterialEdit e;
        e.uid = new_uid();
        e.material = material;
        app.project.materials.push_back(e);
        return app.project.materials.back();
    };
    t.prune = [&app] {
        auto& v = app.project.materials;
        v.erase(std::remove_if(v.begin(), v.end(), [](const MaterialEdit& e) { return e.slots.empty() && e.props.empty() && e.notes.empty(); }), v.end());
    };
    t.reset = [&app, material] {
        auto& v = app.project.materials;
        v.erase(std::remove_if(v.begin(), v.end(), [&](const MaterialEdit& e) { return p2m::lower(e.material) == p2m::lower(material); }), v.end());
    };
    t.note = tr("exported as a loose copy of the material (every object that uses it changes)");
    draw_material(app, selected_, t);
}

void MaterialInspector::draw_material(App& app, const std::string& material, const MatTarget& t) {
    const Original& orig = original(app, material);
    if (!orig.ok) {
        ImGui::TextWrapped("%s", trf("The material cannot be read: {0}", {orig.error}).c_str());
        return;
    }
    // what the material looks like with the project's change applied
    Material cur = orig.m;
    if (const MaterialEdit* e = t.find()) {
        std::string why;
        apply_material_edit(cur, *e, why);
    }
    auto edit = [&]() -> MaterialEdit& { return t.create(); };
    auto prune = [&] { t.prune(); };
    gfx::ResourceCache* res = app.resources();

    ImGui::SeparatorText(tr("Shader slots"));
    if (ImGui::BeginTable("##slots", 3, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("##t", ImGuiTableColumnFlags_WidthFixed, 44);
        ImGui::TableSetupColumn(tr("slot"));
        ImGui::TableSetupColumn(tr("image"));
        const auto slots = cur.slots();
        const auto orig_slots = orig.m.slots();
        for (size_t k = 0; k < slots.size(); ++k) {
            const MatSlot& s = slots[k];
            if (!s.linked || s.node < 0) continue;
            const int sampler = cur.slot_sampler(s);
            const std::string img = cur.slot_image(s);
            const std::string orig_img = k < orig_slots.size() ? orig.m.slot_image(orig_slots[k]) : std::string();
            ImGui::PushID(s.name.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (res && !img.empty()) ImGui::Image(static_cast<ImTextureID>(static_cast<intptr_t>(res->texture(compiled_image(img)))), ImVec2(40, 40));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(s.name.c_str());
            ImGui::TextDisabled("%s%s", cur.objects[static_cast<size_t>(s.node)].cls.c_str() + 2, s.component < 6 ? kComponents[s.component] : "");
            ImGui::TableNextColumn();
            if (sampler < 0) {
                ImGui::TextDisabled("%s", tr("no texture (a constant or an effect)"));
            } else {
                const bool changed = img != orig_img;
                if (changed) ImGui::PushStyleColor(ImGuiCol_Text, kChanged);
                if (ImGui::Button((img.empty() ? std::string("...") : img).c_str(), ImVec2(-1, 0))) ImGui::OpenPopup("pick");
                if (changed) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", changed ? trf("shipped: {0}", {orig_img}).c_str() : tr("click to choose another image"));
                std::string picked;
                if (pick_image(app, "pick", &picked)) {
                    MaterialEdit& e = edit();
                    if (picked == orig_img) e.slots.erase(s.name);
                    else e.slots[s.name] = picked;
                    prune();
                    app.touch();
                }
                if (changed && ImGui::SmallButton(tr("Reset"))) {
                    if (MaterialEdit* e = t.find()) e->slots.erase(s.name);
                    prune();
                    app.touch();
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    const MatObject* shader = shader_of(cur);
    const MatObject* shipped_shader = shader_of(orig.m);
    if (shader && shipped_shader) {
        ImGui::SeparatorText(tr("Shader flags"));
        for (const char* f : kFlags) {
            const MatProp* p = shader->find(f);
            if (!p || p->payload.size() != 1) continue;
            bool v = shader->get_bool(f);
            const bool shipped = shipped_shader->get_bool(f);
            if (v != shipped) ImGui::PushStyleColor(ImGuiCol_Text, kChanged);
            const bool hit = ImGui::Checkbox(f, &v);
            if (v != shipped) ImGui::PopStyleColor();
            if (hit) {
                const std::string key = std::string("eCShaderDefault.") + f;
                MaterialEdit& e = edit();
                if (v == shipped) e.props.erase(key);
                else e.props[key] = v ? 1 : 0;
                prune();
                app.touch();
            }
        }
    }
    if (t.find()) {
        ImGui::Spacing();
        if (ImGui::Button(tr("Reset the whole material"))) {
            t.reset();
            app.touch();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", t.note);
    }
}

void MaterialInspector::draw_look(App& app, ObjectLook& look) {
    if (look.mesh.empty() || !app.game) return;
    const MeshInfo* info = info_of(app, look.mesh);
    if (!info || !info->writable) {
        ImGui::TextDisabled("%s", tr("The mesh of this object cannot be changed by the editor."));
        return;
    }
    float sc[3] = {look.scale[0], look.scale[1], look.scale[2]};
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::DragFloat3("##lookscale", sc, 0.01f, 0.01f, 100.0f, "%.2f")) {
        for (int k = 0; k < 3; ++k) look.scale[static_cast<size_t>(k)] = std::max(0.01f, sc[k]);
        app.touch();
    }
    ImGui::TextDisabled("%s", tr("scale x, y, z of this object's own mesh"));
    for (size_t i = 0; i < info->subs.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const int idx = static_cast<int>(i);
        ImGui::SeparatorText(trf("Sub-mesh {0}: {1}", {std::to_string(i), info->subs[i].material}).c_str());
        auto it = look.materials.find(idx);
        if (it == look.materials.end()) {
            if (ImGui::Button(tr("Give it its own material"))) {
                MaterialEdit m;
                m.uid = new_uid();
                m.material = info->subs[i].material;
                m.notes = "own";
                look.materials[idx] = m;
                app.touch();
            }
        } else {
            MatTarget t;
            t.find = [&look, idx]() -> MaterialEdit* {
                auto f = look.materials.find(idx);
                return f == look.materials.end() ? nullptr : &f->second;
            };
            t.create = [&look, idx]() -> MaterialEdit& { return look.materials[idx]; };
            t.prune = [] {};  // an own material stays, even when nothing differs from the shipped one yet
            t.reset = [&look, idx] { look.materials.erase(idx); };
            t.note = tr("this object has its own copy of the material");
            draw_material(app, it->second.material, t);
        }
        ImGui::PopID();
    }
}

void MaterialInspector::sync(App& app) {
    gfx::ResourceCache* res = app.resources();
    if (!res) return;
    std::unordered_map<std::string, std::string> m;
    for (const MaterialEdit& e : app.project.materials) {
        auto it = e.slots.find("Diffuse");
        if (it != e.slots.end()) m[p2m::lower(e.material)] = compiled_image(it->second);
    }
    res->set_diffuse_overrides(std::move(m));
}

}  // namespace mk
