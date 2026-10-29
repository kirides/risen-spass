// The material editor of the World view's inspector (docs/editor/REQUIREMENTS.md WLD-10, docs/RE.md section 60): for the selected entity's mesh it lists the
// sub-meshes' materials and edits the selected one: the image behind each shader slot (diffuse, normal, specular ...) and the shader flags. Changes go into the
// project's `materials` section (core/material.h MaterialEdit) and show in the 3D view at once.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "core/material.h"
#include "core/mesh_edit.h"

namespace mk {

class App;

class MaterialInspector {
public:
    // inside the inspector: the materials of `mesh` (compiled name "X._xmsh") and the editor of the selected one
    void draw(App& app, const std::string& mesh);
    // every frame: tells the 3D view which diffuse images the project's changes use
    void sync(App& app);
    // a material to edit (from outside, e.g. a list of changed materials)
    void select(const std::string& material) { selected_ = material; }

private:
    struct Original {
        bool ok = false;
        Material m;
        std::string error;
    };
    const Original& original(App& app, const std::string& material);
    const std::vector<std::string>& materials_of(App& app, const std::string& mesh);
    const std::vector<std::string>& images(App& app);
    void draw_material(App& app, const std::string& material);
    bool pick_image(App& app, const char* id, std::string* picked);
    bool pick_name(const char* id, const std::vector<std::string>& all, std::string* picked, const char* suffix);
    const std::vector<std::string>& material_names(App& app);
    const MeshInfo* info_of(App& app, const std::string& mesh);
    void draw_mesh(App& app, const std::string& mesh);

    std::string selected_, filter_;
    std::map<std::string, Original> originals_;  // by lower-case name
    std::map<std::string, std::vector<std::string>> mesh_materials_;
    std::map<std::string, std::pair<bool, MeshInfo>> mesh_infos_;
    std::vector<std::string> material_names_;
    std::vector<std::string> images_;  // stems of the game's and the project's images
    size_t images_assets_ = SIZE_MAX;
};

}  // namespace mk
