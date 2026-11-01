#include "common/pipeclient.h"

#include <windows.h>

namespace spass::pipeclient {

bool validName(const std::string& name) {
    if (name.empty() || name.size() > 100) {
        return false;
    }
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

Client::Client(std::string name, double retrySeconds) : name_(std::move(name)), retry_(retrySeconds < 1.0 ? 1.0 : retrySeconds) {
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    wakeEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

Client::~Client() {
    stop();
    if (stopEvent_) CloseHandle(stopEvent_);
    if (wakeEvent_) CloseHandle(wakeEvent_);
}

void Client::start() {
    if (!thread_.joinable() && stopEvent_ && wakeEvent_) {
        thread_ = std::thread([this] { run(); });
    }
}

void Client::stop() {
    if (thread_.joinable()) {
        SetEvent(stopEvent_);
        thread_.join();
    }
}

std::vector<std::string> Client::poll(std::size_t max) {
    std::vector<std::string> out;
    std::lock_guard lock(mutex_);
    while (!lines_.empty() && out.size() < max) {
        out.push_back(std::move(lines_.front()));
        lines_.pop_front();
    }
    return out;
}

std::size_t Client::pending() const {
    std::lock_guard lock(mutex_);
    return lines_.size();
}

bool Client::send(const std::string& text) {
    if (!connected_.load()) {
        return false;
    }
    {
        std::lock_guard lock(mutex_);
        if (outBytes_ + text.size() + 1 > kMaxOutBytes) {
            return false;
        }
        out_.push_back(text + "\n");
        outBytes_ += text.size() + 1;
    }
    SetEvent(wakeEvent_);
    return true;
}

void Client::pushLine(std::string line) {
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    if (line.empty()) {
        return;
    }
    if (lines_.size() >= kMaxLines) {
        ++dropped_;
        return;
    }
    lines_.push_back(std::move(line));
}

void Client::feed(const char* data, std::size_t n) {
    std::lock_guard lock(mutex_);
    for (std::size_t i = 0; i < n; ++i) {
        const char c = data[i];
        if (c == '\n') {
            if (discarding_) {
                discarding_ = false;
                ++dropped_;
            } else {
                pushLine(std::move(partial_));
            }
            partial_.clear();
        } else if (!discarding_) {
            if (partial_.size() >= kMaxLineBytes) {
                discarding_ = true;
                partial_.clear();
            } else {
                partial_.push_back(c);
            }
        }
    }
}

void Client::run() {
    const std::string path = "\\\\.\\pipe\\" + name_;
    HANDLE readEv = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE writeEv = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    const DWORD retryMs = static_cast<DWORD>(retry_ * 1000.0);

    while (WaitForSingleObject(stopEvent_, 0) != WAIT_OBJECT_0) {
        HANDLE h = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                               FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            if (WaitForSingleObject(stopEvent_, retryMs) == WAIT_OBJECT_0) {
                break;
            }
            continue;
        }
        {
            std::lock_guard lock(mutex_);
            partial_.clear();
            discarding_ = false;
            out_.clear();
            outBytes_ = 0;
        }
        ++connects_;
        connected_ = true;

        char buf[4096];
        bool alive = true;
        bool reading = false;
        OVERLAPPED ro{};
        ro.hEvent = readEv;
        while (alive) {
            if (!reading) {
                ResetEvent(readEv);
                DWORD got = 0;
                if (!ReadFile(h, buf, sizeof(buf), &got, &ro)) {
                    if (GetLastError() != ERROR_IO_PENDING) {
                        break;  // broken pipe: the server went away
                    }
                }
                reading = true;
            }
            HANDLE waits[3] = {stopEvent_, readEv, wakeEvent_};
            const DWORD w = WaitForMultipleObjects(3, waits, FALSE, INFINITE);
            if (w == WAIT_OBJECT_0) {
                CancelIo(h);
                alive = false;
                break;
            }
            if (w == WAIT_OBJECT_0 + 1) {
                DWORD got = 0;
                if (!GetOverlappedResult(h, &ro, &got, FALSE)) {
                    if (GetLastError() == ERROR_MORE_DATA) {
                        feed(buf, got);
                        reading = false;
                        continue;
                    }
                    break;
                }
                reading = false;
                if (got > 0) {
                    feed(buf, got);
                }
            }
            // anything to write (also after a read, the wake event is auto-reset so check the queue every time)
            for (;;) {
                std::string chunk;
                {
                    std::lock_guard lock(mutex_);
                    if (out_.empty()) {
                        break;
                    }
                    chunk = std::move(out_.front());
                    out_.pop_front();
                    outBytes_ -= chunk.size();
                }
                OVERLAPPED wo{};
                wo.hEvent = writeEv;
                ResetEvent(writeEv);
                DWORD wrote = 0;
                if (!WriteFile(h, chunk.data(), static_cast<DWORD>(chunk.size()), &wrote, &wo)) {
                    if (GetLastError() != ERROR_IO_PENDING) {
                        alive = false;
                        break;
                    }
                    // a server that stops reading must not hang the thread forever: give it 5 s
                    HANDLE ww[2] = {stopEvent_, writeEv};
                    const DWORD r = WaitForMultipleObjects(2, ww, FALSE, 5000);
                    if (r != WAIT_OBJECT_0 + 1) {
                        CancelIo(h);
                        alive = false;
                        break;
                    }
                    if (!GetOverlappedResult(h, &wo, &wrote, FALSE)) {
                        alive = false;
                        break;
                    }
                }
            }
        }
        if (reading) {
            CancelIo(h);
            DWORD ignored = 0;
            GetOverlappedResult(h, &ro, &ignored, TRUE);
        }
        connected_ = false;
        CloseHandle(h);
        if (WaitForSingleObject(stopEvent_, retryMs) == WAIT_OBJECT_0) {
            break;
        }
    }
    connected_ = false;
    CloseHandle(readEv);
    CloseHandle(writeEv);
}

}  // namespace spass::pipeclient
