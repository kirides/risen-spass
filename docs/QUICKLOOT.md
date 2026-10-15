# Quick Loot (`mods/quickloot`)

Take what you look at with one key press: items on the ground, the belongings of dead and knocked-out humans, the contents of chests, cupboards and sarcophagi, **without walking over, kneeling,
the loot window or any animation** (the idea of the Gothic 2 mod Kwickloot). It is a normal mod of the layer (`risen-spass`), **standalone**: it needs nothing but the mod layer, no other mod, no example,
no driver. All of its policy and settings live in its Lua files; the layer only supplies generic building blocks (`risen.interact`, `risen.input`, `risen.hud`, `risen.config`, [LUA_API.md](LUA_API.md)).

## Install

1. Install the mod layer (`scripts\install.ps1`, see the main README).
2. Copy the folder `mods\quickloot` to `<Risen>\bin\spass\mods\quickloot` (`mod.toml`, `main.lua`, `policy.lua`, `config.toml`, `README.md`). Remove the folder to uninstall; the mod stores nothing in savegames.
3. Start the game. `spass.log` says `quickloot: ready (key SecondaryAction, ...)`.

## Use

Look at an item, a body or a chest (the usual focus label appears) and press the **right mouse button** (default). Hold it to take everything in reach, one target per `repeat_ms`. Nothing happens, and the
right mouse button keeps its normal meaning, when:

* a **weapon is drawn** (or a bow, crossbow, spell...: any player state other than walking around, `PS_Normal*`), the inventory, a dialogue or the loot window is open, or an interaction is running (this rule is fixed, not an option);
* the target is not one of the plain things below, or too far (`max_distance`), or empty;
* a living human is within `witness_veto` units (taking items or opening a chest in front of people is theft in the game; the quick way would skip the theft check, so it stays out: use the normal
  use key, which walks over, kneels and lets the people react).

The normal use key keeps doing the game's full interaction (walk, animation, loot window) for everything, also for what quick loot declines.

## Options (`config.toml` in the mod folder)

