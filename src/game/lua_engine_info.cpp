#include <windows.h>

#include <string>

#include <spdlog/spdlog.h>

#include "game/bindings.h"
#include "game/symbols.h"
#include "hook/exports.h"

// risen.engine: what the Lua layer found in the running game (docs/LUA_API.md). Lets a mod -- or a bug report --
// tell which engine functions were bound, and which build of the game it is running on.
//   risen.engine.symbols()  -> { symbol_name = true|false, ... }   every engine function the layer knows about
//   risen.engine.missing()  -> { "symbol_name", ... }               the ones that could not be resolved
//   risen.engine.build()    -> { ["Game.dll"] = { timestamp = <PE link time>, base = <hex> }, ... }

namespace spass::game {

void bindEngineInfo(sol::state_view lua, sol::table risen) {
    sol::table engine = lua.create_table();

    engine.set_function("symbols", [](sol::this_state ts) {
        sol::state_view L(ts);
        sol::table t = L.create_table();
#define SPASS_REPORT(field, module, name, type, required) t[#field] = (sym().field != nullptr);
        SPASS_SYMBOLS(SPASS_REPORT)
#undef SPASS_REPORT
        return t;
    });

    engine.set_function("missing", [](sol::this_state ts) {
        sol::state_view L(ts);
        sol::table t = L.create_table();
        int i = 1;
#define SPASS_REPORT(field, module, name, type, required) \
    if (sym().field == nullptr) {                         \
        t[i++] = std::string(#field) + " (" + (required ? "required" : "optional") + ")"; \
    }
        SPASS_SYMBOLS(SPASS_REPORT)
#undef SPASS_REPORT
        return t;
    });

    engine.set_function("build", [](sol::this_state ts) {
        sol::state_view L(ts);
        sol::table t = L.create_table();
        for (const wchar_t* module : {L"Risen.exe", L"Game.dll", L"Engine.dll", L"Script.dll", L"FFC.dll", L"SharedBase.dll"}) {
            HMODULE h = GetModuleHandleW(module);
            if (h == nullptr) {
                continue;
            }
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(h);
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const char*>(h) + dos->e_lfanew);
            sol::table m = L.create_table();
            m["timestamp"] = static_cast<double>(nt->FileHeader.TimeDateStamp);
            m["size"] = static_cast<double>(nt->OptionalHeader.SizeOfImage);
            char base[32];
            std::snprintf(base, sizeof base, "0x%llx", static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(h)));
            m["base"] = std::string(base);
            std::string name(module, module + wcslen(module));
            t[name] = m;
        }
        return t;
    });

    risen["engine"] = engine;
}

}  // namespace spass::game
