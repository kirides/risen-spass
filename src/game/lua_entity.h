#pragma once

#include <string>

#include <sol/sol.hpp>

namespace spass::game {

// Lua-side handle for an engine entity (eCEntity*). Like the engine's own `Entity` class it does not keep
// the entity alive, but it remembers the entity's unique name: whenever the "world generation" changed since
// the pointer was taken (a savegame was loaded, a session started/stopped, the world changed) the handle
// looks the entity up by name again instead of trusting the old pointer, and reports a missing entity as
// stale (see ptr()). Every engine call is additionally wrapped in SEH.
struct LuaEntity {
    mutable void* e = nullptr;
    mutable std::string name;
    mutable unsigned generation = 0;
};

// Pointer of `h` valid for the current world generation, or nullptr if the entity is gone (or was never named).
void* ptr(const LuaEntity& h);

// Advances the world generation (called by the lifecycle hooks); every older handle revalidates on next use.
void bumpEntityGeneration();

// Registers risen.entity.* and the Entity usertype.
void bindEntity(sol::state_view lua, sol::table risen);

// Registers risen.template, risen.entity.spawn and risen.item.*. Must run after bindEntity().
void bindItems(sol::state_view lua, sol::table risen);

// Registers risen.dialog.* and the Info usertype (runtime-created dialogues). Needs risen.loc and risen.script.
void bindDialog(sol::state_view lua, sol::table risen);

// Registers the Template usertype, risen.template.get/patch (adds to risen.template; must run after bindItems()).
void bindTemplates(sol::state_view lua, sol::table risen);

// Defines risen.template.patch (a Lua chunk on top of Template:property_sets/list_properties/set_property/instances). Split out so the
// unit tests can run it against a mock risen.template.get without the game.
void installTemplatePatchHelper(sol::state_view lua, sol::table risen);

// Adds Entity:template() (the template an entity was created from) to the Entity usertype.
void addTemplateMethods(sol::usertype<LuaEntity>& ut);

// Registers the Quest usertype and risen.quest.new/get/define/add_log. Needs risen.quest (bindQuest), risen.dialog.
void bindQuestObjects(sol::state_view lua, sol::table risen);

// Adds the item methods (item_value, item_amount, item_icon, ...) to the Entity usertype.
void addItemMethods(sol::usertype<LuaEntity>& ut);

// Adds the NPC methods (guild, hp, level, set_state, goto_*, equip_all, ...) to the Entity usertype.
void addNpcMethods(sol::usertype<LuaEntity>& ut);

// Registers risen.npc.spawn (Lua helper built on the primitives). Must run after bindItems().
void bindNpcHelpers(sol::state_view lua, sol::table risen);

// Wraps a raw pointer into a Lua value: Entity userdata, or nil for null.
sol::object makeEntityObject(sol::state_view lua, void* entity);

}  // namespace spass::game
