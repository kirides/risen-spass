// WinHTTP backend of httpclient.h: asynchronous mode, a request is a state machine driven by WinHTTP's status callbacks.
//
//   start -> WinHttpSendRequest -> SENDREQUEST_COMPLETE -> WinHttpReceiveResponse -> HEADERS_AVAILABLE
//         -> (redirect: close, start again)  or  WinHttpQueryDataAvailable -> DATA_AVAILABLE -> WinHttpReadData -> READ_COMPLETE -> (loop) -> done
//
// Ownership: an `Op` is reference counted. One reference belongs to the client (released when the request finishes), one to every WinHTTP request
// handle that has our callback (released by that handle's HANDLE_CLOSING notification, which WinHTTP always delivers last). So an Op outlives every
// callback that can still reach it, whatever thread they run on. `Op::mutex` guards the handles and the response under construction.
#include "httpclient.h"

#ifdef _WIN32

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <thread>

namespace httpc {
namespace {

using Clock = std::chrono::steady_clock;

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string errorText(DWORD code) {
    switch (code) {
        case ERROR_WINHTTP_TIMEOUT: return "timed out";
        case ERROR_WINHTTP_OPERATION_CANCELLED: return "cancelled";
        case ERROR_WINHTTP_NAME_NOT_RESOLVED: return "host name not found";
        case ERROR_WINHTTP_CANNOT_CONNECT: return "cannot connect";
        case ERROR_WINHTTP_CONNECTION_ERROR: return "connection error";
        case ERROR_WINHTTP_SECURE_FAILURE: return "TLS/certificate error";
        case ERROR_WINHTTP_SECURE_CERT_CN_INVALID: return "certificate does not match the host";
        case ERROR_WINHTTP_SECURE_CERT_DATE_INVALID: return "certificate expired or not yet valid";
        case ERROR_WINHTTP_SECURE_INVALID_CA: return "certificate from an unknown authority";
        case ERROR_WINHTTP_SECURE_INVALID_CERT: return "invalid certificate";
        case ERROR_WINHTTP_SECURE_CERT_REV_FAILED: return "certificate revocation check failed";
        case ERROR_WINHTTP_INVALID_URL: return "invalid URL";
        case ERROR_WINHTTP_UNRECOGNIZED_SCHEME: return "unsupported URL scheme";
        default: return "network error " + std::to_string(code);
    }
}

Headers parseRawHeaders(const std::string& raw) {
    Headers out;
    std::size_t pos = raw.find('\n');  // skip the status line
    while (pos != std::string::npos && pos + 1 < raw.size()) {
        const std::size_t next = raw.find('\n', pos + 1);
        std::string line = raw.substr(pos + 1, next == std::string::npos ? std::string::npos : next - pos - 1);
        pos = next;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos || colon == 0) continue;
        out.emplace_back(lower(trim(line.substr(0, colon))), trim(line.substr(colon + 1)));
    }
    return out;
}

bool validHeaderName(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')) return false;
    }
    return true;
}

bool validHeaderValue(const std::string& s) { return s.find_first_of(std::string("\r\n\0", 3)) == std::string::npos; }

struct Impl;

struct Op {
    Impl* impl = nullptr;  // only touched while !finished (the client waits for all ops before it dies)
    std::uint64_t id = 0;
    Request req;
    Done done;

    // the hop in progress
    Url url;
    std::string method;
    Headers headers;
    std::string body;
    std::string userAgent;

    std::mutex mutex;  // handles + res
    HINTERNET connect = nullptr;
    HINTERNET request = nullptr;
    Response res;
    std::vector<char> chunk;  // the buffer WinHttpReadData fills (must stay valid while a read is pending)
    Clock::time_point started;

    std::atomic<bool> finished{false};
    std::atomic<bool> cancelled{false};
    std::atomic<bool> timedOut{false};
    std::atomic<int> refs{1};

    void addRef() { refs.fetch_add(1); }
    void release() {
        if (refs.fetch_sub(1) == 1) delete this;
    }
};

