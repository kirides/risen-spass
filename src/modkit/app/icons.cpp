#include "app/icons.h"

#include <imgui.h>

#include <algorithm>

#include "app/render/resources.h"

namespace mk::ui {

namespace {
gfx::ResourceCache* g_res = nullptr;
const GameData* g_game = nullptr;
const Project* g_project = nullptr;
}  // namespace

void set_icon_source(gfx::ResourceCache* res, const GameData* game, const Project* project) {
    g_res = res;
    g_game = game;
    g_project = project;
}

bool icon(const std::string& image_name, float size) {
    const ImVec2 sz(size, size);
    const ImageEntry* e = g_game && !image_name.empty() ? g_game->images().find(image_name) : nullptr;
    if (!e || !g_res || e->width() <= 0 || e->height() <= 0) {
        ImGui::Dummy(sz);
        return false;
    }
    const std::string tex = e->texture_file();
    const GLuint t = g_res->texture(tex);
    int tw = 0, th = 0;
    if (!g_res->texture_size(tex, tw, th)) {
        ImGui::Dummy(sz);  // still loading
        return false;
    }
    // half a texel in: the mip levels would bleed the neighbouring icon in otherwise
    const float hx = 0.5f / static_cast<float>(tw), hy = 0.5f / static_cast<float>(th);
    const ImVec2 uv0(static_cast<float>(e->left) / static_cast<float>(tw) + hx, static_cast<float>(e->top) / static_cast<float>(th) + hy);
    const ImVec2 uv1(static_cast<float>(e->right) / static_cast<float>(tw) - hx, static_cast<float>(e->bottom) / static_cast<float>(th) - hy);
    const float aspect = static_cast<float>(e->width()) / static_cast<float>(e->height());
    ImVec2 draw = sz;
    if (aspect > 1) draw.y = size / aspect;
    else draw.x = size * aspect;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(sz);
    ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(t)), ImVec2(origin.x + (size - draw.x) * 0.5f, origin.y + (size - draw.y) * 0.5f),
                                         ImVec2(origin.x + (size + draw.x) * 0.5f, origin.y + (size + draw.y) * 0.5f), uv0, uv1);
    return true;
}

std::string item_icon_name(const std::string& item) {
    const Project* project = g_project;
    const GameData* game = g_game;
    std::string base = item;
    if (project) {
        for (int depth = 0; depth < 8; ++depth) {
            const ItemDef* def = nullptr;
            for (auto& i : project->items)
                if (i.name == base) def = &i;
            if (!def) break;
            if (def->icon && !def->icon->empty()) return *def->icon;
            if (def->mode == Mode::Edit || def->base.empty()) break;
            base = def->base;
        }
    }
    if (game)
        if (const TemplateInfo* t = game->tpl(base)) return t->icon;
    return {};
}

bool item_icon(const std::string& item, float size) { return icon(item_icon_name(item), size); }

}  // namespace mk::ui
