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
* the target is not one of the plain things below, or too far (`max_distance`), or empty.

**Witnesses.** Taking an item or opening a chest or cupboard inside a watched place (a house, a camp: an area with a security level) is theft to the game's NPCs; on the open beach it is not, and neither are
bodies and sarcophagi. The animated take asks the NPCs with the game's own function (`AssessTheft` for every NPC of the world; the NPC must be within 700 units, see you, be human and not in your party, and
the place must be watched; then he comments, watches, threatens, [RE.md §40](RE.md)). Quick loot calls **the same function at the same moment** (before the first thing is taken, as the animated take does),
so a witness reacts exactly as to the animated take (`witness_mode = "assess"`, the default). The alternatives are `"veto"` (nothing is taken while a human who can see you is within `witness_veto` units; the use
key still walks over and lets people react) and `"ignore"` (nobody reacts: thieving is undetectable). Another mod can make NPCs ignore thefts with `risen.ai.perceive("theft", fn, { all = true })`
answering 0 (`mods/example_ignore_theft`): quick loot honours it like the animated take does.

The normal use key keeps doing the game's full interaction (walk, animation, loot window) for everything, also for what quick loot declines.

## Options (in the game, or `config.toml` in the mod folder)

**In the game:** ESC > **Mods** > Quick Loot > **Options (15)**: every option below (but the free key names) is a row with a toggle, drop-down or slider, and a change **applies at once**, no restart
(the key and modifier are worked out again, `enabled = false` leaves the mod loaded but idle, ...). The menu stores what differs from the defaults in `<Risen>\bin\spass\options\quickloot.toml`;
the mod reads that file on top of `config.toml` at the next start (delete it to go back to `config.toml`). The key list of the menu offers the three actions and a few keys; any other key name
(`F7`, `Space`, ...) is set in `config.toml` and then shown as it is. How the page and the declaration work: [LUA_API.md](LUA_API.md) "Options menu", scenario `options`. The texts of the page are in `strings/all.csv` (English) and `strings/German.csv` and follow the game's text language (Settings > Game options > Text language > Apply, no restart).

The file `config.toml` (the defaults, edited by hand):

| Option | Default | Meaning |
|---|---|---|
| `enabled` | `true` | `false` loads nothing |
| `key` | `"SecondaryAction"` | an engine action (`SecondaryAction` = right mouse button in the default map, `PrimaryAction` = left, `Selection` = the use key, `MeleeWeapon`, ...; `risen.input.actions()` lists them) **or** a Windows virtual-key name (`RButton`, `MButton`, `V`, `F5`, `Space`, ...). An action is consumed while quick loot uses the press (the game does not also do it), a raw key is only polled |
| `modifier` | `"none"` | `shift`, `ctrl` or `alt` must be held with the key |
| `items`, `corpses`, `chests` | `true` | switch the three kinds off |
| `keys` | `true` | a locked chest whose own key (`gCLock_PS.Key`) is in the inventory is opened with it; `false` declines every locked chest. Quick loot never lockpicks and never skips the mini-game, whatever this says |
| `print` | `true` | a HUD item line for picked-up ground items (looted stacks are printed by the game's own loot function) |
| `pickup_sound` | `true` | the sound of the take animation for **ground items**: the pick-up effect `eff_item_<material>_transfer_01` on the player (`risen.effect.start`), which plays the item material's transfer sound `sfx_items_<material>_transfer_NN` (apple, gold, wood, plant, ring, potion, flesh, keys...). Not for bodies and chests: the loot window plays no such sound (see below) |
| `pickup_sound_gap` | `600` | milliseconds: after one pick-up sound the next waits this long. Holding the key takes about 12 items a second and every sound plays to its end, so without a gap they pile up into seconds of noise (the animated take does one at a time); `0` = a sound for every item |
| `hold_repeat`, `repeat_ms` | `true`, `80` | hold the key to repeat every `repeat_ms` (at least 20) |
| `witness_mode` | `"assess"` | what happens to the people who could see a theft: `"assess"` = the game's own reaction (see Witnesses), `"veto"` = decline while a living human who can see you (field of view and a clear line, `risen.world.can_see`) is within `witness_veto` units, `"ignore"` = nobody reacts. An unknown word is ignored (`assess`); a mod layer without `risen.ai.broadcast` falls back to `"veto"` |
| `witness_veto` | `1000` | `"veto"` mode only: the radius in game units; `0` = never veto |
| `max_distance` | `450` | the target must be this close |

A value of the wrong type, an unknown key or a broken file is ignored (defaults used) and written to `spass.log` as `config: [quickloot] ...`.

## What is taken and how (the engine's own sequence without its animation)

The mod does not move items "by magic": it looks at the interaction the use key would run (`risen.interact.info`: the first player interaction whose can script accepts) and only
shortcuts the ones it knows by script name, [RE.md §31](RE.md) for the evidence. Anything else is declined.

| Target | Taken when | How | What the animated path does besides |
|---|---|---|---|
| ground item | interaction `Interact_Player_TakeItem` (no pre/post script), can script (if any) accepts | `risen.item.pick_up` = `PSInventory::InsertStack`: the same events as the animated one (`entity.destroyed`, `item.picked_up`, `inventory.added` cause `"pickup"`) | take animation, the pick-up sparkle `eff_item_<EffectMaterial>` (the theft assessment is done by quick loot too; the tutorial hint `takeitem` is ended by quick loot as well) |
| dead or knocked-out human, **monster or animal carcass**, washed-up body of the beach | `CanInteract_Player_Loot` (monsters: the can script of their species, `CanInteract_Player_Wolf`, `_Boar`, `_Gnome`...) / `PreInteract_Player_Loot` (the beach bodies: `PreInteract_Player_Loot_Comment`) / `Interact_Player_Loot`, and at least one stack passes the loot filter | the game's script `AutoLoot` (what it uses itself for corpses in water): exactly the stacks the loot window would list (`InvFilter_Loot`), **no** body parts, fists or hidden stacks (`risen.item.loot` takes those), HUD lines by the engine | kneeling and the window; looting bodies is no crime in the game (`AssessPlayerLooting`), so no witness rule. The species can script of a monster puts the trophies into the corpse the first time it is asked (`It_Teeth`, `It_Meat_Raw`, fur only with the Trophy skill; the engine's own rule, the animated path asks the same script), so they are taken with the rest; the beach bodies' pre script makes the hero **say** the standard voice message `GamestartLoot` (`risen.voice.say_svm`, not again while he is still speaking; a game-log line with the text when the text language is not the voice language, as the game's helper does) |
| chest, cupboard, sarcophagus | plain `Chest`/`Cupboard`/`Sarcophagus` interaction without post script, something passes the loot filter | `AutoLoot` on the container; a locked chest with its key in the inventory is unlocked first the way the game's `Unlock` script does it (`gCLock_PS.Status := Unlocked`, the key stays) | walking, kneeling, the lid animation, the window, `Unlock`'s effect (the NPC theft assessment is done by quick loot too; sarcophagi have none in the animated path either) |

