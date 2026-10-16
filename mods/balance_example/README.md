# balance_example: one mod that uses the whole stack

A showcase to read and copy. It re-balances the game, adds content and tells a small story, and every part is checked in a running game (`scenario showcase`, below).
It is independent of the Quick Loot mod and of every other example. **It changes the game's balance (the common sword, potions, wolves, boars, Fireball):** install it only in a game you want that in, or copy it as a starting point and change the numbers.

| File | What it shows | Guide |
|---|---|---|
| `main.lua` | the order of things: files first, then the declarative spec; the test hook | [MODDING_GUIDE.md](../../docs/MODDING_GUIDE.md) section 3 |
| `balance.lua` | **one table** (`risen.balance.install`): shipped items (`It_1H_Sword` value/damage/condition, the health potion's effect, all potions by group), shipped monsters (`Wolf` absolute, `Boar` relative), the `Fireball` spell; **new** items (a sword, a club with its own mesh and texture), a **new spell** (Strong Fireball: crystal and scroll), a **smithing recipe** (regular sword + 2 iron ore -> Slightly Improved Sword at the anvil), three NPC templates | 4.1 to 4.5 |
| `assets.lua` | the club's mesh, material and texture. A real mod ships these files in `assets/`; this one derives them from shipped files at run time (this repository has no artwork) | 4.12 |
| `story.lua` | **Brogar the Smith** (a persistent NPC; a voiced reply; the quest "Ore for the Forge": bring him 3 iron ore; a hand-in option that the *engine* offers only while the quest runs and the player carries the ore; an option that appears when the player carries the sword from the recipe), **Tilda the Apprentice** (appears when the quest has succeeded, remembered per savegame, with a day/evening/night routine), **Watchman Rolf** (behaviour written in Lua: turns to the player and warns him once) | 4.6 to 4.11 |
| `test_showcase.lua` | the live check, loaded only in a test run | 6 |

## What you will see in the game

* The common sword is dearer and stronger and needs less Strength; potions are cheaper; wolves are tougher and carry an apple; boars have twice the hit points.
* **Brogar** stands near the start (150 units from where you appear). Ask "Any work for me?", fetch 3 iron ore (find it in the world; the live check hands it over with `risen.item.give`), and say "Here are your 3 pieces of iron ore": you get the **Ember Club**.
  The hand-in option is simply not in his list until the quest runs *and* you carry the ore: no script checks it.
* **Tilda** is not there until you finish the quest. Then she appears (once, in this savegame), guards the forge by day, looks into the fire in the evening and sleeps on the ground at night.
* **Watchman Rolf** turns to you and warns you the first time you come close.
* You start with the **smithing plan** (a recipe is an item). At an anvil, with a hammer, the Smith skill, a regular sword and two iron ore you can make the **Slightly Improved Sword**; Brogar then has a new thing to say.
* The **Strong Fireball** crystal (`Spass_BX_Fireball_Strong`) and scroll (`Spass_BX_Fireball_Strong_Scroll`) exist as items but nothing hands them out: give them with `risen.item.give(player, name, 1)` or add them to a loot table (it needs the second Fireball circle, and casts for 20 mana).

## Install

Copy this folder to `<Risen>\bin\spass\mods\balance_example\`. Everything runs from the one `main.lua`; `bin\spass\generated\balance_example\` gets three small files the first time (the club's mesh, material and texture). Remove the folder to uninstall: savegames made with the mod keep
the new items dormant until it is back (nothing crashes), and the NPCs you met stay in the world as ordinary nameless NPCs.

## Make it yours

1. Copy the folder, change `id` in `mod.toml` and the prefixes `Spass_BX_` / `BX_` / `Q_BX_` (every template, NPC, quest and dialogue name must be unique in the game; IDs derive from the template *names*).
2. Change the numbers in `balance.lua` (the property tables in [../../docs/ITEM_PROPERTIES.md](../../docs/ITEM_PROPERTIES.md), `NPC_PROPERTIES.md`, `SPELL_PROPERTIES.md`, `RECIPE_PROPERTIES.md` say what exists); change the story in `story.lua`.
3. Delete what you do not need. Each of the three files works alone (the story uses the club and the sword from `balance.lua` as its reward and its condition).

## Test it

```powershell
.\scripts\smoke-test.ps1 -Scenario showcase                       # two game processes, about 4 minutes, in the isolated windowed test copy
.\scripts\test-run.ps1 -Mods balance_example -WaitForResult -Seconds 400 -Scenario showcase -Param phase=play -ShowChecks
```

`phase=play` starts a new game and checks, in order: the installed numbers (items, potion, monsters, spell, new items and spell), the club's loose files (found, loose, loaded by the engine), the recipe, Brogar (template, label, the voiced line registered), the dialogue
conditions (before the quest, while it runs without ore, with two ore, with three ore), the hand-in (ore taken, club given, quest done), Tilda (waited, then stands there, keeps her post by day), Rolf (in his Lua state, warns once), crafting, the sword option; then a quick save, a quick load in the same
process (nobody made twice, Tilda the same entity, the rule and the quest remembered, Rolf back in his state) and a second save. `phase=load` loads that savegame in a **fresh process** and checks that everything resolves again from the savegame.
