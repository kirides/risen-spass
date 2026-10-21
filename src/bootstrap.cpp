#include "bootstrap.h"

#include <atomic>
#include <mutex>

#include <spdlog/spdlog.h>

#include "common/crash.h"
#include "common/log.h"
#include "common/paths.h"
#include "common/trace.h"
#include "config.h"
#include "game/background_window.h"
#include "game/bindings.h"
#include "game/events.h"
#include "game/folder_redirect.h"
#include "game/hooks.h"
#include "game/instance_mutex.h"
#include "game/lua_test.h"
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
    if (g_config.crashHandler) {
        crash::Options co;
        co.dump = crash::parseDumpKind(g_config.crashDump);
        co.dumpDir = paths::dataDir() / "crash";
        co.logFile = paths::dataDir() / "spass.log";
        crash::install(co);
        spdlog::info("crash handler installed (minidump: {})", g_config.crashDump);
    }

    if (!g_config.trace.empty() || g_config.traceFile) {
        trace::init(g_config.trace, g_config.traceFile ? paths::dataDir() / "trace.log" : std::filesystem::path());
        spdlog::info("trace channels enabled: {}", trace::enabledList().empty() ? "(none)" : trace::enabledList());
    }
    {
        game::test::Settings ts;
        ts.active = g_config.testEnabled || g_config.unsafeLua;
        ts.exitOnFinish = g_config.testEnabled && g_config.testExit;
        ts.deadlineSeconds = g_config.testDeadline;
        ts.scenario = g_config.testScenario;
        ts.params = g_config.testParams;
        ts.resultFile = paths::dataDir() / "test-result.json";
        game::test::configure(std::move(ts));
    }

    if (!g_config.enabled) {
        spdlog::warn("disabled by config.toml (enabled = false)");
        return;
    }
    if (!hooks::initialize()) {
        return;
    }
    if (g_config.crashHandler) {
        crash::installHooks();  // before enableAll(): the game's crash reporter must not take over the top-level filter
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

    if (!g_config.instance.empty()) {
        game::installInstanceMutex(g_config.instance);
    }

    if (g_config.background) {
        game::installBackgroundWindow();
    }

    if (!g_config.appData.empty()) {
        game::installFolderRedirect(g_config.appData);
    }

    if (!g_config.traceFiles.empty()) {
        game::setFileTraceFilter(g_config.traceFiles);
    }

    game::configureEngineGuards(g_config.engineGuards, g_config.raceProbe);
    game::registerBindings();
    game::installCallTrace(g_config.traceScripts);  // before enableAll: the `calls` trace channel
    if (!game::installHooks() || !hooks::enableAll()) {
        spdlog::error("hook installation failed, mod layer disabled");
        return;
    }
    g_active = true;
    spdlog::info("hooks active, waiting for the script system to come up");
}

void onProcessDetach() {
    if (g_active) {
        game::test::shutdown();
        game::aiShutdown();
        game::combatShutdown();
        lua::Runtime::instance().stop();
        hooks::shutdown();
    }
    log::shutdown();
}

void onGameReady() {
    std::call_once(g_readyFlag, [] {
        spdlog::info("game ready: starting Lua runtime");
        crash::setGameThread();
        game::test::startWatchdog();
        auto& rt = lua::Runtime::instance();
        rt.start(paths::modsDir(), g_config.unsafeLua, paths::dataDir() / "mods.toml");
        if (g_config.modsWatch) {
            rt.setWatch(true, g_config.modsWatchInterval);
            spdlog::info("mods: watching the mod files for changes (config.toml [mods] watch)");
        }
    });
}

}  // namespace spass
