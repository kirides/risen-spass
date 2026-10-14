#include "bootstrap.h"

#include <atomic>
#include <mutex>

#include <spdlog/spdlog.h>

#include "common/log.h"
#include "common/paths.h"
#include "config.h"
#include "game/bindings.h"
#include "game/hooks.h"
#include "game/symbols.h"
#include "hook/hook_manager.h"
#include "lua/runtime.h"

namespace spass {
namespace {

Config g_config;
std::atomic<bool> g_active{false};
std::once_flag g_readyFlag;

spdlog::level::level_enum parseLevel(const std::string& s) {
    if (s == "trace") return spdlog::level::trace;
    if (s == "debug") return spdlog::level::debug;
    if (s == "warn") return spdlog::level::warn;
    if (s == "error") return spdlog::level::err;
    return spdlog::level::info;
}

}  // namespace

void onProcessAttach(HMODULE self) {
    paths::init(self);
    log::init(paths::dataDir() / "spass.log");
    g_config = loadConfig(paths::dataDir() / "config.toml");
    spdlog::set_level(parseLevel(g_config.logLevel));
    spdlog::info("risen-spass {} loaded in {}", SPASS_VERSION, paths::binDir().string());

    if (!g_config.enabled) {
        spdlog::warn("disabled by config.toml (enabled = false)");
        return;
    }
    if (!hooks::initialize()) {
        return;
    }

    const auto missing = game::resolveSymbols();
    if (!missing.empty()) {
        std::string list;
        for (const auto& m : missing) {
            list += m + " ";
        }
        spdlog::error("required engine symbols missing ({}): wrong game version? Mod layer disabled.", list);
        return;
    }

    game::registerBindings();
    if (!game::installHooks() || !hooks::enableAll()) {
        spdlog::error("hook installation failed, mod layer disabled");
        return;
    }
    g_active = true;
    spdlog::info("hooks active, waiting for the script system to come up");
}

void onProcessDetach() {
    if (g_active) {
        lua::Runtime::instance().stop();
        hooks::shutdown();
    }
    log::shutdown();
}

void onGameReady() {
    std::call_once(g_readyFlag, [] {
        spdlog::info("game ready: starting Lua runtime");
        lua::Runtime::instance().start(paths::modsDir(), g_config.unsafeLua);
    });
}

}  // namespace spass
