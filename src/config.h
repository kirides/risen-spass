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
struct Config {
    bool enabled = true;
    std::string logLevel = "info";
    bool unsafeLua = false;
};

Config loadConfig(const std::filesystem::path& file);

}  // namespace spass
