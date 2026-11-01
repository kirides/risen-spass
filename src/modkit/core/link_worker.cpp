#include "core/link_worker.h"

#include <chrono>

namespace mk {

LinkWorker::LinkWorker() { thread_ = std::thread([this] { loop(); }); }

LinkWorker::~LinkWorker() {
    {
        std::lock_guard<std::mutex> lock(m_);
        stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void LinkWorker::set_game_dir(const std::string& dir) {
    {
        std::lock_guard<std::mutex> lock(m_);
        if (dir == game_dir_) return;
        game_dir_ = dir;
        dir_changed_ = true;
    }
    cv_.notify_all();
}

LinkWorker::Status LinkWorker::status() const {
    std::lock_guard<std::mutex> lock(m_);
    return status_;
}

void LinkWorker::request(const std::string& tag, const std::string& cmd, const nlohmann::json& args, int timeout_ms) {
    {
        std::lock_guard<std::mutex> lock(m_);
        if (!status_.connected) return;  // nobody to ask
        for (Pending& p : queue_) {
            if (p.tag == tag) {
                p.cmd = cmd;
                p.args = args;
                p.timeout_ms = timeout_ms;
                return;
            }
        }
        queue_.push_back(Pending{tag, cmd, args, timeout_ms});
    }
    cv_.notify_all();
}

std::vector<LinkWorker::Result> LinkWorker::take_results() {
    std::lock_guard<std::mutex> lock(m_);
    std::vector<Result> out;
    out.swap(results_);
    return out;
}

std::vector<LinkWorker::LogLine> LinkWorker::take_log() {
    std::lock_guard<std::mutex> lock(m_);
    std::vector<LogLine> out;
    out.swap(log_);
    return out;
}

std::vector<nlohmann::json> LinkWorker::take_events() {
    std::lock_guard<std::mutex> lock(m_);
    std::vector<nlohmann::json> out;
    out.swap(events_);
    return out;
}

void LinkWorker::set_log_level(const std::string& level) {
    {
        std::lock_guard<std::mutex> lock(m_);
        if (level == log_level_) return;
        log_level_ = level;
        level_dirty_ = true;
    }
    cv_.notify_all();
}

void LinkWorker::loop() {
    LinkClient client;
    auto next_attempt = std::chrono::steady_clock::now();
    for (;;) {
        std::string dir;
        bool changed = false;
        {
            std::unique_lock<std::mutex> lock(m_);
            if (stop_) return;
            if (queue_.empty() && !level_dirty_ && !dir_changed_) cv_.wait_for(lock, std::chrono::milliseconds(client.connected() ? 60 : 250));
            if (stop_) return;
            dir = game_dir_;
            changed = dir_changed_;
            dir_changed_ = false;
        }
        if (changed && client.connected()) {
            client.close();
            std::lock_guard<std::mutex> lock(m_);
            status_.connected = false;
            queue_.clear();
        }
        if (dir.empty()) {
            std::lock_guard<std::mutex> lock(m_);
            status_.connected = false;
            status_.note = "no game folder set (Window > Settings > Play)";
            continue;
        }
        if (!client.connected()) {
            if (std::chrono::steady_clock::now() < next_attempt) continue;
            next_attempt = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
            std::string error;
            if (!client.connect(dir, error)) {
                std::lock_guard<std::mutex> lock(m_);
                status_.connected = false;
                status_.note = error;
                continue;
            }
            const LinkReply hello = client.request("hello", nlohmann::json::object(), 5000);
            std::string level;
            {
                std::lock_guard<std::mutex> lock(m_);
                status_ = Status{};
                status_.connected = hello.ok;
                if (hello.ok) {
                    status_.pipe = hello.result.value("pipe", std::string());
                    status_.pid = hello.result.value("pid", 0);
                    status_.layer = hello.result.value("layer", std::string());
                    status_.session = hello.result.value("session", std::string());
                } else {
                    status_.note = "connected, but the game does not answer: " + hello.error;
                }
                static unsigned long long counter = 0;
                status_.connects = ++counter;
                level = log_level_;
                level_dirty_ = false;
            }
            if (!hello.ok) {
                client.close();
                continue;
            }
            if (level != "off") client.request("subscribe", {{"log", level}}, 3000);
            continue;
        }

        // the log level changed
        std::string level;
        bool send_level = false;
        {
            std::lock_guard<std::mutex> lock(m_);
            if (level_dirty_) {
                level = log_level_;
                level_dirty_ = false;
                send_level = true;
            }
        }
        if (send_level) client.request("subscribe", {{"log", level}}, 3000);

        // one queued request
        Pending p;
        bool have = false;
        {
            std::lock_guard<std::mutex> lock(m_);
            if (!queue_.empty()) {
                p = std::move(queue_.front());
                queue_.pop_front();
                have = true;
            }
        }
        if (have) {
            Result r;
            r.tag = p.tag;
            r.cmd = p.cmd;
            r.reply = client.request(p.cmd, p.args, p.timeout_ms);
            std::lock_guard<std::mutex> lock(m_);
            results_.push_back(std::move(r));
            if (results_.size() > 2000) results_.erase(results_.begin(), results_.begin() + 1000);
        }

        // what the game pushed (log lines and other events), also read by request() while it waited
        for (nlohmann::json& e : client.take_events()) {
            std::lock_guard<std::mutex> lock(m_);
            if (e.value("event", std::string()) == "log" && e.contains("data")) {
                log_.push_back(LogLine{e["data"].value("level", std::string("info")), e["data"].value("text", std::string())});
                if (log_.size() > 20000) log_.erase(log_.begin(), log_.begin() + 10000);
            } else {
                events_.push_back(std::move(e));
                if (events_.size() > 2000) events_.erase(events_.begin(), events_.begin() + 1000);
            }
        }
        if (!client.connected()) {  // the pipe closed (the game quit or restarted)
            std::lock_guard<std::mutex> lock(m_);
            status_.connected = false;
            status_.note = "the game closed the link";
            queue_.clear();
            next_attempt = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        }
    }
}

}  // namespace mk
