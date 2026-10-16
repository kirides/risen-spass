#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace spass::testrun {

// Bookkeeping of one test run (risen.test): the recorded checks, the planned ones, pending waits and the machine-readable result.
// Pure logic (no engine, no Lua); thread-safe because the deadline watchdog reads it from its own thread.

// JSON string literal (with quotes). Control characters are escaped and bytes that are not valid UTF-8 become '?'.
std::string jsonString(std::string_view s);

struct Check {
    std::string name;
    bool ok = false;
    std::string detail;
    std::string section;
    double t = 0;  // seconds since the layer started
};

struct Verdict {
    bool pass = false;
    std::string reason;  // empty when passed
};

class Run {
public:
    void reset();
    void setScenario(const std::string& name);
    std::string scenario() const;

    void section(const std::string& name);
    std::string currentSection() const;

    // Records a check; returns `ok`. An empty name becomes "check N".
    bool expect(const std::string& name, bool ok, const std::string& detail, double t);

    // Declares the checks the scenario must record (a planned check that never ran fails the run as "not run").
    void plan(const std::vector<std::string>& names);

    void mark(const std::string& name, double t);  // "first_tick", "session.start", ...

    // Pending waits (risen.test.wait_for): reported as unfinished when the run ends before they do.
    std::uint64_t beginWait(const std::string& description);
    void endWait(std::uint64_t id);

    int total() const;
    int failed() const;
    // Planned checks that were never recorded, followed by "waiting: <description>" for every pending wait.
    std::vector<std::string> unfinished() const;

    // pass = at least one check, none failed, nothing unfinished, not aborted.
    Verdict verdict(const std::string& abortReason) const;

    // One-process suites (risen.test.suite_begin): adds the outcome of another run as checks named "<prefix>: <name>"; its unfinished checks and its abort
    // reason become failed checks, and a run without any check fails ("no check was recorded").
    void appendFrom(const Run& other, const std::string& prefix, const std::string& abortReason, double t);

    // The result file: {"scenario", "status": "pass"|"fail", "reason", "summary", "elapsed_s", "section", "marks", "counts",
    // "unfinished", "checks": [...]}.
    std::string toJson(const std::string& abortReason, const std::string& summary, double elapsed) const;

private:
    Verdict verdictLocked(const std::string& abortReason) const;
    std::vector<std::string> unfinishedLocked() const;

    mutable std::mutex mutex_;
    std::string scenario_ = "unnamed";
    std::string section_;
    std::vector<Check> checks_;
    std::vector<std::string> planned_;
    std::map<std::string, double> marks_;
    std::map<std::uint64_t, std::string> waits_;
    std::uint64_t nextWait_ = 1;
};

}  // namespace spass::testrun
