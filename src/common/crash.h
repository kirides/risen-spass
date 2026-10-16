#pragma once

// Crash diagnostics (docs/DEVELOPMENT.md "Crashes": M17). The game used to disappear without a trace ("exited", no result, a log that stops):
//
//   * a vectored exception handler reports every fatal-class fault (access violation, illegal instruction, heap corruption, ...) that is NOT
//     caught by guarded(), and a top-level exception filter reports the unhandled one: a banner in spass.log (exception, address as
//     module+RVA, thread, "layer or engine", the unwound stack, a scan of the stack for return addresses, the breadcrumbs below) and, with
//     [debug] crash_dump = "mini" | "full", a minidump <bin>\spass\crash\crash-<time>-pid<pid>.dmp;
//   * guarded() calls that fault are logged too (a swallowed access violation is the early warning of a race);
//   * ExitProcess / TerminateProcess of the own process are logged with their caller when the layer did not ask for the exit;
//   * breadcrumbs: every Lua event handler, lifecycle detour and guarded() call is noted per thread, so the banner says what the layer was
//     doing ("session.start > balance_example > guarded lua_template.cpp:155").
//
// Everything in the crash path avoids the C++ heap and spdlog (the crashing thread may hold their locks).

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

struct _EXCEPTION_POINTERS;

namespace spass::crash {

enum class DumpKind { Off, Mini, Full };

// "", "off", "false", "0" -> Off; "mini", "true", "1" -> Mini; "full" -> Full (anything else: Off).
DumpKind parseDumpKind(std::string_view text);

struct Options {
    DumpKind dump = DumpKind::Off;
    std::filesystem::path dumpDir;  // where the .dmp files go (created on demand)
    std::filesystem::path logFile;  // the banner is appended to this file (spass.log)
};

// Installs the vectored handler and the top-level filter. Call once, early (DllMain is fine: nothing here needs the loader).
void install(const Options& options);
// Needs MinHook initialised (hooks::initialize) and runs before hooks::enableAll(): keeps our filter the outermost one when the game's own
// crash reporter (sentry) installs its filter later, and reports process exits that nobody asked for.
void installHooks();

// The calling thread is the game thread (the banner says so for the faulting thread).
void setGameThread();
// The layer is about to end the process on purpose (a finished test run): not reported as a surprise.
void expectExit();

// ---- pure helpers (unit tests) -------------------------------------------------------------------------------------------
const char* exceptionName(unsigned code);  // "ACCESS_VIOLATION", ... or "" for an unknown code
std::string formatModuleOffset(std::string_view module, std::uint64_t rva);  // "Game.dll+0x805587", or "0x<address>" when the module is empty
std::string baseName(std::string_view path);                                  // "lua_template.cpp" from "B:\\x\\src\\game\\lua_template.cpp"

// ---- breadcrumbs ---------------------------------------------------------------------------------------------------------
// A trail per thread of what the layer is running: Lua event handlers, lifecycle detours (text), guarded() engine calls (source line).
class Scope {
public:
    explicit Scope(std::string_view what, std::string_view detail = {});
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

// guarded() entry: marks "an access violation here is caught" (the vectored handler leaves it to the caller) and notes the call site.
class GuardScope {
public:
    GuardScope(const char* file, int line);
    ~GuardScope();
    GuardScope(const GuardScope&) = delete;
    GuardScope& operator=(const GuardScope&) = delete;
};

std::string breadcrumbs();  // the trail of the calling thread, outermost first, " > " separated ("" when empty)

// Called from the exception filter of guarded(): logs the fault (rate limited) and counts it.
void noteGuardedFault(unsigned code, _EXCEPTION_POINTERS* info);
std::uint64_t guardedFaultCount();

// A fault that the caller catches on purpose but wants evidence of: writes the banner (registers, stack, verdict) to crash.log and a one-line summary
// to spass.log, and a minidump for the first few. Rate limited (3 per process).
void reportCaught(_EXCEPTION_POINTERS* info, const char* what);
// The thread id set by setGameThread() (0 before).
unsigned long gameThreadId();
// "Game.dll+0x805587" for an address (the module is found without the loader lock), "0x... (no module)" otherwise.
std::string addressText(const void* address);

}  // namespace spass::crash
