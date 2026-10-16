#include "common/test_run.h"

#include <algorithm>
#include <cstdio>

namespace spass::testrun {

std::string jsonString(std::string_view s) {
    std::string out = "\"";
    const auto n = s.size();
    for (std::size_t i = 0; i < n;) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == '"') {
            out += "\\\"";
        } else if (c == '\\') {
            out += "\\\\";
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else if (c == '\t') {
            out += "\\t";
        } else if (c < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\u%04x", c);
            out += buf;
        } else if (c < 0x80) {
            out += static_cast<char>(c);
        } else {
            // a multi-byte UTF-8 sequence is copied only when it is complete and well-formed
            std::size_t len = 0;
            if (c >= 0xC2 && c <= 0xDF) len = 2;
            else if (c >= 0xE0 && c <= 0xEF) len = 3;
            else if (c >= 0xF0 && c <= 0xF4) len = 4;
            bool valid = len != 0 && i + len <= n;
            for (std::size_t k = 1; valid && k < len; ++k) {
                valid = (static_cast<unsigned char>(s[i + k]) & 0xC0) == 0x80;
            }
            if (valid) {
                out.append(s.substr(i, len));
                i += len;
                continue;
            }
            out += '?';
        }
        ++i;
    }
    out += '"';
    return out;
}

namespace {
std::string number(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.3f", v);
    return buf;
}
}  // namespace

void Run::reset() {
    std::lock_guard lock(mutex_);
    scenario_ = "unnamed";
    section_.clear();
    checks_.clear();
    planned_.clear();
    marks_.clear();
    waits_.clear();
    nextWait_ = 1;
}

void Run::setScenario(const std::string& name) {
    std::lock_guard lock(mutex_);
    scenario_ = name.empty() ? "unnamed" : name;
}

std::string Run::scenario() const {
    std::lock_guard lock(mutex_);
    return scenario_;
}

void Run::section(const std::string& name) {
    std::lock_guard lock(mutex_);
    section_ = name;
}

std::string Run::currentSection() const {
    std::lock_guard lock(mutex_);
    return section_;
}

bool Run::expect(const std::string& name, bool ok, const std::string& detail, double t) {
    std::lock_guard lock(mutex_);
    Check c;
    c.name = name.empty() ? "check " + std::to_string(checks_.size() + 1) : name;
    c.ok = ok;
    c.detail = detail;
    c.section = section_;
    c.t = t;
    checks_.push_back(std::move(c));
    return ok;
}

void Run::plan(const std::vector<std::string>& names) {
    std::lock_guard lock(mutex_);
    for (const auto& n : names) {
        if (std::find(planned_.begin(), planned_.end(), n) == planned_.end()) {
            planned_.push_back(n);
        }
    }
}

void Run::mark(const std::string& name, double t) {
    std::lock_guard lock(mutex_);
    marks_.emplace(name, t);  // the first occurrence counts
}

std::uint64_t Run::beginWait(const std::string& description) {
    std::lock_guard lock(mutex_);
    const std::uint64_t id = nextWait_++;
    waits_[id] = description;
    return id;
}

void Run::endWait(std::uint64_t id) {
    std::lock_guard lock(mutex_);
    waits_.erase(id);
}

int Run::total() const {
    std::lock_guard lock(mutex_);
    return static_cast<int>(checks_.size());
}

int Run::failed() const {
    std::lock_guard lock(mutex_);
    return static_cast<int>(std::count_if(checks_.begin(), checks_.end(), [](const Check& c) { return !c.ok; }));
}

std::vector<std::string> Run::unfinishedLocked() const {
    std::vector<std::string> out;
    for (const auto& p : planned_) {
        const bool ran = std::any_of(checks_.begin(), checks_.end(), [&](const Check& c) { return c.name == p; });
        if (!ran) {
            out.push_back(p);
        }
    }
    for (const auto& [id, description] : waits_) {
        out.push_back("waiting: " + description);
    }
    return out;
}

std::vector<std::string> Run::unfinished() const {
    std::lock_guard lock(mutex_);
    return unfinishedLocked();
}

