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

}  // namespace

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
