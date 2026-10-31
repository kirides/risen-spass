#include "common/http.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <deque>
#include <mutex>
#include <thread>

namespace spass::http {
namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) {
        return {};
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::string lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string winhttpError(DWORD code) {
    switch (code) {
        case ERROR_WINHTTP_TIMEOUT: return "timed out";
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

struct Handle {
    HINTERNET h = nullptr;
    Handle() = default;
    explicit Handle(HINTERNET v) : h(v) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() {
        if (h) WinHttpCloseHandle(h);
    }
    explicit operator bool() const { return h != nullptr; }
};

bool validHeaderText(const std::string& s) {
    return s.find('\r') == std::string::npos && s.find('\n') == std::string::npos && s.find('\0') == std::string::npos;
}

bool validHeaderName(const std::string& s) {
    if (s.empty()) {
        return false;
    }
    for (char c : s) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')) {
            return false;
        }
    }
    return true;
}

struct Hop {
    int status = 0;
    Headers headers;
    std::string body;
    std::string error;
    bool ok = false;
};

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

Hop doHop(HINTERNET session, const Url& url, const std::string& method, const Headers& headers, const std::string& body, const Request& req,
          ULONGLONG deadline) {
    Hop hop;
    Handle connect(WinHttpConnect(session, widen(url.host).c_str(), static_cast<INTERNET_PORT>(url.port), 0));
    if (!connect) {
        hop.error = winhttpError(GetLastError());
        return hop;
    }
    Handle request(WinHttpOpenRequest(connect.h, widen(method).c_str(), widen(url.path).c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, url.https ? WINHTTP_FLAG_SECURE : 0));
    if (!request) {
        hop.error = winhttpError(GetLastError());
        return hop;
    }
    DWORD disable = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    WinHttpSetOption(request.h, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable));
    DWORD decompress = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(request.h, WINHTTP_OPTION_DECOMPRESSION, &decompress, sizeof(decompress));  // Windows 8.1+; ignored when unsupported

    std::string headerText;
    bool hasUa = false;
    for (const auto& [name, value] : headers) {
        if (lower(name) == "user-agent") hasUa = true;
        headerText += name + ": " + value + "\r\n";
    }
    if (!hasUa) {
        headerText += "User-Agent: " + req.userAgent + "\r\n";
    }
    const std::wstring wideHeaders = widen(headerText);
    const DWORD bodyLen = static_cast<DWORD>(body.size());
    if (!WinHttpSendRequest(request.h, wideHeaders.c_str(), static_cast<DWORD>(wideHeaders.size()), bodyLen ? const_cast<char*>(body.data()) : WINHTTP_NO_REQUEST_DATA,
                            bodyLen, bodyLen, 0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) {
        hop.error = winhttpError(GetLastError());
        return hop;
    }
    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    hop.status = static_cast<int>(status);

    DWORD rawSize = 0;
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &rawSize, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && rawSize > 0) {
        std::wstring raw(rawSize / sizeof(wchar_t), L'\0');
        if (WinHttpQueryHeaders(request.h, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, raw.data(), &rawSize, WINHTTP_NO_HEADER_INDEX)) {
            raw.resize(rawSize / sizeof(wchar_t));
            hop.headers = parseRawHeaders(narrow(raw));
        }
    }

    for (;;) {
        if (GetTickCount64() > deadline) {
            hop.error = "timed out";
            return hop;
        }
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(request.h, &avail)) {
            hop.error = winhttpError(GetLastError());
            return hop;
        }
        if (avail == 0) {
            break;
        }
        if (hop.body.size() + avail > req.maxBytes) {
            hop.error = "response larger than " + std::to_string(req.maxBytes) + " bytes";
            return hop;
        }
        const std::size_t old = hop.body.size();
        hop.body.resize(old + avail);
        DWORD got = 0;
        if (!WinHttpReadData(request.h, hop.body.data() + old, avail, &got)) {
            hop.error = winhttpError(GetLastError());
            return hop;
        }
        hop.body.resize(old + got);
        if (got == 0) {
            break;
        }
    }
    hop.ok = true;
    return hop;
}

}  // namespace

std::string Response::header(const std::string& lowerName) const {
    for (const auto& [k, v] : headers) {
        if (k == lowerName) return v;
    }
    return {};
}

