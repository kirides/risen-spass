#include "game/symbols.h"

#include <windows.h>

#include "hook/exports.h"

namespace spass::game {
namespace {
Symbols g_symbols;
}

const Symbols& sym() { return g_symbols; }

// Script.dll is not statically imported by the exe: it is loaded when Script_Game.dll is loaded, which
// happens inside gCScriptAdmin::PostInitialize. Therefore resolveSymbols() runs twice (at DllMain and
// again after the script DLLs are up); each call only resolves symbols of modules that are loaded by now.
std::vector<std::string> resolveSymbols() {
    std::vector<std::string> missing;
#define SPASS_RESOLVE(field, module, name, type, required)                                   \
    if (g_symbols.field == nullptr && GetModuleHandleW(module) != nullptr) {                 \
        g_symbols.field = exports::resolveAs<type>(module, name);                            \
        if (g_symbols.field == nullptr && (required)) {                                      \
            missing.emplace_back(#field);                                                    \
        }                                                                                    \
    }
    SPASS_SYMBOLS(SPASS_RESOLVE)
#undef SPASS_RESOLVE
    return missing;
}

}  // namespace spass::game
