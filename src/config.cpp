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
        cfg.trace = t["debug"]["trace"].value_or(cfg.trace);
        cfg.traceFile = t["debug"]["trace_file"].value_or(cfg.traceFile);
        cfg.testEnabled = t["test"]["enabled"].value_or(cfg.testEnabled);
        cfg.testDeadline = t["test"]["deadline_s"].value_or(cfg.testDeadline);
        cfg.testScenario = t["test"]["scenario"].value_or(cfg.testScenario);
        cfg.testExit = t["test"]["exit_on_finish"].value_or(cfg.testExit);
        if (const toml::table* params = t["test"]["params"].as_table()) {
            for (const auto& [key, value] : *params) {
                if (value.is_string()) {
                    cfg.testParams[std::string(key.str())] = *value.value<std::string>();
                } else if (value.is_integer()) {
                    cfg.testParams[std::string(key.str())] = std::to_string(*value.value<std::int64_t>());
                } else if (value.is_boolean()) {
                    cfg.testParams[std::string(key.str())] = *value.value<bool>() ? "true" : "false";
                } else if (value.is_floating_point()) {
                    cfg.testParams[std::string(key.str())] = std::to_string(*value.value<double>());
                }
            }
        }
    } catch (const toml::parse_error& e) {
        spdlog::error("config: {}: {}", file.string(), e.description());
    }
    return cfg;
}

}  // namespace spass