struct Impl {
    ClientOptions options;
    HINTERNET session = nullptr;

    std::mutex m;
    std::condition_variable idle;
    std::map<std::uint64_t, Op*> ops;  // submitted and not finished (running or waiting)
    std::deque<Op*> waiting;
    int running = 0;
    bool stopping = false;
    std::atomic<std::uint64_t> nextId{1};

    // the service thread: deferred work and deadlines
    std::mutex sm;
    std::condition_variable scv;
    std::deque<std::function<void()>> tasks;
    std::multimap<Clock::time_point, std::function<void()>> timers;
    bool stopService = false;
    std::thread service;

    void post(std::function<void()> fn) {
        {
            std::lock_guard<std::mutex> lock(sm);
            tasks.push_back(std::move(fn));
        }
        scv.notify_one();
    }
    void addTimer(Clock::time_point when, std::function<void()> fn) {
        {
            std::lock_guard<std::mutex> lock(sm);
            timers.emplace(when, std::move(fn));
        }
        scv.notify_one();
    }
    void serviceMain() {
        std::unique_lock<std::mutex> lock(sm);
        for (;;) {
            if (stopService && tasks.empty()) return;
            if (!tasks.empty()) {
                auto fn = std::move(tasks.front());
                tasks.pop_front();
                lock.unlock();
                fn();
                lock.lock();
                continue;
            }
            if (!timers.empty() && timers.begin()->first <= Clock::now()) {
                auto fn = std::move(timers.begin()->second);
                timers.erase(timers.begin());
                lock.unlock();
                fn();
                lock.lock();
                continue;
            }
            if (timers.empty()) {
                scv.wait(lock);
            } else {
                scv.wait_until(lock, timers.begin()->first);
            }
        }
    }

    void startOp(Op* op);
    void startHop(Op* op);
    void finish(Op* op);
    void abort(std::uint64_t id, bool timedOut);
    static void CALLBACK statusCallback(HINTERNET h, DWORD_PTR context, DWORD status, LPVOID info, DWORD infoLength);
};

void closeHandles(Op* op) {
    HINTERNET request, connect;
    {
        std::lock_guard<std::mutex> lock(op->mutex);
        request = op->request;
        connect = op->connect;
        op->request = nullptr;
        op->connect = nullptr;
    }
    if (request) WinHttpCloseHandle(request);  // its HANDLE_CLOSING releases the request's reference
    if (connect) WinHttpCloseHandle(connect);
}

// Ends the request with an error. `code` 0 = use `text`.
void fail(Op* op, DWORD code, const std::string& text = {}) {
    if (op->finished) return;
    {
        std::lock_guard<std::mutex> lock(op->mutex);
        op->res.ok = false;
        op->res.status = 0;
        if (op->cancelled) {
            op->res.cancelled = true;
            op->res.error = "cancelled";
        } else if (op->timedOut) {
            op->res.error = "timed out";
        } else {
            op->res.error = text.empty() ? errorText(code) : text;
        }
    }
    op->impl->finish(op);
}

void Impl::finish(Op* op) {
    if (op->finished.exchange(true)) return;
    closeHandles(op);
    Response result;
    {
        std::lock_guard<std::mutex> lock(op->mutex);
        op->res.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - op->started).count();
        result = std::move(op->res);
    }
    Done done = std::move(op->done);
    const std::uint64_t id = op->id;
    Op* next = nullptr;
    {
        std::lock_guard<std::mutex> lock(m);
        --running;
        if (!waiting.empty() && !stopping) {
            next = waiting.front();
            waiting.pop_front();
            ++running;
        }
    }
    if (next) post([this, next] { startOp(next); });
    if (done) {
        try {
            done(id, std::move(result));
        } catch (...) {
        }
    }
    {
        // the last access to the client: after this the destructor may go on and free it
        std::lock_guard<std::mutex> lock(m);
        ops.erase(id);
        idle.notify_all();
    }
    op->release();  // the client's reference
}

