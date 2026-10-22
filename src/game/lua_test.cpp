#include "game/lua_test.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#include <windows.h>

#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

#include "common/crash.h"
#include "common/trace.h"
#include "lua/runtime.h"

// risen.test (docs/LUA_API.md "risen.test"). Native part: check bookkeeping, the result file, pending waits polled once per frame
// and the deadline thread. The Lua part (eq, matches, run/sleep/wait_until as a coroutine scenario) sits on top of it.
//
// Every function exists in every run; the result file, the deadline and the process exit only apply when the layer was configured
// with `[test] enabled = true` (or `[lua] unsafe`), so a mod may ship its checks.

namespace spass::game::test {
namespace {

using Clock = std::chrono::steady_clock;

Settings g_settings;
const Clock::time_point g_start = Clock::now();
testrun::Run g_run;
std::atomic<bool> g_finished{false};
std::atomic<int> g_pending{0};
std::atomic<int> g_framePending{0};  // the frame waits among them (wait_frames: counted by onFrame)
std::atomic<bool> g_watchdogStarted{false};
double g_lastBeat = 0;
bool g_firstTick = false;

double since() { return std::chrono::duration<double>(Clock::now() - g_start).count(); }

// One-process suites (risen.test.suite_begin): every scenario of the suite ends with finish() without ending the process; the outcomes are collected in g_suiteRun
// and written as one result when suite_finish() is called.
bool g_suite = false;
bool g_suiteDone = false;
testrun::Run g_suiteRun;
int g_errorBase = 0;  // error lines logged before the current scenario began

std::atomic<bool> g_allowErrors{false};  // risen.test.allow_errors(true): error-level log lines are not counted (the hardening scenario provokes them on purpose)
// Counts everything the layer logged at error level (Lua handler errors, engine failures) except the test's own FAIL lines:
// finish() turns the count into one more check, the equivalent of the old "no [error] in the log" regex.
class ErrorCounter final : public spdlog::sinks::base_sink<std::mutex> {
public:
    std::vector<std::string> messages;
    int count = 0;

