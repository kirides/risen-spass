#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

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

// Native code behind a registry entry: runs instead of the script (`original` is what was registered before, nullptr for a new script; it may be
// called to continue with the game's own logic). Used by the AI bridge (lua_ai.cpp) for the perception hooks and the exit script of Lua AI states.
using NativeScript = std::function<std::int64_t(void* unit, void* a, void* b, std::int64_t arg, std::int64_t (*original)(void* unit, void* a, void* b, std::int64_t arg))>;

// Defines a new script (isOverride = false: refused when the name exists) or intercepts an existing one with native code. Returns an error
// message, or an empty string on success. Installing again on the same name swaps the function.
std::string installNative(const std::string& name, NativeScript fn, bool isOverride);

// Whether the registry has a script of that name with a function (the same test as risen.script.exists).
bool scriptExists(const std::string& name);

// What intercepts the registry entry `name` right now: "native" (installNative), "lua" (risen.script.define / override) or an empty string (nothing of ours).
std::string hookKind(const std::string& name);

// Registers risen.script.* (define, override, exists, call) with the Lua state.
void bindScripts(sol::state_view lua, sol::table risen);

// Puts the original function pointers back (used on shutdown/reload).
void restoreAll();

// Hot reload: undoes what the mod registered with risen.script.override (the game's script is put back) and silences what it defined with
// risen.script.define (the registry entry stays, because infos may still name it, and returns 0 until it is defined again). Appends a line per script to `released`.
void releaseOwner(const std::string& mod, std::vector<std::string>& released);

}  // namespace spass::game::scripts
