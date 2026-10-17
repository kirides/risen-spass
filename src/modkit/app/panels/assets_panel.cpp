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
#include "core/mesh.h"
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
                app.status(u8str(p.filename()) + " is already an asset of the project");
                continue;
            }
            AssetFile a{new_uid(), stored, asset_kind(u8str(p.filename()))};
            app.project.assets.push_back(a);
            sel_ = a.uid;
            ++added;
        }
        if (added) {
            app.touch();
            app.status(std::to_string(added) + " asset file(s) added");
        }
    }

    void draw(App& app) override {
        if (!ImGui::Begin(title(), &open)) {
            ImGui::End();
            return;
        }
        if (ImGui::Button("Add files...")) {
            const std::string dir = app.project_path.empty() ? std::string() : u8str(u8(app.project_path).parent_path());
            add_files(app, dialog_open_files("Resources and audio", "_xmsh,_xmat,_ximg,_xmac,_xmot,_xsnd,ogg,wav,mp3", dir));
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload")) {
            app.refresh_assets(true);
            info_.clear();
        }
        ui::help("Reads the files again (after you changed them outside the editor).");
        ImGui::SameLine();
        ImGui::TextDisabled("%zu files", app.project.assets.size());
        if (app.project_path.empty())
            ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "Save the project first to store the files relative to the project folder.");

        ImGui::BeginChild("##list", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.45f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
        std::string remove;
        if (ImGui::BeginTable("##assets", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch, 3);
            ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthStretch, 1);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch, 1);
            ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthStretch, 2);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 110);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
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
                if (exists) ImGui::Text("%.1f KB", static_cast<double>(fs::file_size(path, ec)) / 1024.0);
                ImGui::TableNextColumn();
                if (!exists) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "missing");
                else if (!is_raw_audio(name) && app.game && app.game->exists(name)) ImGui::TextColored(ImVec4(1, 0.75f, 0.3f, 1), "replaces the game's file");
                else if (is_raw_audio(name)) ImGui::TextDisabled("-> sounds/");
                else ImGui::TextDisabled("-> assets/");
                ImGui::TableNextColumn();
                if (asset_kind(name) == "sound") {
                    ui::sound_button(app, name);
                    ImGui::SameLine();
                }
                if (ImGui::SmallButton("Remove")) remove = a.uid;
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

        if (const AssetFile* a = selected(app)) draw_details(app, *a);
        else ImGui::TextDisabled("Add the files of your mod: compiled resources of the engine (made with the game's tools or risen.assets) and audio for voiced lines.");
        ImGui::End();
    }

private:
    std::string sel_;
    // what the details show of a file (read once per selection)
    struct Info {
        std::string path;
        std::string text;   // a line about the content
        std::string error;
        bool sound = false;
    };
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
                    std::snprintf(buf, sizeof buf, "%s%s, %d channel(s), %d Hz, %.2f s", s->compiled ? "compiled sound, " : "", s->format.c_str(), s->channels,
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
                in.text = std::to_string(m.submeshes.size()) + " sub-mesh(es), " + std::to_string(tris) + " triangles; materials: " + mats;
            } else if (asset_kind(name) == "material") {
                const std::string tex = material_texture(read_text(u8(path)));
                in.text = tex.empty() ? std::string("no texture found") : "texture: " + tex;
            } else if (asset_kind(name) == "image") {
                const ImageData img = decode_image(read_text(u8(path)));
                in.text = std::to_string(img.width) + " x " + std::to_string(img.height);
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
            ImGui::TextDisabled(is_raw_audio(name) ? "Use it as the Sound of a dialogue line (Dialogues panel)."
                                                   : "A compiled sound: SoundName of a SaySound command (as %s), or it replaces a game sound of the same name.");
            if (!is_raw_audio(name)) {
                ImGui::SameLine(0, 0);
                ImGui::TextDisabled(" %s", sound_raw_name(name).c_str());
            }
        } else if (kind == "mesh") {
            if (app.game) preview_.draw(app, name, ImVec2(ImGui::GetContentRegionAvail().x, std::max(160.0f, ImGui::GetContentRegionAvail().y)));
            else ImGui::TextDisabled("load the game data to see the mesh (its materials and textures may come from the game)");
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
