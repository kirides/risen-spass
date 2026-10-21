#include "game/background_window.h"

#include <windows.h>

#include <intrin.h>

#include <atomic>
#include <filesystem>
#include <string>

#include <spdlog/spdlog.h>

#include "hook/hook_manager.h"

#pragma intrinsic(_ReturnAddress)

namespace spass::game {
namespace {

using ShowWindow_t = BOOL(WINAPI*)(HWND, int);
using SetWindowPos_t = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);
using SetForegroundWindow_t = BOOL(WINAPI*)(HWND);
using BringWindowToTop_t = BOOL(WINAPI*)(HWND);
using SetActiveWindow_t = HWND(WINAPI*)(HWND);
using SetCursorPos_t = BOOL(WINAPI*)(int, int);
using ClipCursor_t = BOOL(WINAPI*)(const RECT*);
using CreateWindowExW_t = HWND(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
using CreateWindowExA_t = HWND(WINAPI*)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);

Hook<ShowWindow_t> g_showWindow;
Hook<ShowWindow_t> g_showWindowAsync;
Hook<SetWindowPos_t> g_setWindowPos;
Hook<SetForegroundWindow_t> g_setForeground;
Hook<BringWindowToTop_t> g_bringToTop;
Hook<SetActiveWindow_t> g_setActive;
Hook<SetCursorPos_t> g_setCursorPos;
Hook<ClipCursor_t> g_clipCursor;
Hook<CreateWindowExW_t> g_createW;
Hook<CreateWindowExA_t> g_createA;

// "Engine.dll+0x51a190": who asked (the first few interceptions of each kind are logged)
std::string caller(void* ret) {
    HMODULE mod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(ret), &mod) ||
        mod == nullptr) {
        return fmt::format("{}", ret);
    }
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(mod, path, MAX_PATH);
    return fmt::format("{}+{:#x}", std::filesystem::path(path).filename().string(),
                       reinterpret_cast<uintptr_t>(ret) - reinterpret_cast<uintptr_t>(mod));
}

// Logs the first 8 interceptions of one kind, then only counts them.
struct Counter {
    std::atomic<int> n{0};
    bool logNext() { return n.fetch_add(1) < 8; }
};
Counter g_cShow, g_cPos, g_cFg, g_cTop, g_cActive, g_cCursor, g_cClip, g_cCreate;

bool ownProcess(HWND h) {
    DWORD pid = 0;
    return h != nullptr && GetWindowThreadProcessId(h, &pid) != 0 && pid == GetCurrentProcessId();
}

// A top-level window of this process (child windows never activate anything themselves).
bool ownTopLevel(HWND h) { return ownProcess(h) && (GetWindowLongPtrW(h, GWL_STYLE) & WS_CHILD) == 0; }

// The user works in another program: the game must stay behind it and keep its hands off the cursor.
bool inBackground() { return !ownProcess(GetForegroundWindow()); }

// SW_* that activate -> the variant that shows without activating
int nonActivating(int cmd) {
    switch (cmd) {
        case SW_SHOWNORMAL:   // 1
        case SW_RESTORE:      // 9
        case SW_SHOWDEFAULT:  // 10
            return SW_SHOWNOACTIVATE;
        case SW_SHOW:  // 5
            return SW_SHOWNA;
        case SW_SHOWMINIMIZED:  // 2
            return SW_SHOWMINNOACTIVE;
        default:
            return cmd;  // SW_HIDE, SW_MINIMIZE, SW_SHOWNOACTIVATE, SW_SHOWNA, SW_SHOWMINNOACTIVE, SW_SHOWMAXIMIZED (no non-activating form)
    }
}

int mapShow(HWND h, int cmd, void* ret, const char* fn) {
    if (!ownTopLevel(h) || !inBackground()) {
        return cmd;
    }
    const int mapped = nonActivating(cmd);
    if (mapped != cmd && g_cShow.logNext()) {
        spdlog::info("background: {}(cmd {} -> {}) from {}", fn, cmd, mapped, caller(ret));
    }
    return mapped;
}

BOOL WINAPI detourShowWindow(HWND h, int cmd) {
    return g_showWindow.original(h, mapShow(h, cmd, _ReturnAddress(), "ShowWindow"));
}

BOOL WINAPI detourShowWindowAsync(HWND h, int cmd) {
    return g_showWindowAsync.original(h, mapShow(h, cmd, _ReturnAddress(), "ShowWindowAsync"));
}

BOOL WINAPI detourSetWindowPos(HWND h, HWND after, int x, int y, int cx, int cy, UINT flags) {
    if (ownTopLevel(h) && inBackground()) {
        UINT f = flags | SWP_NOACTIVATE;
        // to the top of the z-order would put the game in front of the user's window, without activating it
        if ((f & SWP_NOZORDER) == 0 && (after == HWND_TOP || after == HWND_TOPMOST)) {
            f |= SWP_NOZORDER;
        }
        if (f != flags && g_cPos.logNext()) {
            spdlog::info("background: SetWindowPos(flags {:#x} -> {:#x}) from {}", flags, f, caller(_ReturnAddress()));
        }
        flags = f;
    }
    return g_setWindowPos.original(h, after, x, y, cx, cy, flags);
}

BOOL WINAPI detourSetForegroundWindow(HWND h) {
    if (ownProcess(h) && inBackground()) {
        if (g_cFg.logNext()) {
            spdlog::info("background: SetForegroundWindow dropped (from {})", caller(_ReturnAddress()));
        }
        return TRUE;
    }
    return g_setForeground.original(h);
}

