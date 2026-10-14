#pragma once

#include <windows.h>

namespace spass {

// Called from DllMain (loader lock held!).
void onProcessAttach(HMODULE self);
void onProcessDetach();

// Called once from the game thread when the engine's script system is ready. Starts LuaJIT and
// loads the mods. Idempotent.
void onGameReady();

}  // namespace spass
