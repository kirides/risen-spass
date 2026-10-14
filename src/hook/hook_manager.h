#pragma once

#include <string>
#include <string_view>

namespace spass::hooks {

// Thin MinHook wrapper. All hooks are created disabled and enabled together via enableAll()
// so partially installed state is never visible to the game.
bool initialize();
void shutdown();

bool create(std::string_view name, void* target, void* detour, void** original);
bool enableAll();

}  // namespace spass::hooks

namespace spass {

// Typed hook: keeps the trampoline to the original function.
//
//   static Hook<__int64(*)(void*, void*)> g_hook;
//   g_hook.install("name", target, &detour);   // then call g_hook.original(...)
template <class Fn>
class Hook {
public:
    Fn original = nullptr;

    bool install(std::string_view name, Fn target, Fn detour) {
        if (target == nullptr) {
            return false;
        }
        return hooks::create(name, reinterpret_cast<void*>(target), reinterpret_cast<void*>(detour),
                             reinterpret_cast<void**>(&original));
    }

    bool installed() const { return original != nullptr; }
};

}  // namespace spass