| Option | Default | Meaning |
|---|---|---|
| `enabled` | `true` | `false` loads nothing |
| `key` | `"SecondaryAction"` | an engine action (`SecondaryAction` = right mouse button in the default map, `PrimaryAction` = left, `Selection` = the use key, `MeleeWeapon`, ...; `risen.input.actions()` lists them) **or** a Windows virtual-key name (`RButton`, `MButton`, `V`, `F5`, `Space`, ...). An action is consumed while quick loot uses the press (the game does not also do it), a raw key is only polled |
| `modifier` | `"none"` | `shift`, `ctrl` or `alt` must be held with the key |
| `items`, `corpses`, `chests` | `true` | switch the three kinds off |
| `keys` | `true` | a locked chest is opened with the key from the inventory (never lockpicked) |
| `print` | `true` | a HUD item line for picked-up ground items (looted stacks are printed by the game's own loot function) |
| `hold_repeat`, `repeat_ms` | `true`, `80` | hold the key to repeat every `repeat_ms` (at least 20) |
| `witness_veto` | `1000` | radius in game units for the witness rule above (the game's own theft check sees items within 1000); `0` = never veto (thieving becomes undetectable) |
| `max_distance` | `450` | the target must be this close |

A value of the wrong type, an unknown key or a broken file is ignored (defaults used) and written to `spass.log` as `config: [quickloot] ...`.

## What is taken and how (the engine's own sequence without its animation)

The mod does not move items "by magic": it looks at the interaction the use key would run (`risen.interact.info`: the first player interaction whose can script accepts) and only
shortcuts the ones it knows by script name, [RE.md §31](RE.md) for the evidence. Anything else is declined.

| Target | Taken when | How | What the animated path does besides |
|---|---|---|---|
| ground item | interaction `Interact_Player_TakeItem` (no pre/post script), can script (if any) accepts | `risen.item.pick_up` = `PSInventory::InsertStack`: the same events as the animated one (`entity.destroyed`, `item.picked_up`, `inventory.added` cause `"pickup"`) | take animation, the tutorial hint `takeitem`, the pick-up sparkle, the theft assessment (hence the witness rule) |
| dead or knocked-out human | `CanInteract_Player_Loot` / `PreInteract_Player_Loot` / `Interact_Player_Loot`, and at least one stack passes the loot filter | the game's script `AutoLoot` (what it uses itself for corpses in water): exactly the stacks the loot window would list (`InvFilter_Loot`), **no** body parts, fists or hidden stacks (`risen.item.loot` takes those), HUD lines by the engine | kneeling and the window; looting bodies is no crime in the game (`AssessPlayerLooting`), so no witness rule |
| chest, cupboard, sarcophagus | plain `Chest`/`Cupboard`/`Sarcophagus` interaction without post script, something passes the loot filter | `AutoLoot` on the container; a locked chest with its key in the inventory is unlocked first the way the game's `Unlock` script does it (`gCLock_PS.Status := Unlocked`, the key stays) | walking, kneeling, the lid animation, the window, `Unlock`'s effect; the NPC theft assessment (hence the witness rule) |

**Declined (nothing happens) and why:**

| What | Why |
|---|---|
| trapped sarcophagi (`Object_Interact_Sarcophagus_Trap`), the Titanlord's sarcophagus | their interaction is another script (`Interact_Player_Sarcophagus_Trap` spawns a skeleton, the Titanlord's needs a quest and prints a message); none of it can be split from the animation |
| chests whose unlocking runs a script (`OnLockStatusChanged`, Greg's treasure chests) | the engine's setter runs the script, a property write does not (verified), so a quick unlock would skip a quest event; opened chests of that kind are taken normally |
| chests that need a lockpick, locked chests without a key | the lockpicking mini-game is an animation and a skill check; the game prints its own reasons when you use the normal key |
| buried pirate treasure (digging first), mining, doors, levers, beds, benches, stations, tombstones, books | other interaction scripts |
| living humans, monster corpses (trophies: `CanInteract_Player_Wolf`...) | a living human has `Talk` first; monsters are skinned for trophies |
| items whose interaction is not the plain take (quest/burning torch variants), items the can script refuses (e.g. in a slot) | unknown side effects |
| any target whose first interaction has an unknown, possibly chatty can script | the can script of a door prints "locked": not asked |
| empty containers | the use key shows the game's "nothing to take" |
| everything while a weapon is drawn, in menus, dialogues, interactions, ... | see Use |

Items taken by quick loot show up exactly like the animated ones to other mods: `inventory.added(owner, template, count, total, cause, other)` with cause `"pickup"` (ground) or `"script"` (corpses, chests:
`AutoLoot` is a script that transfers), `item.picked_up`, `entity.destroyed`. The mod emits `quickloot.result` (`{ target, kind, handled, reason, source }`) after each attempt on a target and listens to `risen.emit("quickloot.try", entity)`
(the same attempt on that entity; the key does it on the focused one): other mods and the tests can use both.

## How it was verified

`mods/quickloot_driver` + the `risen.test` scenario `quickloot` (`scripts\smoke-test.ps1 -Scenario quickloot`, ~2 minutes, two game processes: the default config and a second config) compare quick loot with the engine path
(`risen.interact.begin` + the loot window's own "take all" script) in the running game:

* items: exact event list of the quick pick-up equals the animated one, no `ZS_Interact_*` state for the quick one (and ~78 frames of it for the baseline);
* dead and knocked-out human looted, the club moves, fists/joint stay, same items as the loot window; living human, monster corpse, bed declined;
* plain chest looted, same inventory events as the window path; locked without key declined and stays locked; with key looted, unlocked, key kept; quest-script lock declined while locked and its script never ran; cupboard, sarcophagus looted; empty chest declined;
* trap sarcophagus declined, nothing spawned; the animated path of the same sarcophagus spawns the skeleton exactly once (so the effect happens once, by the engine);
* weapon drawn (real `PS_Melee_Loop` via the simulated middle button) declined, put away works; too far declined; a living human within the radius vetoes;
* the real key path: simulated `SecondaryAction` presses at the engine's input admin with the focus pinned: the press takes the item and is consumed (a later handler never sees it), the hold takes the next target, a press with no target is not consumed;
* quick save, take one more, quick load: counts equal;
* second process, `config.toml` with `key = "PrimaryAction"` and `items = false`: the apple is declined and its press reaches the engine, the corpse is looted by the same key and the press is consumed, `SecondaryAction` is no longer the key.

**Not driven:** a physical mouse click (the layer cannot move the OS mouse into the game; the engine's own action path is driven instead), what the HUD lines look like (screenshots of the game are black), whether the
game's use key works in the test copy (`GUI2::IsBrowsing` is true there), items in slots, the theft reaction of a human actually seeing a pick-up (the mod declines instead).

## Known limits

* `AutoLoot` always prints its own HUD lines: `print = false` only silences the lines for ground items.
* Looted stacks of corpses/chests report cause `"script"` instead of `"loot"` (the window's cause).
* The tutorial hints `takeitem` / `loot` are not dismissed by a quick take.
* The witness rule counts every living human of species `Human` in range, whether or not it could see you (no line-of-sight test).
