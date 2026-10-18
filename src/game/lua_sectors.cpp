#include <cctype>
#include <string>
#include <tuple>

#include <spdlog/spdlog.h>

#include "game/abi.h"
#include "game/bindings.h"
#include "game/lua_entity.h"

// risen.world.set_sector / sector_enabled / sectors (docs/RE.md section 55, docs/LUA_API.md "Sectors"): the game's world sectors (X.sec + X.lrent) have an
// "enabled on startup" flag; the story's sectors that start off are switched by `World::SetSectorStatus(name, bool)` (Script.dll: the current world's
// `gCWorld::GetSector(name)`, then `gCSector::Enable`, which stores the flag when the sector is not created yet and toggles the layer contexts otherwise).
// The enabled state is part of the savegame (gCSectorPersistence_PS). Names are the sector's file base name, case-insensitive.

namespace spass::game {

namespace {

// a sector name as the engine takes it: the file base name (letters, digits, _ . -); a trailing ".sec" is dropped
bool cleanSectorName(const std::string& in, std::string& out, std::string& why) {
    out = in;
    if (out.size() > 4) {
        std::string tail = out.substr(out.size() - 4);
        for (char& c : tail) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (tail == ".sec") out.resize(out.size() - 4);
    }
    if (out.empty() || out.size() > 127) {
        why = "the sector name must be 1..127 characters";
        return false;
    }
    for (unsigned char c : out)
        if (!(std::isalnum(c) && c < 0x80) && c != '_' && c != '-' && c != '.') {
            why = "the sector name may only contain letters, digits, _, - and .";
            return false;
        }
    return true;
}

void* currentWorld(const Symbols& s) {
    void* w = nullptr;
    if (s.gcworld_current != nullptr) guarded([&] { w = s.gcworld_current(); });
    return w;
}

std::string baseNameOf(const Symbols& s, const void* sector) {
    std::string name;
    if (s.proc_file_base_name == nullptr) return name;
    guarded([&] {
        OutString out;
        s.proc_file_base_name(sector, out.ptr());
        name = out.take();
    });
    return name;
}

}  // namespace

void bindSectors(sol::state_view lua, sol::table risen) {
    sol::table world = risen["world"];
    const Symbols& s = sym();
    using Two = std::tuple<sol::object, sol::object>;
    const auto fail = [](sol::state_view L, const std::string& reason) -> Two { return {sol::make_object(L, sol::lua_nil), sol::make_object(L, reason)}; };

    // risen.world.sector_enabled(name) -> boolean | nil, reason: whether the current world's sector is enabled (its stored flag)
    world.set_function("sector_enabled", [&s, fail](sol::this_state ts, sol::object nameObj) -> Two {
        sol::state_view L(ts);
        if (nameObj.get_type() != sol::type::string) return fail(L, "the sector name must be a string");
        std::string name, why;
        if (!cleanSectorName(nameObj.as<std::string>(), name, why)) return fail(L, why);
        if (s.gcworld_get_sector == nullptr || s.gcsector_is_enabled == nullptr) return fail(L, "engine function gCWorld::GetSector / gCSector::IsEnabled is unavailable");
        void* w = currentWorld(s);
        if (w == nullptr) return fail(L, "no world is active");
        void* sec = nullptr;
        const BString key(name);
        guarded([&] { sec = s.gcworld_get_sector(w, key.get()); });
        if (sec == nullptr) return fail(L, "no sector called '" + name + "' in the active world");
        bool on = false;
        if (!guarded([&] { on = s.gcsector_is_enabled(sec); })) return fail(L, "the engine could not be asked");
        return {sol::make_object(L, on), sol::make_object(L, sol::lua_nil)};
    });

    // risen.world.set_sector(name, enabled) -> true | nil, reason: World::SetSectorStatus; the layer contexts of an already created sector toggle at once,
    // a sector that is not created yet remembers the flag. true when the sector's flag reads back as asked
    world.set_function("set_sector", [&s, fail](sol::this_state ts, sol::object nameObj, sol::object flag) -> Two {
        sol::state_view L(ts);
        if (nameObj.get_type() != sol::type::string) return fail(L, "the sector name must be a string");
        if (flag.get_type() != sol::type::boolean) return fail(L, "enabled must be true or false");
        std::string name, why;
        if (!cleanSectorName(nameObj.as<std::string>(), name, why)) return fail(L, why);
        const bool on = flag.as<bool>();
        if (s.world_set_sector_status == nullptr || s.gcworld_get_sector == nullptr || s.gcsector_is_enabled == nullptr)
            return fail(L, "engine function World::SetSectorStatus is unavailable");
        void* w = currentWorld(s);
        if (w == nullptr) return fail(L, "no world is active");
        const BString key(name);
        void* sec = nullptr;
        guarded([&] { sec = s.gcworld_get_sector(w, key.get()); });
        if (sec == nullptr) return fail(L, "no sector called '" + name + "' in the active world");  // the engine would do nothing
        if (!guarded([&] { s.world_set_sector_status(nullptr, key.get(), on); })) return fail(L, "the engine call failed");
        bool now = !on;
        guarded([&] { now = s.gcsector_is_enabled(sec); });
        if (now != on) return fail(L, "the sector did not change its state");
        return {sol::make_object(L, true), sol::make_object(L, sol::lua_nil)};
    });

    // risen.world.sectors() -> { { name, enabled, on_startup }, ... } of the active world (empty without one)
    world.set_function("sectors", [&s](sol::this_state ts) -> sol::table {
        sol::state_view L(ts);
        sol::table list = L.create_table();
        if (s.gcworld_sector_count == nullptr || s.gcworld_sector_at == nullptr || s.gcsector_is_enabled == nullptr) return list;
        void* w = currentWorld(s);
        if (w == nullptr) return list;
        int count = 0;
        guarded([&] { count = s.gcworld_sector_count(w); });
        if (count < 0 || count > 100000) return list;
        int n = 0;
        for (int i = 0; i < count; ++i) {
            void* sec = nullptr;
            guarded([&] { sec = s.gcworld_sector_at(w, i); });
            if (sec == nullptr) continue;
            sol::table e = L.create_table();
            e["name"] = baseNameOf(s, sec);
            bool on = false;
            guarded([&] { on = s.gcsector_is_enabled(sec); });
            e["enabled"] = on;
            if (s.gcsector_enabled_on_startup != nullptr) {
                bool startup = false;
                guarded([&] {
                    const bool* p = s.gcsector_enabled_on_startup(sec);
                    startup = p != nullptr && *p;
                });
                e["on_startup"] = startup;
            }
            list[++n] = e;
        }
        return list;
    });
}

}  // namespace spass::game
