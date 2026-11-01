// httpclient: a small asynchronous HTTP(S) client abstraction (client connections only).
//
// PORTABLE: this folder (httpclient.h, url.cpp, winhttp_backend.cpp) depends on nothing but the C++17 standard library and, in
// winhttp_backend.cpp, <windows.h> + WinHTTP (link winhttp.lib). Copy the three files into another project, add the two .cpp files to the build.
// The namespace is `httpc`; rename it with a search-and-replace if it clashes.
//
// Design
//   * `Client` is the interface: submit() a Request, get exactly one callback with the Response. cancel() ends a request early.
//   * `Client::create_winhttp()` is the Windows backend: WinHTTP in ASYNC mode, no thread per request (a request is a state machine driven by
//     WinHTTP's own callbacks), one small service thread for deadlines and deferred completions.
//   * Nothing listens on a port: only outbound connections, so there is never a firewall "allow incoming" prompt.
//   * The completion callback runs on an arbitrary thread (a WinHTTP thread, or the service thread). It is never called from inside submit().
//     A game or GUI that wants the result on its own thread uses `CompletionQueue` (below): the callback pushes, the main loop polls.
//   * Redirects are followed by hand (every hop is checked against the host allow-list; https -> http is refused; Authorization / Cookie are dropped
//     when the host changes). Cookies, ambient credentials and WinHTTP's automatic redirects are off.
//   * Other backends (libcurl, a test double) implement `Client` too; the helpers at the bottom work with any of them.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace httpc {

using Headers = std::vector<std::pair<std::string, std::string>>;

struct Request {
    std::string method = "GET";
    std::string url;                        // http:// or https://
    Headers headers;                        // Host, Content-Length and Connection cannot be set
    std::string body;
    int timeoutMs = 15000;                  // per phase (resolve / connect / send / receive)
    int totalTimeoutMs = 0;                 // for the whole request including redirects; 0 = twice timeoutMs
    std::size_t maxBytes = 8u << 20;        // response body limit; more is an error
    int maxRedirects = 5;
    std::vector<std::string> allowedHosts;  // empty = any host; "example.com" or "*.example.com" (subdomains only)
    std::string userAgent;                  // empty = the client's default
};

struct Response {
    bool ok = false;      // a response arrived (any status); false = transport error, see `error`
    bool cancelled = false;
    int status = 0;
    std::string error;    // "timed out", "cancelled", "host name not found", ...
    Headers headers;      // names lower-cased
    std::string body;
    std::string finalUrl;
    int redirects = 0;
    long long elapsedMs = 0;

    // First header with this (lower-case) name; "" when absent.
    std::string header(const std::string& lowerName) const;
};

// Called exactly once per submitted request, on an arbitrary thread, never from inside submit(). Must not block for long.
using Done = std::function<void(std::uint64_t id, Response response)>;

struct ClientOptions {
    std::string userAgent = "httpc";  // used when a request has none
    int maxInFlight = 16;             // more submitted requests wait in a queue
};

class Client {
public:
    using Options = ClientOptions;

    virtual ~Client() = default;
    // Queues a request; returns its id (never 0). The deadline starts when the request starts running, not while it waits in the queue.
    virtual std::uint64_t submit(Request request, Done done) = 0;
    // Ends the request early: its callback gets `cancelled = true`, error "cancelled". false when it is unknown or already finished.
    virtual bool cancel(std::uint64_t id) = 0;
    // Requests submitted whose callback has not been called yet.
    virtual std::size_t in_flight() const = 0;

    // The WinHTTP backend (Windows only; on other platforms returns nullptr).
    static std::unique_ptr<Client> create_winhttp(const ClientOptions& options = ClientOptions());
    // Destroying a client cancels everything still running and waits (a few seconds at most) until the callbacks are through.
};

// ---- helpers that work with any Client ------------------------------------------------------------------------------------------------

// Blocking request on `client` (do not call from a thread the client's callbacks need).
Response perform(Client& client, Request request);
// Blocking request on a process-wide default client (created on first use, never destroyed).
Response perform(Request request);

// Hands completions from the callback's thread to the thread that polls (a game loop, a GUI): `sink()` is the Done to pass to submit().
struct Completed {
    std::uint64_t id = 0;
    Response response;
};
class CompletionQueue {
public:
    Done sink();
    void poll(std::vector<Completed>& out);  // moves everything finished so far into `out`
    bool empty() const;

private:
    // shared so that a late callback after the queue is gone is harmless
    struct State {
        std::mutex m;
        std::vector<Completed> items;
    };
    std::shared_ptr<State> state_ = std::make_shared<State>();
};

// ---- URLs and host rules (pure, no platform code) -------------------------------------------------------------------------------------

struct Url {
    bool https = false;
    std::string host;  // lower-case
    int port = 0;
    std::string path;  // path + query, starts with '/'
};
// http(s) only; no credentials in the URL, no IPv6 literals, no control characters. false + `error`.
bool parse_url(const std::string& url, Url& out, std::string& error);
// Does `host` match one of the patterns (case-insensitive)? An empty list allows everything.
bool host_allowed(const std::string& host, const std::vector<std::string>& patterns);
// Resolves the Location header of a redirect against the URL it came from.
std::string resolve_location(const Url& base, const std::string& location);
// "scheme://host[:port]/path" of a parsed URL (the port only when it is not the default).
std::string to_string(const Url& url);

}  // namespace httpc
