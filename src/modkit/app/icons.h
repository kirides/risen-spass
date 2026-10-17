// Icon previews: the shipped GUI images (core/imagelist.h) drawn from their texture atlas wherever items are listed.
#pragma once

#include <string>

#include "core/gamedata.h"
#include "core/project.h"

namespace mk {

namespace gfx {
class ResourceCache;
}

namespace ui {

// the app sets the source every frame (nullptr without game data); the project's items may carry their own icon
void set_icon_source(gfx::ResourceCache* res, const GameData* game, const Project* project);

// draws the shipped image `image_name` ("IC_It_Apple") as a square of `size` pixels; always takes the space, so rows line up. Returns false (and
// draws a blank) for an unknown name or while the texture loads. The image's own proportions are kept inside the square.
bool icon(const std::string& image_name, float size = 20.0f);
// the icon name of an item: its project definition (own icon, else its base's), else the shipped template's; empty when none
std::string item_icon_name(const std::string& item);
// the icon of an item (see item_icon_name)
bool item_icon(const std::string& item, float size = 20.0f);

}  // namespace ui
}  // namespace mk
