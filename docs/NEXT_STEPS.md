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
`eCTemplateEntityProxy::GetEntity`. `eCTemplateEntity::CreateEntityCopy / SetName / SetID` are exported (cloning, M4, not yet tried). Verified in M1: **templates can be edited at runtime**; spawns and inventory stacks follow at once;
**existing entities keep their own property copies and savegames/new-game worlds win over the template**, so edits are propagated into live entities property by property
(`risen.template.patch(..., { instances = true })`); the engine's whole-template `PatchInstances` is a no-op after a plain edit (stamp check) and resets per-instance state when forced.

| # | Milestone | Status |
|---|---|---|
| M1 | **Template access and editing**: `risen.template.get(name)` handle, property sets/properties by name (get/set/list), `patch_instances()`; effect on new spawns, inventories and world entities; declarative `risen.template.patch`. | **done** (RE.md §22, `mods/templatepatch`, smoke test `-Scenario templates`). Also added: `Template:instances/instance_count/patch_entity`, `risen.template.census`, `Entity:template()`, `Entity:stack_item(i)`. Notes for later milestones below. |
| M2 | **Item balancing**: gold value, required skills ("conditions"), skill modifiers, damage/armor values, use type, weight-like fields; helpers for the array properties (`RequiredSkills`, `ModifySkills`); `risen.balance.apply{...}` tables. | **done** (RE.md §23, [`ITEM_PROPERTIES.md`](ITEM_PROPERTIES.md), `mods/balance_items` + `mods/balance_driver`, smoke test `-Scenario balance`). `risen.item.requirements/set_requirements/modifiers/set_modifiers/can_use/consume/skill_total`, `risen.balance.apply/install/register_section`, `Entity:requirements/modifiers/skill_total/...`. Notes below. |
| M3 | **NPC/monster balancing**: skills, hit points, level, guild/species, combat values on templates (`gCNPC_PS`, `gCSkills_PS`, ...) and propagation to instances already in the world. | **done** (RE.md §24, [`NPC_PROPERTIES.md`](NPC_PROPERTIES.md), `mods/balance_npcs` + `mods/balance_npcs_driver`, smoke test `-Scenario npcs`). `risen.npc.stats/set_stats`, `risen.balance` sections `npcs`/`npc_groups`/`item_groups`, `risen.balance.scale_npcs`, natives `risen.npc.skill_raw/set_skill_base/template_inventory/template_give/template_take/combat_species`. Notes below. |
| M3b | **Faster verification** (inserted after M2/M3 runs took hours, and a hung smoke test went unnoticed for 20+ min): `risen.test` (expect/finish, result file, run ends the moment a scenario is done), a watchdog in `test-run.ps1`/`smoke-test.ps1` (no log progress / time budget -> kill, print last lines), opt-in structured timestamped trace channels (`template`, `balance`, `save`, `spawn`, `events`, ...), per-scenario timings and a pass/fail table, single-scenario reruns, parallel-safe cleanup of stray `Risen.exe`. | **done** ([DEVELOPMENT.md](DEVELOPMENT.md) "Testing and tracing", [LUA_API.md](LUA_API.md) `risen.test`/`risen.trace`, `mods/testdemo`). `templates`/`balance`/`npcs` migrated to `risen.test` (about 55 s each instead of 240-300 s); `smoke-test.ps1 -List/-Scenario/-FailFast/-TotalSeconds`, `test-run.ps1 -WaitForResult/-StallSeconds/-Trace`. Use it for every later milestone: write the scenario as `risen.test.run`, plan the checks, turn on the relevant trace channels when something fails. |
| M4 | **New templates**: `risen.template.create(name, base, overrides)` (clone + rename + deterministic GUID + registration) for items and NPCs; savegame-safe because the ID derives from the name; new items reusing meshes/textures/icons. | **done** (RE.md §25, [LUA_API.md](LUA_API.md) "New templates", `mods/newtemplates` + `mods/newtemplates_driver`, smoke test `-Scenario newtemplates`, five game processes). `risen.template.create/clone/remove/created/id_for/name_problem`, `Template:id/info/focus_name/description`, `Entity:focus_name/description/dependencies`, `risen.balance` sections `new_items`/`new_npcs`/`templates`. Notes below. |
| M5 | **New meshes, textures, icons**: whether loose files from a mounted mod directory load for `eCMesh_PS.MeshFileName`, materials, icon images (the open question of 4.2); a minimal "new asset" path. | open |
| M6 | **Spells**: find the spell templates and their property sets (`gCItem_PS.Spell`, `gCInteraction_PS.Spell`, `gCCastInfo_PS`); modify cost/damage/range; new spells by cloning (M4) plus scroll/rune items that use them. | open |
| M7 | **Persistent NPCs**: `risen.npc.persistent{...}`: spawned from a stored spec on `session.start`/`load.after`, position/health/dead state kept in `risen.save`, dialogues and quests defined in `load.before`; attempt an engine-saved layer first (1.1 open point). | open |
| M8 | **Conditional spawning**: spawn/despawn rules triggered by quest status, game variables or item possession (`risen.spawn.when{...}`), remembered across savegames; example NPC that appears after a quest. | open |
| M9 | **Dialogue conditions beyond quest state**: `gCInfoCondition*` objects (item amount, game variable, skill, guild, ...) and a Lua-script condition in `risen.dialog.create`. | open |
| M10 | **NPC routines**: `gCScriptRoutine_PS` routines, working/sleeping/relaxing points, guard points, named waypoints/freepoints; `risen.npc.set_routine`. | open |
| M11 | **AI script bridging**: Lua AI states/functions/callbacks (`RegisterScriptAI*`), so custom behaviour can be attached to NPCs. | open |
| M12 | **Showcase + docs**: `mods/balance_example` (items, NPCs, spells, new item, persistent NPC with quest, conditional spawn) with smoke-test coverage; `docs/MODDING_GUIDE.md`; roadmap tick-off. | open |

