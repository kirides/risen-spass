#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <spdlog/spdlog.h>

#include "common/trace.h"
#include "game/abi.h"
#include "game/bindings.h"
#include "game/lua_entity.h"
#include "hook/hook_manager.h"

// The `calls` and `hud` trace channels (docs/DEVELOPMENT.md "Testing and tracing", docs/RE.md section 31): they show what the ENGINE does, which no Lua
// script can see, so two engine paths (the animated interaction and a mod's shortcut) can be compared line by line.
//
//   calls  `[debug] trace_scripts = "Interact,Loot,Assess"`: every call through an eCScriptProxyScript (the scripts that data refers to by name: the
//          CanInteract/PostInteract scripts of an interaction, item and lock scripts, perception, ...), every AI function the script admin starts
//          (PreInteract_*, Interact_*, Unlock, ...: logged when the running function of an AI unit changes) and every AI state change
//          (ZS_Interact_PreInteract, ...) whose name contains one of the substrings. Nothing is logged without the list.
//   hud    every PrintGameLog / PrintItemLog of the GUI (text and type, item and amount), also the engine's own.
//
// Both hooks are only installed when the channel is on at start, so a normal run pays nothing.

namespace spass::game {
namespace {

std::vector<std::string> g_filter;

Hook<std::int64_t (*)(const void*, void*, void*, std::int64_t)> g_proxyCall;
Hook<bool (*)(void*, const void*, void*, void*)> g_runFunction;
Hook<int (*)(void*, void*)> g_setState;
Hook<void (*)(void*, const void*, int, bool, char*)> g_printLog;
Hook<void (*)(void*, void*, int)> g_printItem;

std::mutex g_mutex;
std::unordered_map<void*, std::string> g_lastFunction;  // AI unit -> the function that was running (log changes only)

bool matches(const std::string& name) {
    if (g_filter.empty() || name.empty()) {
        return false;
    }
    for (const std::string& f : g_filter) {
        if (f[0] == '^' ? name.compare(0, f.size() - 1, f, 1, std::string::npos) == 0 : name.find(f) != std::string::npos) {  // "^Interact_" = starts with
            return true;
        }
    }
    return false;
}

std::string bstr(const void* s) { return s != nullptr ? std::string(sym().bstring_get_text(s)) : std::string(); }

std::string nameOfEntity(void* e) { return e != nullptr ? entityNameOf(e) : std::string("-"); }

std::int64_t detourProxyCall(const void* proxy, void* self, void* other, std::int64_t arg) {
    std::string name;
    guarded([&] {
        if (sym().script_proxy_name != nullptr && proxy != nullptr) {
            name = bstr(sym().script_proxy_name(proxy));
        }
    });
    const bool log = matches(name);
    if (log) {
        trace::emit(trace::Channel::Calls, "script " + name + "(self=" + nameOfEntity(self) + ", other=" + nameOfEntity(other) + ", arg=" + std::to_string(arg) + ")");
    }
    const std::int64_t r = g_proxyCall.original(proxy, self, other, arg);
    if (log) {
        trace::emit(trace::Channel::Calls, "script " + name + " -> " + std::to_string(r));
    }
    return r;
}

// gCScriptAdmin::RunScriptFunction(name, stack, unit): called every frame while an AI function is unfinished; log when a unit starts another one.
bool detourRunFunction(void* admin, const void* name, void* stack, void* unit) {
    const std::string n = bstr(name);
    {
        std::lock_guard lock(g_mutex);
        std::string& last = g_lastFunction[unit];
        const bool changed = last != n;
        last = n;
        if (changed && matches(n)) {
            std::string who = "-";
            guarded([&] {
                if (sym().unit_get_self != nullptr) {
                    who = nameOfEntity(sym().unit_get_self(unit));
                }
            });
            trace::emit(trace::Channel::Calls, "ai-function " + n + " (unit of " + who + ")");
        }
    }
    return g_runFunction.original(admin, name, stack, unit);
}

int detourSetState(void* unit, void* name) {
    const std::string n = bstr(name);
    {
        std::lock_guard lock(g_mutex);
        g_lastFunction.erase(unit);
    }
    if (matches(n)) {
        std::string who = "-";
        guarded([&] {
            if (sym().unit_get_self != nullptr) {
                who = nameOfEntity(sym().unit_get_self(unit));
            }
        });
        trace::emit(trace::Channel::Calls, "ai-state " + n + " (unit of " + who + ")");
    }
    return g_setState.original(unit, name);
}

void detourPrintLog(void* gui, const void* text, int type, bool flag, char* extra) {
    SPASS_TRACE(trace::Channel::Hud, "game log (type {}): {}", type, UString::narrow(sym().ustring_get_text(text)));
    g_printLog.original(gui, text, type, flag, extra);
}

void detourPrintItem(void* gui, void* item, int amount) {
    std::string name = "?";
    guarded([&] {
        if (item != nullptr) {
            name = nameOfEntity(*static_cast<void**>(item));
        }
    });
    SPASS_TRACE(trace::Channel::Hud, "item log: {} x{}", name, amount);
    g_printItem.original(gui, item, amount);
}

Hook<bool (*)(const void*, const void*)> g_guiSound;
Hook<bool (*)(const void*, int, float, void*)> g_play2d;
Hook<bool (*)(const void*, const void*)> g_play3d;
using EffectFn = void* (*)(void*, const void*, const void*, const void*);
Hook<EffectFn> g_effect[6];

bool detourGuiSound(const void* window, const void* name) {
    SPASS_TRACE(trace::Channel::Sound, "gui sound '{}'", bstr(name));
    return g_guiSound.original(window, name);
}
bool detourPlay2d(const void* name, int group, float volume, void* channel) {
    SPASS_TRACE(trace::Channel::Sound, "play2d '{}' group {} volume {}", bstr(name), group, volume);
    return g_play2d.original(name, group, volume, channel);
}
bool detourPlay3d(const void* name, const void* play) {
    SPASS_TRACE(trace::Channel::Sound, "play3d '{}'", bstr(name));
    return g_play3d.original(name, play);
}
Hook<void* (*)(const void*)> g_getSound;
Hook<bool (*)(void*, const void*, bool)> g_psEffect;
void* detourGetSound(const void* name) {
    SPASS_TRACE(trace::Channel::Sound, "sound resource '{}'", bstr(name));
    return g_getSound.original(name);
}
bool detourPsEffect(void* self, const void* name, bool flag) {
    SPASS_TRACE(trace::Channel::Sound, "effect (PSEffect) '{}' flag {}", bstr(name), flag);
    return g_psEffect.original(self, name, flag);
}
std::mutex g_effectMutex;
std::map<std::string, int> g_effectStarts;  // names starting with "eff_" -> how often the engine started them (risen.effect.count)

template <int N>
void* detourEffect(void* ret, const void* name, const void* a, const void* b) {
    guarded([&] {
        const std::string n = bstr(name);
        if (n.compare(0, 4, "eff_") == 0) {
            std::lock_guard lock(g_effectMutex);
            ++g_effectStarts[n];
        }
    });
    if (trace::enabled(trace::Channel::Sound)) {
        std::string target;
        if (N == 1 && a != nullptr) {  // (name, Entity const&): the Entity is a one-pointer holder
            guarded([&] { target = nameOfEntity(*static_cast<void* const*>(a)); });
        }
        SPASS_TRACE(trace::Channel::Sound, "effect '{}' (overload {}){}", bstr(name), N, target.empty() ? std::string() : " on " + target);
    }
    return g_effect[N].original(ret, name, a, b);  // two-parameter overloads ignore the extra register arguments
}

}  // namespace

int effectStartCount(const std::string& prefix) {
    std::lock_guard lock(g_effectMutex);
    int n = 0;
    for (const auto& [name, count] : g_effectStarts) {
        if (name.compare(0, prefix.size(), prefix) == 0) {
            n += count;
        }
    }
    return n;
}

// The effect hooks are always on (they count the "eff_*" effects for risen.effect.count); the sound hooks only with the `sound` trace channel.
void installSoundTrace() {
    const Symbols& s = sym();
    g_effect[0].install("EffectSystem::StartEffect#0", s.fx_start_e1, &detourEffect<0>);
    g_effect[1].install("EffectSystem::StartEffect#1", s.fx_start_e, &detourEffect<1>);
    g_effect[2].install("EffectSystem::StartEffect#2", s.fx_start_m, &detourEffect<2>);
    g_effect[3].install("EffectSystem::StartEffect#3", s.fx_start_me, &detourEffect<3>);
    g_effect[4].install("EffectSystem::StartEffect#4", s.fx_start_v, &detourEffect<4>);
    g_effect[5].install("EffectSystem::StartEffect#5", s.fx_start_ve, &detourEffect<5>);
    if (!trace::enabled(trace::Channel::Sound)) {
        return;
    }
    g_guiSound.install("eCGuiWindow2::PlaySoundA", s.snd_gui_play, &detourGuiSound);
    g_play2d.install("eCSoundResource2::Play2D(name)", s.snd_play2d_named, &detourPlay2d);
    g_play3d.install("eCSoundResource2::Play3D(name)", s.snd_play3d_named, &detourPlay3d);
    g_getSound.install("eCResource2::GetSound", s.snd_get_sound, &detourGetSound);
    g_psEffect.install("PSEffect::StartEffect", s.fx_ps_start, &detourPsEffect);
}

void installCallTrace(const std::string& scriptFilter) {
    if (!trace::enabled(trace::Channel::Calls)) {
        return;
    }
    std::string token;
    for (const char c : scriptFilter + ",") {
        if (c == ',' || c == ';') {
            if (!token.empty()) {
                g_filter.push_back(token);
            }
            token.clear();
        } else if (c != ' ') {
            token += c;
        }
    }
    if (g_filter.empty()) {
        spdlog::warn("trace channel 'calls' is on but [debug] trace_scripts is empty: nothing will be logged");
        return;
    }
    const Symbols& s = sym();
    if (s.script_proxy_call != nullptr) {
        g_proxyCall.install("eCScriptProxyScript::operator()", s.script_proxy_call, &detourProxyCall);
    }
    if (s.admin_run_function != nullptr) {
        g_runFunction.install("gCScriptAdmin::RunScriptFunction", s.admin_run_function, &detourRunFunction);
    }
    if (s.unit_set_state != nullptr) {
        g_setState.install("gCScriptProcessingUnit::SetCurrentAIState", s.unit_set_state, &detourSetState);
    }
    spdlog::info("trace channel 'calls': {} script name filter(s)", g_filter.size());
}

void installHudTrace() {
    if (!trace::enabled(trace::Channel::Hud)) {
        return;
    }
    const Symbols& s = sym();
    if (s.gui_print_log != nullptr) {
        g_printLog.install("GUI2::PrintGameLog", s.gui_print_log, &detourPrintLog);
    }
    if (s.gui_print_item_log != nullptr) {
        g_printItem.install("GUI2::PrintItemLog", s.gui_print_item_log, &detourPrintItem);
    }
}

}  // namespace spass::game
