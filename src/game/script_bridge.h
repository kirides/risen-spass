#pragma once

#include <cstdint>
#include <string>

#include <sol/sol.hpp>

namespace spass::game::scripts {

// Remembers the live gCScriptAdmin (captured from the PostInitialize hook); needed for risen.script.call.
void setAdmin(void* admin);

// Verifies the assumed eSScriptBase layout (function pointer at +0x20) against Script_Game.dll's real
// exports. Must run after the script DLLs were loaded. Returns false (and logs why) if the layout does not
// match, in which case define()/override() refuse to touch the registry.
bool selfTest();

// True while a script started with risen.script.call runs (the hooks use it to tell a scripted kill from a combat death).
bool inLuaScriptCall();

// Registers risen.script.* (define, override, exists, call) with the Lua state.
void bindScripts(sol::state_view lua, sol::table risen);

// Puts the original function pointers back (used on shutdown/reload).
void restoreAll();

}  // namespace spass::game::scripts