**Declined (nothing happens) and why:**

| What | Why |
|---|---|
| trapped sarcophagi (`Object_Interact_Sarcophagus_Trap`), the Titanlord's sarcophagus | their interaction is another script (`Interact_Player_Sarcophagus_Trap` spawns a skeleton, the Titanlord's needs a quest and prints a message); none of it can be split from the animation |
| chests whose unlocking runs a script (`OnLockStatusChanged`, Greg's treasure chests) | the engine's setter runs the script, a property write does not (verified), so a quick unlock would skip a quest event; opened chests of that kind are taken normally |
| every other locked chest: one that needs a lockpick (also with lockpicks and the skill), one with another key than yours, a `Difficulty` 0 lock without a key (the game's `Unlock` opens that one freely, quick loot does not), a lock whose state cannot be read | the lockpicking mini-game is an animation and a skill check and stays the game's: the normal use key runs it (or prints the game's reasons). The lock is screened before the can script, so quick loot prints nothing |
| buried pirate treasure (digging first), mining, doors, levers, beds, benches, stations, tombstones, books | other interaction scripts |
| living humans, living monsters | a living human has `Talk` first; a living monster's can script refuses (AI mode not Down/Dead) |
| bodies with a special loot script (`CanInteract_Player_Loot_Sara`) | unknown script |
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
* dead and knocked-out human looted, the club moves, fists/joint stay, same items as the loot window; wolf, boar and gnome carcasses looted (the trophy moves, the monster fist stays, same items as the loot window), the same six species with the Trophy skill and recipes (fur, tusk, claws, sting appear exactly then) and a boar against the loot window; a washed-up beach body (`Beach_Seaman_Dead`) looted like the window does and the hero says his `GamestartLoot` line; living human, bed declined;
* plain chest looted, same inventory events as the window path; locked without key declined and stays locked; a lockpick lock declined while the player carries lockpicks and has the skill (still locked, nothing taken, no lockpick used), a `Difficulty` 0 lock without key and a lock of another key declined; with key looted, unlocked, key kept; quest-script lock declined while locked and its script never ran; cupboard, sarcophagus looted; empty chest declined;
* trap sarcophagus declined, nothing spawned; the animated path of the same sarcophagus spawns the skeleton exactly once (so the effect happens once, by the engine);
* weapon drawn (real `PS_Melee_Loop` via the simulated middle button) declined, put away works; too far declined; a living human nearby does not stop an item pick-up outside a watched area (assess mode);
* the real key path: simulated `SecondaryAction` presses at the engine's input admin with the focus pinned: the press takes the item and is consumed (a later handler never sees it), the hold takes the next target, a press with no target is not consumed;
* quick save, take one more, quick load: counts equal;
* second process, `config.toml` with `key = "PrimaryAction"`, `items = false` and `witness_mode = "veto"`: the apple is declined and its press reaches the engine, the corpse is looted by the same key and the press is consumed, `SecondaryAction` is no longer the key; a human who looks at the player stops the chest looting (nothing taken, nobody asked), one who looks away does not.

The NPC reactions have their own scenario, `quickloot_react` (`scripts\smoke-test.ps1 -Scenario quickloot_react`, ~2.5 minutes, two game processes), which makes the beach a watched area (an `AreaZone_Default` with the area name `SL_Public`,
`Entity:set_area_names`) and puts a Peasant 300 units in front of the player:

* an item and a chest taken by the animated path and by quick loot give the **same end state** for the witness (`ZS_Observe`, AI mode `Observe`, comment `Theft`, reason `Theft`), the same `AssessTheft(Peasant, target)` calls (recorded before the take), a cupboard reacts too;
* sarcophagi (both paths), dead bodies (both paths) and a declined (locked) chest ask nobody and cause no reaction; outside a watched area, with the witness behind the player or 900 units away, with nobody around, and for an item the player dropped himself, the NPCs are asked and nobody reacts;
* second process with `witness_mode = "ignore"`: nobody is asked, nobody reacts.

`mods/example_ignore_theft` (`ex_ignore_theft`) proves the suppression: with a game variable set a `risen.ai.perceive("theft")` handler answers 0 and neither the animated take nor quick loot makes the witness react; without it he does.

**Not driven:** a physical mouse click (the layer cannot move the OS mouse into the game; the engine's own action path is driven instead), what the HUD lines look like (screenshots of the game are black), whether the
game's use key works in the test copy (`GUI2::IsBrowsing` is true there), items in slots, what a witness says and does beyond the state he is put in (black screenshots, no audio check).

## Sounds of the animated paths (trace channel `sound`)

Measured with `-Trace sound` (every GUI sound, effect start and named sound the engine plays): the animated **take** starts the effect `eff_item_<EffectMaterial>_transfer_01` on the player, which plays `sfx_items_<material>_transfer_NN` (`pickup_sound` does the same for quick loot). **Looting bodies and chests** plays no item sound at all when the items move: the loot window's "Take all" button only makes the generic UI click (`sfx_ui_button_mousedown_01` / `_mouseup_01`, played by the GUI when the mouse button is pressed, not by the script), the chest's own sounds belong to its animation (`sfxadd_interact_chest_open_01` / `_close_01`, `sfx_interact_padlock_open_01` from the unlock effect). Quick loot therefore plays nothing for those.

## Known limits

* `AutoLoot` always prints its own HUD lines: `print = false` only silences the lines for ground items.
* Looted stacks of corpses/chests report cause `"script"` instead of `"loot"` (the window's cause).
* The tutorial hints `takeitem` / `loot` are ended with `risen.hud.tutorial_stop` like the game's scripts do (`Tutorial::StopEvent`); `Tutorial::IsCompleted` does not change in the test copy even after the animated take, so the effect on the screen was not verified. * **Trophies follow the skill**, because quick loot asks the very same species can script as the animated path: teeth, meat and some claws always; fur only with the Trophy skill; tusk, horn, claw, sting and wings also with the matching `It_Recipe_*` item in the inventory (verified for wolf, boar, rotworm, giant scorpion, ghul, ash beast with and without the skill/recipes, and against the loot window). The game generates a carcass's trophies once (`IsGeneratedPlunder`), the first time anything asks the can script (also the focus system looking at it), with the skills of that moment: a skill learned later does not add fur to a carcass that was already asked, in the animated path either.
* The reaction is the game's: it only exists inside watched areas (the open beach is none; real houses and camps were not walked into, the area of the tests is made up) and only `SL_Public` was tried as the area's name. An area with an owner group (the NPC would record a *crime*, not just a comment) was only read in the decompilation, not driven.
* The animated chest asks the NPCs several times while you stand at it (the game's looting check runs once a second); quick loot asks once. A witness's reaction is the same.
* There is no noise-based perception in the game: opening a chest or taking an item makes no sound that an NPC reacts to ([RE.md §40](RE.md)).
* `veto` mode counts living humans of species `Human` within the radius who can see the player (`risen.world.can_see`: 180 degree field of view, a clear line); it does not know the area, so it also vetoes on the open beach.
