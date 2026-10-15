#pragma once

#include <sol/sol.hpp>

namespace spass::game {

// risen.input.* (lua_input.cpp) and the hook of the key script that raises the `input.action` event.
void bindInput(sol::state_view lua, sol::table risen);
void installInputHooks();  // Game.dll only; installed with the other hooks
void onInputFrame();       // once per frame (gCSession::Process): samples the keys when a mod asked for input

}  // namespace spass::game
