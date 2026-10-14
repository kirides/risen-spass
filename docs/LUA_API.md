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

```lua
local h = risen.on("tick", function() ... end, 10)   -- optional priority, higher runs first
risen.off(h)
risen.emit("my_mod.event", a, b)                      -- mod-to-mod events
```

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
`enable_rendering(b)`, `teleport(name_or_entity)`, `address`. Handles are raw pointers: do not keep them across
frames or loads; failures return `nil`/`false` instead of crashing.

## risen.template / risen.entity.spawn / risen.item
Template names are the blueprint names stored in `data/common/templates.pak` (`It_1H_Machete`, `It_1H_Sword`, …).
```lua
risen.template.exists("It_1H_Machete")                    -- bool
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

**Persistence:** entities created at runtime are part of the world and can be saved; Lua state is not. Guard
spawns with `risen.entity.get("YourName")` so a loaded save does not get a second copy.

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
with an existing quest** (`quest = ...`), otherwise the engine reports an error.

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
Quests created at runtime are part of the quest manager and its savegame data; define them from `main.lua`/the first tick
of every session (`if not risen.quest.exists(...)`).

## risen.quest / risen.gamevar / risen.world
```lua
risen.quest.open|run|close|fail|succeed("Quest_Id")   risen.quest.is_running("Quest_Id")
risen.gamevar.get("Name") -> int|nil                   risen.gamevar.set("Name", 1)
risen.world.is_game_running()  risen.world.new_game()  risen.world.quick_load()
```

## risen.loc — localization overlay
```lua
risen.loc.trace(true)                                 -- log every string id the game reads
risen.loc.set("GO_QuestRunning", "Quest läuft", "German")   -- optional language; default: all languages
risen.loc.get("GO_QuestRunning"), risen.loc.exists(id), risen.loc.clear(id), risen.loc.language()
```
Ids are matched case-insensitively. The game's own tables are never modified (they are read-only at runtime).

## Files
Log: `<Risen>\bin\spass\spass.log`, config: `<Risen>\bin\spass\config.toml`
(`enabled`, `log_level`, `[lua] unsafe`).