BOOL WINAPI detourBringWindowToTop(HWND h) {
    if (ownTopLevel(h) && inBackground()) {
        if (g_cTop.logNext()) {
            spdlog::info("background: BringWindowToTop dropped (from {})", caller(_ReturnAddress()));
        }
        return TRUE;
    }
    return g_bringToTop.original(h);
}

HWND WINAPI detourSetActiveWindow(HWND h) {
    if (ownTopLevel(h) && inBackground()) {
        if (g_cActive.logNext()) {
            spdlog::info("background: SetActiveWindow dropped (from {})", caller(_ReturnAddress()));
        }
        return GetActiveWindow();
    }
    return g_setActive.original(h);
}

BOOL WINAPI detourSetCursorPos(int x, int y) {
    if (inBackground()) {
        if (g_cCursor.logNext()) {
            spdlog::info("background: SetCursorPos({}, {}) dropped (from {})", x, y, caller(_ReturnAddress()));
        }
        return TRUE;
    }
    return g_setCursorPos.original(x, y);
}

BOOL WINAPI detourClipCursor(const RECT* r) {
    if (r != nullptr && inBackground()) {  // ClipCursor(nullptr) releases a clip: always fine
        if (g_cClip.logNext()) {
            spdlog::info("background: ClipCursor({},{},{},{}) dropped (from {})", r->left, r->top, r->right, r->bottom,
                         caller(_ReturnAddress()));
        }
        return TRUE;
    }
    return g_clipCursor.original(r);
}

// A top-level window created with WS_VISIBLE is shown (and activated) inside CreateWindowEx: create it hidden, show it
// without activation.
bool stripVisible(DWORD& style, HWND parent) {
    if ((style & WS_VISIBLE) == 0 || (style & WS_CHILD) != 0 || (parent != nullptr && parent != HWND_DESKTOP) || !inBackground()) {
        return false;
    }
    style &= ~WS_VISIBLE;
    return true;
}

void showCreated(HWND h, DWORD style, void* ret) {
    if (h == nullptr) {
        return;
    }
    if (g_cCreate.logNext()) {
        spdlog::info("background: CreateWindowEx(WS_VISIBLE) shown without activation (from {})", caller(ret));
    }
    const int cmd = (style & WS_MAXIMIZE) ? SW_SHOWMAXIMIZED : (style & WS_MINIMIZE) ? SW_SHOWMINNOACTIVE : SW_SHOWNA;
    g_showWindow.original(h, cmd);
}

HWND WINAPI detourCreateWindowExW(DWORD ex, LPCWSTR cls, LPCWSTR name, DWORD style, int x, int y, int w, int h, HWND parent,
                                  HMENU menu, HINSTANCE inst, LPVOID param) {
    if (!stripVisible(style, parent)) {
        return g_createW.original(ex, cls, name, style, x, y, w, h, parent, menu, inst, param);
    }
    HWND wnd = g_createW.original(ex, cls, name, style, x, y, w, h, parent, menu, inst, param);
    showCreated(wnd, style, _ReturnAddress());
    return wnd;
}

HWND WINAPI detourCreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR name, DWORD style, int x, int y, int w, int h, HWND parent,
                                  HMENU menu, HINSTANCE inst, LPVOID param) {
    if (!stripVisible(style, parent)) {
        return g_createA.original(ex, cls, name, style, x, y, w, h, parent, menu, inst, param);
    }
    HWND wnd = g_createA.original(ex, cls, name, style, x, y, w, h, parent, menu, inst, param);
    showCreated(wnd, style, _ReturnAddress());
    return wnd;
}

template <class Fn>
bool hookUser32(HMODULE u32, Hook<Fn>& hook, const char* name, Fn detour) {
    if (hook.install(name, reinterpret_cast<Fn>(GetProcAddress(u32, name)), detour)) {
        return true;
    }
    spdlog::warn("background: could not hook {}", name);
    return false;
}

}  // namespace

bool installBackgroundWindow() {
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (u32 == nullptr) {
        u32 = LoadLibraryW(L"user32.dll");
    }
    if (u32 == nullptr) {
        return false;
    }
    bool ok = true;
    ok &= hookUser32(u32, g_showWindow, "ShowWindow", &detourShowWindow);
    ok &= hookUser32(u32, g_showWindowAsync, "ShowWindowAsync", &detourShowWindowAsync);
    ok &= hookUser32(u32, g_setWindowPos, "SetWindowPos", &detourSetWindowPos);
    ok &= hookUser32(u32, g_setForeground, "SetForegroundWindow", &detourSetForegroundWindow);
    ok &= hookUser32(u32, g_bringToTop, "BringWindowToTop", &detourBringWindowToTop);
    ok &= hookUser32(u32, g_setActive, "SetActiveWindow", &detourSetActiveWindow);
    ok &= hookUser32(u32, g_setCursorPos, "SetCursorPos", &detourSetCursorPos);
    ok &= hookUser32(u32, g_clipCursor, "ClipCursor", &detourClipCursor);
    ok &= hookUser32(u32, g_createW, "CreateWindowExW", &detourCreateWindowExW);
    ok &= hookUser32(u32, g_createA, "CreateWindowExA", &detourCreateWindowExA);
    spdlog::info("debug: background mode: the game window does not take the foreground and leaves the mouse cursor alone while "
                 "another program is in front (config.toml [debug] background)");
    return ok;
}

}  // namespace spass::game
