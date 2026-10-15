#pragma once

#include <functional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace spass::game::bus {

// Deferred Lua events for the gameplay hooks (docs/LUA_API.md "Events", docs/RE.md section 27).
//
// The engine hooks run inside inventory loops, scripts and AI code. Calling Lua from there lets a handler mutate the very inventory or entity
// the engine is iterating, so gameplay events are not dispatched from the hook: the hook reads everything it needs (names, counts), queues the
// event and the layer dispatches it at the end of the same frame (gCSession::Process, right before `tick`). All data is captured when the
// event happens; entities are handed to Lua as handles that re-resolve by name.

// An entity captured at hook time (pointer plus unique name).
struct EntityRef {
    void* ptr = nullptr;
    std::string name;
};

// Reads the unique name now (SEH guarded). A null pointer gives an empty ref (nil in Lua).
EntityRef capture(void* entity);

// A list of { item template, count } pairs (the ingredients of a `crafting` event): a Lua list of { item = ..., count = ... }.
using ItemList = std::vector<std::pair<std::string, double>>;

using Arg = std::variant<std::monostate, std::string, double, bool, EntityRef, ItemList>;

// Queues `event(args...)`. Dropped while no game session runs or while a savegame is being loaded (the world is being rebuilt then).
// Traced on the `events` channel at once, dispatched by flush().
void post(const std::string& event, std::vector<Arg> args);

// True when posting `event` is worth the work: a mod listens to it or the `events` trace channel is on.
bool wanted(const std::string& event);

// Dispatches the queued events (handlers may queue more; at most a few rounds per frame, the rest follows next frame).
void flush();

// Drops everything queued (session stop, load).
void clear();

// Gameplay events are only produced while a session runs (between gCSession::Start and Stop/load begin). `mode` is the session mode
// ("new", "load", "other") reported by world.ready.
//
// A world settles first: right after a session started the engine fills the inventories of all NPCs and creates their equipment; those
// initial contents are not gameplay. Until the player entity exists (then `world.ready(mode)` is emitted) and for 30 frames after it, inventory.*
// and item.* events of other owners than the player and entity.destroyed are dropped.
void setActive(bool active, const std::string& mode = {});
// Turns events on again after a failed savegame load (the session goes on; world.ready is not repeated).
void resume();
bool active();
bool settling();

}  // namespace spass::game::bus