bool parseUrl(const std::string& url, Url& out, std::string& error) {
    if (url.empty() || url.size() > 4096) {
        error = "empty or too long URL";
        return false;
    }
    for (char c : url) {
        if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7f) {
            error = "the URL contains spaces or control characters (encode them)";
            return false;
        }
    }
    const std::string l = lower(url);
    std::size_t at;
    if (l.rfind("https://", 0) == 0) {
        out.https = true;
        at = 8;
    } else if (l.rfind("http://", 0) == 0) {
        out.https = false;
        at = 7;
    } else {
        error = "only http:// and https:// URLs are supported";
        return false;
    }
    std::size_t end = url.find_first_of("/?#", at);
    const std::string authority = url.substr(at, end == std::string::npos ? std::string::npos : end - at);
    std::string rest = end == std::string::npos ? std::string() : url.substr(end);
    if (const std::size_t hash = rest.find('#'); hash != std::string::npos) {
        rest.erase(hash);
    }
    if (authority.find('@') != std::string::npos) {
        error = "credentials inside the URL are not supported (use the Authorization header)";
        return false;
    }
    std::string host = authority;
    int port = out.https ? 443 : 80;
    if (!host.empty() && host.front() == '[') {
        error = "IPv6 literals are not supported";
        return false;
    }
    if (const std::size_t colon = host.rfind(':'); colon != std::string::npos) {
        const std::string p = host.substr(colon + 1);
        host.erase(colon);
        if (p.empty() || p.size() > 5 || !std::all_of(p.begin(), p.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
            error = "invalid port";
            return false;
        }
        port = std::stoi(p);
        if (port < 1 || port > 65535) {
            error = "invalid port";
            return false;
        }
    }
    if (host.empty()) {
        error = "the URL has no host";
        return false;
    }
    for (char c : host) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_')) {
            error = "invalid character in the host name";
            return false;
        }
    }
    out.host = lower(host);
    out.port = port;
    out.path = rest.empty() ? "/" : (rest.front() == '/' ? rest : "/" + rest);
    return true;
}

bool hostAllowed(const std::string& host, const std::vector<std::string>& patterns) {
    if (patterns.empty()) {
        return true;
    }
    const std::string h = lower(host);
    for (const std::string& raw : patterns) {
        const std::string p = lower(raw);
        if (p.rfind("*.", 0) == 0) {
            const std::string suffix = p.substr(1);  // ".example.com"
            if (h.size() > suffix.size() && h.compare(h.size() - suffix.size(), suffix.size(), suffix) == 0) {
                return true;
            }
        } else if (p == h) {
            return true;
        }
    }
    return false;
}

std::string resolveLocation(const Url& base, const std::string& location) {
    const std::string l = lower(location);
    if (l.rfind("http://", 0) == 0 || l.rfind("https://", 0) == 0) {
        return location;
    }
    const bool defaultPort = base.port == (base.https ? 443 : 80);
    const std::string origin = std::string(base.https ? "https://" : "http://") + base.host + (defaultPort ? "" : ":" + std::to_string(base.port));
    if (location.rfind("//", 0) == 0) {
        return std::string(base.https ? "https:" : "http:") + location;
    }
    if (!location.empty() && location.front() == '/') {
        return origin + location;
    }
    std::string dir = base.path.substr(0, base.path.find('?'));
    dir.erase(dir.rfind('/') + 1);
    return origin + dir + location;
}