void Impl::abort(std::uint64_t id, bool timedOut) {
    Op* op = nullptr;
    {
        std::lock_guard<std::mutex> lock(m);
        const auto it = ops.find(id);
        if (it == ops.end()) return;
        op = it->second;
        op->addRef();  // keeps it alive while we work on it
        // a request that still waits in the queue never ran: remove it from there
        const auto w = std::find(waiting.begin(), waiting.end(), op);
        if (w != waiting.end()) {
            waiting.erase(w);
            ++running;  // finish() decrements
        }
    }
    if (timedOut) op->timedOut = true;
    else op->cancelled = true;
    fail(op, ERROR_WINHTTP_OPERATION_CANCELLED);  // closes the handles; late callbacks find `finished` set
    op->release();
}

void Impl::startOp(Op* op) {
    if (op->finished) return;
    op->started = Clock::now();
    const int total = op->req.totalTimeoutMs > 0 ? op->req.totalTimeoutMs : std::clamp(op->req.timeoutMs, 500, 120000) * 2;
    const std::uint64_t id = op->id;
    addTimer(op->started + std::chrono::milliseconds(total), [this, id] { abort(id, true); });
    startHop(op);
}

void Impl::startHop(Op* op) {
    if (op->finished) return;
    if (op->cancelled || op->timedOut) {
        fail(op, ERROR_WINHTTP_OPERATION_CANCELLED);
        return;
    }
    if (!host_allowed(op->url.host, op->req.allowedHosts)) {
        fail(op, 0, "host '" + op->url.host + "' is not allowed for this request");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(op->mutex);
        op->res.finalUrl = to_string(op->url);
    }
    HINTERNET connect = WinHttpConnect(session, widen(op->url.host).c_str(), static_cast<INTERNET_PORT>(op->url.port), 0);
    if (!connect) {
        fail(op, GetLastError());
        return;
    }
    HINTERNET request = WinHttpOpenRequest(connect, widen(op->method).c_str(), widen(op->url.path).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           op->url.https ? WINHTTP_FLAG_SECURE : 0);
    if (!request) {
        const DWORD e = GetLastError();
        WinHttpCloseHandle(connect);
        fail(op, e);
        return;
    }
    // From here on the request handle owns a reference of the op, released by its HANDLE_CLOSING.
    op->addRef();
    DWORD_PTR context = reinterpret_cast<DWORD_PTR>(op);
    WinHttpSetOption(request, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context));
    if (WinHttpSetStatusCallback(request, &Impl::statusCallback, WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES | WINHTTP_CALLBACK_FLAG_REQUEST_ERROR, 0) ==
        WINHTTP_INVALID_STATUS_CALLBACK) {
        const DWORD e = GetLastError();
        op->release();  // no HANDLE_CLOSING will come
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        fail(op, e);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(op->mutex);
        op->request = request;
        op->connect = connect;
    }
    DWORD disable = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable));
    DWORD decompress = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(request, WINHTTP_OPTION_DECOMPRESSION, &decompress, sizeof(decompress));  // Windows 8.1+; ignored when unsupported
    const int t = std::clamp(op->req.timeoutMs, 500, 120000);
    WinHttpSetTimeouts(request, t, t, t, t);

    std::string headerText;
    bool hasUa = false;
    for (const auto& [name, value] : op->headers) {
        if (lower(name) == "user-agent") hasUa = true;
        headerText += name + ": " + value + "\r\n";
    }
    if (!hasUa) headerText += "User-Agent: " + op->userAgent + "\r\n";
    const std::wstring wideHeaders = widen(headerText);
    WinHttpAddRequestHeaders(request, wideHeaders.c_str(), static_cast<DWORD>(wideHeaders.size()), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    const DWORD bodyLen = static_cast<DWORD>(op->body.size());
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, bodyLen ? const_cast<char*>(op->body.data()) : WINHTTP_NO_REQUEST_DATA, bodyLen, bodyLen, context)) {
        fail(op, GetLastError());
    }
}

bool isRedirect(int status) { return status == 301 || status == 302 || status == 303 || status == 307 || status == 308; }

