#include "hook/exports.h"

#include <cstdint>
#include <cwchar>
#include <string>

#include <spdlog/spdlog.h>

namespace spass::exports {
namespace {

std::string narrow(const wchar_t* w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) {
        return {};
    }
    std::string s(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

}  // namespace

void* resolve(const wchar_t* module, const char* symbol) {
    const HMODULE mod = GetModuleHandleW(module);
    if (mod == nullptr) {
        spdlog::warn("exports: module {} is not loaded (looking for {})", narrow(module), symbol);
        return nullptr;
    }
    void* p = reinterpret_cast<void*>(GetProcAddress(mod, symbol));
    if (p == nullptr) {
        spdlog::warn("exports: symbol not found: {}", symbol);
    }
    return p;
}

std::uintptr_t moduleBase(const wchar_t* module) {
    return reinterpret_cast<std::uintptr_t>(GetModuleHandleW(module));
}

}  // namespace spass::exports
