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
```

The Lua layer starts right after the game's script DLLs are loaded (`gCScriptAdmin::PostInitialize`), before any
world is running. `main.lua` runs once at that moment: **register handlers there, do not expect a running world**.

Every mod gets its own sandboxed environment: base functions, `string`, `table`, `math`, `bit` and
`os.{time,clock,date,difftime}`; no `io`, `ffi`, `debug`, `package`, `dofile`, `loadfile`, or bytecode loading.
`require("a.b")` loads `<mod dir>/a/b.lua` inside the same sandbox. `print` writes to the log.
`config.toml` `[lua] unsafe = true` lifts the restrictions for development.

Globals: `mod` (`id, name, version, author, dir`), `risen`.

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
risen.item.give(player, "It_1H_Machete", 2)               -- created amount, -1 on failure; optional 4th arg: stack type
risen.item.count(player, "It_1H_Machete")                 -- integer
risen.item.money(player)                                  -- number
```

## NPCs — `risen.npc.spawn` and the Entity NPC methods
Spawn a template and configure it in one call (see `mods/npcdemo`):
```lua
local bob = risen.npc.spawn("Peasant_1_01", x, y, z, {
    name = "SPASS_Bob",            -- unique entity name; also the key for its label: "FO_SPASS_Bob"
    level = 10, hp_max = 400,      -- hp defaults to hp_max
    mana = 100, guild = 3, group = "SPASS",
    items = { {"It_1H_Machete", 1} }, equip = true,
    state = "ZS_HangAround",       -- initial AI state (the AI may switch states immediately)
})
risen.loc.set("FO_SPASS_Bob", "Bob the Lua Peasant")   -- text shown when the player focuses him
```
Entity methods (every call returns `nil`/`false` instead of crashing on a bad handle):

| Method | Notes |
|---|---|
| `guild()`, `set_guild(n)` | `0` none/animals, `1` Don's camp, `3` peasants, `5` Inquisition, `7` pirates/bandits (observed; other values exist) |
| `group()`, `set_group(s)` | free-form group name used by AI to find allies |
| `hp()`, `hp_max()`, `level()`, `mana()`, `mana_max()` | |
| `set_hp_max(n)`, `set_hp(n)`, `set_level(n)`, `set_mana(n)`, `set_mana_max(n)` | absolute (the engine setters are additive; the wrapper computes the difference). Set `hp_max` before `hp` |
| `add_hp(d)`, `add_hp_max(d)`, `add_level(d)`, `add_mana(d)`, `add_mana_max(d)` | relative |
| `state()`, `set_state("ZS_…")` | AI state scripts: `ZS_Ambient`, `ZS_AmbientObserve`, `ZS_AmbientSleep`, `ZS_HangAround`, `ZS_Housewife`, `ZS_Observe`, `ZS_Watch`, `ZS_Study`, `ZS_Smith`, `ZS_Trader`, `ZS_Talk`, `ZS_FollowPlayer`, `ZS_GuidePlayer`, `ZS_WaitForPlayer`, `ZS_Attack`, `ZS_Dead`, `ZS_Monster`, … |
| `full_stop()` | cancel the current AI action |
| `goto_pos(x,y,z[,mode])`, `goto_entity(e[,mode])`, `stop_goto()`, `turn_to(e)` | `mode` is the engine's walk mode enum (0 default) |
| `equip_all()`, `stack_count()` | equips every equippable inventory stack |

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
`Quest` methods `name()`, `set_topic(key)`, `add_log_entry(key)`, `log_count()`, `set_property/get_property`, `register()`.
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
risen.world.is_game_running()  risen.world.new_game()  risen.world.quick_load()
risen.world.quick_save()  risen.world.auto_save("name")  risen.world.is_save_allowed()
```
`new_game`, `quick_load`, `quick_save` and `auto_save` are *requests*: the engine performs them on its next processing step
(watch `save.after` / `load.after` / `session.start`).

## risen.loc — localization overlay
```lua
risen.loc.trace(true)                                 -- log every string id the game reads
risen.loc.set("GO_QuestRunning", "Quest läuft", "German")   -- optional language; default: all languages
risen.loc.get("GO_QuestRunning"), risen.loc.exists(id), risen.loc.clear(id), risen.loc.language()
```
Ids are matched case-insensitively. The game's own tables are never modified (they are read-only at runtime).

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

## Files
Log: `<Risen>\bin\spass\spass.log`, config: `<Risen>\bin\spass\config.toml`
(`enabled`, `log_level`, `[lua] unsafe`, `[debug] app_data` – redirects the game's `%LOCALAPPDATA%`/`%APPDATA%`;
`scripts/test-run.ps1` uses it to keep a test copy's savegames and `ConfigUser.xml` away from the real ones).
