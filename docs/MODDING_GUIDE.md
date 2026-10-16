# Modding guide

A task-oriented guide to writing mods for Risen Enhanced (x64) with the Lua modding layer. It tells you **what to do for what you want** and where the traps are; the exhaustive
reference is [LUA_API.md](LUA_API.md) (every function, option and event), the verified patterns are the example mods ([EXAMPLES.md](EXAMPLES.md)), and where the numbers of the
game live is in [ITEM_PROPERTIES.md](ITEM_PROPERTIES.md), [NPC_PROPERTIES.md](NPC_PROPERTIES.md), [SPELL_PROPERTIES.md](SPELL_PROPERTIES.md) and [RECIPE_PROPERTIES.md](RECIPE_PROPERTIES.md).
**[`mods/balance_example`](../mods/balance_example/README.md) is one finished mod that uses almost everything below together: copy it as a starting point.**

Contents: [1 Install](#1-install) · [2 What a mod is](#2-what-a-mod-is) · [3 The event model](#3-the-event-model-when-to-do-what) · [4 Recipes by goal](#4-recipes-by-goal) ·
[5 Configuration](#5-configuration) · [6 Debugging, tracing and testing](#6-debugging-tracing-and-testing) · [7 Limits and gotchas](#7-limits-and-gotchas) · [8 Where to look next](#8-where-to-look-next)

## 1. Install

**Players and modders** (the layer is a proxy `sentry.dll` that `Risen.exe` loads; nothing else in the game folder changes):

```powershell
.\install.ps1   -GameDir 'C:\...\Risen' [-WithExamples]       # from the release zip (or scripts\install.ps1 in a checkout after building)
.\uninstall.ps1 -GameDir 'C:\...\Risen'                       # puts the original sentry.dll back
```

* Mods go to `<Risen>\bin\spass\mods\<mod>\`; the log is `<Risen>\bin\spass\spass.log` (read it first when something does not work: the engine itself is silent).
* `<Risen>\bin\spass\config.toml` with `enabled = false` switches the layer off without uninstalling.
* Editor support: open `bin\spass\mods` in VS Code with the *Lua* extension; the installer puts `types\risen.d.lua` and a `.luarc.json` there (completion, hover documentation, type checks; see [../types/README.md](../types/README.md)).
* **Risen Enhanced x64 only** (the engine's exported C++ API is bound by name; the layer logs `risen.engine.missing()` for anything it could not find and turns only that feature off).
* **Do not develop in your real install.** Make a copy for testing (`bin\` copied next to junctions to `data\common` and `data\compiled`, see [DEVELOPMENT.md](DEVELOPMENT.md) "Testing"), run it windowed, keep your savegames out of the way. Steam may relaunch the real game when the copy starts: `scripts\run-windowed.ps1` sets `SteamAppId` for that.

**Quick Loot** (`mods/quickloot`, [QUICKLOOT.md](QUICKLOOT.md)) is the one finished, distributable mod of the repository: copy its folder to `bin\spass\mods\quickloot`, nothing else is needed. The release zip also contains `hello`, `fetchquest`, `stringsdemo`,
`eventlog` and the `_template` mod; the small examples (`example_*`) and the showcase `balance_example` change the game's balance or demonstrate one pattern each, so the release zip keeps them in `examples\` (not installed by `install.ps1`; copy a folder into `bin\spass\mods` to use it). In a checkout `install.ps1 -WithExamples` copies everything under `mods\`, test drivers included: install single folders by hand instead.

## 2. What a mod is

```
bin\spass\mods\my_mod\
    mod.toml            required: id, name, version, author, main, load_after, requires, api_version, assets
    main.lua            runs ONCE while the game starts (no world exists yet): register things here
    *.lua               your modules, loaded with require("name") / require("sub.name") from inside the mod folder
    config.toml         optional: options the player may edit, read with risen.config{ defaults }
    strings\all.csv     optional: texts (strings\<Language>.csv per language), loaded automatically
    assets\             optional: loose meshes, textures, images, icons, actors, sounds ("Name._xmsh", "._xmat", "._ximg", "._xmac", ...): mounted at startup
    voice\<Language>\   optional: spoken lines, <option name>_<line number>.ogg|wav|mp3, loaded with risen.voice.load_dir("voice")
```

```toml
id          = "my_mod"         # required: a-z 0-9 _  (it names your save data, your generated files, your log lines)
name        = "My Mod"
version     = "1.0.0"
author      = "you"
main        = "main.lua"       # default
load_after  = ["other_mod"]    # ordering only
requires    = ["base_mod"]     # hard dependency (the mod is skipped, with an error, if it is missing)
api_version = 1                # a mod asking for a newer API generation than the layer has is skipped with an error
assets      = "assets"         # directory mounted at startup (false = none)
```

* **Load order.** Mods load in dependency order (`requires` and `load_after`), otherwise by `id`. A mod that creates templates (`new_items`, `new_npcs`) should load *before* mods that use them (`load_after`). A dependency cycle is dropped with an error.
* **Sandbox.** Every mod has its own environment: base functions, `string`, `table`, `math`, `bit`, `os.time/clock/date/difftime`; no `io`, `ffi`, `debug`, `package`, `dofile`, `loadfile`, no bytecode. Globals do not leak between mods.
  `print` writes to the log; `mod` gives `id, name, version, author, dir`. `[lua] unsafe = true` in the layer's `config.toml` lifts the sandbox for development.
* **One `risen` per mod.** `risen` in your mod reads through to the shared API, but `risen.on`, `risen.save`, `risen.config`, `risen.balance.install`, `risen.npc.persistent`, `risen.spawn.when`, ... remember *your* mod id. Mods talk to each other with `risen.emit("mymod.event", ...)` / `risen.on("mymod.event", ...)`.
* **An error in a handler** is logged (`[error] lua: [my_mod] error in handler for 'tick': ...`) and the game goes on. A mod whose `main.lua` raises is skipped; the others load.
* **Argument types are checked** everywhere: a wrong type is a Lua error (`pcall` it if the input is not yours), an unusable value gives `nil, "reason"` or `false`. A call from Lua can not crash the game (the `hardening` scenario tries ~1.6 million wrong calls on every smoke run, [DEVELOPMENT.md](DEVELOPMENT.md) "Hardening").

## 3. The event model: when to do what

There is no "world" while `main.lua` runs. The engine reports its life through events; register handlers with `risen.on(event, fn [, priority])` (higher priority runs first; `risen.off(handle)` removes one). The full table is in [LUA_API.md](LUA_API.md#events). The ones you need:

| Moment | What exists | Do this there |
|---|---|---|
| `main.lua` (load time) | no world, no templates | register handlers, `risen.script.define`, `risen.loc.set`, `risen.balance.install(...)`, `risen.npc.persistent`, `risen.spawn.when`, `risen.ai.state`, `risen.voice.load_dir` |
| first `tick` | templates resolve (`mods.loaded` is too early) | `risen.template.patch/create` (the layer's `install` does it), `risen.assets.write`, items given to templates; **loose files must be on disk before the engine loads them** |
| `load.before` | the engine is about to read a savegame; quests/infos are *not yet* restored | `risen.quest.define`, `risen.dialog.create`: **the engine restores quest status and "option used" flags only into objects that already exist** |
| `session.start` (`"new"` and `"load"`) | the world is up | the same define calls again (a new game removes runtime quests); live-entity edits (`instances = true`); everything `install`/`persistent` does for you |
| `world.ready` | the player exists (about a second later) | spawn runtime NPCs, start AI states, gameplay events for NPCs are on from here |
| `inventory.*`, `entity.died`, `quest.status_changed`, `info.finished`, ... | | **react instead of polling**: they come from hooks on the engine's own operations, dispatched at the end of the frame, so your handler may call any API |
| `tick` | every frame | keep it cheap; prefer an event or a `risen.cond` condition |

Rules that make a mod correct across savegames:

1. **Declare, do not spawn.** Anything that must exist in a game is *declared* once while the mod loads (`risen.npc.persistent`, `risen.spawn.when`, `risen.balance.install`, `risen.ai.state` + `persist`). The layer creates it in a new game, restores it after a load, and never makes it twice. The old
   "spawn it on `session.start` if `risen.entity.get(name)` is nil" duplicates the NPC on every load, because the engine saves spawned entities but not their unique name.
2. **Make `define` functions idempotent.** One `define_content()` that calls `risen.quest.define` / `risen.dialog.create` serves `load.before` and `session.start` (and `persistent{ define = ... }` calls it for you).
3. **Edits of templates are per process**, not saved: apply them in every game (`risen.balance.install` does: first `tick` and every `session.start`). A savegame keeps its *own* values of the entities that were already in the world; `instances = true` (what `install` uses) rewrites them.
4. **Triggers must be idempotent.** `quest.status_changed` may be seen again after a load; count kills in a set by entity name (the engine can report one death twice); keep "reward already given" in `risen.save`.
5. **What is saved:** quest status and log entries, "option used" flags, game variables (`risen.gamevar`), inventories (stacks of templates *your mod created* resolve again when the mod is loaded; without the mod they stay dormant and come back with it), spawned entities with position/health/inventory/AI state,
   values written into world entities. **Not saved:** the Lua state, template edits and created templates (apply them again each process), `risen.entity.spawn(.., true)` temporary entities. Your own data goes into `risen.save` (below).
6. **An open conversation blocks saving.** End every dialogue option with `{ command = "End" }`.

## 4. Recipes by goal

Every recipe: the smallest working form, what to know, the example that is run in the live test on each release. Names of templates (items, NPCs) are listed in [TEMPLATES.md](TEMPLATES.md) (`risen.template.list(group)` at run time).

### 4.1 Balance shipped items (value, damage, conditions, effects)

```lua
risen.balance.install({
    items = { It_1H_Sword = { value = 90, damage = 20, requires = { Strength = 8 } },       -- "conditions": what the wearer needs
              It_Po_Health_01 = { modifies = { HitPoints = 40 } } },                          -- effect while worn / when used
    item_groups = { { group = "Items/Potions", value_mult = 0.5 } },                          -- every template of a group path, a Lua pattern ("match") or a list
})
```
`install` validates first (a typo in a template, setting or skill name is an error in the log, not a silent no-op), then applies on the first `tick` and on every `session.start`. Relative settings (`value_mult`, `hp_mult`, `level_add`) are computed from the *shipped* values, so applying twice never compounds.
Anything without a named setting is `properties = { gCItem_PS = { Name = "value" } }` ([ITEM_PROPERTIES.md](ITEM_PROPERTIES.md) lists where the numbers live). Only the *player* is held to equip requirements; edit worn items before they are worn.
Examples: `example_balance`, `example_template_patch`; API: LUA_API "Balancing items".

### 4.2 Balance NPCs and monsters

```lua
risen.balance.install({
    npcs = { Wolf = { hp_max = 200, xp = 120, skills = { Strength = 60 }, loot = { It_Apple = 1 } },      -- absolute
             Boar = { hp_mult = 2, level_add = 2 } },                                                         -- relative to the shipped values
    npc_groups = { { match = "^Stingrat", hp_mult = 1.5, damage_mult = 2 } },
})
```
Hit points, level, experience (a kill gives `10 * level`, at least 50), attributes, protections, guild, bravery, loot. Monsters bite with their Strength (their fist item has damage 0). A new spawn follows the template at once; the monsters already in a world are rewritten on `session.start`. Loot of NPCs that already exist is not changed.
Examples: `example_balance`; API: LUA_API "Balancing NPCs and monsters"; [NPC_PROPERTIES.md](NPC_PROPERTIES.md).

### 4.3 Change a spell, add a new one

```lua
risen.balance.install({
    spells = { Fireball = { mana_cost = 10, damage = 140 } },
    new_spells = { My_Strong_Fireball = { base = "Fireball", name = "Strong Fireball", mana_cost = 20, requires = { Fireball = 2 }, damage_mult = 2, forms = { "crystal", "scroll" } } },
})
```
A spell is not an object: it is a group of items (a crystal for Fireball/Frost/Missile; rune, scroll and orb for the others) plus scripts. Cost, requirement and "used up" live in the items, the damage in the projectile. A `new_spell` makes new item templates (savegame-safe IDs) and, for projectile spells, per-shot numbers.
Numbers that are constants in the game's scripts (heals 50/100/150, the Fireball area, +25 damage per skill level) are not template data. Examples: `example_spell`; [SPELL_PROPERTIES.md](SPELL_PROPERTIES.md).

### 4.4 Add a new item or NPC type (from existing ones)

```lua
risen.balance.install({
    new_items = { My_Knife = { base = "It_1H_Machete", name = { English = "Hob's Knife", German = "Hobs Messer" }, description = "Hob made it himself.", value = 250, damage = 20 } },
    new_npcs  = { My_Hob = { base = "Peasant_1_01", name = "Hob the Tinkerer", level = 5, hp_max = 300, guild = "Cit" } },
})
```
A new template is a clone of a base with other numbers, texts, mesh/actor, icon, material. **The ID derives from the name**, so a savegame that contains it loads again; give each template a unique, prefixed name. The clone is a snapshot of the base *when it is created*: give explicit numbers if you also edit the base in the same spec.
A human clone looks like its base (heads and body parts are separate items). `risen.template.create` is the single-call form. Examples: `example_custom_npc`, `newtemplates`.

### 4.5 Crafting recipes

```lua
risen.balance.install({
    new_recipes = { My_Sword_Plan = { name = "Smithing plan: Improved Sword", station = "anvil", ingredients = { It_1H_Sword = 1, It_IronOre = 2 },
                                       result = "My_Improved_Sword", skills = { Smith = 1 }, learn = true } },
})
```
**A recipe is an item** (the plan); a character knows it while he carries it, `learn = true` gives it once per savegame. Stations: anvil, forge, grindstone, coolweapon, alchemy, goldsmith, scroll, cooking, frying. Crafting is the engine's; `risen.recipe.craft(who, recipe)` does it from a script, the real station is
`risen.interact.begin(player, anvil)` plus the page's click script (`example_recipe/test_recipe.lua` has the sequence). A `crafting` event fires once per craft. One result item per craft. Examples: `example_recipe`; [RECIPE_PROPERTIES.md](RECIPE_PROPERTIES.md).

### 4.6 Quests and dialogues (fetch, kill, hand-in)

```lua
local QUEST, NPC = "Q_My_Ore", "My_Brogar"
risen.script.define("My_Start", function() risen.quest.open(QUEST) risen.quest.run(QUEST) return 0 end)    -- scripts are what dialogue lines call
local function define_content()
    risen.quest.define({ name = QUEST, title = "Ore for the Forge", type = "Report", xp = 60 })
    risen.dialog.create({ name = "My_Work", owner = NPC, lines = {                 -- not permanent: gone once used
        { speaker = "player", listener = "npc", text = "Any work for me?" },
        { speaker = "npc", listener = "player", text = "Bring me 3 iron ore." },
        { script = "My_Start" }, { command = "End" } } })
    risen.dialog.create({ name = "My_Ore", owner = NPC,
        when = risen.cond.all { risen.cond.quest(QUEST, "Running"), risen.cond.item("It_IronOre", 3) },      -- the ENGINE offers it only when this holds
        lines = { { speaker = "player", listener = "npc", text = "Here is the ore." },
                  { command = "Give", Entity1 = "player", Entity2 = "npc", Item = "It_IronOre", Amount = 3 },
                  { script = "My_Reward" }, { command = "End" } } })
end
```
* Dialogue lines are `Say` lines, engine **commands** by name with their parameters (`Give`, `GiveXP`, `SetGameEvent`, ...: [PROPERTIES.md](PROPERTIES.md)) or `script = "Name"`. Every command is validated against the engine's own parameter lists; a bad spec is refused with `nil, "reason"` *before* anything is created.
* **Conditions** (`when = ...`, since M9): `risen.cond.quest / item / var / event / skill / guild / dead / alive / fn / all / any / negate`. What the engine knows natively (item amount, a variable existing, a skill value, an NPC dead/alive, a quest state) is evaluated by the engine; the rest (`any`, `var` with a value, `guild`, your own function) is one Lua condition behind it.
  The list of options is built when a conversation opens or an option finished: change the world, then talk again. `condition = "Running", quest = Q` (the info's own condition) still works.
* **Completing without a dialogue**: react to events (`inventory.changed` for "collect", `entity.died` for "kill") and keep counters in `risen.save`; `example_quest_tracker/tracker.lua` is a reusable table-driven tracker (dialogue, items, kills).
* A quest-bound option makes the engine append the speakers' labels to the quest log, so the log grows by more than your own `add_log` lines. A quest-state condition on a quest that does not exist yet blocks the game with a message box: `risen.dialog.create` refuses it.
Examples: `example_custom_npc`, `example_fetch_turnin`, `example_fetch_pickup`, `example_kill_quest`, `example_quest_rewards`, `example_quest_tracker`, `example_dialog_when`; API: LUA_API "Dialogues", "Dialogue conditions", "Quests".

### 4.7 Persistent NPCs

```lua
risen.npc.persistent({ name = "My_Brogar", template = "My_Brogar_T", offset = { 150, 20, 150 }, label = "Brogar the Smith", peaceful = true,
                       define = define_content })        -- quests and dialogues, run on load.before and every session.start
```
One declaration; the layer spawns the NPC in a new game, finds him again after a load by his entity ID and gives the name back, replaces him when you raise `version`, keeps a dead NPC dead (`respawn = true` replaces him at the next start), and prunes the record of an NPC you stopped declaring.
`on_ready(npc, reason)` is "he is in the world" (`"spawned"`, `"restored"`, ...), `enabled = fn` holds him back. `routine = {...}` and an AI state are applied once. The records are in your mod's `risen.save`.
**A savegame without the sidecar file makes a second NPC.** Examples: `example_custom_npc`, `persistnpc` (the executable reference).

### 4.8 NPCs that appear (or leave) when something happened

```lua
risen.spawn.when({ name = "My_Tilda", template = "My_Tilda_T", offset = { 150, 20, -250 }, label = "Tilda", peaceful = true,
                   when = risen.cond.quest("Q_My_Ore", "Success"),          -- a one-shot rule: fires once, remembered per savegame
                   despawn_when = risen.cond.event("My_Tilda_Leaves") })   -- optional: SetGameEvent in a dialogue sets it
```
The options of `risen.npc.persistent` plus `when`, `despawn_when`, `once` (`false` = follow the condition both ways), `on_fire`, `on_despawn`. Nothing is polled: a condition names the events that can change it. After every load each rule is looked at once, so a savegame made before the trigger, after it and after the despawn are all consistent.
**NPCs only** (no items). Conditions with a custom function use `risen.cond.fn(predicate, { on = events, poll = frames })`. Examples: `example_spawn_when`, `spawnwhen`; API: LUA_API "Conditional spawning".

### 4.9 Routines: posts, day and night, patrols

```lua
risen.npc.persistent({ name = "My_Guard", template = "Peasant_1_01", offset = { 250, 20, 350 }, peaceful = true,
    routine = { work = { dx = 350, dy = 0, dz = -100, kind = "guard", yaw = 90 },       -- by day (6-20): stand guard, facing east (dx, dy, dz are all required; relative to the NPC's home)
                relax = { dx = 500, dy = 0, dz = 150, kind = "lookat" }, sleep = { dx = -100, dy = 0, dz = 250, kind = "sleep" } } })
-- or a patrol: routine = { patrol = { {dx=-400,dy=0,dz=0}, {dx=-100,dy=0,dz=450}, {dx=300,dy=0,dz=-250}, name = "Camp", kind = "guard" } }
```
The same works for any NPC at run time: `risen.npc.set_routine(npc, spec)`, `get_routine`, `clear_routine`, `apply_routine` (send him on now). Points are freepoints of a kind (`risen.world.point_kinds`); positions are moved onto the walkable ground, but a post the NPC cannot walk to (across a rock, over water) leaves him standing: choose posts near his home that are connected. Humans work 6:00-20:00, relax from 20:00, sleep before 6:00.
An NPC notices a changed clock only when his task ends (a guard stands 1-2 minutes) or after `apply_routine`; monster hours, roam/local anchors, beds and benches were not driven. Examples: `example_routines`, `routines`.

### 4.10 Behaviour in Lua (AI states)

```lua
risen.ai.state("ZS_My_Watch", { interval = 0.5, task = "OnRoutine",
    loop = function(ctx) local p = risen.entity.player() if p and ctx.self:distance_to(p) < 450 then ctx.self:turn_to(p) end end,
    on = { notice = function(ctx) if ctx.other and ctx.other:is_player() then risen.hud.log("Halt!") end end } })
-- on_ready = function(npc, reason) if reason == "spawned" then risen.ai.start(npc, "ZS_My_Watch", nil, { persist = true }) end end
```
A state is run by the engine's AI like its own `ZS_*` states (`begin` once, `loop` every `interval` seconds, `finish` at the end; `ctx.reason`, `ctx.data` scratch table, `ctx.args`). `task = "OnRoutine"` keeps the engine's perception; `on` hooks the `Assess*` scripts, `risen.ai.perceive(kind, fn, filter)` handles one without a state; return `true` from a perception handler to say "handled".
**A Lua state is not saved**: `{ persist = true }` makes the layer start it again after every load. Starting a state from inside an engine callback is deferred to the end of the frame. No Lua AI *functions* and no overriding of stock states. Examples: `example_ai`, `aistates`.

### 4.11 Spoken dialogue lines

```lua
risen.voice.load_dir("voice")                                             -- voice/<Language>/<option name>_<line number>.ogg|wav|mp3, before risen.dialog.create
{ speaker = "npc", listener = "player", text = { default = "Hello!", de = "Hallo!" }, sound = { en = "sounds/hello_en.ogg", de = "sounds/hello_de.ogg" } }   -- or sound = "file" / { data = <bytes> }
```
The game's voice language picks the variant (then `default`, then English). The layer compiles the audio itself (no tool), refuses a bad file with `nil, reason`, and the line lasts as long as the audio. Opus/FLAC/M4A are refused (convert to Vorbis); there is no lip sync.
`risen.voice.say(npc, key)` is a bark. Examples: `example_voice`; API: LUA_API "Voice lines".

### 4.12 New meshes, textures, icons (loose files)

Put the files into `<mod>/assets/` (any sub-directory, the engine's compiled names: `Name._xmsh`, `._xmat`, `._ximg`, `._xmac`, `._xsnd`, ...) and name them in `risen.template.create` / `new_items` (`mesh = "Name.xmsh"`, `icon = "..."`, `material = n`). The engine finds a resource by the bare compiled file name in a cache of everything mounted; a loose file with the name of a shipped one **shadows** it.
The layer checks the names (`nil, reason` when a mesh or icon does not resolve). There is no exporter: files come from the game or from your own tools (`risen.assets.read` + patch + `risen.assets.write` derives them at run time, as `balance_example/assets.lua` and `newassets` do).
Write generated files on the first `tick` at the latest: a resource that is already loaded is not replaced. Entity *templates* themselves cannot be loose files (the engine terminates). What it looks like is not verified by the tests (screenshots of the game window are black); the checks prove the properties and that the engine loaded the files.
Examples: `example_material`, `newassets`; API: LUA_API "New meshes, textures and icons".

### 4.13 Texts and localization

`risen.loc.set("FO_My_Npc", "Hob the Tinkerer" [, language])` or `strings\all.csv` / `strings\<Language>.csv` in your mod (`id;text`). Names and descriptions of new templates take a string or `{ English = ..., German = ... }`. The label of an NPC is `FO_<name>` (`label =` of `risen.npc.persistent`). Example: `stringsdemo`.

### 4.14 Remember things in the savegame

`risen.save.get(key, default)` / `set(key, value)` / `remove` / `keys` / `clear`: a table of plain data per mod, written next to every savegame (a sidecar file), rolled back by a load, empty in a new game. Use it for counters, baselines and "reward already given". Game variables (`risen.gamevar.get/set/exists/remove`, saved by the engine) are
the right thing when a *dialogue condition* or another mod must see the value (`risen.cond.var`, `SetGameEvent`).

### 4.15 Input, interaction, HUD (building blocks)

`risen.interact` (what the player looks at, `info(entity)`, `begin(user, mob)` starts the engine's real interaction), `risen.input` (named actions, `pressed`, `simulate`), the `input.action` event (**return a value to consume the key press**), `risen.hud.log / item_log / state`, `risen.config{ defaults }` (reads the mod's `config.toml`, wrong values fall back to the defaults with a log line).
`mods/quickloot` is the complete example ([QUICKLOOT.md](QUICKLOOT.md)): the right mouse button takes what you look at, only with no weapon drawn, configurable, with the engine's own side effects (inventory events, theft checks stay with the normal use key). API: LUA_API "Interaction, input and HUD building blocks".

### 4.16 Edit any property of anything

`risen.template.get(name)` gives a template; `:get_property(set, name)` / `:set_property` / `:list_properties(set)` work by name on any property set (`gCItem_PS`, `gCNPC_PS`, ...); `risen.template.patch(name, { gCItem_PS = { GoldValue = 5 } }, { instances = true })` is the declarative form (apply on the first `tick` and on every `session.start`).
`Entity:get_property/set_property` are the same for a live entity. Never probe enum values blindly (a quest-state condition without a quest opens a blocking message box): use names. Example: `example_template_patch`; [PROPERTIES.md](PROPERTIES.md).

### 4.17 Vectors, line of sight, "where can the player not see me", randomness

```lua
local M, W, R = risen.math, risen.world, risen.random
local player = risen.entity.player()
-- spawn an enemy at a freepoint around the player that he cannot see and that lies behind him (mods/example_unseen_spawn)
local points = W.points({ near = player, radius = 2200 })                         -- the game's own waypoints / freepoints, nearest first
local cam = W.camera()                                                            -- the render camera: what the player sees
local facing = cam and { cam.dx, cam.dy, cam.dz } or player
local hidden = {}
for _, p in ipairs(points) do
    local far_enough = M.distance2d(p, player) > 700
    if far_enough and not W.is_seen_by({ p.x, p.y + 100, p.z }, player, { camera = true }) and M.is_behind(p, player, facing, 20) then hidden[#hidden + 1] = p end
end
local p = R.choice(hidden)                                                        -- nil when nothing qualifies
if p then risen.npc.spawn("Wolf", p.x, p.y + 5, p.z) end
```
* **Conventions:** +y is up, a yaw of 0 looks along +z and 90 along +x, angles are degrees. Points are entities, `{ x, y, z }` or `{ x = , y = , z = }`; `risen.math` returns `nil, reason` for bad input instead of raising. `M.offset(player, player, -300)` is a point behind the player, `M.polar(origin, yaw, distance)` a point in a direction,
  `M.relative_angle`, `M.in_cone`, `M.is_behind` answer "in front / to the side / behind".
* **Line of sight** is the engine's own ray test: terrain, rocks, trees and buildings block, open air does not, **characters never block** (the AI's sight test ignores them; `{ bodies = true }` changes that). `W.line_of_sight(a, b)` gives `true`, or `false` and what is in the way (`hit.entity:name()` is `Levelmesh_Landscape_...` for the ground, `Obj_Nat_Stone_...` for a rock).
  `W.can_see(viewer, target, { fov = 180, range, eye_height })` adds the viewer's field of view (180 is the engine's NPC perception: it agrees with the AI's `CanSee`), `W.is_seen_by(point, viewers)` asks it for a list of viewers (default: the player). For the player use `{ camera = true }`: his screen, not the way his body faces.
  A point on the ground is judged by a spot above it (+100 for a man, +50 for a wolf): the ray is one line, a head above a wall is not noticed.
* **Randomness:** `risen.random` is your mod's own stream (`int`, `float`, `chance`, `choice`, `shuffle`, `sample`, `weighted`, `point_in_ring`); `R.seed(42)` makes it repeatable for a test, `R.persist("name")` saves its state with the savegame. **Do not use `math.random` for anything that matters:** all mods share one generator that starts the same way every run.
* Examples and tests: `example_unseen_spawn`, `geometry`. Details: [LUA_API.md](LUA_API.md) "Geometry, line of sight and randomness", [RE.md](RE.md) §38.

## 5. Configuration

* **Your mod's options:** `config.toml` next to `main.lua`; `local cfg = risen.config{ enabled = true, key = "SecondaryAction", repeat_ms = 80 }` returns the merged table (types are taken from your defaults, problems are logged as `config: [my_mod] ...` and the default is used).
* **The layer's own file** `<Risen>\bin\spass\config.toml` (all optional):

| Key | Meaning |
|---|---|
| `enabled = false` | the layer does nothing |
| `log_level = "info"` | `trace`, `debug`, `info`, `warn`, `error` |
| `[lua] unsafe = true` | no sandbox (development only) |
| `[debug] trace = "events,template"` | timestamped `[trace:channel]` lines (`events`, `template`, `balance`, `save`, `spawn`, `script`, `lua`, `test`, `voice`, `calls`, `hud`, `ai`; your own name works with `risen.trace`); `trace_file = true` writes them to their own file |
| `[debug] trace_files = "text"` | log every file the engine opens whose path contains the text |
| `[debug] app_data = "..."`, `instance = "n"` | test-copy isolation (savegames and `ConfigUser.xml` stay in the copy; a second game instance may run) |
| `[test] enabled = true, scenario = "...", deadline_s = 300` | the `risen.test` run (below) |

## 6. Debugging, tracing and testing

* **Read `spass.log`** (`<Risen>\bin\spass\spass.log`): `[error] lua: [my_mod] ...` is a Lua error with the mod and the event; `[warn]` is something refused (`nil, reason`) that you ignored. The engine's own log is an empty stub: failures are silent, which is why `nil, reason` carries the reason.
* **Tools:** `mods/eventlog` logs every event with its arguments (copy it in while you work); `risen.engine.missing()` lists engine functions that were not found; `risen.debug.trace_files("name")` shows which files the engine opens; `-Trace` / `[debug] trace` as above. The numbers of any template: `mods/itemdump`, `npcdump`, `spelldump`, `recipedump`, `templatedump`.
* **Test it live, not by reading.** `risen.test` turns a mod into a scenario that plays itself and ends the game the moment it is done:

```lua
local T = risen.test
T.plan { "player exists", "quest runs" }                          -- a check that never ran fails the run as "unfinished"
T.run(function()
    T.wait_event("session.start", 180, function(mode) return mode == "new" end)    -- the autostart helper starts a new game
    T.sleep(150)                                                   -- frames, not seconds
    T.expect("player exists", risen.entity.player() ~= nil)
    risen.dialog.start(player, npc); T.sleep(90); risen.dialog.get("My_Work"):run(); T.wait_event("info.finished", 60)   -- talk like a player
    T.expect("quest runs", risen.quest.is_running("Q_My_Ore"))
end, "my_scenario")
```
```powershell
.\scripts\test-run.ps1 -Mods my_mod -WaitForResult -Seconds 240 -Scenario my_scenario [-Trace events,save] [-ShowChecks] [-Param phase=load -Start load -Keep]
.\scripts\smoke-test.ps1 -Parallel 4                  # the whole regression suite (about 16 minutes), one scenario: -Scenario showcase
```
  It runs in an **isolated windowed copy of the game** (`B:\DevDrive\risen-test` and friends in the development setup, made by the scripts), prints the failed checks and exits 0 / 1 / 2 (pass / failed / no result: crash or hang, with the last log lines and where it stopped). `-Param phase=...` with `-Start load` tests what a savegame does in a **fresh process**
  (`example_recipe` and `balance_example` do), `risen.world.quick_save() / quick_load()` inside one process. The details, the trace channels and the parallel instances are in [DEVELOPMENT.md](DEVELOPMENT.md) "Testing and tracing"; the pattern catalogue for quest tests is in [EXAMPLES.md](EXAMPLES.md).
* **Unit tests** (`build\RelWithDebInfo\spass_tests.exe`) cover the pure logic; the type check of your mods is `cd mods; lua-language-server --check . --configpath .luarc.json --checklevel Warning`.

## 7. Limits and gotchas

Collected from the milestones; each is verified or marked as inferred in [RE.md](RE.md) and [NEXT_STEPS.md](NEXT_STEPS.md).

**General**
* Risen Enhanced x64 only; the English text version was tested. Entity *templates* cannot be loose files (the engine terminates on a loose `.tple`); new templates are clones made at run time. Human appearance (heads, body parts) is not part of the NPC template.
* A game process starts in about 30 s; a new game 5 s later. A hung engine call is not recoverable (the stall watchdog of `test-run.ps1` kills it): the layer guards the known ones (unknown script names, endless names, NaN positions).
* Screenshots of the game window are black (D3D9): nothing about *appearance* is verified, only properties, loaded resources and engine state.
* The engine's log is silent; a failing engine call shows up as `nil`/`false` and a warning in `spass.log`.

**Saving and loading**
* Template edits, created templates and the Lua state are not saved: apply them in every process (`risen.balance.install` does). A savegame made with your mod and loaded without it keeps your items dormant (count 0, no crash, a save keeps them) and they resolve when the mod is back; spawned entities of a missing template are dropped.
* Entities made with `risen.entity.spawn` / `risen.npc.spawn` are saved but lose their unique name; `risen.entity.get(name)` of an old spawn is nil after a load. Use `risen.npc.persistent` / `risen.spawn.when`, or keep `entity:id()` and `risen.entity.by_id`. A dead persistent NPC stays dead; "after N days" belongs to the mod (`handle:respawn()`, no timer).
* The sidecar file of `risen.save` is next to the savegame; a savegame without it knows no persistent record and no rule state. Quests and infos must exist **before** the engine reads a savegame (`load.before`). Never save with a conversation open.
* A new game removes runtime quests: define them again on `session.start`.

**Balance and content**
* Only the player is held to equip requirements; modifiers of *worn* items drift when edited (edit before wearing). Existing entities and savegame worlds keep their own values: use `instances = true` / `install`. NPC loot edits do not reach NPCs that already exist.
* Fixed constants of the game's scripts (heal amounts, the Fireball radius, +25 per skill level, kill experience formula) are not template data. One result item per recipe; only the anvil and the alchemy table were driven as stations; the alchemy page asks for an amount in a modal dialog when several crafts are possible.
* `risen.template.create` on an existing name revives it; `remove` only gives the name back (entities and ID stay until exit). A clone is a snapshot of its base at creation.
* Voice: no lip sync, `SaySound` cannot switch language at run time, an already loaded shipped resource keeps its audio, Opus/FLAC/M4A are refused.
* Loose assets: a resource that is already loaded is not replaced (write files on the first `tick` or ship them in `assets/`); the icon in the inventory screen and the look of anything are not verified.

**Dialogues and quests**
* The option list is built when a conversation opens or an option finished and does not change while it is shown. There is no engine OR/NOT (those are the Lua script condition). `OwnerInArea`, `TeachEnabled`, `CanTeachSkill` and most `NPCStatus` values are reachable through `engine = {...}` but were not evaluated in a game.
* No dialogue command "take items / set a variable with a value" (`Give` player to NPC takes items; scripts set variables). Comment/news/hello infos and `SetTradeEnabled` in a real trade were not verified.
* A save right after a `Give` command crashed the game once: end the conversation with `End`.

**NPCs, routines, AI**
* Spawned NPCs next to hostile guilds fight at once; `peaceful = true` makes an NPC a citizen. Hit points regenerate by themselves; skills set with `add_level` did not persist.
* Routines: an NPC notices a changed clock only when his task ends or after `apply_routine`; a guard displaced more than ~400 units stays until then; a freepoint somebody stands on is not free; `stand`/`sit`/`smalltalk` points only make the NPC hang around where he is; monster hours, roam/local anchors, benches and beds, custom routine names and the poses' look were not driven. Removing a point in the frame its user continues is a crash: the layer defers removal by 40 frames.
* AI: no Lua AI functions, no override of a stock state / function / callback; `finish` is not called when an NPC is removed or the world torn down; a hook on an `Assess*` script is lost if another mod `risen.script.override`s the same script afterwards; only peasants were driven (monsters and party members were not).
* Conditional spawning: NPCs only; `cond.item` for non-player holders, `cond.alive`, late declarations and event/polling `fn` conditions are unit-tested only.

**Geometry and randomness**
* Line of sight is the engine's own ray test between two points: terrain, rocks, trees and buildings block, characters never do (unless `bodies = true`), one ray per question (a head above a wall is not noticed). The camera's field-of-view axis is inferred, not proven. `camera = true` is only meaningful for the player. During the first seconds of a new game the camera flies in.
* `math.random` is one generator for all mods and starts from the same state in every run: use `risen.random` (own stream per mod, seedable, `persist`).

**Events**
* `item.used` for food is `item.consumed` (inferred for a real meal), no "container opened" event, `entity.spawned` only for `Entity::Spawn` (not for what a savegame brings back), `Entity:set_hp(0)` is not a death, humans knocked out are `entity.defeated`, not `entity.died`.
* The interactive buy/sell screen and interactive loot/pick-up are the same engine operations as the Lua functions (checked in the decompilation) but cannot be driven from a script, so they are not run live.

**Open ideas** are collected in [NEXT_STEPS.md](NEXT_STEPS.md) "State of the roadmap".

## 8. Where to look next

| You want | Read |
|---|---|
| every function, option, event | [LUA_API.md](LUA_API.md), [../types/risen.d.lua](../types/risen.d.lua) (the same, for the editor) |
| a working pattern | [EXAMPLES.md](EXAMPLES.md) (seventeen small examples, the showcase `balance_example`, the real mod Quick Loot) |
| names of items, NPCs, monsters | [TEMPLATES.md](TEMPLATES.md) |
| where the numbers live | [ITEM_PROPERTIES.md](ITEM_PROPERTIES.md), [NPC_PROPERTIES.md](NPC_PROPERTIES.md), [SPELL_PROPERTIES.md](SPELL_PROPERTIES.md), [RECIPE_PROPERTIES.md](RECIPE_PROPERTIES.md), [PROPERTIES.md](PROPERTIES.md) |
| how the engine behaves (verified findings, RVAs) | [RE.md](RE.md) |
| working on the layer itself, tests, parallel instances | [DEVELOPMENT.md](DEVELOPMENT.md) |
| what is done and what is open | [NEXT_STEPS.md](NEXT_STEPS.md) |
