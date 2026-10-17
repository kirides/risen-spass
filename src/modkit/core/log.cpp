#include "core/log.h"

#include <chrono>
#include <cstdio>

namespace mk {

namespace {
const auto kStart = std::chrono::steady_clock::now();
constexpr size_t kKeep = 20000;
}

Log& Log::get() {
    static Log log;
    return log;
}

void Log::add(Level level, std::string text) {
    const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - kStart).count();
    std::lock_guard<std::mutex> lock(mu_);
    if (echo_) {
        static const char* kNames[] = {"debug", "info", "warn", "error"};
        std::fprintf(stderr, "[%8.3f] [%s] %s\n", t, kNames[static_cast<int>(level)], text.c_str());
    }
    lines_.push_back({level, std::move(text), t});
    if (lines_.size() > kKeep * 2) {
        const size_t drop = lines_.size() - kKeep;
        lines_.erase(lines_.begin(), lines_.begin() + static_cast<std::ptrdiff_t>(drop));
        dropped_ += drop;
    }
}

size_t Log::copy_since(size_t from, std::vector<LogLine>& out) const {
    std::lock_guard<std::mutex> lock(mu_);
    const size_t total = dropped_ + lines_.size();
    if (from < dropped_) from = dropped_;
    for (size_t i = from - dropped_; i < lines_.size(); ++i) out.push_back(lines_[i]);
    return total;
}

size_t Log::size() const {
    std::lock_guard<std::mutex> lock(mu_);
    return dropped_ + lines_.size();
}

void Log::clear() {
    std::lock_guard<std::mutex> lock(mu_);
    dropped_ += lines_.size();
    lines_.clear();
}

void log_debug(std::string text) { Log::get().add(Level::Debug, std::move(text)); }
void log_info(std::string text) { Log::get().add(Level::Info, std::move(text)); }
void log_warn(std::string text) { Log::get().add(Level::Warn, std::move(text)); }
void log_error(std::string text) { Log::get().add(Level::Error, std::move(text)); }

}  // namespace mk
