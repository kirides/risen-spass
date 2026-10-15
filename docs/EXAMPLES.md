# Example mods

Nine small, self-contained mods under `mods/example_*`: each is the simplest realistic form of one thing a modder wants, written to be copied
(`main.lua` with a header that says what you learn, `mod.toml`, and a `test_*.lua` that is loaded **only** in a test run). Every one of them has a
`risen.test` scenario that was **run live in the game**; nothing in this table is claimed that was not. The scenarios are registered in
`scripts\smoke-test.ps1` (`ex_*`), so the regression suite covers them.

Run one (about a minute; use your own test copy, never the real install, always windowed, see [DEVELOPMENT.md](DEVELOPMENT.md)):

```powershell
.\scripts\test-run.ps1 -Instance 2 -Mods example_kill_quest -WaitForResult -Seconds 300 -Scenario example_kill_quest -ShowChecks
.\scripts\smoke-test.ps1 -Scenario ex_template_patch,ex_balance,ex_custom_npc,ex_fetch_turnin,ex_fetch_pickup,ex_kill_quest,ex_quest_rewards,ex_quest_tracker,ex_material -Parallel 3
```

## Index

| # | Example | Shows | Key APIs | Smoke scenario / time | Verified (2026-10-15) |
|---|---|---|---|---|---|
| 1 | `example_template_patch` | edit existing item and NPC templates: gold value, weapon damage, a guild; first `tick` + every `session.start` | `risen.template.patch(name, sets, { instances })` | `ex_template_patch`, 45 s | template read-back, new spawn, inventory stack, all 30 live world apples, NPC spawn (8 checks) |
| 2 | `example_balance` | declarative balancing of items (value, damage, requirements, modifiers), NPCs/monsters (hp, level, xp, skills, bravery, loot, relative `hp_mult`/`level_add`), groups by pattern/template group | `risen.balance.install` (`items`, `npcs`, `npc_groups`, `item_groups`), `risen.item.can_use` | `ex_balance`, 35 s | numbers on templates, spawns, loot, 98 live world wolves rewritten (11 checks) |
| 3 | `example_material` | (A) other material variant of an existing mesh: `MaterialSwitch` on the health potion, a new black wolf; (B) a texture file that **shadows** a shipped one (red/blue swapped sword blades, written with `risen.assets.write`) | `risen.template.patch/create(material=)`, `risen.assets.read/write/info/resource` | `ex_material`, 40 s | switch values equal the shipped `It_Po_Mana_01` / `Wolf_Black`; the engine resolved and loaded (resource state 3) the loose file; shadow removed again (11 checks). **Not** how it looks: screenshots of the game window are black |
| 4 | `example_custom_npc` | a new NPC template (`Spass_Hob`) and reward item (`Spass_Hob_Knife`), the NPC placed at the start, three dialogue options, a quest started by one option and finished by another that is only offered while it runs | `risen.balance.install{new_npcs,new_items}`, `risen.npc.spawn`, `risen.dialog.create`, `risen.script.define`, `risen.quest.define/succeed` | `ex_custom_npc`, 65 s | NPC uses the new template and label, quest runs, option offered only while running, quest succeeds, reward item of the new template (12 checks) |
| 5a | `example_fetch_turnin` | fetch quest: the NPC asks for 3 berries, a permanent hand-in option checks the inventory (too few: log line, nothing taken; enough: items taken, gold paid, quest succeeds) | `risen.item.count/take/give/money`, `condition = "Running"`, `permanent = true` | `ex_fetch_turnin`, 75 s | both hand-ins incl. the failed one (9 checks) |
| 5b | `example_fetch_pickup` | fetch quest that completes **by itself** when the player carries 3 more berries than at the start (baseline in `risen.save`), progress log lines, berries scattered around the NPC, reward, items kept | `item.picked_up` event **plus** inventory poll, `risen.save` | `ex_fetch_pickup`, 60 s | progress and completion via the poll; the items arrive through `risen.item.give` (see Gaps: a script cannot pick up for the player) (10 checks) |
| 6 | `example_kill_quest` | kill 3 `Wolf` (counter in `risen.save`, log "Wolves: 2/3", duplicate death counted once, a clone of Wolf does not count), then a `Success`-conditioned option unlocks the boss quest: kill one named entity (`EX_DireWolf`, template `Spass_Dire_Wolf`) | `npc.killed`, `Entity:template():name()`, `Entity:name()` | `ex_kill_quest`, 75 s | counting, negative cases, unlock, boss spawn and kill (12 checks); kills are driven with the game's `Kill` script (see Gaps) |
| 7 | `example_quest_rewards` | what happens when a quest completes: gold, a spawned guard, a game variable, an unlocked option; **idempotent after save/load** | `quest.status_changed`, `risen.save`, `risen.gamevar`, `risen.world.quick_save/quick_load`, `condition = "Success"` | `ex_quest_rewards`, 100 s | trigger ran once, gold paid once across a real quick save + quick load, guard respawned at the same spot, variable and unlocked option survive (17 checks) |
| 8 | `example_quest_tracker` | one reusable `tracker.lua` completing quests by **dialogue** (`info.finished`), by **items** (inventory poll + `item.picked_up`) and by **kills** (`npc.killed`), progress in `risen.save`, rewards on completion | `info.finished`, `item.picked_up`, `npc.killed`, `tick` | `ex_quest_tracker`, 90 s | all three completion paths, other templates ignored, repeated death counted once, rewards (15 checks) |