void onHeaders(Op* op, HINTERNET h) {
    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    Headers headers;
    DWORD rawSize = 0;
    WinHttpQueryHeaders(h, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &rawSize, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && rawSize > 0) {
        std::wstring raw(rawSize / sizeof(wchar_t), L'\0');
        if (WinHttpQueryHeaders(h, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, raw.data(), &rawSize, WINHTTP_NO_HEADER_INDEX)) {
            raw.resize(rawSize / sizeof(wchar_t));
            headers = parseRawHeaders(narrow(raw));
        }
    }
    std::string location;
    for (const auto& [k, v] : headers) {
        if (k == "location") {
            location = v;
            break;
        }
    }
    const int code = static_cast<int>(status);
    if (isRedirect(code) && !location.empty()) {
        if (op->res.redirects >= op->req.maxRedirects) {
            fail(op, 0, "too many redirects");
            return;
        }
        Url next;
        std::string why;
        if (!parse_url(resolve_location(op->url, location), next, why)) {
            fail(op, 0, "bad redirect: " + why);
            return;
        }
        if (op->url.https && !next.https) {
            fail(op, 0, "refused a redirect from https to http");
            return;
        }
        if (next.host != op->url.host) {
            op->headers.erase(std::remove_if(op->headers.begin(), op->headers.end(),
                                             [](const auto& x) {
                                                 const std::string n = lower(x.first);
                                                 return n == "authorization" || n == "cookie" || n == "proxy-authorization";
                                             }),
                              op->headers.end());
        }
        if (code == 303 || ((code == 301 || code == 302) && op->method == "POST")) {
            op->method = "GET";
            op->body.clear();
            op->headers.erase(std::remove_if(op->headers.begin(), op->headers.end(),
                                             [](const auto& x) {
                                                 const std::string n = lower(x.first);
                                                 return n == "content-type" || n == "content-length";
                                             }),
                              op->headers.end());
        }
        op->url = next;
        {
            std::lock_guard<std::mutex> lock(op->mutex);
            ++op->res.redirects;
            op->res.body.clear();
        }
        closeHandles(op);  // the old request is done; `h` must not be used any more
        op->impl->startHop(op);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(op->mutex);
        op->res.status = code;
        op->res.headers = std::move(headers);
    }
    if (!WinHttpQueryDataAvailable(h, nullptr)) fail(op, GetLastError());
}

void CALLBACK Impl::statusCallback(HINTERNET h, DWORD_PTR context, DWORD status, LPVOID info, DWORD infoLength) {
    Op* op = reinterpret_cast<Op*>(context);
    if (op == nullptr) return;
    if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
        op->release();
        return;
    }
    if (op->finished) return;
    {
        std::lock_guard<std::mutex> lock(op->mutex);
        if (h != op->request) return;  // a request we already replaced (redirect)
    }
    switch (status) {
        case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
            if (!WinHttpReceiveResponse(h, nullptr)) fail(op, GetLastError());
            break;
        case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
            onHeaders(op, h);
            break;
        case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE: {
            const DWORD size = info ? *static_cast<DWORD*>(info) : 0;
            if (size == 0) {
                {
                    std::lock_guard<std::mutex> lock(op->mutex);
                    op->res.ok = true;
                }
                op->impl->finish(op);
                break;
            }
            bool tooBig;
            {
                std::lock_guard<std::mutex> lock(op->mutex);
                tooBig = op->res.body.size() + size > op->req.maxBytes;
            }
            if (tooBig) {
                fail(op, 0, "response larger than " + std::to_string(op->req.maxBytes) + " bytes");
                break;
            }
            op->chunk.resize(std::min<DWORD>(size, 64 * 1024));
            if (!WinHttpReadData(h, op->chunk.data(), static_cast<DWORD>(op->chunk.size()), nullptr)) fail(op, GetLastError());
            break;
        }
        case WINHTTP_CALLBACK_STATUS_READ_COMPLETE: {
            {
                std::lock_guard<std::mutex> lock(op->mutex);
                op->res.body.append(op->chunk.data(), infoLength);
            }
            if (!WinHttpQueryDataAvailable(h, nullptr)) fail(op, GetLastError());
            break;
        }
        case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR: {
            const WINHTTP_ASYNC_RESULT* r = static_cast<const WINHTTP_ASYNC_RESULT*>(info);
            fail(op, r ? r->dwError : ERROR_WINHTTP_CONNECTION_ERROR);
            break;
        }
        default: break;
    }
}

