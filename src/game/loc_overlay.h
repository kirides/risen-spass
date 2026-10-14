#pragma once

#include <string>

#include <sol/sol.hpp>

namespace spass::game {

// Localization overlay (docs/RE.md §5). Every localized string the game reads goes through
// CFXTableMap::LookupValue (FFC.dll); the hooks installed here answer from a mod-supplied overlay first
// and fall back to the real tables. The tables themselves are never modified.
bool installLocHooks();

// Adds (or replaces) an overlay string. `language` empty = every language. `text` and `id` are UTF-8.
bool setOverlayString(const std::string& id, const std::string& text, const std::string& language);

// Registers risen.loc.* (set, get, exists, language, trace).
void bindLoc(sol::state_view lua, sol::table risen);

}  // namespace spass::game
