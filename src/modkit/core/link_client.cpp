#include "core/link_client.h"

#include <chrono>
#include <sstream>
#include <thread>

#include "core/i18n.h"
#include "core/paths.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace mk {

#ifdef _WIN32

bool LinkClient::connect(const std::string& game_dir, std::string& error) {
    const fs::path info = u8(game_dir) / "bin" / "spass" / "link.json";
    std::error_code ec;
    if (!fs::exists(info, ec)) {
        error = "the game does not run with the live link (config.toml [link] enabled = true)";
        return false;
    }
    try {
        const nlohmann::json j = nlohmann::json::parse(read_text(info));
        const std::string pipe = j.value("pipe", std::string());
        if (pipe.rfind("\\\\.\\pipe\\", 0) != 0) {
            error = "link.json names no pipe";
            return false;
        }
        return connect_pipe(pipe, error);
    } catch (const std::exception& e) {
        error = std::string("link.json: ") + e.what();
        return false;
    }
}

bool LinkClient::connect_pipe(const std::string& pipe_path, std::string& error) {
    close();
    HANDLE h = CreateFileA(pipe_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        error = "cannot open the pipe (error " + std::to_string(GetLastError()) + "): the game is gone or another editor is connected";
        return false;
    }
    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(h, &mode, nullptr, nullptr)) {
        error = "cannot switch the pipe to message mode";
        CloseHandle(h);
        return false;
    }
    handle_ = h;
    return true;
}

void LinkClient::close() {
    if (handle_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
    }
    events_.clear();
}

bool LinkClient::connected() const { return handle_ != nullptr; }

int LinkClient::read_message(nlohmann::json& out, int timeout_ms) {
    if (handle_ == nullptr) return -1;
    HANDLE h = static_cast<HANDLE>(handle_);
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) {
            close();
            return -1;
        }
        if (avail > 0) {
            // data is there, so ReadFile does not block (a client must not block on a synchronous handle: other threads could not write)
            std::string data;
            std::vector<char> buf(65536);
            for (;;) {
                DWORD got = 0;
                const BOOL ok = ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr);
                data.append(buf.data(), got);
                if (ok) break;
                if (GetLastError() != ERROR_MORE_DATA) {
                    close();
                    return -1;
                }
            }
            out = nlohmann::json::parse(data, nullptr, false);
            if (out.is_discarded()) out = nlohmann::json::object();
            return 1;
        }
        if (std::chrono::steady_clock::now() >= end) return 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

LinkReply LinkClient::request(const std::string& cmd, const nlohmann::json& args, int timeout_ms) {
    LinkReply r;
    if (handle_ == nullptr) {
        r.error = "not connected";
        return r;
    }
    const int id = next_id_++;
    const std::string text = nlohmann::json{{"id", id}, {"cmd", cmd}, {"args", args}}.dump();
    DWORD written = 0;
    if (!WriteFile(static_cast<HANDLE>(handle_), text.data(), static_cast<DWORD>(text.size()), &written, nullptr)) {
        close();
        r.error = "the game closed the link";
        return r;
    }
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(end - std::chrono::steady_clock::now()).count();
        if (left <= 0) {
            r.error = "timeout (the game did not answer)";
            return r;
        }
        nlohmann::json msg;
        const int got = read_message(msg, static_cast<int>(std::min<long long>(left, 200)));
        if (got < 0) {
            r.error = "the game closed the link";
            return r;
        }
        if (got == 0) continue;
        if (msg.contains("event")) {
            events_.push_back(std::move(msg));
            continue;
        }
        if (msg.contains("id") && msg["id"] == id) {
            r.ok = msg.value("ok", false);
            r.error = msg.value("error", std::string());
            if (msg.contains("result")) r.result = msg["result"];
            return r;
        }
    }
}

std::vector<nlohmann::json> LinkClient::take_events() {
    nlohmann::json msg;
    while (read_message(msg, 0) > 0) {
        if (msg.contains("event")) events_.push_back(std::move(msg));
    }
    std::vector<nlohmann::json> out;
    out.swap(events_);
    return out;
}

#else  // not Windows: the game only runs there

bool LinkClient::connect(const std::string&, std::string& error) {
    error = "the live link needs Windows";
    return false;
}
bool LinkClient::connect_pipe(const std::string&, std::string& error) {
    error = "the live link needs Windows";
    return false;
}
void LinkClient::close() { events_.clear(); }
bool LinkClient::connected() const { return false; }
int LinkClient::read_message(nlohmann::json&, int) { return -1; }
LinkReply LinkClient::request(const std::string&, const nlohmann::json&, int) {
    LinkReply r;
    r.error = "the live link needs Windows";
    return r;
}
std::vector<nlohmann::json> LinkClient::take_events() { return {}; }

#endif

LinkReload link_reload(const std::string& game_dir, const std::string& mod_id) {
    LinkReload out;
    LinkClient c;
    std::string error;
    if (!c.connect(game_dir, error)) return out;
    out.reachable = true;
    const LinkReply r = c.request("reload", {{"mod", mod_id}}, 15000);
    // what the game did to the mod: the first action of the first report
    std::string action, action_error;
    int added = 0, removed = 0;
    if (r.result.is_object() && r.result.contains("reports") && r.result["reports"].is_array() && !r.result["reports"].empty()) {
        const nlohmann::json& rep = r.result["reports"][0];
        if (rep.contains("actions") && rep["actions"].is_array() && !rep["actions"].empty()) {
            const nlohmann::json& a = rep["actions"][0];
            action = a.value("action", std::string());
            action_error = a.value("error", std::string());
            added = a.value("handlers_added", 0);
            removed = a.value("handlers_removed", 0);
        }
    }
    if (!r.ok && r.error.empty() && action.empty()) {
        out.message = tr("The game did not answer over the live link.");
    } else if (!r.ok || action == "failed") {
        const std::string why = !action_error.empty() ? action_error : r.error;
        out.message = trf("The game could not reload {0}: {1}", {mod_id, why});
    } else {
        out.ok = true;
        out.message = trf("{0} reloaded in the running game over the live link ({1} handlers removed, {2} added).", {mod_id, std::to_string(removed), std::to_string(added)});
    }
    return out;
}

std::string config_with_link(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    for (std::string l; std::getline(in, l);) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        lines.push_back(l);
    }
    auto trim = [](const std::string& s) {
        const size_t a = s.find_first_not_of(" \t");
        return a == std::string::npos ? std::string() : s.substr(a, s.find_last_not_of(" \t") - a + 1);
    };
    bool in_link = false, have_section = false, done = false;
    size_t header = 0;
    for (size_t i = 0; i < lines.size();) {
        const std::string t = trim(lines[i]);
        if (!t.empty() && t[0] == '[') {
            in_link = t == "[link]";
            if (in_link && !have_section) {
                have_section = true;
                header = i;
            }
        } else if (in_link && t.rfind("enabled", 0) == 0 && t.find('=') != std::string::npos) {
            if (t.find("true") != std::string::npos) done = true;
            lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        ++i;
    }
    (void)done;
    if (have_section) {
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(header) + 1, "enabled = true");
    } else {
        if (!lines.empty() && !trim(lines.back()).empty()) lines.push_back("");
        lines.push_back("[link]");
        lines.push_back("enabled = true");
    }
    std::string out;
    for (auto& l : lines) out += l + "\n";
    return out;
}

}  // namespace mk
