# Next steps

State: hook layer, script bridge, localization overlay, entities, items, NPCs, dialogues, quests, **savegame persistence,
lifecycle events, per-mod string files, virtual-file-system access, an automated test loop and a complete savegame-safe example
quest** all work in a live game (Risen Enhanced x64, English text). Findings are in [`RE.md`](RE.md), the API in
[`LUA_API.md`](LUA_API.md), how to work on it in [`DEVELOPMENT.md`](DEVELOPMENT.md).

Legend: **done** (verified in the game or by tests), **partly** (what works / what does not), **open**.
"Verify" means: do it in the isolated test copy (`scripts/test-run.ps1`) and confirm through engine state/log lines as the demos do.

## 0. Plan: balancing and content mods (current work)

Goal: a modder can write **balancing mods** (item conditions and values, NPC values, spells), **add content** (new items from existing or new
meshes/textures, new spells from existing ones, persistent NPCs with quests, NPCs that appear after quests) and script **dialogue conditions,
routines and AI** from Lua. Each milestone below is one commit (feature built *and* verified live, `types/risen.d.lua` + docs updated in the same commit).
Order matters: every later milestone builds on the template access of M1.

Key finding that drives the plan (RE.md §22): the engine keeps one `eCTemplateEntity` per template name. It is an `eCEntity`, so
`GetPropertySet(className)` + the generic property mechanism (RE.md §11/§19) can read and write its property sets, exactly like for a
live entity -- no file shadowing needed (that path is a dead end, §17). Reachable from a name through `Template(name)` +
`eCTemplateEntityProxy::GetEntity`. `eCTemplateEntity::CreateEntityCopy / SetName / SetID` and `PatchInstances` are exported, which makes
**editing existing templates, cloning new ones and updating already spawned instances** the central technique.

| # | Milestone | Status |
|---|---|---|
| M1 | **Template access and editing**: `risen.template.get(name)` handle, property sets/properties by name (get/set/list), `patch_instances()`; effect on new spawns, inventories and world entities; declarative `risen.template.patch`. | open |
| M2 | **Item balancing**: gold value, required skills ("conditions"), skill modifiers, damage/armor values, use type, weight-like fields; helpers for the array properties (`RequiredSkills`, `ModifySkills`); `risen.balance.apply{...}` tables. | open |
| M3 | **NPC/monster balancing**: skills, hit points, level, guild/species, combat values on templates (`gCNPC_PS`, `gCSkills_PS`, ...) and propagation to instances already in the world. | open |
| M4 | **New templates**: `risen.template.create(name, base, overrides)` (clone + rename + deterministic GUID + registration) for items and NPCs; savegame-safe because the ID derives from the name; new items reusing meshes/textures/icons. | open |
| M5 | **New meshes, textures, icons**: whether loose files from a mounted mod directory load for `eCMesh_PS.MeshFileName`, materials, icon images (the open question of 4.2); a minimal "new asset" path. | open |
| M6 | **Spells**: find the spell templates and their property sets (`gCItem_PS.Spell`, `gCInteraction_PS.Spell`, `gCCastInfo_PS`); modify cost/damage/range; new spells by cloning (M4) plus scroll/rune items that use them. | open |
| M7 | **Persistent NPCs**: `risen.npc.persistent{...}`: spawned from a stored spec on `session.start`/`load.after`, position/health/dead state kept in `risen.save`, dialogues and quests defined in `load.before`; attempt an engine-saved layer first (1.1 open point). | open |
| M8 | **Conditional spawning**: spawn/despawn rules triggered by quest status, game variables or item possession (`risen.spawn.when{...}`), remembered across savegames; example NPC that appears after a quest. | open |
| M9 | **Dialogue conditions beyond quest state**: `gCInfoCondition*` objects (item amount, game variable, skill, guild, ...) and a Lua-script condition in `risen.dialog.create`. | open |
| M10 | **NPC routines**: `gCScriptRoutine_PS` routines, working/sleeping/relaxing points, guard points, named waypoints/freepoints; `risen.npc.set_routine`. | open |
| M11 | **AI script bridging**: Lua AI states/functions/callbacks (`RegisterScriptAI*`), so custom behaviour can be attached to NPCs. | open |
| M12 | **Showcase + docs**: `mods/balance_example` (items, NPCs, spells, new item, persistent NPC with quest, conditional spawn) with smoke-test coverage; `docs/MODDING_GUIDE.md`; roadmap tick-off. | open |

Process rules for this plan: one fresh sub-agent per milestone, run strictly one after another; each one reads this section, `docs/RE.md`, `CLAUDE.md`, verifies in the
isolated windowed test copy, records findings in `docs/RE.md`, updates `types/risen.d.lua`/`docs/LUA_API.md`, runs `check_luadefs.py`, the unit tests and (when events,
saving, quests, dialogues or items are touched) the smoke test, adds unit tests for pure logic, and makes exactly one commit. If something turns out impossible the milestone
documents what was tried and the closest working alternative instead of stopping.

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

## 2. Dialogue features — partly done
Done: **every dialogue command by name with its parameters** (`{ command = "Give", ... }` lines in `risen.dialog.create`, validated against the engine's own parameter lists;
reflection via `list_properties`; [`PROPERTIES.md`](PROPERTIES.md)); `risen.gamevar.exists/remove`; `Info:set_command_text`.
Done too: **branching conversations** (`parent = "<info>"`, sub-menu, `Back`; RE.md §21, `mods/dialogbranch`).
Open (needs RE + live tests each): conditions beyond quest state
(`gCInfoCondition*` objects, a Lua-script condition), voice/sound (`SaySound` needs the speech archives), comment/news/hello infos (setting `Type`/condition works,
unverified), verifying `SetTradeEnabled`/`SetTeachEnabled` in a real trade.
Learned: a quest-bound option makes the engine append the speakers' labels to the quest log (RE.md §18); `risen.item.take/give/count` and
`Entity:item_*` exist for effects that need items.

## 3. NPCs — partly done
Done: the real template names are listed in [`TEMPLATES.md`](TEMPLATES.md) (`risen.template.list`; 2679 templates, groups `NPC/<area>`, `Monster`, `Items/...`);
**any property of any property set by name** (`Entity:get_property/set_property`, e.g. `gCNPC_PS` `Guild`, `Species`, `GuardStatus`, `BraveryOverride`);
all guild names and numbers; a **peaceful** spawn option (citizen guild: pirates leave it alone; `AttitudeLock` does not help); `Entity:kill()` verified to remove an entity.
Open: reliable behaviour (routines: `gCScriptRoutine_PS`, guard points), relationships beyond guilds (individual enemies, party membership), appearance
(head/body templates `NPC/_HumanParts`), named spawn points (needs an entity/waypoint enumeration), `Decay` as a gentler despawn.

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
