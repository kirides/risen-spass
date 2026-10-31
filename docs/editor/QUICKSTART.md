# risen-modkit quick start

`risen-modkit` makes mods for **Risen Enhanced (x64)** without writing Lua: items, recipes, NPCs, dialogues, quests, spells, placements in the 3D world, voice lines.
It exports an ordinary mod folder that the **risen-spass** modding layer loads.

## What is in this zip

| | |
|---|---|
| `risen-modkit.exe` | the editor (start this) |
| `examples\` | finished example projects: open one and look around (below) |
| `lang\` | the editor's language files (English is built in, German is here); keep the folder next to the exe |
| `risen-pack2mod.exe` | turns a Risen mod made as `.p00` / `.pak` archives (e.g. the Extended Mod) into a project or a layer mod, see `docs\PACK2MOD.md` in the repository |
| `PakView\PakView.exe` | looks into the game's `.pak` / `.pNN` archives |

## 1. Install the layer (once)

The editor needs the game's data to read; **Play** and the mods need the layer. Download `risen-spass.zip` from the same release and follow its notes:
rename `<Risen>\bin\sentry.dll` to `sentry_orig.dll`, copy the new `sentry.dll` to `<Risen>\bin` and the mods to `<Risen>\bin\spass\mods`.
(`<Risen>` is the Risen Enhanced folder, the one with `bin\Risen.exe`. Do not use the 32-bit Classic edition.)

## 2. Tell the editor where the game is (once)

Start `risen-modkit.exe`, open **Window > Settings** and set

* **Game data**: `<Risen>\data` (the folder with `common\*.pak` and `compiled\*.pak`). The editor only reads it. Press *Load game data*: 2664 templates, all texts and the world appear in a couple of seconds.
* **Export**: `<Risen>\bin\spass\mods` (where exported mods go).
* **Play**: the `<Risen>` folder. *Play* then exports your mod and starts the game (tick *Windowed* if you do not want a fullscreen game).

## 3. Look at an example

**File > Open project...** and choose `examples\showcase.rmk.json`: a small mod with a new item, a recipe, three NPCs, a dialogue and a quest. Click through the tabs (Items, Recipes, NPCs, Dialogues, Quests, World).
`examples\chronicle.rmk.json` is the big one: a story over all four chapters made with every feature of the editor.
The other examples show one feature each: `spells`, `events` (triggers and conditions), `voice`, `grass`, `water`, `looks`, `extras`, `extensions`.

## 4. Make your first mod

1. **File > New project**, then the **Mod** panel: an id (folder name, `a-z 0-9 _`), a name, a version and an author.
2. **Browser**: pick a shipped item (say a sword), press *New item based on this* and change its numbers in **Items**.
3. **World**: place an NPC on the ground (Place NPC, then click in the view), give him a day routine in **NPCs** (work, relax and sleep places; a warning tells you when a place is off the walkable ground).
4. Look at **Problems**: the editor checks the project before every export.
5. **Play** (or **File > Export mod...**): the game starts with your mod. While the game runs, Play again reloads the mod.
6. The game's own errors for your mod show up in **Problems** too.

## 5. Share it

**File > Package for sharing...** makes `<id>-<version>.zip`: players unpack it into `<Risen>\bin\spass\mods` and start the game. The dialog also checks whether your mod
changes things another installed mod changes too.

## Where to read more

* `docs\editor\USER_GUIDE.md` in the repository: every panel in detail (the screenshot tour is `docs\Modding_With_ModKit.md`).
* `docs\MODDING_GUIDE.md`: the game's side of modding: what a mod can change, the event model, limits.
* The editor can be switched to German: **Settings > Interface > Language**.
* Problems, ideas, missing features: the issue tracker of the repository.
