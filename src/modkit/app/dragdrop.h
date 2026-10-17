// Drag and drop of templates and project objects between the panels: the Browser (and the lists of the Items / Recipes / NPCs panels) are sources,
// template fields, inventory / loot / ingredient lists, the item group's name list and the World view are targets.
#pragma once

#include <initializer_list>
#include <string>
#include <vector>

#include "core/gamedata.h"

namespace mk::ui {

inline constexpr const char* kDragTemplate = "MK_TEMPLATE";

struct DragTemplate {
    char name[128] = {};
    TplKind kind = TplKind::Other;
    bool entity = false;   // a placed NPC of the project: its unique entity name, not a template name
    bool project = false;  // made by the project (a new item, recipe, NPC template or a placed NPC)
};

// makes the last item a drag source of `name` (call right after the item; the tooltip shows the name and `label`)
void drag_source(const std::string& name, TplKind kind, bool entity = false, bool project = false, const std::string& label = {});
// the payload being dragged (nullptr when none)
const DragTemplate* dragging();
// makes the last item a drop target for templates of `kinds` (empty = every kind) and for names in `extra` (project names, placed NPCs);
// returns the payload when one was dropped on it this frame, else nullptr. An entity payload is only accepted when its name is in `extra`.
const DragTemplate* drop_target(std::initializer_list<TplKind> kinds, const std::vector<std::string>& extra = {});
const DragTemplate* drop_target(const std::vector<TplKind>& kinds, const std::vector<std::string>& extra = {});
// a drop target for the unique names of placed NPCs only (dialogue owners)
const DragTemplate* drop_entity();

}  // namespace mk::ui
