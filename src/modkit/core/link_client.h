// The editor's end of the live link (docs/LUA_API.md "Live link"): talks to the running game over its named pipe `\\.\pipe\risen-spass-<pid>`.
// Named pipe only (Windows); no network socket. The game describes the pipe in <game>\bin\spass\link.json while `[link] enabled = true`.
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace mk {

struct LinkReply {
    bool ok = false;
    std::string error;       // the game's reason, or why the request could not be made ("timeout", "not connected")
    nlohmann::json result;   // the reply's `result` (null when none)
};

class LinkClient {
public:
    LinkClient() = default;
    ~LinkClient() { close(); }
    LinkClient(const LinkClient&) = delete;
    LinkClient& operator=(const LinkClient&) = delete;

    // Opens the pipe named by <game_dir>\bin\spass\link.json. false + `error` when the game is not running with the link on.
    bool connect(const std::string& game_dir, std::string& error);
    // Opens a pipe by its path (tests).
    bool connect_pipe(const std::string& pipe_path, std::string& error);
    void close();
    bool connected() const;

    // Sends {"id", "cmd", "args"} and waits for the reply with that id (events that arrive meanwhile are kept in take_events()).
    LinkReply request(const std::string& cmd, const nlohmann::json& args = nlohmann::json::object(), int timeout_ms = 5000);
    // The pushed events (`{"event": ..., "data": ...}`) received so far; also reads what is waiting in the pipe without blocking.
    std::vector<nlohmann::json> take_events();

private:
    // Next message: 1 = got one, 0 = nothing within timeout_ms, -1 = the pipe is closed.
    int read_message(nlohmann::json& out, int timeout_ms);

    void* handle_ = nullptr;
    int next_id_ = 1;
    std::vector<nlohmann::json> events_;
};

// Reloads one mod (or "all") in the running game over the link. `message` is a sentence for the status line, `ok` false when the game refused or the mod
// failed to load again. `reachable` = false: no link (the caller falls back to the file watcher).
struct LinkReload {
    bool reachable = false;
    bool ok = false;
    std::string message;
};
LinkReload link_reload(const std::string& game_dir, const std::string& mod_id);

// config.toml of the layer with `[link] enabled = true` (the rest stays); the pipe is off by default.
std::string config_with_link(const std::string& text);

}  // namespace mk
