// Opening a file with the system's program for it (behaviour scripts in the user's code editor). Its own file: <windows.h> must stay away from the GL headers.
#include <string>

#include "core/paths.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <cstdlib>
#endif

namespace mk {

bool open_in_os(const std::string& path) {
#ifdef _WIN32
    const std::wstring w = u8(path).wstring();
    // "edit" first: a .lua file may be associated with an interpreter for "open"
    for (const wchar_t* verb : {L"edit", L"open"}) {
        const auto r = reinterpret_cast<intptr_t>(ShellExecuteW(nullptr, verb, w.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        if (r > 32) return true;
    }
    // no program for the type: Notepad is always there
    return reinterpret_cast<intptr_t>(ShellExecuteW(nullptr, L"open", L"notepad.exe", (L"\"" + w + L"\"").c_str(), nullptr, SW_SHOWNORMAL)) > 32;
#elif defined(__APPLE__)
    return std::system(("open -t \"" + path + "\"").c_str()) == 0;
#else
    return std::system(("xdg-open \"" + path + "\" >/dev/null 2>&1 &").c_str()) == 0;
#endif
}

}  // namespace mk