    int snapshot(std::vector<std::string>& first) {
        std::lock_guard lock(mutex_);
        first = messages;
        return count;
    }
    int total() {
        std::lock_guard lock(mutex_);
        return count;
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        if (msg.level < spdlog::level::err || g_allowErrors.load()) {
            return;
        }
        const std::string text(msg.payload.data(), msg.payload.size());
        if (text.rfind("[test]", 0) == 0) {
            return;
        }
        ++count;
        if (messages.size() < 5) {
            messages.push_back(text.substr(0, 300));
        }
    }
    void flush_() override {}
};
std::shared_ptr<ErrorCounter> g_errors;

struct Wait {
    std::uint64_t runId = 0;
    // main_protected_function: a callback registered from inside a scenario coroutine must still run on the main thread
    sol::main_protected_function pred;  // empty for tick waits
    sol::main_protected_function done;
    Clock::time_point deadline;
    std::string name;
    int ticksLeft = -1;  // >= 0: a tick wait
    int framesLeft = -1;  // >= 0: a frame wait (rendered frames, onFrame: also while the world is paused or frozen)
    double timeout = 0;
};
std::vector<Wait> g_waits;

std::string str(sol::state_view lua, const sol::object& o) {
    switch (o.get_type()) {
        case sol::type::lua_nil:
        case sol::type::none:
            return {};
        case sol::type::string:
            return o.as<std::string>();
        default: {
            try {
                sol::protected_function tostring = lua["tostring"];
                sol::protected_function_result r = tostring(o);
                if (r.valid()) {
                    return r.get<std::string>();
                }
            } catch (...) {
            }
            return "?";
        }
    }
}

bool truthy(const sol::object& o) {
    const sol::type t = o.get_type();
    if (t == sol::type::lua_nil || t == sol::type::none) {
        return false;
    }
    return !(t == sol::type::boolean && !o.as<bool>());
}

bool record(const std::string& name, bool ok, const std::string& detail) {
    g_run.expect(name, ok, detail, since());
    const std::string shown = name.empty() ? "check" : name;
    if (ok) {
        spdlog::info("[test] ok {}{}{}", shown, detail.empty() ? "" : ": ", detail);
    } else {
        spdlog::error("[test] FAIL {}{}{}", shown, detail.empty() ? "" : ": ", detail);
    }
    return ok;
}

void writeResult(const std::string& json) {
    if (g_settings.resultFile.empty()) {
        return;
    }
    try {
        std::filesystem::create_directories(g_settings.resultFile.parent_path());
        const auto tmp = std::filesystem::path(g_settings.resultFile).concat(".tmp");
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            out << json;
        }
        std::filesystem::rename(tmp, g_settings.resultFile);  // the script never sees a half-written file
    } catch (const std::exception& e) {
        spdlog::error("[test] cannot write {}: {}", g_settings.resultFile.string(), e.what());
    }
}

void finishRun(const std::string& summary, const std::string& abortReason, bool fatal = false) {
    if (g_finished.exchange(true)) {
        return;
    }
    const bool suiteFinal = g_suite && g_suiteDone;   // the whole suite is over: the outcomes of its scenarios are in g_suiteRun already
    if (g_errors && !suiteFinal) {
        std::vector<std::string> first;
        const int n = g_errors->snapshot(first) - g_errorBase;
        std::string detail = n <= 0 ? "" : std::to_string(n) + " error line(s), first: ";
        for (std::size_t i = 0; i < first.size() && i < 3; ++i) {
            detail += (i ? " | " : "") + first[i];
        }
        g_run.expect("no errors logged", n <= 0, detail, since());
        if (n > 0) {
            spdlog::error("[test] FAIL no errors logged: {}", detail);
        }
    }
    const double elapsed = since();
    if (g_suite && !g_suiteDone) {
        // a scenario of the suite is over; the process goes on with the next one (a fatal end, the deadline, finishes the suite too)
        const testrun::Verdict sv = g_run.verdict(abortReason);
        spdlog::info("[test] suite scenario '{}' finished after {:.1f}s: {} ({} checks, {} failed{}{})", g_run.scenario(), elapsed, sv.pass ? "PASS" : "FAIL", g_run.total(),
                     g_run.failed(), sv.reason.empty() ? "" : "; ", sv.reason);
        g_suiteRun.appendFrom(g_run, g_run.scenario(), abortReason, elapsed);
        if (!fatal) {
            return;
        }
        g_suiteDone = true;
    }
    testrun::Run& run = g_suite ? g_suiteRun : g_run;
    const testrun::Verdict v = run.verdict(g_suite ? std::string() : abortReason);
    spdlog::info("[test] finished '{}' after {:.1f}s: {} ({} checks, {} failed{}{})", run.scenario(), elapsed, v.pass ? "PASS" : "FAIL",
                 run.total(), run.failed(), v.reason.empty() ? "" : "; ", v.reason);
    SPASS_TRACE(trace::Channel::Test, "finish: section '{}' unfinished={}", run.currentSection(), run.unfinished().size());
    if (g_settings.active) {
        writeResult(run.toJson(g_suite ? (fatal ? abortReason : std::string()) : abortReason, summary, elapsed));
    }
    if (g_settings.active && g_settings.exitOnFinish) {
        const UINT code = v.pass ? 0 : 1;
        std::thread([code] {
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));  // the script normally kills us first
            if (auto logger = spdlog::default_logger()) {
                logger->flush();
            }
            crash::expectExit();
            TerminateProcess(GetCurrentProcess(), code);
        }).detach();
    }
}

void updatePending() {
    int frames = 0;
    for (const Wait& w : g_waits) {
        frames += w.framesLeft >= 0 ? 1 : 0;
    }
    g_pending.store(static_cast<int>(g_waits.size()) - frames);
    g_framePending.store(frames);
}

void addWait(Wait w, const std::string& description) {
    w.runId = g_run.beginWait(description);
    SPASS_TRACE(trace::Channel::Test, "wait begin: {}", description);
    g_waits.push_back(std::move(w));
    updatePending();
}

const char* kLuaChunk = R"LUA(
local risen = ...
local T = risen.test
local debug_traceback = debug and debug.traceback