Timings are wall-clock on the development machine (engine start ~30 s of it). Each scenario ends itself the moment its checks are done.

## Patterns

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

**Ordering.** Items given with `risen.item.give` (or `loot`) must name templates that exist; mods that create templates (`new_items`) should load before mods that use them
(`load_after` in `mod.toml`). `risen.balance.install` applies its spec on the first tick and every `session.start`; `risen.npc.spawn` after that sees the edited templates. Set the
hit point maximum before the hit points (`risen.npc.spawn` options do it for you).

**Dialogue gotchas.** End an option with `{ command = "End" }` (an open conversation blocks saving). An info disappears after use unless `permanent = true`.
`condition = "Running"|"Success"|... , quest = Q` is the engine's own switch: offer the hand-in only while the quest runs, unlock follow-ups after it succeeded - no code.
A quest-bound option makes the engine append the speakers' labels to the quest log, so the log grows by more than your own `add_log` lines. A quest-state condition on a quest
that does not exist yet blocks the game with a message box (`risen.dialog.create` refuses it). `Entity:position()` returns `nil`s without a world: guard `x, y, z` before arithmetic.

**Finding a material variant.** Scan the templates and group them by mesh/actor: the entries of a group with different `MaterialSwitch` values are the shipped variants
(`Wolf` 0 / `Wolf_Black` 1, `It_Po_Health_01` 0 / `It_Po_Mana_01` 1, `Gnome` 0 / `Gnome_Chief` 1, crystals 3/4/6, gems). Loop `risen.template.list(group)`, read
`get_property("eCMesh_PS", "MeshFileName")` (items) or `get_property("eCAnimation_PS", "ResourceFilePath")` (creatures) and `MaterialSwitch`.

**Testing a quest mod** (what the scenarios here do): drive conversations like a player (`risen.dialog.start(player, npc)`, wait 90 frames, `info:run()`, `T.wait_event("info.finished", ...)`),
stand in for the player's actions where a script cannot do them (below), assert on quest status, log entries, counters, inventory counts and `risen.save`. Save/load inside one process:
`risen.world.quick_save()`, wait `save.after`, `risen.world.quick_load()`, wait `session.start` with mode `"load"`. Several scenarios at once: `-Instance 2/3/4` (own game copies).
A scenario that writes loose assets must remove them again (`example_material` does), generated files live in the test copy and later scenarios would see them.

## Gaps / not possible yet (this is also the specification for M5b, "reliable engine events")

The examples contain two kinds of workaround. Each is marked in the source with `>>> EVENT BLOCK` / `>>> POLLING ...` comments so it can be swapped for an event later.

### Polling that an event should replace

| Where | Workaround today | Wanted event (name, payload) |
|---|---|---|
| `example_fetch_pickup/main.lua` `on_item_count_changed`, `tracker.lua` `items_changed` | every 20 frames `risen.item.count(player, item)`; `item.picked_up` only as an accelerator | `inventory.changed(owner, template, delta, count, source, other)`: `owner` Entity whose inventory changed, `template` name, `delta` signed amount, `count` the new total of that template, `source` string (table below), `other` the counterparty Entity (corpse, chest, trader, giver) or `nil` |
| `example_kill_quest`, `tracker.lua` kills | `npc.killed` works for real deaths, but nothing for scripted kills, and the quest author cannot tell who killed | `entity.died(entity, killer, cause)`: `killer` Entity or `nil`, `cause` `"combat"`, `"script"` (`Kill` command / `risen.script.call("Kill")`), `"damage"` (hp 0), `"other"`; fired exactly once per entity; plus `entity.defeated(entity, attacker)` for humans that were knocked out |
| `example_*` NPC/boss respawn after loads | every mod re-implements "after `session.start` wait 60 frames, then spawn if missing" | `entity.spawned(entity, template)` / `entity.destroyed(name, template)` for every entity of the world incl. savegame loads (so a mod sees "the world is built now" per entity), and a `world.ready` event when the player exists |
| (not used by the examples, same problem) | `risen.item.can_use` polling for equipment | `item.used/equipped/unequipped/consumed(owner, template)` |

### Which engine action should trigger which event

