# Risen Mod Kit ("Mod-Baukasten"): requirements and implementation plan

`risen-modkit` is a separate, cross-platform desktop application for building mods for this layer without writing Lua. A modder creates items, recipes, NPCs and
monsters, dialogues and quests, places NPCs, items and chest contents in a 3D view of the game world, and exports the result as an ordinary, self-contained Lua
mod (`<Risen>\bin\spass\mods\<id>\`) that uses the layer's declarative API (`risen.balance.install`, `risen.npc.persistent`, `risen.dialog.create`,
`risen.quest.define`, ...). The tool never writes into the game folder and never executes anything from the game data.

Status of the stages: section 9. This document is the plan; the user-facing manual is written as the stages land (section 8.4).

Contents: [1 Goals](#1-goals-and-non-goals) · [2 Users and workflow](#2-users-and-workflow) · [3 Functional requirements](#3-functional-requirements) ·
[4 Non-functional requirements](#4-non-functional-requirements) · [5 Architecture](#5-architecture) · [6 Data model and files](#6-data-model-and-files) ·
[7 Export: the generated mod](#7-export-the-generated-mod) · [8 Testing](#8-testing-and-verification) · [9 Stages](#9-stages) · [10 Risks and open questions](#10-risks-and-open-questions)

## 1. Goals and non-goals

**Goals**

* G1: Content mods without Lua: items, recipes, NPCs/monsters, dialogues, quests, world placement.
* G2: The output is a normal mod of this layer: readable, stackable with other mods, savegame-safe (it follows the rules of MODDING_GUIDE section 3), switchable in the Mods tab,
  and it needs nothing but the layer at run time (no editor, no extra DLL).
* G3: The editor uses what the game ships (templates, meshes, textures, string tables, world layers), read **offline** from the player's own copy of the game archives.
  Custom assets (compiled `._x*` files, later images and meshes from authoring formats) are possible too.
* G4: Round trip: a project file holds everything; an exported mod can be opened again (import) and updated.
* G5: Cross-platform (Windows, Linux, macOS), C++20, CMake, vcpkg, GLFW + Dear ImGui (docking) + ImGuizmo + a node editor in the style of Unreal Blueprints.

**Non-goals**

* Editing the game's archives or writing `.pak`/`.pNN` volumes (the layer's approach is run-time patching; pack2mod converts the other way).
* A level editor for terrain, navigation meshes, lighting or sectors. The world view places *entities*; the static geometry is shown, not edited.
* Running the game from the editor on non-Windows hosts (the game and the layer are Windows x64; the editor only *produces* mods there).
* Scripting arbitrary behaviour graphically (AI states, damage hooks). The exported mod has a `custom.lua` hook file for hand-written Lua that the export never overwrites.

## 2. Users and workflow

Users: modders who know the game but not Lua; Lua modders who want the tedious parts (numbers, dialogue trees, positions) generated.

1. First start: choose the game's `data` directory (`<Risen>\data`, holding `common\*.pak` and `compiled\*.pak`). The editor indexes the archives (templates, strings, meshes,
   world layers) and caches the index.
2. *File > New project*: mod id, name, author, version, languages.
3. Create content in the panels (Items, Recipes, NPCs, Dialogues, Quests, World). Everything references other content by name, pickers list shipped and project content.
4. *Validate* (always running in the background): errors and warnings in the Problems panel (unknown template, duplicate name, a dialogue with an NPC that does not exist,
   a quest nobody starts, ...). Export is refused while errors exist.
5. *File > Export mod* to a folder (normally `<Risen>\bin\spass\mods\<id>`), start the game, test. Hot reload of the layer (`[mods] watch = true`) picks up a re-export
   for most content (MODDING_GUIDE 4.20).
6. *File > Import mod* opens an exported mod again (G4).

## 3. Functional requirements

IDs are referenced by the stages (section 9). **M** = must for the stage that implements it, **S** = should, **B** = bonus (named in the request).

### 3.1 Application shell and project (stage E0)

| ID | Requirement |
|---|---|
| APP-1 M | Window with a docking layout (ImGui docking branch): menu bar, panels can be rearranged, layout is saved per user (`imgui.ini` in the user config directory). |
| APP-2 M | Settings: game `data` directory, export directory, UI scale, language list of the project defaults. Stored in the per-user config directory (`%APPDATA%\risen-modkit`, `$XDG_CONFIG_HOME/risen-modkit`, `~/Library/Application Support/risen-modkit`). |
| APP-3 M | Project new / open / save / save as (JSON, section 6), recent projects, unsaved-changes prompt, undo/redo for every edit (command stack over the project model). |
| APP-4 M | Log panel (everything the tool does and every parse problem) and Problems panel (validation result, click to jump to the object). |
| APP-5 M | Command line: `risen-modkit [project] [--data DIR]`; for automation `--export DIR` (no window), `--screenshot FILE --frames N --panel NAME` (render N frames, save the window as PNG, exit; the test loop's way to look at the UI). |
| APP-7 S | **Play** (F5, the green button, menu Game): export into `<game folder>\bin\spass\mods\<id>` and start Risen windowed (main menu / new game / last quick save), or, when the game runs with the layer's file watcher on, let it hot-reload the mod. |
| APP-8 S | **Game errors**: the editor tails `spass.log`, keeps the warnings/errors that carry `[<mod id>]` or name a project object (item, recipe, template, NPC, quest and its `MK_<quest>_<action>` scripts, a dialogue option's info name, behaviours) and lists them in the Problems panel; a click jumps to the object. They never block the export. |
| SPELL-1 S | Spells panel: changes of the shipped spells and new spells (`spells`, `new_spells`). |
| NPC-6 S | NPC groups panel: selector (group path, Lua pattern, names) with the list of what it matches, relative changes (`npc_groups`). |
| QST-5 S | Quest branches: an Either / or node whose paths each start with an Objective; the first one completed decides the path. |
| EDT-1 S | Command palette (Ctrl+P): one search over project objects, the game's templates, panels and commands. |
| EDT-2 S | Wizards for common jobs (fetch quest, new weapon with recipe, trader with stock) that generate ordinary, editable content. |
| APP-6 S | Native file dialogs (nativefiledialog-extended); fallback: ImGui path input when the platform has no dialog service. |

### 3.2 Game data (stage E1)

| ID | Requirement |
|---|---|
| DATA-1 M | Read the game archives (G3V0 `.pak` and patch volumes `.pNN`, later wins) of `common` and `compiled` through the existing reader `src/pack2mod/pak.*` (made portable). |
| DATA-2 M | Template catalogue: every `*.tple` of `common/templates.pak`, name (entity name, RE.md section 48), group path (`Items/Weapons/...`), kind (item, NPC, monster, recipe, spell item, mobile/interactive, effect, other), the property sets and values (`src/pack2mod/tple.*`). |
| DATA-3 M | String tables (`compiled/strings/*.tab`): labels `FO_<name>`, descriptions `ITEMDESC_<name>` in every language column, shown next to template names. |
| DATA-4 M | Asset name index: meshes (`._xmsh`), materials, images, actors (`._xmac`), icons (`compiled_imagelists.bin` image names, as far as the format is decoded), with the archive path. Pickers for `mesh`, `icon` use it. |
| DATA-5 M | Browser panel: tree by group, search by name and label, filter by kind, property inspector of the selected template (set, name, type, value). "Use as base", "Edit shipped template" actions. |
| DATA-6 S | Index cache on disk (keyed by archive sizes and times) so a second start is fast. |
| DATA-7 S | Icon preview: decode the icon atlas (`._ximg` = DDS, RE.md section 26) and show the item's cell. |

### 3.3 Items (stage E2)

| ID | Requirement |
|---|---|
| ITEM-1 M | Edit a shipped item: the named settings of `BalanceItem` (value, damage, requires, modifies, use type, category, mana cost) with the shipped value shown next to the field, plus free properties (`properties = { set = { name = value } }`) picked from the template's own property list. |
| ITEM-2 M | New item: name (validated like `risen.template.name_problem`: letters, digits, `_`, max 63, unique), base item (picker), label and description per language, the `BalanceItem` settings, mesh, icon, material switch. |
| ITEM-3 M | Item groups: relative settings for a group path / pattern (`item_groups`: `value_mult`, `damage_mult`). |
| ITEM-4 S | Show what the base provides (inherited values greyed) and the diff that will be exported. |

### 3.4 Recipes (stage E3)

| ID | Requirement |
|---|---|
| REC-1 M | New recipe: station (anvil, forge, grindstone, coolweapon, alchemy, goldsmith, scroll, cooking, frying), 1..16 ingredients with counts, result (shipped or project item), skills, recipe item label/description/icon/value, `learn` (the player gets the plan once per savegame). |
| REC-2 M | Edit a shipped recipe (`recipes` section): station, ingredients or counts, skills, result. The shipped recipe is read from the template (`gCRecipe_PS`). |
| REC-3 S | Recipe item handed out by an NPC (trader stock / dialogue `Give`) instead of `learn`: via the NPC inventory (E4) or a dialogue command (E5). |

### 3.5 NPCs and monsters (stage E4)

| ID | Requirement |
|---|---|
| NPC-1 M | Edit a shipped NPC/monster template: `BalanceNpc` (hit points, mana, level/xp, skills and protections, damage, guild, species, attitude, bravery, combat species, AI group, loot). |
| NPC-2 M | New NPC/monster template: base (picker with label, species, guild), name/label per language, the `BalanceNpc` settings, actor (`mesh` = `.xac` actor of monsters; humans look like their base, RE.md section 25). |
| NPC-3 M | Inventory and equipment: a list of items with amounts; "equipped" marks what the NPC wears/holds (template `loot` for every instance; per placed NPC `items` + `equip`). |
| NPC-4 M | Placed NPC (instance): unique name, template, position (numeric now, picked in the 3D view in E10), label, peaceful/guild, level, hp, routine (post / patrol / day-night with relative points), respawn, version. Exported as `risen.npc.persistent`. |
| NPC-5 S | Conditional NPC: "appears when" / "leaves when" conditions (`risen.spawn.when`), using the condition builder of E5. |
| NPC-6 S | Teacher limits and trader stock of an NPC (dialog commands / `gCDialog_PS` properties), as far as the layer exposes them. |

### 3.6 Dialogues (stage E5)

| ID | Requirement |
|---|---|
| DLG-1 M | Node-based editor on a scrolling, zoomable canvas (imgui-node-editor, the Unreal-Blueprints look): a dialogue graph per NPC. Node types: **Option** (an info: label = first line, type, permanent, sort order), **Line** (speaker, listener, text per language, optional voice file), **Command** (any engine dialogue command by name with its parameters: `Give`, `GiveXP`, `SetGameEvent`, `RunQuest`, `End`, ...), **Script** (call a named Lua script of the project), **Log** (quest log entry), **Condition** (see DLG-3), **Quest action** (start / succeed / fail a project quest). |
| DLG-2 M | Multi-stage conversations: an Option node's "then" pin leads to child options (`parent`, the engine's sub-menu, RE.md section 21); "End" closes the conversation; an option without "End" returns to the option list. |
| DLG-3 M | Conditions as nodes or inline: quest state, item count (player or NPC), game variable / game event, skill value, guild, NPC dead/alive, all/any/not. Mapped to `risen.cond.*` (engine conditions where possible, M9). |
| DLG-4 M | Validation: every option has an owner NPC that exists (shipped placed NPC by unique name or a project NPC), every quest condition names a defined quest, command parameters exist (the layer's parameter lists, PROPERTIES.md "Dialogue commands"), every path ends. |
| DLG-5 S | Preview: walk through the conversation in the editor (choose options, see lines, conditions evaluated against toggles). |
| DLG-6 S | Comments / groups on the canvas, minimap, auto layout of a new graph. |

### 3.7 Quests (stage E6)

| ID | Requirement |
|---|---|
| QST-1 M | Node-based quest editor (same canvas): a **Quest** node (name, title per language, type, xp, silent), **Start** triggers (dialogue option, item picked up, NPC killed, location... as far as the layer has events), **Objective** nodes (fetch N items, kill N of a template or a named NPC, talk to an NPC (an option finished), game variable reached), **Outcome** nodes (succeed / fail / cancel, rewards: xp, items, gold, game variable, log text), links to the next quest (quest chains). |
| QST-2 M | Log entries per stage (text per language). |
| QST-3 M | Dialogue integration: a dialogue Option can start/advance a quest; quest states are available as conditions in the dialogue editor. |
| QST-4 M | Export: `risen.quest.define` in a `define` function run on `load.before` and `session.start`, objectives through the event-driven tracker pattern (`mods/example_quest_tracker`), progress in `risen.save`, rewards given once (idempotent). |
| QST-5 S | Chapter conditions (`risen.quest.chapter`), quest-giver markers in the world view. |

### 3.8 World view and placement (stages E7, E8, E10)

| ID | Requirement |
|---|---|
| WLD-1 M | Load a world (`common/projects/<World>/`: `.wrl`, sectors `.sec`, layers `.lrent`): every entity with its template name, GUID, matrix and property sets. Layer/sector tree with visibility toggles. |
| WLD-2 M | 3D view (OpenGL 3.3 core): free-fly and orbit camera, grid, bounding boxes for every entity, frustum culling, selection by click (ray against bounds), entity inspector (name, template, GUID, position, properties, inventory). |
| WLD-3 M | Meshes: decode `._xmsh` (GR01 / MS02, RE.md section 26: property schema + vertex streams), materials `._xmat` -> diffuse image `._ximg` (DDS inside; DXT1/3/5 and A8R8G8B8), with a resource cache and level-of-detail by distance (box beyond a range). Landscape level meshes, buildings, props. SpeedTree vegetation as placeholders. |
| WLD-4 M | Place NPCs: drag a template or a project NPC into the view; position and yaw with the ImGuizmo gizmo; snaps onto the ground (ray against level meshes). Exported as `risen.npc.persistent{ position = {x,y,z} }`. |
| WLD-5 M | Place items in the world: exported as a spawn once per savegame (the entity ID kept in `risen.save`, never spawned twice). |
| WLD-6 M | Chest contents: select a container (chest, corpse, any entity with a `gCInventory_PS`) of the loaded world, add items. Exported as `risen.entity.by_id(<GUID>)` + `risen.item.give` once per savegame (the pattern pack2mod's `world.lua` uses, RE.md section 47). An item ticked *equip* (an NPC's own inventory) is listed in the entry's `equip` and equipped with `risen.item.equip` after it was given (runtime.lua VERSION 10). |
| WLD-7 B | Place static meshes (buildings, campfires, props): spawn of a mesh/interactive template at a position and rotation, once per savegame. Needs a layer API for the rotation (section 10). |
| WLD-8 B | Remove and move existing world entities (by GUID). Needs layer APIs `Entity:remove()` / `Entity:set_transform()` that the engine saves (the savegame has a list of removed native entities, RE.md section 32). |
| WLD-9 S | Show the project's placed content and the player's start point; "copy position for Lua" helper. |

### 3.9 Assets (stage E11)

| ID | Requirement |
|---|---|
| AST-1 M | Built-in assets: every mesh/icon/actor picker lists the names of the game archives (DATA-4) with a preview where decoded (mesh in a small 3D preview, icon cell). |
| AST-2 M | Custom compiled assets: add `._xmsh`, `._xmat`, `._ximg`, `._xmac`, ... files to the project; they are copied to `<mod>/assets/` on export and offered in the pickers. |
| AST-5 M | Sounds: audio files (Ogg Vorbis, WAV, MP3) and compiled `._xsnd` in the project; the game's speech and effect sounds (`sounds.pak`, `speech_<language>.pak`) are listed by voice language; every sound can be **played in the editor** (a `._xsnd` is unpacked: its payload is the audio file, RE.md section 28). A dialogue line's sound is exported as `sound = "sounds/<file>"` (one file or one per language), game sounds unpacked into `sounds/`; SaySound's `SoundName` takes a game sound by its raw name. |
| AST-3 S | Custom images: PNG/TGA/JPG -> `._ximg` (DDS A8R8G8B8 or DXT inside the GR01 container); icons -> an icon atlas + `.ximl` image list (`risen.assets.load_icons`). |
| AST-4 B | Custom meshes from authoring formats (glTF, OBJ, FBX through assimp) -> `._xmsh` + `._xmat`. Needs a writer of the MS02 container that the engine accepts (verified in the game, section 10). |

### 3.11 Events and triggers (stage E21)

A visual event system for game designers: the **Events** tab holds *event graphs*, node graphs on the same canvas as dialogues and quests. A graph starts at a **trigger node**
(something happens in the game), walks along its links through **condition**, **flow** and **action** nodes, and ends. Example:
`[On chapter 3] -> [Condition: variable intro_done = 0] -> [Set variable intro_done = 1] -> [HUD message "Chapter 3"] -> [End]`.

| ID | Requirement |
|---|---|
| EVT-1 M | Project model `EventGraphDef { uid, name, enabled, graph, notes }` (`Project::events`), persisted in the project file (`events`), edited in the Events tab: list of graphs, canvas, palette / context menu by group (Triggers, Conditions, Flow, Actions), inspector per node (table-driven from one field list per node type), undo / redo through the project history like the other panels. |
| EVT-2 M | Triggers (start nodes, one output): `on_session` (new game / load / any), `on_chapter`, `on_kill` (template or unique name), `on_move` (player moved >= N m, N >= 1), `on_area` (player enters / leaves a sphere around a point or a named entity), `on_quest` (quest status), `on_item` (picked up / dropped / equipped / unequipped / added / removed / consumed), `on_dialogue` (option finished), `on_time` (clock reaches hh:mm), `on_variable` (a variable changed, with comparison), `on_timer` (every / after N seconds of play), `on_signal` (another graph's Send signal). |
| EVT-3 M | Conditions (the *Condition* flow node has a `then` and an `else` output and tests a list of checks combined with all / any, optionally negated): variable compare, quest status, item count, chapter, player level, skill, NPC alive / dead, guild, game event, time of day window, player in area, random chance. Chaining condition nodes gives nested and / or. |
| EVT-4 M | Actions: set / add / clear variable (game variable or mod save variable), spawn / remove NPC, give / take item, give XP, start / succeed / fail / cancel quest, quest log, HUD message (`{var}` placeholders), play sound, teleport the player, set chapter, set time, heal / hurt, run a script by name, send signal, write to the log. |
| EVT-5 M | Flow: Condition, Once (only the first pass per savegame), Count gate (the N-th pass), Cooldown, Delay (game seconds, survives save / load), Repeat N, Sequence (outputs 1..4 in order), End. An output with several links runs them all (parallel), top to bottom. |
| EVT-6 M | State: variables are game variables (`risen.gamevar`, integers, visible to dialogue and quest conditions) or mod variables in `risen.save` (numbers); Once / Count / Cooldown / pending Delays also live in `risen.save`, so a quick load rolls them back together with the world and nothing fires twice or is lost. |
| EVT-7 M | **Movement debounce:** `on_move` / `on_area` consume player positions through one pure filter (`movement_step` in `modkit/events.lua`): a sample is produced only when the player is >= 1 m (100 units) away from the last sample; rotation and smaller movement never produce one; enter / leave of areas are judged on samples only. Unit-tested without the game. |
| EVT-8 M | Validation (Problems panel and the node's red border): a graph needs a trigger; a trigger without a link, nodes no trigger reaches, required fields empty, unknown quest / item / template / NPC, loops without a Delay (error), `on_move` below 1 m. Export refuses what validation marks as error. |
| EVT-9 M | Export `data/events.lua` (plain data, one entry per graph with its nodes, their data and outputs) + `modkit/events.lua` (the runtime module, written only when the project has event graphs; its pure-Lua parts are tested with LuaJIT); import from the data files rebuilds identical graphs (positions kept), the project file round trip is exact. |
| EVT-10 S | Layer additions only where cheap: none are needed (chapter changes, movement and time of day are polled per frame by the runtime). "On interact" and "enable a world sector" have no layer event / API and are not offered. |

### 3.12 Mod layers (stage E21)

A mod layer is a named group of the project's placed NPCs, world spawns and container fills that is switched on and off as a whole, like the game's story sectors
(`X.sec` + `X.lrent`, `EnabledOnStartup`, switched by `World::SetSectorStatus`, RE.md section 55).

| ID | Requirement |
|---|---|
| LAY-1 M | Project model `LayerDef { uid, name, label, enabled (at start), enable_when, disable_when, npcs[], spawns[], containers[] }` (`Project::layers`, project file `layers`); an object belongs to one layer. The Layers panel lists the layers, edits members (pickers over the objects not yet in a layer), and draws a plan (x / z, one colour per layer, ring = off at start, tooltip with the object and its layer). |
| LAY-2 M | Switching: `enable_when` / `disable_when` are condition sets (the checks of the Condition node: chapter, quest, variable, item, NPC dead, area ...); the first time one holds the layer switches (once per savegame, looked at about twice a second). The event graphs switch it with the **Layer** node (enable / disable / toggle) and ask it with the **Layer is on / off** check. |
| LAY-3 M | Runtime (`modkit/layers.lua`): state in `risen.save` (`modkit.layer.<name>`, absent = `enabled`), rolled back by a load; members carry a `layer` field in `data/npcs.lua` / `data/world.lua`: an NPC becomes a tracking `risen.spawn.when` rule (made while the layer is on, removed when it goes off, a new body each time it comes back, his own appear / leave conditions still apply), a world spawn is made when the layer is on and taken away when it goes off, a container fill happens once, the first time the layer is on. Nothing is doubled after a quick load. |
| LAY-4 M | **Game sectors:** the layer API `risen.world.set_sector(name, enabled)`, `sector_enabled(name)`, `sectors()` (World::SetSectorStatus / gCSector::IsEnabled); the Events editor has a **Game sector** action and a check, with a picker over the game's sectors read from `projects/World/**.sec` (`core/layers.*` `stock_sectors`; 289 sectors, 21 start off). |
| LAY-5 S | Done in the World view (2026-10-20): members in their layer's colour, a cross for layers that start off, container fills outlined, layers hidden per layer, membership changed in the inspector. Open: containers have no position in the Layers panel's plan, and a layer cannot hold template edits or items placed through the balance data. |

### 3.13 Strings and voice recording (stage E22)

One table of every text of the mod and of the game (the **Strings** panel, `app/panels/strings_panel.cpp`; model and export in `core/voice.*`, recordings in `core/audio_convert.*`,
the process runner in `core/process.*`). It replaces the former Strings panel of the free string rows (EXT-3): those are its `string` rows.

| ID | Requirement |
|---|---|
| VOX-1 M | **All strings in one table**: the project's texts (dialogue options, lines, SaySound commands and log nodes, quest titles and log entries, event graph texts, labels and descriptions of items, recipes, spells, NPC templates, placed NPCs, layers, free string rows) and every row of the game's string tables (`GameData::string_rows`: table, id and the non-text columns `<Language>_StageDir`, `<Language>_WaveFile`, `Voice`, `Info`, `Order`, `Quest`, `Owner`, `Role`, `Group`). Columns: id, source, speaker, audio status per mod language, text, stage direction, context. A virtual list (clipper) over an index built once (the game's ~24 000 rows on a worker thread, ~0.5 s); filters (text, source, kind, speaker/voice, dialogue/quest/info, language, spoken only, missing audio, overridden rows) run only when they change. |
| VOX-2 M | **Editing**: rows of the mod are edited in place (`set_line_text`, `set_line_stage`: the node / object they come from; undo through the history). Game rows are read-only; *Override in the mod* makes a free string row with the game's texts (`override_game_line`), the game row is then marked overridden and hidden. Dialogue nodes take an editor-only stage direction per language (node data `stage`). |
| VOX-3 M | **Stable line ids**: a dialogue node is `<mod id>_<first 8 hex of its uid>` (an option without own info name: its info name); they survive saving, renaming and moving. The expected recording is `<line_id>_<Language>.ogg`; `runtime_sound` is the name the layer plays it under, `INFO_SPASS_<mod>_<info>_<n>_<Language>_%_SPASS.wav` (`spass::sound::voiceBaseName`). |
| VOX-4 M | **Export for a studio** of the shown rows: `voice_lines.csv` (UTF-8 with BOM, CRLF, RFC 4180; the columns of USER_GUIDE.md "Voice recording"), `voice_lines.json`, `scripts/<speaker>_<Language>.txt`; options: languages, only missing, only spoken / every text. GUI (*Export for voice recording...*) and `--voice-export DIR [--language L] [--missing] [--all-text] [--with-game] [--no-scripts]`. |
| VOX-5 M | **Batch import** of a folder onto the shown (or all) spoken lines: matched by `<line_id>_<Language>` (name or code), `<line_id>` (a chosen language), the runtime name, or the `file` column of a filled CSV (bare name or relative path); a preview lists matched (copy / convert / cannot), unmatched, ambiguous and duplicate files (duplicates and ambiguous files are not taken). Applying copies / converts into `voice/` of the project, adds the assets and sets the node's `sound` per language: one undo step. `--voice-import DIR [--csv FILE] [--dry-run]`. |
| VOX-6 M | **Conversion with ffmpeg**: files the game does not play (anything but Ogg Vorbis, WAV PCM/float/ADPCM, MP3: RE.md section 28) are converted to Ogg Vorbis (libvorbis; 16-bit WAV without it), mono 44.1 kHz like the shipped speech by default, optional EBU R128 loudness normalisation, optional for every file; the result is checked with the layer's probe. ffmpeg from `PATH` or Settings > Audio (never bundled); without it playable files are imported and the others listed with how to get ffmpeg. |
| VOX-7 M | **Never block the UI**: the game rows, the ffmpeg probe, the preview and the import run on worker threads; the import shows progress and can be cancelled (the running ffmpeg is killed). Processes are started without a shell (`CreateProcessW` with MSVC quoting on Windows, `posix_spawnp` elsewhere), output captured. |
| VOX-8 M | **Verified**: unit tests (`voice_*`, `ffmpeg_*`: rows and ids across a save, names against `voiceBaseName`, CSV quoting round trip, matcher cases, import + undo, command line builder, probing with a mocked runner, a real ffmpeg conversion and cancel when ffmpeg is on PATH; `voice_game_rows` with `--data`), screenshots, scenario `modkit_voice` (English and German voice). |

### 3.10 Export and import (stages E2 onwards, E9)

| ID | Requirement |
|---|---|
| EXP-1 M | Export writes a self-contained mod (section 7): `mod.toml`, `main.lua`, `modkit/runtime.lua`, `data/*.lua`, `strings/*.csv`, `assets/`, `custom.lua` (created once, never overwritten), `modkit/project.json` (the source project). Deterministic output (sorted keys, no time stamps). |
| EXP-2 M | Export validates first; refuses a target folder that holds a mod it did not create (no `modkit/project.json`) and any folder inside the game's `data` directory. |
| EXP-3 M | The exported mod uses the layer's declarative API only; numbers that the layer validates (template names, skills, commands) are validated by the editor too, so a mod that exports cleanly loads cleanly. |
| IMP-1 B | Import an exported mod: read `modkit/project.json` (exact round trip). |
| IMP-2 B | Import without `project.json` or after hand edits of `data/*.lua`: evaluate the data files in a sandboxed LuaJIT state (they are plain tables) and map them back to the project model; anything that does not map is reported and kept as "raw Lua" for re-export. |
| EXT-1 S | Requirement and modifier lists keep the engine's **order** and the modifier **mode** (add, set, max, percent): `ItemDef` stores the amounts as skill -> number maps plus `requirement_order`, `modifier_order` and `modifier_modes` (empty = alphabetical, all add); the export writes the list form `{ { skill =, amount =, mode = }, ... }` only where order or a mode matters and the plain `{ Skill = amount }` table otherwise; the Items panel edits rows with a mode combo and up buttons. The engine's amounts are integers (`gCSkillValue.Amount` is a long; the layer refuses others), so there are no non-integer amounts to keep. |
| EXT-2 S | **Template patches** (`Project::patches`, Patches panel, Browser action "Patch this template's properties"): template + `{ Set = { Property = value } }` with the property picker of the Items panel; `data/patches.lua`, applied by the runtime with `risen.template.patch` on the first tick and again with `instances = true` on every session start, a bad property costs only itself. Validated against the game's template data (set and property exist). |
| EXT-3 S | **Free string rows** (`Project::strings`, the `string` rows of the Strings panel, VOX-1): id -> text per language for rows no object owns; exported into `strings/*.csv` next to the objects' labels (an object's label wins), read back from the csv files, `\r\n` kept. Texts fall back to the first language only (`all.csv`), never to another language: a row without first-language text stays in its own file (validation warns). A name table of a new object carries `default` (the layer's `set_text` serves it to the languages the table does not name). |
| EXT-4 S | **Shipped dialogue edits** (`Project::dialog_edits`, Shipped dialogues panel): per shipped info an XP reward added or changed and trader refills (`RunScript OnTradeInfo_RefillTradeItems`, docs/RE.md section 44); `data/dialog_edits.lua`, applied once per process on the first tick with `risen.dialog.get` / `add_command`; the refills of chapter 1 are also handed out when a new game starts. **Property edits** (`DialogEdit::ops`, runtime VERSION 12): a value of the info, of one of its commands or conditions (by place in the list) changed where it still has the old value (a mismatch is warned about and left), or a condition appended (`add_condition`: class and properties); applied before the XP edits. |
| EXT-5 S | **Teaching limits** (`Project::teachers`, Shipped dialogues panel): a placed NPC's entity ID, a skill and the highest value he teaches (`risen.npc.set_teach_limit`); `teachers` in `data/world.lua`, written on every world.ready (idempotent). Runtime VERSION 11. |
| IMP-3 S | `risen-pack2mod --project FILE.rmk.json` writes a converted Risen mod's archives as a project (items changed and new with the engine's list order and modes, recipes, NPC / projectile template edits, template patches, container fills with equip marks, XP and refill edits of shipped dialogue infos, teaching limits, texts per language and free string rows, assets in the project's `assets/`, with `--world-edits` moved / removed / added world entities); what the engine cannot hold is listed; `--verify` exports the project again and compares it with the layer mod of the same run, and recomputes the world edits from the layers (`core/pack2project.*`, docs/PACK2MOD.md "Editor project"; live: scenario `extras`, `scripts/pack2project-live.ps1`). |

## 4. Non-functional requirements

| ID | Requirement |
|---|---|
| NF-1 | C++20, CMake >= 3.25, vcpkg manifest (feature `modkit`): glfw3, imgui (docking, glfw and opengl3 bindings), imgui-node-editor, imguizmo, glad, nlohmann-json, stb, nativefiledialog-extended, miniaudio (sound preview; Ogg Vorbis through stb_vorbis), zlib, luajit (+ sol2), assimp (only for AST-4). No Windows API outside `#ifdef _WIN32`. |
| NF-2 | Builds on Windows (MSVC, the repository presets), Linux (GCC/Clang) and macOS (Apple Clang, OpenGL 3.3 core profile, forward compatible). On non-Windows hosts only the editor and its tests are built (the layer DLL is Windows-only). |
| NF-3 | The game data is read only; the tool never writes into the game directory (the same guard as pack2mod). Paths of the game install are never stored in an exported mod. |
| NF-4 | Startup with a cached index under 3 s; the first index of the game archives under 30 s; the world view loads `World` (608 layer files) progressively without blocking the UI (background thread + main-thread GPU upload). |
| NF-5 | Robust parsing: a damaged or unknown file costs only itself (logged), never a crash; every reader is bounds-checked (the `p2m::rd*` helpers throw). |
| NF-6 | Licensing: the editor ships no game content; a project and an exported mod contain only names of shipped assets and the modder's own data and files. |
| NF-7 | Code style of the repository: comments explain why, docs in English, `docs/editor/` holds the editor's documentation. |
| NF-8 | The editor is localizable: its own texts are looked up through `tr()` (`core/i18n.h`); English is embedded and the fallback, other languages are TOML files (`src/modkit/lang/`, user folder `lang/`), German ships with the editor; docs/editor/LOCALIZATION.md. |

## 5. Architecture

```
src/modkit/
  core/        no GUI dependency; built into modkit_core (linked by the app and by modkit_tests)
    project.*       the project model (items, recipes, npc templates, placed npcs, dialogues, quests, world edits, assets) + JSON (de)serialisation
    commands.*      undo/redo command stack (snapshot-based: a command stores the JSON of the object before and after)
    gamedata.*      archive set (pak + pNN layering), template catalogue, string tables, asset index, cache
    validate.*      the Problems list (one function per object kind)
    export.*        project -> mod folder (Lua emitter, strings, assets, runtime)
    import.*        mod folder -> project (project.json, else sandboxed Lua data)
    lua_writer.*    deterministic Lua table writer (shared with pack2mod's style)
    world.*         world loading: .wrl/.sec/.lrent -> entities (name, template, GUID, matrix, props)
    mesh.*          ._xmsh / ._xmat / ._ximg decoding into CPU meshes and images
    runtime_lua.h   the embedded modkit/runtime.lua (from src/modkit/lua/runtime.lua, embedded by CMake like pack2mod's main.lua)
  app/         GLFW + ImGui application: main.cpp, app.*, panels/*.cpp (browser, items, recipes, npcs, dialogs, quests, world, problems, log, settings)
    render/         GL helpers: shaders, mesh/texture upload, camera, picking, debug lines
    nodes/          the node-editor wrapper shared by dialogues and quests (graph <-> imgui-node-editor ids, pins, links, layout)
  lua/runtime.lua   the generic run-time part of every exported mod
tests/modkit_tests.cpp   unit tests (no GUI, no game data needed; tests that need the game data skip without --data)
```

* **Reuse.** The archive, template, string-table and world readers already exist for `risen-pack2mod` (`src/pack2mod/pak.*`, `tple.*`, `tab.*`, `world.cpp` scanner,
  `util.*`). They are made portable (`_wfopen` only on Windows) and linked through `pack2mod_core`; the world scanner's entity record logic moves into a public header
  so the editor and pack2mod share it.
* **Model first.** All panels edit `Project` through commands; panels hold no state of their own except UI state (selection, scroll, canvas positions are part of the model,
  so a reopened project looks the same). Validation and export work on the model only, so they are unit-testable.
* **Names as references.** Objects reference each other by name (template names, quest names, NPC names) exactly as the exported Lua does; renaming an object updates
  references through a model-wide rename command.
* **Threads.** Indexing and world/mesh loading run on a worker thread and hand finished CPU data to the main thread, which uploads to the GPU. The model is touched only on the
  main thread.
* **CMake.** The top-level `CMakeLists.txt` keeps the Windows layer targets inside `if(WIN32)` and adds `src/modkit` when `SPASS_BUILD_MODKIT` is on (default ON when the
  `modkit` vcpkg feature is enabled in the preset). `vcpkg.json` marks `minhook` Windows-only and puts the editor's packages in the feature `modkit`, so a Linux/macOS
  configure installs only what the editor needs. The presets `default` and `ninja` enable the feature.

## 6. Data model and files

**Project file** `<name>.rmk.json` (UTF-8 JSON, pretty printed, stable key order, so it diffs well in git):

```json
{
  "format": "risen-modkit-project", "format_version": 1,
  "mod": { "id": "my_mod", "name": "My Mod", "version": "1.0.0", "author": "me", "description": "...", "languages": ["English", "German"] },
  "items":        [ { "uid": "…", "mode": "new", "name": "My_Knife", "base": "It_1H_Machete", "label": {"English": "Hob's Knife"}, "value": 250, "damage": 20, "requires": {"Strength": 10}, "properties": {} } ],
  "item_groups":  [ { "group": "Items/Potions", "value_mult": 0.5 } ],
  "recipes":      [ { "uid": "…", "mode": "new", "name": "My_Plan", "station": "anvil", "ingredients": [["It_IronOre", 2]], "result": "My_Knife", "skills": {"Smith": 1}, "learn": true } ],
  "npc_templates":[ { "uid": "…", "mode": "new", "name": "My_Hob_T", "base": "Peasant_1_01", "label": {"English": "Hob"}, "hp_max": 300, "loot": {"It_Apple": 2} } ],
  "npcs":         [ { "uid": "…", "name": "My_Hob", "template": "My_Hob_T", "position": [1200.0, 50.0, -3400.0], "yaw": 90, "label": {"English": "Hob"}, "peaceful": true, "items": [["My_Knife", 1, true]] } ],
  "dialogs":      [ { "uid": "…", "owner": "My_Hob", "nodes": [ … ], "links": [ … ] } ],
  "quests":       [ { "uid": "…", "name": "Q_My_Ore", "title": {"English": "Ore for the Forge"}, "nodes": [ … ], "links": [ … ] } ],
  "world":        { "world": "World", "spawns": [ … ], "containers": [ … ], "removals": [ … ], "moves": [ … ] },
  "assets":       [ { "file": "assets/My_Knife._xmsh", "kind": "mesh" } ],
  "editor":       { "notes": "…" }
}
```

* `uid` is a random 128-bit id per object (stable across renames; graph nodes and links have their own ids, used by the node editor).
* `mode` is `"new"` (a template the mod creates) or `"edit"` (a change of a shipped one). Unset numeric fields are absent (an absent field means "leave as shipped").
* Text per language is a `{ Language: text }` object; the export maps it to `name = { English = ..., German = ... }` (templates) or `text = { default = ..., de = ... }`
  (dialogue lines; the language codes come from the layer's voice/loc table).
* Graphs (dialogues, quests): `nodes` = `{ uid, type, pos: [x, y], fields... }`, `links` = `{ uid, from: [node, pin], to: [node, pin] }`. Pins are named per node type
  (section 7.3), so links survive a reorder of a node's fields.

**Per-user files**: `settings.json` (data dir, recent projects, UI scale), `imgui.ini`, `index-cache/` (the game index, keyed by the archive file sizes and modification times).

## 7. Export: the generated mod

### 7.1 Layout

```
<mods>\<id>\
  mod.toml               id, name, version, author, description, api_version = 1, assets = "assets"
  main.lua               generated, short: require the runtime and the data, call runtime.install(data); then require("custom") when it exists
  custom.lua             created once with comments; the export never overwrites it (hand-written additions)
  modkit\runtime.lua     the generic code (embedded in the editor, versioned: runtime.VERSION), the same for every exported mod
  modkit\project.json    the editor project the mod was made from (import, IMP-1)
  data\balance.lua       return { items = …, item_groups = …, new_items = …, npcs = …, new_npcs = …, recipes = …, new_recipes = … }  (a BalanceSpec)
  data\npcs.lua          return { { name = …, template = …, position = {…}, … } }                  persistent NPC declarations (+ spawn conditions)
  data\dialogs.lua       return { { name = …, owner = …, parent = …, when = <cond data>, lines = {…} } }  dialogue options in creation order
  data\quests.lua        return { { name = …, title = …, type = …, xp = …, start = …, objectives = {…}, outcomes = {…} } }
  data\world.lua         return { spawns = {…}, containers = {…}, removals = {…}, moves = {…} }
  strings\all.csv        texts that are not inline (log entries keyed by id), English fallback; strings\<Language>.csv per language
  assets\                the project's custom asset files
```

### 7.2 Runtime behaviour (`modkit/runtime.lua`)

Each data file is applied with the API call that is savegame-safe for it (MODDING_GUIDE section 3):

| Data | Applied with | When |
|---|---|---|
| balance | `risen.balance.install(spec)` | the layer applies it on the first `tick` and on every `session.start` |
| npcs | `risen.npc.persistent{ …, define = define_content }` (or `risen.spawn.when` with conditions) | declared at load time; the layer spawns/restores |
| quests, dialogs | `define_content()`: `risen.quest.define` then `risen.dialog.create` in order (parents first) | on `load.before` and on every `session.start` (also through `persistent{define}`) |
| quest logic | one script per quest action (`risen.script.define("MK_<id>_…")`) called from dialogue lines; objectives fed by `inventory.changed`, `entity.died`, `info.finished`, `gamevar.changed`; progress and "reward given" in `risen.save` | events |
| world spawns, container items | once per savegame on `world.ready` (a key per entry in `risen.save`, the spawned entity's ID remembered) | `world.ready` |
| removals, moves | once per savegame (B: needs the layer APIs of section 10) | `world.ready` |

Conditions are exported as data (`{ "quest", "Q_X", "Running" }`, `{ "all", {…} }`) and turned into `risen.cond.*` objects by the runtime, so `data/*.lua` stays plain
data that the importer can read back (IMP-2) and that a human can read.

### 7.3 Dialogue graph -> `risen.dialog.create`

* Each **Option** node becomes one info: `name = MK_<mod>_<owner>_<n>` (stable: the node uid decides the number once and the project keeps it), `owner`, `parent` (the option
  whose "then" pin leads here), `permanent`, `type`, `when` (the condition chain on its "condition" pin).
* The **sequence** from the option's "lines" pin (Line, Command, Script, Log, Quest action nodes linked one after another) becomes `lines` in order; a Quest action becomes a
  `script` line that calls the runtime's generated script for that action.
* A path that ends without an `End` command gets one automatically when the option has no children (an open conversation blocks saving, MODDING_GUIDE section 3 rule 6).

### 7.4 Quest graph -> quest data

The Quest node gives `risen.quest.define{ name, title, type, xp }`. Start triggers, objectives and outcomes become the `start`, `objectives`, `outcomes` entries of
`data/quests.lua`; the runtime registers one handler per event kind and advances every running quest's objectives (the tracker of `mods/example_quest_tracker`, generalised:
several objectives per quest, all must be done, in order or in any order).

**Branches (E18).** The Either / or node (`branch`) has one output with several links; each linked node must be an Objective (the gate of a path) and the paths are ordered top
to bottom by their position on the canvas. The flow is exported as one flat list of steps: the branch is a `race` step `{ kind = "race", paths = { 2, 5 } }` (1-based indexes of
each path's first step, the path's own steps follow right after the race step) and a path that does not end in an Outcome is closed by `{ kind = "stop" }`, so the runtime
never runs on into the next path. Paths do not join (a node reached twice is an error). The runtime (`modkit/runtime.lua` version 8) keeps one counter table per gate in
the quest's state (`s.gates["<step>"] = { n, talked }`), looks at all gates on every event, and the first gate whose objective is done sets `s.step` to the step after it.
### 7.5 Event graphs -> `data/events.lua` (E21)

Each graph becomes `{ name, key, enabled, nodes = { { id, type, pos = { x, y }, <the node's fields>, outs = { next = { "id", ... } } }, ... } }`; `id` is the first 10 hex digits of the node uid (stable,
the key of the Once / Count / Cooldown state in `risen.save`: `modkit.ev.<graph key>.<id>`), `outs` names the targets per output pin (`next`, `then`, `else`, `1`..`4`, `again`, `body`, `done`),
ordered top to bottom by the target's position. `modkit/events.lua` registers one handler per trigger kind used by any graph, evaluates conditions at once, runs action nodes through the layer's API, keeps
a step budget per run (no endless loops) and the pending delays in `risen.save` (`modkit.ev.pending`, a clock in `modkit.ev.clock` that only advances while the world runs).
Movement and areas: see EVT-7. Mod events for tests: `"modkit.event"` (mod id, graph name, node id, trigger kind) when a trigger starts a graph, `"modkit.event_node"` (mod id, graph name, node id, node type) per executed node.

### 7.6 Mod layers -> `data/layers.lua` (E21)

`data/layers.lua` is a list of `{ name, enabled, enable_when?, disable_when? }` (condition sets are `{ mode, negate, tests = { { kind = "chapter", op = ">=", value = 2 }, ... } }`, the same data the
Condition node of an event graph has). Members are not listed there: every member names its layer (`layer = "Siege"` in `data/npcs.lua` entries and in the `spawns` / `containers` of
`data/world.lua`), so the import rebuilds the membership from the data files alone. `modkit/runtime.lua` (version 9) requires `modkit/layers.lua` and `modkit/events.lua` when `layers` is given:
NPC rules use `layers.cond(name)` (a `risen.cond.fn` re-evaluated on `modkit.layer_changed`), `apply_world` consults `layers.enabled(name)` and runs again on every layer change.
Mod event for tests: `"modkit.layer_changed"` (name, enabled).

## 8. Testing and verification

1. **Unit tests** (`modkit_tests`, runs in CI without game data): project JSON round trip, undo/redo, validation rules, Lua writer determinism, export of the example
   projects compared with committed golden output (`tests/modkit/golden/<project>/…`), import of the golden output back into an equal project, mesh/DDS decoding of
   synthetic files. Tests that need the game data (`--data DIR`) parse every template, string table, world layer and mesh of the real archives and report counts and failures.
2. **Live tests** (Windows, the layer's test loop): the golden export of `examples/modkit/showcase.rmk.json` is installed as a mod and checked by a driver mod with a
   `risen.test` scenario (`modkit`): items exist with their numbers, recipe craftable, NPC present at the position after a new game and after a load, dialogue options
   offered under their conditions, quest advances and pays once, chest contents added once, world spawns once. `test-run.ps1 -Mods modkit_showcase,modkit_driver -Scenario modkit`.
3. **UI checks**: `risen-modkit --screenshot out.png --panel <name> --frames 10 <project>` renders the panel with the project loaded and writes a PNG; the stages look at
   these images (the Read tool shows them) to check layouts, the node canvas and the 3D view.
4. **User manual**: `docs/editor/USER_GUIDE.md` grows with each stage (what the panel does, the export mapping, limits).

## 9. Stages

Each stage ends with a usable feature (or the base the next stage needs), is verified (unit tests, screenshots, and in the game where the export changes) and committed.

| Stage | Delivers | Requirements | State |
|---|---|---|---|
| **E0** Foundation | CMake/vcpkg feature `modkit`, portable pack2mod readers, app shell (GLFW, ImGui docking, menus, settings, log), project model with JSON and undo/redo, `--screenshot`, `modkit_tests` | APP-1..6, NF-1..3 | **done** (Windows; Linux/macOS builds not verified: no host) |
| **E1** Game data browser | archive set, template catalogue with kinds and groups, string labels, asset index, browser + property inspector, index cache | DATA-1..6 | **done** (2664 templates, 24213 strings in 7 languages, 19 archives in 0.7 s: the cache of DATA-6 is not needed) |
| **E2** Items + export | item editor (shipped edits, new items, groups), export pipeline (runtime, balance data, mod.toml, project.json, custom.lua), validation, golden test, live scenario `modkit` (items) | ITEM-1..4, EXP-1..3 | **done** (scenario `modkit`: 12 checks pass in the game) |
| **E3** Recipes | recipe editor (new, edits), station/ingredient pickers incl. project items, live check (craft) | REC-1..3 | **done** (REC-1, REC-2; REC-3 with E4/E5. Scenario `modkit`: the plan is given, crafting makes the new item) |
| **E4** NPCs and monsters | NPC template editor, inventory/equipment/loot, placed NPCs with numeric positions and routines, live check | NPC-1..4, (NPC-5 with E5) | **done** (NPC-1..4; offline NPC numbers from the decoded gCSkills_PS, checked against all 105 rows of NPC_PROPERTIES.md; scenario `modkit`: 25 checks) |
| **E5** Dialogues | node editor framework, dialogue graph, condition builder, export to `risen.dialog.create`, validation, live check (options offered, branches) | DLG-1..4 (5, 6) | **done** (DLG-1..4 and DLG-6's comments; DLG-5 preview open. Scenario `modkit`: 33 checks, the conversation is played like a player) |
| **E6** Quests | quest graph (start, objectives, outcomes, chains), dialogue integration, runtime tracker, live check (fetch/kill/talk quest) | QST-1..4 | **done** (scenario `modkit`: 42 checks: a quest started by another's success, kills counted, rewards paid once, a quick load rolls the progress back, a quest started with the game) |
| **E7** World view | world/sector/layer loading, 3D view with boxes (monsters as actors since E19b), camera, picking, inspector, layer toggles | WLD-1, WLD-2, WLD-9 | **done** (48347 entities of 317 layers in 1.5 s; boxes by kind, fly camera, picking, inspector with properties and inventory; RE.md section 49) |
| **E8** Meshes and textures | `._xmsh`/`._xmat`/`._ximg` decoding, GPU cache, LOD, mesh preview in pickers | WLD-3, AST-1, DATA-7 | **done** (WLD-3, AST-1 mesh previews; 1665 of 1666 meshes decode, all 25921 entity meshes resolve; DATA-7 icon previews: E15) |
| **E9** Import | project.json import, sandboxed Lua data import, raw-Lua passthrough | IMP-1, IMP-2 | **done** (export -> import from the data files alone -> export gives identical files; unknown balance sections are kept and exported again) |
| **E10** Placement | gizmo placement of NPCs, items in the world, chest contents, ground snapping, export of `data/world.lua`, live check | WLD-4..6 | **done** (click on the ground through the depth buffer, ImGuizmo moves, chest contents; scenario `modkit`: 46 checks, nothing doubled after a load) |
| **E11** Custom assets | compiled asset files in the project, sounds for dialogue lines with preview, image -> `._ximg`, icons, (B) meshes via assimp | AST-2..5 | AST-2, AST-3, AST-5 **done** (Assets panel; pictures -> DXT textures with mips or icon atlas + `.ximl`, verified drawn in the game; the 3D views read project files first like the engine's name cache; sound picker over 77 000 game sounds with playback through miniaudio; scenario `modkit`: the layer compiled the line's Ogg and MP3 and the engine asked for the line's voice); AST-4 open |
| **E12** World edits (bonus) | layer API for transform/remove of entities (RE + live verification, types/risen.d.lua), static mesh placement, moving/removing existing entities | WLD-7, WLD-8 | **done** (`Entity:pose` / `set_pose` via `Entity::MoveTo`, RE.md section 50; objects placed with a rotation and kept in the savegame; world entities moved/turned or removed by ID on every `world.ready`; scenario `modkit`: 55 checks, after a load too) |
| **E13** Play and game errors | Play button (export + start windowed / hot reload), the game's errors in Problems linked to project objects, `--game-dir DIR --play` | APP-7, APP-8 | **done** (`core/play.*`, `core/gamelog.*`, unit tests `game_log_*`, `play_config_helpers`; live: the editor started the isolated test copy, the layer loaded the exported mod and the autostart helper began a new game; a second Play with the game running reported the hot reload) |

| **E14** Dialogue playthrough | the Conversation panel: a dialogue played like a player against a pretend world (quests, items, variables, events, skills, dead NPCs; conditions pinned yes/no), `core/dialog_sim.*` | DLG-5 | **done** (unit tests `dialog_sim_*`: conditions, a full run with permanent / used-up options, sub-options, Important, End, the showcase; screenshots checked; the rules are the layer's, so the engine itself was not run) |

| **E15** Icon previews | `compiled_imagelists.bin` decoded (`core/imagelist.*`, RE.md section 51), icons drawn from the atlas in the item lists, pickers, Browser, recipes and the icon combo | DATA-7 | **done** (768 images in 7 collections; 479 of 503 item icons resolve, the rest are the engine's own gaps; unit tests `imagelist_*`; screenshots checked; project pictures used as an icon are not previewed) |

| **E16** Shipped loot | a template's starting inventory decoded (`core/tpl_inventory.*`, RE.md section 52); the NPC panel shows what the template carries and what it wears, "Start from the shipped loot" | NPC-3 | **done** (359 of 361 NPC and monster templates readable, all equal to the engine's `template_inventory`; unit tests `inventory_*`; screenshot checked) |

| **E17** Spells and NPC groups | the Spells panel (`spells`, `new_spells`) and the NPC groups panel (`npc_groups`): model, project JSON, export, import from the data files (no raw data any more), validation, `core/spells.*`, example `examples/modkit/spells.rmk.json` | SPELL-1, NPC-6 | **done** (unit tests `spells_*`, `npc_groups_*`, `lua_patterns_*`; live: the exported example was installed by the layer, "applied 8 of 8 entries", and a wrong setting was refused with the layer's own message) |

| **E18** Quest branches | the Either / or node: `race` / `stop` steps, runtime version 8, import from data files rebuilds the branches | QST-5 | **done** (unit tests `quest_branch_*` incl. a run of the generated runtime against a stand-in API: both ways announced, progress per path, the first objective decides, a reward once, a load rolls back; live: the linear showcase flows still pass, 60/60 of scenario `modkit`) |

| **E20** Palette and wizards | the command palette (Ctrl+P: project objects, shipped templates, panels, commands; `core/search.*`) and the Wizards menu (fetch quest, weapon with recipe, trader with stock; `core/wizards.*`, `app/wizards.*`) | EDT-1, EDT-2 | **done** (unit tests `search_*`, `wizard_*`: what a wizard makes compiles, is played through by the dialogue simulator and validates against the game data; screenshots checked; the trader's `DoTrade` was not tried in a real trade) |

| **E19a** 3D model import (AST-4) | OBJ / glTF / GLB -> `._xmsh`: the writer `core/meshwrite.*` (byte-exact against a shipped mesh, RE.md section 53), the readers `core/model_import.*`, the Assets panel's **Import 3D model...**, `--convert-model`, the probe mods `meshprobe` / `meshprobe_shots` (scenario `meshprobe`) | AST-4 | **done** (unit tests `meshwrite_*`, `model_import_*`; live: the engine loaded the mesh and drew it textured, and the first winding was found wrong from the pictures and fixed; the handedness default is inferred) |

| **E19b** Actors in the World view | `core/actor.*` reads the EMotion FX actor inside `._xmac` (RE.md section 54); the World view draws monsters, animals and humans as their textured bind pose (T-position) instead of boxes; humans and ogres are assembled from skeleton, armour and head parts | WLD-3 | **done** (unit tests `actor_*`, `world_monsters_have_actors`: 107 of 111 actors (five stored big endian), 1253 of 1306 monsters, `world_humans_are_assembled_from_parts`: all 246 NPCs with body and head; Sara and Gyrger checked in pictures of the world view; the humans wear a stand-in head, the real one is not in the data; SpeedTrees stay boxes) |

| **E21** Events, triggers, layers and sectors | the Events tab (event graphs: triggers, conditions, flow, actions; `core/events.*`, `lua/events.lua`), the Layers panel (mod layers, `core/layers.*`, `lua/layers.lua`), export `data/events.lua` / `data/layers.lua` + `modkit/events.lua` / `layers.lua`, import, validation, movement debounce, `risen.world.set_sector / sector_enabled / sectors` | EVT-1..10, LAY-1..5 | **done** (unit tests `events_*`, `layers_*`: the example graph, every node kind, validation, project and data round trips, the movement filter in C++ and Lua, the generated Lua run against a stand-in API (chapter -> variable, once, delays that survive a load, signals, movement per metre, areas, layers switched by condition and graph); live scenario `evlive` (23 checks): chapter 3 sets the variable, movement fires once per metre and never for turning, a layer's NPC appears / goes / is not doubled by a quick load, a graph switches a game sector and the lizard scouts of `Monastery_Story_LizardAttack_Off` appear; screenshots of both panels checked; EVT-10 open: no interact event, no World view tag) |

| **E22** Strings and voice recording | the Strings panel over every text of the mod and the game (`core/voice.*`, `GameData::string_rows`), export for studios (CSV / JSON / scripts), batch import of recordings with matcher and preview, ffmpeg conversion in the background (`core/audio_convert.*`, `core/process.*`), Settings > Audio, `--voice-export` / `--voice-import` | VOX-1..8 | **done** (unit tests `voice_*`, `ffmpeg_*` incl. a real ffmpeg; 24218 game rows in 0.47 s; scenario `modkit_voice`: a stereo 48 kHz FLAC converted to mono 44.1 kHz Ogg Vorbis, a WAV copied and an AIFF named in the filled CSV play for the NPC's and the hero's lines with English and German voice, FMOD type 10, 1200 ms; `examples/modkit/voice.rmk.json`) |

| **E23** Localization | `core/i18n.*` (`tr` / `trc` / `trf` / `N_`, TOML language files, discovery, system language), every UI text and the messages of the core wrapped, Settings > Interface > Language, `--lang`, `tools/i18n.py` (extract / update / check), `lang/de.toml` (German, complete), panel titles as `translated###English` ids | NF-8 | **done** (unit tests `i18n_*`: lookup, id suffix, context, placeholders, discovery and override, broken files, every shipped file keeps its placeholders; screenshots of the German UI checked; CI runs `tools/i18n.py check`) |

All of the roadmap of 2026-10-17 is done (E13 to E20).

Order rationale: E0-E2 give a usable tool with a real mod at the end of E2; E3-E6 add the other content kinds on the same export pipeline; the world (E7-E8) is independent
of the content editors and needs format work; placement (E10) needs both; E11/E12 need engine-side work and verification.

## 10. Risks and open questions

* **Mesh format (E8, solved).** `._xmsh` is a GR01 container (`MS02`): a property schema (`eCMeshResource2`: `SubMeshes` with `MaterialName`, `FirstIndex`,
  `IndexCount`, `FirstVertex`, `VertexCount`, `Boundary`), then a D3D9 vertex declaration, a header (vertex data size, stride, index data size, index format
  101/102), the interleaved vertices (position, normal, tangent, two colours, one or two UV sets: 28 to 68 bytes) and absolute indices (RE.md section 49).
  1665 of 1666 meshes decode (the SpeedTree billboard has FLOAT4 positions); level meshes name their mesh by a virtual path (`#G3:/data/raw/...`): the
  bare name is what resolves.
* **Textures.** `._ximg` holds a DDS (RE.md section 26); DXT via `GL_EXT_texture_compression_s3tc` where present (all desktop drivers including macOS), else a CPU decode.
* **World size.** `World` has ~600 layer files with thousands of entities and large level meshes: progressive loading, culling, a distance limit for mesh drawing.
* **Rotation of spawned entities, removal and moving of world entities (E12)** -- solved: `Entity:set_pose` (MoveTo) for the rotation and moves, `Entity:kill`
  for removals; the runtime applies removals and moves on every `world.ready`, so it does not depend on what the engine saves (RE.md section 50).
* **Static mesh templates** -- a spawned object is drawn and stands where it was placed, but it is not save-relevant by itself: the runtime sets it
  (RE.md section 50). Whether such an object collides was not checked.
* **Custom meshes (AST-4, solved in E19a).** The MS02 layout is reproduced byte for byte and the engine draws a converted OBJ (RE.md section 53); the handedness default is inferred.
* **Icons (solved in E15).** `compiled_imagelists.bin` is decoded (RE.md section 51); the editor draws the shipped icons from the atlas. A picture of the project used as an item icon is not previewed.
* **Placed NPC names.** Dialogues of a *shipped* NPC need its unique entity name; the world layers provide it (the entity name of an NPC instance), the catalogue lists them.
* **NPC numbers offline (solved in E4).** `gCSkills_PS` is not a reflected property set, but the template stores it as a fixed record (`c9 00 <u32 0x90> c9 00 <u32 0> <u16 4>`
  and 34 int32 base values in `gESkill` order) after the set's name index; `core/gamedata.cpp` decodes it and the data test compares every NPC of
  `docs/NPC_PROPERTIES.md` (hit points, level, experience, attributes, protection, guild, species). Species numbers 4, 22, 26 (Ghost, Ogre_Tame, Wolf_Tame) are
  used by no shipped NPC and inferred; attitude, bravery and damage calculation names follow the layer's order (not cross-checked). A template's starting
  inventory (`gCInventory_PS` stacks) is decoded since E16 (RE.md section 52) and checked against the engine's answer for every NPC and monster template.
