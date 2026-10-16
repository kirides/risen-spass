#include "game/abi.h"

#include <windows.h>

#include "common/crash.h"

namespace spass::game {

std::wstring UString::widen(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), w.data(), n);
    return w;
}

std::string UString::narrow(const wchar_t* w) {
    if (w == nullptr || *w == L'\0') {
        return {};
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n > 0 ? n - 1 : 0), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

namespace {
int filter(unsigned code, EXCEPTION_POINTERS* info) {
    crash::noteGuardedFault(code, info);
    return EXCEPTION_EXECUTE_HANDLER;
}

// The __try lives in its own function: it must not share a frame with the GuardScope object.
bool runSeh(void (*fn)(void*), void* ctx) {
    __try {
        fn(ctx);
        return true;
    } __except (filter(GetExceptionCode(), GetExceptionInformation())) {
        return false;
    }
}
}  // namespace

bool invokeGuarded(void (*fn)(void*), void* ctx, const char* file, int line) {
    crash::GuardScope scope(file != nullptr ? file : "?", line);
    return runSeh(fn, ctx);
}

}  // namespace spass::game
