#pragma once

#include <filesystem>
#include <vector>

#include <spdlog/spdlog.h>

namespace spass::log {

// Creates a file logger (and OutputDebugString sink) and makes it spdlog's default logger.
// Safe to call from DllMain: no threads are created.
void init(const std::filesystem::path& file);

// The sinks of the main logger (file + debugger); trace channels write through the same sinks.
const std::vector<spdlog::sink_ptr>& sinks();

void shutdown();

}  // namespace spass::log