M1 findings the later milestones build on (details RE.md §22):
* Edit templates on the first `tick` (earliest; `mods.loaded` is too early) and again with `{ instances = true }` on every `session.start`; edits are per process. A savegame/new-game world keeps *its own* values, so
  propagation into live entities is part of every balancing mod; patched entity values are then saved with the game.
* Inventory stacks are references to templates: edits to items show in every inventory at once (no propagation needed there). `Template:patch_instances()` (whole-template copy) resets hit points, group and item amounts: use the targeted
  `patch(... instances = true)`.
* NPC/monster templates are lazy-loaded (handled by the API). **`gCSkills_PS` has no reflected properties** (M3 must find another way for hit points/skills, e.g. `PSSkills` setters on instances plus a raw layout of the template's set);
  array properties (`RequiredSkills`, `ModifySkills`) show only as `Array[n]` through the text format (solved natively in M2, see below).
* `CreateEntityCopy` (M4) was decompiled only; the scene walk (`Template:instances`, `census`) is available for M7/M8 bookkeeping.

M2 findings the later milestones build on (details RE.md §23):
* **Array properties are natively editable**: `bTObjArray<T>` = `{ data*, int count, int capacity }` with inline elements; an element (`gCSkillValue`, `gCModifySkill`) is a reflected `bCObjectBase` (`Skill`, `Amount`, `Modifier`), so read through `bCAccessorProperty` on `data + i * stride` and write by
  building elements with the exported constructors and calling the owner's `Set<Array>` export (`gCItem_PS::SetRequiredSkills/SetModifySkills`). The same recipe works for `gCRecipe_PS.RequiredSkills`/`CraftIngredients`, `gCQuest.ModifySkillsSuccess`, `gCInteraction_PS.Slots` once the owner's setter is found (M4/M6/M7).
* `risen.balance` has a **section registry** (`risen.balance.register_section(name, handler)`; a handler validates its data and returns jobs `{ label, run(opts) }`): M3 registers `npcs`, M6 `spells`; `apply` validates all sections before writing anything, `install` calls `apply` on the first `tick` and on every `session.start`.
* skill ids (`gESkill` 0..33, names in RE.md §23) and `Entity:skill_total(name)` are available for M3; the engine's own skill writers are `gCSkills_PS::ModifySkillValue(gCModifySkill, bool)` and `PSSkills::SetBase(skill, value, modifier)`.
* Only the player is held to equip requirements (NPCs always pass `CanEquipHasRequieredStats`); modifiers of *worn* items drift when edited (see RE.md §23): edit before anything is worn.
* Verified live: weapon requirement edits block/allow the engine's equip, armor edits change the wearer's protection skills, potion edits change the heal, world items and inventory stacks follow; saved worlds keep the saved arrays (`mods/balance_driver`).
* Not covered: measuring weapon damage in an actual fight (formula decompiled: `DamageMultiplier * (DamageAmount + attacker Strength)`), ranged damage chain, recipes' arrays, the HUD tooltip texts of items.

M3 findings the later milestones build on (details RE.md §24):
* **`gCSkills_PS` is plain data**: base values `int[34]` at `+0x20`, modifications `int[34]` at `+0xa8` (gESkill ids of section 23), limits in the exported `ms_arrSkillInfos`. An NPC *template*'s set is the real object (read/write the storage directly); live entities go through `SetSkillBaseValue`. A spawn clones the template's base block: hit points, level, attributes and protection edits reach new spawns at once and survive the init scripts and AI.
* **Derived values, not stored**: kill experience = `10 * level` (at least 50; Chicken/Nautilus exactly `10 * level`) via the `GiveXP` script; melee damage = `DamageMultiplier * (DamageAmount + attacker Strength) - victim protection`, and monsters hit with fist item templates of damage 0 (`It_Fist_Monster`, ...), so their bite is their Strength (verified in a fight). Fist items are named by the combat species (`risen.npc.combat_species`).
* NPC *inventories* of templates are editable through `PSInventory` on the template entity (`risen.npc.template_give/take`, spec key `loot`); existing NPCs are not propagated. `risen.balance` relative settings (`*_mult`, `*_add`) are computed from the values captured when a template is first touched in the process and written as absolutes: idempotent over the first tick / `session.start` re-applications; a template cloned by M4 is captured on first touch too.
* Setting names: `ai_group` is `gCNPC_PS.Group` (the selector key `group` of `npc_groups` means a template group path). `risen.template.list(path)` provides the group universes (`NPC`, `Monster`, `Items/...`).
* Not covered: effect of combat-species edits on running fights, real combat kills for experience, ranged NPC damage, routines/AI state (`gCScriptRoutine_PS`, M10), `gCCharacterMovement_PS` speeds (plain properties, untested).

M4 findings the later milestones build on (details RE.md §25):
* **A template is a pair** (hidden root `"<name>.tple"` + the entity `"<name>"` below it); `CreateEntityCopy` of both + `SetName`/`SetID` + `AttachChild` makes a clone that `Template(name)`, spawn, give, dialogue `Give` commands, loot and balancing all accept. The **ID is `MD5("spass.template:" + name)`** printed as a GUID (`risen.template.id_for`), so the same mod makes the same template in every process;
  a stack in a savegame stores the template's ID (`eCTemplateEntityProxy::operator>>`).
* **Savegames** (fresh processes, verified): create on the first `tick` and everything resolves; a session **without** the templates keeps the stacks dormant (count 0, no crash, a save keeps them) and they resolve once the template exists, even when it is created after the load (pending proxies). Spawned entities are still not saved: only inventories carry clone items.
* **NPC templates can be cloned while a world is loaded** only because the layer skips `gCNavigation_PS::OnPostCopy` during the copy (it raised an access violation for a template's set); items never had the problem. The clone is a snapshot of the base at creation time; `remove` only gives the names back (entities and ID stay until exit); `create` with the same base revives it.
* **Overrides** reuse the `items`/`npcs` sections (`value`, `requires`, `modifies`, `damage`, `hp_max`, `skills`, `loot`, `properties`, ...) plus `name`/`description` (`FO_<name>`, `ITEMDESC_<name>` loc ids; the overlay now also answers the engine's prefixed `focus:FO_<name>` from an unprefixed key), `mesh` (`eCMesh_PS.MeshFileName`; NPC: `eCAnimation_PS.ResourceFilePath`), `icon`, `material`.
  `Entity:dependencies()` (the engine's own `GetDependencies`) proves the mesh/actor that a spawn really loaded. **M5**: `mesh`/`icon` are not checked against the archives; `risen.template.create("X", "It_1H_Sword", { mesh = "NewMesh.xmsh" })` is the entry point for a new asset. **M6**: a spell item is a clone of a rune/scroll with `properties` on `gCCastInfo_PS`/`gCItem_PS.Spell`.
  **M7/M8**: `risen.npc.spawn("MyClone", ...)` works, spawned clone NPCs still have to be re-spawned on `session.start`/`load.after`.
* **Not covered**: human appearance (heads/body parts are `eCBodyPart_PS` item templates equipped into slots, not part of the NPC template; a human clone looks like its base), the inventory screen with a dormant stack, multi templates (refused), new meshes/textures (M5). Found on the way: an open conversation blocks saving and a save after a `Give` command crashed the game (end the conversation with `End`).

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
* **4.3 New item and NPC templates — done (M4, RE.md §25).** `risen.template.create(name, base, overrides)` clones a template at runtime with an ID derived from the name (savegame-safe, verified in fresh processes), items reusing the mesh/icon of other items, NPC clones with other stats/loot/actor;
  `.tple` (`GENOMETP`) files carry their own property schema (RE.md §17) and stay unused. Open: genuinely new meshes/textures (M5).
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
