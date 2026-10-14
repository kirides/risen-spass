#pragma once

#include <string_view>

#include <windows.h>

// Risen's engine DLLs export their C++ classes with MSVC-decorated names, so we bind to
// `GetProcAddress("?CallScript@gCScriptAdmin@@QEAA_JAEBVbCString@@PEAVeCEntity@@1_J@Z")`
// instead of hard-coding addresses. See docs/RE.md.
namespace spass::exports {

// Returns the address of `symbol` in an already loaded module, or nullptr.
// Never loads libraries (safe under the loader lock).
void* resolve(const wchar_t* module, const char* symbol);

template <class Fn>
Fn resolveAs(const wchar_t* module, const char* symbol) {
    return reinterpret_cast<Fn>(resolve(module, symbol));
}

// Module base for RVA-based fallbacks (only used where a symbol is not exported).
std::uintptr_t moduleBase(const wchar_t* module);

}  // namespace spass::exports