Today's behaviour in the first column ("does not fire" / "no event" were seen live by the examples agent with `-Trace events`/probes; "unknown" and "not tested" were not tried), the wish in the second.

| Engine action | `item.picked_up` today | `inventory.changed` wanted (`source`) |
|---|---|---|
| Player (or an NPC) takes an item from the ground | fires (`owner, template, amount`) | `"pickup"`, `other` = nil |
| Script gives: `risen.item.give`, `PSInventory::CreateItems` | **does not fire** (probe: `give` of two apples, no event) | `"script"`, delta +n |
| Script takes: `risen.item.take` | not tested (the same path as `give`, no event expected) | `"script"`, delta -n |
| Dialogue `Give` command (NPC to player or from nowhere) / `Take` | not tested | `"dialogue"`, `other` = the NPC |
| Looting a corpse or chest (items move from its inventory to the player's) | unknown; the examples cannot perform it (interactive) | `"loot"`, two events: -n for the container, +n for the looter, `other` set on both |
| Buying from / selling to a trader | unknown, not testable from a script | `"trade"`, both sides |
| Crafting, brewing, smithing (ingredients consumed, product created) | unknown | `"craft"` (-ingredients, +product) |
| Dropping an item into the world | unknown | `"drop"`, delta -n (then a world item exists) |
| Using up a potion/food (`risen.item.consume`) | not tested | `"consume"`, delta -1 (plus `item.used`) |

| Engine action | `npc.killed` / `npc.defeated` today | `entity.died` / `entity.defeated` wanted |
|---|---|---|
| Monster dies in a fight (pirate vs wolf, seen live) | `npc.killed(victim)`, once | `died(victim, killer, "combat")` once |
| Human loses all hit points in a fight (peasants against a pirate) | **`npc.defeated`** (knocked out, `ZS_Unconscious`), not `killed` | `defeated(victim, attacker)`; `died` only if it really dies |
| `risen.script.call("Kill", killer, victim)` (`self` = killer, `other` = victim; the arguments in the other order kill the *player* in the quest manager's eyes: `npc.killed(PC_Hero)`, hit points 0, `is_dead() == true`) | `npc.killed(victim)` at once, but the victim only has 0 hit points (`is_dead()` still false after 100 frames, the AI keeps attacking); a **second** `npc.killed` follows when something really finishes it | `died(victim, killer, "script")` exactly once, when the victim is dead |
| `Entity:set_hp(0)` | nothing (documented in RE.md) | `died(victim, nil, "damage")` if the engine treats it as death, else nothing |
| `Entity:kill()` (removes the entity) | nothing | `destroyed(name, template)` |

### Other gaps found

* **A script cannot act for the player.** No API makes the player pick something up, loot, fight or walk to a place, so `example_fetch_pickup` stands in with `risen.item.give`
  and the kill examples with the `Kill` script. `Entity:set_state("ZS_FetchItem")` on an NPC did not make it fetch an item. Real combat works but is not deterministic (a pirate against
  wolves: killed within seconds in one run, "threatening" for over two minutes in the next), so it is no regression test. Wanted: a way to run the engine's pickup/loot/attack
  actions on the player or an NPC (`risen.player.pickup(item_entity)`, `Entity:attack(target)`), or the M5b events driven by test doubles.
* **Quest types do nothing by themselves.** `type = "Kill"` / `"HasItems"` etc. are labels in the quest log; nothing in the examples relied on the engine evaluating them, and none was
  observed to (not investigated). All completion logic lives in Lua.
* **Dialogue conditions beyond quest state** (has item, game variable, level) do not exist; the examples check inside the script and answer "not enough" (`example_fetch_turnin`).
  An option that should only be visible with the items needs a condition on a variable that a poll keeps up to date.
* **Textures and meshes cannot be authored here.** `example_material` derives its texture from a shipped one (nothing of the game is stored in the repository); a modder with
  artwork needs a converter to the compiled `._ximg`/`._xmsh` formats (RE.md §26) and puts the files into `<mod>/assets/`. A texture the engine already loaded is not replaced,
  and the checks stop at "the engine loaded the loose file": there is no way to look at the rendered result (screenshots of the D3D9 window are black). Material variants
  (`MaterialSwitch`) are only numbers; which numbers are valid for a mesh has to be found by scanning shipped templates (see Patterns).
* **No killer identity, no position filter on events.** `npc.killed` reports every death in the world, also those far away from the player.
* **Per-entity "create if missing" boilerplate** (`ensure_npc`) is repeated in every example; a `risen.npc.persistent(name, template, pos, opts)` helper that respawns on every
  session start would remove it (positions are kept in `risen.save` by hand today).
* **Combat-driven scenarios are slow and flaky; item pick-up scenarios are impossible** (see above), so the examples are verified with stand-ins. What the stand-ins do not prove is
  listed per example in the index (the "Verified" column) and in the header of each `test_*.lua`.
