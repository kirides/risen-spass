#include "common/link.h"

#include <windows.h>
#include <sddl.h>

#include <atomic>
#include <deque>
#include <mutex>
#include <thread>

#include <spdlog/spdlog.h>

namespace spass::link {
namespace {

constexpr std::size_t kMaxMessage = 4u << 20;
constexpr std::size_t kMaxOutbox = 8u << 20;

struct State {
    std::mutex m;
    std::deque<std::string> outbox;
    std::size_t outboxBytes = 0;
    Inbox inbox;
    std::string path;
    std::atomic<bool> running{false};
    std::atomic<bool> connected{false};
    HANDLE stopEvent = nullptr;
    HANDLE outEvent = nullptr;
    std::thread thread;
};

// Intentionally leaked: the I/O thread must never see a destroyed object at process exit.
State& st() {
    static State* s = new State();
    return *s;
}

std::wstring widen(const std::string& s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

HANDLE createPipe(const std::wstring& path, bool first) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    PSECURITY_DESCRIPTOR sd = nullptr;
    // protected DACL: only the owner of the process (the player) may open the pipe
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;OW)", SDDL_REVISION_1, &sd, nullptr)) {
        return INVALID_HANDLE_VALUE;
    }
    sa.lpSecurityDescriptor = sd;
    const DWORD flags = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0);
    HANDLE h = CreateNamedPipeW(path.c_str(), flags, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 64 * 1024, 64 * 1024, 0, &sa);
    LocalFree(sd);
    return h;
}

// Waits for `ov` (or the stop event). true when the operation finished; `bytes` is what it transferred, `error` the Win32 error (0 = ok).
bool waitFor(HANDLE pipe, OVERLAPPED& ov, HANDLE stop, DWORD timeoutMs, DWORD& bytes, DWORD& error) {
    HANDLE waits[2] = {ov.hEvent, stop};
    const DWORD w = WaitForMultipleObjects(2, waits, FALSE, timeoutMs);
    if (w != WAIT_OBJECT_0) {
        CancelIoEx(pipe, &ov);
        GetOverlappedResult(pipe, &ov, &bytes, TRUE);
        error = w == WAIT_TIMEOUT ? WAIT_TIMEOUT : ERROR_OPERATION_ABORTED;
        return false;
    }
    error = GetOverlappedResult(pipe, &ov, &bytes, FALSE) ? 0 : GetLastError();
    return true;
}

void serve(HANDLE pipe, HANDLE connEvent, HANDLE readEvent, HANDLE writeEvent) {
    State& s = st();
    OVERLAPPED ov{};
    ov.hEvent = connEvent;
    if (!ConnectNamedPipe(pipe, &ov)) {
        const DWORD e = GetLastError();
        if (e == ERROR_IO_PENDING) {
            DWORD bytes = 0, err = 0;
            if (!waitFor(pipe, ov, s.stopEvent, INFINITE, bytes, err) || err != 0) {
                return;
            }
        } else if (e != ERROR_PIPE_CONNECTED) {
            return;
        }
    }
    {
        std::lock_guard lock(s.m);
        s.outbox.clear();
        s.outboxBytes = 0;
        ++s.inbox.connects;
    }
    s.connected = true;
    spdlog::info("link: client connected");

    std::string message;
    std::vector<char> buf(64 * 1024);
    OVERLAPPED rov{};
    rov.hEvent = readEvent;
    bool readPending = false;
    for (;;) {
        if (!readPending) {
            ResetEvent(readEvent);
            if (!ReadFile(pipe, buf.data(), static_cast<DWORD>(buf.size()), nullptr, &rov) && GetLastError() != ERROR_IO_PENDING) {
                if (GetLastError() != ERROR_MORE_DATA) {
                    break;
                }
            }
            readPending = true;
        }
        HANDLE waits[3] = {readEvent, s.outEvent, s.stopEvent};
        const DWORD w = WaitForMultipleObjects(3, waits, FALSE, INFINITE);
        if (w == WAIT_OBJECT_0 + 2) {
            CancelIoEx(pipe, &rov);
            DWORD b = 0;
            GetOverlappedResult(pipe, &rov, &b, TRUE);
            break;
        }
        if (w == WAIT_OBJECT_0) {
            DWORD got = 0;
            const BOOL ok = GetOverlappedResult(pipe, &rov, &got, FALSE);
            const DWORD err = ok ? 0 : GetLastError();
            readPending = false;
            if (!ok && err != ERROR_MORE_DATA) {
                break;  // the client went away
            }
            message.append(buf.data(), got);
            if (message.size() > kMaxMessage) {
                spdlog::warn("link: message over 4 MB, disconnecting the client");
                break;
            }
            if (ok) {
                std::lock_guard lock(s.m);
                s.inbox.messages.push_back(std::move(message));
                message.clear();
            }
            continue;
        }
        if (w == WAIT_OBJECT_0 + 1) {
            for (;;) {
                std::string out;
                {
                    std::lock_guard lock(s.m);
                    if (s.outbox.empty()) break;
                    out = std::move(s.outbox.front());
                    s.outbox.pop_front();
                    s.outboxBytes -= std::min(s.outboxBytes, out.size());
                }
                OVERLAPPED wov{};
                wov.hEvent = writeEvent;
                ResetEvent(writeEvent);
                if (!WriteFile(pipe, out.data(), static_cast<DWORD>(out.size()), nullptr, &wov) && GetLastError() != ERROR_IO_PENDING) {
                    goto done;
                }
                DWORD bytes = 0, err = 0;
                if (!waitFor(pipe, wov, s.stopEvent, 5000, bytes, err) || err != 0) {
                    goto done;  // a client that does not read for 5 s is dropped
                }
            }
        }
    }
done:
    CancelIoEx(pipe, nullptr);
    s.connected = false;
    {
        std::lock_guard lock(s.m);
        s.outbox.clear();
        s.outboxBytes = 0;
        ++s.inbox.disconnects;
    }
    DisconnectNamedPipe(pipe);
    spdlog::info("link: client disconnected");
}

