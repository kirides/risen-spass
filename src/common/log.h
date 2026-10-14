#pragma once

#include <filesystem>

#include <spdlog/spdlog.h>

namespace spass::log {

// Creates a file logger (and OutputDebugString sink) and makes it spdlog's default logger.
// Safe to call from DllMain: no threads are created.
void init(const std::filesystem::path& file);

void shutdown();

}  // namespace spass::log
