#include "game/folder_redirect.h"

#include <windows.h>

#include <cstring>
#include <string>

#include <spdlog/spdlog.h>

#include "hook/hook_manager.h"

namespace spass::game {
namespace {

constexpr int kCsidlAppData = 0x1a;
constexpr int kCsidlLocalAppData = 0x1c;
constexpr int kCsidlMask = 0xff;  // the high bits are CSIDL_FLAG_* (create, dont-verify, ...)

using GetFolderPathA_t = HRESULT(WINAPI*)(HWND, int, HANDLE, DWORD, LPSTR);
using GetSpecialFolderPathA_t = BOOL(WINAPI*)(HWND, LPSTR, int, BOOL);
Hook<GetFolderPathA_t> g_getFolder;
Hook<GetSpecialFolderPathA_t> g_getSpecial;
std::string g_root;

bool redirected(int csidl, LPSTR out) {
    const int id = csidl & kCsidlMask;
    if (id != kCsidlAppData && id != kCsidlLocalAppData) {
        return false;
    }
    const std::string path = g_root + (id == kCsidlAppData ? "\\Roaming" : "\\Local");
    CreateDirectoryA(g_root.c_str(), nullptr);
    CreateDirectoryA(path.c_str(), nullptr);
    std::strncpy(out, path.c_str(), MAX_PATH - 1);
    out[MAX_PATH - 1] = '\0';
    return true;
}

HRESULT WINAPI detourGetFolder(HWND hwnd, int csidl, HANDLE token, DWORD flags, LPSTR out) {
    if (out != nullptr && redirected(csidl, out)) {
        return S_OK;
    }
    return g_getFolder.original(hwnd, csidl, token, flags, out);
}

BOOL WINAPI detourGetSpecial(HWND hwnd, LPSTR out, int csidl, BOOL create) {
    if (out != nullptr && redirected(csidl, out)) {
        return TRUE;
    }
    return g_getSpecial.original(hwnd, out, csidl, create);
}

}  // namespace

bool installFolderRedirect(const std::filesystem::path& dir) {
    HMODULE shell = LoadLibraryW(L"shell32.dll");
    if (shell == nullptr) {
        return false;
    }
    g_root = std::filesystem::absolute(dir).string();
    const bool a = g_getFolder.install("SHGetFolderPathA", reinterpret_cast<GetFolderPathA_t>(GetProcAddress(shell, "SHGetFolderPathA")), &detourGetFolder);
    const bool b = g_getSpecial.install("SHGetSpecialFolderPathA", reinterpret_cast<GetSpecialFolderPathA_t>(GetProcAddress(shell, "SHGetSpecialFolderPathA")), &detourGetSpecial);
    if (a || b) {
        spdlog::warn("debug: %LOCALAPPDATA%/%APPDATA% of the game are redirected to {}", g_root);
    }
    return a || b;
}

}  // namespace spass::game
