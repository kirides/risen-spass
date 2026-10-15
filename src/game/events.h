#pragma once

namespace spass::game {

// Installs the engine lifecycle hooks that feed the Lua events (session.start, save.before, load.before,
// quest.status_changed, ...). A symbol that cannot be resolved only disables its own event.
// Needs the Game.dll/Engine.dll symbols: call it from installHooks() at DllMain time.
void installEventHooks();

// The hook that lets NPC templates be cloned while a world is loaded (docs/RE.md section 25). (lua_template.cpp)
void installTemplateHooks();

}  // namespace spass::game
