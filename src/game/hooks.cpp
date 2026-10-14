#include "game/hooks.h"

#include <string>

#include <spdlog/spdlog.h>

#include "bootstrap.h"
#include "game/loc_overlay.h"
#include "game/script_bridge.h"
#include "game/symbols.h"
#include "hook/hook_manager.h"
#include "lua/runtime.h"

namespace spass::game {
namespace {

Hook<int (*)(void*)> g_scriptAdminPostInit;
Hook<void (*)(void*)> g_sessionProcess;

// gCScriptAdmin::PostInitialize(this) -> bEResult  (docs/RE.md §4.2)
// The original calls LoadAllScriptGame, i.e. every Script_*.dll is loaded and registered when it returns.
// That is the earliest moment where Lua can define new scripts and override existing ones.
int detourScriptAdminPostInit(void* self) {
    const int result = g_scriptAdminPostInit.original(self);
    spdlog::info("gCScriptAdmin::PostInitialize returned {}", result);

    // Script.dll (quest/entity API) was pulled in by Script_Game.dll just now.
    const auto missing = resolveSymbols();
    if (!missing.empty()) {
        std::string list;
        for (const auto& m : missing) {
            list += m + " ";
        }
        spdlog::error("engine symbols missing after script DLL load ({}): Lua layer not started", list);
        return result;
    }
    scripts::setAdmin(self);
    scripts::selfTest();
    onGameReady();
    return result;
}

// gCSession::Process(this): once per frame while a session exists. Emits the Lua "tick" event.
void detourSessionProcess(void* self) {
    g_sessionProcess.original(self);
    auto& rt = lua::Runtime::instance();
    if (rt.running() && rt.hasHandlers("tick")) {
        rt.emit("tick");
    }
}

}  // namespace

bool installHooks() {
    const Symbols& s = sym();
    bool ok = true;
    ok &= g_scriptAdminPostInit.install("gCScriptAdmin::PostInitialize", s.script_admin_post_init,
                                        &detourScriptAdminPostInit);
    ok &= g_sessionProcess.install("gCSession::Process", s.session_process, &detourSessionProcess);
    ok &= installLocHooks();
    return ok;
}

}  // namespace spass::game