class WinHttpClient final : public Client {
public:
    explicit WinHttpClient(const ClientOptions& options) : impl_(new Impl()) {
        impl_->options = options;
        impl_->options.maxInFlight = std::clamp(options.maxInFlight, 1, 256);
        std::wstring ua = widen(options.userAgent);
        impl_->session = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
        if (!impl_->session) {
            impl_->session = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
        }
        impl_->service = std::thread([p = impl_] { p->serviceMain(); });
    }

    ~WinHttpClient() override {
        Impl* p = impl_;
        std::vector<std::uint64_t> ids;
        {
            std::unique_lock<std::mutex> lock(p->m);
            p->stopping = true;
            for (const auto& [id, op] : p->ops) ids.push_back(id);
        }
        for (std::uint64_t id : ids) p->abort(id, false);
        {
            std::unique_lock<std::mutex> lock(p->m);
            p->idle.wait_for(lock, std::chrono::seconds(5), [&] { return p->ops.empty(); });
        }
        {
            std::lock_guard<std::mutex> lock(p->sm);
            p->stopService = true;
        }
        p->scv.notify_all();
        if (p->service.joinable()) p->service.join();
        if (p->session) WinHttpCloseHandle(p->session);
        delete p;  // late HANDLE_CLOSING callbacks only touch their Op, never the Impl
    }

    std::uint64_t submit(Request request, Done done) override {
        Impl* p = impl_;
        Op* op = new Op();
        op->impl = p;
        op->id = p->nextId++;
        op->done = std::move(done);
        op->userAgent = request.userAgent.empty() ? p->options.userAgent : request.userAgent;
        op->method = request.method.empty() ? "GET" : request.method;
        for (char& c : op->method) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        op->headers = request.headers;
        op->body = request.body;
        op->req = std::move(request);
        const std::uint64_t id = op->id;

        std::string problem;
        if (!parse_url(op->req.url, op->url, problem)) {
            // delivered from the service thread, like every other early failure
        }
        for (const auto& [name, value] : op->headers) {
            const std::string n = lower(name);
            if (!validHeaderName(name) || !validHeaderValue(value)) problem = "invalid header '" + name + "'";
            else if (n == "host" || n == "content-length" || n == "connection") problem = "the header '" + name + "' cannot be set";
        }
        bool start = false;
        {
            std::lock_guard<std::mutex> lock(p->m);
            p->ops[id] = op;
            if (p->stopping) problem = "the client is shutting down";
            if (p->running < p->options.maxInFlight || !problem.empty()) {
                ++p->running;
                start = true;
            } else {
                p->waiting.push_back(op);
            }
        }
        if (start) {
            if (!problem.empty()) {
                p->post([p, op, problem] {
                    op->started = Clock::now();
                    fail(op, 0, problem);
                });
            } else {
                p->post([p, op] { p->startOp(op); });
            }
        }
        return id;
    }

    bool cancel(std::uint64_t id) override {
        {
            std::lock_guard<std::mutex> lock(impl_->m);
            if (impl_->ops.find(id) == impl_->ops.end()) return false;
        }
        impl_->abort(id, false);
        return true;
    }

    std::size_t in_flight() const override {
        std::lock_guard<std::mutex> lock(impl_->m);
        return impl_->ops.size();
    }

private:
    Impl* impl_;
};

}  // namespace

std::unique_ptr<Client> Client::create_winhttp(const ClientOptions& options) { return std::make_unique<WinHttpClient>(options); }

}  // namespace httpc

#else  // not Windows

namespace httpc {
std::unique_ptr<Client> Client::create_winhttp(const ClientOptions&) { return nullptr; }
}  // namespace httpc

#endif
