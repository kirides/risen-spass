#pragma once

#include <sol/sol.hpp>

namespace spass::game {

// Localization overlay (docs/RE.md §5). Every localized string the game reads goes through
// CFXTableMap::LookupValue (FFC.dll); the hooks installed here answer from a mod-supplied overlay first
// and fall back to the real tables. The tables themselves are never modified.
bool installLocHooks();

// Registers risen.loc.* (set, get, exists, language, trace).
void bindLoc(sol::state_view lua, sol::table risen);

}  // namespace spass::game
