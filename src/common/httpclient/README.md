# httpclient: an async HTTP(S) client you can copy into another project

Three files, no dependencies beyond the C++17 standard library and WinHTTP (`winhttp.lib`, part of Windows):

| File | What |
|---|---|
| `httpclient.h` | the interface (`httpc::Client`, `Request`, `Response`), `CompletionQueue`, URL helpers |
| `url.cpp` | URL parsing, host allow-lists, redirect resolution, `perform()`, `CompletionQueue` (pure C++, portable) |
| `winhttp_backend.cpp` | `Client::create_winhttp()`: WinHTTP in **async mode** (Windows only; on other platforms it returns `nullptr`) |

Add `url.cpp` and `winhttp_backend.cpp` to your build and link `winhttp`. The namespace is `httpc`; search-and-replace it if it clashes.
Nothing here includes anything of risen-spass.

## Use

```cpp
#include "httpclient.h"

auto client = httpc::Client::create_winhttp({ "my-app/1.0", /*maxInFlight*/ 8 });

httpc::Request req;
req.url = "https://api.example.com/latest";
req.headers = { { "Authorization", "Bearer abc" } };
req.timeoutMs = 10000;                    // per phase; totalTimeoutMs (0 = 2x) bounds the whole transfer incl. redirects
req.allowedHosts = { "api.example.com", "*.cdn.example.com" };   // optional: every redirect hop is checked too

uint64_t id = client->submit(req, [](uint64_t id, httpc::Response r) {
    // runs on a WinHTTP / service thread, exactly once, never inside submit()
    if (!r.ok)      { /* r.error: "timed out", "cancelled", "host name not found", "TLS/certificate error", ... */ return; }
    if (r.status == 200) { /* r.body, r.header("content-type"), r.finalUrl, r.redirects, r.elapsedMs */ }
});
client->cancel(id);                        // the callback still comes, with r.cancelled == true
```

**Getting the result on your own thread** (a game loop, a GUI): use the queue instead of doing work in the callback.

```cpp
httpc::CompletionQueue queue;
client->submit(req, queue.sink());
// every frame:
std::vector<httpc::Completed> done;
queue.poll(done);                          // each: { id, response }
```

**Blocking** (tools, tests): `httpc::Response r = httpc::perform(req);` (a shared default client) or `httpc::perform(*client, req)`.

## Behaviour worth knowing

* **Outbound only.** No listening socket exists anywhere, so there is no firewall "allow incoming" prompt.
* **No thread per request.** A request is a state machine driven by WinHTTP's callbacks (send, receive headers, query data, read, ...). One small service
  thread keeps the deadlines and delivers errors that are known at once (a bad URL) so that the callback never runs inside `submit()`.
* **At most `maxInFlight` run at once**; the rest wait in a queue (and can be cancelled there). The deadline starts when a request starts running.
* **Redirects are done by hand** (`maxRedirects`, default 5): the host allow-list applies to every hop, `https` -> `http` is refused, `Authorization` / `Cookie`
  are dropped when the host changes, 301/302 on POST and 303 become GET. Cookies, ambient (NTLM/Kerberos) credentials and WinHTTP's own redirects are off.
  gzip/deflate are decoded by WinHTTP (Windows 8.1+).
* **Limits:** `maxBytes` (default 8 MB) ends an oversized answer with an error; header names/values are validated (no line breaks, `Host`/`Content-Length`/
  `Connection` cannot be set); URLs must be `http(s)://host[:port]/...` without credentials.
* **Shutdown:** destroying the client cancels everything still running and waits (5 s at most) until every callback has been called; no callback runs
  after the destructor returned.
* **Lifetime inside the backend:** each request object is reference counted (one reference for the client, one per WinHTTP request handle, released by
  the handle's `HANDLE_CLOSING` notification), so a late callback on any thread never touches freed memory.

## Other backends

`Client` is an ordinary interface (`submit`, `cancel`, `in_flight`). A libcurl backend or a test double implements it and everything else
(`perform`, `CompletionQueue`, the URL helpers) keeps working. In tests of code that *uses* a client, a fake that calls the `Done` from another
thread is enough.

## Tests

`tests/test_main.cpp` (`http_*`): a loopback server on `127.0.0.1` serving every connection on its own thread; checks for redirects and headers, size limits, the
allow-list, 40 requests through 4 slots, cancel while running and while queued, the total deadline against a trickling server, early errors from another
thread, and a client destroyed with requests in flight.
