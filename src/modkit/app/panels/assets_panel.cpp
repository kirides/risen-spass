// Assets panel (docs/editor/REQUIREMENTS.md AST-1, AST-2): the project's own files. Compiled resources (._xmsh, ._xmat, ._ximg, ._xmac, ._xmot,
// ._xsnd) are exported into assets/ and mounted by the layer, where they shadow the game's files of the same name (RE.md section 26); audio files
// (Ogg Vorbis, WAV, MP3) go to sounds/ for voiced dialogue lines. The 3D views read the project's files first, the pickers offer them.
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <map>

#include "app/audio.h"
#include "app/editor_util.h"
#include "app/panels/panels.h"
#include "app/pickers.h"
#include "app/render/mesh_preview.h"
#include "app/render/resources.h"
#include "app/widgets.h"
#include "core/assets.h"
#include "core/i18n.h"
#include "core/image.h"
#include "core/mesh.h"
#include "core/model_import.h"
#include "core/paths.h"
#include "pack2mod/util.h"

namespace mk {

namespace {

class AssetsPanel : public Panel {
public:
    const char* title() const override { return kPanelAssets; }

    void on_project_changed(App&) override {
        sel_.clear();
        info_.clear();
    }
    bool select(App& app, const std::string& key) override {
        for (auto& a : app.project.assets)
            if (a.uid == key || p2m::lower(p2m::bare_name(a.file)) == p2m::lower(key)) {
                sel_ = a.uid;
                app.focus_panel(title());
                return true;
            }
        return false;
    }

    // adds files; a file inside the project's folder is stored relative to it (the project can move with its assets)
    void add_files(App& app, const std::vector<std::string>& files) {
        const fs::path base = app.project_path.empty() ? fs::path() : u8(app.project_path).parent_path();
        int added = 0;
        for (auto& f : files) {
            fs::path p = u8(f);
            std::string stored = u8str(p);
            if (!base.empty() && path_within(p, base)) stored = u8str(fs::relative(p, base));
            std::replace(stored.begin(), stored.end(), '\\', '/');
            if (find_asset(app.project, u8str(p.filename()))) {
                app.status(trf("{0} is already an asset of the project", {u8str(p.filename())}));
                continue;
            }
            AssetFile a{new_uid(), stored, asset_kind(u8str(p.filename()))};
            app.project.assets.push_back(a);
            sel_ = a.uid;
            ++added;
        }
        if (added) {
            app.touch();
            app.status(trf("{0} asset file(s) added", {std::to_string(added)}));
        }
    }

