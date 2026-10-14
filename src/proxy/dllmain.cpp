// Entry point of the proxy DLL (installed as bin/sentry.dll, see CMakeLists.txt).
//
// DllMain runs under the loader lock, so it only does what is safe there: resolve exports of
// already-mapped modules, create MinHook trampolines. Everything heavier (LuaJIT, mod loading)
// is deferred to the first game-thread callback (see game/hooks.cpp).

#include <windows.h>

#include <spdlog/spdlog.h>

#include "bootstrap.h"

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID /*reserved*/) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(module);
            spass::onProcessAttach(module);
            break;
        case DLL_PROCESS_DETACH:
            spass::onProcessDetach();
            break;
        default:
            break;
    }
    return TRUE;
}