Response perform(const Request& req) {
    const auto started = std::chrono::steady_clock::now();
    Response res;
    const auto finish = [&](Response& r) -> Response {
        r.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        return r;
    };

    Url url;
    if (!parseUrl(req.url, url, res.error)) {
        return finish(res);
    }
    for (const auto& [name, value] : req.headers) {
        if (!validHeaderName(name) || !validHeaderText(value)) {
            res.error = "invalid header '" + name + "'";
            return finish(res);
        }
        if (lower(name) == "host" || lower(name) == "content-length" || lower(name) == "connection") {
            res.error = "the header '" + name + "' cannot be set";
            return finish(res);
        }
    }
    const int timeout = std::clamp(req.timeoutMs, 500, 120000);
    const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(timeout) * 2;  // whole transfer: twice the per-phase timeout

    Handle session(WinHttpOpen(widen(req.userAgent).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        session.h = WinHttpOpen(widen(req.userAgent).c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (!session) {
        res.error = winhttpError(GetLastError());
        return finish(res);
    }
    WinHttpSetTimeouts(session.h, timeout, timeout, timeout, timeout);

    std::string method = req.method;
    Headers headers = req.headers;
    std::string body = req.body;
    const std::string origin = url.host;

    for (int hopIndex = 0;; ++hopIndex) {
        if (!hostAllowed(url.host, req.allowedHosts)) {
            res.error = "host '" + url.host + "' is not allowed for this mod";
            return finish(res);
        }
        Hop hop = doHop(session.h, url, method, headers, body, req, deadline);
        res.finalUrl = std::string(url.https ? "https://" : "http://") + url.host + (url.port == (url.https ? 443 : 80) ? std::string() : ":" + std::to_string(url.port)) + url.path;
        if (!hop.ok) {
            res.error = hop.error;
            return finish(res);
        }
        res.status = hop.status;
        res.headers = std::move(hop.headers);
        res.body = std::move(hop.body);
        const bool redirect = hop.status == 301 || hop.status == 302 || hop.status == 303 || hop.status == 307 || hop.status == 308;
        const std::string location = res.header("location");
        if (!redirect || location.empty()) {
            res.ok = true;
            return finish(res);
        }
        if (hopIndex >= req.maxRedirects) {
            res.error = "too many redirects";
            res.status = 0;
            return finish(res);
        }
        Url next;
        std::string why;
        if (!parseUrl(resolveLocation(url, location), next, why)) {
            res.error = "bad redirect: " + why;
            res.status = 0;
            return finish(res);
        }
        if (url.https && !next.https) {
            res.error = "refused a redirect from https to http";
            res.status = 0;
            return finish(res);
        }
        if (next.host != url.host) {
            headers.erase(std::remove_if(headers.begin(), headers.end(), [](const auto& h) {
                              const std::string n = lower(h.first);
                              return n == "authorization" || n == "cookie" || n == "proxy-authorization";
                          }),
                          headers.end());
        }
        if (hop.status == 303 || ((hop.status == 301 || hop.status == 302) && method == "POST")) {
            method = "GET";
            body.clear();
            headers.erase(std::remove_if(headers.begin(), headers.end(), [](const auto& h) {
                              const std::string n = lower(h.first);
                              return n == "content-type" || n == "content-length";
                          }),
                          headers.end());
        }
        url = next;
        ++res.redirects;
        res.headers.clear();
        res.body.clear();
    }
}

// ---- worker threads -------------------------------------------------------------------------------------------------------------------

namespace {

struct Job {
    std::uint64_t id;
    Request request;
};

// Deliberately leaked: a worker may still be inside WinHTTP when the process shuts down.
struct Pool {
    std::mutex m;
    std::condition_variable cv;
    std::deque<Job> jobs;
    std::vector<Completed> done;
    int workers = 0;
    int idle = 0;
    std::uint64_t nextId = 1;
    std::size_t submitted = 0;  // submitted - polled
};

Pool& pool() {
    static Pool* p = new Pool();
    return *p;
}

constexpr int kMaxWorkers = 4;

void workerMain() {
    Pool& p = pool();
    for (;;) {
        Job job;
        {
            std::unique_lock lock(p.m);
            ++p.idle;
            const bool got = p.cv.wait_for(lock, std::chrono::seconds(10), [&] { return !p.jobs.empty(); });
            --p.idle;
            if (!got) {
                --p.workers;
                return;
            }
            job = std::move(p.jobs.front());
            p.jobs.pop_front();
        }
        Completed c;
        c.id = job.id;
        try {
            c.response = perform(job.request);
        } catch (const std::exception& e) {
            c.response.error = std::string("internal error: ") + e.what();
        } catch (...) {
            c.response.error = "internal error";
        }
        std::lock_guard lock(p.m);
        p.done.push_back(std::move(c));
    }
}

}  // namespace

std::uint64_t submit(Request request) {
    Pool& p = pool();
    std::lock_guard lock(p.m);
    const std::uint64_t id = p.nextId++;
    p.jobs.push_back(Job{id, std::move(request)});
    ++p.submitted;
    if (p.idle < static_cast<int>(p.jobs.size()) && p.workers < kMaxWorkers) {
        ++p.workers;
        try {
            std::thread(workerMain).detach();
        } catch (...) {
            --p.workers;
        }
    }
    p.cv.notify_one();
    return id;
}

void poll(std::vector<Completed>& out) {
    Pool& p = pool();
    std::lock_guard lock(p.m);
    p.submitted -= std::min(p.submitted, p.done.size());
    for (Completed& c : p.done) {
        out.push_back(std::move(c));
    }
    p.done.clear();
}

std::size_t inFlight() {
    Pool& p = pool();
    std::lock_guard lock(p.m);
    return p.submitted;
}

}  // namespace spass::http
