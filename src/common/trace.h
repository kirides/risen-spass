#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/fmt/fmt.h>

namespace spass::trace {

// Opt-in structured trace channels. Disabled channels cost one relaxed atomic load: SPASS_TRACE checks the bit mask before the
// message is formatted. Output lines look like `[HH:MM:SS.mmm] [trace:template] ...` in spass.log (and in trace.log when
// `[debug] trace_file = true`).
//
//   events    every event the layer emits to Lua, with its arguments (tick excluded)
//   template  risen.template.get / patch / propagation into live entities
//   balance   every applied risen.balance job
//   save      save/load phases with their durations
//   spawn     entity spawns
//   script    risen.script define / override / call and every call the engine makes into a Lua script
//   lua       Lua errors with their traceback
//   test      the risen.test machinery
//   voice     custom voice lines: every sample title the engine asks for, which variant answered, sound output started/stopped
//   calls     every script call of the engine (proxy scripts, AI functions, AI state changes) whose name contains one of the
//             comma separated substrings of `[debug] trace_scripts` (nothing is logged without that list); used to compare engine paths
//   ai        the AI bridge (lua_ai.cpp): Lua AI states begin / finish, starts, perception handlers, callbacks
//   hud       every line the engine prints to the HUD game log / item log (text, type, item, amount)
//   combat    the damage hooks (lua_combat.cpp): every hit a handler saw, with the damage before and after, cancelled hits, handler errors
// A mod may use any other name (risen.trace("mymod", ...)).
enum class Channel : unsigned { Events, Template, Balance, Save, Spawn, Script, Lua, Test, Voice, Calls, Hud, Ai, Combat, Count };

constexpr const char* kChannelNames[] = {"events", "template", "balance", "save", "spawn", "script", "lua", "test", "voice", "calls", "hud", "ai", "combat"};
static_assert(sizeof(kChannelNames) / sizeof(kChannelNames[0]) == static_cast<std::size_t>(Channel::Count));

// Result of parsing "template,save, mymod" (separators: comma, semicolon, whitespace). "all" and "*" select everything.
struct ChannelSet {
    std::uint64_t mask = 0;               // built-in channels
    std::vector<std::string> custom;      // any other name
    bool all = false;                     // every built-in and every custom channel
};

ChannelSet parseChannels(std::string_view list);

// Comma separated names of the built-in channels in `mask`.
std::string channelNames(std::uint64_t mask);

namespace detail {
extern std::atomic<std::uint64_t> g_mask;
}  // namespace detail

inline bool enabled(Channel c) {
    return (detail::g_mask.load(std::memory_order_relaxed) >> static_cast<unsigned>(c)) & 1u;
}

// Starts tracing: `list` as in config.toml `[debug] trace`, `traceFile` (may be empty) receives the lines as well.
void init(std::string_view list, const std::filesystem::path& traceFile);
void enable(std::string_view list);
void disable(std::string_view list);
// Currently enabled channels (built-in and custom), comma separated.
std::string enabledList();

bool enabledName(const std::string& channel);
void emit(Channel c, const std::string& message);
void emitNamed(const std::string& channel, const std::string& message);

}  // namespace spass::trace

// SPASS_TRACE(Channel::Save, "saved '{}' in {} ms", name, ms) -- the arguments are only evaluated when the channel is on.
#define SPASS_TRACE(channel, ...)                                              \
    do {                                                                       \
        if (::spass::trace::enabled(channel)) {                                \
            ::spass::trace::emit(channel, ::fmt::format(__VA_ARGS__));          \
        }                                                                      \
    } while (0)
