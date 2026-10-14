#include "config.h"

#include <spdlog/spdlog.h>
#include <toml++/toml.hpp>

namespace spass {

Config loadConfig(const std::filesystem::path& file) {
    Config cfg;
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) {
        return cfg;
    }
    try {
        const toml::table t = toml::parse_file(file.string());
        cfg.enabled = t["enabled"].value_or(cfg.enabled);
        cfg.logLevel = t["log_level"].value_or(cfg.logLevel);
        cfg.unsafeLua = t["lua"]["unsafe"].value_or(cfg.unsafeLua);
        cfg.appData = t["debug"]["app_data"].value_or(cfg.appData);
        cfg.traceFiles = t["debug"]["trace_files"].value_or(cfg.traceFiles);
    } catch (const toml::parse_error& e) {
        spdlog::error("config: {}: {}", file.string(), e.description());
    }
    return cfg;
}

}  // namespace spass
