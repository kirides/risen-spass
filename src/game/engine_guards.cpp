// Guards for engine bugs found by the crash hunt (M17, docs/RE.md section 43).
//
// THE BUG. Risen Enhanced starts a Discord rich-presence thread (Game.dll FUN_180970e60, created by gCSession::PostInitialize when the Discord
// SDK initialises) that, every few seconds of game time, calls gCSession::GetHud2Location(): player entity -> frustum element item ->
// eCPortalAdmin::FindPortalRoom -> area admin, i.e. it reads the live world from a background thread without any lock. The first call of a
// fresh process comes the moment the session is up (the timer starts at zero), that is, while the game thread is still setting the freshly
// loaded world up. When the two collide the thread reads a portal-room array that is being (re)built or torn down (array pointer 0 while
// the count is not: the crash at Engine.dll+0x672644, `mov rcx,[array+i*8]`), or an entity that is being destroyed. It is rare (one short
// window per process, right after a load from the main menu), more likely when the machine is busy, and independent of the mods.
//
// THE FIX. The location is only ever computed on the game thread: the per-frame hook calls the engine's function once a second and keeps the
// text; a call from any other thread is answered with that text without touching the world. The Discord status still follows the player (up to
// a second late); until the first text exists the answer is "NO_AREA", which the thread treats as "try again in 10 ms".
// FindPortalRoom itself additionally gets a fault guard (a fault is answered with "no room", the function's normal answer for a position outside
// all rooms) and reports the first time it is called from a thread other than the game thread, so that any other offender shows up in spass.log.

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include <spdlog/spdlog.h>

#include "common/crash.h"
#include "game/abi.h"
#include "game/events.h"
#include "game/symbols.h"
#include "hook/hook_manager.h"

