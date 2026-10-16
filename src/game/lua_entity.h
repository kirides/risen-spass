#pragma once

#include <functional>
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
    mutable unsigned epoch = 0;  // how often the engine had removed an entity at this address when the handle was made (see noteEntityRemoved)
};

// Pointer of `h` valid for the current world generation, or nullptr if the entity is gone (or was never named).
void* ptr(const LuaEntity& h);

// Advances the world generation (called by the lifecycle hooks); every older handle revalidates on next use.
void bumpEntityGeneration();

// Called by the eCEntity::Kill hook (the engine's one entry for removing an entity) before the entity goes: handles made earlier for that address are stale from now
// on (ptr() returns nullptr), even when the engine reuses the memory for a new entity. Cleared with every world generation.
void noteEntityRemoved(void* entity);
// How often noteEntityRemoved() was called for this address in the current world generation: what a handle made now remembers in `epoch`.
unsigned entityRemovalCount(void* entity);

// The current world generation (a handle made with another one re-resolves by name), and an entity's unique name read straight from the engine
// (empty when it cannot be read).
unsigned currentEntityGeneration();
std::string entityNameOf(void* entity);

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

// One pass over every entity of the scene's dynamic contexts (the world's own entities included): `visit(entity, creator)` gets the eCEntity* and the
// template entity it was made from (nullptr for none). False when the scene is not available (no world). `templateCreatorPtr(name)` is the value
// `creator` has for entities of that template (nullptr when the template is unknown), `templateNameOfCreator` the reverse. (lua_template.cpp)
bool walkSceneEntities(const std::function<void(void* entity, const void* creator)>& visit);
const void* templateCreatorPtr(const std::string& templateName);
std::string templateNameOfCreator(const void* creator);

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

// Registers risen.template.create (the Lua part on top of risen.template.clone and the balancing sections) and the `new_items`, `new_npcs` and
// `templates` sections of risen.balance. Must run after bindNpcBalance(). Split out so the unit tests can run it against mocks. (lua_template.cpp)
void installTemplateCreateHelpers(sol::state_view lua, sol::table risen);
void bindTemplateCreate(sol::state_view lua, sol::table risen);

// Registers risen.spell (natives: equipped, can_cast, consume, shooter; Lua: the catalogue, the `spells` / `new_spells` sections, risen.spell.create/modify).
// Must run after bindTemplateCreate(). installSpellHelpers runs only the Lua part (the unit tests mock the natives); installSpellModApi adds the per-mod
// part (the entity.spawned handler of per-shot overrides). (lua_spell.cpp)
void bindSpell(sol::state_view lua, sol::table risen);
void installSpellHelpers(sol::state_view lua, sol::table risen);
void installSpellModApi(sol::state_view lua, sol::table api);

// Registers risen.recipe (natives: ingredients_raw/set_ingredients_raw/can_craft_raw/craft_raw; Lua: the catalogue, risen.recipe.create/modify/known/learn and the
// `recipes` / `new_recipes` sections). Must run after bindSpell(). installRecipeHelpers runs only the Lua part (the unit tests mock the natives). (lua_recipe.cpp)
void bindRecipe(sol::state_view lua, sol::table risen);
void installRecipeHelpers(sol::state_view lua, sol::table risen);
void installRecipeModApi(sol::state_view lua, sol::table api);

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

// NPC routines (lua_routine.cpp, M10): the natives risen.npc.routine_points/set_routine_points/select_routine/apply_routine and risen.world.scan/time/set_time
// (after bindWorld and bindNpcHelpers), and the Lua part risen.npc.set_routine/get_routine/clear_routine, risen.world.points/add_point/remove_point
// (installRoutineHelpers runs only the Lua chunk: the unit tests mock the natives). addRoutineMethods adds Entity:children/attach.
void bindRoutine(sol::state_view lua, sol::table risen);
void installRoutineHelpers(sol::state_view lua, sol::table risen);
// Per mod, before installPersistentNpcModApi: risen.npc.set_routine/get_routine/clear_routine, risen.world.add_patrol/remove_patrol and the restore of the patrol anchors after a load
// (records in the mod's risen.save, so register it after bindSave()). (lua_routine.cpp)
void installRoutineModApi(sol::state_view lua, sol::table api, const std::string& modId);
void addRoutineMethods(sol::usertype<LuaEntity>& ut);

// Geometry (lua_geometry.cpp, M13): the natives risen.world.trace / engine_free_los / camera (after bindRoutine: they live in risen.world), the Lua part risen.math,
// risen.world.raycast / line_of_sight / can_see / is_seen_by (geometry_lua.h; installGeometryHelpers runs only the Lua chunks: the unit tests mock the natives),
// Entity:bounds, and the per-mod risen.random streams.
void bindGeometry(sol::state_view lua, sol::table risen);
void installGeometryHelpers(sol::state_view lua, sol::table risen);
void installRandomModApi(sol::state_view lua, sol::table api, const std::string& modId);
void addGeometryMethods(sol::usertype<LuaEntity>& ut);

// Wraps a raw pointer into a Lua value: Entity userdata, or nil for null.
sol::object makeEntityObject(sol::state_view lua, void* entity);

// Defines the per-mod risen.npc.persistent / persistent_get / persistent_list on `api` (the mod's view of `risen`; must run after bindSave's per-mod
// risen.save exists, i.e. register it after bindSave()). `modId` names the owner of the declared names. (lua_persistent_npc.cpp)
void installPersistentNpcModApi(sol::state_view lua, sol::table api, const std::string& modId);
// risen.spawn.when / risen.cond (spawn_when_lua.h): per-mod, after installPersistentNpcModApi. (lua_persistent_npc.cpp)
void installSpawnWhenModApi(sol::state_view lua, sol::table api, const std::string& modId);
// risen.dialog.create learns `when` / `engine` (dialog_cond_lua.h): per-mod, after installSpawnWhenModApi. (lua_persistent_npc.cpp)
void installDialogCondModApi(sol::state_view lua, sol::table api, const std::string& modId);

}  // namespace spass::game
