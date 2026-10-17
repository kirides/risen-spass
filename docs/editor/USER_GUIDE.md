# risen-modkit user guide

`risen-modkit` builds mods for the Risen modding layer without writing Lua: you create and change items (later recipes, NPCs, dialogues, quests and
world placements) in a desktop application and export an ordinary mod folder that the layer loads. The plan and the state of the stages are in
[REQUIREMENTS.md](REQUIREMENTS.md); this guide describes what works now.

Contents: [Build and start](#build-and-start) · [First start](#first-start) · [The window](#the-window) · [Items](#items) · [Recipes](#recipes) · [NPCs and monsters](#npcs-and-monsters) · [Dialogues](#dialogues) · [Quests](#quests) · [Export](#export) ·
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
| **Recipes** | new crafting recipes and changes of shipped ones (below) |
| **NPCs** | NPC and monster templates, and placed NPCs (below) |
| **Dialogues** | conversations as node graphs (below) |
| **Quests** | quests and their flow as node graphs (below) |
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

## Recipes

A recipe is an **item** (the plan, a "recipe item"): a character knows the recipe while the plan is in his inventory, and the station lists the
plans of its kind.

* **New recipe**: the plan's template name, the **station** (anvil, forge, grindstone, coolweapon, alchemy, goldsmith, scroll, cooking, frying),
  the **result item** (a shipped item or one of your new items), 1 to 16 **ingredients** with their counts, the **skills** the crafter needs
  (`Smith 1`, `Alchemy 2`, ...), and the plan's label and description (empty label: "Recipe: <result>"). **learn** gives the plan to the player once
  per savegame; without it, hand the plan out yourself (an NPC's inventory or a dialogue, coming stages). The engine makes one result item per craft.
* **Change a shipped recipe**: station, ingredients (tick *Replace ingredients*: the shipped list is copied in), skills and result; the shipped
  recipe is shown above. The smithing chain of the game is a sequence of recipe items (raw blade at the forge, hot blank at the anvil, ...,
  see `docs/RECIPE_PROPERTIES.md`).
* The Browser offers **New recipe for this item** on an item and **Change this recipe** on a recipe.

## NPCs and monsters

Two kinds of objects:

* **Templates** are kinds of NPCs and monsters. A **new template** is a clone of a base (a peasant, a wolf, ...) with its own name, label and
  numbers; a **change** of a shipped template rewrites it (new spawns at once, the ones already in the world at the next session start). The shipped
  values are shown in brackets: hit points, level, experience for a kill (10 x level, at least 50: setting it sets the level), mana, the skills
  (attributes 0..200, protection, combat skills 0..10), guild, species, attitude, bravery, damage calculation, combat species, AI group, the actor of a
  monster. Factors (hit points, damage) are computed from the shipped values. **Loot** sets target amounts of items the template carries (reaches NPCs
  made afterwards). A human clone looks like its base: heads and body parts are separate items.
* **Placed NPCs** are persons in the world: a **unique name** (letters, digits, `_`, `-`: dialogues name their owner by it, savegames know the NPC by
  it), a template (shipped or one of yours), a label per language, **where** (an absolute position, or an offset from the player when the NPC is first
  made in a savegame: handy until the World view can place him), peaceful (a citizen nobody attacks) or a guild, level and hit points, an
  **inventory** (tick *equip* for what he wears or holds when he is made) and a **routine**: one post, a day / evening / night point, or a patrol;
  points are relative to his home and have a kind (guard, lookat, sleep, ...) and a facing. *Version* replaces the NPC in existing savegames when
  raised; *respawn* brings a dead one back at the next start.

The export declares every placed NPC with `risen.npc.persistent`: made in a new game, found again after a load, never made twice.

## Dialogues

A dialogue is the conversation of one **owner** NPC (a placed NPC of the project, or the unique name of an NPC in the world) with the player, drawn as
a graph on a canvas: scroll with the right mouse button, zoom with the wheel, **right-click** the background for a new node, **drag from a pin into
empty space** for a new node that is already linked, select and press **Delete** to remove. The selected node's properties are on the right.

| Node | Pins | What it is |
|---|---|---|
| **Option** | in: `parent`, `when`; out: `then`, `choices` | what the player says (one text per language). `then` leads to the lines and commands that follow; `choices` to sub-options shown after this one ran (the engine's sub-menu); `when` takes conditions (several = all). Type (Info, Important, ...), permanent (stays after use), info name (the option's key in savegames) |
| **Line** | in, `next` | a spoken line: the NPC, the player or another NPC (unique name) |
| **Command** | in, `next` | an engine dialogue command with its parameters: Give, GiveXP, SetGameEvent, DoTrade, ... (the parameter list comes from the engine) |
| **Quest** | in, `next` | starts, succeeds, fails or cancels a quest of the project |
| **Log entry** | in, `next` | a line in a quest's log |
| **Script** | in, `next` | calls a script (yours in `custom.lua`, or a game script) |
| **End** | in | ends the conversation |
| **Condition** | out | quest state, items carried, a game variable or event, a skill, a guild, an NPC dead / alive |
| **All of / Any of / Not** | in, out | combine conditions |
| **Comment** | | a note on the canvas |

Links only join pins that fit (the pin shapes and colours tell: triangles for the sequence, blue circles for options, teal for conditions), and the
option tree and the conditions cannot loop. A sequence that does not end with **End** returns to the list of options; an option without
sub-options gets an End by itself (an open conversation blocks saving). The inspector of an option shows the Lua the export writes for it; nodes with a
problem have a red border and the problems are listed under the properties and in the Problems panel.

The quest conditions and item amounts become conditions the engine checks itself; any, not, values of variables and guilds are one Lua condition
behind them (`risen.cond`, see `docs/LUA_API.md`).

## Quests

A quest has a **name** (its id, the key of its state in savegames), a **title** per language (the quest log shows the first language's; the layer
takes one title), a **type** (Report, Kill, HasItems, ...), the **experience** the engine pays on success and *silent* (no "quest started"
notification). Its **flow** is a chain on the canvas:

| Node | What it does |
|---|---|
| **Start** | how the quest begins: `dialog` (a dialogue's Quest node with *start*), `game_start` (every new game), `item` (the player gets an item), `kill` (a template or a named NPC dies), `var` (a game variable reaches a value), `quest` (another quest reaches a status: quest chains) |
| **Objective** | waits until it is done: `items` (the player carries N of an item), `kill` (N deaths of a template or a named NPC, counted from the moment the objective begins), `talk` (a dialogue option finished playing), `var`, `quest`; may add a log entry when it begins |
| **Log entry** | a line in the quest log |
| **Reward** | experience (through the game's own GiveXP), gold, items, a game variable |
| **Outcome** | success, failure or cancelled, a log line, and a follow-up quest to start |

Log entries, rewards and the outcome happen the moment the flow reaches them; objectives hold it until the player did them. The progress is part of
every savegame (a load rolls it back with the game), a reward is never paid twice, a quest with a flow is started once per savegame. A quest without a
Start node is started, succeeded and failed by its dialogues alone (their Quest nodes). The inspector shows the exported flow.

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
  data\balance.lua      items, recipes, NPC and monster templates as a risen.balance.install spec (readable, plain data)
  data\npcs.lua         the placed NPCs (risen.npc.persistent declarations)
  data\quests.lua       the quests (risen.quest.define)
  data\dialogs.lua      the dialogue options (risen.dialog.create, parents first) and the quest actions their lines call
  strings\all.csv       texts of shipped items you renamed (first language = fallback), strings\<Language>.csv for the others
  modkit\runtime.lua    the generic code, the same for every exported mod
  modkit\project.json   the editor project the mod was made from (File > Open it, or keep it for a later import)
```

The runtime applies the data with the layer's savegame-safe calls (`risen.balance.install`: on the first tick and on every session start) and emits
the event `modkit.applied` (`mod id, when, report`) after every application, and `modkit.npc_ready` (`mod id, NPC name, reason`) whenever a
placed NPC is in the world, and `modkit.quest_action` (`mod id, quest, action`) after a dialogue line started, succeeded, failed or
cancelled a quest, and `modkit.quest_started` / `modkit.objective` / `modkit.quest_done` for quest flows, so other mods and tests can react. Quests and dialogue options are made on `load.before` and on every session start (the engine
restores their saved state only into objects that exist).

## Command line

```
risen-modkit [project.rmk.json] [--data DIR]                       open the editor
risen-modkit project.rmk.json --export DIR [--data DIR]            validate and export without a window (exit code 0 = exported)
risen-modkit ... --screenshot FILE [--frames N] [--panel TITLE] [--select NAME] [--size WxH]   render, save a PNG, exit (tests)
--verbose                                                          debug lines on stderr too
```

## Limits

* Items, recipes, NPCs, dialogues and quests so far; the world view follows (REQUIREMENTS.md section 9).
* A quest flow is one chain (no branches inside a quest); branch with dialogues and follow-up quests.
* A dialogue cannot be played through inside the editor yet (DLG-5); test it in the game.
* A template's shipped loot is not shown (the editor cannot read it offline yet).
* Icons are offered from the shipped items' icons; a preview of meshes and icons comes with stage E8.
* Linux and macOS builds are part of the CMake setup but were not built yet (no such host in the development setup).
