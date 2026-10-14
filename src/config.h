#pragma once

#include <filesystem>
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
//   trace_files = ""        # log every file the engine opens whose path contains this text, from process start
struct Config {
    bool enabled = true;
    std::string logLevel = "info";
    bool unsafeLua = false;
    std::string appData;
    std::string traceFiles;
};

Config loadConfig(const std::filesystem::path& file);

}  // namespace spass
