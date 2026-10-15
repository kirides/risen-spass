# Example mods

Twelve small, self-contained mods under `mods/example_*`: each is the simplest realistic form of one thing a modder wants, written to be copied
(`main.lua` with a header that says what you learn, `mod.toml`, and a `test_*.lua` that is loaded **only** in a test run). Every one of them has a
`risen.test` scenario that was **run live in the game**; nothing in this table is claimed that was not. The scenarios are registered in
`scripts\smoke-test.ps1` (`ex_*`), so the regression suite covers them.

Run one (about a minute; use your own test copy, never the real install, always windowed, see [DEVELOPMENT.md](DEVELOPMENT.md)):

```powershell
.\scripts\test-run.ps1 -Instance 2 -Mods example_kill_quest -WaitForResult -Seconds 300 -Scenario example_kill_quest -ShowChecks
.\scripts\smoke-test.ps1 -Scenario ex_template_patch,ex_balance,ex_custom_npc,ex_fetch_turnin,ex_fetch_pickup,ex_kill_quest,ex_quest_rewards,ex_quest_tracker,ex_material,ex_voice,ex_spell,ex_recipe -Parallel 3
```

## Index

| # | Example | Shows | Key APIs | Smoke scenario / time | Verified (2026-10-15) |
|---|---|---|---|---|---|
| 1 | `example_template_patch` | edit existing item and NPC templates: gold value, weapon damage, a guild; first `tick` + every `session.start` | `risen.template.patch(name, sets, { instances })` | `ex_template_patch`, 45 s | template read-back, new spawn, inventory stack, all 30 live world apples, NPC spawn (8 checks) |
| 2 | `example_balance` | declarative balancing of items (value, damage, requirements, modifiers), NPCs/monsters (hp, level, xp, skills, bravery, loot, relative `hp_mult`/`level_add`), groups by pattern/template group | `risen.balance.install` (`items`, `npcs`, `npc_groups`, `item_groups`), `risen.item.can_use` | `ex_balance`, 35 s | numbers on templates, spawns, loot, 98 live world wolves rewritten (11 checks) |
| 3 | `example_material` | (A) other material variant of an existing mesh: `MaterialSwitch` on the health potion, a new black wolf; (B) a texture file that **shadows** a shipped one (red/blue swapped sword blades, written with `risen.assets.write`) | `risen.template.patch/create(material=)`, `risen.assets.read/write/info/resource` | `ex_material`, 40 s | switch values equal the shipped `It_Po_Mana_01` / `Wolf_Black`; the engine resolved and loaded (resource state 3) the loose file; shadow removed again (11 checks). **Not** how it looks: screenshots of the game window are black |
| 4 | `example_custom_npc` | a new NPC template (`Spass_Hob`) and reward item (`Spass_Hob_Knife`), the NPC placed at the start, three dialogue options, a quest started by one option and finished by another that is only offered while it runs | `risen.balance.install{new_npcs,new_items}`, `risen.npc.spawn`, `risen.dialog.create`, `risen.script.define`, `risen.quest.define/succeed` | `ex_custom_npc`, 65 s | NPC uses the new template and label, quest runs, option offered only while running, quest succeeds, reward item of the new template (12 checks) |
| 5a | `example_fetch_turnin` | fetch quest: the NPC asks for 3 berries, a permanent hand-in option checks the inventory (too few: log line, nothing taken; enough: items taken, gold paid, quest succeeds) | `risen.item.count/take/give/money`, `condition = "Running"`, `permanent = true` | `ex_fetch_turnin`, 75 s | both hand-ins incl. the failed one (9 checks) |
| 5b | `example_fetch_pickup` | fetch quest that completes **by itself** when the player carries 3 more berries than at the start (baseline in `risen.save`), progress log lines, berries scattered around the NPC, reward, items kept | `inventory.changed` event, `risen.save` | `ex_fetch_pickup`, 60 s | progress and completion through the event; the items arrive through `risen.item.give` (see Gaps: a script cannot pick up for the player; `risen.item.pick_up` exists for tests) (10 checks) |
| 6 | `example_kill_quest` | kill 3 `Wolf` (counter in `risen.save`, log "Wolves: 2/3", duplicate death counted once, a clone of Wolf does not count), then a `Success`-conditioned option unlocks the boss quest: kill one named entity (`EX_DireWolf`, template `Spass_Dire_Wolf`) | `entity.died(entity, killer, cause)`, `Entity:template():name()`, `Entity:name()` | `ex_kill_quest`, 75 s | counting, negative cases, unlock, boss spawn and kill (12 checks); kills are driven with the game's `Kill` script (see Gaps) |
| 7 | `example_quest_rewards` | what happens when a quest completes: gold, a spawned guard, a game variable, an unlocked option; **idempotent after save/load** | `quest.status_changed`, `risen.save`, `risen.gamevar`, `risen.world.quick_save/quick_load`, `condition = "Success"` | `ex_quest_rewards`, 100 s | trigger ran once, gold paid once across a real quick save + quick load, guard respawned at the same spot, variable and unlocked option survive (17 checks) |
| 8 | `example_quest_tracker` | one reusable `tracker.lua` completing quests by **dialogue** (`info.finished`), by **items** (`inventory.changed`) and by **kills** (`entity.died`), progress in `risen.save`, rewards on completion | `info.finished`, `inventory.changed`, `entity.died` | `ex_quest_tracker`, 90 s | all three completion paths, other templates ignored, repeated death counted once, rewards (15 checks) |
| 9 | `example_voice` | an NPC whose dialogue lines are **spoken** from audio files of the mod: ogg, wav and mp3, one file per language (voice language of the game), subtitles per language, a sound made by Lua code, a sound-only `SaySound` line, a bark | `risen.dialog.create` (`sound`, `text` per language), `risen.voice.load_dir/set/say/speaking/language`, `risen.assets.import_sound/sound` | `ex_voice` (English, then German voice language: two processes), 140 s | the engine asked for the line's text id and took the variant of the voice language; FMOD created ogg/mp3/wav sounds; an option with a 0.4 s voice took 53 frames instead of 467; speaking state; refusal of bad files; a shipped NPC line shadowed by a loose sound (22 checks per language). The user heard the tones. **Not** lip sync (no `._xmot` for custom lines) |
| 10 | `example_spell` | edit shipped spells (Fireball mana cost and damage, Light's rune and scroll cost) and add a **Strong Fireball** (double damage, double cost, needs Fireball 2) as a crystal and a scroll that is used up | `risen.balance.install` (`spells`, `new_spells`), `risen.spell.get/equipped/consume`, `risen.item.can_use` | `ex_spell`, 55 s | the player really casts (`PS_Magic` -> `PS_MagicBall_Shoot`): mana taken 10 / 20 / 10, projectile damage 140 / 280 / 280, the engine's hit script takes `damage + 25 * Fireball - protection` (191 / 331), the last scroll is gone, the edited rune cost (9 checks). **Not** the look of the fireball, NPC casters |
| 11 | `example_recipe` | a **new smithing recipe**: a regular sword (`It_1H_Sword`) and two iron ore (`It_IronOre`) become a **Slightly Improved Sword** (a new item with its own numbers and a localized label) at the anvil; the plan item that is the recipe is given once; crafted items, the plan and the equipped sword survive a quick save and load in a fresh process, and a game without the mod keeps them dormant | `risen.balance.install` (`new_items`, `new_recipes`), `risen.recipe.info/known/can_craft/craft`, `risen.interact.begin`, the `crafting` event | `ex_recipe`, 4 processes ~190 s | real anvil twice with exact counts and events, refusal for a missing skill and for 1 ore, equipped sword, load / missing / again phases (see header of `test_recipe.lua`) |

Timings are wall-clock on the development machine (engine start ~30 s of it). Each scenario ends itself the moment its checks are done.

## Patterns

**Recipes.** A recipe is an item: a character knows it while it carries the recipe item (`learn = true` gives the plan once per savegame). `risen.balance.install` with `new_items` + `new_recipes` creates both (on the first `tick`, and the item the recipe makes may be in the same spec); crafting is the
engine's: `risen.recipe.craft` for a script, `risen.interact.begin(player, anvil)` plus the click script for the real station (`example_recipe/test_recipe.lua` has the whole sequence). React to `crafting(crafter, recipe, result, count, ingredients)`; the inventory events of a craft have cause `"crafting"`.

**When to apply what.**

| Moment | Do | Why |
|---|---|---|
| `main.lua` (load time) | register handlers, `risen.script.define`, `risen.loc.set`, `risen.balance.install(...)` | no world and no templates exist yet; `install` schedules itself |
| first `tick` | `risen.template.patch/create`, `risen.assets.write`, `risen.item.give` to templates (`npc.template_give`) | earliest moment templates resolve (`mods.loaded` is too early); textures must be on disk **before** the engine loads them |
| `load.before` | `risen.quest.define`, `risen.dialog.create` | the engine restores quest state and "option used" flags only into objects that already exist; the `risen.save` data of the loaded savegame is already readable |
| `session.start` (`"new"` **and** `"load"`) | the same define calls again; `risen.template.patch(..., { instances = true })`; spawn runtime entities (after ~60 frames: the player entity exists a moment later) | a new game wipes runtime quests; worlds keep their own copies of values; runtime entities are not saved |
| `quest.status_changed`, `npc.killed`, `info.finished`, ... | react | events fire for the game's own quests, NPCs and dialogues too: compare names |

All `define`/`create` calls are idempotent, so one `define_content()` serves `load.before` and `session.start`.

**What persists.** Quest status and log entries, "option used" flags, game variables (`risen.gamevar`), inventories (including stacks of templates created by the mod: create
them on the first tick, before a load), values written into world entities (`instances = true`) are in the savegame. `risen.save.get/set` data is written next to every savegame
(sidecar file, restored on load, empty in a new game): use it for counters, baselines, "reward already given", where a runtime NPC stands. **Not saved:** entities made with
`risen.npc.spawn` / `risen.entity.spawn` (NPCs, bosses, scattered items), the Lua state, template edits and created templates (apply them again in every process). So: remember
*that* something exists (and where) in `risen.save`, and recreate it on every `session.start` with a guard (`if not risen.entity.get(NAME) then ... end`).

**Triggers must be idempotent.** `quest.status_changed` may be seen again after a load. Check a flag in `risen.save` first, set it, then act (`example_quest_rewards`); count
deaths by entity name in a set, because the engine can report one death twice (`example_kill_quest`, `tracker.lua`).

**Events instead of polling (M5b).** Never look at inventories or the entity list every few frames: react to `inventory.changed(owner, template, delta, total, cause, other)` (filter `owner:is_player()` and
the template; `total` is the new amount, `cause` tells pickup / loot / trade / script / dialogue / drop / consume), `entity.died(entity, killer, cause)` (once per entity; `killer:is_player()` for "the player killed it"),
`entity.defeated` (knocked out humans), `entity.spawned/destroyed`, `world.ready` ("the player exists", the moment to spawn runtime NPCs) and `item.equipped/consumed`. They are dispatched at the end of the frame, so a
handler may do anything (give items to the same inventory, kill, spawn) without disturbing the engine. `example_fetch_pickup`, `example_kill_quest` and `tracker.lua` show the three shapes (count a template, count kills, a table-driven tracker);
`mods/events_demo` is the executable reference of every event and cause.

**Ordering.** Items given with `risen.item.give` (or `loot`) must name templates that exist; mods that create templates (`new_items`) should load before mods that use them
(`load_after` in `mod.toml`). `risen.balance.install` applies its spec on the first tick and every `session.start`; `risen.npc.spawn` after that sees the edited templates. Set the
hit point maximum before the hit points (`risen.npc.spawn` options do it for you).

**Dialogue gotchas.** End an option with `{ command = "End" }` (an open conversation blocks saving). An info disappears after use unless `permanent = true`.
`condition = "Running"|"Success"|... , quest = Q` is the engine's own switch: offer the hand-in only while the quest runs, unlock follow-ups after it succeeded - no code.
A quest-bound option makes the engine append the speakers' labels to the quest log, so the log grows by more than your own `add_log` lines. A quest-state condition on a quest
that does not exist yet blocks the game with a message box (`risen.dialog.create` refuses it). `Entity:position()` returns `nil`s without a world: guard `x, y, z` before arithmetic.

**Spells.** A spell is a group of items: a crystal (Fireball, Frost, Missile) or a rune / scroll / orb (everything else) holds mana cost, requirement and "used up"; the damage lives in the projectile. `spells = { Fireball = { mana_cost = 10, damage = 140 } }` edits a shipped spell, `new_spells = { My_Spell = { base = "Fireball", damage_mult = 2, forms = { "crystal", "scroll" } } }` builds a new one;
both go through `risen.balance.install` (first `tick` + every `session.start`). To test a spell, cast it for real: `risen.item.equip(player, crystal)`, `player:set_state("PS_Magic")`, wait for `player:state() == "PS_MagicBall_Loop"`, then `player:set_state("PS_MagicBall_Shoot")`: the mana and the projectile are the engine's own (`test_spell.lua`). Numbers fixed in the game's scripts (heals, the Fireball area, +25 per skill level) are in `docs/SPELL_PROPERTIES.md`.

**Voiced lines.** Put the audio next to the dialogue and name it by text id: `voice/<language>/<option name>_<line number>.<ogg|wav|mp3>`, `risen.voice.load_dir("voice")` before `risen.dialog.create` (or `sound = "..."` / `sound = { en = ..., de = ... }` on the line;
`text = { default = ..., de = ... }` for the subtitle). The game's voice language picks the variant (`default`, then English, when it is missing); register on `load.before` and `session.start` like dialogues. The layer compiles the files itself (no tool), refuses a bad one
with `nil, reason` before the dialogue exists, and the line lasts as long as the audio. `risen.voice.say(npc, key)` is a bark; `-Trace voice` lists every id the engine asks a voice for. Test it with `test-run.ps1 -VoiceLanguage German` (writes the option into the copy's `ConfigUser.xml`).

**Finding a material variant.** Scan the templates and group them by mesh/actor: the entries of a group with different `MaterialSwitch` values are the shipped variants
(`Wolf` 0 / `Wolf_Black` 1, `It_Po_Health_01` 0 / `It_Po_Mana_01` 1, `Gnome` 0 / `Gnome_Chief` 1, crystals 3/4/6, gems). Loop `risen.template.list(group)`, read
`get_property("eCMesh_PS", "MeshFileName")` (items) or `get_property("eCAnimation_PS", "ResourceFilePath")` (creatures) and `MaterialSwitch`.

**Testing a quest mod** (what the scenarios here do): drive conversations like a player (`risen.dialog.start(player, npc)`, wait 90 frames, `info:run()`, `T.wait_event("info.finished", ...)`),
stand in for the player's actions where a script cannot do them (below), assert on quest status, log entries, counters, inventory counts and `risen.save`. Save/load inside one process:
`risen.world.quick_save()`, wait `save.after`, `risen.world.quick_load()`, wait `session.start` with mode `"load"`. Several scenarios at once: `-Instance 2/3/4` (own game copies).
A scenario that writes loose assets must remove them again (`example_material` does), generated files live in the test copy and later scenarios would see them.

## Gaps / not possible yet

**Resolved by M5b ("reliable engine events", [LUA_API.md "Gameplay events"](LUA_API.md), live-verified in `mods/events_demo`).** The first version of the examples polled inventories and worked around
missing death events; the three examples that did (`example_fetch_pickup`, `example_kill_quest`, `example_quest_tracker`) now use the events and poll nothing:

| What the examples wanted | Now |
|---|---|
| "the player's inventory changed" for every source | `inventory.changed(owner, template, delta, total, cause, other)` (and `inventory.added/removed`): script give/take/transfer, dialogue `Give`, loot, trade, drop, ground pick-up, consume; `cause` says which, `other` is the corpse / trader / giver |
| a death exactly once, with the killer | `entity.died(entity, killer, cause)` (`"script"`, `"cheat"`, `"combat"`, `"other"`), deduplicated per entity; `entity.defeated(entity, attacker)` for knocked-out humans; `npc.killed/defeated` carry the killer too (raw, may repeat) |
| `Kill` leaves the victim not dead, a second `npc.killed` follows | `risen.script.call("Kill", killer, victim)` (killer = `self`, victim = `other`) gives one `entity.died(victim, killer, "script")`; a second call on the same victim fires nothing new. In the live run the victim was dead (`is_dead()`) at once; the examples agent saw a victim in combat that was not, the event is only posted when the engine says the victim is dead, so a later death reports it |
| `Entity:kill()` | `entity.destroyed(name, kind, template)` (no death) |
| `entity.spawned` / `entity.destroyed` | `entity.spawned(entity, template)` for `Entity::Spawn` (script API, game scripts), `entity.destroyed` for every removal; `world.ready(mode)` when the player exists |
| `item.used/equipped/unequipped/consumed` | `item.equipped/unequipped(owner, template, slot)`, `item.consumed(owner, template, count)` (`item.used` for food is `item.consumed`) |
| the player cannot be made to pick up / loot / trade from a script | `risen.item.pick_up/loot/trade/drop/transfer/equip/unequip` run the engine's own operations, so the events can be tested; the *interactive* paths (buy/sell screen, looting with the mouse) end in the same engine functions but are not run live |

Still open: `Entity:set_hp(0)` is no death (the AI keeps fighting, no event: use `Kill`); (crafting has its own cause and event since M6b); container-opened and `item.used` for non-consumables
are not events; a mod that must not poll for *positions* ("an NPC came near") still has to.

### Other gaps found

* **A script cannot act for the player.** No API makes the player pick something up, loot, fight or walk to a place, so `example_fetch_pickup` stands in with `risen.item.give`
  and the kill examples with the `Kill` script. `Entity:set_state("ZS_FetchItem")` on an NPC did not make it fetch an item. Real combat works but is not deterministic (a pirate against
  wolves: killed within seconds in one run, "threatening" for over two minutes in the next), so it is no regression test. Since M5b `risen.item.pick_up/loot/trade/drop` run the engine's pickup/loot actions
  on any inventory, which the event tests use; there is still no `Entity:attack(target)`.
* **Quest types do nothing by themselves.** `type = "Kill"` / `"HasItems"` etc. are labels in the quest log; nothing in the examples relied on the engine evaluating them, and none was
  observed to (not investigated). All completion logic lives in Lua.
* **Dialogue conditions beyond quest state** (has item, game variable, level) do not exist; the examples check inside the script and answer "not enough" (`example_fetch_turnin`).
  An option that should only be visible with the items needs a condition on a variable that a poll keeps up to date.
* **Textures and meshes cannot be authored here.** `example_material` derives its texture from a shipped one (nothing of the game is stored in the repository); a modder with
  artwork needs a converter to the compiled `._ximg`/`._xmsh` formats (RE.md §26) and puts the files into `<mod>/assets/`. A texture the engine already loaded is not replaced,
  and the checks stop at "the engine loaded the loose file": there is no way to look at the rendered result (screenshots of the D3D9 window are black). Material variants
  (`MaterialSwitch`) are only numbers; which numbers are valid for a mesh has to be found by scanning shipped templates (see Patterns).
* **No position filter on events.** `entity.died` reports every death in the world, also those far away from the player (the killer is given: compare it with the player, `entity:distance_to(player)` filters by distance).
* **Per-entity "create if missing" boilerplate** (`ensure_npc`) is repeated in every example; a `risen.npc.persistent(name, template, pos, opts)` helper that respawns on every
  session start would remove it (positions are kept in `risen.save` by hand today).
* **Combat-driven scenarios are slow and flaky; item pick-up scenarios are impossible** (see above), so the examples are verified with stand-ins. What the stand-ins do not prove is
  listed per example in the index (the "Verified" column) and in the header of each `test_*.lua`.
