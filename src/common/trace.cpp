#include "common/trace.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <set>

#include <spdlog/sinks/basic_file_sink.h>

#include "common/log.h"

namespace spass::trace {
namespace detail {
std::atomic<std::uint64_t> g_mask{0};
}  // namespace detail

namespace {

std::mutex g_mutex;
std::set<std::string> g_custom;  // enabled custom channels
bool g_allCustom = false;
std::shared_ptr<spdlog::sinks::sink> g_fileSink;
std::map<std::string, std::shared_ptr<spdlog::logger>> g_loggers;

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

int builtinIndex(const std::string& name) {
    for (unsigned i = 0; i < static_cast<unsigned>(Channel::Count); ++i) {
        if (name == kChannelNames[i]) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::uint64_t allBits() { return (std::uint64_t{1} << static_cast<unsigned>(Channel::Count)) - 1; }

std::shared_ptr<spdlog::logger> loggerFor(const std::string& channel) {
    std::lock_guard lock(g_mutex);
    auto it = g_loggers.find(channel);
    if (it != g_loggers.end()) {
        return it->second;
    }
    std::vector<spdlog::sink_ptr> sinks = log::sinks();
    if (g_fileSink) {
        sinks.push_back(g_fileSink);
    }
    auto logger = std::make_shared<spdlog::logger>("trace:" + channel, sinks.begin(), sinks.end());
    logger->set_pattern("[%H:%M:%S.%e] [%n] %v");
    logger->set_level(spdlog::level::trace);
    logger->flush_on(spdlog::level::info);
    g_loggers[channel] = logger;
    return logger;
}

void setMask(std::uint64_t mask) { detail::g_mask.store(mask, std::memory_order_relaxed); }
std::uint64_t getMask() { return detail::g_mask.load(std::memory_order_relaxed); }

}  // namespace

ChannelSet parseChannels(std::string_view list) {
    ChannelSet out;
    std::string token;
    const auto flush = [&] {
        if (token.empty()) {
            return;
        }
        const std::string name = lower(token);
        token.clear();
        if (name == "all" || name == "*") {
            out.all = true;
            out.mask = allBits();
            return;
        }
        const int idx = builtinIndex(name);
        if (idx >= 0) {
            out.mask |= std::uint64_t{1} << idx;
        } else if (std::find(out.custom.begin(), out.custom.end(), name) == out.custom.end()) {
            out.custom.push_back(name);
        }
    };
    for (const char c : list) {
        if (c == ',' || c == ';' || c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            flush();
        } else {
            token += c;
        }
    }
    flush();
    return out;
}

std::string channelNames(std::uint64_t mask) {
    std::string out;
    for (unsigned i = 0; i < static_cast<unsigned>(Channel::Count); ++i) {
        if ((mask >> i) & 1u) {
            out += out.empty() ? "" : ",";
            out += kChannelNames[i];
        }
    }
    return out;
}

void init(std::string_view list, const std::filesystem::path& traceFile) {
    if (!traceFile.empty()) {
        try {
            std::filesystem::create_directories(traceFile.parent_path());
            std::lock_guard lock(g_mutex);
            g_fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(traceFile.string(), /*truncate=*/true);
            g_loggers.clear();
        } catch (const spdlog::spdlog_ex& e) {
            spdlog::warn("trace: cannot open {}: {}", traceFile.string(), e.what());
        }
    }
    enable(list);
}

void enable(std::string_view list) {
    const ChannelSet set = parseChannels(list);
    setMask(getMask() | set.mask);
    std::lock_guard lock(g_mutex);
    g_allCustom = g_allCustom || set.all;
    g_custom.insert(set.custom.begin(), set.custom.end());
}

void disable(std::string_view list) {
    const ChannelSet set = parseChannels(list);
    setMask(getMask() & ~set.mask);
    std::lock_guard lock(g_mutex);
    if (set.all) {
        g_allCustom = false;
        g_custom.clear();
    }
    for (const auto& c : set.custom) {
        g_custom.erase(c);
    }
}

std::string enabledList() {
    std::string out = channelNames(getMask());
    std::lock_guard lock(g_mutex);
    for (const auto& c : g_custom) {
        out += out.empty() ? "" : ",";
        out += c;
    }
    return out;
}

bool enabledName(const std::string& channel) {
    const std::string name = lower(channel);
    const int idx = builtinIndex(name);
    if (idx >= 0) {
        return (getMask() >> idx) & 1u;
    }
    std::lock_guard lock(g_mutex);
    return g_allCustom || g_custom.count(name) != 0;
}

void emit(Channel c, const std::string& message) {
    loggerFor(kChannelNames[static_cast<unsigned>(c)])->info("{}", message);
}

void emitNamed(const std::string& channel, const std::string& message) {
    loggerFor(lower(channel))->info("{}", message);
}

}  // namespace spass::trace