namespace spass::game {
namespace {

Hook<void* (*)(void*, const void*)> g_findRoom;
Hook<void* (*)(void*, void*)> g_hud2Location;

std::atomic<std::uint64_t> g_guardedFinds{0};
std::atomic<std::uint64_t> g_findCalls{0};
std::atomic<std::uint64_t> g_answeredFromCache{0};
std::mutex g_cacheMutex;
std::string g_hud2Cache;  // the last location the game thread computed ("" = none yet)

struct FaultInfo {
    unsigned code = 0;
    std::uintptr_t address = 0;
    std::uintptr_t target = 0;
    unsigned long long accessKind = 0;
};

int captureFault(unsigned code, EXCEPTION_POINTERS* info, FaultInfo* out) {
    out->code = code;
    if (info != nullptr && info->ExceptionRecord != nullptr) {
        out->address = reinterpret_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionAddress);
        if (info->ExceptionRecord->NumberParameters >= 2) {
            out->accessKind = info->ExceptionRecord->ExceptionInformation[0];
            out->target = static_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionInformation[1]);
        }
        crash::reportCaught(info, "guarded FindPortalRoom");
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

// No C++ objects here: the frame holds the __try.
void* callFindGuarded(void* self, const void* position, FaultInfo* fault, bool* faulted) {
    __try {
        return g_findRoom.original(self, position);
    } __except (captureFault(GetExceptionCode(), GetExceptionInformation(), fault)) {
        *faulted = true;
        return nullptr;
    }
}

bool readQword(const void* from, std::uint64_t* out) {
    __try {
        std::memcpy(out, from, sizeof(*out));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool onGameThread() {
    const unsigned long game = crash::gameThreadId();
    return game == 0 || GetCurrentThreadId() == game;
}

std::atomic<bool> g_reportedForeignFind{false};
std::string g_mode = "all";
bool g_raceProbe = false;
std::atomic<void*> g_session{nullptr};  // the gCSession, remembered by the frame hook (for the race probe)
std::atomic<bool> g_probeStarted{false};
std::atomic<std::uint64_t> g_probeCalls{0};

// Test tool (config [debug] race_probe = true): asks gCSession::GetHud2Location from a thread of its own in a tight loop, like the rich-presence thread does
// once per interval, so that the window in which that call collides with a world being built or torn down is hit far more often. With
// engine_guards = "none" the call reaches the engine and the crash shows (banner + dump); with "all" it is answered from the cache.
void raceProbeLoop() {
    spdlog::warn("race probe: a thread now calls gCSession::GetHud2Location in a tight loop (engine_guards = {})", g_mode);
    for (;;) {
        void* session = g_session.load();
        if (session != nullptr && sym().session_hud2_location != nullptr) {
            alignas(8) unsigned char slot[8] = {};
            sym().session_hud2_location(session, slot);
            sym().bstring_dtor(slot);
            if (++g_probeCalls % 50000000 == 0) spdlog::info("race probe: {} calls so far", g_probeCalls.load());
        }
        Sleep(0);
    }
}

void* detourFindRoom(void* self, const void* position) {
    ++g_findCalls;
    if (!onGameThread() && !g_reportedForeignFind.exchange(true)) {
        void* frames[14] = {};
        const USHORT n = RtlCaptureStackBackTrace(0, 14, frames, nullptr);
        std::string callers;
        for (USHORT i = 0; i < n; ++i) callers += (i == 0 ? " callers: " : " < ") + crash::addressText(frames[i]);
        spdlog::warn("portal: FindPortalRoom is called on a thread that is not the game thread (t{}){}", GetCurrentThreadId(), callers);
    }
    crash::GuardScope scope("FindPortalRoom", 0);  // the vectored handler leaves a fault in here to the __except below
    FaultInfo fault;
    bool faulted = false;
    void* room = callFindGuarded(self, position, &fault, &faulted);
    if (faulted) {
        const std::uint64_t n = ++g_guardedFinds;
        std::uint64_t array = 0, count = 0;
        const bool readable = self != nullptr && readQword(static_cast<const char*>(self) + 0x20, &array) && readQword(static_cast<const char*>(self) + 0x28, &count);
        spdlog::warn("portal: FindPortalRoom #{} faulted (code 0x{:08X}, {} of 0x{:X} at 0x{:X}) and was answered with 'no room'; admin {} array 0x{:X} count {} on t{} ({}) after {} calls",
                     n, fault.code, fault.accessKind == 0 ? "read" : fault.accessKind == 1 ? "write" : "access", fault.target, fault.address, self, readable ? array : 0,
                     readable ? static_cast<std::uint32_t>(count) : 0, GetCurrentThreadId(), onGameThread() ? "the game thread" : "NOT the game thread", g_findCalls.load());
    }
    return room;
}

// gCSession::GetHud2Location(this) -> bCString (hidden return pointer in RDX): "CONTEXT_AREA_OUTSIDE", "NO_AREA", ...
void* detourHud2Location(void* self, void* ret) {
    if (onGameThread()) {
        void* r = g_hud2Location.original(self, ret);
        if (r != nullptr) {
            const char* text = sym().bstring_get_text(ret);
            if (text != nullptr && *text != '\0') {
                std::lock_guard lock(g_cacheMutex);
                g_hud2Cache = text;
            }
        }
        return r;
    }
    std::string text;
    {
        std::lock_guard lock(g_cacheMutex);
        text = g_hud2Cache;
    }
    if (g_answeredFromCache.fetch_add(1) == 0) {
        spdlog::info("engine guard: the Discord rich-presence thread (t{}) asked for the player's location; answered from the game thread's last value '{}' instead of reading the world",
                     GetCurrentThreadId(), text.empty() ? "NO_AREA" : text);
    }
    sym().bstring_ctor_cstr(ret, text.empty() ? "NO_AREA" : text.c_str());
    return ret;
}

}  // namespace

void configureEngineGuards(const std::string& mode, bool raceProbe) {
    g_mode = mode == "none" || mode == "findroom" ? mode : "all";
    g_raceProbe = raceProbe;
}

void installEngineGuards() {
    const Symbols& s = sym();
    if (g_mode == "none") {
        spdlog::warn("engine guards are switched off ([debug] engine_guards = \"none\")");
        return;
    }
    if (s.portal_find_room != nullptr && !g_findRoom.install("eCPortalAdmin::FindPortalRoom", s.portal_find_room, &detourFindRoom)) {
        spdlog::warn("engine guard 'eCPortalAdmin::FindPortalRoom' could not be installed");
    }
    if (g_mode == "all" && s.session_hud2_location != nullptr && !g_hud2Location.install("gCSession::GetHud2Location", s.session_hud2_location, &detourHud2Location)) {
        spdlog::warn("engine guard 'gCSession::GetHud2Location' could not be installed");
    }
}

// Once per frame on the game thread (gCSession::Process): refreshes the location text the rich-presence thread is answered with, about once a second.
void engineGuardsFrame(void* session) {
    static unsigned frame = 0;
    if (g_raceProbe && session != nullptr) {
        g_session = session;
        if (!g_probeStarted.exchange(true)) {
            std::thread(raceProbeLoop).detach();
        }
    }
    if (!g_hud2Location.installed() || session == nullptr || (++frame % 60) != 0) {
        return;
    }
    guarded([&] {
        OutString out;
        g_hud2Location.original(session, out.ptr());  // the engine's own function, on the game thread; the text goes to the cache
        const std::string text = out.take();
        if (!text.empty()) {
            std::lock_guard lock(g_cacheMutex);
            g_hud2Cache = text;
        }
    });
}

}  // namespace spass::game
