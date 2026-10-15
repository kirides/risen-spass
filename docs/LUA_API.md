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
| `npc.killed` / `npc.defeated` | `entity` | an NPC died / was knocked out in combat |
| `info.started` / `info.finished` | `info_name, owner` | a dialogue option was chosen / finished playing |
| `item.picked_up` | `owner, template, amount` | an inventory took an item entity from the world (`owner` is the entity whose inventory it is) |

```lua
local h = risen.on("tick", function() ... end, 10)   -- optional priority, higher runs first
risen.off(h)
risen.emit("my_mod.event", a, b)                      -- mod-to-mod events
```
`mods/eventlog` logs every event above with its arguments (development aid).

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
`lua` (handler errors with traceback), `test`. A disabled channel costs one bit test; use `risen.trace.enabled(ch)` to guard expensive message building.

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
  an unknown setting or template is an error listing the valid settings. More sections: `npcs`, `npc_groups` and `item_groups` (see "Balancing NPCs"); later features register theirs (`spells`, ...) with `risen.balance.register_section`.
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
  `icon` (`gCItem_PS.IconImage`) and `material` (`MaterialSwitch`). `properties` reaches everything else (e.g. `gCItem_PS = { ItemInventory = ... }`). **Everything is validated before anything is created** (names, base, setting names, types, skill
  names, conflicts such as `mesh` plus `properties.eCMesh_PS.MeshFileName`); a bad request returns `nil, reason` and creates nothing. Names: letters, digits and `_`, not starting with a digit, at most 63 characters; a shipped template name is refused.
* **Labels**: the engine shows `FO_<name>` for the focus label of an entity and for the inventory name of an item (a `focus_unique:FO_<name>` entry wins), `ITEMDESC_<name>` for an item's description. `name`/`description` set them with `risen.loc.set`;
  a mod's `strings/all.csv` with the same ids works too. A clone without `name` shows its base's label unless a mod already supplied one. A key set without table prefix (`FO_Bob`) now also answers the prefixed id the engine asks for (`focus:FO_Bob`).
* **The ID** is derived from the name, so the same mod creates the same template in every process on every machine: **savegames that contain items of it resolve again** (a stack stores the template's ID). Create the templates on the first `tick` (as
  `install` does), before a savegame is loaded. Verified live in a fresh process: items in the player's and a world NPC's inventory survive a restart; if the template is **missing** in a session the stacks stay dormant (`risen.item.count` 0, `stack_item` nil, the game
  runs, a save keeps them) and **resolve as soon as the template exists**, even when it is created after the load. Spawned entities are not saved by the engine (RE.md §14), so only inventories (and world entities that existed before) carry clones.
* **A clone is a snapshot** of its base when it is created (later edits of the base do not reach it); creating again with the same base only re-applies the overrides. Existing entities keep their own values (`instances = true` writes them). The clone lives until the
  process ends; `remove` only gives the names back (refused while live entities of it exist) -- items already handed out keep working -- and `create` with the same base brings it back with the same ID.
* **Timing**: items and NPC templates can be cloned from the first `tick` on, also while a world is loaded (a hook skips `gCNavigation_PS::OnPostCopy` during the copy: it crashed on templates once a world existed). The base is loaded on demand.
* **Not covered**: new meshes/textures/icons (only existing ones can be referenced, M5), spells, appearance parts of humans (heads are `eCBodyPart_PS` item templates `Hero_Head_*`, not part of the NPC template), multi templates (refused).

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
risen.vfs.mount("mymod", mod.dir)     -- mount a physical directory (files appear under mymod/..., and shadow packed files of the same relative path)
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
