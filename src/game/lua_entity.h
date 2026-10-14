#pragma once

#include <sol/sol.hpp>

namespace spass::game {

// Lua-side handle for an engine entity (eCEntity*). It is a raw pointer: like the engine's own
// `Entity` class it does not keep the entity alive. Every call is wrapped in SEH, so using a handle
// to a destroyed entity yields `nil` instead of a crash in the common case, but handles should not
// be stored across frames/loads.
struct LuaEntity {
    void* e = nullptr;
};

// Registers risen.entity.* and the Entity usertype.
void bindEntity(sol::state_view lua, sol::table risen);

// Registers risen.template, risen.entity.spawn and risen.item.*. Must run after bindEntity().
void bindItems(sol::state_view lua, sol::table risen);

// Wraps a raw pointer into a Lua value: Entity userdata, or nil for null.
sol::object makeEntityObject(sol::state_view lua, void* entity);

}  // namespace spass::game