-- readable form of a value for check details: strings quoted, tables shallow (depth 2, 12 entries)
local function describe(v, depth)
    depth = depth or 0
    local t = type(v)
    if t == "string" then return string.format("%q", v) end
    if t ~= "table" then return tostring(v) end
    if depth >= 2 then return "{...}" end
    local parts, shown = {}, 0
    local n = #v
    for i = 1, n do
        shown = shown + 1
        if shown > 12 then parts[#parts + 1] = "..." break end
        parts[#parts + 1] = describe(v[i], depth + 1)
    end
    local keys = {}
    for k in pairs(v) do
        if not (type(k) == "number" and k >= 1 and k <= n and k == math.floor(k)) then keys[#keys + 1] = k end
    end
    table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
    for _, k in ipairs(keys) do
        shown = shown + 1
        if shown > 12 then parts[#parts + 1] = "..." break end
        parts[#parts + 1] = tostring(k) .. "=" .. describe(v[k], depth + 1)
    end
    return "{" .. table.concat(parts, ", ") .. "}"
end

local function same(a, b, depth)
    if a == b then return true end
    if type(a) ~= "table" or type(b) ~= "table" or depth > 8 then return false end
    for k, v in pairs(a) do if not same(v, b[k], depth + 1) then return false end end
    for k in pairs(b) do if a[k] == nil then return false end end
    return true
end

-- risen.test.eq(name, actual, expected [, tolerance]) -> boolean; tables compare by content, numbers within `tolerance`
function risen.test.eq(name, actual, expected, tolerance)
    local ok
    if tolerance and type(actual) == "number" and type(expected) == "number" then
        ok = math.abs(actual - expected) <= tolerance
    else
        ok = same(actual, expected, 0)
    end
    local detail
    if ok then detail = describe(actual) else detail = "expected " .. describe(expected) .. ", got " .. describe(actual) end
    return T.expect(name, ok, detail)
end

-- risen.test.matches(name, text, pattern) -> boolean: a Lua pattern (string.find) must match tostring(text)
function risen.test.matches(name, text, pattern)
    local s = tostring(text)
    local ok = s:find(pattern) ~= nil
    return T.expect(name, ok, ok and s or ("'" .. s .. "' does not match '" .. tostring(pattern) .. "'"))
end

-- ---- scenarios as a coroutine --------------------------------------------------------------------------------------------
local runner = nil

local function in_scenario(what)
    if not runner or coroutine.running() ~= runner.co then
        error("risen.test." .. what .. " can only be called inside the function passed to risen.test.run", 3)
    end
    return runner
end

-- risen.test.reset([scenario_name]): starts a new scenario in the same process (checks, plan, waits and the running scenario are forgotten); see risen.test.suite_begin
local native_reset = T.reset
function risen.test.reset(scenario_name)
    local ok = native_reset(scenario_name)
    runner = nil
    return ok
end

-- risen.test.run(fn [, scenario_name]): runs fn from the next frame on as a coroutine that may call risen.test.sleep(ticks),
-- risen.test.wait_until(pred, timeout_s) and risen.test.wait_event(name, timeout_s). An error aborts with a FAIL; when fn returns,
-- risen.test.finish() is called.
function risen.test.run(fn, scenario_name)
    if type(fn) ~= "function" then error("risen.test.run: expected a function", 2) end
    if runner then error("risen.test.run: a scenario is already running", 2) end
    if scenario_name then T.scenario(scenario_name) end
    local r = { co = coroutine.create(fn) }
    runner = r
    function r.resume(...)
        local ok, err = coroutine.resume(r.co, ...)
        if not ok then
            local text = tostring(err)
            if debug_traceback then text = debug_traceback(r.co, text) end
            T.fail("scenario error: " .. text)
        elseif coroutine.status(r.co) == "dead" then
            T.finish()
        end
    end
    T.wait_ticks(1, function() r.resume() end)
end

-- risen.test.sleep(ticks): continue after that many frames (the `tick` event)
function risen.test.sleep(ticks)
    local r = in_scenario("sleep")
    T.wait_ticks(ticks or 1, function() r.resume() end)
    coroutine.yield()
end

-- risen.test.sleep_frames(n): continue after n rendered frames (the `ui.frame` event: also counts while the world is paused or time is frozen)
function risen.test.sleep_frames(n)
    local r = in_scenario("sleep_frames")
    T.wait_frames(n or 1, function() r.resume() end)
    coroutine.yield()
end

-- risen.test.wait_until(pred, timeout_s [, name]) -> true; on timeout the check "wait <name>" fails and the scenario ends
function risen.test.wait_until(pred, timeout_s, name)
    local r = in_scenario("wait_until")
    local ok
    T.wait_for(pred, timeout_s or 60, function(done_ok) r.resume(done_ok) end, name)
    ok = coroutine.yield()
    if not ok then
        T.finish("aborted: timed out waiting for " .. tostring(name or "a condition"))
        while true do coroutine.yield() end
    end
    return true
end
)LUA";

const char* kModChunk = R"LUA(
local api = ...
local shared = api.test
local t = setmetatable({}, { __index = shared })
api.test = t

-- risen.test.wait_event(event, timeout_s [, filter]) -> the event's arguments: inside risen.test.run, waits until the event fired
-- (and filter(...) returned true, when given). Times out like wait_until.
function t.wait_event(event, timeout_s, filter)
    -- a lifecycle event that already happened in this process counts (a scenario started late: a one-process suite, a reloaded mod)
    local life = shared.lifecycle()
    local mode = (event == "session.start" and life.session) or (event == "world.ready" and life.world) or nil
    if mode and (not filter or filter(mode)) then return mode end
    local got = nil
    local handle = api.on(event, function(...)
        if got == nil and (not filter or filter(...)) then got = { n = select("#", ...), ... } end
    end)
    shared.wait_until(function() return got ~= nil end, timeout_s or 120, "event " .. tostring(event))
    api.off(handle)
    return unpack(got, 1, got.n)
end
)LUA";

void runChunk(sol::state_view lua, const char* chunk, const char* name, sol::object arg) {
    sol::load_result loaded = lua.load(chunk, name);
    if (!loaded.valid()) {
        spdlog::error("lua: {} failed to load: {}", name, loaded.get<sol::error>().what());
        return;
    }
    sol::protected_function fn = loaded;
    sol::protected_function_result res = fn(arg);
    if (!res.valid()) {
        spdlog::error("lua: {} failed to run: {}", name, res.get<sol::error>().what());
    }
}

}  // namespace

void configure(Settings settings) {
    g_settings = std::move(settings);
    if (!g_settings.scenario.empty()) {
        g_run.setScenario(g_settings.scenario);
    }
    if (g_settings.active) {
        std::error_code ec;
        std::filesystem::remove(g_settings.resultFile, ec);  // never report an older run
        g_errors = std::make_shared<ErrorCounter>();
        spdlog::default_logger()->sinks().push_back(g_errors);
        spdlog::info("[test] active: scenario '{}', deadline {}s, exit on finish: {}", g_run.scenario(), g_settings.deadlineSeconds,
                     g_settings.exitOnFinish);
    }
}

void bindTest(sol::state_view lua, sol::table risen) {
    sol::table t = lua.create_table();

    // risen.test.expect(name, cond [, detail]) -> boolean: records a check and logs "[test] ok|FAIL name: detail"; never throws
    t.set_function("expect", [](sol::this_state ts, sol::object name, sol::object cond, sol::object detail) {
        sol::state_view lua(ts);
        return record(str(lua, name), truthy(cond), str(lua, detail));
    });
    t.set_function("section", [](sol::this_state ts, sol::object name) {
        const std::string n = str(sol::state_view(ts), name);
        g_run.section(n);
        spdlog::info("[test] section {}", n);
        SPASS_TRACE(trace::Channel::Test, "section {}", n);
    });
    t.set_function("plan", [](sol::table names) {
        std::vector<std::string> list;
        for (const auto& kv : names) {
            if (kv.second.get_type() == sol::type::string) {
                list.push_back(kv.second.as<std::string>());
            }
        }
        g_run.plan(list);
    });
    t.set_function("scenario", [](sol::optional<std::string> name) {
        if (name) {
            g_run.setScenario(*name);
        }
        return g_run.scenario();
    });
    t.set_function("param", [](sol::this_state ts, const std::string& key, sol::object fallback) -> sol::object {
        const auto it = g_settings.params.find(key);
        if (it == g_settings.params.end()) {
            return fallback;
        }
        return sol::make_object(ts, it->second);
    });
    // risen.test.allow_errors(on): while on, error-level log lines do not count towards the final "no errors logged" check
    t.set_function("allow_errors", [](sol::object on) {
        g_allowErrors = truthy(on);
        return g_allowErrors.load();
    });
    t.set_function("is_active", [] { return g_settings.active; });
    // risen.test.reset([scenario]): see the Lua part (it also forgets the running scenario)
    t.set_function("reset", [](sol::optional<std::string> name) {
        g_waits.clear();
        updatePending();
        g_run.reset();
        if (name && !name->empty()) {
            g_run.setScenario(name->substr(0, 80));
        }
        g_finished = false;
        g_errorBase = g_errors ? g_errors->total() : 0;
        return true;
    });
    // risen.test.suite_begin([name]): from now on finish() / fail() end only the current scenario; the result of the process is written by suite_finish
    t.set_function("suite_begin", [](sol::optional<std::string> name) {
        g_suite = true;
        g_suiteDone = false;
        g_suiteRun.reset();
        g_suiteRun.setScenario(name && !name->empty() ? name->substr(0, 80) : (g_settings.scenario.empty() ? std::string("suite") : g_settings.scenario));
        g_errorBase = g_errors ? g_errors->total() : 0;
        return true;
    });
    t.set_function("suite_finish", [](sol::this_state ts, sol::object summary) {
        if (!g_suite) {
            return false;
        }
        g_suiteDone = true;
        g_finished = false;
        finishRun(str(sol::state_view(ts), summary), "");
        return true;
    });
    t.set_function("is_finished", [] { return g_finished.load(); });
    // risen.test.lifecycle() -> { session = mode | nil, world = mode | nil }: what this process has seen of session.start / world.ready
    t.set_function("lifecycle", [](sol::this_state ts) {
        sol::state_view L(ts);
        sol::table out = L.create_table();
        auto& rt = lua::Runtime::instance();
        if (const auto m = rt.sessionMode()) out["session"] = *m;
        if (const auto m = rt.worldMode()) out["world"] = *m;
        return out;
    });
    t.set_function("elapsed", [] { return since(); });
    t.set_function("finish", [](sol::this_state ts, sol::object summary) {
        finishRun(str(sol::state_view(ts), summary), "");
        return true;
    });
    t.set_function("fail", [](sol::this_state ts, sol::object reason) {
        const std::string r = str(sol::state_view(ts), reason);
        record("fail", false, r);
        finishRun("", "aborted: " + r);
        return false;
    });
    // risen.test.wait_for(predicate, timeout_s, on_done [, name]): polls predicate() once per frame; on_done(true) when it returned
    // a truthy value, on_done(false, reason) after timeout_s (a FAIL check "wait <name>" is recorded) or when the predicate raised.
    t.set_function("wait_for", [](sol::object pred, double timeout, sol::object done, sol::object name) {
        if (pred.get_type() != sol::type::function) {
            throw sol::error("risen.test.wait_for: the predicate must be a function");
        }
        Wait w;
        w.pred = pred.as<sol::main_protected_function>();
        if (done.get_type() == sol::type::function) {
            w.done = done.as<sol::main_protected_function>();
        }
        w.name = name.get_type() == sol::type::string ? name.as<std::string>() : "condition";
        w.timeout = timeout > 0 ? timeout : 60;
        w.deadline = Clock::now() + std::chrono::milliseconds(static_cast<long long>(w.timeout * 1000));
        const std::string description = w.name + " (timeout " + std::to_string(static_cast<int>(w.timeout)) + "s)";
        addWait(std::move(w), description);
    });
    // risen.test.wait_ticks(n, on_done): on_done(true) after n frames
    t.set_function("wait_ticks", [](int n, sol::object done) {
        Wait w;
        if (done.get_type() == sol::type::function) {
            w.done = done.as<sol::main_protected_function>();
        }
        w.ticksLeft = n > 0 ? n : 1;
        w.name = "ticks";
        addWait(std::move(w), "ticks");
    });
    // risen.test.wait_frames(n, on_done): on_done(true) after n rendered frames (eCApplication::DoRender: the `ui.frame` event)
    t.set_function("wait_frames", [](int n, sol::object done) {
        Wait w;
        if (done.get_type() == sol::type::function) {
            w.done = done.as<sol::main_protected_function>();
        }
        w.framesLeft = n > 0 ? n : 1;
        w.name = "frames";
        addWait(std::move(w), "frames");
    });
    risen["test"] = t;
    runChunk(lua, kLuaChunk, "=risen.test", sol::make_object(lua, risen));
}

void bindTestForMod(sol::state_view lua, sol::table api) {
    runChunk(lua, kModChunk, "=risen.test.mod", sol::make_object(lua, api));
}

void pollWaits(sol::state_view lua, bool frame) {
    (void)lua;
    if (g_waits.empty()) {
        return;
    }
    std::vector<Wait> current;
    current.swap(g_waits);
    std::vector<Wait> keep;
    const auto now = Clock::now();
    for (Wait& w : current) {
        bool completed = false;
        bool ok = false;
        std::string why;
        if ((w.framesLeft >= 0) != frame) {
            keep.push_back(std::move(w));  // a frame wait on a tick, or the other way round
            continue;
        }
        if (w.framesLeft >= 0) {
            if (--w.framesLeft <= 0) {
                completed = ok = true;
            }
        } else if (w.ticksLeft >= 0) {
            if (--w.ticksLeft <= 0) {
                completed = ok = true;
            }
        } else {
            sol::protected_function_result r = w.pred();
            if (!r.valid()) {
                completed = true;
                why = r.get<sol::error>().what();
                record("wait " + w.name, false, "the predicate raised an error: " + why);
            } else if (truthy(r.get<sol::object>())) {
                completed = ok = true;
            } else if (now >= w.deadline) {
                completed = true;
                why = "timeout";
                record("wait " + w.name, false, "timed out after " + std::to_string(static_cast<int>(w.timeout)) + "s");
            }
        }
        if (!completed) {
            keep.push_back(std::move(w));
            continue;
        }
        g_run.endWait(w.runId);
        SPASS_TRACE(trace::Channel::Test, "wait end: {} ok={} {}", w.name, ok, why);
        if (w.done.valid()) {
            sol::protected_function_result r = w.done(ok, why);
            if (!r.valid()) {
                record("wait " + w.name + " callback", false, r.get<sol::error>().what());
            }
        }
    }
    g_waits.insert(g_waits.begin(), std::make_move_iterator(keep.begin()), std::make_move_iterator(keep.end()));
    updatePending();
}

void onTick() {
    if (!g_firstTick) {
        g_firstTick = true;
        mark("first_tick");
    }
    if (g_settings.active && !g_finished.load()) {
        const double t = since();
        if (t - g_lastBeat >= 15.0) {  // proof of life for the script's stall watchdog while a scenario idles
            g_lastBeat = t;
            spdlog::info("[test] alive t={:.0f}s section='{}' checks={} failed={}", t, g_run.currentSection(), g_run.total(), g_run.failed());
        }
    }
    if (g_pending.load() > 0) {
        auto& rt = lua::Runtime::instance();
        if (rt.running()) {
            auto lock = rt.lock();
            pollWaits(rt.lua());
        }
    }
}

void onFrame() {
    if (g_framePending.load() > 0) {
        auto& rt = lua::Runtime::instance();
        if (rt.running()) {
            auto lock = rt.lock();
            pollWaits(rt.lua(), /*frame=*/true);
        }
    }
}

void mark(const std::string& name) {
    if (!g_settings.active) {
        return;
    }
    const double t = since();
    g_run.mark(name, t);
    spdlog::info("[test] mark {} at {:.1f}s", name, t);
}

void startWatchdog() {
    if (!g_settings.active || g_settings.deadlineSeconds <= 0 || g_watchdogStarted.exchange(true)) {
        return;
    }
    std::thread([] {
        // Works even when the game thread is stuck inside Lua: it only touches the check bookkeeping and the log.
        while (!g_finished.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            if (since() > g_settings.deadlineSeconds) {
                const std::string where = g_run.currentSection();
                spdlog::error("[test] deadline of {:.0f}s exceeded (section '{}', {} unfinished)", g_settings.deadlineSeconds, where,
                              g_run.unfinished().size());
                finishRun("", "timeout: deadline of " + std::to_string(static_cast<int>(g_settings.deadlineSeconds)) + "s exceeded" +
                                  (where.empty() ? "" : " in section '" + where + "'"), /*fatal=*/true);
                return;
            }
        }
    }).detach();
}

void shutdown() {
    g_waits.clear();
    updatePending();
}

testrun::Run& currentRun() { return g_run; }
bool finished() { return g_finished.load(); }

void resetForTests() {
    g_waits.clear();
    updatePending();
    g_finished = false;
    g_run.reset();
    g_suite = g_suiteDone = false;
    g_suiteRun.reset();
    g_errorBase = 0;
}

}  // namespace spass::game::test
