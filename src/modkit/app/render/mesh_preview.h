// A small turning 3D preview of a game mesh for the editors (an item's mesh, a template's mesh in the Browser).
#pragma once

#include <imgui.h>

#include <string>

#include "app/render/gl_util.h"
#include "app/render/mesh_renderer.h"

namespace mk {

class App;

class MeshPreview {
public:
    // draws the preview (or a note while loading / when the mesh cannot be read); `mesh` is a raw or compiled name ("X.xmsh" / "X._xmsh")
    void draw(App& app, const std::string& mesh, ImVec2 size);

private:
    gfx::RenderTarget rt_;
    gfx::MeshRenderer renderer_;
    float angle_ = 0.6f;
};

}  // namespace mk