    void draw(App& app) override {
        if (!ImGui::Begin(ui::panel_label(title()).c_str(), &open)) {
            ImGui::End();
            return;
        }
        if (ImGui::Button(tr("Add files..."))) {
            const std::string dir = app.project_path.empty() ? std::string() : u8str(u8(app.project_path).parent_path());
            add_files(app, dialog_open_files("Resources, pictures and audio", "_xmsh,_xmat,_ximg,_xmac,_xmot,_xsnd,ximl,png,tga,jpg,jpeg,bmp,ogg,wav,mp3", dir));
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Import 3D model..."))) begin_model_import(app);
        ui::help(tr("A model of your own (Wavefront OBJ, glTF, GLB) becomes a compiled mesh (._xmsh) in the project's assets folder. Pick the game materials it uses."));
        ImGui::SameLine();
        if (ImGui::Button(tr("Reload"))) {
            app.refresh_assets(true);
            info_.clear();
        }
        ui::help(tr("Reads the files again (after you changed them outside the editor)."));
        ImGui::SameLine();
        ImGui::TextDisabled(tr("%zu files"), app.project.assets.size());
        if (app.project_path.empty())
            ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "%s", tr("Save the project first to store the files relative to the project folder."));

        ImGui::BeginChild("##list", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.45f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
        std::string remove;
        if (ImGui::BeginTable("##assets", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn(tr("File"), ImGuiTableColumnFlags_WidthStretch, 3);
            ImGui::TableSetupColumn(tr("Kind"), ImGuiTableColumnFlags_WidthStretch, 1);
            ImGui::TableSetupColumn(tr("Size"), ImGuiTableColumnFlags_WidthStretch, 1);
            ImGui::TableSetupColumn(tr("State"), ImGuiTableColumnFlags_WidthStretch, 2);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 110);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            const auto icons = icon_pictures(app.project);
            for (auto& a : app.project.assets) {
                ImGui::PushID(a.uid.c_str());
                const std::string name = p2m::bare_name(a.file);
                const fs::path path = u8(app.asset_path(a));
                std::error_code ec;
                const bool exists = fs::exists(path, ec);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::Selectable(name.c_str(), sel_ == a.uid, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) sel_ = a.uid;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", u8str(path).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(asset_kind(name).c_str());
                ImGui::TableNextColumn();
                if (exists) ImGui::Text(tr("%.1f KB"), static_cast<double>(fs::file_size(path, ec)) / 1024.0);
                ImGui::TableNextColumn();
                const bool icon = std::find(icons.begin(), icons.end(), p2m::lower(name)) != icons.end();
                const std::string target = is_picture(name) ? picture_compiled_name(name) : name;
                if (!exists) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", tr("missing"));
                else if (icon) ImGui::TextDisabled(tr("-> icon %s"), project_icon_name(app.project.mod.id, name).c_str());
                else if (!is_raw_audio(name) && app.game && app.game->exists(target)) ImGui::TextColored(ImVec4(1, 0.75f, 0.3f, 1), tr("replaces the game's %s"), target.c_str());
                else if (is_raw_audio(name)) ImGui::TextDisabled("%s", tr("-> sounds/"));
                else ImGui::TextDisabled(tr("-> assets/%s"), target.c_str());
                ImGui::TableNextColumn();
                if (asset_kind(name) == "sound") {
                    ui::sound_button(app, name);
                    ImGui::SameLine();
                }
                if (ImGui::SmallButton(tr("Remove"))) remove = a.uid;
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();
        if (!remove.empty()) {
            auto& v = app.project.assets;
            v.erase(std::remove_if(v.begin(), v.end(), [&](const AssetFile& a) { return a.uid == remove; }), v.end());
            if (sel_ == remove) sel_.clear();
            app.touch();
        }

        draw_model_import(app);
        if (const AssetFile* a = selected(app)) draw_details(app, *a);
        else ImGui::TextDisabled("%s", tr("Add the files of your mod: compiled resources of the engine (made with the game's tools or risen.assets) and audio for voiced lines."));
        ImGui::End();
    }

private:
    // ---- the import of a 3D model ----
    struct ModelImport {
        bool want_open = false, open = false;
        std::string path, name, error, material;
        ModelSource source;
        ModelOptions options;
        std::vector<std::string> source_materials;
        std::map<std::string, std::string> mapped;
    } mi_;

    void begin_model_import(App& app) {
        if (app.project_path.empty()) {
            app.status(tr("Save the project first: the mesh is stored in its assets folder"));
            return;
        }
        const std::string dir = u8str(u8(app.project_path).parent_path());
        const std::string f = dialog_open_file("3D models", "obj,gltf,glb", dir);
        if (f.empty()) return;
        mi_ = ModelImport{};
        mi_.path = f;
        const fs::path p = u8(f);
        mi_.name = u8str(p.stem());
        for (char& c : mi_.name)
            if (!(std::isalnum(static_cast<unsigned char>(c)) && static_cast<unsigned char>(c) < 0x80) && c != '_') c = '_';
        try {
            const fs::path folder = p.parent_path();
            mi_.source = parse_model(read_text(p), u8str(p.filename()), [folder](const std::string& rel) {
                try {
                    return read_text(folder / u8(rel));
                } catch (const std::exception&) {
                    return std::string();
                }
            });
        } catch (const std::exception& e) {
            mi_.source.error = e.what();
        }
        const std::string ext = p2m::lower(u8str(p.extension()));
        mi_.options.scale = ext == ".obj" ? 1.0 : 100.0;  // glTF is in metres, the game in about centimetres
        mi_.options.mirror_z = true;
        for (auto& g : mi_.source.groups)
            if (std::find(mi_.source_materials.begin(), mi_.source_materials.end(), g.material) == mi_.source_materials.end()) mi_.source_materials.push_back(g.material);
        mi_.material = "ItWpn_SwordMisc_01_Diffuse_01._xmat";
        mi_.want_open = true;
    }

    void do_model_import(App& app) {
        ModelOptions o = mi_.options;
        o.material = mi_.material;
        o.materials = mi_.mapped;
        std::string error;
        const MeshBuild mesh = model_to_mesh(mi_.source, o, error);
        std::string file;
        if (error.empty()) file = build_xmsh(mesh, error);
        if (!error.empty()) {
            mi_.error = error;
            return;
        }
        const std::string rel = "assets/" + mi_.name + "._xmsh";
        const fs::path target = u8(app.project_path).parent_path() / u8(rel);
        try {
            write_text(target, file);
        } catch (const std::exception& e) {
            mi_.error = e.what();
            return;
        }
        const std::string bare = mi_.name + "._xmsh";
        bool known = false;
        for (auto& a : app.project.assets)
            if (p2m::lower(p2m::bare_name(a.file)) == p2m::lower(bare)) {
                known = true;
                a.file = rel;
                a.kind = "mesh";
                sel_ = a.uid;
            }
        if (!known) {
            AssetFile a{new_uid(), rel, "mesh"};
            app.project.assets.push_back(a);
            sel_ = a.uid;
        }
        app.touch();
        app.refresh_assets(true);
        info_.clear();
        size_t tris = 0;
        for (auto& p : mesh.parts) tris += p.indices.size() / 3;
        app.status(trf("Imported {0}: {1} part(s), {2} triangles", {bare, std::to_string(mesh.parts.size()), std::to_string(tris)}));
        mi_.open = false;
    }

    void draw_model_import(App& app) {
        if (mi_.want_open) {
            ImGui::OpenPopup(tr("Import 3D model"));
            mi_.want_open = false;
            mi_.open = true;
        }
        ImGui::SetNextWindowSize(ImVec2(640, 0), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal(tr("Import 3D model"), nullptr)) return;
        ImGui::TextWrapped("%s", mi_.path.c_str());
        if (!mi_.source.error.empty()) {
            ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", mi_.source.error.c_str());
            if (ImGui::Button(tr("Close"))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        ImGui::Text(tr("%zu triangles in %zu material group(s)"), mi_.source.triangles(), mi_.source.groups.size());
        for (auto& w : mi_.source.warnings) ImGui::TextColored(ImVec4(1, 0.8f, 0.35f, 1), "%s", w.c_str());
        ImGui::PushItemWidth(300);
        ui::input_text(tr("Name of the mesh"), mi_.name);
        ImGui::SameLine();
        ImGui::TextDisabled(tr("-> assets/%s._xmsh"), mi_.name.c_str());
        ImGui::InputDouble(tr("Scale"), &mi_.options.scale, 1, 10, "%g");
        ui::help(tr("The game's units are about centimetres: a human is 180 units high, so a glTF model in metres wants 100."));
        ImGui::Checkbox(tr("The model's up axis is +Z"), &mi_.options.z_up);
        ui::help(tr("Blender and CAD models are often +Z up; the game is +Y up."));
        ImGui::Checkbox(tr("Mirror Z (right-handed model)"), &mi_.options.mirror_z);
        ui::help(tr("OBJ and glTF are right-handed, the game is left-handed: z is negated (which also turns the front faces the way the game wants them). Untick if the model looks mirrored."));
        ImGui::PopItemWidth();
        ImGui::SeparatorText(tr("Materials"));
        ImGui::TextDisabled("%s", tr("A mesh names game materials (._xmat); the textures come from them. Pick what each material of the model should become."));
        ui::asset_picker(tr("Default material"), mi_.material, app.game.get(), "_xmat", false, 420);
        if (mi_.source_materials.size() > 1 || (mi_.source_materials.size() == 1 && !mi_.source_materials[0].empty())) {
            for (auto& m : mi_.source_materials) {
                ImGui::PushID(m.c_str());
                std::string v = mi_.mapped[m];
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%s", m.empty() ? tr("(no material)") : m.c_str());
                ImGui::SameLine(200);
                if (ui::asset_picker("##map", v, app.game.get(), "_xmat", false, 420)) mi_.mapped[m] = v;
                ImGui::PopID();
            }
            ImGui::TextDisabled("%s", tr("An empty field uses the default material."));
        }
        if (!mi_.error.empty()) ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", mi_.error.c_str());
        const bool ok = !mi_.name.empty() && !mi_.material.empty();
        ImGui::BeginDisabled(!ok);
        if (ImGui::Button(tr("Import"), ImVec2(120, 0))) do_model_import(app);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(tr("Cancel"), ImVec2(100, 0)) || !mi_.open) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    std::string sel_;
    // what the details show of a file (read once per selection)
    struct Info {
        std::string path;
        std::string text;   // a line about the content
        std::string error;
        bool sound = false;
        Picture picture;
    };
    GLuint picture_tex_ = 0;
    std::string picture_of_;  // the uid the texture shows
    std::map<std::string, Info> info_;
    MeshPreview preview_;

    const AssetFile* selected(const App& app) const {
        for (auto& a : app.project.assets)
            if (a.uid == sel_) return &a;
        return nullptr;
    }

    const Info& info_of(App& app, const AssetFile& a) {
        const std::string path = app.asset_path(a);
        auto it = info_.find(a.uid);
        if (it != info_.end() && it->second.path == path) return it->second;
        Info in;
        in.path = path;
        const std::string name = p2m::bare_name(a.file);
        try {
            if (asset_kind(name) == "sound") {
                in.sound = true;
                std::string why;
                if (auto s = open_sound(read_text(u8(path)), why)) {
                    char buf[160];
                    std::snprintf(buf, sizeof buf, tr("%s%s, %d channel(s), %d Hz, %.2f s"), s->compiled ? tr("compiled sound, ") : "", s->format.c_str(), s->channels,
                                  s->sample_rate, static_cast<double>(s->duration_ms) / 1000.0);
                    in.text = buf;
                } else {
                    in.error = why;
                }
            } else if (asset_kind(name) == "mesh") {
                const MeshData m = decode_mesh(read_text(u8(path)));
                size_t tris = 0;
                for (auto& s : m.submeshes) tris += s.index_count / 3;
                std::string mats;
                for (auto& s : m.submeshes) mats += (mats.empty() ? "" : ", ") + s.material;
                in.text = trf("{0} sub-mesh(es), {1} triangles; materials: {2}", {std::to_string(m.submeshes.size()), std::to_string(tris), mats});
            } else if (asset_kind(name) == "material") {
                const std::string tex = material_texture(read_text(u8(path)));
                in.text = tex.empty() ? std::string(tr("no texture found")) : trf("texture: {0}", {tex});
            } else if (asset_kind(name) == "image") {
                const ImageData img = decode_image(read_text(u8(path)));
                in.text = std::to_string(img.width) + " x " + std::to_string(img.height);
            } else if (asset_kind(name) == "picture") {
                std::string why;
                if (auto pic = load_picture(read_text(u8(path)), why)) {
                    in.text = std::to_string(pic->width) + " x " + std::to_string(pic->height) + (pic->has_alpha() ? tr(", with alpha") : "");
                    in.picture = std::move(*pic);
                } else {
                    in.error = why;
                }
            }
        } catch (const std::exception& e) {
            in.error = e.what();
        }
        return info_[a.uid] = in;
    }

    void draw_details(App& app, const AssetFile& a) {
        const std::string name = p2m::bare_name(a.file);
        ImGui::SeparatorText(name.c_str());
        ImGui::TextDisabled("%s", a.file.c_str());
        const Info& in = info_of(app, a);
        if (!in.error.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", in.error.c_str());
        if (!in.text.empty()) ImGui::TextWrapped("%s", in.text.c_str());
        const std::string kind = asset_kind(name);
        if (kind == "sound") {
            ui::sound_button(app, name, "##detail");
            if (app.audio().current() == name) {
                ImGui::SameLine();
                const float len = app.audio().length();
                ImGui::ProgressBar(len > 0 ? app.audio().position() / len : 0, ImVec2(240, 0));
            }
            ImGui::TextDisabled(is_raw_audio(name) ? tr("Use it as the Sound of a dialogue line (Dialogues panel).")
                                                   : tr("A compiled sound: SoundName of a SaySound command (as %s), or it replaces a game sound of the same name."));
            if (!is_raw_audio(name)) {
                ImGui::SameLine(0, 0);
                ImGui::TextDisabled(" %s", sound_raw_name(name).c_str());
            }
        } else if (kind == "mesh") {
            if (app.game) preview_.draw(app, name, ImVec2(ImGui::GetContentRegionAvail().x, std::max(160.0f, ImGui::GetContentRegionAvail().y)));
            else ImGui::TextDisabled("%s", tr("load the game data to see the mesh (its materials and textures may come from the game)"));
        } else if (kind == "picture") {
            const auto icons = icon_pictures(app.project);
            const bool icon = std::find(icons.begin(), icons.end(), p2m::lower(name)) != icons.end();
            if (icon) ImGui::TextDisabled(tr("An item icon: exported into the icon atlas of the mod as %s."), project_icon_name(app.project.mod.id, name).c_str());
            else ImGui::TextDisabled(tr("Exported as the texture %s (DXT, with mip maps). Choose it as an item's icon to make it an icon instead."),
                                     picture_compiled_name(name).c_str());
            if (!in.picture.rgba.empty()) {
                if (picture_of_ != a.uid) {
                    if (!picture_tex_) glGenTextures(1, &picture_tex_);
                    glBindTexture(GL_TEXTURE_2D, picture_tex_);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, in.picture.width, in.picture.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, in.picture.rgba.data());
                    picture_of_ = a.uid;
                }
                const float avail = std::min(256.0f, ImGui::GetContentRegionAvail().x);
                const float sc = avail / static_cast<float>(std::max(in.picture.width, in.picture.height));
                ImGui::Image(static_cast<ImTextureID>(picture_tex_), ImVec2(in.picture.width * sc, in.picture.height * sc));
            }
        } else if (kind == "image") {
            if (gfx::ResourceCache* res = app.resources()) {
                const GLuint t = res->texture(name);
                const float w = std::min(256.0f, ImGui::GetContentRegionAvail().x);
                ImGui::Image(static_cast<ImTextureID>(t), ImVec2(w, w));
            }
        }
    }
};

}  // namespace

std::unique_ptr<Panel> make_assets_panel() { return std::make_unique<AssetsPanel>(); }

}  // namespace mk
