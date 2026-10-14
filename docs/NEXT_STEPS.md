# Next steps

State: hook layer, script bridge, localization overlay, entities, items, NPCs, dialogues, quests, **savegame persistence,
lifecycle events, per-mod string files, virtual-file-system access, an automated test loop and a complete savegame-safe example
quest** all work in a live game (Risen Enhanced x64, English text). Findings are in [`RE.md`](RE.md), the API in
[`LUA_API.md`](LUA_API.md), how to work on it in [`DEVELOPMENT.md`](DEVELOPMENT.md).

Legend: **done** (verified in the game or by tests), **partly** (what works / what does not), **open**.
"Verify" means: do it in the isolated test copy (`scripts/test-run.ps1`) and confirm through engine state/log lines as the demos do.

## 1. Must-do before mods can rely on it — done

### 1.1 Persistence across save / load — done
Answers (RE.md §14): runtime-spawned entities are **not** saved (respawn them on `session.start` / `load.after`, guarded by `risen.entity.get`);
quests keep status and log entries and infos keep their "given" flag *if they exist before the engine reads the savegame* (define them on
`load.before`, and again on `session.start` because a new game removes runtime quests; both `define`/`create` are idempotent); the Lua state is
not saved, so `risen.save` stores per-mod data in a sidecar file per savegame. `mods/fetchquest` + `scripts/smoke-test.ps1` prove the complete pattern.
Open: an NPC that the *game* saves (a way to create entities in a layer the savegame keeps) would remove the respawn step; try other spawn layers
(`gCWorld::CreateDynamicEntity`) and the `temporary` flag.

### 1.2 Robust events — done, one nicety open
`session.start/stop`, `world.changed`, `save.before/after`, `load.before/after`, `quest.status_changed`, `npc.killed/defeated`, `info.started/finished`,
`item.picked_up` (plus the existing `mods.loaded`, `tick`, `shutdown`). All verified in-game with `mods/eventlog`.
Open: hot reload of `main.lua` (`restoreAll()` for script slots exists), `item.dropped`, `entity.spawned`, `dialog.option_chosen` with the chosen info.

### 1.3 Safety of the helpers — done
`risen.dialog.create` / `risen.quest.define` validate everything first and return `nil, "reason"` (a quest-state condition with a missing quest is refused);
entity handles remember the unique name and re-resolve after loads. Open: validate loc keys/command lists passed to the low-level `Info` methods.

## 2. Dialogue features — open
Not started (needs RE + live tests each): branching conversations (`Parent` infos, `AreChildInfosAvailable`), conditions beyond quest state
(`gCInfoCondition*` objects, a Lua-script condition), exposing the remaining info commands' parameters (`Give`, `GiveXP`, `SetRoutine`, `MoveTo`, ...), voice/sound
(`SaySound`), comment/news/hello infos, trade and teaching setup.
Learned: a quest-bound option makes the engine append the speakers' labels to the quest log (RE.md §18); `risen.item.take/give/count` and
`Entity:item_*` exist for effects that need items.

## 3. NPCs — mostly open
Done: the real template names are listed in [`TEMPLATES.md`](TEMPLATES.md) (`risen.template.list`; 2679 templates, groups `NPC/<area>`, `Monster`, `Items/...`).
Open: reliable behaviour (routines: `gCScriptRoutine_PS`, guard points), relationships beyond guild numbers, appearance (head/body templates `NPC/_HumanParts`),
despawn/cleanup, named spawn points, a "peaceful" spawn option (spawned NPCs next to hostile guilds fight immediately).

## 4. New content
* **4.1 Read the game archives — done** through the engine's own VFS instead of parsing `*.pak`: `risen.vfs.dirs/find/find_ext/walk/read` (RE.md §16).
* **4.2 VFS mounts — partly.** `risen.vfs.mount/unmount` work at runtime and a mounted directory shadows packed files for archive-manager reads; `mod.toml`
  `data_overlay = true` mounts a mod directory before the engine loads anything (experimental). **Does not work for entity templates**: the engine opens a loose
  `.tple` and then terminates with a fatal error, even for a byte-identical file (RE.md §17). Open: why (probably the template database validates or indexes packed
  entries), the meaning of the mount `flags` (4), mount priority rules, and whether other resource types (meshes, textures, sounds, ini) accept loose files.
* **4.3 New item and NPC templates — open.** Learned: `.tple` (`GENOMETP`) files carry their own property schema; the gold value of an item is a plain int at a fixed offset
  (RE.md §17). Next: find how the template database is loaded and whether a runtime clone (`eCTemplateEntity::CreateEntityCopy`) can be registered.
* **4.4 Localization — done for files, open for conventions.** `strings/all.csv` and `strings/<Language>.csv` per mod, `risen.loc.load_file`. Open: documenting the id
  conventions per table (`FO_`, `TUT_`, `HUD2_`, `GO_`, `QUEST_`, info ids) from a trace of a full playthrough; voice-language switching.

## 5. Platform and robustness
* **Version report — done**: `risen.engine.missing/symbols/build`.
* **Tests — done**: unit tests (`spass_tests`: parser, mod discovery/order, Lua sandbox/events/require), `scripts/smoke-test.ps1` (persistence and fetch-quest scenarios, 24 assertions),
  `.github/workflows/build.yml` (build with the Ninja preset, unit tests, API sync, LuaLS check, package) — the workflow mirrors commands verified locally but has not run on GitHub yet.
* **Other editions — open** (Risen Classic is 32-bit with a different DLL set). **Multiple Script DLL conflicts — open** (document load order). **AI scripts — open**
  (`RegisterScriptAI*` signatures). **Sandbox — partly**: unit-tested; open: per-mod permissions and a manifest `api_version`. **Threading — open** (per-frame cost of tick
  handlers and the loc hooks). **Binary hygiene — open** (~177 LuaJIT exports from the static `lua51.lib` are still exported by the proxy).

## 6. Tooling and documentation — mostly done
Done: [`SYMBOLS.md`](SYMBOLS.md) generator, developer guide, example mods (`hello`, `fetchquest`, `stringsdemo`, `_template`), packaging (`scripts/package.ps1`, release zip with installer),
LuaLS definitions (`types/risen.d.lua` + `tools/check_luadefs.py`), `ghidra-decompiler` subagent, test copy isolation (`[debug] app_data`), `CLAUDE.md`.
Open: a shop-NPC example (needs the trade setup from section 2), persisting the Ghidra analysis (a project export or a script that applies the names/types from RE.md),
a visual verification path (screen capture of the D3D9 window is black: hook `Present` or use the engine's screenshot function).

## 7. Known issues
* `LogTopic` and other loc-string properties cannot be set through property strings (use `Quest:set_topic`; same +8 rule for `gCInfoLocString`, `eCLocString` members).
* `gESkillModifier` value `0` is additive; other modifier values are unexplored.
* Keep all engine enum probing by name: scanning condition values blindly froze the game on an engine message box.
* The engine's log function is an empty stub; failures are silent. The generic fatal-error handler cannot even show its dialog text
  ("can't load ge3dialog resource dll") — use `risen.debug.trace_files` and the event log to see what happened before.
* Spawned NPCs next to hostile guilds fight immediately.
* Steam may relaunch the real install (RE.md §13): `run-windowed.ps1` sets `SteamAppId`; document the same safeguard for modders who launch a copied install.
* ~~The game rewrites `%LOCALAPPDATA%\Risen\ConfigUser.xml` for the test copy~~ — solved by `[debug] app_data` (RE.md §15).
