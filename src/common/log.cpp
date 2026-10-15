#include "common/log.h"

#include <memory>
#include <vector>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>

namespace spass::log {
namespace {
std::vector<spdlog::sink_ptr> g_sinks;
}

const std::vector<spdlog::sink_ptr>& sinks() { return g_sinks; }

void init(const std::filesystem::path& file) {
    std::vector<spdlog::sink_ptr> sinks;
    try {
        std::filesystem::create_directories(file.parent_path());
        sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(file.string(), /*truncate=*/true));
    } catch (const spdlog::spdlog_ex&) {
        // Fall through: the debugger sink below is still useful.
    }
    sinks.push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());

    g_sinks = sinks;
    auto logger = std::make_shared<spdlog::logger>("spass", sinks.begin(), sinks.end());
    logger->set_pattern("[%H:%M:%S.%e] [%^%l%$] [t%t] %v");
    logger->set_level(spdlog::level::debug);
    logger->flush_on(spdlog::level::info);
    spdlog::set_default_logger(std::move(logger));
}

void shutdown() {
    spdlog::shutdown();
}

}  // namespace spass::log
