#pragma once

// The live link: a named-pipe server through which an editor (risen-modkit) or a script talks to the running game (docs/LUA_API.md "Live link").
//
//  * Named pipe only, never a socket: nothing listens on the network, so no firewall prompt, and the pipe rejects remote clients and
//    is readable/writable by the current user only (security descriptor "owner rights").
//  * One client at a time. Messages are UTF-8 JSON, one per pipe message (message mode: no length prefix needed), at most 4 MB.
//  * All I/O runs on one background thread (overlapped); the game thread polls the received messages once per frame and
//    answers with send(). send() may be called from any thread and never blocks.

#include <string>
#include <vector>

namespace spass::link {

// Creates the pipe `\\.\pipe\<name>` and starts the I/O thread. false + `error` when the pipe name is taken or the thread cannot start.
bool start(const std::string& name, std::string& error);
void stop();
bool running();
// Full pipe path (empty while not running).
std::string pipePath();
bool connected();

struct Inbox {
    std::vector<std::string> messages;
    int connects = 0;     // clients that connected since the last poll
    int disconnects = 0;  // clients that went away since the last poll
};
// Takes what arrived since the last call.
void poll(Inbox& out);

// Queues a message for the client; false (dropped) when no client is connected or the queue is full (8 MB).
bool send(const std::string& message);

}  // namespace spass::link
