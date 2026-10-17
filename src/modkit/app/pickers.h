// Pickers: combo boxes with a filter over the game's templates / resources plus the project's own objects.
#pragma once

#include <initializer_list>
#include <string>
#include <vector>

#include "app/app.h"

namespace mk::ui {

// a template name; `kinds` restricts the game templates listed (empty = all); `extra` are project names listed first (e.g. new items).
// Free text is accepted too (a name the catalogue does not know is shown in orange).
bool template_picker(const char* label, std::string& name, const GameData* game, std::initializer_list<TplKind> kinds,
                     const std::vector<std::string>& extra = {}, float width = 0);
// a resource name by compiled extension ("_xmsh"); `strip_compiled` turns "X._xmsh" into "X.xmsh" (the raw name templates use)
bool asset_picker(const char* label, std::string& name, const GameData* game, const char* ext, bool strip_compiled, float width = 0);
// a label for a template name: the project's label, else the game's English label, else ""
std::string label_of(const App& app, const std::string& name);

}  // namespace mk::ui
