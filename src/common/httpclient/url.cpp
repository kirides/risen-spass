#include "httpclient.h"

#include <algorithm>
#include <cctype>
#include <future>

namespace httpc {
namespace {

std::string lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

}  // namespace

std::string Response::header(const std::string& lowerName) const {
    for (const auto& [k, v] : headers) {
        if (k == lowerName) return v;
    }
    return {};
}

bool parse_url(const std::string& url, Url& out, std::string& error) {
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
    const std::size_t end = url.find_first_of("/?#", at);
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

bool host_allowed(const std::string& host, const std::vector<std::string>& patterns) {
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

std::string to_string(const Url& url) {
    const bool defaultPort = url.port == (url.https ? 443 : 80);
    return std::string(url.https ? "https://" : "http://") + url.host + (defaultPort ? std::string() : ":" + std::to_string(url.port)) + url.path;
}

std::string resolve_location(const Url& base, const std::string& location) {
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

// ---- helpers ---------------------------------------------------------------------------------------------------------------------------

Response perform(Client& client, Request request) {
    // shared state: the callback may run after a (rare) early return of this function
    auto promise = std::make_shared<std::promise<Response>>();
    std::future<Response> future = promise->get_future();
    client.submit(std::move(request), [promise](std::uint64_t, Response r) { promise->set_value(std::move(r)); });
    return future.get();
}

Response perform(Request request) {
    static Client* shared = Client::create_winhttp().release();  // never destroyed: workers may outlive main()
    if (shared == nullptr) {
        Response r;
        r.error = "no HTTP backend on this platform";
        return r;
    }
    return perform(*shared, std::move(request));
}

Done CompletionQueue::sink() {
    std::shared_ptr<State> s = state_;
    return [s](std::uint64_t id, Response r) {
        std::lock_guard<std::mutex> lock(s->m);
        s->items.push_back(Completed{id, std::move(r)});
    };
}

void CompletionQueue::poll(std::vector<Completed>& out) {
    std::lock_guard<std::mutex> lock(state_->m);
    for (Completed& c : state_->items) {
        out.push_back(std::move(c));
    }
    state_->items.clear();
}

bool CompletionQueue::empty() const {
    std::lock_guard<std::mutex> lock(state_->m);
    return state_->items.empty();
}

}  // namespace httpc
