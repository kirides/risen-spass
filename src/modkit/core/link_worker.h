// The editor's live connection to the running game (core/link_client.h) on a background thread: it finds the pipe (<game>\bin\spass\link.json), connects by itself,
// reconnects when the game is started again, answers the panels' requests without ever blocking the UI, and collects the log lines the game pushes.
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/link_client.h"

namespace mk {

class LinkWorker {
public:
    LinkWorker();
    ~LinkWorker();
    LinkWorker(const LinkWorker&) = delete;
    LinkWorker& operator=(const LinkWorker&) = delete;

    // The Risen folder to look in (empty = the link is off). Changing it drops the connection.
    void set_game_dir(const std::string& dir);

    struct Status {
        bool connected = false;
        std::string pipe;
        int pid = 0;
        std::string layer;
        std::string session;  // "new" / "load" / "" (no game running yet)
        std::string note;     // why not connected, in words
        unsigned long long connects = 0;  // counts up with every (re)connection: panels refresh their data when it changes
    };
    Status status() const;

    // Queues a request; the answer comes back through take_results() with the same `tag`. A request whose tag is already waiting replaces it (a panel that asks
    // every second never builds a backlog).
    void request(const std::string& tag, const std::string& cmd, const nlohmann::json& args = nlohmann::json::object(), int timeout_ms = 4000);

    struct Result {
        std::string tag;
        std::string cmd;
        LinkReply reply;
    };
    std::vector<Result> take_results();
    // Results of requests whose tag starts with "live:" (the World view's mirror of the running game): kept apart so the Live panel's take_results() never swallows them.
    std::vector<Result> take_live();
    // Results of requests whose tag starts with "app:" (the application's own, e.g. the live edit's reload).
    std::vector<Result> take_app();

    // The game's log lines (while a level is subscribed) and the other pushed events.
    struct LogLine {
        std::string level;
        std::string text;
    };
    std::vector<LogLine> take_log();
    std::vector<nlohmann::json> take_events();
    // "off" | "trace" | "debug" | "info" | "warn" | "error": what the game pushes (applied at once, and again after a reconnect)
    void set_log_level(const std::string& level);

private:
    struct Pending {
        std::string tag, cmd;
        nlohmann::json args;
        int timeout_ms;
    };
    void loop();
    bool try_connect();

    std::thread thread_;
    mutable std::mutex m_;
    std::condition_variable cv_;
    bool stop_ = false;
    std::string game_dir_;
    bool dir_changed_ = false;
    Status status_;
    std::deque<Pending> queue_;
    std::vector<Result> results_, live_results_, app_results_;
    std::vector<LogLine> log_;
    std::vector<nlohmann::json> events_;
    std::string log_level_ = "off";
    bool level_dirty_ = false;
};

}  // namespace mk
