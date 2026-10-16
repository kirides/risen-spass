#include "common/crash.h"

#include <windows.h>

#include <dbghelp.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include <spdlog/spdlog.h>

#include "hook/hook_manager.h"

namespace spass::crash {
namespace {

// ---- breadcrumbs ----------------------------------------------------------------------------------------------------------

constexpr int kTrailMax = 16;

struct Entry {
    char text[80];     // a lifecycle / event note, or empty for a guarded() call
    const char* file;  // guarded(): source file (static string) and line
    int line;
};

struct Trail {
    Entry e[kTrailMax];
    int depth = 0;       // entries pushed (may exceed kTrailMax: the deeper ones are not stored)
    int guardDepth = 0;  // guarded() calls the thread is inside of
};

thread_local Trail t_trail;

void copyText(char* dst, std::size_t cap, std::string_view a, std::string_view b) {
    std::size_t n = 0;
    for (char c : a) {
        if (n + 1 >= cap) break;
        dst[n++] = c;
    }
    if (!b.empty() && n + 2 < cap) {
        dst[n++] = ' ';
        for (char c : b) {
            if (n + 1 >= cap) break;
            dst[n++] = c;
        }
    }
    dst[n] = '\0';
}

// ---- small allocation-free text builder (the crash path) -------------------------------------------------------------------

struct Buf {
    char* p;
    std::size_t cap;
    std::size_t len = 0;
    void add(const char* fmt, ...) {
        if (len + 1 >= cap) return;
        va_list ap;
        va_start(ap, fmt);
        const int n = _vsnprintf_s(p + len, cap - len, _TRUNCATE, fmt, ap);
        va_end(ap);
        len += n < 0 ? (cap - len - 1) : static_cast<std::size_t>(n);
    }
};

// ---- state ----------------------------------------------------------------------------------------------------------------

Options g_options;
wchar_t g_logPath[520] = {};     // spass.log (the banner of a dying process is appended there)
wchar_t g_crashLog[520] = {};    // crash.log next to it: every banner, never touched by spdlog
wchar_t g_dumpDir[520] = {};
char g_buffer[32768];            // the banner; one reporter at a time (g_busy)
std::atomic<int> g_busy{0};
std::atomic<int> g_firstChance{0};
std::atomic<int> g_dumps{0};
std::atomic<bool> g_installed{false};
std::atomic<bool> g_exitExpected{false};
std::atomic<DWORD> g_gameThread{0};
std::atomic<std::uint64_t> g_guardedFaults{0};
std::uintptr_t g_selfBase = 0;

// last fault the vectored handler reported: the top-level filter must not report it a second time
std::atomic<std::uintptr_t> g_lastAddress{0};
std::atomic<unsigned> g_lastCode{0};
std::atomic<DWORD> g_lastThread{0};

LPTOP_LEVEL_EXCEPTION_FILTER g_chained = nullptr;
Hook<LPTOP_LEVEL_EXCEPTION_FILTER(WINAPI*)(LPTOP_LEVEL_EXCEPTION_FILTER)> g_setFilter;
Hook<void(WINAPI*)(UINT)> g_exitProcess;
Hook<BOOL(WINAPI*)(HANDLE, UINT)> g_terminateProcess;

using RtlPcToFileHeader_t = PVOID(NTAPI*)(PVOID, PVOID*);
RtlPcToFileHeader_t g_pcToFileHeader = nullptr;

bool isFatalClass(unsigned code) {
    switch (code) {
        case 0xC0000005:  // access violation
        case 0xC0000006:  // in-page error
        case 0xC000001D:  // illegal instruction
        case 0xC0000025:  // non-continuable
        case 0xC0000094:  // integer divide by zero
        case 0xC0000096:  // privileged instruction
        case 0xC000008C:  // array bounds
        case 0xC00000FD:  // stack overflow
        case 0xC0000374:  // heap corruption
        case 0xC0000409:  // stack buffer overrun / fast fail
            return true;
        default:
            return false;
    }
}

// ---- modules (no loader lock, no allocation) ------------------------------------------------------------------------------

bool safeCopy(const void* from, void* to, std::size_t n) {
    __try {
        std::memcpy(to, from, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The module that contains `addr`: image base and a name. The name comes from the export directory (every Risen DLL has one); our own
// proxy is told apart from the renamed original (both call themselves sentry.dll).
bool moduleOf(const void* addr, std::uintptr_t* base, char* name, std::size_t cap) {
    name[0] = '\0';
    *base = 0;
    if (g_pcToFileHeader == nullptr) {
        HMODULE nt = GetModuleHandleW(L"ntdll.dll");
        g_pcToFileHeader = nt != nullptr ? reinterpret_cast<RtlPcToFileHeader_t>(GetProcAddress(nt, "RtlPcToFileHeader")) : nullptr;
        if (g_pcToFileHeader == nullptr) return false;
    }
    PVOID image = nullptr;
    if (g_pcToFileHeader(const_cast<void*>(addr), &image) == nullptr || image == nullptr) {
        return false;
    }
    *base = reinterpret_cast<std::uintptr_t>(image);
    if (*base == g_selfBase) {
        strncpy_s(name, cap, "sentry.dll[layer]", _TRUNCATE);
        return true;
    }
    if (image == GetModuleHandleW(nullptr)) {
        strncpy_s(name, cap, "Risen.exe", _TRUNCATE);
        return true;
    }
    __try {
        const auto* dos = static_cast<const IMAGE_DOS_HEADER*>(image);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(static_cast<const char*>(image) + dos->e_lfanew);
        const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (dir.VirtualAddress != 0) {
            const auto* exp = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(static_cast<const char*>(image) + dir.VirtualAddress);
            if (exp->Name != 0) {
                strncpy_s(name, cap, static_cast<const char*>(image) + exp->Name, _TRUNCATE);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        name[0] = '\0';
    }
    if (std::strcmp(name, "sentry.dll") == 0) {
        strncpy_s(name, cap, "sentry_orig.dll", _TRUNCATE);  // the renamed original of the game's crash reporter
    }
    if (name[0] == '\0') {
        strncpy_s(name, cap, "?", _TRUNCATE);
    }
    return true;
}

// "Game.dll+0x805587" or "0x<addr> (no module)" written into out
void describeAddress(const void* addr, char* out, std::size_t cap) {
    char name[64];
    std::uintptr_t base = 0;
    if (moduleOf(addr, &base, name, sizeof(name))) {
        _snprintf_s(out, cap, _TRUNCATE, "%s+0x%llx", name, static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(addr) - base));
    } else {
        _snprintf_s(out, cap, _TRUNCATE, "0x%llx (no module)", static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(addr)));
    }
}

bool inExecutableSection(const void* addr) {
    std::uintptr_t base = 0;
    char name[8];
    if (!moduleOf(addr, &base, name, sizeof(name))) return false;
    bool exec = false;
    __try {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
        const std::uintptr_t rva = reinterpret_cast<std::uintptr_t>(addr) - base;
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            if ((sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0 && rva >= sec[i].VirtualAddress &&
                rva < sec[i].VirtualAddress + std::max<DWORD>(sec[i].Misc.VirtualSize, sec[i].SizeOfRawData)) {
                exec = true;
                break;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        exec = false;
    }
    return exec;
}

// ---- output ---------------------------------------------------------------------------------------------------------------

void appendRaw(const wchar_t* path, const char* text, std::size_t n) {
    if (path[0] == L'\0' || n == 0) return;
    HANDLE h = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(h, text, static_cast<DWORD>(n), &written, nullptr);
    FlushFileBuffers(h);
    CloseHandle(h);
}

void timestamp(char* out, std::size_t cap) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    _snprintf_s(out, cap, _TRUNCATE, "[%02d:%02d:%02d.%03d]", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

const char* accessName(ULONG_PTR kind) { return kind == 0 ? "read" : kind == 1 ? "write" : kind == 8 ? "execute (DEP)" : "access"; }

void addFault(Buf& b, const EXCEPTION_RECORD* r) {
    const char* name = exceptionName(r->ExceptionCode);
    b.add("%s (0x%08lX)", name[0] != '\0' ? name : "exception", r->ExceptionCode);
    if ((r->ExceptionCode == 0xC0000005 || r->ExceptionCode == 0xC0000006) && r->NumberParameters >= 2) {
        b.add(": %s of 0x%llx", accessName(r->ExceptionInformation[0]), static_cast<unsigned long long>(r->ExceptionInformation[1]));
        if (r->ExceptionCode == 0xC0000006 && r->NumberParameters >= 3) {
            b.add(" (NTSTATUS 0x%llx)", static_cast<unsigned long long>(r->ExceptionInformation[2]));
        }
    }
}

// ---- the report -----------------------------------------------------------------------------------------------------------

bool writeDump(EXCEPTION_POINTERS* ep, char* pathOut, std::size_t cap) {
    pathOut[0] = '\0';
    if (g_options.dump == DumpKind::Off || g_dumpDir[0] == L'\0') return false;
    HMODULE dbg = LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (dbg == nullptr) return false;
    using Write_t = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION,
                                  PMINIDUMP_CALLBACK_INFORMATION);
    auto write = reinterpret_cast<Write_t>(GetProcAddress(dbg, "MiniDumpWriteDump"));
    if (write == nullptr) return false;
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t file[620];
    _snwprintf_s(file, _countof(file), _TRUNCATE, L"%s\\crash-%04d%02d%02d-%02d%02d%02d-pid%lu.dmp", g_dumpDir, st.wYear, st.wMonth, st.wDay, st.wHour,
                 st.wMinute, st.wSecond, GetCurrentProcessId());
    HANDLE h = CreateFileW(file, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    MINIDUMP_EXCEPTION_INFORMATION mei{};
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = ep;
    mei.ClientPointers = FALSE;
    const MINIDUMP_TYPE type =
        g_options.dump == DumpKind::Full
            ? static_cast<MINIDUMP_TYPE>(MiniDumpWithFullMemory | MiniDumpWithHandleData | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules |
                                         MiniDumpWithFullMemoryInfo)
            : static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs | MiniDumpWithHandleData | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules |
                                         MiniDumpWithProcessThreadData | MiniDumpWithFullMemoryInfo | MiniDumpWithIndirectlyReferencedMemory);
    const BOOL ok = write(GetCurrentProcess(), GetCurrentProcessId(), h, type, ep != nullptr ? &mei : nullptr, nullptr, nullptr);
    CloseHandle(h);
    if (!ok) return false;
    WideCharToMultiByte(CP_UTF8, 0, file, -1, pathOut, static_cast<int>(cap), nullptr, nullptr);
    return true;
}

// Walks the stack of the faulting context with the unwind tables (no dbghelp). Frames of jitted code or a smashed stack end the walk.
void addFrames(Buf& b, const CONTEXT& start, bool* layerFrame, int* layerIndex) {
    CONTEXT ctx = start;
    char where[160];
    for (int i = 0; i < 48; ++i) {
        const void* rip = reinterpret_cast<const void*>(ctx.Rip);
        describeAddress(rip, where, sizeof(where));
        b.add("    #%d %s%s\n", i, where, i == 0 ? "   <- faulting instruction" : "");
        if (!*layerFrame && std::strstr(where, "sentry.dll[layer]") != nullptr) {
            *layerFrame = true;
            *layerIndex = i;
        }
        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION rf = nullptr;
        __try {
            rf = RtlLookupFunctionEntry(ctx.Rip, &imageBase, nullptr);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            rf = nullptr;
        }
        __try {
            if (rf == nullptr) {
                // leaf function (or no unwind data at all): the return address is on top of the stack
                std::uint64_t ret = 0;
                if (!safeCopy(reinterpret_cast<void*>(ctx.Rsp), &ret, sizeof(ret))) break;
                ctx.Rip = ret;
                ctx.Rsp += 8;
            } else {
                PVOID handlerData = nullptr;
                DWORD64 establisher = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ctx.Rip, rf, &ctx, &handlerData, &establisher, nullptr);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            break;
        }
        if (ctx.Rip == 0 || ctx.Rsp == 0) break;
    }
}

// Words on the stack that look like return addresses (point into the executable section of a module): shows the call chain of a thread whose
// unwinding failed (jitted code, a smashed return address).
void addStackScan(Buf& b, const CONTEXT& ctx) {
    const std::uintptr_t rsp = static_cast<std::uintptr_t>(ctx.Rsp);
    char where[160];
    int shown = 0;
    b.add("  stack scan (code addresses within 0x800 bytes above rsp):\n");
    for (std::uintptr_t off = 0; off < 0x800 && shown < 28; off += 8) {
        std::uint64_t w = 0;
        if (!safeCopy(reinterpret_cast<void*>(rsp + off), &w, sizeof(w))) break;
        if (w < 0x10000 || !inExecutableSection(reinterpret_cast<void*>(w))) continue;
        describeAddress(reinterpret_cast<void*>(w), where, sizeof(where));
        b.add("    rsp+0x%03llx: %s\n", static_cast<unsigned long long>(off), where);
        ++shown;
    }
    if (shown == 0) b.add("    (none)\n");
    b.add("  raw stack from rsp-0x10:");
    for (int i = -2; i < 14; ++i) {
        std::uint64_t w = 0;
        if (!safeCopy(reinterpret_cast<void*>(rsp + i * 8), &w, sizeof(w))) break;
        b.add("%s%016llx", (i + 2) % 4 == 0 ? "\n    " : " ", static_cast<unsigned long long>(w));
    }
    b.add("\n");
}

// Builds the banner into g_buffer. Returns its length.
std::size_t buildBanner(EXCEPTION_POINTERS* ep, const char* kind) {
    Buf b{g_buffer, sizeof(g_buffer)};
    char ts[32];
    timestamp(ts, sizeof(ts));
    const EXCEPTION_RECORD* r = ep->ExceptionRecord;
    const CONTEXT& c = *ep->ContextRecord;
    char where[160];
    describeAddress(r->ExceptionAddress, where, sizeof(where));
    const DWORD tid = GetCurrentThreadId();

    b.add("%s [fatal] [t%lu] CRASH (%s): ", ts, tid, kind);
    addFault(b, r);
    b.add(" at %s\n", where);

    const DWORD gameThread = g_gameThread.load();
    b.add("  thread: t%lu (%s", tid, gameThread == 0 ? "the game thread is not known yet" : tid == gameThread ? "the game thread" : "NOT the game thread");
    if (gameThread != 0 && tid != gameThread) b.add(", which is t%lu", gameThread);
    b.add(")\n");

    const std::string trail = breadcrumbs();  // allocates; the trail is thread-local and the fault is not a heap fault in the common case
    b.add("  layer was doing: %s\n", trail.empty() ? "(nothing recorded on this thread)" : trail.c_str());

    b.add("  registers: rip=%016llx rsp=%016llx rbp=%016llx rax=%016llx rbx=%016llx\n", static_cast<unsigned long long>(c.Rip), static_cast<unsigned long long>(c.Rsp),
          static_cast<unsigned long long>(c.Rbp), static_cast<unsigned long long>(c.Rax), static_cast<unsigned long long>(c.Rbx));
    b.add("             rcx=%016llx rdx=%016llx rsi=%016llx rdi=%016llx r8=%016llx\n", static_cast<unsigned long long>(c.Rcx), static_cast<unsigned long long>(c.Rdx),
          static_cast<unsigned long long>(c.Rsi), static_cast<unsigned long long>(c.Rdi), static_cast<unsigned long long>(c.R8));
    b.add("             r9=%016llx r10=%016llx r11=%016llx r12=%016llx r13=%016llx r14=%016llx r15=%016llx\n", static_cast<unsigned long long>(c.R9),
          static_cast<unsigned long long>(c.R10), static_cast<unsigned long long>(c.R11), static_cast<unsigned long long>(c.R12), static_cast<unsigned long long>(c.R13),
          static_cast<unsigned long long>(c.R14), static_cast<unsigned long long>(c.R15));

    std::uint8_t code[16] = {};
    if (safeCopy(reinterpret_cast<void*>(c.Rip), code, sizeof(code))) {
        b.add("  code at rip:");
        for (unsigned char x : code) b.add(" %02x", x);
        b.add("\n");
    } else {
        b.add("  code at rip: (unreadable: the instruction pointer itself is bad, e.g. a return to a smashed address)\n");
    }

    bool layerFrame = false;
    int layerIndex = -1;
    b.add("  stack (unwound with the PE unwind tables):\n");
    addFrames(b, c, &layerFrame, &layerIndex);
    addStackScan(b, c);

    char faultModule[64];
    std::uintptr_t base = 0;
    const bool inModule = moduleOf(r->ExceptionAddress, &base, faultModule, sizeof(faultModule));
    if (inModule && std::strcmp(faultModule, "sentry.dll[layer]") == 0) {
        b.add("  verdict: the fault is IN THE LAYER (sentry.dll)\n");
    } else if (layerFrame) {
        b.add("  verdict: the fault is in %s, reached through the layer (nearest layer frame #%d)\n", inModule ? faultModule : "code outside any module", layerIndex);
    } else {
        b.add("  verdict: the fault is in %s, no layer frame on the unwound stack (an engine thread, or the layer's call was not on the stack)\n",
              inModule ? faultModule : "code outside any module (a jumped-to bad address: stack/function pointer corruption?)");
    }
    return b.len;
}

// Appends the line of `banner` that starts with `prefix` (without the rest of the buffer) to the compact summary.
void addLineStartingWith(Buf& out, const char* banner, const char* prefix) {
    const char* p = std::strstr(banner, prefix);
    if (p == nullptr) return;
    const char* end = std::strchr(p, '\n');
    const int n = end != nullptr ? static_cast<int>(end - p) : static_cast<int>(std::strlen(p));
    out.add("%.*s\n", n, p);
}

void report(EXCEPTION_POINTERS* ep, const char* kind, bool terminal, bool wantDump) {
    int expected = 0;
    if (!g_busy.compare_exchange_strong(expected, 1)) return;  // another thread is already reporting (or this handler faulted)
    buildBanner(ep, kind);
    Buf b{g_buffer, sizeof(g_buffer)};
    b.len = std::strlen(g_buffer);
    char dumpLine[640] = {};
    if (wantDump) {
        char path[520];
        if (writeDump(ep, path, sizeof(path))) {
            _snprintf_s(dumpLine, sizeof(dumpLine), _TRUNCATE, "  minidump: %s\n", path);
            g_dumps.fetch_add(1);
        } else if (g_options.dump != DumpKind::Off) {
            _snprintf_s(dumpLine, sizeof(dumpLine), _TRUNCATE, "  minidump: could not be written (error %lu)\n", GetLastError());
        }
    }
    b.add("%s", dumpLine);
    OutputDebugStringA(g_buffer);
    appendRaw(g_crashLog, g_buffer, b.len);  // the complete banner
    // spass.log gets the short form (what, where, what the layer was doing, verdict, dump): the full text is in crash.log
    static char summary[2048];
    Buf s{summary, sizeof(summary)};
    summary[0] = '\0';
    addLineStartingWith(s, g_buffer, "[");
    addLineStartingWith(s, g_buffer, "  layer was doing:");
    addLineStartingWith(s, g_buffer, "  verdict:");
    s.add("%s  (full banner with registers and stack: crash.log next to this file)\n", dumpLine);
    if (terminal) {
        appendRaw(g_logPath, summary, s.len);  // the process is dying: spdlog's file position no longer matters
    } else if (auto logger = spdlog::default_logger()) {
        // one line through spdlog (appending to its file behind its back would be overwritten by its next write)
        std::string line(summary, s.len);
        while (!line.empty() && (line.back() == '\n' || line.back() == ' ')) line.pop_back();
        for (char& c : line) {
            if (c == '\n') c = '|';
        }
        logger->error("{}", line);
    }
    g_busy.store(0);
}

// ---- handlers -------------------------------------------------------------------------------------------------------------

LONG CALLBACK vectoredHandler(EXCEPTION_POINTERS* ep) {
    const unsigned code = ep->ExceptionRecord->ExceptionCode;
    if (!isFatalClass(code) || t_trail.guardDepth > 0) {
        return EXCEPTION_CONTINUE_SEARCH;  // guarded(): its own filter reports the fault
    }
    if (g_firstChance.fetch_add(1) >= 12) {
        return EXCEPTION_CONTINUE_SEARCH;  // a noisy driver must not flood the log
    }
    const bool terminal = code == 0xC0000409 || code == 0xC0000374 || code == 0xC00000FD;
    g_lastAddress = reinterpret_cast<std::uintptr_t>(ep->ExceptionRecord->ExceptionAddress);
    g_lastCode = code;
    g_lastThread = GetCurrentThreadId();
    // the first fault of the process gets a dump; later first-chance ones are mostly the same story
    report(ep, terminal ? "fatal, not catchable" : "first chance, outside guarded()", terminal, /*wantDump=*/terminal || g_dumps.load() == 0);
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI topLevelFilter(EXCEPTION_POINTERS* ep) {
    const bool sameAsReported = g_lastCode == ep->ExceptionRecord->ExceptionCode &&
                                g_lastAddress == reinterpret_cast<std::uintptr_t>(ep->ExceptionRecord->ExceptionAddress) && g_lastThread == GetCurrentThreadId();
    if (sameAsReported) {
        char ts[32];
        timestamp(ts, sizeof(ts));
        char line[256];
        const int n = _snprintf_s(line, sizeof(line), _TRUNCATE, "%s [fatal] [t%lu] CRASH: the fault reported above was not handled; the process ends\n", ts,
                                  GetCurrentThreadId());
        appendRaw(g_crashLog, line, n > 0 ? n : 0);
        appendRaw(g_logPath, line, n > 0 ? n : 0);
    } else {
        report(ep, "UNHANDLED", /*terminal=*/true, /*wantDump=*/true);
    }
    if (g_chained != nullptr) {
        return g_chained(ep);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

LPTOP_LEVEL_EXCEPTION_FILTER WINAPI detourSetFilter(LPTOP_LEVEL_EXCEPTION_FILTER filter) {
    // somebody (the game's crash reporter) wants to be the top-level filter: it becomes the one we chain to, we stay the outermost
    LPTOP_LEVEL_EXCEPTION_FILTER previous = g_chained;
    g_chained = filter;
    return previous;
}

void logExit(const char* api, UINT code) {
    if (g_exitExpected.load()) return;
    void* frames[12] = {};
    const USHORT n = RtlCaptureStackBackTrace(1, 12, frames, nullptr);
    Buf b{g_buffer, sizeof(g_buffer)};
    int expected = 0;
    if (!g_busy.compare_exchange_strong(expected, 1)) return;
    char ts[32];
    timestamp(ts, sizeof(ts));
    const bool abnormal = code != 0;
    b.add("%s [%s] [t%lu] process: %s(0x%X) called by the process itself; the layer did not ask for it (%s). Callers:", ts, abnormal ? "warning" : "info",
          GetCurrentThreadId(), api, code, abnormal ? "a non-zero exit code: the engine or the crash reporter is giving up" : "normal quit");
    char where[160];
    for (USHORT i = 0; i < n; ++i) {
        describeAddress(frames[i], where, sizeof(where));
        b.add(" %s", where);
    }
    const std::string trail = breadcrumbs();
    if (!trail.empty()) b.add("\n  layer was doing: %s", trail.c_str());
    b.add("\n");
    if (abnormal) appendRaw(g_crashLog, g_buffer, b.len);
    appendRaw(g_logPath, g_buffer, b.len);
    g_busy.store(0);
}

void WINAPI detourExitProcess(UINT code) {
    logExit("ExitProcess", code);
    g_exitProcess.original(code);
}

BOOL WINAPI detourTerminateProcess(HANDLE process, UINT code) {
    if (process == GetCurrentProcess() || (process != nullptr && GetProcessId(process) == GetCurrentProcessId())) {
        logExit("TerminateProcess", code);
    }
    return g_terminateProcess.original(process, code);
}

void toWide(const std::filesystem::path& p, wchar_t* out, std::size_t cap) {
    out[0] = L'\0';
    const std::wstring w = p.wstring();
    if (w.size() + 1 < cap) {
        wcsncpy_s(out, cap, w.c_str(), _TRUNCATE);
    }
}

}  // namespace

// ---- public ---------------------------------------------------------------------------------------------------------------

DumpKind parseDumpKind(std::string_view text) {
    if (text == "mini" || text == "true" || text == "1") return DumpKind::Mini;
    if (text == "full") return DumpKind::Full;
    return DumpKind::Off;
}

const char* exceptionName(unsigned code) {
    switch (code) {
        case 0xC0000005: return "ACCESS_VIOLATION";
        case 0xC0000006: return "IN_PAGE_ERROR";
        case 0xC000001D: return "ILLEGAL_INSTRUCTION";
        case 0xC0000025: return "NONCONTINUABLE_EXCEPTION";
        case 0xC0000094: return "INT_DIVIDE_BY_ZERO";
        case 0xC0000096: return "PRIVILEGED_INSTRUCTION";
        case 0xC000008C: return "ARRAY_BOUNDS_EXCEEDED";
        case 0xC00000FD: return "STACK_OVERFLOW";
        case 0xC0000374: return "HEAP_CORRUPTION";
        case 0xC0000409: return "STACK_BUFFER_OVERRUN (fast fail)";
        case 0xE06D7363: return "C++ exception";
        case 0x80000003: return "BREAKPOINT";
        case 0xC0000008: return "INVALID_HANDLE";
        default: return "";
    }
}

std::string formatModuleOffset(std::string_view module, std::uint64_t rva) {
    char buf[96];
    if (module.empty()) {
        std::snprintf(buf, sizeof(buf), "0x%llx", static_cast<unsigned long long>(rva));
        return buf;
    }
    std::snprintf(buf, sizeof(buf), "+0x%llx", static_cast<unsigned long long>(rva));
    return std::string(module) + buf;
}

std::string baseName(std::string_view path) {
    const std::size_t slash = path.find_last_of("\\/");
    return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

Scope::Scope(std::string_view what, std::string_view detail) {
    Trail& t = t_trail;
    const int i = t.depth++;
    if (i < kTrailMax) {
        copyText(t.e[i].text, sizeof(t.e[i].text), what, detail);
        t.e[i].file = nullptr;
        t.e[i].line = 0;
    }
}

Scope::~Scope() {
    if (t_trail.depth > 0) --t_trail.depth;
}

GuardScope::GuardScope(const char* file, int line) {
    Trail& t = t_trail;
    const int i = t.depth++;
    ++t.guardDepth;
    if (i < kTrailMax) {
        t.e[i].text[0] = '\0';
        t.e[i].file = file;
        t.e[i].line = line;
    }
}

GuardScope::~GuardScope() {
    Trail& t = t_trail;
    if (t.depth > 0) --t.depth;
    if (t.guardDepth > 0) --t.guardDepth;
}

std::string breadcrumbs() {
    const Trail& t = t_trail;
    std::string out;
    const int n = t.depth < kTrailMax ? t.depth : kTrailMax;
    for (int i = 0; i < n; ++i) {
        if (!out.empty()) out += " > ";
        if (t.e[i].file != nullptr) {
            out += "guarded " + baseName(t.e[i].file) + ":" + std::to_string(t.e[i].line);
        } else {
            out += t.e[i].text;
        }
    }
    if (t.depth > kTrailMax) out += " > (+" + std::to_string(t.depth - kTrailMax) + " deeper)";
    return out;
}

void noteGuardedFault(unsigned code, _EXCEPTION_POINTERS* info) {
    const std::uint64_t n = ++g_guardedFaults;
    if (info == nullptr || info->ExceptionRecord == nullptr) return;
    if (code == 0xE06D7363) {
        return;  // a C++ exception thrown inside a guarded call: the old behaviour (caught here) is kept, it is not a hardware fault
    }
    if (n > 40 && n % 500 != 0) return;  // the hardening scenario faults on purpose, thousands of times
    char where[160];
    describeAddress(info->ExceptionRecord->ExceptionAddress, where, sizeof(where));
    char fault[160];
    Buf b{fault, sizeof(fault)};
    addFault(b, info->ExceptionRecord);
    spdlog::warn("guarded call #{} faulted: {} at {} (layer was doing: {})", n, fault, where, breadcrumbs());
}

std::uint64_t guardedFaultCount() { return g_guardedFaults.load(); }

void setGameThread() { g_gameThread = GetCurrentThreadId(); }
unsigned long gameThreadId() { return g_gameThread.load(); }

std::string addressText(const void* address) {
    char text[200];
    describeAddress(address, text, sizeof(text));
    return text;
}

void reportCaught(_EXCEPTION_POINTERS* info, const char* what) {
    static std::atomic<int> reported{0};
    if (info == nullptr || reported.fetch_add(1) >= 3) return;
    report(info, what, /*terminal=*/false, /*wantDump=*/true);
}
void expectExit() { g_exitExpected = true; }

void install(const Options& options) {
    if (g_installed.exchange(true)) return;
    g_options = options;
    toWide(options.logFile, g_logPath, _countof(g_logPath));
    if (!options.logFile.empty()) {
        toWide(options.logFile.parent_path() / "crash.log", g_crashLog, _countof(g_crashLog));
    }
    if (options.dump != DumpKind::Off && !options.dumpDir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(options.dumpDir, ec);
        toWide(options.dumpDir, g_dumpDir, _countof(g_dumpDir));
    }
    HMODULE self = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&vectoredHandler), &self)) {
        g_selfBase = reinterpret_cast<std::uintptr_t>(self);
    }
    AddVectoredExceptionHandler(1, vectoredHandler);
    g_chained = SetUnhandledExceptionFilter(topLevelFilter);
}

void installHooks() {
    if (!g_installed.load()) return;
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (k32 == nullptr) return;
    g_setFilter.install("SetUnhandledExceptionFilter", reinterpret_cast<decltype(g_setFilter.original)>(GetProcAddress(k32, "SetUnhandledExceptionFilter")),
                        &detourSetFilter);
    g_exitProcess.install("ExitProcess", reinterpret_cast<decltype(g_exitProcess.original)>(GetProcAddress(k32, "ExitProcess")), &detourExitProcess);
    g_terminateProcess.install("TerminateProcess", reinterpret_cast<decltype(g_terminateProcess.original)>(GetProcAddress(k32, "TerminateProcess")),
                               &detourTerminateProcess);
}

}  // namespace spass::crash
