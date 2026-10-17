#include "app/render/mesh_preview.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

#include "app/app.h"
#include "app/render/resources.h"
#include "core/catalog.h"

namespace mk {

void MeshPreview::draw(App& app, const std::string& mesh, ImVec2 size) {
    gfx::ResourceCache* res = app.resources();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##preview", size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(26, 28, 33, 255), 4);
    if (!res || mesh.empty()) return;
    const std::string name = compiled_name(p2m::bare_name(mesh));
    const gfx::GpuMesh* m = res->mesh(name);
    res->update(3.0);
    if (!m) {
        dl->AddText(ImVec2(origin.x + 8, origin.y + 8), IM_COL32(150, 150, 150, 255), res->failed(name) ? "cannot read this mesh" : "loading...");
        return;
    }
    if (ImGui::IsItemActive()) angle_ += ImGui::GetIO().MouseDelta.x * 0.01f;
    else angle_ += ImGui::GetIO().DeltaTime * 0.6f;
    const int w = static_cast<int>(size.x), h = static_cast<int>(size.y);
    rt_.resize(w, h);
    // frame the mesh's box from the front, turning around its centre
    const glm::vec3 mn = m->box_min, mx = m->box_max;
    const glm::vec3 c = (mn + mx) * 0.5f;
    const glm::vec3 ext = mx - mn;
    const float r = std::max(1.0f, std::max({ext.x, ext.y, ext.z}) * 0.5f);  // the largest extent fills the view (long weapons)
    const glm::mat4 model = glm::rotate(glm::mat4(1), angle_, glm::vec3(0, 1, 0)) * glm::scale(glm::mat4(1), glm::vec3(1, 1, -1)) *
                            glm::translate(glm::mat4(1), -c);
    const glm::mat4 view = glm::lookAt(glm::vec3(0, r * 0.4f, r * 2.2f), glm::vec3(0), glm::vec3(0, 1, 0));
    const glm::mat4 proj = glm::perspective(glm::radians(40.0f), size.x / std::max(1.0f, size.y), r * 0.05f, r * 10.0f);
    rt_.bind();
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.10f, 0.11f, 0.13f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    renderer_.begin(proj * view, glm::vec3(0.4f, -0.8f, -0.5f));
    renderer_.draw(*m, model, *res);
    renderer_.end();
    glDisable(GL_DEPTH_TEST);
    rt_.unbind();
    dl->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(rt_.texture())), origin, ImVec2(origin.x + size.x, origin.y + size.y), ImVec2(0, 1), ImVec2(1, 0));
    char buf[64];
    std::snprintf(buf, sizeof buf, "%zu triangles", m->triangles);
    dl->AddText(ImVec2(origin.x + 6, origin.y + size.y - 20), IM_COL32(170, 170, 170, 255), buf);
}

}  // namespace mk
