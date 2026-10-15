#pragma once

#include <filesystem>
#include <map>
#include <string>

#include <sol/sol.hpp>

#include "common/test_run.h"

namespace spass::game::test {

// risen.test: scenario checks for live verification (docs/LUA_API.md "risen.test", docs/DEVELOPMENT.md "Testing and tracing").
struct Settings {
    bool active = false;         // [test] enabled = true or [lua] unsafe: finish() writes the result file, the deadline applies
    bool exitOnFinish = false;   // [test] enabled and exit_on_finish: the process ends shortly after finish()
    double deadlineSeconds = 0;  // [test] deadline_s (0 = none)
    std::string scenario;        // [test] scenario
    std::map<std::string, std::string> params;  // [test.params]
    std::filesystem::path resultFile;           // <bin>/spass/test-result.json
};

void configure(Settings settings);

// Adds the `risen.test` table (and the Lua part: eq, run, sleep, wait_until).
void bindTest(sol::state_view lua, sol::table risen);
// Per-mod additions (risen.test.wait_event needs the mod's own `on`).
void bindTestForMod(sol::state_view lua, sol::table api);

// Called by the engine hooks on the game thread.
void onTick();                         // every frame (cheap when nothing is pending)
void mark(const std::string& name);    // "first_tick", "session.start", ... for the timing report
void startWatchdog();                  // the deadline thread; call once the game is up (not from DllMain)
void shutdown();                       // drops pending waits while the Lua state still exists

// Exposed for the unit tests.
void pollWaits(sol::state_view lua);
testrun::Run& currentRun();
bool finished();
void resetForTests();

}  // namespace spass::game::test
