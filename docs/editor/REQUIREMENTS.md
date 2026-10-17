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
| WLD-6 M | Chest contents: select a container (chest, corpse, any entity with a `gCInventory_PS`) of the loaded world, add items. Exported as `risen.entity.by_id(<GUID>)` + `risen.item.give` once per savegame (the pattern pack2mod's `world.lua` uses, RE.md section 47). |
| WLD-7 B | Place static meshes (buildings, campfires, props): spawn of a mesh/interactive template at a position and rotation, once per savegame. Needs a layer API for the rotation (section 10). |
| WLD-8 B | Remove and move existing world entities (by GUID). Needs layer APIs `Entity:remove()` / `Entity:set_transform()` that the engine saves (the savegame has a list of removed native entities, RE.md section 32). |
| WLD-9 S | Show the project's placed content and the player's start point; "copy position for Lua" helper. |

### 3.9 Assets (stage E11)

| ID | Requirement |
|---|---|
| AST-1 M | Built-in assets: every mesh/icon/actor picker lists the names of the game archives (DATA-4) with a preview where decoded (mesh in a small 3D preview, icon cell). |
| AST-2 M | Custom compiled assets: add `._xmsh`, `._xmat`, `._ximg`, `._xmac`, ... files to the project; they are copied to `<mod>/assets/` on export and offered in the pickers. |
| AST-3 S | Custom images: PNG/TGA/JPG -> `._ximg` (DDS A8R8G8B8 or DXT inside the GR01 container); icons -> an icon atlas + `.ximl` image list (`risen.assets.load_icons`). |
| AST-4 B | Custom meshes from authoring formats (glTF, OBJ, FBX through assimp) -> `._xmsh` + `._xmat`. Needs a writer of the MS02 container that the engine accepts (verified in the game, section 10). |

### 3.10 Export and import (stages E2 onwards, E9)

| ID | Requirement |
|---|---|
| EXP-1 M | Export writes a self-contained mod (section 7): `mod.toml`, `main.lua`, `modkit/runtime.lua`, `data/*.lua`, `strings/*.csv`, `assets/`, `custom.lua` (created once, never overwritten), `modkit/project.json` (the source project). Deterministic output (sorted keys, no time stamps). |
| EXP-2 M | Export validates first; refuses a target folder that holds a mod it did not create (no `modkit/project.json`) and any folder inside the game's `data` directory. |
| EXP-3 M | The exported mod uses the layer's declarative API only; numbers that the layer validates (template names, skills, commands) are validated by the editor too, so a mod that exports cleanly loads cleanly. |
| IMP-1 B | Import an exported mod: read `modkit/project.json` (exact round trip). |
| IMP-2 B | Import without `project.json` or after hand edits of `data/*.lua`: evaluate the data files in a sandboxed LuaJIT state (they are plain tables) and map them back to the project model; anything that does not map is reported and kept as "raw Lua" for re-export. |

## 4. Non-functional requirements

| ID | Requirement |
|---|---|
| NF-1 | C++20, CMake >= 3.25, vcpkg manifest (feature `modkit`): glfw3, imgui (docking, glfw and opengl3 bindings), imgui-node-editor, imguizmo, glad, nlohmann-json, stb, nativefiledialog-extended, zlib, luajit (+ sol2), assimp (only from E11). No Windows API outside `#ifdef _WIN32`. |
| NF-2 | Builds on Windows (MSVC, the repository presets), Linux (GCC/Clang) and macOS (Apple Clang, OpenGL 3.3 core profile, forward compatible). On non-Windows hosts only the editor and its tests are built (the layer DLL is Windows-only). |
| NF-3 | The game data is read only; the tool never writes into the game directory (the same guard as pack2mod). Paths of the game install are never stored in an exported mod. |
| NF-4 | Startup with a cached index under 3 s; the first index of the game archives under 30 s; the world view loads `World` (608 layer files) progressively without blocking the UI (background thread + main-thread GPU upload). |
| NF-5 | Robust parsing: a damaged or unknown file costs only itself (logged), never a crash; every reader is bounds-checked (the `p2m::rd*` helpers throw). |
| NF-6 | Licensing: the editor ships no game content; a project and an exported mod contain only names of shipped assets and the modder's own data and files. |
| NF-7 | Code style of the repository: comments explain why, docs in English, `docs/editor/` holds the editor's documentation. |

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
| **E7** World view | world/sector/layer loading, 3D view with boxes, camera, picking, inspector, layer toggles | WLD-1, WLD-2, WLD-9 | **done** (48347 entities of 317 layers in 1.5 s; boxes by kind, fly camera, picking, inspector with properties and inventory; RE.md section 49) |
| **E8** Meshes and textures | `._xmsh`/`._xmat`/`._ximg` decoding, GPU cache, LOD, mesh preview in pickers | WLD-3, AST-1, DATA-7 | **done** (WLD-3, AST-1 mesh previews; 1665 of 1666 meshes decode, all 25921 entity meshes resolve; DATA-7 icon previews open: compiled_imagelists.bin is not decoded) |
| **E9** Import | project.json import, sandboxed Lua data import, raw-Lua passthrough | IMP-1, IMP-2 | open |
| **E10** Placement | gizmo placement of NPCs, items in the world, chest contents, ground snapping, export of `data/world.lua`, live check | WLD-4..6 | open |
| **E11** Custom assets | compiled asset files in the project, image -> `._ximg`, icons, (B) meshes via assimp | AST-2..4 | open |
| **E12** World edits (bonus) | layer API for transform/remove of entities (RE + live verification, types/risen.d.lua), static mesh placement, moving/removing existing entities | WLD-7, WLD-8 | open |

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
* **Rotation of spawned entities, removal and moving of world entities (E12).** The layer has `risen.entity.spawn(template, x, y, z)` (no rotation) and no
  `Entity:remove()` / `set_transform`. Needed: binding of the engine's matrix setter and entity removal (`eCEntity` exports), and verification that the engine saves both
  (the savegame lists removed native entities, RE.md section 32; a moved native entity's matrix must be saved with its sector). Until then the editor marks these edits
  "needs layer E12" and does not export them.
* **Static mesh templates.** Whether `risen.entity.spawn` of building/prop templates (no NPC, no item) gives a visible, colliding, saved entity is not verified (E12).
* **Custom meshes (AST-4).** Writing an MS02 file the engine accepts is unverified; the risk is contained by verifying in the game (resource state 3 and
  `risen.debug.screenshot`, RE.md section 26).
* **Icons.** `compiled_imagelists.bin` (`V001`) is not decoded yet; until it is, icon names come from the item templates (`gCItem_PS.IconImage`) and previews are skipped.
* **Placed NPC names.** Dialogues of a *shipped* NPC need its unique entity name; the world layers provide it (the entity name of an NPC instance), the catalogue lists them.
* **NPC numbers offline (solved in E4).** `gCSkills_PS` is not a reflected property set, but the template stores it as a fixed record (`c9 00 <u32 0x90> c9 00 <u32 0> <u16 4>`
  and 34 int32 base values in `gESkill` order) after the set's name index; `core/gamedata.cpp` decodes it and the data test compares every NPC of
  `docs/NPC_PROPERTIES.md` (hit points, level, experience, attributes, protection, guild, species). Species numbers 4, 22, 26 (Ghost, Ogre_Tame, Wolf_Tame) are
  used by no shipped NPC and inferred; attitude, bravery and damage calculation names follow the layer's order (not cross-checked). A template's starting
  inventory (`gCInventory_PS` stacks) is not decoded: the editor sets loot amounts without showing the shipped loot.
