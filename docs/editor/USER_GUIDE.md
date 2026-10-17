# risen-modkit user guide

`risen-modkit` builds mods for the Risen modding layer without writing Lua: you create and change items (later recipes, NPCs, dialogues, quests and
world placements) in a desktop application and export an ordinary mod folder that the layer loads. The plan and the state of the stages are in
[REQUIREMENTS.md](REQUIREMENTS.md); this guide describes what works now.

Contents: [Build and start](#build-and-start) · [First start](#first-start) · [The window](#the-window) · [Items](#items) · [Export](#export) ·
[The exported mod](#the-exported-mod) · [Command line](#command-line) · [Limits](#limits)

## Build and start

The editor is part of the repository's CMake build (vcpkg manifest feature `modkit`, enabled by every preset):

```powershell
$env:VCPKG_ROOT = 'E:\git\github\vcpkg'
cmake --preset default
cmake --build build --config RelWithDebInfo --target risen-modkit     # -> build\src\modkit\RelWithDebInfo\risen-modkit.exe
```

On Linux and macOS (only the editor and its tests are built there; the layer itself is Windows-only):

```sh
cmake --preset unix && cmake --build --preset unix-release --target risen-modkit
```

It needs OpenGL 3.3. The game itself is not needed on the machine, only a copy of its `data` folder (the editor reads it, never writes it).

## First start

Open **Window > Settings** and choose the game's **data directory**: `<Risen>\data`, the folder that holds `common\*.pak` and `compiled\*.pak`.
The editor reads the archives in under a second (templates, texts in every language, the names of meshes and icons) and remembers the folder.
Also set the **export folder**, normally `<Risen>\bin\spass\mods`.

## The window

Panels can be dragged, docked and tabbed; **Window > Reset layout** brings back the default arrangement.

| Panel | What it does |
|---|---|
| **Mod** | the mod's identity: id (folder name, a-z 0-9 _), name, version, author, description, the languages you write texts in (the first is the fallback for all others), dependencies; an overview of the content |
| **Browser** | every template of the game by group (Items, NPC, Monster, Objects, ...), search by name or label, filter by kind; the inspector shows every property of the selected template. Buttons for the editors: **New item based on this**, **Change this item** |
| **Items** | your new items, changes of shipped items and item groups (below) |
| **Problems** | what is wrong with the project (errors block the export, warnings do not); click a line to jump to the object |
| **Log** | what the editor did, and problems reading the game data |
| **Settings** | data directory, export folder, UI scale |

**File** menu: new / open / save (`*.rmk.json`, plain JSON that works well in git), **Export mod...** (`Ctrl+E`). **Edit > Undo / Redo** (`Ctrl+Z`, `Ctrl+Y`) covers every change.

## Items

* **New item**: a copy of a *base* item (choose it with the arrow next to the field, or press *New item based on this* in the Browser) with its own
  **name** (the template name: letters, digits, `_`; give it a prefix of your mod, it is the item's key in savegames), a **label** and a
  **description** per language and the numbers you want to differ from the base. Everything you do not set is taken from the base.
* **Change a shipped item**: only the values you tick are changed; the game's value is shown in brackets next to each field. A label or description
  entered here replaces the game's text (exported into `strings/*.csv`).
* **Numbers**: value (gold), damage (weapons; melee damage = multiplier * (damage + Strength)), mana cost (runes and scrolls).
* **Requirements / modifiers**: tick *Replace ...* to replace the item's list; the shipped list is copied in as the start. Requirements are what the
  player needs to equip or use the item; modifiers are what the item adds while worn or when used (`HitPoints` for a potion, `ProtEdge` for armour).
* **Kind**: use type and category (the engine's enum names).
* **Appearance**: mesh (any `.xmsh` of the game), icon (any icon a shipped item uses), material switch.
* **Other properties**: any property of the template by name (`docs/ITEM_PROPERTIES.md` lists where the numbers live); *add property* offers the
  template's own properties with their current value.
* **Item groups**: a factor on the value or damage of every item of a group path (`Items/Potions`), a name pattern (a Lua pattern) or a list.
  Factors are computed from the shipped values, so applying them twice never compounds.

## Export

**File > Export mod...** writes the mod into a folder, normally `<Risen>\bin\spass\mods\<mod id>`. The export

* is refused while the Problems panel has errors,
* writes only into an empty folder or one it wrote before (it recognises its own `modkit\project.json`), never into the game's `data` folder,
* writes only the files that changed and removes generated files that are no longer needed,
* creates `custom.lua` once for your own Lua code and never overwrites it.

Start the game: the layer loads the mod like any other (it is listed in the Mods tab of the ESC menu). With `[mods] watch = true` in the layer's
`config.toml`, a re-export while the game runs reloads the mod (texts and numbers of items; new templates need a restart).

## The exported mod

```
<mods>\<id>\
  mod.toml              generated from the Mod panel
  main.lua              generated: requires the runtime, installs the data, then requires custom.lua
  custom.lua            yours: created once, never overwritten
  data\balance.lua      the items as a risen.balance.install spec (readable, plain data)
  strings\all.csv       texts of shipped items you renamed (first language = fallback), strings\<Language>.csv for the others
  modkit\runtime.lua    the generic code, the same for every exported mod
  modkit\project.json   the editor project the mod was made from (File > Open it, or keep it for a later import)
```

The runtime applies the data with the layer's savegame-safe calls (`risen.balance.install`: on the first tick and on every session start) and emits
the event `modkit.applied` (`mod id, when, report`) after every application, so other mods and tests can react.

## Command line

```
risen-modkit [project.rmk.json] [--data DIR]                       open the editor
risen-modkit project.rmk.json --export DIR [--data DIR]            validate and export without a window (exit code 0 = exported)
risen-modkit ... --screenshot FILE [--frames N] [--panel TITLE] [--select NAME] [--size WxH]   render, save a PNG, exit (tests)
--verbose                                                          debug lines on stderr too
```

## Limits

* Only items so far; recipes, NPCs, dialogues, quests and the world view follow (REQUIREMENTS.md section 9).
* Icons are offered from the shipped items' icons; a preview of meshes and icons comes with stage E8.
* Linux and macOS builds are part of the CMake setup but were not built yet (no such host in the development setup).
