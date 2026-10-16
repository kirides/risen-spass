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
//   trace_scripts = ""      # substrings of script names (comma separated) the `calls` trace channel reports
//   crash_handler = true    # a crash (or a fault outside guarded(), or a guarded() call that faulted) is reported in spass.log and spass\crash.log with the stack
//   crash_dump = "off"      # "mini" | "full": also write a minidump <bin>\spass\crash\crash-<time>-pid<pid>.dmp of a crash (scripts/test-run.ps1 -CrashDump); docs/DEVELOPMENT.md "Crashes"
//   [mods]
//   watch     = false       # development: a changed .lua / .toml / .csv file of a loaded mod reloads that mod (risen.mods.reload; docs/LUA_API.md "Hot reload")
//   watch_interval = 1.0    # seconds between two looks at the files
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
    std::string traceScripts;
    bool crashHandler = true;
    std::string crashDump = "off";
    bool modsWatch = false;
    double modsWatchInterval = 1.0;
    bool testEnabled = false;
    double testDeadline = 0;
    std::string testScenario;
    bool testExit = true;
    std::map<std::string, std::string> testParams;
};

Config loadConfig(const std::filesystem::path& file);

}  // namespace spass
