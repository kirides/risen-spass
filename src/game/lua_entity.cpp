#include "game/lua_entity.h"

#include <atomic>
#include <mutex>
#include <tuple>
#include <unordered_map>

#include <spdlog/spdlog.h>

#include "game/abi.h"

namespace spass::game {
namespace {

// Calls `f` under SEH. Returns nullopt when the engine raised a hardware exception.
template <class R, class F>
sol::optional<R> tryCall(const char* what, F&& f) {
    R result{};
    if (!guarded([&] { result = f(); })) {
        spdlog::warn("entity.{}: engine raised an exception (stale entity handle or game not ready)", what);
        return sol::nullopt;
    }
    return result;
}

template <class F>
bool tryRun(const char* what, F&& f) {
    if (!guarded([&] { f(); })) {
        spdlog::warn("entity.{}: engine raised an exception (stale entity handle or game not ready)", what);
        return false;
    }
    return true;
}

}  // namespace

namespace {
std::atomic<unsigned> g_generation{1};

// Removals seen per entity address in this world generation: a handle remembers the count from when it was made, so a handle to a removed entity (whose memory
// the engine may have freed and reused) is recognised as stale instead of dereferencing freed memory.
std::mutex g_removedMutex;
std::unordered_map<void*, unsigned> g_removed;

unsigned removalsOf(void* entity) {
    std::lock_guard lock(g_removedMutex);
    const auto it = g_removed.find(entity);
    return it == g_removed.end() ? 0u : it->second;
}

// Unique name of an entity pointer straight from the engine; empty if it cannot be read.
std::string nameOf(void* entity) {
    std::string name;
    guarded([&] {
        EntityHolder h{entity};
        OutString out;
        sym().ent_get_name(&h, out.ptr());
        name = out.take();
    });
    return name;
}

void* lookupByName(const std::string& name) {
    if (name.empty()) {
        return nullptr;
    }
    EntityHolder ret;
    if (!guarded([&] {
            ByValueString arg(name);  // by-value bCString: the callee destroys it
            sym().ent_get_entity(&ret, arg.ptr());
        })) {
        return nullptr;
    }
    return ret.entity;
}
}  // namespace

void bumpEntityGeneration() {
    {
        std::lock_guard lock(g_removedMutex);
        g_removed.clear();  // every older handle re-resolves by name (and takes the then-current count)
    }
    ++g_generation;
}

unsigned entityRemovalCount(void* entity) { return removalsOf(entity); }

void noteEntityRemoved(void* entity) {
    if (entity == nullptr) {
        return;
    }
    std::lock_guard lock(g_removedMutex);
    ++g_removed[entity];
}

unsigned currentEntityGeneration() { return g_generation.load(); }

std::string entityNameOf(void* entity) { return nameOf(entity); }

void* ptr(const LuaEntity& h) {
    const unsigned now = g_generation.load();
    if (h.generation == now) {
        if (h.e != nullptr && removalsOf(h.e) != h.epoch) {
            h.e = nullptr;  // removed since the handle was made: the pointer dangles
        }
        return h.e;
    }
    // The world changed since this handle was made: the pointer may dangle. Trust only a lookup by name.
    void* fresh = lookupByName(h.name);
    h.e = fresh;
    h.generation = now;
    h.epoch = fresh != nullptr ? removalsOf(fresh) : 0;
    if (fresh == nullptr) {
        spdlog::debug("entity '{}' is gone after a world change; the handle is stale", h.name);
    }
    return fresh;
}

sol::object makeEntityObject(sol::state_view lua, void* entity) {
    if (entity == nullptr) {
        return sol::make_object(lua, sol::lua_nil);
    }
    return sol::make_object(lua, LuaEntity{entity, nameOf(entity), g_generation.load(), removalsOf(entity)});
}

void bindEntity(sol::state_view lua, sol::table risen) {
    const Symbols& s = sym();

    auto entityType = lua.new_usertype<LuaEntity>(
        "Entity", sol::no_constructor,
        sol::meta_function::equal_to, [](const LuaEntity& a, const LuaEntity& b) { return ptr(a) == ptr(b); },
        sol::meta_function::to_string,
        [](const LuaEntity& a) { return fmt::format("Entity({}@{})", a.name, ptr(a)); },
        "address", sol::property([](const LuaEntity& a) { return reinterpret_cast<std::uintptr_t>(ptr(a)); }),
        "name", [&s](const LuaEntity& self) -> sol::optional<std::string> {
            return tryCall<std::string>("name", [&] {
                EntityHolder h{ptr(self)};
                OutString out;
                s.ent_get_name(&h, out.ptr());
                return out.take();
            });
        },
        "display_name", [&s](const LuaEntity& self) -> sol::optional<std::string> {
            return tryCall<std::string>("display_name", [&] {
                EntityHolder h{ptr(self)};
                OutString out;
                s.ent_get_display_name(&h, out.ptr());
                return out.take();
            });
        },
        // entity:valid() -> true while the entity still exists in the world (looked up by its unique name)
        "valid", [](const LuaEntity& self) {
            self.generation = 0;  // force a re-resolve through the name
            return ptr(self) != nullptr;
        },
        "set_name", [&s](const LuaEntity& self, const std::string& name) {
            const bool ok = tryRun("set_name", [&] {
                EntityHolder h{ptr(self)};
                const BString n(name);
                s.ent_set_name(&h, n.get());
            });
            if (ok) {
                self.name = name;  // the handle re-resolves by this name after a world change
            }
            return ok;
        },
        "position", [&s](const LuaEntity& self) -> std::tuple<sol::optional<float>, sol::optional<float>, sol::optional<float>> {
            float v[3] = {};
            if (!tryRun("position", [&] {
                    EntityHolder h{ptr(self)};
                    s.ent_get_position(&h, v);
                })) {
                return {sol::nullopt, sol::nullopt, sol::nullopt};
            }
            return {v[0], v[1], v[2]};
        },
        "distance_to", [&s](const LuaEntity& self, const LuaEntity& other) -> sol::optional<float> {
            return tryCall<float>("distance_to", [&] {
                EntityHolder a{ptr(self)};
                EntityHolder b{ptr(other)};
                return s.ent_distance_to(&a, &b);
            });
        },
        "is_npc", [&s](const LuaEntity& self) { return tryCall<bool>("is_npc", [&] { EntityHolder h{ptr(self)}; return s.ent_is_npc(&h); }).value_or(false); },
        "is_player", [&s](const LuaEntity& self) { return tryCall<bool>("is_player", [&] { EntityHolder h{ptr(self)}; return s.ent_is_player(&h); }).value_or(false); },
        "is_dead", [&s](const LuaEntity& self) { return tryCall<bool>("is_dead", [&] { EntityHolder h{ptr(self)}; return s.ent_is_dead(&h); }).value_or(false); },
        "is_item", [&s](const LuaEntity& self) { return tryCall<bool>("is_item", [&] { EntityHolder h{ptr(self)}; return s.ent_is_item(&h); }).value_or(false); },
        "is_character", [&s](const LuaEntity& self) { return tryCall<bool>("is_character", [&] { EntityHolder h{ptr(self)}; return s.ent_is_character(&h); }).value_or(false); },
        "kill", [&s](const LuaEntity& self) { return tryRun("kill", [&] { EntityHolder h{ptr(self)}; s.ent_kill(&h); }); },
        // entity:save_relevant() -> bool | nil: whether the engine writes this entity into savegames (eCEntity flag 0x80000, docs/RE.md section 32)
        "save_relevant", [&s](const LuaEntity& self) -> sol::optional<bool> {
            if (s.eent_is_save_relevant == nullptr) {
                return sol::nullopt;
            }
            return tryCall<bool>("save_relevant", [&] { return s.eent_is_save_relevant(ptr(self)); });
        },
        // entity:set_save_relevant(on) -> bool: makes the engine save (true) or skip (false) this entity
        "set_save_relevant", [&s](const LuaEntity& self, bool on) {
            return s.eent_set_save_relevant != nullptr && tryRun("set_save_relevant", [&] { s.eent_set_save_relevant(ptr(self), on); });
        },
        "set_frozen",[&s](const LuaEntity& self, bool on) { return tryRun("set_frozen", [&] { EntityHolder h{ptr(self)}; s.ent_set_frozen(&h, on); }); },
        "enable_rendering", [&s](const LuaEntity& self, bool on) { return tryRun("enable_rendering", [&] { EntityHolder h{ptr(self)}; s.ent_enable_rendering(&h, on); }); },
        // entity:teleport("EntityName")  or  entity:teleport(otherEntity)
        "teleport", [&s](const LuaEntity& self, sol::object target) -> bool {
            if (target.is<LuaEntity>()) {
                const LuaEntity& other = target.as<LuaEntity>();
                return tryCall<bool>("teleport", [&] {
                    EntityHolder h{ptr(self)};
                    EntityHolder o{ptr(other)};
                    return s.ent_teleport_entity(&h, &o);
                }).value_or(false);
            }
            if (target.is<std::string>()) {
                const std::string name = target.as<std::string>();
                return tryCall<bool>("teleport", [&] {
                    EntityHolder h{ptr(self)};
                    const BString n(name);
                    return s.ent_teleport_name(&h, n.get());
                }).value_or(false);
            }
            return false;
        });

    addNpcMethods(entityType);
    addItemMethods(entityType);
    addTemplateMethods(entityType);
    addRoutineMethods(entityType);
    addBalanceMethods(entityType);

    sol::table entity = lua.create_table();
    // risen.entity.get("NPC_Name") -> Entity | nil   (unique entity name as used by the scene admin)
    entity.set_function("get", [&s](sol::this_state ts, const std::string& name) -> sol::object {
        EntityHolder ret;
        const bool ok = guarded([&] {
            ByValueString arg(name);  // by-value bCString: the callee destroys it
            s.ent_get_entity(&ret, arg.ptr());
        });
        if (!ok) {
            spdlog::warn("entity.get('{}'): engine raised an exception (game not ready?)", name);
            return sol::make_object(ts, sol::lua_nil);
        }
        return makeEntityObject(ts, ret.entity);
    });
    entity.set_function("player", [&s](sol::this_state ts) -> sol::object {
        EntityHolder ret;
        if (!guarded([&] { s.ent_get_player(&ret); })) {
            return sol::make_object(ts, sol::lua_nil);
        }
        return makeEntityObject(ts, ret.entity);
    });
    risen["entity"] = entity;
}

}  // namespace spass::game
