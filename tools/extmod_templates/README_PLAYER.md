# Risen Extended Mod, re-implemented for the risen-spass layer: how to play it

**What this is.** The German "Risen Extended Mod" (new weapons, armor and rings, smithing chains, changed numbers, new monster skins, darker caves, a melee system with
one-hand / two-hand / staff styles, trader stock per chapter, higher teaching limits, XP for pickpocket lessons ...) ships as old-style `.p00` / `.p01` / `.p02` archive
volumes for the 32-bit game. This repository does **not** contain the mod. It contains a converter, `risen-pack2mod`, that reads *your own copy* of the mod's archives and
your own game, and writes a normal mod for the risen-spass layer (Risen Enhanced, x64): loose resource files, generated Lua data and string tables. The result can be
switched on and off in the game's *Mods* page, stacks with other layer mods, and works with savegames (items are created from their names at every start).
The converted mod is the work of the mod's author: it stays on your machine, do not publish it.

## What you need

* Risen Enhanced (the 64-bit Steam version) - the **game directory** `<Risen>` (contains `bin\Risen.exe` and `data\`).
* The Risen Extended Mod as you got it: a folder with `common\*.p00 / .p01 / .p02`, `compiled\*.p01 / .p02`, ... (only read, never changed).
* This release: `sentry.dll`, `install.ps1`, `risen-pack2mod.exe`, `pack2mod-profiles\risen_extended.json`, the layer's own mods.

## Install (about 2 minutes)

```powershell
# 1. the layer (a proxy for bin\sentry.dll; the original is kept as sentry_orig.dll; undo with uninstall.ps1)
.\install.ps1 -GameDir 'C:\...\Risen'

# 2. convert the mod into a layer mod (7 seconds; 100 MB of loose files)
.\risen-pack2mod.exe 'D:\Mods\Risen_Extended_Mod' `
    --baseline 'C:\...\Risen\data' `
    --out 'C:\...\Risen\bin\spass\mods\risen_extended' `
    --profile .\pack2mod-profiles\risen_extended.json
```

Start the game and begin a **new game** (the chests and the first chapter's trader stock are filled once per savegame; the numbers of items are applied at every start). The ESC menu has a *Mods* page (`modmanager`): every layer mod can be switched off there (restart).
`bin\spass\mods\risen_extended\report.md` lists what was converted and, more important, what was **not** (see below). Run the converter again after you changed the mod
or updated the layer; `config.lua` of the mod folder is kept.

## Options

* `bin\spass\mods\risen_extended\config.lua`: `starter_kit` (`"none"` default, `"plans"` hands out the smithing plans, `"all"` one of every new item to look at them),
  `balance`, `new_items`, `dialogue`, `world`, `teachers` (each can be switched off; start the game again).
* The groups that are files (the engine reads them once at start): `combat` (the melee table of the changelog, the classic one, or the game's own), `wind`, `caves`,
  `vegetation`, `skins`: `scripts\extmod-features.ps1 -ModDir <...>\mods\risen_extended -Set combat=off,caves=off` (restart the game).
* `--combat changelog|shipped|off` of the converter chooses which of the mod's two melee tables becomes the default.

## What is in it, and how far each part was checked

Every item of the author's changelog, with its status (verified live in the running game, generated and loading, partly, or missing), is in
`docs/EXTENDED_MOD_ANALYSIS.md`. In short: the changed numbers of 200 shipped items and the 132 new items (weapons incl. the Uriziel sword that the Titan dungeon places, the
equippable pickaxe and ogre club, armors, helmets, shields, rings, amulets, books, smithing stages), their recipes, trader stock of all four chapters (checked
entry by entry), 12 chests and 3 equipped NPC items, teaching limits, the melee table, the lizard / female / armor skins, darker caves and the wind are applied and were
looked at or read back in the running game. XP for the pickpocket lessons is added to the lessons' dialogue infos; the engine pays an added `GiveXP` (verified in a
conversation), the lessons themselves run the interactive pickpocket game and were not played to the end.

## Known gaps

* 24 new dialogue infos, 32 changed existing ones (mostly the classic-vs-Enhanced noise of the mod's base), the mod's own `.dll` / script changes and
  `compiled_meshes.bin` are not converted (listed in `report.md`).
* The combat table is the mod's, but how the melee system *feels* was not judged: switch it off with `combat=off` if you prefer the game's own.
* `The One Ring` exists but is placed nowhere by the mod's world layers.
* If something looks wrong, `bin\spass\spass.log` (and `crash.log` after a crash) say what the layer did; the mod's entries are applied one by one, a bad entry costs only itself.

Documentation: `docs/PACK2MOD.md` (the converter), `docs/EXTENDED_MOD_ANALYSIS.md` (what the mod is made of, status), `docs/RE.md` (how the engine side works).
