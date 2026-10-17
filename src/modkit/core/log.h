// The editor's log: every action and every parse problem goes here (shown by the Log panel, mirrored to stderr). Thread-safe: the
// background loaders log too.
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace mk {

enum class Level : uint8_t { Debug, Info, Warn, Error };

struct LogLine {
    Level level;
    std::string text;
    double time;  // seconds since the start of the process
};

class Log {
public:
    static Log& get();
    void add(Level level, std::string text);
    // copies the lines from `from` on (the panel keeps its own copy and asks for new lines each frame)
    size_t copy_since(size_t from, std::vector<LogLine>& out) const;
    size_t size() const;
    void clear();
    void set_echo(bool on) { echo_ = on; }  // stderr mirror (on by default; the tests switch it off)
    void set_echo_level(Level l) { echo_level_ = l; }  // the lowest level mirrored to stderr (Info by default, --verbose: Debug)

private:
    mutable std::mutex mu_;
    std::vector<LogLine> lines_;
    size_t dropped_ = 0;  // lines removed at the front (the log keeps the last 20000)
    bool echo_ = true;
    Level echo_level_ = Level::Info;
};

void log_debug(std::string text);
void log_info(std::string text);
void log_warn(std::string text);
void log_error(std::string text);

}  // namespace mk
