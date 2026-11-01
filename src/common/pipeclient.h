#pragma once

// A named-pipe CLIENT for mods (risen.pipe, docs/LUA_API.md "Named pipes"): the game connects to `\\.\pipe\<name>` that another local program hosts
// (a Twitch bridge, an overlay, a tool). Never a listening socket, so no firewall prompt.
//
//  * One background thread per pipe: connects (and reconnects every `retrySeconds` while the server is absent or went away), reads bytes, splits them
//    at '\n' into lines, writes what send() queued. The game thread only takes lines with poll() and queues with send(); nothing blocks it.
//  * The connection opens with SECURITY_IDENTIFICATION: the server cannot impersonate the game's user.
//  * Two framings: "lines" (text split at the line break) and "framed" (each message is a 2-byte little-endian length followed by that many bytes; what the
//    Twitch connector and many Go/C# tools write). The server's pipe may be a byte or a message pipe; the client always reads bytes.
//  * Read-only by default (GENERIC_READ only, least privilege); `write = true` also asks for write access so send() works.
//  * Bounded: at most 1000 unread lines (further lines are counted as dropped), a line over 64 KB is cut, at most 1 MB of unsent text.

#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace spass::pipeclient {

constexpr std::size_t kMaxLines = 1000;
constexpr std::size_t kMaxLineBytes = 64 * 1024;
constexpr std::size_t kMaxOutBytes = 1024 * 1024;

// True for a bare pipe name that is safe to append to `\\.\pipe\`: 1..100 characters of a-z A-Z 0-9 _ . -
bool validName(const std::string& name);

struct Options {
    bool framed = false;  // false: lines; true: 2-byte little-endian length prefix
    bool write = false;   // open for writing as well
};

class Client {
public:
    Client(std::string name, double retrySeconds, Options options = {});
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    void start();
    void stop();  // joins the thread; also done by the destructor

    bool connected() const { return connected_.load(); }
    // Complete lines received since the last call (at most `max`), without the line break.
    std::vector<std::string> poll(std::size_t max);
    std::size_t pending() const;
    // Queues text (lines: a line break is appended; framed: a length prefix, at most 65535 bytes). false when not connected, not opened with `write`, or the queue is full.
    bool send(const std::string& text);

    const std::string& name() const { return name_; }
    int connects() const { return connects_.load(); }
    int dropped() const { return dropped_.load(); }

    // Feeds raw bytes through the line splitter or the frame parser (exposed for the unit test).
    void feed(const char* data, std::size_t n);

private:
    void run();
    void pushLine(std::string line);

    std::string name_;
    double retry_;
    Options options_;
    std::thread thread_;
    void* stopEvent_ = nullptr;   // HANDLE
    void* wakeEvent_ = nullptr;   // HANDLE: set by send()
    std::atomic<bool> connected_{false};
    std::atomic<int> connects_{0};
    std::atomic<int> dropped_{0};
    mutable std::mutex mutex_;
    std::deque<std::string> lines_;
    std::string partial_;
    bool discarding_ = false;     // inside a line that exceeded kMaxLineBytes
    std::deque<std::string> out_;
    std::size_t outBytes_ = 0;
};

}  // namespace spass::pipeclient
