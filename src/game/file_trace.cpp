#include <windows.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>

#include <spdlog/spdlog.h>

#include "game/bindings.h"
#include "hook/hook_manager.h"
#include "lua/runtime.h"

// Development aid: logs the engine's file opens (CreateFileA/W) whose path contains a substring, together with the
// access mode, disposition and result. Answers "which file does the engine really read/write here?"
//   risen.debug.trace_files("SaveGames")   -- start tracing paths containing that text (case-insensitive)
//   risen.debug.trace_files("")            -- stop

namespace spass::game {
namespace {

using CreateFileW_t = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using CreateFileA_t = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
Hook<CreateFileW_t> g_createW;
Hook<CreateFileA_t> g_createA;

std::mutex g_mutex;
std::string g_filter;  // lower-case; empty = off
std::atomic<bool> g_active{false};
thread_local bool t_inside = false;

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string narrow(LPCWSTR w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 1 ? n - 1 : 0, '\0');
    if (n > 1) {
        WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    }
    return s;
}

void trace(const std::string& path, DWORD access, DWORD disposition, HANDLE result, DWORD err) {
    std::string filter;
    {
        std::lock_guard lock(g_mutex);
        filter = g_filter;
    }
    if (filter.empty() || lower(path).find(filter) == std::string::npos) {
        return;
    }
    spdlog::info("file: {} {} disp={} -> {} (error {})", (access & GENERIC_WRITE) || (access & FILE_WRITE_DATA) ? "write" : "read",
                 path, disposition, result == INVALID_HANDLE_VALUE ? "FAILED" : "ok", err);
}

HANDLE WINAPI detourW(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl) {
    HANDLE h = g_createW.original(name, access, share, sa, disp, flags, tmpl);
    if (g_active && !t_inside && name != nullptr) {
        const DWORD err = GetLastError();
        t_inside = true;
        trace(narrow(name), access, disp, h, err);
        t_inside = false;
        SetLastError(err);
    }
    return h;
}

HANDLE WINAPI detourA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl) {
    HANDLE h = g_createA.original(name, access, share, sa, disp, flags, tmpl);
    if (g_active && !t_inside && name != nullptr) {
        const DWORD err = GetLastError();
        t_inside = true;
        trace(name, access, disp, h, err);
        t_inside = false;
        SetLastError(err);
    }
    return h;
}

}  // namespace

void installFileTrace() {
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (k32 == nullptr) {
        return;
    }
    g_createW.install("CreateFileW", reinterpret_cast<CreateFileW_t>(GetProcAddress(k32, "CreateFileW")), &detourW);
    g_createA.install("CreateFileA", reinterpret_cast<CreateFileA_t>(GetProcAddress(k32, "CreateFileA")), &detourA);
}

void setFileTraceFilter(const std::string& filter) {
    std::lock_guard lock(g_mutex);
    g_filter = lower(filter);
    g_active = !g_filter.empty();
}

void bindDebug(sol::state_view lua, sol::table risen) {
    sol::table dbg = lua.create_table();
    dbg.set_function("trace_files", [](const std::string& filter) {
        std::lock_guard lock(g_mutex);
        g_filter = lower(filter);
        g_active = !g_filter.empty();
        spdlog::info("file trace: {}", g_active ? g_filter : std::string("off"));
    });
    risen["debug"] = dbg;
}

}  // namespace spass::game
