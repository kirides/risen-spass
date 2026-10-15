#include "game/instance_mutex.h"

#include <windows.h>

#include <cstring>

#include <spdlog/spdlog.h>

#include "hook/hook_manager.h"

namespace spass::game {
namespace {

using CreateMutexA_t = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR);
using CreateMutexW_t = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, BOOL, LPCWSTR);
using OpenMutexA_t = HANDLE(WINAPI*)(DWORD, BOOL, LPCSTR);
using OpenMutexW_t = HANDLE(WINAPI*)(DWORD, BOOL, LPCWSTR);
Hook<CreateMutexA_t> g_createA;
Hook<CreateMutexW_t> g_createW;
Hook<OpenMutexA_t> g_openA;
Hook<OpenMutexW_t> g_openW;
std::string g_suffix;  // "_<id>"

constexpr const char* kName = "GenomeMutex";

bool isEngineMutex(LPCSTR name) { return name != nullptr && std::strcmp(name, kName) == 0; }
bool isEngineMutex(LPCWSTR name) { return name != nullptr && std::wcscmp(name, L"GenomeMutex") == 0; }

std::string renamedA() { return std::string(kName) + g_suffix; }
std::wstring renamedW() {
    const std::string a = renamedA();
    return std::wstring(a.begin(), a.end());
}

HANDLE WINAPI detourCreateA(LPSECURITY_ATTRIBUTES sa, BOOL owner, LPCSTR name) {
    if (isEngineMutex(name)) {
        const std::string n = renamedA();
        return g_createA.original(sa, owner, n.c_str());
    }
    return g_createA.original(sa, owner, name);
}

HANDLE WINAPI detourCreateW(LPSECURITY_ATTRIBUTES sa, BOOL owner, LPCWSTR name) {
    if (isEngineMutex(name)) {
        const std::wstring n = renamedW();
        return g_createW.original(sa, owner, n.c_str());
    }
    return g_createW.original(sa, owner, name);
}

HANDLE WINAPI detourOpenA(DWORD access, BOOL inherit, LPCSTR name) {
    if (isEngineMutex(name)) {
        const std::string n = renamedA();
        return g_openA.original(access, inherit, n.c_str());
    }
    return g_openA.original(access, inherit, name);
}

HANDLE WINAPI detourOpenW(DWORD access, BOOL inherit, LPCWSTR name) {
    if (isEngineMutex(name)) {
        const std::wstring n = renamedW();
        return g_openW.original(access, inherit, n.c_str());
    }
    return g_openW.original(access, inherit, name);
}

}  // namespace

bool installInstanceMutex(const std::string& id) {
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (k32 == nullptr || id.empty()) {
        return false;
    }
    g_suffix = "_" + id;
    const bool a = g_createA.install("CreateMutexA", reinterpret_cast<CreateMutexA_t>(GetProcAddress(k32, "CreateMutexA")), &detourCreateA);
    const bool w = g_createW.install("CreateMutexW", reinterpret_cast<CreateMutexW_t>(GetProcAddress(k32, "CreateMutexW")), &detourCreateW);
    g_openA.install("OpenMutexA", reinterpret_cast<OpenMutexA_t>(GetProcAddress(k32, "OpenMutexA")), &detourOpenA);
    g_openW.install("OpenMutexW", reinterpret_cast<OpenMutexW_t>(GetProcAddress(k32, "OpenMutexW")), &detourOpenW);
    if (a || w) {
        spdlog::info("debug: instance '{}': the engine's single-instance mutex is named {}{}", id, kName, g_suffix);
    }
    return a || w;
}

}  // namespace spass::game
