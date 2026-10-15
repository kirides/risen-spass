#pragma once

namespace spass::game {

// Installs the engine lifecycle hooks that feed the Lua events (session.start, save.before, load.before,
// quest.status_changed, ...). A symbol that cannot be resolved only disables its own event.
// Needs the Game.dll/Engine.dll symbols: call it from installHooks() at DllMain time.
void installEventHooks();

// Inventory events (inventory_events.cpp) and entity events (entity_events.cpp); installEventHooks() installs the ones that only need Game.dll/Engine.dll.
void installInventoryEventHooks();
// PSInventory::Synthesize (crafting event): needs Script.dll, called with installScriptEventHooks().
void installInventoryScriptHooks();
void installEntityEventHooks();
// The hooks on Script.dll / Script_Game.dll functions (script Kill/Defeat, Entity::Spawn): called after those DLLs are loaded; enable the hooks afterwards.
void installScriptEventHooks();
// Called by the gCQuestManager::OnNPCKilled / OnNPCDefeated hooks (events.cpp): post npc.killed / entity.died (once per entity) resp.
// npc.defeated / entity.defeated, with the attacker of the Kill/Defeat script that is running.
// The attacker of the game script `Kill`/`Defeat` that is running right now (nullptr when there is none).
void* currentAttacker();
void reportNpcKilled(void* victim);
void reportNpcDefeated(void* victim);
// Forgets what entity_events.cpp remembers about the world (died entities, cheat kills): session start/stop, load.
void resetEntityEvents();

// The hook that lets NPC templates be cloned while a world is loaded (docs/RE.md section 25). (lua_template.cpp)
void installTemplateHooks();

}  // namespace spass::game
