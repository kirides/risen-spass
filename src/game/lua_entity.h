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

// The property set `set` (class name, e.g. "gCItem_PS") of the entity template `templateName`, loading the template content first;
// nullptr and a reason in `error` when the template or the set does not exist. (lua_template.cpp)
void* templatePropertySet(const std::string& templateName, const std::string& set, std::string& error);

// The eCTemplateEntity of the template `templateName` (content loaded), usable as the `this` holder of the PS* wrappers (Script.dll);
// nullptr and a reason in `error` otherwise. (lua_template.cpp)
void* templateEntityPtr(const std::string& templateName, std::string& error);

// Registers risen.item.skill_array/set_skill_array/can_use (native access to the RequiredSkills / ModifySkills arrays of items)
// and Entity:skill_total. Must run after bindItems(). (lua_balance.cpp)
void bindBalance(sol::state_view lua, sol::table risen);

// Defines the Lua part of the balancing API (risen.item.requirements/modifiers/..., risen.balance.apply) on top of the native
// skill-array functions and risen.template.patch. Split out so the unit tests can run it against mocks.
void installBalanceHelpers(sol::state_view lua, sol::table risen);

// Defines the per-mod part (risen.balance.install, which registers event handlers for the calling mod) in `api`.
void installBalanceModApi(sol::state_view lua, sol::table api);

// Adds Entity:skill_total() and the item balancing methods to the Entity usertype. (lua_balance.cpp)
void addBalanceMethods(sol::usertype<LuaEntity>& ut);

// Adds Entity:template() (the template an entity was created from) to the Entity usertype.
void addTemplateMethods(sol::usertype<LuaEntity>& ut);

// Registers the Quest usertype and risen.quest.new/get/define/add_log. Needs risen.quest (bindQuest), risen.dialog.
void bindQuestObjects(sol::state_view lua, sol::table risen);

// Adds the item methods (item_value, item_amount, item_icon, ...) to the Entity usertype.
void addItemMethods(sol::usertype<LuaEntity>& ut);

// Adds the NPC methods (guild, hp, level, set_state, goto_*, equip_all, ...) to the Entity usertype.
void addNpcMethods(sol::usertype<LuaEntity>& ut);

// Registers the native NPC balancing functions risen.npc.skill_raw/set_skill_base/combat_species. Must run after bindNpcHelpers(). (lua_npc_balance.cpp)
void bindNpcBalance(sol::state_view lua, sol::table risen);

// Defines risen.npc.stats/set_stats and the npcs / npc_groups / item_groups sections of risen.balance (pure Lua on top of the native functions and
// risen.balance.register_section). Split out so the unit tests can run it against mocks. (lua_npc_balance.cpp)
void installNpcBalanceHelpers(sol::state_view lua, sol::table risen);

// Registers risen.npc.spawn (Lua helper built on the primitives). Must run after bindItems().
void bindNpcHelpers(sol::state_view lua, sol::table risen);

// Wraps a raw pointer into a Lua value: Entity userdata, or nil for null.
sol::object makeEntityObject(sol::state_view lua, void* entity);

}  // namespace spass::game
