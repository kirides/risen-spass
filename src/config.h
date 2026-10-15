#pragma once

#include <filesystem>
#include <map>
#include <string>

namespace spass {

// <bin>/spass/config.toml (all keys optional):
//
//   enabled   = true        # false: the proxy DLL does nothing (quick way to bisect crashes)
//   log_level = "info"      # trace|debug|info|warn|error
//   [lua]
//   unsafe    = false       # true: mods get io/ffi/debug/package (development only)
//   [debug]
//   app_data  = ""          # redirect the game's %LOCALAPPDATA% and %APPDATA% (config, savegames, screenshots,
//                           # sentry) to this directory: lets a test copy run without touching the user's data
//   instance  = ""         # id of a parallel test instance: the engine's single-instance mutex "GenomeMutex" is renamed to
//                           # "GenomeMutex_<id>" so several Risen.exe (separate test copies, each with its own app_data) can run at once
//   trace_files = ""        # log every file the engine opens whose path contains this text, from process start
//   trace      = ""         # opt-in trace channels, e.g. "template,balance,save,events" or "all" (see common/trace.h)
//   trace_file = false      # also write the trace lines to <bin>/spass/trace.log
//   [test]                  # live verification (risen.test): scripts/test-run.ps1 -WaitForResult sets these
//   enabled   = false       # finish() writes <bin>/spass/test-result.json and ends the process, the deadline applies
//   deadline_s = 0          # > 0: the run is reported as FAIL "timeout" (with the unfinished checks) after this many seconds
//   scenario  = ""          # name shown in the result file
//   exit_on_finish = true   # the process ends shortly after risen.test.finish() (the script kills it first)
//   [test.params]           # strings the scenario reads with risen.test.param(name, default)
struct Config {
    bool enabled = true;
    std::string logLevel = "info";
    bool unsafeLua = false;
    std::string appData;
    std::string instance;
    std::string traceFiles;
    std::string trace;
    bool traceFile = false;
    bool testEnabled = false;
    double testDeadline = 0;
    std::string testScenario;
    bool testExit = true;
    std::map<std::string, std::string> testParams;
};

Config loadConfig(const std::filesystem::path& file);

}  // namespace spass
