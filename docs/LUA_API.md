# Lua API reference

Mods live in `<Risen>\bin\spass\mods\<mod>\` and need a `mod.toml`:

```toml
id          = "my_mod"         # required: a-z 0-9 _
name        = "My Mod"
version     = "1.0.0"
author      = "you"
main        = "main.lua"       # default
load_after  = ["other_mod"]    # ordering only
requires    = ["base_mod"]     # hard dependency (mod is skipped if missing)
api_version = 1                # Lua API generation the mod was written for; a mod asking for a newer one is skipped with an error (risen.api_version)
data_overlay = false           # experimental: mount the mod directory into the virtual file system at startup (docs/RE.md §17)
assets       = "assets"        # directory of loose meshes/textures/... mounted at startup (false = none; see "New meshes, textures and icons")
```

The Lua layer starts right after the game's script DLLs are loaded (`gCScriptAdmin::PostInitialize`), before any
world is running. `main.lua` runs once at that moment: **register handlers there, do not expect a running world**.

Every mod gets its own sandboxed environment: base functions, `string`, `table`, `math`, `bit` and
`os.{time,clock,date,difftime}`; no `io`, `ffi`, `debug`, `package`, `dofile`, `loadfile`, or bytecode loading.
`require("a.b")` loads `<mod dir>/a/b.lua` inside the same sandbox. `print` writes to the log.
`config.toml` `[lua] unsafe = true` lifts the restrictions for development.

Globals: `mod` (`id, name, version, author, dir`), `risen`.

**Editor support:** [`types/risen.d.lua`](../types/risen.d.lua) describes every function below for LuaLS (completion, hover docs, type
checks); see [`types/README.md`](../types/README.md).
**Examples:** eleven small, live-verified example mods (patching, balancing, materials/textures, custom NPC, fetch/kill quests, completion triggers, quest tracker, voiced dialogue, spells) are indexed in [`EXAMPLES.md`](EXAMPLES.md).

## Events

| Event | Arguments | When |
|---|---|---|
| `mods.loaded` | – | after every mod's `main.lua` ran |
| `tick` | – | every frame of a running game session (`gCSession::Process`) |
| `shutdown` | – | Lua runtime stops |
| `session.start` | `mode` = `"new"`, `"load"` or `"other"` | a game session was (re)started: new game, or after a savegame was loaded |
| `session.stop` | – | the running session is being torn down (also fires a few times while a game is starting) |
| `world.changed` | `name` | a world became the active one |
| `save.before` / `save.after` | `name` / `name, ok` | around every savegame write (manual, quick, auto) |
| `load.before` / `load.after` | `name` / `name, ok` | around reading a savegame. `load.before` runs **before the engine reads quests and infos** |
| `quest.status_changed` | `quest, status, previous` | any quest changed status (`"Open"`, `"Running"`, `"Success"`, `"Failed"`, `"Obsolete"`, `"Cancelled"`, `"Lost"`, `"Won"`) – including the game's own quests |
| `info.started` / `info.finished` | `info_name, owner` | a dialogue option was chosen / finished playing |
| `world.ready` | `mode` (as `session.start`) | the player entity exists in a new session (about a second after `session.start`): the world has settled, gameplay events are on for NPCs too |
| `inventory.added` / `inventory.removed` | `owner, template, count, total, cause, other` | the amount of one template in `owner`'s inventory rose / fell by `count` (> 0); `total` is the new amount; see "Gameplay events" |
| `inventory.changed` | `owner, template, delta, total, cause, other` | the same changes as one event with a signed `delta` |
| `item.picked_up` | `owner, template, amount` | an inventory took an item entity from the world (`owner` is the entity whose inventory it is) |
| `item.equipped` / `item.unequipped` | `owner, template, slot` | a stack was put on / taken off (`slot` is the engine's `gEEquipSlot` number) |
| `item.consumed` | `owner, template, count` | the engine used items up (`gCInventory_PS::ConsumeItems`: potions, food; `risen.item.consume`) |
| `entity.died` | `entity, killer, cause` | an entity **died**, once per entity; `cause` `"combat"`, `"script"`, `"cheat"` or `"other"`; `killer` may be `nil` |
| `entity.defeated` | `entity, attacker` | a human was **knocked out** (unconscious, not dead) |
| `npc.killed` / `npc.defeated` | `entity, killer` / `entity, attacker` | the engine's raw report behind `entity.died` / `entity.defeated` (may repeat for one entity; prefer the `entity.*` events) |
| `entity.damaged` | `entity, attacker, damage, raw` | a hit took hit points (`damage` = hit points lost; `raw` = the weapon's own damage before protection, 0 for monster bites) |
| `entity.spawned` | `entity, template` | `Entity::Spawn` created an entity: `risen.entity.spawn`, `risen.npc.spawn` and the game's scripts |
| `entity.destroyed` | `name, kind, template` | an entity was removed from the world (`eCEntity::Kill`: picked-up items, `Entity:kill()`, despawns); `kind` `"npc"`, `"player"`, `"item"`, `"other"`; the entity itself is gone, so only its name |

```lua
local h = risen.on("tick", function() ... end, 10)   -- optional priority, higher runs first
risen.off(h)
risen.emit("my_mod.event", a, b)                      -- mod-to-mod events
```
`mods/eventlog` logs every event above with its arguments (development aid; `-Trace events` shows them too, with the engine-side timing).

### Gameplay events (M5b, see [RE.md §27](RE.md))
`inventory.*`, `item.*`, `entity.*`, `npc.*` and `world.ready` come from hooks on the engine's own operations, so a mod never has to poll an inventory or the entity list:
every route that changes an inventory or kills / creates / removes an entity fires **exactly one** event per change. Live-verified in `mods/events_demo` (scenario `events`, 41 exact-count checks).

* **Dispatch.** The hook runs inside engine code (an inventory loop, a script, the AI), so the event is only *queued* there, with everything it needs captured (names,
  counts); the layer dispatches the queue at the end of the same frame, right before `tick`. A handler may therefore call every API (`risen.item.give` on the same inventory, `Entity:kill()`, ...)
  without disturbing the code that was running. Events raised by handlers are dispatched in the same frame (up to four rounds). An `Entity` argument is a normal handle (re-resolved by name).
* **When.** Only while a game session runs; nothing during a savegame load (the world is rebuilt). After `session.start` the engine fills the NPC inventories and creates their equipment:
  until the player exists (`world.ready`) and for 30 frames after it, inventory and item events of anyone **but the player**, and `entity.destroyed`, are dropped, so the initial population
  of the world is not reported. What the player gets or does is reported from the first frame.
* **`inventory.added/removed/changed(owner, template, count|delta, total, cause, other)`.** `owner` is the Entity whose inventory changed, `template` the template name, `total` the new amount of that
  template in the whole inventory (all stacks, equipped included), `other` the counterpart Entity of a transfer (the corpse, the trader, the giver) or `nil`. One event per inventory and template per engine
  operation: a loot of ten different items is ten pairs of events (one `removed` for the container, one `added` for the looter, each with `other`). `cause`:

| `cause` | what happened | triggered by (verified) |
|---|---|---|
| `"script"` | items created / deleted / moved by script code: the game's scripts (quest rewards, spawning an NPC's inventory) and mods | `risen.item.give/take/transfer`, `risen.npc.spawn` items |
| `"dialogue"` | a dialogue command | `{ command = "Give", ... }` (`CreateItem` / `DestroyItem` are hooked the same way, but only `Give` was driven live) |
| `"pickup"` | an item entity from the ground went into an inventory | `risen.item.pick_up` (the engine's `InsertStack`; also fires `item.picked_up` and `entity.destroyed` of the world item) |
| `"loot"` | corpse / chest looting (`LootSelections`) | `risen.item.loot` |
| `"trade"` | the trade screen's transfer (`TradeSelections`) | `risen.item.trade` |
| `"drop"` | items dropped into the world | `risen.item.drop` |
| `"consume"` | items used up (`ConsumeItems`), together with `item.consumed` | `risen.item.consume` |
| `"other"` | a stack appeared / vanished without a hooked operation announcing it (a fallback; only creations and deletions are seen) | not seen in the tests |

  Gold is the item `It_Gold`. *Not seen / limits:* crafting, brewing and smithing use the same create/delete operations, so they appear as `"script"` (there is no separate cause); the trade screen's buy/sell
  and the player's interactive loot/pick-up are the same engine operations as the Lua functions above (checked in the decompilation), but cannot be driven from a script, so they are not run live.
* **`entity.died(entity, killer, cause)`** fires **once per entity** (`npc.killed` repeats when something hits a corpse again). The engine kills through its `Kill` script
  (`self` = the killer, `other` = the victim): `killer` is its `self` (`nil` for `risen.script.call("Kill", nil, victim)`). `cause`: `"script"` (a `Kill` called through `risen.script.call`), `"cheat"` (the `OnCheatKill` script),
  `"combat"` (the engine's own death with an attacker), `"other"`. Humans that lose all hit points in a fight are **knocked out**, not killed: `entity.defeated` (and `npc.defeated`) fire for them and `entity.died` does not,
  until something really kills them. `Entity:set_hp(0)` does not kill (the AI keeps fighting) and fires nothing; `Entity:kill()` removes the entity (`entity.destroyed`, no death). Verified: `Kill` with and without killer, twice on one victim,
  `Defeat`, `OnCheatKill`, a real fight (a wolf kills the player: `entity.died(PC_Hero, wolf, "combat")` with the `entity.damaged` before it).
* **`entity.spawned`** is raised by `Entity::Spawn` (what `risen.entity.spawn`, `risen.npc.spawn` and the game's own spawn scripts call); entities the engine creates internally (items dropped from an inventory,
  equipment copies, effects) are not reported. **`entity.destroyed`** comes from `eCEntity::Kill`; effect entities (`VFX_*`) are skipped. **`entity.damaged`** only fires for hits that took hit points.
* **Not covered:** "container opened" (opening a chest is a dialogue-less interaction that moves nothing by itself; its loot is reported as `"loot"`), `item.used` for food (the use scripts call `ConsumeItems`, reported as
  `item.consumed`; not driven live: `risen.item.consume` is), per-item property changes.

## risen.log / risen.version
`risen.log.trace|debug|info|warn|error(...)` (arguments are `tostring`-ed and joined by spaces).

## risen.test / risen.trace — live verification and tracing (see [DEVELOPMENT.md](DEVELOPMENT.md) "Testing and tracing", `mods/testdemo`)
```lua
local T = risen.test
T.plan { "player exists", "item spawned" }          -- checks that must run; a missing one fails the run as "unfinished"
T.run(function()                                     -- a scenario as a coroutine (starts on the next frame)
    T.section("new game")                            -- later checks carry the section name; a hang is reported "in section X"
    local mode = T.wait_event("session.start", 180)  -- pauses until the event fired (timeout: FAIL + the run ends)
    T.sleep(150)                                     -- frames (the `tick` event), not seconds
    T.wait_until(function() return flag end, 60, "flag")   -- polled once per frame
    T.expect("player exists", risen.entity.player() ~= nil, "detail shown in the log and the result")
    T.eq("hero hit points", player:hp(), 80)         -- detail "expected 80, got 70" on a mismatch; tables compare by content, `tolerance` 4th argument for numbers
    T.matches("log line", text, "value=%d+")         -- Lua pattern
end, "my_scenario")                                  -- returning from the function calls T.finish()
T.finish("summary")      -- writes <Risen>\bin\spass\test-result.json and (with [test] enabled) ends the game
T.fail("reason")         -- records a failed check and ends the run as "aborted: reason"
T.wait_for(pred, 30, function(ok, why) end, "name")  -- callback style, without a coroutine; T.wait_ticks(n, cb)
T.param("mode", "pass")  -- [test.params] of config.toml (scripts\test-run.ps1 -Param mode=fail)
```
`risen.test.*` exists in every run and then only logs `[test] ok|FAIL name: detail` (a mod may ship its checks). It becomes a real run with
`config.toml [test] enabled = true` (set by `test-run.ps1 -WaitForResult`) or `[lua] unsafe`: `finish` writes the result file and `[test] deadline_s` applies
(a run that is not finished by then is reported as FAIL `timeout` with the unfinished checks, also when the game thread hangs inside Lua). `finish` adds the check
`no errors logged` (no `[error]` line in `spass.log`). Status is `pass` only with at least one check, none failed, nothing planned/waited for left unfinished.
Timeouts of `wait_for`/`wait_until`/`wait_event` are wall-clock seconds. `risen.balance.install(spec, { on_applied = fn(when, report) })` reports each application.

```lua
risen.trace("mymod", "got", x)           -- "[12:00:00.123] [trace:mymod] got 5" when the channel is on, nothing otherwise
risen.trace.enable("template,save")      -- also config.toml  [debug] trace = "template,balance,save,events"  ("all" = every channel); trace_file = true -> trace.log
risen.trace.disable("save");  risen.trace.enabled("template");  risen.trace.channels()
```
Built-in channels: `events` (every event the layer emits, with arguments, also when nobody listens; `tick` excluded), `template` (get/patch/propagation counts),
`balance` (every applied job), `save` (save/load phases with durations), `spawn`, `script` (define/override/call and every engine call into a Lua script),
`lua` (handler errors with traceback), `test`, `voice` (every sample title the engine asks for and what was answered, voice registrations, barks). A disabled channel costs one bit test; use `risen.trace.enabled(ch)` to guard expensive message building.

## risen.script — the game's scripts are native functions looked up by name
```lua
risen.script.exists("AssessActive")                    -- bool
risen.script.override("AssessActive", function(ctx)    -- replace an existing script
    return ctx:call_original()                         -- run the previous implementation
end)
risen.script.define("MyMod_Hello", function(ctx)       -- new script, usable from game data by name
    return ctx.arg * 2
end)
risen.script.call("MyMod_Hello", self_entity, other_entity, 21)   -- -> 42 (needs a running game)
```
`ctx` fields: `self`, `other` (Entity or nil), `arg` (integer), `a`, `b` (raw integers, script-to-script calls), `name`.
Return a number or boolean (nil = 0). Errors are logged and the original runs (for overrides).
Limit: 1024 Lua scripts.

## risen.entity
`risen.entity.get("UniqueName")`, `risen.entity.player()` → `Entity | nil`.
Entity methods: `name()`, `display_name()`, `set_name(s)`, `position()` → `x, y, z`, `distance_to(e)`,
`is_npc()`, `is_player()`, `is_dead()`, `is_item()`, `is_character()`, `kill()`, `set_frozen(b)`,
`enable_rendering(b)`, `teleport(name_or_entity)`, `valid()`, `address`. A handle remembers the entity's unique
name: after a savegame load, a session start/stop or a world change it looks the entity up **by name again** instead of
using the old pointer, so handles kept across loads either point at the right entity or fail with `nil`/`false`
(`entity:valid()` tells which). Failures never crash the game. Entities without a unique name cannot be re-resolved
and become invalid after such a change.

## risen.template / risen.entity.spawn / risen.item
Template names are the blueprint names stored in `data/common/templates.pak` (`It_1H_Machete`, `It_1H_Sword`, …); the complete
list (2679 names) is in [TEMPLATES.md](TEMPLATES.md).
```lua
risen.template.exists("It_1H_Machete")                    -- bool
risen.template.list("NPC")                                -- every template name below data/common/templates/<group> (all groups if omitted)
local e = risen.entity.spawn("It_1H_Sword", x, y, z)      -- Entity | nil; needs a running game
risen.item.give(player, "It_1H_Machete", 2)               -- index of the stack the items went to, -1 on failure; optional 4th arg: stack type
risen.item.count(player, "It_1H_Machete")                 -- integer
risen.item.take(player, "It_Apple", 3)                    -- removes up to 3, returns how many were removed
risen.item.money(player)                                  -- number
risen.item.transfer(npc, player, "It_Apple", 2)           -- moved amount (the engine's own transfer); events: removed for npc, added for player (cause "script")
risen.item.loot(player, corpse [, "It_Apple", 2])         -- the engine's loot action (everything, or a part of one stack); cause "loot"
risen.item.trade(npc, player, "It_Apple", 1)              -- the engine's trade transfer (no gold); cause "trade"
local e = risen.item.drop(player, "It_Apple", 1)          -- Entity of the item lying in the world; cause "drop"
risen.item.pick_up(player, e)                             -- takes a world item (item.picked_up, cause "pickup"); returns the stack index
risen.item.equip(player, "It_1H_Sword"); risen.item.unequip(player, "It_1H_Sword")   -- item.equipped / item.unequipped
```

### Editing templates — `risen.template.get / patch` (see `mods/templatepatch`, [RE.md §22](RE.md))
The engine keeps one object per template name; spawns, items given to inventories and **inventory stacks** (which only reference templates) read their values from it,
so a *balancing mod* can change an item or NPC archetype at runtime. Edits are in memory only: **apply them again in every game process**, on the first `tick` at the earliest
(`mods.loaded` is too early, nothing resolves yet; the main menu already ticks once).
```lua
-- declarative: validated first (template, property sets, properties, value types), nothing is written when anything is wrong
local ok, applied, touched = risen.template.patch("It_1H_Sword", { gCItem_PS = { GoldValue = 100 } }, { instances = true })
-- ok = true, applied = number of values written, touched = entities that got them (only with instances = true); or nil, "reason"

-- handle API
local t = risen.template.get("Peasant_1_01")                    -- Template | nil, reason
t:property_sets()                                               -- { "gCNPC_PS", "gCInventory_PS", ... }
t:list_properties("gCNPC_PS")                                   -- { {name=, type=}, ... }
t:get_property("gCNPC_PS", "Guild")                             -- "gEGuild_Grd"
t:set_property("gCNPC_PS", "Guild", "gEGuild_Cit")              -- true | nil, reason   (numbers/booleans as text: "777", "true")
t:instance_count(); t:instances(limit)                          -- the live entities made from it (NPCs, world items, spawns; not inventory stacks)
local e = risen.entity.spawn("It_1H_Sword", x, y, z); e:template() -- Template (the creator of an entity)
risen.template.census()                                         -- { It_Gold = 107, Wolf = 97, ... }, total entities
```
What an edit reaches (verified, details in RE.md §22):

| | new spawns, items given, inventory stacks | entities that already exist |
|---|---|---|
| `t:set_property` / `patch` | **yes**, at once | no: each entity has its own copies |
| `patch(..., { instances = true })` | yes | **yes**, only the listed properties (hit points, amounts, ... untouched) |
| `t:patch_instances()` / `t:patch_entity(e)` | | yes, but copies the **whole** template over the entity: resets per-instance state in those sets (NPC hit points and `Group`, item `Amount`) |

**Savegames.** A savegame stores its entities' values and wins over the template: after a load (and in the world of a new game) existing entities have the saved/level values, so
the edit must be written into them again (`instances = true`). Those values are then saved with the game. Recommended pattern:
```lua
local EDITS = { It_1H_Sword = { gCItem_PS = { GoldValue = 100 } } }
local function apply(instances)
    for name, sets in pairs(EDITS) do assert(risen.template.patch(name, sets, { instances = instances })) end
end
local first = true
risen.on("tick", function() if first then first = false; apply(false) end end)   -- earliest moment
risen.on("session.start", function() apply(true) end)                            -- new game and load: update the world's entities too
```
NPC and monster templates are loaded lazily by the engine; the template methods load them on demand. `gCSkills_PS` (hit points, level, attributes, skills) has no reflected properties: use `risen.npc.stats/set_stats` ("Balancing NPCs" below).
Entity methods added with this API: `Entity:template()`, `Entity:stack_item(index)` (inventory stack `index`'s item: the template entity, handy for `item_value()` of what a character carries).

### Balancing items — `risen.balance`, `risen.item.requirements / modifiers` (see `mods/balance_items`, [RE.md §23](RE.md), [ITEM_PROPERTIES.md](ITEM_PROPERTIES.md))
Where an item's numbers live: gold value `gCItem_PS.GoldValue`, weapon damage `gCDamage_PS.DamageAmount`, the **equip condition** `gCItem_PS.RequiredSkills` and the **effects** of wearing or
using the item `gCItem_PS.ModifySkills` (armor and ring protection, potion healing, attribute bonuses). The two arrays show up as `Array[n]` in the property text format, so they have their
own functions; everything else is a normal property (`risen.template.patch`).
```lua
-- declarative: one table, everything validated first (nothing is written when anything is wrong), applied to templates and live entities
local report, why, problems = risen.balance.apply({
    items = {
        It_1H_Sword    = { value = 120, damage = 25, requires = { Strength = 5 } },
        It_Armor_Guard = { requires = { Strength = 8 }, modifies = { ProtEdge = 35, ProtBlunt = 25, ProtPoint = 20 } },
        It_Po_Health_01 = { modifies = { HitPoints = 40 } },
        It_Ri_STR_Small = { modifies = { Strength = 4 }, properties = { gCItem_PS = { SortValue = 5 } } },
    },
}, { instances = true })                      -- report = { ok, applied = {...}, failed = {...}, values, instances }  |  nil, "reason", { problems }

-- at the right moments, idempotent, with a log summary (first `tick` into the templates, every `session.start` including live entities)
risen.balance.install(SPEC)                   -- the recommended one-liner of a balancing mod

-- single items (template name, Template handle or an item Entity), friendly skill names (risen.balance.skills())
risen.item.requirements("It_1H_Sword")                          -- { Strength = 10 }
risen.item.set_requirements("It_1H_Sword", { Strength = 5 })     -- replaces the whole list; {} removes every requirement
risen.item.modifiers("It_Armor_Guard")                          -- { { skill = "ProtBlunt", amount = 20, mode = "add" }, ... }
risen.item.set_modifiers("It_Armor_Guard", { ProtEdge = 35 })    -- or { { skill=, amount=, mode= } } / { ProtEdge = { amount = 5, mode = "set" } }
risen.item.set_modifiers("It_Plant_Berry", { ManaPoints = 9 }, { instances = true })   -- also the live entities of the template
local sword = risen.entity.spawn("It_1H_Sword", x, y, z)
sword:set_requirements({ Dexterity = 3 }); sword:requirements() -- one entity only: Entity:requirements/set_requirements/modifiers/set_modifiers

-- the engine's own checks and effects (used to verify a balance)
risen.item.can_use(player, "It_1H_Sword")                       -- does the character meet the item's requirements right now? (gCSkills_PS::HasRequiredSkills)
player:skill_total("ProtEdge")                                  -- total, base: current skill value incl. worn items (risen.item.skill_total)
risen.item.consume(player, "It_Po_Health_01")                   -- what using a potion does: applies the modifiers, removes one item
```
* **Skill names** (`risen.balance.skills()`): attributes `Strength`, `Dexterity`, `Intelligence`; `HitPoints`, `ManaPoints` (the *current* values: a potion with `HitPoints = 75` heals), `MaxHitPoints`,
  `MaxManaPoints`, `LearnPoints`, `Experience`, `Level`; weapon skills `Axe`, `Sword`, `Staff`, `Bow`, `CrossBow`; protection `ProtEdge`, `ProtBlunt`, `ProtPoint`, `ProtFire`, `ProtIce`, `ProtMagic`;
  magic `Missile`, `Frost`, `Fireball`, `Circle`; `Scribe`, `Trophy`, `Acrobat`, `Pickpocket`, `Lockpick`, `Sneak`, `Mining`, `Smith`, `Alchemy`. The engine suffix (`Stat_STR`) and the full enum name
  (`gESkill_Stat_STR`) are accepted too. An unknown name is an error that lists the valid ones.
* **Modifier modes** (`risen.balance.modes()`): `add` (default; the shipped value of every item), `set`, `max`, `percent`. Amounts are integers (negative allowed).
* **`items` settings**: `value`, `damage`, `requires`, `modifies`, `use_type`, `category`, `mana_cost` and `properties = { Set = { Property = value } }` (anything else, validated like `risen.template.patch`);
  an unknown setting or template is an error listing the valid settings. More sections: `npcs`, `npc_groups` and `item_groups` (see "Balancing NPCs"); `spells` and `new_spells` (see "Spells"); later features register theirs with `risen.balance.register_section`.
* **`instances`**: `risen.balance.apply`/`install` write into the live entities of each template by default (world items; **existing entities keep their own copies**, RE.md §22);
  `set_requirements`/`set_modifiers` and `risen.template.patch` default to templates only. Values written into entities are saved with the game.
* **Timing**: edits are runtime-only; `install` applies them on the first `tick` and on every `session.start`. In a savegame the world's items keep the *saved* values until a mod writes them again
  (verified: a template edited back after the save did not change the loaded world, `mods/balance_driver`).
* **Gotchas.** Only the **player** is held to `requires` (the `CanEquipHasRequieredStats` script lets NPCs equip anything; the HUD says which skill is missing). Armor and rings apply their
  modifiers to the wearer when equipped and subtract the **current** list when unequipped: change modifiers before anything is worn (first `tick`), otherwise a character that already wears the item
  drifts by the difference when it takes the item off. Potions/food apply their modifiers to the base value when used up (`HitPoints` heals, `MaxHitPoints` raises the maximum). Melee damage is
  `DamageMultiplier * (DamageAmount + the attacker's Strength)` (+50 while Berserker).

### Balancing NPCs and monsters — `risen.npc.stats / set_stats`, `risen.balance` sections `npcs`, `npc_groups`, `item_groups` (see `mods/balance_npcs`, [RE.md §24](RE.md), [NPC_PROPERTIES.md](NPC_PROPERTIES.md))
Where an NPC's numbers live: hit points, mana, **level, attributes, protection and combat skills** in the engine's `gCSkills_PS` (not reflected: the API reads and writes its
storage natively, for templates and live entities); guild, species, attitude, bravery, damage calculation in `gCNPC_PS`; the starting inventory in `gCInventory_PS`;
the fight style in the combat species of `gCCombatSystem_PS`. Derived values: a kill pays `10 * level` experience (at least 50; exactly `10 * level` for Chicken and
Nautilus), a melee hit does `weapon DamageMultiplier * (DamageAmount + attacker Strength) - victim's protection`, and animals/monsters hit with fist items of damage 0
(`It_Fist_Monster`), i.e. **a bite = Strength** (verified in a fight: Strength 30 vs Prot_Edge 20 = 10, Strength 90 = 70).
```lua
-- declarative: validated first (templates, setting names, skill names, ranges, enum names, patterns); nothing is written when anything is wrong
risen.balance.install({                                    -- or risen.balance.apply(spec): templates on the first tick, live NPCs on every session.start
    npcs = {
        Wolf = { hp_max = 500, xp = 200, skills = { Strength = 90, ProtEdge = 60 }, bravery = "Brave" },   -- absolute
        Boar = { hp_mult = 2, level_add = 2 },                                                              -- relative to the SHIPPED values
        Peasant_1_01 = { guild = "Cit", skills = { Strength = 50, Sword = 3 }, loot = { It_Apple = 3 } },
        Gnome = { properties = { gCNPC_PS = { RoleDescription = "x" } } },                                 -- any other property
    },
    npc_groups = {                                         -- every template a selector matches (Lua pattern, template group path, names)
        { match = "^Stingrat", hp_mult = 1.5, damage_mult = 2 },
        { group = "Monster/Kapitel 2", level_add = 1, skills_mult = { ProtEdge = 1.1 } },
    },
    item_groups = { { group = "Items/Potions", value_mult = 0.5 } },   -- relative gold value / weapon damage of items
})

risen.balance.scale_npcs{ filter = "Monster", hp = 1.5, damage = 1.2 }          -- one-call bulk form (filter: a template group, else a Lua pattern)
local s = risen.npc.stats("Wolf")                         -- { hp, hp_max, mana, mana_max, level, xp_reward, skills = { Strength = 30, ProtEdge = 24, ... },
                                                          --   guild, species, bravery, attitude, damage_calc, ai_group, damage, combat = { species, right_fist }, loot }
risen.npc.stats(entity); risen.npc.stats(template_handle, { base = true })
risen.npc.set_stats("Wolf", { hp_max = 300 }, { instances = true })     -- template (+ live NPCs of it); or an entity: risen.npc.set_stats(e, { level = 3 })
```
* **Settings** of an `npcs` entry / `npc_groups` entry / `set_stats` spec (all optional; numbers are **base values**, what the template stores): `hp_max`, `hp`, `hp_mult`, `mana_max`, `mana`, `mana_mult`, `level`,
  `level_mult`, `level_add`, `xp` (reward, set through the level: `level = xp / 10`; the engine's minimum is 50), `xp_mult`, `skills` / `skills_mult` / `skills_add` (`{ Strength = 40 }`, names of `risen.balance.skills()`;
  attributes 0..200, combat skills 0..10), `damage_mult` (Strength and the NPC's own `gCDamage_PS.DamageAmount`), `damage`, `damage_type`, `guild`, `species`, `attitude`, `bravery`, `damage_calc`
  (`"Immortal"`: makes the damage 0 according to the decompiled damage scripts, not tried in a fight), `combat_species`, `ai_group`, `loot = { It_Apple = 3 }`, `properties = { Set = { Property = value } }`. An unknown key lists the valid ones; an absolute and a relative
  setting of the same quantity, out-of-range absolutes (Strength 999), unknown enum names and templates that are no NPCs are errors; relative results are clamped to the skill's range (noted in the report).
* **Selectors** of `npc_groups` / `item_groups`: `group = "Monster/Kapitel 2"` (any path of [TEMPLATES.md](TEMPLATES.md); `risen.template.list`), `match = "^Wolf"` (Lua pattern on the name), `exclude = "..."`, `names = { ... }`.
  Without a selector: all templates below `NPC` and `Monster` (never `PC_Hero`, the player's template; heads and other non-NPCs are skipped). A selector that matches nothing is an error.
* **Relative settings never compound.** `*_mult`, `*_add` and `damage_mult` are computed from the values the template had *when the API first touched it in this process* and written as absolutes, so applying the same spec on the first
  tick, on every `session.start` and onto NPCs that were already saved with the first result gives the same numbers. (On a single entity, `set_stats(entity, ...)` relative settings are relative to that entity.)
* **Hit points.** `hp_max` changes the maximum; an NPC that is at full health stays at full health (its hit points follow), a **damaged or dead NPC keeps its hit points** (the engine only clamps them to a lower maximum).
  `hp` (starting hit points, `<= hp_max`) is written into templates, and into live NPCs only when they are at full health; `set_stats(entity, { hp = 50 })` on one entity is written always. Same for mana.
* **What an edit reaches** (verified in the game): new spawns at once (hit points, level, attributes, protection, guild, loot; the template edit survives the spawn's init scripts and the first AI ticks), `instances = true` (the
  default of `apply`/`install`) also every live NPC of the template (only the listed values), a world restored from a savegame keeps the *saved* values until a mod writes them again (a template edited back after the save
  did not change the loaded world), the world of a **new game** has the shipped values (propagate on `session.start`: `install` does). Values written into entities are saved with the game. `loot` reaches NPCs spawned afterwards only.
* Low level: `risen.npc.skill_raw(target, skill)` (base, modification, total, min, max; `skill` = `gESkill` 0..33), `risen.npc.set_skill_base(target, skill, value)` (template storage or the engine's `SetSkillBaseValue`),
  `risen.npc.template_inventory/template_give/template_take(name, ...)` (the inventory of an NPC template), `risen.npc.combat_species(name)` / `set_combat_species_property(name, prop, value)` (the combat species object:
  `RightFistItem` / `LeftFistItem` are the item templates monsters hit with: balance their damage with the `items` section, e.g. `It_Fist_Monster = { damage = 20 }`; shared by every monster that uses them).
* **Cost:** NPC templates are loaded lazily by the engine; a spec or `scale_npcs` that selects every NPC template (several hundred below `NPC`/`Monster`) loads them all once on first use (not timed), later applications reuse the loaded templates.
* **Not measured / open:** the effect of `set_combat_species_property` on running fights (only read back), kill experience through a real combat death (the `GiveXP` script that the kill calls was run directly: 200 for level 20),
  ranged damage chains, `Dexterity`/`Intelligence`/magic skills of NPCs (stored and clamped, no consumer checked), changing the inventory of NPCs that already exist (use `risen.item.give/take` on them).

### New templates — `risen.template.create`, `risen.balance` sections `new_items`, `new_npcs`, `templates` (see `mods/newtemplates`, [RE.md §25](RE.md))
A mod can add **new item and NPC/monster templates** at runtime: a clone of an existing template (property sets, mesh, icon, skills, inventory, ...) under a new unique name with the **ID derived from that name**,
so everything that refers to a template -- `risen.entity.spawn`, `risen.npc.spawn`, `risen.item.give`, `loot`, dialogue `Give` commands (`Item = "My_Sword"`), inventory stacks, savegames -- accepts it.
```lua
local sword = assert(risen.template.create("Spass_Flame_Sword", "It_1H_Sword", {   -- Template | nil, reason ; second result: created now (true) or overrides re-applied (false)
    name = { English = "Flame Tongue", German = "Flammenzunge" },       -- label: localization id FO_Spass_Flame_Sword (a string = every language)
    description = "A sword that feels warm to the touch.",              -- ITEMDESC_Spass_Flame_Sword (items)
    value = 900, damage = 55, requires = { Strength = 25 }, modifies = { Strength = 5 },   -- the settings of the `items` section
    mesh = "It_Wpn_1H_Club.xmsh", icon = "ic_it_1h_club",              -- another existing weapon's mesh and inventory icon
}))
risen.template.create("Spass_Dire_Wolf", "Wolf", { name = "Dire Wolf", hp_max = 600, level = 15, skills = { Strength = 80 },   -- the settings of the `npcs` section
                                                  loot = { It_Apple = 2, Spass_Flame_Sword = 1 }, mesh = "Ani_Boar_Monster_Boar.xac" })
-- declaratively: applied on the first tick and on every session.start, idempotent (check a spec with risen.balance.apply(spec, { dry_run = true })); `loot` may name items of new_items:
risen.balance.install({
    new_items = { Spass_Flame_Sword = { base = "It_1H_Sword", value = 900, name = "Flame Tongue" } },
    new_npcs  = { Spass_Dire_Wolf = { base = "Wolf", hp_max = 600, loot = { Spass_Flame_Sword = 1 } } },
    templates = { ... },                                              -- either kind, and other templates (only name / mesh / properties)
})
risen.template.id_for("Spass_Flame_Sword")   -- "c095059b-0585-7a31-efea-da3f9de97a06": MD5 of "spass.template:<name>" printed as a GUID; sword:id() reads it back
risen.template.created()                     -- { Spass_Flame_Sword = "It_1H_Sword", ... }      risen.template.remove("Spass_Flame_Sword")   -- gives the names back (see below)
sword:focus_name(), sword:description()      -- the texts the engine shows (Entity:focus_name()/description() for spawned items and NPCs)
entity:dependencies()                        -- the mesh/actor files the engine loaded for a spawned entity (verifies `mesh`)
```
* **Overrides**: for an item base the keys of the `items` section (`value`, `damage`, `requires`, `modifies`, `use_type`, `category`, `mana_cost`, `properties`), for an NPC/monster base those of the `npcs` section
  (`hp_max`, `level`, `skills`, `guild`, `loot`, `properties`, ...), for both `name`, `description` (items), `mesh` (item: `eCMesh_PS.MeshFileName`; NPC/monster: `eCAnimation_PS.ResourceFilePath`, the actor file, e.g. `Ani_Boar_Monster_Boar.xac`),
  `icon` (`gCItem_PS.IconImage`) and `material` (`MaterialSwitch`, a number). `mesh` and `icon` are **checked** against the game archives and the mounted asset directories (`check_assets = false` opts out; new files: next section). `properties` reaches everything else (e.g. `gCItem_PS = { ItemInventory = ... }`). **Everything is validated before anything is created** (names, base, setting names, types, skill
  names, conflicts such as `mesh` plus `properties.eCMesh_PS.MeshFileName`); a bad request returns `nil, reason` and creates nothing. Names: letters, digits and `_`, not starting with a digit, at most 63 characters; a shipped template name is refused.
* **Labels**: the engine shows `FO_<name>` for the focus label of an entity and for the inventory name of an item (a `focus_unique:FO_<name>` entry wins), `ITEMDESC_<name>` for an item's description. `name`/`description` set them with `risen.loc.set`;
  a mod's `strings/all.csv` with the same ids works too. A clone without `name` shows its base's label unless a mod already supplied one. A key set without table prefix (`FO_Bob`) now also answers the prefixed id the engine asks for (`focus:FO_Bob`).
* **The ID** is derived from the name, so the same mod creates the same template in every process on every machine: **savegames that contain items of it resolve again** (a stack stores the template's ID). Create the templates on the first `tick` (as
  `install` does), before a savegame is loaded. Verified live in a fresh process: items in the player's and a world NPC's inventory survive a restart; if the template is **missing** in a session the stacks stay dormant (`risen.item.count` 0, `stack_item` nil, the game
  runs, a save keeps them) and **resolve as soon as the template exists**, even when it is created after the load. Spawned entities are not saved by the engine (RE.md §14), so only inventories (and world entities that existed before) carry clones.
* **A clone is a snapshot** of its base when it is created (later edits of the base do not reach it); creating again with the same base only re-applies the overrides. Existing entities keep their own values (`instances = true` writes them). The clone lives until the
  process ends; `remove` only gives the names back (refused while live entities of it exist) -- items already handed out keep working -- and `create` with the same base brings it back with the same ID.
* **Timing**: items and NPC templates can be cloned from the first `tick` on, also while a world is loaded (a hook skips `gCNavigation_PS::OnPostCopy` during the copy: it crashed on templates once a world existed). The base is loaded on demand.
* **Not covered**: new meshes/textures/icons (see the next section), appearance parts of humans (heads are `eCBodyPart_PS` item templates `Hero_Head_*`, not part of the NPC template), multi templates (refused).

### Spells -- `risen.spell`, `risen.balance` sections `spells` and `new_spells` (see `mods/balance_spells`, `mods/example_spell`, [RE.md §29](RE.md), [SPELL_PROPERTIES.md](SPELL_PROPERTIES.md))
There is no spell object: a spell is a group of **items** and scripts. **Projectile spells** (`Fireball`, `Frost`, `Missile`) are cast with a *crystal* (`It_Crystal_Fire`, ...) and shoot a *projectile template*
(`It_Projectile_Fireball_S0`: damage, speed, radius); every other spell (`Light`, `Inferno`, `Skeleton`, the heals, ...) is a *rune* (permanent), *scroll* and *orb* (used up). Mana cost, requirement and "used up" live in the
item's `gCCastInfo_PS`, the damage in the projectile. `risen.spell.names()` lists the 21 shipped spells, `risen.spell.get("Fireball")` shows the numbers the templates hold now.
```lua
-- modify the shipped spells (validated first, nothing is written when anything is wrong; relative *_mult settings never compound)
risen.balance.install({
    spells = {
        Fireball = { mana_cost = 12, damage = 150 },                    -- crystal 8 -> 12 mana, projectile 100 -> 150 damage (a list sets one value per level: damage = { 100, 120, 150 })
        Frost    = { damage_mult = 2, speed_mult = 1.5, requires = { Frost = 2 } },
        Light    = { mana_cost = 7, scroll_mana_cost = 3 },              -- rune spells: rune, scroll and orb have their own costs
        Skeleton = { mana_mult = 0.5 },                                  -- rune and scroll cost = shipped * 0.5 (an orb stays free)
    },
    new_spells = {                                                       -- new spells from the pieces of shipped ones (new item templates, savegame-safe IDs)
        Spass_Fireball_Strong = { base = "Fireball", name = { English = "Strong Fireball", German = "Starker Feuerball" }, mana_cost = 24,
                                  requires = { Fireball = 2 }, damage_mult = 2, forms = { "crystal", "scroll" } },
        Spass_Skeleton_Cheap  = { base = "Skeleton", mana_cost = 15, requires = { Circle = 2 }, forms = { "rune", "scroll", "orb" }, scroll = { mana_cost = 7 } },
    },
})
risen.spell.create("Spass_Frost_Heavy", { base = "Frost", damage = 90 })  -- the one-spell form (also enables the per-shot overrides for this mod); risen.spell.modify(name, settings) likewise
```
* **`spells` settings** (all optional, an unknown key lists the valid ones): `mana_cost`, `mana_mult` (crystal, rune and scroll; excludes `mana_cost`), `scroll_mana_cost`, `orb_mana_cost` (rune spells), `requires` (crystal/rune, e.g. `{ Fireball = 2 }` or `{ Circle = 3 }`),
  `consume`, `properties` (any other property of the crystal/rune); projectile spells only: `damage` (number or one per projectile level; `damage_mult`), `damage_type` (`"Magic"`, `"Fire"`, `"Ice"`, ...), `speed`/`speed_mult` (`StartVelocity`),
  `radius`/`radius_mult` (`SweepTestRadius`), `effect` (the flying effect), `projectile_properties`. A projectile setting on a rune spell (and a scroll/orb cost on a projectile spell) is an error.
* **`new_spells` settings**: `base` (required: a name of `risen.spell.names()`), `forms` (projectile spells: `crystal`, `scroll`; rune spells: `rune`, `scroll`, `orb`; default the main form), the item settings of the main form (`name`, `description`, `icon`, `value`,
  `mana_cost`, `requires`, `consume`, `properties`, `mana_mult`), per-form tables (`crystal`, `rune`, `scroll`, `orb` with the same item settings) and, for projectile spells, the projectile settings above. Template names: crystal/rune `<name>`, scroll `<name>_Scroll`, orb `<name>_Orb`
  (clones made with `risen.template.create`: the deterministic ID makes savegames with them load again; create them on the first `tick`, like `install` does). A *scroll of a projectile spell* is the crystal used up by its cast (`consume`, no requirements, half the cost, a fifth of the value by default).
* **What reaches the engine** (verified live with the player casting through the engine's own state scripts): the mana taken is exactly `ManaCost`; the projectile that is spawned carries `DamageAmount`; the engine's hit script takes `DamageAmount + 25 * Fireball skill - protection`
  (Frost/Missile add 15 per level; see SPELL_PROPERTIES.md); crystals and scrolls are equipped into slot 11 (`item.equipped`), runes and scrolls of rune spells into slot 10 (inferred from the decompilation); `risen.item.can_use` is the engine's requirement check.
* **Per-shot overrides.** The engine picks the projectile template per spell *type* (a crystal in the hand of a Fireball caster always shoots `It_Projectile_Fireball_S0`), so the projectile settings of a **new** projectile spell are written into the projectile *entity* of each of its shots
  (`entity.spawned` of the projectile, its shooter's equipped crystal decides; relative `*_mult` are relative to what that projectile carries, i.e. including a `spells` edit). `risen.balance.install` with `new_spells` and `risen.spell.create` register the handler for the calling mod (`risen.spell.enable_overrides()`).
  The same handler removes the **last scroll** of a projectile spell after its shot: the engine's `PSCastInfo::Consume` unequips only from the rune slot, so a scroll cast from the magic slot would keep one item forever.
* **Natives**: `risen.spell.equipped(who [, slot])` (template in an equip slot, default 11), `can_cast(caster, item)` (the engine's `PSCastInfo::CanCast`), `consume(caster, item)` (`PSCastInfo::Consume`: takes the cost and, for scrolls of rune spells, one item), `discard(owner, item)`, `shooter(projectile)`, `overrides()`.
* **Casting from a script** (what the tests do): `risen.item.equip(player, crystal)`, `player:set_state("PS_Magic")`, wait until `player:state() == "PS_MagicBall_Loop"` (about 4.5 s), then `player:set_state("PS_MagicBall_Shoot")` is one real shot (~1 s); the mana, the `entity.spawned` projectile and `entity.damaged` follow.
* **Not covered / limits**: numbers that are constants in the game's scripts (heal amounts 50/100/150, Fireball area radius 100/200/300 and the +25 per Fireball level, the Protection/Berserker durations) are not template data -- they are listed in SPELL_PROPERTIES.md and can only be changed by overriding the script;
  a projectile spell has no rune (the crystal is the only item the combat move accepts); an NPC shooter was not driven live (the same scripts, so the same numbers are inferred); the visual effect of a changed `effect`/`radius` was not looked at (screenshots are black); the *effect* of a rune spell (heal, summon) was not driven live, only its item numbers and the engine's cost accounting.

### New meshes, textures and icons — `risen.assets` (see `mods/newassets`, [RE.md §26](RE.md))
A mod can bring its own **loose resource files** and use them for new items and creatures. The engine finds a resource by the bare, case-insensitive name of the *compiled* file in a cache of everything that is mounted
(`X.xmsh` -> `X._xmsh`, `X.tga`/`.dds`/`.ximg` -> `X._ximg`, `X.xmat` -> `X._xmat`, `X.xac` -> `X._xmac`), so a file only has to sit in a mounted directory (any sub-directory):
* `<mod>/assets/` is mounted at startup (`mod.toml`: `assets = "assets"` is the default, `assets = "files/res"` another directory, `assets = false` none).
* `risen.assets.write(relpath, data)` writes into `<Risen>\bin\spass\generated\<mod>\` (the one place a sandboxed mod may write; mounted at startup in every later process): for assets a mod derives itself.
* `risen.assets.mount("some/dir")` mounts another directory of the mod at run time (no restart needed; `risen.vfs.mount` does the same for any path).
A loose file with the name of a shipped file **shadows** it (retexturing: ship `Item_Apple._xmsh` and the apple looks different; effective for resources not loaded yet).
```lua
-- 1. files (here derived at run time from shipped ones: nothing of the game has to be redistributed; see mods/newassets for the whole recipe)
local club = assert(risen.assets.read("It_Wpn_1H_Club.xmsh"))                 -- bytes of a shipped resource, by its property name
assert(risen.assets.write("meshes/Spass_Club._xmsh", club))                   -- or: ship <mod>/assets/meshes/Spass_Club._xmsh
-- 2. icons: an XML image list + its texture, or a whole image file
assert(risen.assets.load_icons("Spass_Icons"))                                -- Spass_Icons.ximl (+ Spass_IconAtlas._ximg) found like any asset; names its IC_* images
risen.assets.load_bitmap("Spass_Icon.ximg")                                   -- a single image file as bitmap named by its file name
-- 3. templates (on the first tick): names are checked, the request is refused with nil, reason for a name the engine cannot resolve
local t, why = risen.template.create("Spass_Club2", "It_1H_Club", { mesh = "Spass_Club.xmsh", icon = "IC_Spass_Club" })
risen.template.create("X", "Wolf", { mesh = "Spass_Wolf.xac" })               -- an NPC's `mesh` is its actor file (.xac, compiled ._xmac)
risen.template.create("Y", "It_1H_Club", { mesh = "Spass_Nowhere.xmsh" })     -- nil, "mesh: no file 'Spass_Nowhere._xmsh' in the game archives or ..."
risen.template.create("Z", "It_1H_Club", { mesh = "Spass_Nowhere.xmsh", check_assets = false })   -- opt out: the name is used as given
risen.balance.install({ new_items = { Spass_Club3 = { base = "It_1H_Club", mesh = "Spass_Club.xmsh", icon = "IC_Spass_Club" } } })   -- the sections accept the same keys (and check_assets)
```
| Function | Notes |
|---|---|
| `risen.assets.info(name)` | `{ name, compiled, kind, found, virtual, physical, loose, source }` for a property-style name (`X.xmsh`); `source` is `"game"`, `"<mod>:assets"`, `"<mod>:generated"`, `"<mod>:runtime"` or `"loose"` |
| `risen.assets.exists(name)`, `compiled_name(name)`, `read(name)` | `read` returns the bytes of the resolved file (packed or loose) for deriving new assets |
| `risen.assets.check(kind, name)` | what `template.create` runs: `kind` `"mesh"`, `"actor"`, `"image"`, `"material"` or `"icon"`; `true, info` or `nil, reason` |
| `risen.assets.resource(name)` | the engine's resource object: `{ exists, state, path }`, `state` 3 = data loaded (an entity that uses it must exist; wait a moment after spawning): the proof that a file was really loaded; a missing file gives state 0 |
| `risen.assets.write(rel, data)`, `remove(rel)`, `clear()` | files below `<spass>\generated\<mod>`; `rel` has no `..`, drive or empty parts; the directory is remounted lazily (`refresh()` does it at once) |
| `risen.assets.mount(reldir)`, `mounted()` | directories mounted for mods |
| `risen.assets.load_icons(list)` | registers `<list>.ximl` (XML image list) as GUI bitmaps; `load_image_list`, `load_bitmap(file.ximg)`, `bitmap(name)` (`{ attached }`) are the engine calls behind it |
| `risen.assets.dump_collection(name, rel)` | development aid: writes a stock collection (`IMC_Icons`) as XML to `<spass>\generated\_dump\<rel>` to show the `.ximl` format |
| `risen.vfs.lookup(file_name)`, `lookup_physical(file_name)` | the engine's name cache: bare compiled file name (`It_Wpn_1H_Club._xmsh`) -> virtual / physical path |
* **Chain**: a mesh names its material (`ItWpn_SwordMisc_01_Diffuse_01._xmat`), a material its images (`...tga`), so a new texture on a new item means a new mesh file that names a new material that names the new image (all three in the mounted directory). The names are stored as strings with length prefixes inside
  the files: **rename to a name of the same length** (`mods/newassets` shows how) or use a tool that rewrites the file. `material = n` in `template.create` is only the *switch number* of the engine's material variants, not a name.
* **Icons**: `icon` is a GUI bitmap name (case-insensitive): shipped icons are `IC_It_...` (cells of `GUI_IconSets01`); a mod registers its own with an image list (one texture, many named rectangles) or a whole image file (scaled into the slot). `risen.assets.load_icons` / `load_bitmap` must have run before `create` (the first `tick` is early enough, the GUI exists then).
  The `.ximl` format is `<ImageList ClassName="eCGuiImageList2" ImageSize="0; 0" TextureName="Spass_IconAtlas.dds"><Images><Image ClassName="eCGuiImage2" ImageName="IC_Spass_Club" RectOnTexture="123; 534; 163; 574" BlendMode="eEImageBlend_AlphaBlend"></Image></Images></ImageList>`
  (rectangle = `left; top; right; bottom`); the texture is `Spass_IconAtlas._ximg`.
* **File formats** (RE.md §26): every `._x*` file is `GR01` + a content tag (`MS02` mesh, `SM01` material, `IM04` image) + header + a property stream; an image's payload is an ordinary DDS (DXT1 or A8R8G8B8) inside. There is no exporter here: take files from the game (`risen.assets.read`, or `risen.vfs.read` for any path), from the game's own tools,
  or derive them as the example does (rename within a file, recolour the DDS payload of an image).
* **Verified live**: loose mesh, material, texture and actor loaded (resource `state` 3 / 1), items and a creature spawned with them, icons registered from an image list and from a single image file, shadowing a shipped name and undoing it, startup mounts of `assets` and `generated`, the refusal of unknown names.
  **Not verified**: how it looks on screen (screenshots of the D3D9 window are black), names of another length inside a patched file, the inventory drawing of a new icon, replacing a resource the engine has already loaded.
* Entity **templates themselves** are still not loadable from files (RE.md §17); the new templates come from `risen.template.create`.

## Voice lines — spoken dialogue in several languages (`risen.voice`, `risen.assets.import_sound`; see `mods/example_voice`, [RE.md §28](RE.md))
A `Say` line of a dialogue can be **voiced** with audio files of your mod: Ogg Vorbis, WAV (PCM or float, any rate) or MP3, mono or stereo. The line lasts as long as the audio (a line without audio waits a few
seconds by the length of an internal name), the subtitle shows as usual, and each language can have its own file. The layer compiles the files into the engine's own sound format (`._xsnd`, in `<Risen>\bin\spass\generated\<mod>\voice`;
an identical file is never written twice) — no tool is needed. A file that is not usable is refused with `nil, "reason"` (naming the file) before anything is created.
```lua
-- 1. in the dialogue: `sound` on a Say line (one file, or one per language), `text` per language
risen.dialog.create({
    name = "Hob_Greet", owner = "Hob", permanent = true,
    lines = {
        { speaker = "npc", listener = "player", text = { default = "Welcome!", de = "Willkommen!" },
          sound = { en = "voice/en/greet.ogg", de = "voice/de/greet.ogg" } },     -- or sound = "voice/greet.ogg" for every language
        { command = "SaySound", Speaker = "npc", sound = "voice/cue.mp3" },       -- sound only, no subtitle; the language is chosen when the option is created
    },
})
-- 2. or by directory convention: files named after the text id of the line (`<option name>_<line number>`), one folder per language
--      voice/English/Hob_Greet_1.ogg   voice/German/Hob_Greet_1.ogg   voice/Hob_Greet_1.wav (= every language)
risen.voice.load_dir("voice")            -- before risen.dialog.create; returns the number of keys and files, or nil, reason
-- 3. audio made by Lua (`{ data = bytes }`) and a bark outside of any dialogue
risen.voice.set("Hob_Warning", { default = { data = wav_bytes } })
risen.voice.say(risen.entity.get("Hob"), "Hob_Warning")      -- true; risen.voice.speaking(npc) is true while it plays
```
* **Which variant plays**: the voice language of the game (`VoiceLanguage` in the options; `risen.voice.language()` -> `"English"`, `"German"`, `"French"`, `"Russian"`), else the `default` entry, else English, else the first one by language name. Language keys are the engine's names or codes
  (`en`, `de`, `fr`, `ru`, `pl`, ...; `risen.voice.normalize`). The subtitle language is separate (`risen.loc.language()`): give `text = { default = ..., de = ... }` per language, or use `strings/<Language>.csv`.
* **Text ids**: `risen.dialog.create` names the text of line `n` of option `name` `<name>_<n>` (that is also the key for `risen.voice.set` and for the file names of the convention). Voice any id the engine speaks — also shipped ones (`risen.voice.set("INFO_FILLERX2_00007314", ...)` re-voices that line; `-Trace voice` lists
  every id the engine asks for, with the title it composed).
* **Register on `load.before` and `session.start`** like dialogues (the registry lives in the process; everything is idempotent and cheap: files are probed again, nothing is rewritten).
* **Shadowing a shipped sound**: `risen.assets.import_sound("INFO_KALVERAMX2B_00000556_Druid_English_%_Eldric.wav", "voice/x.ogg")` makes the engine take your audio for that shipped line, as long as the sound has not been loaded yet (lines of the hero are prefetched at the start). The stored `Duration` stays the shipped one; playback is your audio.
* **Lip sync**: shipped lines carry an EMotionFX `._xmot`; yours have none, so the face does not move (no error).

| Function | Notes |
|---|---|
| `risen.assets.import_sound(name, source)` | compiles an audio file into `<name>._xsnd` (`name` = bare file name, `Spass_Hello.wav`; the extension is a label) and mounts it: `name` then works as `SoundName` of a `SaySound` command, for `risen.voice.say`, or as a shadow of a shipped sound. `source`: a file of the mod (relative path) or `{ data = bytes }`. Returns `{ name, compiled, format, channels, sample_rate, bits, duration_ms, bytes, written }` or `nil, reason` |
| `risen.voice.set(key, variants)` | registers audio for a text id; `variants`: a file / `{ data = }`, or `{ en = file, de = file, default = file }`. Returns `{ key, variants = { [Language] = import info } }` or `nil, reason` (nothing changes) |
| `risen.voice.check(variants)` | the same validation without writing |
| `risen.voice.load_dir(dir)` | `<dir>/<language>/<key>.<ogg\|wav\|mp3>` and `<dir>/<key>.<ext>` (default) -> `set` for every key; returns `keys, files` |
| `risen.voice.get(key)`, `keys()`, `remove(key)`, `title(key)` | the entry (`variants` by language with title, compiled name, source, duration; `requests` = how often the engine asked), the keys, forget, the raw sound name for the current language (for `SaySound`) |
| `risen.voice.say(entity, key_or_sound[, { type = n }])` | a bark: starts a registered key or any sound name the engine finds as the entity's speech output (no animation or subtitle); `type` 0 NPC (default), 2 the player, 3 comment. `false` when it cannot start |
| `risen.voice.speaking(entity)`, `stop(entity)` | whether a speech sound plays; stops it |
| `risen.voice.language()`, `normalize(name)`, `last()`, `title_calls()` | current voice language; `"de"` -> `"German"`; `{ key, title, language }` of the last registered key the engine asked for (the proof that a spoken line used your sound); the number of titles the engine asked for |
| `risen.assets.sound(name)` | what the engine and FMOD made of a sound: `{ state, duration_ms, flags, fmod, fmod_length_ms, channels, bits, sample_rate, type }` (`state` 3 = loaded when it is first played; `fmod` true = playable; `type` 10 Ogg Vorbis, 9 MPEG, 15 WAV) |
* **Verified live** (`ex_voice`, English and German voice language): the engine asks `GetSampleTitle` for the line's text id and takes the registered variant of the current language; ogg, mp3 and wav (8, 16 and float32 bit) are found, opened, loaded and played by FMOD; an option with a 0.4 s voice takes 53 frames against 467 for the same line without voice; the speaking state follows the audio; a bad file refuses the whole `create`;
  a loose sound replaces a shipped NPC line; the user heard the test tones. **Not verified**: lip sync, stereo or other rates (FMOD opens them: untested), what the voices sound like on other hardware.
* **Limits**: formats are recognised by content (Ogg Opus, FLAC, M4A are refused with a hint: convert with e.g. `ffmpeg -i in.flac -c:a libvorbis -q:a 4 out.ogg`, ffmpeg is not needed otherwise); ids are case-insensitive; `SaySound` has no language switch at run time (the variant is chosen when the option is created); a long file is read into memory whole.

## NPCs — `risen.npc.spawn` and the Entity NPC methods
Spawn a template and configure it in one call (see `mods/npcdemo`):
```lua
local bob = risen.npc.spawn("Peasant_1_01", x, y, z, {
    name = "SPASS_Bob",            -- unique entity name; also the key for its label: "FO_SPASS_Bob"
    level = 10, hp_max = 400,      -- hp defaults to hp_max
    mana = 100, guild = "Cit", group = "SPASS",   -- guild: number or name; `peaceful = true` = citizen (nobody attacks them)
    items = { {"It_1H_Machete", 1} }, equip = true,
    state = "ZS_HangAround",       -- initial AI state (the AI may switch states immediately)
})
risen.loc.set("FO_SPASS_Bob", "Bob the Lua Peasant")   -- text shown when the player focuses him
```
Entity methods (every call returns `nil`/`false` instead of crashing on a bad handle):

| Method | Notes |
|---|---|
| `guild()`, `set_guild(n)` | numbers of the `gEGuild_*` enum: `0` None, `1` Don, `2` Dig (diggers), `3` Grd (guards), `4` Cit (citizens), `5` Inq, `6` Mag, `7` Pir (`risen.npc.guilds`). Pirates attack Don/Grd/Inq/Mag but leave Cit/Dig/None alone |
| `get_property(set, name)`, `set_property(set, name, value)` | any property of a property set by name, e.g. `e:get_property("gCNPC_PS", "Guild")` -> `"gEGuild_Pir"`, `"AttitudeLock"`, `"Species"`, `"GuardStatus"`, `"BraveryOverride"` |
| `group()`, `set_group(s)` | free-form group name used by AI to find allies |
| `hp()`, `hp_max()`, `level()`, `mana()`, `mana_max()` | |
| `set_hp_max(n)`, `set_hp(n)`, `set_level(n)`, `set_mana(n)`, `set_mana_max(n)` | absolute (the engine setters are additive; the wrapper computes the difference). Set `hp_max` before `hp` |
| `add_hp(d)`, `add_hp_max(d)`, `add_level(d)`, `add_mana(d)`, `add_mana_max(d)` | relative |
| `state()`, `set_state("ZS_…")` | AI state scripts: `ZS_Ambient`, `ZS_AmbientObserve`, `ZS_AmbientSleep`, `ZS_HangAround`, `ZS_Housewife`, `ZS_Observe`, `ZS_Watch`, `ZS_Study`, `ZS_Smith`, `ZS_Trader`, `ZS_Talk`, `ZS_FollowPlayer`, `ZS_GuidePlayer`, `ZS_WaitForPlayer`, `ZS_Attack`, `ZS_Dead`, `ZS_Monster`, … |
| `full_stop()` | cancel the current AI action |
| `goto_pos(x,y,z[,mode])`, `goto_entity(e[,mode])`, `stop_goto()`, `turn_to(e)` | `mode` is the engine's walk mode enum (0 default) |
| `equip_all()`, `stack_count()`, `stack_item(i)` | equips every equippable inventory stack; number of stacks; the item of stack `i` (a template entity, see "Editing templates") |

**Persistence:** entities created at runtime are **not** part of a savegame (verified in-game): after a load they are
gone and must be spawned again. Use the guard pattern on `session.start` and `load.after`:
```lua
local function ensure() if not risen.entity.get("SPASS_Bob") then risen.npc.spawn(...) end end
risen.on("session.start", ensure)
risen.on("load.after", function(_, ok) if ok then ensure() end end)
```

## Dialogues — `risen.dialog` (see `mods/dialogdemo`)
A dialogue option is an *info* owned by an NPC (by entity name). Its lines are commands; the text of the **first
line is the label** shown in the dialog list. Texts are stored in the localization overlay automatically.
```lua
local info = risen.dialog.create({
    name  = "SPASS_Gus_Work",            -- unique info name
    owner = "SPASS_Gus",                 -- NPC entity name
    -- type = "Info" (default) | "Important" | "Comment" | "News" | "Refuse" | "Parent"
    -- condition = "General" (default) | "Running" | "Success" | "Failed" | "Open" | "Hello" | "End" | "Back" | "Trade" | ...
    -- quest = "Q_Name"                 -- for quest-state conditions
    lines = {
        { speaker = "player", listener = "npc", text = "Do you have any work for me?" },
        { speaker = "npc", listener = "player", text = "Walk around, then come back." },
        { script = "MyScript" },                       -- runs a script made with risen.script.define
    },
})
```
A `Say` line can be **voiced**: `sound = "voice/hello.ogg"` (or a table per language, or the files `voice/<language>/<option>_<n>.ogg`), and `text` may be a table per language, see "Voice lines" below.
Speaker/listener keywords: `"player"`, `"npc"` (the other party), or an entity name. An info disappears after it was
used unless `permanent = true`. Condition names are the engine's `gEInfoCondType_*`; **quest-state conditions only work
with an existing quest** (`quest = ...`).

`risen.dialog.create` validates the whole spec *before* touching the engine and returns `nil, "reason"` (also logged as a
warning) for: an unknown type/condition, a quest-state condition without `quest`, a `quest` that does not exist yet
(the engine would block the game with a message box), an unregistered `script`, empty `lines`, non-string texts.
It is **idempotent**: an info that already exists is returned as is, so the call can sit in a "create if missing" block.

**Engine commands as lines:** a line `{ command = "<Name>", <Parameter> = value, ... }` adds any dialogue command by name with its parameters
(names and types: [PROPERTIES.md](PROPERTIES.md)): `{ command = "Give", Entity1 = "npc", Entity2 = "player", Item = "It_Apple", Amount = 3 }`,
`{ command = "GiveXP", Amount = 50 }`, `{ command = "SetGameEvent", GameEvent = "MyMod_Done" }`, `{ command = "ShowSubtitle", text = "..." }`.
Unknown commands/parameters are refused with `nil, "reason"` before anything is created (see `mods/dialogcmds`). `info:list_properties()`,
`info:list_command_properties(i)`, `entity:list_properties(set)` show what is available on a live object.

**Branching:** `parent = "<info name>"` makes an option a child of another option of the same NPC. After the parent's lines finished, the menu shows the children
(and keeps showing them after a permanent child ran) until a child runs the `Back` command (`{ command = "Back" }`) or `End`. The parent itself is a normal option
(`type` `"Info"`); `risen.dialog.create` links parents and children for you (`risen.dialog.rebuild`). See `mods/dialogbranch`.

Other calls: `risen.dialog.start(player, npc)` (open the conversation), `risen.dialog.available()` (options currently
offered: list of `Info`), `risen.dialog.is_running()`, `risen.dialog.has_infos(npc)`, `risen.dialog.get(name)`,
`risen.dialog.rebuild()`. `Info` methods: `name()`, `owner()`, `run()` (only works for infos the running conversation
already knows), `available()`, `set_property(name, value)`/`get_property(name)` (any engine property by name),
`add_command("Say"|"Description"|"End"|"Back"|"SetGameEvent"|"ClearGameEvent"|"GiveXP"|"Give"|"RunQuest"|"SucceedQuest"|
"CloseQuest"|"Attack"|"MoveTo"|"StartEffect"|"SetRoutine"|"DoTrade"|"Heal"|"FullStop"|"InfoWait"|"RunInfo"|"SaySVM"|
"SaySound"|"ShowSubtitle")`, `add_script_command()`, `set_say(i, speaker, listener, key)`, `set_script(i, name)`.

## Quests — `risen.quest.define` / `add_log` (see `mods/questdemo`)
```lua
local q = risen.quest.define({ name = "Q_MyQuest", title = "My Quest", type = "Report", xp = 100 })
risen.quest.open("Q_MyQuest"); risen.quest.run("Q_MyQuest")        -- status Open -> Running
risen.quest.add_log("Q_MyQuest", "Talk to Gus again.")             -- quest log entry
risen.quest.succeed("Q_MyQuest")                                   -- or fail / close
q:get_property("Status")                                           -- "gEQuestStatus_Running", ...
```
Quest types (`gEQuestType_*`): `Report` (talk), `Kill`, `HasItems`, `EnterArea`, `BringNpc`, `FollowNpc`, `DriveAway`,
`Defeat`, `Duel`, `Arena`, `Plunder`, `Sparring`. Also: `risen.quest.get(name)`, `exists`, `count()`, `at(i)`, `remove(name)`;
`Quest` methods `name()`, `set_topic(key)`, `add_log_entry(key)`, `log_count()`, `log_keys()` (the engine also appends the speakers' labels when a quest-bound option runs), `set_property/get_property`, `register()`.
`risen.quest.define` / `add_log` validate their arguments and return `nil, "reason"` / `false, "reason"`; `define` is
idempotent (an existing quest is returned unchanged).

**Quests, infos and savegames** (verified in-game): the engine saves a quest's state (status, log entries) and an info's
"given" flag *by name* and restores it while it reads a savegame – but only into objects that **already exist** at that
moment. Define quests and infos in a `load.before` handler (a fresh process starts without them), and again on
`session.start` (a new game resets the quest manager and removes runtime quests; runtime infos survive). Because
`define`/`create` are idempotent, the same function can serve both events:
```lua
local function content()
    risen.quest.define{ name = "Q_Mine", title = "Mine", type = "Report" }
    risen.dialog.create{ name = "Mine_Hello", owner = "Bob", condition = "Running", quest = "Q_Mine", lines = { ... } }
end
risen.on("load.before", content)
risen.on("session.start", content)
```

## risen.quest / risen.gamevar / risen.world
```lua
risen.quest.open|run|close|fail|succeed("Quest_Id")   risen.quest.is_running("Quest_Id")
risen.gamevar.get("Name") -> int|nil                   risen.gamevar.set("Name", 1)
risen.gamevar.exists("Name")   risen.gamevar.remove("Name")      -- dialogue commands SetGameEvent / ClearGameEvent create / remove a variable
risen.world.is_game_running()  risen.world.new_game()  risen.world.quick_load()
risen.world.quick_save()  risen.world.auto_save("name")  risen.world.is_save_allowed()
```
`new_game`, `quick_load`, `quick_save` and `auto_save` are *requests*: the engine performs them on its next processing step
(watch `save.after` / `load.after` / `session.start`). `is_save_allowed()` is false while a conversation is open; a save requested then crashed the game when the conversation had run a `Give`
command (and so did the next new game): finish the conversation first (an `End` command as the last line of the info, or let the player leave it).

## risen.loc — localization overlay
```lua
risen.loc.trace(true)                                 -- log every string id the game reads
risen.loc.set("GO_QuestRunning", "Quest läuft", "German")   -- optional language; default: all languages
risen.loc.get("GO_QuestRunning"), risen.loc.exists(id), risen.loc.clear(id), risen.loc.language()
```
Ids are matched case-insensitively. A prefixed id the engine asks for (`focus:FO_Bob`, `item:ITEMDESC_Sword`) also finds a text set without the table prefix (`FO_Bob`). The game's own tables are never modified (they are read-only at runtime).

**String files:** instead of calling `risen.loc.set` for every text, put them into CSV files (see `mods/stringsdemo`):
`strings/all.csv` (every language) and `strings/<Language>.csv` (`English.csv`, `German.csv`, ...; wins over `all.csv`) of every mod are
loaded before its `main.lua` runs. UTF-8, one `id;text` per line (`,` also works), fields may be quoted (`""` for a quote, may span
lines), `#` starts a comment line, a header row `id;text` is skipped. `risen.loc.load_file("extra/more.csv" [, "German"])` loads further
files from the mod directory (returns the number of strings, `nil` if missing).

## Persistence — `risen.save`
The Lua state is not part of a savegame, so mods keep their data in a per-mod store that is written next to every
savegame and restored when that savegame is loaded (`<Risen>\bin\spass\saves\<savegame name>.lua`). A new game starts
with an empty store; a failed load keeps the previous data.
```lua
risen.save.set("kills", risen.save.get("kills", 0) + 1)       -- key: string or number
risen.save.get("kills", 0)   risen.save.remove("kills")   risen.save.keys()   risen.save.clear()
risen.save.set("visited", { "Harbor", "Mine" })               -- booleans, numbers, strings, acyclic tables of those
```
`set` raises an error for functions, userdata, cyclic tables and non-finite numbers. The data of the loaded savegame is
already visible in `load.before` (so content can depend on it); `save.before` is the place for a last update. The store is
namespaced by mod id. Sidecar files of deleted savegames are not removed.

## risen.vfs / risen.debug — development aids
```lua
risen.vfs.physical("#G3:/save")       -- physical path of a virtual path (nil when unknown)
risen.vfs.exists("#G3:/save/QuickSave.save")
-- the engine's own view of the files, including everything inside the packed archives (docs/RE.md §16)
risen.vfs.dirs("data/common")                    -- sub-directory names
risen.vfs.find("data/common/templates/Items", "*")   -- file names matching a pattern ("*.dll", "It_*", ...)
risen.vfs.find_ext("ini", "xml")
risen.vfs.walk("data/common/templates", "*.tple")   -- recursive: every file below a directory as "dir/sub/name"
risen.vfs.mount("mymod", mod.dir)     -- mount a physical directory (files appear under mymod/... and in the name cache that resources are found in; a loose file shadows a packed one of the same name)
risen.vfs.lookup("It_Wpn_1H_Club._xmsh")        -- the engine's name cache: bare compiled file name -> virtual path (lookup_physical: path on disk)
risen.vfs.unmount("mymod")
risen.vfs.read("ini/loc.ini" [, max_bytes])      -- file contents as a string (nil if it cannot be opened); default limit 16 MiB
risen.vfs.size(path)                             -- unreliable for packed files (often 0): read instead
```
`mods/vfsprobe` crawls the tree and logs it. Paths are mount-relative (`ini`, `scripts`, `data/...`) or start with `#G3:/`.
```lua
risen.debug.trace_files("SaveGames")  -- log every file the engine opens whose path contains that text; "" stops
```

## risen.engine — what the layer found in the running game
```lua
risen.engine.missing()   -- { "symbol (required|optional)", ... }: engine functions that could not be bound (their features are off)
risen.engine.symbols()   -- { symbol = true|false }
risen.engine.build()     -- { ["Game.dll"] = { timestamp = <PE link time>, size = ..., base = "0x..." }, ... } (include it in bug reports)
```

## Items
```lua
local sword = risen.entity.spawn("It_1H_Sword", x, y, z)
sword:item_value()  sword:item_amount()  sword:set_item_amount(3)  sword:item_icon()  -- "ic_it_1h_sword"
sword:item_category()  sword:item_use_type()  sword:is_item_valid()  sword:is_mission_item()
```

## Files
Log: `<Risen>\bin\spass\spass.log`, config: `<Risen>\bin\spass\config.toml`
(`enabled`, `log_level`, `[lua] unsafe`, `[debug] trace_files` (log the engine's file opens from process start), `[debug] app_data` – redirects the game's `%LOCALAPPDATA%`/`%APPDATA%`; `[debug] instance = "<id>"` – gives the engine's single-instance mutex `GenomeMutex` the name `GenomeMutex_<id>` so several test copies can run at once (see DEVELOPMENT.md "Parallel instances");
`scripts/test-run.ps1` uses it to keep a test copy's savegames and `ConfigUser.xml` away from the real ones).
