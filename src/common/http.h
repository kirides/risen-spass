#pragma once

// A small outbound-only HTTP(S) client on top of WinHTTP (docs/LUA_API.md "Network access").
//
//  * Only client connections are ever made: nothing listens, so the player sees no firewall "allow this app to host a server" prompt.
//  * perform() blocks; submit()/poll() run requests on a few worker threads and hand the finished ones back to the caller's thread
//    (the game thread asks once per frame), so the game never waits for the network.
//  * Redirects are followed by hand (max `maxRedirects`): every hop is checked against the host allow-list, a hop from https to http is
//    refused, and the Authorization header is dropped when the host changes.
//  * Cookies, ambient credentials (NTLM/Kerberos) and automatic redirects of WinHTTP are switched off.

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace spass::http {

using Headers = std::vector<std::pair<std::string, std::string>>;

struct Request {
    std::string method = "GET";
    std::string url;
    Headers headers;
    std::string body;
    int timeoutMs = 15000;                  // per phase (connect / send / receive) and, as a whole, for the transfer
    std::size_t maxBytes = 8u << 20;        // response body limit; more is an error
    int maxRedirects = 5;
    std::vector<std::string> allowedHosts;  // empty = any host; "example.com" or "*.example.com" (matches subdomains, not the bare domain)
    std::string userAgent = "risen-spass";
};

struct Response {
    bool ok = false;      // a response arrived (any status); false = transport error, see `error`
    int status = 0;
    std::string error;
    Headers headers;      // names lower-cased
    std::string body;
    std::string finalUrl;
    int redirects = 0;
    long long elapsedMs = 0;

    // First header with this (lower-case) name; "" when absent.
    std::string header(const std::string& lowerName) const;
};

// The parts of an http(s) URL. false when it is not a valid http/https URL.
struct Url {
    bool https = false;
    std::string host;  // lower-case
    int port = 0;
    std::string path;  // path + query, starts with '/'
};
bool parseUrl(const std::string& url, Url& out, std::string& error);

// Does `host` match one of the patterns (case-insensitive)? An empty list allows everything.
bool hostAllowed(const std::string& host, const std::vector<std::string>& patterns);

// Resolves the Location header of a redirect against the URL it came from.
std::string resolveLocation(const Url& base, const std::string& location);

// Blocking request.
Response perform(const Request& request);

// ---- asynchronous use -------------------------------------------------------------------------------------------------------------

struct Completed {
    std::uint64_t id = 0;
    Response response;
};

// Queues a request on the worker threads (at most 4 run at once, the rest wait). Returns its id.
std::uint64_t submit(Request request);
// Moves the finished requests into `out` (in completion order); never blocks.
void poll(std::vector<Completed>& out);
// Requests that were submitted and are not yet polled.
std::size_t inFlight();

}  // namespace spass::http