Verdict Run::verdictLocked(const std::string& abortReason) const {
    Verdict v;
    const auto failedCount = std::count_if(checks_.begin(), checks_.end(), [](const Check& c) { return !c.ok; });
    const auto open = unfinishedLocked();
    std::string reason;
    if (!abortReason.empty()) {
        reason = abortReason;
    }
    if (failedCount > 0) {
        reason += (reason.empty() ? "" : "; ") + std::to_string(failedCount) + " check(s) failed";
    }
    if (!open.empty()) {
        reason += (reason.empty() ? "" : "; ") + std::to_string(open.size()) + " unfinished";
    }
    if (checks_.empty() && reason.empty()) {
        reason = "no check was recorded";
    }
    v.pass = reason.empty();
    v.reason = reason;
    return v;
}

Verdict Run::verdict(const std::string& abortReason) const {
    std::lock_guard lock(mutex_);
    return verdictLocked(abortReason);
}

void Run::appendFrom(const Run& other, const std::string& prefix, const std::string& abortReason, double t) {
    std::vector<Check> checks;
    std::vector<std::string> open;
    {
        std::lock_guard lock(other.mutex_);
        checks = other.checks_;
        open = other.unfinishedLocked();
    }
    std::lock_guard lock(mutex_);
    for (Check c : checks) {
        c.name = prefix + ": " + c.name;
        checks_.push_back(std::move(c));
    }
    const auto add = [&](const std::string& name, const std::string& detail) {
        Check c;
        c.name = prefix + ": " + name;
        c.ok = false;
        c.detail = detail;
        c.section = prefix;
        c.t = t;
        checks_.push_back(std::move(c));
    };
    for (const std::string& o : open) {
        add("unfinished", o);
    }
    if (!abortReason.empty()) {
        add("scenario ended early", abortReason);
    }
    if (checks.empty() && open.empty() && abortReason.empty()) {
        add("no check was recorded", "the scenario recorded nothing");
    }
}

std::string Run::toJson(const std::string& abortReason, const std::string& summary, double elapsed) const {
    std::lock_guard lock(mutex_);
    const Verdict v = verdictLocked(abortReason);
    const auto open = unfinishedLocked();
    const auto failedCount = std::count_if(checks_.begin(), checks_.end(), [](const Check& c) { return !c.ok; });
    std::string j = "{\n";
    j += "  \"scenario\": " + jsonString(scenario_) + ",\n";
    j += std::string("  \"status\": ") + (v.pass ? "\"pass\"" : "\"fail\"") + ",\n";
    j += "  \"reason\": " + jsonString(v.reason) + ",\n";
    j += "  \"summary\": " + jsonString(summary) + ",\n";
    j += "  \"elapsed_s\": " + number(elapsed) + ",\n";
    j += "  \"section\": " + jsonString(section_) + ",\n";
    j += "  \"marks\": {";
    bool first = true;
    for (const auto& [name, t] : marks_) {
        j += std::string(first ? "" : ", ") + jsonString(name) + ": " + number(t);
        first = false;
    }
    j += "},\n";
    j += "  \"counts\": {\"total\": " + std::to_string(checks_.size()) +
         ", \"passed\": " + std::to_string(checks_.size() - static_cast<std::size_t>(failedCount)) +
         ", \"failed\": " + std::to_string(failedCount) + "},\n";
    j += "  \"unfinished\": [";
    for (std::size_t i = 0; i < open.size(); ++i) {
        j += (i ? ", " : "") + jsonString(open[i]);
    }
    j += "],\n";
    j += "  \"checks\": [";
    for (std::size_t i = 0; i < checks_.size(); ++i) {
        const Check& c = checks_[i];
        j += i ? ",\n    " : "\n    ";
        j += "{\"name\": " + jsonString(c.name) + ", \"ok\": " + (c.ok ? "true" : "false") + ", \"detail\": " + jsonString(c.detail) +
             ", \"section\": " + jsonString(c.section) + ", \"t\": " + number(c.t) + "}";
    }
    j += checks_.empty() ? "]\n" : "\n  ]\n";
    j += "}\n";
    return j;
}

}  // namespace spass::testrun
