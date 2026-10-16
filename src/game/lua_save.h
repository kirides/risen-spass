#pragma once

#include <string>
#include <vector>

#include <sol/sol.hpp>

namespace spass::game {

// Registers `risen.save` (a per-mod persistent key/value store) for every mod. The engine saves quests,
// game variables and entities itself, but the Lua state is not part of a savegame, so mods keep their own
// state here: it is written to <spass>/saves/<savegame name>.lua when the game saves and read back when
// that savegame is loaded (docs/LUA_API.md, "Persistence").
void bindSave();

namespace savestore {

// Serializes the store of every mod. A mod whose data cannot be serialized (a cycle, a function, ...) is
// left out and logged, so one broken mod never loses the data of the others. Takes the Lua lock itself.
std::string serialize();

// Writes `text` as the sidecar of savegame `name` (atomic replace). removeSidecar() deletes a stale one.
bool writeSidecar(const std::string& name, const std::string& text);
void removeSidecar(const std::string& name);

// Replaces the live store with the sidecar of savegame `name` (an empty store if there is none) and keeps
// the previous store so a failed load can undo it with restoreBackup().
void beginLoad(const std::string& name);
void restoreBackup();
void commitLoad();

// Empties the store (a new game starts without mod data).
void clear();

// The live store, for the mod manager (risen.mods): the table of one mod (nil when it has none), the version of the mod that last wrote its data (the savegame's
// `@mods` record, filled in when the game is saved), and dropping a mod's data. Take the Lua lock first. Keys that start with '@' are the layer's own.
sol::object modRecord(sol::state_view lua, const std::string& id);
std::string recordedVersion(sol::state_view lua, const std::string& id);
bool dropMod(sol::state_view lua, const std::string& id);
// Ids of every mod that has data (or a version record) in the live store, sorted.
std::vector<std::string> recordedMods(sol::state_view lua);

}  // namespace savestore
}  // namespace spass::game
