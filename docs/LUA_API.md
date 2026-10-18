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
# [retire]                      # content older versions made and this one does not (the same lists as risen.retire: npcs, quests, game_vars, items, save_keys; see "Updating and removing content")
```

The Lua layer starts right after the game's script DLLs are loaded (`gCScriptAdmin::PostInitialize`), before any
world is running. `main.lua` runs once at that moment: **register handlers there, do not expect a running world**.

Every mod gets its own sandboxed environment: base functions, `string`, `table`, `math`, `bit` and
`os.{time,clock,date,difftime}`; no `io`, `ffi`, `debug`, `package`, `dofile`, `loadfile`, or bytecode loading.
`require("a.b")` loads `<mod dir>/a/b.lua` inside the same sandbox. `print` writes to the log.
`config.toml` `[lua] unsafe = true` lifts the restrictions for development.

Globals: `mod` (`id, name, version, author, dir`, and `reload` / `retire` when they apply), `risen`.

**Argument types are checked.** Every function of the API checks the type of its arguments before it does anything: a wrong one (`risen.item.count({}, "It_Apple")`, `entity:set_frozen(1)`, a numeric string
where a number is wanted, a fractional number where an integer is wanted) raises an ordinary Lua error that `pcall` catches, and an optional parameter given a wrong type counts as absent. Where an input is merely
unusable (an unknown name, a missing entity, an entity that was deleted) the functions return `nil, "reason"` / `false` instead. No input from Lua may crash or hang the game; the scenario `hardening`
calls every function with ~1.6 million wrong or hostile argument combinations on every smoke run (DEVELOPMENT.md "Hardening"). An `Entity` handle to an entity the engine has since deleted answers like a missing one (`address == 0`, every call returns its nil/false answer).

**Editor support:** [`types/risen.d.lua`](../types/risen.d.lua) describes every function below for LuaLS (completion, hover docs, type
checks); see [`types/README.md`](../types/README.md).
**Examples:** sixteen small, live-verified example mods (patching, balancing, materials/textures, custom NPC, fetch/kill quests, completion triggers, quest tracker, voiced dialogue, spells, recipes, NPCs that appear and disappear, dialogue conditions, routines, AI states) are indexed in [`EXAMPLES.md`](EXAMPLES.md); the showcase [`mods/balance_example`](../mods/balance_example/README.md) combines them in one mod and [`MODDING_GUIDE.md`](MODDING_GUIDE.md) is the task-oriented guide.

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
| `gamevar.changed` | `name, value, previous` | a game variable was set, changed or removed (the dialogue commands `SetGameEvent` / `ClearGameEvent`, the game's scripts, `risen.gamevar.set/remove`): `value` is `nil` for a removed one, `previous` `nil` for a new one; `SetGameEvent` creates the variable with the value 0; nothing while a savegame loads |
| `ai.finished` | `entity, state, reason, loops, run` | a Lua AI state ended (`reason` `"done"`, `"stopped"`, `"error"` or `"replaced"`; `run` is the run's id, see `risen.ai.generation`); not for an NPC that was removed or when the world is torn down; see "AI states and perception" |
| `input.action` | `ev` = `{ key, action, phase, value, duration }` | every action event the engine delivers (key press, hold each frame, release, analog look/move): **a handler that returns a value other than `nil`/`false` consumes it**, the engine never sees it (`phase` `"pressed"`, `"held"`, `"released"`, `"analog"`); see "Input" below |
| `ui.frame` | - | once per rendered frame of the application: also while the world is paused (a menu page is open), at the title screen and while loading, when `tick` does not fire; see "GUI windows" |
| `ui.page` | `ev` = `{ page, kind = "opened" / "closed" / "back" }` | a page given a controller by `risen.gui.leaf_page` was shown, hidden, or got ESC / back |

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
| `"crafting"` | the ingredients taken and the result made by `PSInventory::Synthesize` (every crafting station and `risen.recipe.craft`), together with one `crafting` event | `risen.recipe.craft`, the real anvil / alchemy table (`risen.interact.begin`) |
| `"other"` | a stack appeared / vanished without a hooked operation announcing it (a fallback; only creations and deletions are seen) | not seen in the tests |

  Gold is the item `It_Gold`. The temporary copies that animations hold in the hands (hammer, recipe, result at a crafting station: stack type 2) are not possessions and are neither counted nor reported.
  *Not seen / limits:* the trade screen's buy/sell
  and the player's interactive loot/pick-up are the same engine operations as the Lua functions above (checked in the decompilation), but cannot be driven from a script, so they are not run live.
* **`crafting(crafter, recipe, result, count, ingredients)`** fires **once per craft**, after the `inventory.removed` (one per ingredient) and `inventory.added` (the result) events of it (cause `"crafting"`). `crafter` is the Entity, `recipe` the template name of the recipe item,
  `result` the template name that was made, `count` the amount crafted at once, `ingredients` the list `{ { item = "It_IronOre", count = 2 }, ... }` for the whole `count`. Hooked on `PSInventory::Synthesize`, which every station calls (RE.md section 30). Verified at the real anvil and alchemy table (`ex_recipe`, `recipes`).
* **`entity.died(entity, killer, cause)`** fires **once per entity** (`npc.killed` repeats when something hits a corpse again). The engine kills through its `Kill` script
  (`self` = the killer, `other` = the victim): `killer` is its `self` (`nil` for `risen.script.call("Kill", nil, victim)`). `cause`: `"script"` (a `Kill` called through `risen.script.call`), `"cheat"` (the `OnCheatKill` script),
  `"combat"` (the engine's own death with an attacker), `"other"`. Humans that lose all hit points in a fight are **knocked out**, not killed: `entity.defeated` (and `npc.defeated`) fire for them and `entity.died` does not,
  until something really kills them. `Entity:set_hp(0)` does not kill (the AI keeps fighting) and fires nothing; `Entity:kill()` removes the entity (`entity.destroyed`, no death). Verified: `Kill` with and without killer, twice on one victim,
  `Defeat`, `OnCheatKill`, a real fight (a wolf kills the player: `entity.died(PC_Hero, wolf, "combat")` with the `entity.damaged` before it).
* **`entity.spawned`** is raised by `Entity::Spawn` (what `risen.entity.spawn`, `risen.npc.spawn` and the game's own spawn scripts call); entities the engine creates internally (items dropped from an inventory,
  equipment copies, effects) are not reported, and **neither is an entity that a savegame brings back** (the engine rebuilds it without calling `Entity::Spawn`, RE.md §32: `risen.npc.persistent`'s `on_ready` says "in the world"). **`entity.destroyed`** comes from `eCEntity::Kill`; effect entities (`VFX_*`) are skipped. **`entity.damaged`** only fires for hits that took hit points.
* **`gamevar.changed(name, value, previous)`** comes from the engine's `gCInfoSystem::SetGameVariable` / `RemoveGameVariable` (RE.md §33), which the dialogue commands `SetGameEvent` / `ClearGameEvent`, the game's scripts and `risen.gamevar.set/remove`
  all end in; it fires only when the value changed (or the variable is new / removed). `SetGameEvent` creates a variable with the **value 0**: `previous = nil, value = 0`. Verified live (`spawnwhen`: a real dialogue command, `risen.gamevar.set`, `remove`).
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
T.allow_errors(true)     -- error-level log lines no longer count towards the final "no errors logged" check (the hardening scenario provokes them on purpose); false switches it back
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
`lua` (handler errors with traceback), `test`, `voice` (every sample title the engine asks for and what was answered, voice registrations, barks), `ai` (Lua AI states: definitions, starts, begin / finish, every perception handler call), `combat` (every hit a damage handler saw with the damage before and after, cancelled hits, handler failures). A disabled channel costs one bit test; use `risen.trace.enabled(ch)` to guard expensive message building.

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
`risen.entity.get("UniqueName")`, `risen.entity.player()` → `Entity | nil`, `risen.entity.by_id("xxxxxxxx-xxxx-…")` → the entity with that ID (see `Entity:id()`),
`risen.entity.npcs([center, radius])` → every NPC/monster (or those within `radius` of `center`).
Entity methods: `name()`, `display_name()`, `set_name(s)`, `id()` (the GUID the savegame stores: unlike the name it survives a load), `save_relevant()`, `set_save_relevant(b)` (whether the engine writes the entity
into savegames; true for every spawned item and NPC, **not** for a spawned object such as a bench: set it to keep one), `position()` → `x, y, z`,
`pose()` → `{ x, y, z, yaw, pitch, roll, scale, matrix }` (the world matrix, `Entity::GetPose`; works for the world's own objects), `set_pose{ x?, y?, z?, yaw?, pitch?, roll? }`
or `set_pose{ matrix = {16} }` → `true | nil, reason` (`Entity::MoveTo`: a teleport that also resets the rigid body, navigation and movement; missing values
keep theirs, the scale stays; RE.md section 50), `distance_to(e)`,
`is_npc()`, `is_player()`, `is_dead()`, `is_item()`, `is_character()`, `kill()`, `set_frozen(b)`,
`enable_rendering(b)`, `teleport(name_or_entity)`, `valid()`, `address`, `area_names()` / `set_area_names(list)` (the names of an area entity: security zones, see "Theft" under `risen.ai`). A handle remembers the entity's unique
name: after a savegame load, a session start/stop or a world change it looks the entity up **by name again** instead of
using the old pointer, so handles kept across loads either point at the right entity or fail with `nil`/`false`
(`entity:valid()` tells which; it looks the **name** up, so for items and other spawns named after their template (`It_Apple`) it may find another entity of that name: do not use it for those). Failures never crash the game. Entities without a unique name cannot be re-resolved
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
  (`gESkill_Stat_STR`) are accepted too. An unknown name is an error that lists the valid ones. `risen.balance.skill_enum(name)` returns the engine's enum name (`"gESkill_Stat_STR"`, `nil` if unknown).
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
* **Teachers** (`gCDialog_PS.TeachSkills`, RE.md section 44): `risen.npc.teach_skills(npc)` -> `{ { skill = "gESkill_Stat_STR", min = 0, max = 100 }, ... }`, `risen.npc.set_teach_limit(npc, "gESkill_Stat_STR", 150 [, min])` raises or lowers how far the NPC teaches a skill (in place; the skill must be in his list). The teaching options of the dialogues ask this element (`CanTeachSkill`: min <= the player's value + the amount <= max), so no dialogue edit is needed. A placed NPC keeps its own copy (saved with the entity): write the instance (`risen.entity.by_id`); a template name only changes later spawns. The engine's attribute cap is 200. Verified live (`extmod`): nine placed teachers of the Extended Mod read back their new limits. Verified in a real teaching dialogue (`extplay`): Sam (DEX limit 200 in the Extended Mod) offers "Dexterity +5" to a player at DEX 100 and teaches it; with the limit set back to 100 nothing is offered, near 200 only "+1" is.
* **Combat style** (`risen.npc.combat_style(npc [, { left = , right = }])` -> `{ name, template, hit_range }` | `nil, reason`): the style `gCCombatSystem_PS::GetCurrentStyle` picks for the NPC's worn weapons and skills, or with `hands` for a hypothetical pair of use types (`"1H"`, `"2H"`, `"BS"`, `"Staff"`, `"Axe"`, `"Shield"`, `"Fist"` ...; a missing hand is an empty hand). Read only; this is how the Extended Mod's melee table was checked (`None_Axe_S5` for a one-hand weapon at Sword 5, `None_2H_S5` with range 2.0 for a two-hand weapon at Axe 5, nothing for a two-hander at Axe 0).
* **Chapter** (`risen.quest.chapter()` -> integer | nil, `risen.quest.set_chapter(n [, notify])`, a test aid that only synchronises the quests and infos to the chapter, it does not run the story's chapter info).

### New templates — `risen.template.create`, `risen.balance` sections `new_items`, `new_npcs`, `templates` (see `mods/newtemplates`, [RE.md §25](RE.md))
A mod can add **new item and NPC/monster templates** at runtime: a clone of an existing template (property sets, mesh, icon, skills, inventory, ...) under a new unique name with the **ID derived from that name**,
so everything that refers to a template -- `risen.entity.spawn`, `risen.npc.spawn`, `risen.item.give`, `loot`, dialogue `Give` commands (`Item = "My_Sword"`), inventory stacks, savegames -- accepts it.
```lua
local sword = assert(risen.template.create("Spass_Flame_Sword", "It_1H_Sword", {   -- Template | nil, reason ; second result: created now (true) or overrides re-applied (false)
    name = { English = "Flame Tongue", German = "Flammenzunge" },       -- label: localization id FO_Spass_Flame_Sword (a string = every language; a `default = "..."` entry serves the languages the table does not name)
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
  runs, a save keeps them) and **resolve as soon as the template exists**, even when it is created after the load. Entities spawned from a clone are saved as well (RE.md §32, they store the template's ID): in a session where the clone does not exist the engine drops them while it reads the savegame (see `risen.npc.persistent`: the NPC is `"missing"` until the template exists again).
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

### Recipes -- `risen.recipe`, `risen.interact`, `risen.balance` sections `recipes` and `new_recipes` (see `mods/example_recipe`, `mods/balance_recipes`, [RE.md §30](RE.md), [RECIPE_PROPERTIES.md](RECIPE_PROPERTIES.md))
A recipe is an **item** with a `gCRecipe_PS` property set: the station (`gERecipeCategory`), the ingredients, the required skills and the result. A character **knows a recipe while it carries the recipe item** -- there is no learned-recipe list; the anvil, forge, alchemy table, ... list the recipe items of their
category from the inventory, check `PSInventory::CanSynthesize` (skills and ingredients) and craft with `PSInventory::Synthesize` (ingredients out, result in). The shipped smithing recipes are the half-finished blades themselves (`It_1H_Steel_Hot` is the anvil recipe for `It_1H_Steel_Shaped`); a new recipe
is a new recipe item (a clone of a written recipe item with deterministic ID, like every new template).
```lua
risen.balance.install({
    new_items = { Spass_Sword_Improved = { base = "It_1H_Sword", name = { English = "Slightly Improved Sword", German = "Leicht verbessertes Schwert" }, value = 65, damage = 18 } },
    new_recipes = {                                                            -- a new recipe item; a recipe may name an item of the same spec's new_items
        Spass_Recipe_Improved_Sword = { station = "anvil", ingredients = { It_1H_Sword = 1, It_IronOre = 2 }, result = "Spass_Sword_Improved",
                                        skills = { Smith = 1 }, name = "Smithing plan: Slightly Improved Sword", learn = true },   -- learn: the player is given the plan once per savegame
    },
    recipes = {                                                                -- edit shipped recipes
        It_1H_Steel_Hot = { ingredients = { It_1H_Steel_Hot = 1, It_IronOre = 2 }, skills = { Smith = 2 } },
        It_Recipe_Health_01 = { counts = { It_Plant_Health_01 = 1, It_Wine = 2 }, skills = {}, result = "It_Po_Health_02" },
    },
})
risen.recipe.create("Spass_Recipe_X", { station = "forge", ingredients = { It_Pickaxe = 1 }, result = "It_Sickle" })   -- the one-recipe forms: create / modify(name, settings)
local ok, result = risen.recipe.craft(risen.entity.player(), "Spass_Recipe_Improved_Sword")                          -- the engine's check, then the engine's craft (no station, no animation)
```
* **`new_recipes` settings**: `station` (required: `risen.recipe.stations()` = anvil, forge, grindstone, coolweapon, alchemy, goldsmith, scroll, cooking, frying), `ingredients` (required: `{ It_IronOre = 2 }` or `{ { item = "It_IronOre", count = 2 }, "It_1H_Sword" }`, 1 to 16),
  `result` (required: an item name; the count is always 1: the engine's craft function creates one result per craft and never reads `ResultAmount`), `skills` (`{ Smith = 1 }`, none by default), `base` (the recipe item to clone, default `It_Recipe_Health_01`), the item settings of the recipe item (`name`, `description`, `icon`, `mesh`, `value`, `properties`),
  `learn` (the player is given the item once per savegame: a game variable `SPASS_RECIPE_<name>` remembers it, so selling the plan is not undone; done on `world.ready` of a new game and of every load). Without `name` the recipe item is called "Recipe: <result label>", without `description` it lists station, ingredients and result.
  Items named in `ingredients`/`result` must exist or be created by `new_items`/`new_npcs`/`templates` of the same spec (those sections are validated first).
* **`recipes` settings** (edit shipped or created recipes; all optional, an unknown key lists the valid ones): `station`, `ingredients` (replaces the list), `counts` (changes the amount of existing ingredients: `{ It_IronOre = 3 }`; excludes `ingredients`), `skills` (replaces the list, `{}` removes the requirement), `result`, `properties`. Validated as a whole before anything is written;
  applied on the first `tick` and on every `session.start` (idempotent: absolute values; live recipe entities are written too).
* **Knowing and teaching**: `risen.recipe.known(who, name)`, `learn(who, name)` (gives the item unless it is carried), `forget`, `learn_once` (once per savegame), `learn_on_start(name)` (this mod's `world.ready` handler; `install`/`create` call it for `learn = true`). The catalogue: `risen.recipe.names([station])`, `info(name)` / `get(name)`.
* **Crafting from a script**: `risen.recipe.can_craft(who, name)` is the engine's own `CanSynthesize` (how many times; 0 comes with what is missing), `risen.recipe.craft(who, name [, count [, { unknown = true }]])` checks like a station (the recipe item must be carried unless `unknown`) and calls `PSInventory::Synthesize`:
  the ingredients are deleted, `count` results created, `inventory.removed/added` fire with cause `"crafting"` and then one `crafting` event. Nothing is touched when the check fails.
* **The real station** (what the tests do): spawn the mob (`risen.entity.spawn("Object_Interact_Anvil", x, y, z)`), `risen.interact.begin(player, mob)` is the use key (`can` asks only the mob's `CanInteract_*` script), the player walks over and the station's page opens (`ZS_Interact_Interact`), `risen.script.call("InvEvent_OnClick_CraftSelect", player, nil, stack_of_the_recipe_item)` is
  the click on the recipe, the engine plays the animation and crafts (about 13 s at the anvil), the state returns to `PS_Normal`. The anvil needs Smith >= 1, `It_SmithHammer` and a recipe item of its category; the alchemy table Alchemy >= 1; a smith blank in the left hand makes other stations refuse. With ingredients for several crafts the alchemy page first asks for the amount in a modal dialog.
* **Natives**: `risen.recipe.ingredients_raw/set_ingredients_raw/can_craft_raw/craft_raw`, `risen.item.skill_array/set_skill_array(target, "recipe_required")` (the recipe's `RequiredSkills`), `risen.balance.skill_entries/skill_map`, `risen.template.is_planned`, `risen.interact.can/begin/finish`.
* **Savegames** (verified, `ex_recipe`: four fresh processes): the recipe item and the crafted items are inventory stacks of templates with deterministic IDs, so they load again when the mod creates the templates on the first `tick` (`risen.balance.install` does); a crafted sword that was equipped stays equipped; the given-once flag is saved; without the mod's content the stacks stay dormant and the game runs.
* **Not covered / limits**: only the anvil and the alchemy table were driven as real stations (forge, quench, grindstone and goldsmith use the same engine functions; scroll writing, cooking and frying only through the shared check); one result item per craft (a recipe cannot make several; make the result stack-able or craft in a loop); the stations' own rules (Smith/Alchemy >= 1, the hammer) are in the game's scripts; the NPC smiths were not looked at;
  nothing was looked at on screen (black screenshots).

### Interaction, input and HUD building blocks (M5d, see [RE.md §31](RE.md); used by the standalone mod [`mods/quickloot`](QUICKLOOT.md))
Generic pieces any mod can use; none of them knows what "quick loot" is.

**Interaction** (`risen.interact`, additions to `can/begin/finish` of the recipe section)
```lua
local e = risen.interact.focus()                  -- the entity under the crosshair (what the use key acts on), or nil
risen.interact.set_focus(e, true)                 -- test aid: pin the focus (until set_focus(nil) / the entity is removed); without `true` it lasts one frame
local info = risen.interact.info(e)               -- { use_type, interactions = { { type, can, pre, interact, post, props }, ... }, user, target }
risen.interact.current(player)                    -- the mob the character is interacting with right now (ZS_Interact_*), or nil
for _, n in ipairs(risen.entity.npcs(player, 1000)) do ... end   -- NPCs and monsters within a radius (all of them without arguments)
```
`info` lists the `gCInteraction` objects of the mob, one per user kind. The use key runs the **first** interaction of type `gEInteractionType_Interact_Player` whose `can` script (a plain
script: `risen.script.call(can, player, mob)`; empty = always) accepts. `pre`/`interact` are AI functions (walk, animation, loot window: resumable, not callable from Lua), `post` a plain script
run when the interaction ends. `use_type` is `gEInteractionUseType_Chest`, `_Cupboard`, `_Item`, `_NPC`, `_Anvil`, ... Plain scripts (`CanInteract_*`, `AutoLoot`, `InvFilter_Loot`, `Unlock`'s
effects ...) can be called with `risen.script.call`; calling a `CanInteract_*` script may print a HUD message ("the door is locked"), exactly as the use key does.

**Input** (`risen.input`, [RE.md §31](RE.md))
```lua
risen.input.actions()                              -- the engine's action names (index = key number): "PrimaryAction", "SecondaryAction", "Selection", "WeaponMode", ...
risen.input.pressed("SecondaryAction")             -- held now?   (also was_pressed / was_released / held_for(name) in seconds)
risen.input.pressed("RButton")                     -- or a Windows virtual-key name: LButton RButton MButton V F5 Shift Ctrl Alt Space Num0 ... (OS state, only while the game window is in the foreground)
risen.input.modifiers()                            -- { shift, ctrl, alt }
risen.input.bindings("SecondaryAction")            -- what the current input map binds to it: { { scan = 513, text = "Right Mouse Button" } }
risen.on("input.action", function(ev) if ev.action == "SecondaryAction" and ev.phase == "pressed" and want() then return true end end)   -- true = consume
risen.input.simulate("SecondaryAction", true)      -- test aid: press as the input device does (the engine handles it next frame); false releases
```
The default map binds the left mouse button to `PrimaryAction` (the game's use key in normal mode together with `Selection`), the right one to `SecondaryAction` (in normal mode it only puts away
an item held in the left hand), the middle one to `WeaponMode` (draw/sheathe). `input.action` runs for **every** action event including the analog mouse-look ones: filter by `ev.action` first.
A consumed `pressed` should be followed by consuming the matching `held` and `released` events of that press.

**Effects** (`risen.effect`): `risen.effect.start(name, entity)` starts an effect of the engine's effect system on the entity (`eff_item_<EffectMaterial>_transfer_01` is the item pick-up effect that plays `sfx_items_<material>_transfer_NN`), `risen.effect.count(prefix)` counts the `eff_*` effects the engine started (a test aid).
**HUD** (`risen.hud`): `risen.hud.log(text [, type])` prints a line into the game log, `risen.hud.item_log("It_Apple" | entity [, amount])` an item line (icon, name, amount),
`risen.hud.tutorial_stop(name)` ends a tutorial hint the way the game's take (`"takeitem"`) and corpse loot (`"loot"`) scripts do, `risen.hud.tutorial_done(name)` asks whether it is completed,
`risen.hud.state()` -> `{ browsing, game, loot, mode, pages }` (`loot` = the loot window is open; `browsing` is true in this test copy, which makes the game's own use key ignore the press).

**Options** (`risen.config`): `local cfg, problems = risen.config({ key = "SecondaryAction", items = true, repeat_ms = 80 })` reads `<mod dir>/config.toml` on top of the defaults (the defaults fix
each option's type; a wrong type, an unknown key or a broken file is ignored and reported in `problems` and the log). On top of that file come the choices the player made in the
options menu of the mod manager (`<spass>\options\<mod>.toml`, see "Options menu" below).

**Tracing the engine** (`-Trace calls,hud`, `test-run.ps1 -TraceScripts "^PreInteract,^Interact_,^Unlock,AutoLoot"`): the `calls` channel logs every script the engine calls through a script proxy
(`script Name(self=.., other=.., arg=..)` and `-> result`), every AI function a character starts (`ai-function PreInteract_Player_Chest (unit of PC_Hero)`) and AI state change (`ai-state ZS_Interact_PreInteract`)
whose name contains one of the comma separated substrings (`^` = starts with); nothing without the list. `hud` logs every HUD line the engine prints. With these and `-Trace events` the animated
engine path and a shortcut can be compared line by line.

### GUI windows — `risen.gui`, `ui.frame`, `ui.page`, `risen.debug.screenshot` (M16, see `mods/modmanager`, `mods/gui_test`, [RE.md §42](RE.md))
The engine's own GUI as a tree of windows: the **pages** (`risen.gui.pages()`: `Game`, `Inventory`, `Menu_Main` = 13 ... `Loading` = 29) and the widgets on them (buttons, labels, panels, spin buttons). Everything the game's menu is made of can be read, cloned and changed:
```lua
local main = risen.gui.page("Menu_Main")                       -- a page by number (13) or name; GuiWindow handles are valid as long as the engine keeps the window
for _, w in ipairs(main:children()) do                          -- the five tabs
    local d = w:describe()                                      -- { class = "eCScriptRadioButton", name, id, text = "Credits", x, y, w, h, visible, disabled }
    print(d.text, w:get("OnClick"))                             -- "Credits  OnClick_Menu_Credits": find stock widgets by what they do, not by their translated text
end
local tab = main:find("some_name") or credits:clone(main)       -- a copy with every property (fonts, images, the OnClick script); NOTE: a clone lies behind its siblings
tab:set_text("Mods") ; tab:set_position(843, 653) ; tab:set("OnClick", "My_Script")   -- properties by name (w:properties() lists them: Text, Position, Size, FontColorNormal "r; g; b; a", TextAlign, ImgPushFaceNormal ...)
risen.script.define("My_Script", function() risen.gui.open(25) end)                       -- a click calls the script (scripts made by risen.script.define work as button handlers)
```
* **Pages:** `risen.gui.open(id)` / `close(id)` go through the game's page system (the page controllers may deny a request, `nil, "the request was denied"`), `show(id, visible)` only flips a page window. While a menu page is open the **world is paused: no `tick` event**; `ui.frame` fires once per rendered frame (also at the title screen and while loading; keep the handler cheap), and the layer runs the work that waits for a tick (queued mod reloads, the waits of
  `risen.test`) in those frames too, so a menu can be driven by a mod. The title screen shows `Menu_Start` (26) "Press START" (the test start leaves it open, which blocks ESC: `risen.gui.close(26)`); `Loading` (29) ignores ESC.
* **Your own page:** the retail game never opens `Menu_Cheats` (25). `risen.gui.leaf_page(25)` gives it the controller of the game's menu leaf pages (ESC / back opens the main menu, it hides when another page opens, it shows the menu background) and raises `ui.page({ page, kind = "opened" | "closed" | "back" })`; `risen.gui.hide_page_on(13, 25)` makes the main menu hide when it opens (the game's controller keeps the main menu visible for pages it does not
  know). Fill the page with clones of stock widgets (clone the background last).
* **Click handlers are not allowed to change the button that was clicked:** when the script returns, the engine puts the click event's visible / disabled flags back on that window. Mark the page dirty and redraw in the next `ui.frame`.
* **Window messages** (a test aid, and a way to drive the stock menu): `risen.gui.key(0x1B)` is the Escape key as the window procedure delivers it (opens and closes the menu), `risen.gui.click(x, y)` / `w:click()` a left click at client pixels (`w:center()` converts the GUI's virtual 1365 x 768 coordinates), `risen.gui.mouse("move" | "down" | "up", x, y)`, `risen.gui.client_size()`.
  The window is never resized at run time (that misaligns the GUI and the mouse).
* **Handles are checked:** a `GuiWindow` is used only after it was found again in the live page trees (or among the clones); one the engine deleted answers `nil` / `false` and nothing is touched. Setters return `true` or `nil, reason` (positions and sizes are range checked).
* **Screenshots:** `risen.debug.screenshot("name" [, wait_frames])` writes `<Risen>\bin\spass\screenshots\name.png` (the D3D9 back buffer; the OS cannot capture it), `risen.debug.screenshot_status("name")` is `"pending"`, `"done"` or `"failed: ..."`. The hook is installed on the first request only.
**Limits:** the special list widgets (savegames, achievements, key bindings) are not a generic API; windows are never deleted (a clone stays); keyboard / gamepad navigation of the menu was not driven (the PC menu is mouse driven; controller mode uses the `_C` page variants); texts you put on a page are not localized.

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
| `risen.voice.say_svm(entity, svm_id[, listener])` | the engine's `Entity::StartSaySVM`: the character says a standard voice message (`"GamestartLoot"`, the hero's remark at the dead on the beach) in its own voice, the plain NPC voice when its voice has none; no subtitle, no repeat protection (the game's script helper adds both). `false` when the engine cannot be asked |
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
    state = "ZS_HangAround",       -- initial AI state (started in the NPC's first engine frame with risen.ai.start; a Lua state works too; the AI may switch states immediately)
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
| `state()`, `set_state("ZS_…")` | AI state scripts: `ZS_Ambient`, `ZS_AmbientObserve`, `ZS_AmbientSleep`, `ZS_HangAround`, `ZS_Housewife`, `ZS_Observe`, `ZS_Watch`, `ZS_Study`, `ZS_Smith`, `ZS_Trader`, `ZS_Talk`, `ZS_FollowPlayer`, `ZS_GuidePlayer`, `ZS_WaitForPlayer`, `ZS_Attack`, `ZS_Dead`, `ZS_Monster`, … (the list: `risen.ai.list("state")`). On an NPC that was just spawned the state is lost (the engine initialises him in his first frame); `risen.ai.start` waits for that frame, see "AI states and perception" |
| `full_stop()` | cancel the current AI action |
| `goto_pos(x,y,z[,mode])`, `goto_entity(e[,mode])`, `stop_goto()`, `turn_to(e)` | `mode` is the engine's walk mode enum (0 default) |
| `equip_all()`, `stack_count()`, `stack_item(i)` | equips every equippable inventory stack; number of stacks; the item of stack `i` (a template entity, see "Editing templates") |

**Persistence:** entities made by `risen.entity.spawn` / `risen.npc.spawn` **are** written into savegames by the engine (verified in the game, RE.md §32: position, hit points,
inventory, guild, group, AI state and the entity's ID come back, also in a fresh process) — **except their unique name**. After a load the NPC stands there nameless (its name is the template's),
so `risen.entity.get("SPASS_Bob")` does not find it, and a mod that "spawns it again if it is missing" creates a **duplicate** on every load. Do not write that guard. Declare the NPC with
`risen.npc.persistent` (next section), or remember `bob:id()` in `risen.save` and find it again with `risen.entity.by_id(id)` + `set_name`.

## Persistent NPCs — `risen.npc.persistent` (see `mods/example_custom_npc`, `mods/persistnpc`, [RE.md §32](RE.md))
A modder declares an NPC **once**; the layer makes sure exactly one of it exists in every game, whatever happens: new game, quick save/load, load in a fresh process, a death, a moved NPC.
```lua
local hob = risen.npc.persistent({
    name = "MyMod_Hob", template = "Peasant_1_01",     -- unique entity name; the template may be one of this mod's new_npcs (created later is fine)
    offset = { 150, 20, 150 },                         -- from the player when he is first created in a savegame; or position = { x, y, z } / function(player) return x, y, z end
    label = "Hob the Tinkerer",                        -- shown when the player looks at him (FO_<name>)
    peaceful = true, level = 3, hp_max = 300,          -- the spawn options of risen.npc.spawn: guild, group, items, equip, state, hp, mana ...
    define = function(handle) ... end,                 -- quests and dialogues: called on load.before and on every session.start
    respawn = false,                                   -- default: a dead NPC stays dead; true: replaced at the next session start/load; function(state) -> bool
    version = 1,                                       -- raise it to replace the NPC in existing savegames after you changed the declaration
    routine = { at = { dx = 300, dy = 0, dz = 0, kind = "guard", yaw = 90 } },   -- a post, a day/night routine or a patrol ("NPC routines" below); applied once, checked on every load
    on_ready = function(entity, reason, handle) end,   -- reason: "spawned" | "restored" | "respawned" | "replaced" | "adopted"
    on_death = function(entity, killer, handle) end,
})
hob:state()      -- "alive" | "dead" | "missing" | "removed" | "pending" | "disabled"       hob:entity()   hob:deaths()   hob:respawn()   hob:remove()   hob:ensure()
risen.npc.persistent_get("MyMod_Hob")   risen.npc.persistent_list()
```
* **How it works.** The engine saves spawned entities (system dynamic layer) with their unique ID but without their name. The layer stores `{ id, template, version, home, last, dead, deaths }` per NPC in
  the mod's `risen.save` (key `persistent_npcs`, so it is written next to every savegame and rolled back by a load), spawns the NPC only if the savegame does not know it, and after a load finds it by
  that ID (`risen.entity.by_id`) and gives the name back, 30 frames after `world.ready`. Position, hit points, inventory, guild, group and AI state are the engine's; the layer re-applies nothing.
* **No duplicates**: new game (the world is reset, so are the records), quick load in the same process, load in a fresh process: one entity per name. An entity that already carries the name when the NPC
  is first reconciled is adopted instead of spawned; a second entity of the same template with the name of a restored one is removed.
* **Dead**: `entity.died` (and a `save.before` check) mark the record dead. `respawn = false` keeps the corpse (the engine saves it, lootable) and never replaces it; `true`/a function replaces it at the next
  session start or load (the corpse is removed): there is no timer, "after N days" belongs to the mod (call `handle:respawn()`). An NPC taken out with `Entity:kill()` (destroyed, no death) counts as
  vanished and is made again at the next session start or load; use `handle:remove()` to take it away for good.
* **Changed or missing**: a changed `template` or `version` replaces the saved NPC; a record whose name the mod no longer declares is pruned (its entity is removed) after the first session
  start; a mod that was removed altogether leaves the saved NPCs in the world as ordinary nameless NPCs; a missing template makes the engine drop the entity on load and the NPC `"missing"` (a warning
  is logged once per session) — it is made again as soon as the template exists (the next session start).
* **One world.** `world = "World"` binds the NPC to a world (`risen.world.name()`): while another world is active he is neither created, restored, replaced nor pruned,
  so an NPC of a mod's second world never appears on the island and the island's NPCs are left alone there (`risen.spawn.when` passes it on). Verified on the island
  (an NPC declared for `Testlevel_Physics` is not made there, scenario `modkit`); entering a second world in a running game was not tested (the game has no
  world change of its own).
* **Hold back until something happened.** `enabled = function() -> bool` (an option; `risen.spawn.when` sets it) keeps a declaration from creating, restoring or replacing its NPC while it answers false (`handle:state()` is `"disabled"`);
  `handle:ensure()` makes the NPC exist now as declared (creates, restores or re-creates, brings a `remove()`d one back; `nil, reason` while the world is not ready). Conditional spawning is built on both (next section).
* **Declare while the mod loads.** `define` and the handlers need to exist before a savegame is read. A declaration made later (inside a running session, e.g. a boss when its quest starts) creates or
  finds the NPC at once, and has to be made again on every `session.start` of a savegame that needs it (see `mods/example_kill_quest`, `example_quest_rewards`): the record is pruned otherwise.
* **Events.** `entity.spawned` fires when the layer creates the NPC (spawned, respawned, replaced), **not** for a restored one: use `on_ready` for "the NPC is in the world". `entity.died` fires as usual.
* **Limits.** The record lives in the savegame's sidecar file: a savegame copied without its `.lua` sidecar (or from before the mod existed) is not recognised and the NPC is made again (an old unnamed
  copy of an earlier mod version stays). Hit points regenerate as for every NPC. A mod name collision is refused (`nil, reason`). No timers. A daily routine, post or patrol is the `routine` option (next section).

## NPC routines — `risen.npc.set_routine`, `routine` of persistent NPCs, `risen.world.points` (see `mods/example_routines`, `mods/routines`, [RE.md §35](RE.md))
The engine has a daily-routine system: an NPC owns a *routine* with a **working**, a **relaxing** and a **sleeping** point; by the clock a human works from 6:00 to 20:00, relaxes from 20:00 and sleeps before 6:00 (a missing relax/sleep point
uses the working point); the NPC walks to the point of the moment and does what the point says. A point is a *freepoint* (`FP_Guard`: stand guard, `FP_LookAt`, `FP_Spectator`, `FP_Sleep_Ground`, ...) or the freepoint of a *patrol anchor*. The layer feeds that system:
```lua
-- a persistent NPC with a routine (positions {dx, dy, dz} are relative to his home): applied once, checked on every load, nothing is made twice
risen.npc.persistent({
    name = "MyMod_Gatekeeper", template = "Peasant_1_01", offset = { 250, 20, 350 }, peaceful = true,
    routine = {
        work  = { dx = 350, dy = 0, dz = -100, kind = "guard", yaw = 90 },   -- by day: stands guard, hands on hips, facing +x
        relax = { dx = 500, dy = 0, dz = 150, kind = "lookat" },             -- in the evening: thinks
        sleep = { dx = -100, dy = 0, dz = 250, kind = "sleep" },             -- at night: sleeps on the ground
    },
})
-- any NPC at run time
local info = risen.npc.set_routine(npc, { at = { x, y, z, kind = "guard", yaw = 180 } })     -- one post for the whole day; nil, reason when it cannot be done
risen.npc.set_routine(npc, { patrol = { { x1, y1, z1 }, { x2, y2, z2 }, { x3, y3, z3 }, name = "Camp" } })     -- walks 1, 2, 3, 1, ...
risen.npc.set_routine(other, { patrol = "Camp" })                                          -- joins the same group
risen.npc.get_routine(npc)        -- { routine = "Start", work = Entity, relax = ..., sleep = ..., guard = ..., created = 1, patrol = "Camp" }
risen.npc.clear_routine(npc)      -- empties the points and removes the ones this mod made
risen.npc.apply_routine(npc)      -- "look at your routine now": after the clock changed, or to send a patroller on / a displaced guard back
risen.world.set_time(21, 0); risen.world.time() --> 21, 0
```
* **Spec** (`RoutineSpec`): `at` (one point for all three times), or `work` / `relax` / `sleep`; `patrol`; `guard` (the engine's guard point; stored and read back only); `routine` (name of another routine of the NPC); `apply = false` (do not send him at once); `kind` (default kind of position points); `snap = false`.
  A **point** is an entity (a world freepoint, or one from `add_point`), the unique name of one, or a position `{ x, y, z [, kind = , yaw = ] }` / `{ dx = , dy = , dz = [, kind, yaw] }` (relative to `opts.origin`, by default the NPC's position; the home for a persistent NPC) that makes a **new freepoint**.
  Kinds: `risen.world.point_kinds` (`guard`, `lookat`, `spectator`, `sleep`, `dance`, `field`, `pace`, `boss`, `roam`, `practice_sword`, `stand`, `sit`, `smalltalk`, ...) or any `FP_*` template. Seen in the game: `guard` stands hands on hips and faces the `yaw`, `lookat` thinks, `spectator` crosses his arms, `sleep` lies down (`AniState SleepGround`), `dance` dances, `field` rakes, `pace`/`boss` run `ZS_Boss`;
  `stand`, `sit`, `smalltalk` make the NPC hang around where he is (`ZS_HangAround`) instead of walking to the point.
* **Everything is checked before anything is made** (`nil, reason`: unknown key or kind, `at` together with `work`, a patrol of one waypoint, a dead or non-NPC entity, an unknown routine, a position with no walkable ground within `snap_radius` 300). Positions are moved onto the walkable ground (the engine's navigation mesh; an NPC never reaches a point outside it: the call says so instead).
* **Idempotent.** The same plain-data spec again while its points exist does nothing (`info.reused`); another spec replaces the old routine: the old points are removed 40 frames after the NPC went on (a point killed in the frame its user continues his routine crashed the game).
* **Persistence.** The points are saved with the game (made save-relevant), the engine saves the NPC's references to them: after a load nothing is repeated. What a savegame does not keep is a patrol anchor's name and number: the mod's `risen.save` record (key `npc_routines`) gives them back 30 frames after `world.ready`.
  `risen.npc.persistent{ routine = ... }` (and `risen.spawn.when`) applies the routine when the NPC is made, checks it on every restore (equal declaration + intact points: nothing happens; changed declaration or lost point: replaced) and removes its points with the NPC.
* **Patrol.** `patrol = { waypoint, ... }` makes a group: one anchor (type patrol, numbered 0..n-1, all with the entity name `<mod id>_Patrol_<name>`) with a freepoint (`kind`, default guard) per waypoint; the NPC works at waypoint 1. The engine sends him on to the next number when his stay at a waypoint is over (a guard stays **about one to two minutes**), from the last to the first;
  `apply_routine` sends him on at once. Keep waypoints off the places where people stand: **a freepoint somebody stands on (the player included) is not free** and is skipped. `risen.world.add_patrol(name, waypoints, opts)` / `remove_patrol(name)` make and remove a group without an NPC.
* **World.** `risen.world.points{ near, radius, kind, template, name, name_prefix, limit }` lists the game's own freepoints (169 guard points, 117 stand points, ...), patrol anchors (`index`, `max_users`) and helpers with position, `use`, `task`; `add_point(kind, x, y, z, { yaw, snap, snap_radius })` / `remove_point`; `nav_point(x, y, z [, radius])` the walkable point nearest to a position; `Entity:children()`, `Entity:attach(child)`, `Entity:direction()`.
* **Time.** `risen.world.time()` -> hour, minute; `set_time(hour, minute)`. NPCs notice a changed clock at the end of their task or when you call `apply_routine` (ignored for a moment while the NPC is still walking: wait a second after he arrived).
* **Events.** The points are spawned entities: `entity.spawned` / `entity.destroyed` fire for them (template `FP_*` / `Anchor_Patrol`). The points of an NPC that is removed without `clear_routine` (or a persistent NPC's `remove()`, which does it) stay in the world and in the mod's record.
* **Limits.** A guard displaced a little may walk back by himself, one displaced far (>~400 units) stays until his task ends or `apply_routine`. Humans' hours are driven; monster hours (sleep before 5:00 / from 23:00), `guard` (stored only), roam/local anchors, benches and beds, custom routine names and the look of the poses were not. Natives: `routine_points`, `set_routine_points`, `select_routine`, `world.scan`, `world.spawn_point`.

## Geometry, line of sight and randomness — `risen.math`, `risen.world.line_of_sight / can_see / is_seen_by`, `risen.random` (see `mods/example_unseen_spawn`, `mods/geometry`, [RE.md §38](RE.md))
Helpers for coordinates, for "can A see B" and for randomness. **Conventions** (the engine's, verified in the game): +y is up and the ground is the x/z plane; units are about centimetres (the player is ~225 tall); a **yaw of 0 looks along +z, 90 along +x, 180 along -z, 270 along -x**
(yaw grows to the right); **angles are degrees**; a pitch above the horizon is positive. `Entity:direction()` is the unit vector an entity looks along, `risen.math.yaw_of(entity)` its yaw.
```lua
local M, W = risen.math, risen.world
-- points are entities, { x, y, z } or { x = , y = , z = }; results are Vec3 tables that are all of these at once ({ x, y, z, x = , y = , z = })
M.distance(a, b)  M.distance2d(a, b)  M.direction(a, b)  M.yaw_to(a, b)  M.add / sub / scale / dot / cross / length / normalize / lerp / clamp
M.from_yaw(90)                          --> 1, 0, 0       M.yaw_of(npc) --> 0..360     M.angle_diff(350, 10) --> 20 (the short way, + = right)
M.offset(player, player, -300)          --> the point 300 units BEHIND the player (offset(origin, facing, forward, right, up); facing = yaw | entity | vector)
M.polar(origin, 90, 500)                --> 500 units east of origin
M.relative_angle(point, player, player) --> -180..180, positive = to his right    M.is_behind(point, player, player, 20) --> more than 110 degrees off the way he looks
M.in_cone(point, origin, facing, 120, 2000)  --> true | false, "fov" | "range"
-- line of sight: the engine's own physics ray test (what its AI uses): terrain, rocks, trees and buildings block, open air and CHARACTERS do not
W.line_of_sight(player, npc)            --> true | false, { blocked = true, distance, x, y, z, entity (what is in the way: "Levelmesh_Landscape...", "Obj_Nat_Stone...") }
W.line_of_sight({x1,y1,z1}, {x2,y2,z2}, { bodies = true, ignore = other })
W.can_see(npc, player)                  --> true | false, "fov" | "range" | "blocked"    (viewer: an entity, or { x, y, z, yaw })
W.can_see(npc, point, { fov = 90, range = 1500, eye_height = 160 })
W.is_seen_by(point, { npc1, npc2 })     --> true, npc1 | false        (viewers = nil: the player; with { camera = true }: what is on the player's screen)
W.camera()                              --> { x, y, z, dx, dy, dz, fov = 60, aspect = 0.5625 }  the render camera, dx/dy/dz the unit view direction
-- randomness: the mod's own stream (every run differs unless you seed it), more streams with new()
local R = risen.random
R.int(1, 6)  R.float()  R.chance(0.25)  R.choice(list)  R.shuffle(list)  R.sample(list, 3)  R.weighted(items, { 5, 3, 1 })
R.point_in_ring(player, 600, 1500, { yaw = M.yaw_of(player) + 180, arc = 120 })     --> a point behind the player, 600-1500 away
R.seed(42)                                              -- a fixed sequence (tests)
local rng = R.new(7); rng:persist("loot")               -- a separate stream whose state is saved with the savegame
```
* **`risen.math`** works on points: an entity (its position, the feet), `{ x, y, z }` or `{ x = , y = , z = }`; the results (`Vec3`) carry both forms, so they can be handed to any function that takes a position. Every function returns `nil, reason` for a bad argument (NaN, infinity, a stale entity, a table that is not a point) and never raises. A *facing* is a yaw in degrees, an entity (the way it looks) or a direction vector.
  `in_cone(point, origin, facing, fov, range, vfov)`: `fov` is the whole opening in degrees; the horizontal angle may be `fov / 2` to either side and, when the facing is an entity or a vector, the vertical one `vfov / 2` (default `fov / 2`). **`fov = 180` is exactly the engine's NPC test** (`Entity::HasInFOV`: 90 degrees to each side, horizontally and vertically).
  `lerp`, `clamp`, `sign`, `round(x, digits)`, `deg`, `rad`, `wrap_angle` (-180..180), `wrap_yaw` (0..360), `angle_between(u, v)`, `rotate_yaw(v, degrees)`, `pitch_of`, `pitch_to`, `facing(entity)`, `in_circle`, `in_sphere`, `distance_sq`, `xyz(p)`, `point(p)`, `vec(x, y, z)` complete it.
* **`risen.world.trace(x1, y1, z1, x2, y2, z2 [, ignore [, bodies]])`** is the native: the engine's `eCPhysicsScene::TraceRayNearestHit` with the flags of the AI's own `IsFreeLOS`. It returns `blocked, distance, hit_x, hit_y, hit_z, entity` (`nil, reason` for bad arguments or no physics world). **Terrain, rocks, trees, buildings and other objects block; characters do not** (a peasant standing in the ray does not hide anybody from the AI) unless `bodies = true`.
  `raycast(from, to, opts)` is the friendly form: points or entities (an entity is aimed at where the engine aims: the centre of its bounding box, raised to three quarters of its height below 350 units, about eye level; `from_height` / `to_height` override that, the entities themselves are ignored: `ignore_endpoints = false` turns it off), `opts.ignore` adds more, and the result is `{ blocked, distance, length, x, y, z, entity, from, to }`.
  `line_of_sight(from, to, opts)` is `true`, or `false, hit`. **Verified live:** open air is clear, a ray into the ground is stopped at the terrain (`Levelmesh_Landscape_...`) at exactly the height of the eye, a rock (`Obj_Nat_Stone_...`) stops a ray: 3 units before its surface is clear and 3 units behind it is blocked by that entity; a character blocks only with `bodies = true`;
  for two entities the answer equals the engine's own `risen.world.engine_free_los` (`eCGeometryEntity::IsFreeLOS`) with and without a stone between them.
* **`can_see(viewer, target, opts)`**: inside the field of view (`opts.fov`, default 180; `vfov`; `range`), then the ray from the viewer's eye (default as above, `eye_height` to change it) to the target. The viewer looks the way it faces (`Entity:direction()`); for a pose without an entity pass `{ x, y, z, yaw [, pitch] }`. `false` comes with the reason: `"fov"`, `"range"` or `"blocked"` (and the hit).
  **Verified against the engine's AI:** for six targets around a guard (in front, behind, and four at 80 / 100 degrees to the side) `can_see` and the script `CanSee` gave the same answer. `opts.camera = true` looks from the render camera along its view direction instead (for the player: what is on the screen); `fov` / `vfov` then default to the camera's opening (60 degrees as the engine stores it, taken as the vertical one, which makes ~92 horizontal at 16:9;
  the axis was not proven). `is_seen_by(point, viewers, opts)` returns `true, viewer` for the first viewer who can see the point (`nil` = the player; dead viewers skipped unless `include_dead`).
* **The camera** (`risen.world.camera()`): the engine's own `GetZAxis` of the camera is not its view direction (it is a row of the view matrix); the layer reads the direction from the matrix: verified, in the third-person follow camera it looks at the player (9 degrees off) and its yaw equals the player's body yaw. During the first seconds of a new game the camera flies in; wait until it looks at the player if you rely on it.
* **`risen.random`** is per mod: `risen.random.int / float / chance / choice / shuffle / shuffled / sample / weighted / point_in_ring` use the mod's own stream, `new(seed)` makes more (a number, a string, a state table or `nil`), `seed(n)` makes the sequence of the mod's stream fixed (**tests**), `state()` / `set_state()` copy the generator (four integers, plain data),
  `persist(key)` writes the state into the mod's `risen.save` before every save and reads it back when a savegame is loaded: a loaded game then draws the numbers the saved game would have drawn (verified: five numbers after a quick save, five after the load, equal). `weighted(items, weights)`: weights are a list of numbers >= 0 or a function `(item, index)`; zero weight is never chosen. `point_in_ring(center, rmin, rmax, { yaw, arc, dy })` is uniform over the ring (or a sector of `arc` degrees around `yaw`).
  The generator is sfc32: the sequence of a seed is the same on every machine. **`math.random` is different:** LuaJIT has one generator per Lua state; the sandbox gives every mod the same functions, so *all mods share it*, any `math.randomseed` reseeds it for everyone, and it starts from the same state in every run (an unseeded mod gets the same numbers each time): use `risen.random`.
* **Limits.** The test is one ray between two points (no head-above-the-wall); characters never block unless asked; stealth, light and the NPC's `AssessActive` rules are not part of it; `camera = true` is only meaningful for the player (the camera follows him); the camera's FOV axis is inferred. Natives refuse |coordinate| > 1e7 and more than 64 ignored entities; all functions return `nil, reason` instead of raising.

## Behaviours — `risen.behavior` (scripts attached to NPCs; see `mods/behaviors`)
Like components in Unity or Unreal: define a behaviour once, attach it to an NPC (or to every NPC of a template); the layer starts it whenever the NPC is in the
world, calls `update` and the event handlers, keeps `self.state` in the savegame and stops it when the NPC dies or is gone.
```lua
risen.behavior.define("guard_post", {
    params = { radius = 600, warning = "Halt!" },   -- defaults; attach() overrides them per NPC (risen-modkit shows them as fields)
    interval = 0.5,                                  -- update() every 0.5 s (0 = every frame)
    near = 500,                                      -- on_player_near / on_player_far at this distance
    start = function(self) self.state.warned = self.state.warned or 0 end,   -- self.resumed: the state came from a savegame
    update = function(self, dt) end,
    on_player_near = function(self, player, d) risen.hud.log(self.params.warning) self.state.warned = self.state.warned + 1 end,
    on_quest = function(self, quest, status) if quest == "Rufo_GoToDruidHut" and status == "Success" then self.entity:set_guild(7) end end,   -- the game's quests too
    on_death = function(self, killer) end, on_damage = function(self, attacker, damage) end,
    on_talk = function(self, info) end, on_items = function(self, template, delta, total, cause) end,
    events = { ["gamevar.changed"] = function(self, name, value) end },   -- any other event, a mod's own risen.emit names included
    stop = function(self, reason) end,               -- "detached" | "died" | "removed" | "restarted"
})
risen.behavior.attach("MyMod_Guard", "guard_post", { radius = 800 })     -- a unique name, an Entity or a persistent NPC handle
risen.behavior.attach_template("Wolf", "pack_hunter")                    -- every NPC of the template, the world's and later spawns
risen.behavior.get("MyMod_Guard", "guard_post")      -- the running `self` (or nil)      risen.behavior.detach(target, name)   detach_template(template, name)
risen.behavior.list()   risen.behavior.defined()   risen.behavior.failures()
```
* **`self`** (`BehaviorInstance`): `entity`, `name`, `key` (the unique name, or `"@" .. Entity:id()` for template NPCs and nameless entities), `params` (defaults with the attachment's on top),
  `state` (saved with the game: plain data), `resumed`, `time` (seconds running in this session), `self:detach()`.
* **When it runs.** Behaviours start 40 frames after `world.ready` (the persistent NPCs have their names back after 30), after a load too (`self.resumed`, the saved `state`), and
  when the NPC appears later (a look every 15 frames; a template's NPCs 3 frames after `entity.spawned`). `update` gets the seconds since its last call (a frame counts 0.25 s at
  most, so a menu pause does not pile up; `risen.clock()` is the steady clock it uses). A dead NPC gets `on_death` and `stop("died")` and is not started again; a removed one
  `stop("removed")`. `stop` is **not** called when the world is torn down (load, new game, quit) or the mod is hot-reloaded.
* **Declarations.** An attachment made while the mod loads holds for every session. One made during a session (in a quest script, a dialogue, `on_ready`) lasts for that session;
  `attach(target, name, params, { persist = true })` keeps it in the savegame (`risen.save` key `behaviors_attached`). Attaching again with other parameters restarts it.
* **Events.** `on_death` (`entity.died`), `on_damage` (`entity.damaged`), `on_talk` (`info.started` of an option the NPC owns), `on_items` (`inventory.changed`), `on_quest`
  (`quest.status_changed`: **every** quest, the original game's included), `on_player_near` / `on_player_far` (the distance `near`, checked every frame), and `events` for any
  other event name.
* **Errors** never reach the engine: a callback that raises is logged once (`risen.behavior.failures()`) and switches that one attachment off until the next session.
* **Storage.** `risen.save` key `behaviors`: `{ [key] = { [behaviour] = state } }`; a state that cannot be saved (a function, an Entity) is reset with a warning at the next save.
  Trace channel `behavior` (`-Trace behavior`) logs definitions, starts and stops.
* **Spawning, not behaving:** NPCs that should *appear* when something happened (enemies after a quest of the game was solved) are `risen.spawn.when{ when = risen.cond.quest("<any quest id>", "Success") }`
  (next sections); a behaviour can be attached to them like to any persistent NPC.

## AI states and perception — `risen.ai` (see `mods/example_ai`, `mods/aistates`, [RE.md §36](RE.md))
The NPCs' behaviour is a stack of **AI states** (`ZS_HangAround`, `ZS_Watch`, `ZS_Attack_CombatMove`, ... `risen.ai.list("state")` names the 246 the game has): script functions the engine runs for an NPC every frame in which he is not in the middle of an action, plus a **task callback** (`OnRoutine`, ...) about once a second, which sweeps what the NPC perceives and runs the game's `Assess*` scripts (`AssessActive`, `AssessDamage`, ...). The layer registers Lua functions in the same registry, so a state written in Lua is run by the engine like its own, and the `Assess*` scripts can be hooked.
```lua
risen.ai.state("ZS_MyGuard", {                      -- defines the state (a name the game does not use); returns true | nil, reason
    interval = 0.5,                                 -- loop() every 0.5 s of state time (default 0.25, 0 = every frame)
    task = "OnRoutine",                             -- the task callback made current at begin: keeps the stock perception sweep going (a name of risen.ai.list("callback") or a function)
    begin = function(ctx)  ctx.data.home = { ctx.self:position() } end,       -- once per start
    loop  = function(ctx)                                                      -- every interval
        local p = risen.entity.player()
        if ctx.self:distance_to(p) < 400 then ctx.self:turn_to(p) end
        if ctx.time > 60 then return "done" end                                -- false / "done": finished, back to the routine; another string: finished, that state follows
    end,
    finish = function(ctx) ... ctx.reason ... end,                             -- "done" | "stopped" | "error" | "replaced"
    on = {                                                                      -- perception handlers, active while an NPC is in this state
        notice = function(ctx) if ctx.other:is_player() then ... end end,     -- AssessActive(self, other): about once a second for everything around, the player included
    },
})
risen.ai.start(npc, "ZS_MyGuard", { post = "gate" })     -- true | nil, reason; ctx.args.post in the callbacks
risen.ai.stop(npc [, "ZS_HangAround"])                   -- finish with reason "stopped"; back to the routine (or to the named state)
risen.ai.current(npc)                                    -- { state, time, loops, resumed, args, data } | nil
```
* **Context** (`AiContext`, a new one per call): `self`, `other`, `args` (what `start` was given), `data` (a scratch table of the run), `time` (seconds since the state began), `dt` (since the previous `loop`), `count` (`loop` calls before this one), `resumed`, `name`, `kind` (`"begin" | "loop" | "finish" | "callback" | "perceive"`), `reason` (`finish`), `arg`; `ctx:finish([next_state])` (end after this callback), `ctx:call_original()` (perception handlers).
* **Cadence.** `begin` runs in the first frame the engine runs the state; `loop` right after it and then every `interval` seconds of state time (measured: 0.50 s with 80 fps, 14 calls in 7 s). The engine does not run a state while the NPC has an *instruction* going (`_AI_Goto`, an animation); `npc:goto_pos / goto_entity` use the navigation set instead and **do not block** the loop (the NPC walked 250 units while `loop` went on every 0.5 s), so a loop can steer: start the walk, poll the distance, stop. `risen.ai.start` stops the NPC's current action first (`{ interrupt = false }` lets it finish).
* **Ending.** `begin` / `loop` returning `false` or `"done"` (`risen.ai.DONE`), or `ctx:finish()`, ends the state: the NPC continues his routine (`ContinueRoutine`; an NPC without one gets `ZS_HangAround`). A string other than `"done"` names the state that follows (`return "ZS_AIT_TaskDemo"`). `risen.ai.stop` does the same from outside. If the **engine** replaces the state (a hit, a fight, a dialogue, another `start`, `Entity:set_state`), `finish` runs with reason `"replaced"`. `finish` is **not** called when the NPC is removed from the world or the world is torn down (load, quit). Do not change AI states in `finish` with `Entity:set_state` (the engine is replacing the state right then); `risen.ai.start / stop` called there are deferred to the end of the frame for you.
* **Errors never reach the engine.** A Lua error in `begin` / `loop` is a warning in the log and ends the state (reason `"error"`; `finish` still runs and may raise again: one more warning); in a perception handler it switches that handler off after one warning (the game's own reaction goes on); in a callback it switches the callback off. `risen.ai.failures()` returns how many there were and the last message.
* **Refusals** (`nil, reason`): a bad name (letters, digits, underscore, up to 100 characters), a name the game already uses, an unknown key / perception / task callback, a handler that is not a function, an empty definition, the player, a dead or non-NPC entity, an unknown state, bad options, arguments that cannot be saved with `persist`. Every native takes any value and checks it (no error is raised through the engine's frames).
* **Fresh NPCs.** An NPC made by `risen.npc.spawn` / `risen.entity.spawn` has no AI yet: the engine initialises him in his first frame and starts `ZS_HangAround`, which would overwrite a state set before. `risen.ai.start` waits for that frame (about two frames; dropped with a warning after 600) and returns `true` at once. `Entity:set_state` does **not** wait (use `risen.ai.start`); the `state` option of `risen.npc.spawn` / `risen.npc.persistent` goes through `risen.ai.start` (a persistent NPC's state is not part of a savegame: start it with `persist = true` in `on_ready`).
* **Savegames.** The engine rebuilds every NPC's script stack on a load: **a Lua state does not survive a load**, the NPC is back at his routine (and no stock state is resumed either). `risen.ai.start(npc, state, args, { persist = true })` remembers `{ state, args }` per NPC in the mod's `risen.save` (the arguments must be plain data) and starts the state again 30 frames after `world.ready`, with `ctx.resumed = true` and the saved `args`. The record is dropped when the state ends by itself (`"done"`, `"error"`) or is stopped (`risen.ai.stop`, `risen.ai.forget(npc)`), and replaced by any other `risen.ai.start` for that NPC; an engine replacement (`"replaced"`: combat, a dialogue) keeps it. A record whose NPC is not in the savegame, or whose state is not defined (the mod lost it), is dropped (warning for the latter). `risen.ai.persisted()` lists the records. A mod removed from a game that was saved with its states: the NPCs simply continue their routine (verified).
* **Events.** `ai.finished(entity, state, reason, loops, run)` fires (end of the frame) whenever a Lua state ended; `risen.ai.generation()` is the id the next run gets.

### Perception
`on = { key = fn }` in a state (while the NPC is in it) or `risen.ai.perceive(key, fn, filter)` (not tied to a state) hook the game's `Assess*` scripts. `key` is one of `risen.ai.perceptions` or the name of any `Assess*` / `Perceive*` script:

| key | script | when (observed) |
|---|---|---|
| `notice` | `AssessActive(self, other)` | every sweep (about once a second) for **every entity the NPC perceives** (the player from 1300 units, dead bodies, himself): cheap enough in a state, use a filter with `perceive` |
| `player` | `AssessPlayer` | about once a second when the NPC perceives the player (sub-checks: room, looting, weapon, sneaking) |
| `enemy`, `attacker`, `threat`, `nuisance`, `fighter`, `avoider` | `AssessEnemy`, ... | called by `AssessActive` for the same pair (`AssessNuisance` answers 1 for the player on the beach) |
| `damage` | `AssessDamage(self, attacker)` | **about 0.4 s after a hit**, from `ResumeTaskAfterStumble`; the hit itself has already replaced the state by `ZS_Stumble` (a state-scoped handler never sees it: use `risen.ai.perceive`) |
| `other_damage` | `AssessOtherDamage(self, victim)` | bystanders of a hit |
| `theft` | `AssessTheft(self, item_or_chest)` | the player takes an item or opens a chest / cupboard (the game's perception broadcast asks every NPC, see "Theft" below); **answers 1 = he takes note, 0 = no reaction** |
| `talk`, `dead`, `defeat`, `murder`, `near_body`, `weapon`, `sneaking`, `looting`, `stand_up`, `down` | `AssessTalk`, ... | the game's own callers (`AssessPlayerLooting` is the once-a-second check while the player stands at a chest) |

A handler gets `ctx` (`self`, `other`, `arg`, `name` = the script, `kind` = `"perceive"`); **returning nil lets the game's own reaction run** (and the next handler), any number or boolean is the script's result and the stock script does **not** run (`return true` = "I reacted"); `ctx:call_original()` runs the stock script from inside a handler (`return ctx:call_original() + 100`). `risen.script.call("AssessNuisance", npc, player)` goes through the hooks like the engine's own call (that is how the tests prove the result mapping).
```lua
-- not tied to a state: whoever is hit, the coward runs
risen.ai.perceive("damage", function(ctx)
    if ctx.self:name() ~= "MyMod_Coward" then return nil end
    risen.ai.start(ctx.self, "ZS_MyFlee", { from = ctx.other })       -- deferred to the end of the frame: the game sets a state of its own right after AssessDamage returns
    return true
end, { npc = coward })              -- required: { npc = entity | { entities } } or { all = true }
risen.ai.unperceive(handle)
```
* A state set inside a perception handler is **deferred to the end of the frame** (the code that called the handler goes on: `ResumeTaskAfterStumble` ends in `ResumeTask`, which sets the state from the NPC's AI mode, `ZS_Avoid` for a peasant, and would overwrite it).
* The hooks replace the registry slot of the `Assess*` script with native code; do not `risen.script.override` the same script in a mod that uses them (the later override replaces the hook).
* Perception only runs while the NPC has a task callback that sweeps. A Lua state with `task = "OnRoutine"` keeps the stock sweep; without `task` the NPC keeps the callback he had (a state started on a `ZS_HangAround` NPC: `OnRoutine`; read in the decompilation, every test state sets `task`); a `task` function replaces the sweep by yours (about once a second, `ctx.self`).

### Theft: the engine's assessment and how to silence it
Taking an item or opening a chest or cupboard inside a **watched area** (a house, a camp: an area with a security level, `SL_Public` ... `SL_Casern`) is theft to the game; on the open beach, with bodies and with sarcophagi it is not. The game asks
with one engine function, the **perception broadcast** (`Script_Game.dll`), called by the animated take before the item is inserted and before a chest is unlocked: it runs the script `AssessTheft(npc, item_or_chest)` for **every NPC of the world**,
the script answers 1 for an NPC who takes note (a human, not in your party, the item not dropped by you, the place watched; it has recorded his comment) and 0 for no reaction; then the game also checks `PerceiveTheft` (within 700 units and
`CanSee`) and applies what was recorded (the NPC comments, watches, threatens: `LastPlayerComment`, AI mode `Observe`). [RE.md §40](RE.md).
```lua
local reacted, why = risen.ai.broadcast("AssessTheft", item_or_chest)  -- do what the game does for a theft; true when any NPC reacted; nil, reason for a bad script / entity
-- no reaction while a game variable is set (mods/example_ignore_theft): the script answers 0, the game's own check does not run
risen.ai.perceive("theft", function(ctx)
    if (risen.gamevar.get("MyMod_ThievesFriend") or 0) ~= 0 then return 0 end
    return nil                                                     -- the game's own reaction
end, { all = true })                                               -- or { npc = shopkeeper }
```
* `risen.ai.broadcast(script, entity)` asks the very question the animated path asks; `risen.script.call("AssessTheft", npc, item)` only records the reaction in the game's shared result record and answers 1 / 0: nothing happens to the NPC. Only `"AssessTheft"`
  is verified (the other callers of the function pass other scripts and result records). It is bound by offset with a prologue check: with another build of `Script_Game.dll` it is unavailable (`nil, reason`; quick loot falls back to its `veto` mode).
* A handler returning 0 for `theft` works for every way the game asks: the animated take, the sweep that runs while the player stands at a chest (`AssessPlayerLooting`, five `AssessTheft` calls for one animated chest), `risen.ai.broadcast` and the standalone quick loot.
* **Watched areas:** `Entity:area_names()` / `Entity:set_area_names({ "SL_Public" })` read and write the names of an area entity (made from the template `AreaZone_Default`, a box of +-500 units around it): `risen.entity.spawn("AreaZone_Default", x, y, z)` turns that place into a watched one.
  `SL_Public`, `SL_Watched`, `SL_Forbidden`, `SL_Inquisition`, `SL_Graveyard`, `SL_Vulcan`, `SL_Casern` are the names the scripts know (only `SL_Public` was tried). Whether a spawned area survives a savegame was not tested.
* There is no noise-based perception in the game: nobody reacts to the sound of a chest.

### Callbacks and the registry
```lua
risen.ai.callback("MyMod_Tick", function(ctx) ... end)       -- an AI callback (bool cb(unit)): true | nil, reason; usable as `task = "MyMod_Tick"`; limit 128
risen.ai.list("state" | "function" | "callback" | "script")   -- sorted names, what Lua defined included
risen.ai.exists(kind, name)   risen.ai.info(kind, name)       -- { module = "Script_Game.dll", rva = 0x..., lua = false } (diagnostics: where to look in a decompiler)
risen.ai.defined()                                            -- { states = {...}, callbacks = {...} }
```
AI *functions* (`_AI_Goto_Ext`, `PreInteract_*`) are listed but cannot be defined or replaced: only a state calls one, and a Lua state does the same in `loop` with `goto_pos`, `turn_to` and the like. Limits: 128 Lua states, 128 callbacks. The trace channel `ai` (`-Trace ai`) logs definitions, starts, begin / finish, and every perception handler call.

## Damage hooks — `risen.combat.on_damage` (see `mods/example_stacking_fireball`, `mods/damagehooks`, [RE.md §39](RE.md))
Custom damage calculations: a handler sees every hit before the engine applies it and answers with the damage to apply.
```lua
local h = risen.combat.on_damage(function(hit)           -- hit: attacker, victim, source, weapon, kind, spell, type, damage_script, damage, original_damage, multiplier, protection
    return hit.damage * 1.5                              -- nil = unchanged, a number = this damage, false or 0 = cancel the hit
end, { attacker = "player", spell = "fireball" })        -- the filter is optional; every key narrows it: kind, spell, type, weapon, script, attacker, victim
risen.combat.off_damage(h)                               -- true; false for a handle that is not registered
risen.combat.handlers()  risen.combat.failures()  risen.combat.classify("DoSwordDamage")
```
* **What a hit is.** Every hit of the game (a sword, an axe, a staff, fists and monster bites, Fireball / Frost / Missile, arrows and bolts, fire, falling objects) ends in the engine script `DoDamage`; the layer puts its handlers in front of it
  (RE.md §39). `hit.attacker` (the caster, the wielder, the monster; `nil` when nobody caused it) and `hit.victim` are entities; `hit.source` is the damaging item (the projectile, the weapon, the monster's attack item) and `hit.weapon` its template name
  (`"It_Projectile_Fireball_S0"`, `"It_1H_Club"`, `"It_Fist_Monster"`); `hit.kind` is `"spell"`, `"melee"`, `"ranged"` or `"other"`, `hit.spell` `"fireball"` / `"frost"` / `"missile"` / `"inferno"` for spells; `hit.type` the damage type of the item
  (`"Magic"`, `"Edge"`, `"Blunt"`, `"Point"`, `"Fire"`, `"Ice"`); `hit.damage_script` the item's `DamageScript` (`"DoMagicFireballDamage"`, `"DoSwordDamage"`, `"DoFistDamage"`).
* **The numbers.** `hit.damage` is the damage that is about to go in: the item's own numbers, the attacker's strength or spell skill bonus (a Fireball at skill 3 is 100 + 25 * 3 = 175) and the **area falloff** (`hit.multiplier` is below 1 for a victim at the edge of a Fireball
  blast and already part of `damage`). `hit.original_damage` is what the engine passed (differs from `damage` once an earlier handler changed it); `hit.protection` the victim's protection against `hit.type`. The engine subtracts the protection **after** the handlers and never goes
  below `multiplier * 10`, applies the difficulty factor when the victim is the player and halves the damage for party members: **return a scaled number, not a constant, and protection, falloff and the minimum keep working** (verified: x4 on a blast gave 700 and four times the falloff
  damage for the outer targets; a returned 50 against a protection of 100 still took the minimum 10).
* **The answer.** `nil` (or `true`) keeps the damage, a number replaces it (rounded to an integer, at most 1 000 000), `false` or a number that rounds to 0 **cancels** the hit: the victim loses nothing and the stumble / knock-out scripts do not run, `entity.damaged` does not fire,
  later handlers are not called. A negative or non-finite number, a string, a table or a function is a warning and counts as `nil`.
* **Several handlers** run in registration order; the second sees the first one's number in `hit.damage`. **Filters** (`kind`, `spell`, `type`, `weapon`, `script` take a string or a list; `attacker` / `victim` an entity, `"player"` or a list of those) are checked in C++
  before Lua is called; an unknown key or value, or a stale entity, makes `on_damage` return `nil, reason` (for example `{ kind = "magic" }`). `attacker` / `victim` compare unique entity names at the time of the hit.
* **Errors.** A handler that raises an error (or returns a bad value) is a warning (the first ten per handler are logged), the stock damage applies and the next handler is still called; `risen.combat.failures()` counts them.
  `DoDamage` calls made from inside a handler (`risen.script.call("DoDamage", hit.source, other, 50)`: a handler may hurt somebody else itself) are **not offered to the handlers again**.
* **Exactly once.** Every hit passes `DoDamage` once, so a handler is called once per victim and hit (the weak hits at the edge of a blast included: one call per victim in the blast); the `entity.damaged` event (§ gameplay events) reports what was really lost afterwards.
* **Limits.** Only hits that reach `DoDamage` are seen (poison, falling, `Entity:set_hp`, the cheats are not); a cancelled hit makes no stumble, so the victim does not react to *that hit* the way a hit NPC does (suppressing reactions to hits that do land is a separate
  milestone); the first handler overrides the registry entry of `DoDamage`, so do not also `risen.script.override("DoDamage", ...)` (`on_damage` refuses while a Lua override is in place). Verified live with real casts, wolf bites and club blows (`damagehooks`), not driven:
  NPC mages, arrows, Frost and Missile hits (classified from the script name, unit tested).
* **Trace.** `-Trace combat` logs every hit a handler saw with the damage before and after, cancelled hits and handler failures.
* **Example.** `mods/example_stacking_fireball`: the player's Fireball does 50% more damage for every earlier hit on the same target (counter per victim in `risen.save`, reset when it dies); the rules and constants are at the top of `main.lua`.

## Conditional spawning — `risen.spawn.when`, `risen.cond` (see `mods/example_spawn_when`, `mods/spawnwhen`, [RE.md §33](RE.md))
An NPC that does not exist until something happened — a quest was finished, the player carries enough of an item, a game variable was set, another NPC died — and that may go away again. It is a
**persistent NPC** (the previous section) with a rule in front of it: the layer creates it when the condition holds, removes it when another one does, remembers per savegame what it did and makes the
world consistent after every load. Nothing is polled.
```lua
local C = risen.cond
risen.spawn.when({
    name = "MyMod_Smith", template = "Peasant_1_02", offset = { -150, 20, 150 }, label = "Smith Bram", peaceful = true,    -- every option of risen.npc.persistent
    when         = C.quest("Q_MyMod_Errand", "Success"),        -- created when this holds
    despawn_when = C.event("MyMod_Smith_Leaves"),               -- removed when this holds (optional)
    once = true,                                                -- default; false = follow the condition in both directions
    on_fire    = function(rule, why, entity) end,               -- why: "condition" (it happened now) | "load" (found true when a game was loaded or started)
    on_despawn = function(rule, why) end,
})
risen.spawn.when({ name = "MyMod_Guard", template = "Peasant_1_03", offset = { 0, 20, 200 }, once = false,
                   when = C.all { C.item("It_Apple", 3), C.negate(C.var("MyMod_Peace")) } })      -- exists while the player holds 3 apples and the variable is 0
```
**Conditions** (`risen.cond`; a plain `function() -> boolean` is a condition too; a bad argument raises an error):

| Constructor | Holds when | Looked at again after |
|---|---|---|
| `quest(name [, status])` | the quest has the status (`"Success"` by default; a list accepts any: `{ "Failed", "Lost" }`; `"None"` = no such quest) | `quest.status_changed` of that quest |
| `item(template [, count [, holder]])` | the holder (the player; or an entity name; in a dialogue `"npc"` = the owner of the option) carries `count` (1) or more of the template | `inventory.changed` of that template |
| `var(name [, value [, op]])` | the game variable (`risen.gamevar`) is non-zero / equals `value` / compares with `op` (`== ~= >= <= > <`); a missing variable counts as 0 | `gamevar.changed` of that name |
| `event(name)` | the game variable **exists**: what the dialogue command `SetGameEvent` does (it creates the variable with the value **0**) | `gamevar.changed` |
| `skill(skill, value [, op [, who]])` | a skill or attribute of the player (or `who`: an entity's unique name; in a dialogue `"npc"` = the owner of the option) compares as wanted (`risen.balance.skills()`, `op` `>=` by default; the total, worn items included) | nothing announces it: `risen.spawn.check()` or a polling predicate |
| `guild(guild [, who])` | the player (or `who`) belongs to the guild: a name of `risen.npc.guilds`, a number, or a list of them | nothing announces it (as `skill`) |
| `dead(who)` / `alive(who)` | the NPC (unique name, or a persistent / rule handle) is dead (corpse, or recorded dead) / exists and lives | `entity.died` / `entity.destroyed` of it, and when a rule's NPC changes |
| `fn(f [, { on = "event" or {...}, poll = frames }])` | the predicate returns true | the events in `on`, every `poll` frames; without either only after a load, with the other conditions of the rule, and `risen.spawn.check()` |
| `all{...}`, `any{...}`, `negate(c)` | and / or / not | what their members need |

`risen.cond.test(c)` answers `true`, `false` or `nil` ("cannot tell": no player yet, a predicate that raised an error — logged once; the rule then does nothing). `tostring(c)` describes a condition (it is what the log says).
The same conditions decide which **dialogue options** are offered (`risen.dialog.create{ when = ... }`, next sections but one: "Dialogue conditions"); `skill` and `guild` exist for them (a spawn rule has no event for either, so combine them with `risen.spawn.check()` or a polling predicate).

**Rules.**
* **One-shot** (`once = true`, default): `waiting` -> (`when` holds) -> `present` (the NPC exists) -> (`despawn_when` holds) -> `gone` (removed; never again in this savegame). Before `despawn_when` the condition may become false again: the NPC stays.
  Both holding the first time it is looked at: nothing is created (the rule is `gone`).
* **Tracking** (`once = false`): the NPC exists exactly while `when` holds and `despawn_when` does not; it is removed (`handle:remove()`) and made again (a new body at the same home position, `handle:respawn()`) as often as that flips; the state goes `waiting` <-> `present`.
* **Remembered.** The state `{ state, spawns }` per rule is stored in the mod's `risen.save` (key `spawn_rules`), i.e. next to every savegame and rolled back by a load: a savegame made before the trigger has the rule `waiting`
  (and, because the engine's savegame has no such NPC, none appears), one made after has it `present` (the NPC is restored by the persistent layer, exactly once), one made after he left keeps him `gone`. Loading an
  earlier savegame in the same process removes what the later one had created. A new game starts everything `waiting`.
* **Event driven.** A matching event only marks the rules that use it; they are evaluated on the next `tick` (events can fire inside engine code, e.g. `quest.status_changed` inside `gCQuest::SetStatus`). Without a polling predicate nothing runs per frame.
  After **every new game and load** (40 frames after `world.ready`, the persistent NPCs reconcile after 30) each rule is evaluated once: a condition that became true or false behind the layer's back (an old savegame, an edited rule, a variable
  restored by the savegame) is caught up, with `why = "load"`.
* **`define`** of the NPC (its quests and dialogues) runs on `load.before` and `session.start` as for any persistent NPC, so its dialogue options exist while the NPC does not.
* **Handle** (`SpawnRule`): `state()` (`"waiting"`, `"present"`, `"gone"`, `"pending"` before the world settled), `entity()`, `fired()` (how often it created the NPC in this savegame), `check()` (look now), `reset()` (back to `waiting`, the NPC removed; fires again when the
  condition still holds), `npc` (the `PersistentNpc`). `risen.spawn.get(name)`, `risen.spawn.rules()`, `risen.spawn.check()`. Same name twice: the same handle.
* **Limits.** NPCs only (items and other entities: use `risen.entity.spawn` in an event handler); a rule's record only exists once it fired; no timers ("N days after"): use a `fn` predicate with `poll`; the sidecar caveat of persistent
  NPCs applies (a savegame without its `.lua` file knows nothing: the rule is `waiting` and fires again when its condition holds, and an old nameless copy of the NPC stays).
  **Game variables**: the event comes from the engine's own `gCInfoSystem::Set/RemoveGameVariable`, so dialogue commands, scripts and `risen.gamevar.set` all report; variables restored by a savegame load are not reported (the look after the load catches them).

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
"SaySound"|"ShowSubtitle")`, `add_script_command()`, `set_say(i, speaker, listener, key)`, `set_script(i, name)`, and the low-level condition
sequence (`add_condition(class)`, `set_condition_property`, `get_condition_property`, `list_condition_properties`, `set_condition_skill`, `condition_count`, `clear_conditions`: "Dialogue conditions" below).

### Dialogue conditions — `when`, `engine` (see `mods/example_dialog_when`, `mods/dialogcond`, [RE.md §34](RE.md))
An option is offered when its **condition type** (`condition = "Running", quest = ...`, the engine's own switch) holds **and** every condition of its **condition sequence**. `when` fills the sequence from the vocabulary of `risen.cond` (the one `risen.spawn.when` uses):
```lua
local C = risen.cond
risen.dialog.create({ name = "Rurik_Secret", owner = "Rurik",
    when = C.all { C.item("It_IronOre", 3), C.var("Rurik_Trust", 2, ">="), C.guild("Pir") },      -- 3+ iron ore AND trust >= 2 AND the player is a bandit
    lines = { ... } })
risen.dialog.create({ name = "Rurik_Hideout", owner = "Rurik", when = C.any { C.event("Rurik_Heard"), C.guild("Pir") }, lines = { ... } })      -- variable set OR guild == Pir
risen.dialog.create({ name = "Rurik_Mood", owner = "Rurik", when = function(npc, player) return npc:hp() > 100 end, lines = { ... } })              -- any Lua predicate
risen.dialog.create({ name = "Rurik_Near", owner = "Rurik", engine = { { class = "OwnerNearEntity", Entity = "PC_Hero" } }, lines = { ... } })    -- a raw engine condition
```
* **What the layer makes of a condition.** The engine evaluates the conditions of the sequence **natively, in order, and stops at the first that fails**. Leaves with an engine equivalent become such conditions, `all{...}` is several of them:

  | Condition | Engine condition (`risen.dialog.describe` shows it) |
  |---|---|
  | `item(t, n)` of the player or `"npc"` (the owner) | `ItemAmount{ Owner, Item, ItemAmount }` (the owner of the option is named by its unique name, the player is `PC_Hero`) |
  | `negate(item(t))` (exactly "none") | `ItemAmount` with amount 0 |
  | `event(name)` / `negate(event(name))` | `PlayerKnows` / `PlayerNotKnows` (the game variable exists / does not exist) |
  | `skill(s, v, op, who)` / its negation | `SkillValue{ Entity, CompareOperation, SkillValue = skill + amount }` (the negation inverts the operator) |
  | `dead(npc)` / `alive(npc)` | `NPCStatus{ SecondaryNPC, SecondaryNPCStatus = Dead / Alive }`; `dead` of a **persistent** NPC stays Lua (its record can say "dead" with no corpse in the world) |
  | `quest(q, S)` for one status `Running` `Success` `Failed` `Cancelled` `Won` `Lost` | `QuestStatus{ Quest, CondType = gEInfoCondType_S, IsCurrentStatus = true }` |

  **Everything else** (`any`, `negate` of anything but the cases above, `var` — the engine cannot compare a variable's value —, `guild`, `fn`/a plain function, `quest` with `Open`/`Obsolete`/`None` or a list of statuses, `item` of another holder) is **one** `Script` condition at the end of the sequence:
  the engine calls a script of this layer (`SPASS_Cond_<mod>_<option>`, made with `risen.script.define`), which evaluates what is left with `risen.cond.test` and answers 1 or 0. So `all { item, var, guild }` is `ItemAmount` + `Script{ var AND guild }`: the Lua only runs when the engine's own condition holds.
  `risen.dialog.compile(when [, owner])` -> `{ engine = { "ItemAmount{...}", ... }, lua = "all(...)" | nil }` tells what `create` would make (changes nothing); `risen.dialog.describe(option_name)` what it made.
* **When the engine looks at an option**: whenever it builds the list of options (a conversation opens, an option finished playing), and on `Info:available()`, `Info:conditions_fulfilled()` and `risen.dialog.has_infos(npc)`; the Lua script is called **once per look** (verified: one call per `available()`, one per option when a conversation opens, none while nothing looks).
  **The list does not change while it is shown**: change the world, then talk again (or finish an option) and the options follow. Nothing is polled.
* **Errors**: a predicate (or any Lua leaf) that raises an error, or answers `nil` ("cannot tell"), makes the option **not offered**; the error is logged **once** as a warning (`dialogue 'Name': condition 'predicate' raised an error ...`), never as an error line, and the game goes on.
  If something cannot be attached (the engine refuses a condition or a property), `create` returns `nil, reason`, the option exists but is marked as already given (**never offered**; an option must not turn unconditional by accident).
* **Predicates** get `(npc, player)`: the entity that owns the option and the player. `risen.cond.test(c, { npc = "Rurik" })` evaluates a condition the way an option does.
* **`engine = { { class = "<Class>", <Property> = value, ... }, ... }`** adds raw engine conditions (checked against the class's property list first; strings, numbers, booleans): `ItemAmount` (`Owner`, `Item`, `ItemAmount`), `NPCStatus` (`SecondaryNPC`, `SecondaryNPCStatus` = `gEInfoNPCStatus_Alive|UnHarmed|Defeated|Dead|TalkedToPlayer|NotTalkedToPlayer`),
  `OwnerInArea` (`AreaPropertyName`), `OwnerNearEntity` (`Entity`: the owner is within the engine's talking distance of it), `PlayerKnows` / `PlayerNotKnows` (`PlayerKnows` / `PlayerNotKnows` = a game variable name), `QuestStatus` (`Quest`, `CondType` = `gEInfoCondType_<state>`, `IsCurrentStatus`), `Script` (`ScriptName`, `Self`, `Other`, `Param`: a script that
  answers non-zero), `SkillValue` / `CanTeachSkill` (`Skill = "Strength"`, `Amount`; `SkillValue` also `Entity` and `CompareOperation = "GreaterEqual"` ...), `TeachEnabled` (`TeachEnabled`). The values are the engine's text format; one it cannot parse is refused (read back and compared).
* **Limits.** The engine's `ItemAmount` reads the item's first stack (the same number as `risen.item.count` for every item tried); `OwnerNearEntity` was only seen true, `OwnerInArea`, `TeachEnabled`, `CanTeachSkill` and the `NPCStatus` values other than `Dead`/`Alive` were attached but not evaluated in a game;
  engine conditions of a **quest that does not exist** are false (no message box, unlike `condition = "Running"` with a missing quest, which `create` refuses); conditions on a dialogue are created again on every load like the dialogue itself (define it in `load.before`/`session.start`, see below). `when` needs the `owner` as an entity name (like the option).

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
risen.gamevar.exists("Name")   risen.gamevar.remove("Name")      -- dialogue commands SetGameEvent (value 0) / ClearGameEvent create / remove a variable; event `gamevar.changed`; conditions: `risen.cond.var/event`
risen.world.is_game_running()  risen.world.new_game()  risen.world.quick_load()
risen.world.quick_save()  risen.world.auto_save("name")  risen.world.is_save_allowed()
risen.world.name()     -- the active world: "World" (the island), a test level's or a mod's world; nil before the first one (event world.changed)
risen.world.sectors()   -- { { name, enabled, on_startup }, ... } of the active world (sector = X.sec + X.lrent of the game's worlds)
risen.world.sector_enabled("OutMain_Story_Chapter3_Lizards_Off")  -- boolean | nil, reason
risen.world.set_sector("OutMain_Story_Chapter3_Lizards_Off", true)  -- World::SetSectorStatus: true | nil, reason; saved with the game
```
`new_game`, `quick_load`, `quick_save` and `auto_save` are *requests*: the engine performs them on its next processing step
(watch `save.after` / `load.after` / `session.start`). `is_save_allowed()` is false while a conversation is open; a save requested then crashed the game when the conversation had run a `Give`
command (and so did the next new game): finish the conversation first (an `End` command as the last line of the info, or let the player leave it).

## risen.loc — localization overlay
```lua
risen.loc.trace(true)                                 -- log every string id the game reads
risen.loc.set("GO_QuestRunning", "Quest läuft", "German")   -- optional language; default: all languages
risen.loc.get("GO_QuestRunning"), risen.loc.exists(id), risen.loc.clear(id), risen.loc.language()
risen.loc.get("HUD2_Btn_Apply")                      -- the game's own text in the active language ("Apply" / "Übernehmen"), also usable for labels of your own pages
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
namespaced by mod id. Sidecar files of deleted savegames are not removed. The sidecar also records which version of each mod wrote the data (`@mods`; `risen.mods.saved_version`), the trigger of `risen.migrate`.

## Hot reload and the mod manager API — `risen.mods` (M15, see `mods/hr_target`, `mods/hotreload`, [RE.md §41](RE.md))
A mod can be torn down and run again **without restarting the game**: `risen.mods.reload("my_mod")` removes everything the running instance registered and runs `main.lua` again in a fresh environment
(same sandbox rules, new `mod.reload` info). Every registration a mod makes is tracked per mod:

| what the mod registered | on reload |
|---|---|
| event handlers (`risen.on`), `risen.emit` listeners | removed, made again by the new run |
| `risen.script.override` | the game's own script is put back; `risen.script.define` stays registered (infos may name it) but answers 0 until it is defined again |
| `risen.ai.state` / `callback` / `perceive` | handlers removed, states retired: NPCs that run one go back to their routine (no `finish` call), the entry can be defined again |
| `risen.combat.on_damage` | removed |
| `risen.npc.persistent`, `risen.spawn.when`, `risen.balance.install`, `risen.dialog` conditions, `risen.config`, strings | made again by the new run (they live in the mod's handlers); NPCs already in the world are found again by their ID, an NPC the new version no longer declares is pruned (M7), balance is applied again with the live entities |
| `risen.save` data | kept (the store belongs to the savegame, not to the instance) |
| dialogue infos, quests, created templates / recipes / spells, entities, loose assets | **engine-owned, kept as they are**; a changed definition cannot be applied and is reported in `restart_required` (`dialog.create` / `quest.define` compare the definition with the earlier one) |

```lua
local rep, why = risen.mods.reload("my_mod", { now = true })   -- default: at the start of the next frame (queued = true)
-- rep = { target, ok, queued, error?, summary, actions = { { id, action = "reloaded", version_before, version_after, handlers_removed, handlers_added,
--         released = { "script override GiveXP (original restored)", "AI state ZS_X (1 NPC(s) sent back to their routine)", ... }, restart_required = {}, state_carried } } }
```
* **Order and dependencies:** the mods that `requires` the reloaded one are torn down first (newest first) and run again after it, each one told what it missed. A mod that `load_after`s it is not touched.
* **Refusals leave everything as it was:** an unknown mod, a `mod.toml` that does not parse or changed its `id`, a `requires` that is not loaded, a syntax error in `main.lua`. A runtime error in `main.lua` leaves the mod `failed` (state, `reason`) with none of its half-made registrations; its dependents are `skipped`; fix the file and reload (the failed mod can be reloaded) and `risen.mods.load(dependent)`.
* **State:** `risen.on_unload(function(reason) return { counter = n } end)` runs while the old instance is torn down; its first result arrives as `mod.reload.state` (`mod.reload = { count, from_version, state }`, `nil` on the first run). Mod data in `risen.save` is untouched.
* **Late mods are told what they missed:** a mod that runs while a game is on gets `mods.loaded`, then `session.start(mode)` and `world.ready(mode)` of the running game (to that mod only). `risen.test.wait_event("session.start")` answers at once from the lifecycle too.
* **When:** `reload` returns `queued = true` and runs at the start of the next frame (before `tick`), because the code that asks may be a handler of the very mod; `{ now = true }` runs at once unless a handler of an affected mod is on the stack. Outside a game (main menu) it always runs at once.
* **From outside the game:** write a mod id (or `all`) into `<Risen>\bin\spass\reload.txt` (`echo my_mod> reload.txt`): the layer looks once a second, reloads the mods named and deletes the file (works without the watcher; the reload happens at the start of the next frame).
* **Watcher:** `config.toml [mods] watch = true` (or `risen.mods.watch(true, 0.5)`): a changed `.lua`, `.toml` or `.csv` file of a loaded or failed mod reloads it (checked every `watch_interval` seconds, default 1). A reload also re-reads `mod.toml` (version, `requires`, `retire`), `strings/*.csv` and `config.toml` (`risen.config`).
* **What a restart is still needed for:** `data_overlay` mounts, `assets` directories that appeared after start, anything in `restart_required`, the C++ layer; and everything the engine owns (see the table). A reload is for the Lua code, its handlers and its declarations.

```lua
for _, m in ipairs(risen.mods.list()) do   -- loaded mods first, in load order
  print(m.id, m.version, m.state, m.reason, table.concat(m.needed_by, ","), m.reloadable, m.data.npcs)
end
risen.mods.get(id)  risen.mods.unload(id, { cascade = true })  risen.mods.load(id)  risen.mods.last_report(id)
risen.mods.disable(id, { cascade = true })  risen.mods.enable(id, { cascade = true })   -- kept in <Risen>\bin\spass\mods.toml; "now" (default) also unloads / loads
risen.mods.order()  risen.mods.check_order(ids)  risen.mods.set_order(ids)  risen.mods.config()        -- custom load order: nobody before what it requires / loads after
```
`ModEntry` (`list` / `get`): `id name version author description dir folder state loaded failed kind reason enabled load_order requires load_after needed_by api_version reloadable reload_note load_count restart_required data`;
`state` is `loaded | failed | skipped | disabled | unloaded` (`kind`: `load_error`, `manifest`, `missing_dependency`, `dependency_unavailable`, `cycle`, `duplicate`, `disabled`). `data` says what the mod left in the current savegame data
(`present`, `saved_version`, `npcs`, `spawn_rules`, `ai_states`, `routines`, `keys`): a manager shows it before a mod is switched off or updated. The player's choices live in `<Risen>\bin\spass\mods.toml` (absent = the automatic order, everything on):
```toml
order = ["base", "extras"]   # optional custom load order; requires / load_after always win (a conflict is a warning in the log, and set_order refuses it)
[enabled]
some_mod = false             # not loaded; a mod that requires it is skipped with the reason
```
`mods.disable` of a mod others require is refused unless `cascade = true` switches those off too; `mods.enable` of a mod whose requirements are off likewise. The automatic order is alphabetical by id with `requires` / `load_after` first (unchanged when the file is absent).
`risen.mods.watch`, `note_restart`, `compare_versions`, `version_matches`, `saved_version`, `recorded`, `forget_data`, `data` are documented in `types/risen.d.lua`.

Migrating saved data when a mod is updated: `risen.migrate("<1.1.0", function(ctx) ctx.save.set("kills", ctx.save.get("old_kills", 0)) end)` runs at `load.before` (before the mod's own handlers) when the savegame was written by a matching version (the sidecar keeps
`@mods = { id = { version } }`), and at a reload from such a version (event `mods.migrate(from, to)`).

### Updating and removing content — `risen.migrate`, `risen.retire`, `risen.mods.orphans` (M15, see `mods/upd_content`, `mods/upd_check`, [RE.md §41](RE.md))
What the game does when a mod is **updated or removed** while a savegame of the old version exists (verified, scenario `updsafe`): it loads, runs, saves and loads again without a crash, a hang or a message box in every case below. What is left behind:

| the update / the removal | what happens to the savegame |
|---|---|
| a quest is no longer defined (removed, renamed) | its saved status is dropped silently (the quest is `nil`/undefined; a renamed one starts `Open`); gone for good at the next save |
| dialogue options are no longer created | they are not offered, their "given" flags are lost with them |
| a persistent NPC of a template that no longer exists | not restored; his record stays in the sidecar |
| a persistent NPC of a shipped template | stays in the world **nameless**; the record stays unless the mod declares other NPCs (then M7 prunes it) |
| items / a recipe plan / a spell scroll of a template that is gone | dormant stacks in the inventory, back with the template |
| game variables, `risen.spawn.when` rule states, `risen.save` keys | stay until someone removes them |
| a mod without its sidecar file (savegame copied) | its NPCs are made a second time next to the saved nameless ones |

*Nothing is removed automatically* except what M7 already prunes; the tools below are explicit.
```lua
-- version 2.0 of a mod that dropped the old errand: say what it took out; the layer does it once the world is ready (new game, load, reload)
risen.retire{
  npcs = { "Old_Giver", "Old_Guard" },     -- entity (found by the ID in the record and by name), record, spawn rule state, saved AI state and routine record; skipped while this mod still declares the NPC
  quests = { "Q_Old_Errand" },             -- risen.quest.remove (clean even while running)
  game_vars = { "OldErrandDone" },         -- risen.gamevar.remove
  items = { "Spass_Old_Token" },           -- taken out of the player's inventory while the template exists
  save_keys = { "old_counter" },           -- keys of this mod's risen.save
}
-- the same lists work in mod.toml:    [retire]   npcs = ["Old_Giver"]   quests = ["Q_Old_Errand"]     (a stub mod with an empty main.lua is a way to clean up after a removed mod)
risen.migrate("<2.0.0", function(ctx)      -- data written by an older version is rewritten when the savegame is loaded (before your load.before handlers) or the mod is reloaded
  ctx.save.set("kills", ctx.save.get("wolf_kills", 0))      -- ctx.from, ctx.to, ctx.save, ctx.log
end)
```
Manager side (for [M16](NEXT_STEPS.md) / tools): `risen.mods.retire(id [, { dry_run = false }])` runs (default: only reports) what a *running* mod declared; `risen.mods.orphans(id)` is a read-only list of what the mod left in the world that nothing owns — `undeclared` (the savegame record names an NPC the
running mod does not declare, or the mod is not installed, and his entity still exists) and `stray` (an NPC of a template this process created for the mod that no record owns: the savegame had no sidecar); `risen.mods.remove_orphans(id, list)` removes the entities of a list `orphans` returned (and the records of undeclared ones), `risen.mods.forget_data(id)` drops a removed mod's record,
`risen.mods.record(id)` reads it. Shipped-template NPCs are only found through a record (a census of `Peasant_1_02` would hit thousands of unrelated NPCs).

### The mod manager page (M16, the built-in mod `mods/modmanager`, [MODDING_GUIDE.md](MODDING_GUIDE.md) 4.22, [RE.md §42](RE.md))
A **Mods** tab in the game's menu (ESC) opens a page with the list of the mods and what the manager API above can do, per mod: state (loaded, failed / skipped in red with the reason, off in grey, `restart_required` in amber), version, author, `requires` / `load_after` / `needed_by`, the data it left in this game, **Switch off / Switch on** (asks before it switches the mods that need it off too, and before it leaves NPCs of a switched-off mod in the
savegame: "Switch off and remove its NPCs" = `disable`, then `orphans` + `remove_orphans`), **Reload**, **Move up / Move down** (the load order is the custom order of `mods.toml`; a move that would put a mod before something it `requires` / `load_after`s is refused with the host's reason, the button reads "(blocked)"), **Default order**. Everything it does is an API call, so a mod can do the same. The page is `mods/modmanager` (`model.lua` = the logic with no GUI,
unit-tested; `ui.lua` = the widgets; `main.lua`), shipped with the layer and switchable off like any mod.
```lua
local st = {} ; risen.emit("modmanager.state", st)      -- st.snapshot = what the page shows { rows, detail, buttons, prompt, message, ... }, st.model, st.ui (for tests and other mods)
st.model:select("my_mod") ; st.model:press("reload")       -- "toggle" | "reload" | "up" | "down" | "reset" | "options"; st.model:answer("yes" | "alt" | "no") for a question that was asked
-- st.model.view is "mods" or "options" (the options of the selected mod, below); there the same buttons mean next value, previous value, default, all defaults, back
```

### Options menu (`risen.options`, the **Options** button of the mod manager page, [MODDING_GUIDE.md](MODDING_GUIDE.md) 4.23)
A mod declares its options as a table of entries; the mod manager shows them (ESC > Mods > the mod > **Options (n)**) and changes them **while the game runs**: the new value is checked, written into the entry, handed to the mod's `on_change`, then stored. Declare once while the mod loads:
```lua
local cfg = risen.config{ enabled = true, key = "SecondaryAction", repeat_ms = 80 }      -- config.toml + the choices made in the menu (below)
risen.options.define{
  title = "My mod",                                                                   -- heading of the page (default: the mod's name)
  { id = "enabled", name = "Enabled", value = cfg.enabled, description = "Switches the mod off." },          -- boolean -> toggle
  { id = "key", name = "Key", type = "string", kind = "dropdown", value = cfg.key, default = "SecondaryAction",
    values = { { value = "SecondaryAction", name = "Right mouse button" }, "MButton", "V" } },                -- list of values, of { value, name }, or a map value -> label
  { id = "repeat_ms", name = "Repeat (ms)", type = "number", kind = "slider", value = cfg.repeat_ms, default = 80, min = 20, max = 500, step = 20 },
  on_change = function(id, value, old) cfg[id] = value end,                           -- apply it: runs after the value changed (an error is logged and shown on the page)
  -- save = function() ... end,                                                         -- optional: you keep the values yourself; the layer then stores nothing
}
```
* **Entry:** `id` (letters, digits, `_`, `-`; by convention the `config.toml` key), `name`, `type` (`boolean` | `number` | `string`, default: the type of `value`), `kind` (`toggle` | `dropdown` | `slider` | `text`; default: `values` -> dropdown, boolean -> toggle, number with `min` and `max` -> slider, else text), `value` (current), `default` (what **Default value** returns to; default: the first `value`), `values`, `min` / `max` / `step`, `description`, `group` (a heading above the option, where the group changes: the page is laid out like the game's own Settings pages), `restart = true` (the mod only applies it at the next start; the page says so). The entries are the list part of the table (or `entries = {...}`).
  `define` returns the table, or `nil, reason` for a wrong entry (nothing is registered then). The layer keeps `value` of **your** entry tables up to date. A second `define` of the same mod replaces the first; a reload or unload forgets it.
* **The page** looks like the game's Settings screens: per option a label on the left and `<  value  >` on the right (the stock arrow and inlay images; a click on the value also takes the next one). A toggle flips with either arrow; a drop-down goes to the next / previous value (wraps around; a `value` outside the list, e.g. from `config.toml`, is shown as it is until the player picks one); a slider (numbers) is drawn like the sliders of the Graphics page, a knob on the track and the number on the right, and moves by `step` inside `min`..`max` (the arrow disappears at the end). Under the rows: the `description` and the default of the selected option. Buttons: **Default value** (the selected option), **All defaults**, **Back to the mods**; changed options are amber. A `text` entry (free text) is shown, not editable (use `config.toml`). Ten rows (headings count) make a page.
* **Storing:** without `save`, the layer writes the values that differ from their `default` into `<Risen>\bin\spass\options\<mod id>.toml` (the file is deleted when everything is at its default). `risen.config` reads it on top of `config.toml`, and `define` puts it into the entries, so a mod needs nothing more than reading `cfg` (or `value`). Delete the file to start from `config.toml` again. With `save`, the layer calls it after `on_change` and stores nothing.
* **API:** `risen.options.get(mod, id)`, `set(mod, id, value)` (same checks as the menu: type, `values`, range, rounded to `step`; then `on_change`, `save` / the store), `step(mod, id, dir)` (1 / -1: what the buttons do), `reset(mod [, id])`, `list([mod])` (a copy for UIs: per mod `{ mod, title, own_save, entries = { { id, name, type, kind, value, default, text, default_text, changed, values, index, custom, min, max, step, description, restart } } }`). `set`, `step` and `reset` return `true, report` (`{ changed, old, value, restart, error?, count? }`) or `nil, reason`.
* **Texts and languages:** every text of an entry (`name`, `description`, `group`, the labels of `values`, the `title`) may be a **string id** written with a leading `@`: `name = "@MY_Key"` is looked up with `risen.loc.get("MY_Key")` from your `strings/all.csv` (every language) and `strings/German.csv` etc. ([String files](#risen-loc--localization-overlay)) *each time the page lists the options*, not once at load. Any other text is shown as it is (`"@@x"` is the literal `@x`; a missing string shows its bare id). The game switches the text language while it runs (**Settings > Game options > Text language > Apply**); the engine rebuilds its menu pages, the manager builds its page again and the options appear in the new language, the mods need no code for it. Keep your ids unique (prefix them with your mod: `SPASS_QL_...`) and put the English texts in `all.csv`, so a language without its own file falls back to them. A toggle shows the game's own `HUD2_Text_On` / `HUD2_Text_Off` ("On" / "An", ...). **The whole mod manager is localized** (English, German; the other languages fall back to the English `all.csv`): every text of the page is a string `SPASS_ModMgr_<key>` in `mods/modmanager/strings`, and where the game already has the label in every language the manager uses the game's string instead: Options (`HUD2_Options`), Back (`HUD_Menu_Back`), Default values (`HUD2_Default`), Yes / No / Cancel (`HUD2_Button_Yes/No`, `HUD_Cancel`) and the restart warning (`HUD_Menu_ResetWarning`). What stays English: the texts a mod brings (its `description` in `mod.toml`) and the reports the layer itself writes (`risen.mods` summaries, the reason a load order is refused).
* `mods/quickloot` is the model: its key, modifier, witness mode and every switch apply at once (`enabled = false` leaves it loaded but idle), and every text is a string id (`mods/quickloot/strings`). Scenario `options` (also the language phase: the real Settings page switches the text language).

### One-process test suites (`risen.test.reset / suite_begin / suite_finish`, `mods/testsuite`)
`scripts/test-run.ps1 -Disable a,b` writes those mods as switched off into `mods.toml`; the driver mod `mods/testsuite` (parameter `suite=name:modA+modB,name2:modC`) waits for a running game, then per scenario: `risen.test.reset(name)`, `risen.mods.load` of the scenario's mods (they are told the
`session.start` / `world.ready` they missed), waits for `risen.test.finish`, unloads them, starts a **new game** (so each scenario meets an untouched world) and goes on; `suite_finish` writes one result with the checks named `<scenario>: <check>`.
Templates and other process-wide edits of earlier scenarios stay (see DEVELOPMENT.md "One-process suites"). `smoke-test.ps1 -Scenario suite` runs four scenarios in one game in ~75 s instead of ~4 minutes.

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
risen.debug.screenshot("menu", 30)    -- a PNG of the game window in <Risen>\bin\spass\screenshots\menu.png after 30 rendered frames (the OS cannot capture the D3D9 window); risen.debug.screenshot_status("menu") -> "pending" | "done" | "failed: ..."
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