void ioMain(std::wstring path) {
    State& s = st();
    HANDLE connEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE readEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE writeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    while (WaitForSingleObject(s.stopEvent, 0) != WAIT_OBJECT_0) {
        HANDLE pipe = createPipe(path, false);
        if (pipe == INVALID_HANDLE_VALUE) {
            spdlog::warn("link: cannot create the pipe again (error {})", GetLastError());
            if (WaitForSingleObject(s.stopEvent, 2000) == WAIT_OBJECT_0) break;
            continue;
        }
        ResetEvent(connEvent);
        serve(pipe, connEvent, readEvent, writeEvent);
        CloseHandle(pipe);
    }
    CloseHandle(connEvent);
    CloseHandle(readEvent);
    CloseHandle(writeEvent);
}

}  // namespace

bool start(const std::string& name, std::string& error) {
    State& s = st();
    if (s.running) {
        return true;
    }
    for (char c : name) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.')) {
            error = "invalid pipe name (letters, digits, - _ . only)";
            return false;
        }
    }
    if (name.empty() || name.size() > 100) {
        error = "empty or too long pipe name";
        return false;
    }
    const std::wstring path = L"\\\\.\\pipe\\" + widen(name);
    // claim the name (FILE_FLAG_FIRST_PIPE_INSTANCE): fails when another process already owns it, so nobody can squat on it
    HANDLE first = createPipe(path, true);
    if (first == INVALID_HANDLE_VALUE) {
        error = "cannot create the pipe (error " + std::to_string(GetLastError()) + "), is the name in use?";
        return false;
    }
    CloseHandle(first);
    s.path = "\\\\.\\pipe\\" + name;
    s.stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    s.outEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    s.running = true;
    try {
        s.thread = std::thread(ioMain, path);
    } catch (...) {
        s.running = false;
        error = "cannot start the I/O thread";
        return false;
    }
    return true;
}

void stop() {
    State& s = st();
    if (!s.running.exchange(false)) {
        return;
    }
    SetEvent(s.stopEvent);
    if (s.thread.joinable()) {
        s.thread.join();
    }
    CloseHandle(s.stopEvent);
    CloseHandle(s.outEvent);
    s.stopEvent = s.outEvent = nullptr;
    s.connected = false;
    s.path.clear();
}

bool running() { return st().running; }
std::string pipePath() { return st().path; }
bool connected() { return st().connected; }

void poll(Inbox& out) {
    State& s = st();
    std::lock_guard lock(s.m);
    for (std::string& m : s.inbox.messages) {
        out.messages.push_back(std::move(m));
    }
    out.connects += s.inbox.connects;
    out.disconnects += s.inbox.disconnects;
    s.inbox = Inbox{};
}

bool send(const std::string& message) {
    State& s = st();
    if (!s.running || !s.connected || message.size() > kMaxMessage) {
        return false;
    }
    {
        std::lock_guard lock(s.m);
        if (s.outboxBytes + message.size() > kMaxOutbox) {
            return false;
        }
        s.outbox.push_back(message);
        s.outboxBytes += message.size();
    }
    SetEvent(s.outEvent);
    return true;
}

}  // namespace spass::link
