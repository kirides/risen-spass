#pragma once

namespace spass::game {

// Registers the native `risen.*` API tables (quest, gamevar, ...) with the Lua runtime.
// Call before Runtime::start().
void registerBindings();

}  // namespace spass::game
