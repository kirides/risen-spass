<#
.SYNOPSIS
    End-to-end smoke test: launches the isolated test copy once per scenario, plays it and reports a pass/fail table.

      persistence - new game -> runtime content -> quick save -> change -> quick load -> new game (mods/persistdemo)       [log checks]
      fetchquest  - the example quest played through by mods/fetchquest_driver                                              [log checks]
      commands    - dialogue commands (mods/dialogcmds)                                                                     [log checks]
      branch      - branching dialogue (mods/dialogbranch)                                                                  [log checks]
      templates   - template editing (mods/templatepatch + templatepatch_driver)                                            [risen.test]
      balance     - item balancing (mods/balance_items + balance_driver)                                                    [risen.test]
      npcs        - NPC balancing (mods/balance_npcs + balance_npcs_driver)                                                 [risen.test]
      newtemplates - new item/NPC templates (mods/newtemplates + newtemplates_driver); five runs: save, then four fresh-process loads
                     (templates created early / missing / early again / late)                                                [risen.test]
      newassets    - new meshes, textures and icons from loose files (mods/newassets); two runs: build (derive + play), use (fresh process, files mounted at startup)
      spells       - spell balancing and new spells (mods/balance_spells): real casts through the engine's state scripts; two runs: save, then a fresh-process load
      ex_spell     - example: edit Fireball/Light, a new Strong Fireball crystal and scroll (mods/example_spell)
      recipes      - recipe balancing (mods/balance_recipes): the real anvil and alchemy table through the engine's interaction scripts
      ex_recipe    - example: a new smithing recipe (sword + 2 iron ore -> Slightly Improved Sword); four runs: save, load, load_missing, load_again
      ex_voice     - spoken dialogue lines from ogg/wav/mp3 files (mods/example_voice); two runs: voice language English, then German
      persistnpc   - persistent NPCs (M7, mods/persistnpc): new game + in-process load + new game, then fresh-process loads (same declaration, changed declaration, lost templates)
      spawnwhen    - conditional spawning (M8, mods/spawnwhen): quest / items / game variable / death triggers, saves before and after them, loaded in the same and in fresh processes; four runs
      ex_spawn_when - example: a messenger while a quest runs, a smith after it succeeded who leaves after a dialogue choice (mods/example_spawn_when)
      dialogcond   - dialogue conditions (M9, mods/dialogcond): engine conditions (item, event, skill, NPC status, quest) and a Lua script condition (any, negate, guild, predicates); two runs: save (state switched step by step), fresh-process load
      ex_dialog_when - example: a smuggler whose secret needs iron ore AND trust AND a guild (mods/example_dialog_when)
      routines     - NPC routines (M10, mods/routines): guard post, displaced guard, work/relax/sleep by the clock, a patrol ring, idempotence, a persistent NPC with a `routine`; save + in-process load, then fresh-process loads (same declaration, changed declaration)
      ex_routines  - example: a gatekeeper with a day, an evening and a night post and a patrol (mods/example_routines)
      aistates     - Lua AI states, callbacks and perception handlers (M11, mods/aistates): begin / loop / finish on real NPCs, walking, errors, a bitten NPC that flees; save + in-process load, then fresh-process loads (the states defined, the mod removed)
      ex_ai        - example: a follower and a sentry who warns the player, a guard who flees when hit (mods/example_ai)
      showcase     - the showcase mod (M12, mods/balance_example): balance of items/monsters/spells, new items (one with loose mesh and texture), a recipe, a persistent smith with a voiced dialogue and an iron-ore quest, an apprentice who appears afterwards (routine), a watchman (Lua AI); play + quick save/load, then a fresh-process load
      geometry     - vectors, the engine's ray test and field of view, the render camera, random streams (M13, mods/geometry): yaw convention against real entities, open air / terrain / rock, can_see against the AI's CanSee, camera, risen.random.persist across a quick save + load
      ex_unseen_spawn - example: an enemy appears at a waypoint/freepoint the player cannot see and that lies behind him (mods/example_unseen_spawn)
      damagehooks  - damage hooks (M14, mods/damagehooks): risen.combat.on_damage against real fireball casts (three dummies, area falloff), wolf bites and club blows; scaling, cancel, chains, filters, errors, unregister, a nested DoDamage; save, then a fresh-process load
      ex_combat    - example: a Fireball that hits harder every time it hits the same target, counters in risen.save, reset on death (mods/example_stacking_fireball); save, then a fresh-process load where the count continues
      quickloot_react - quick loot and the NPCs (M5e, mods/quickloot): the engine's theft assessment on the quick path against the animated path in a watched area (items, chests, cupboards, sarcophagi, corpses, out of sight / range, dropped items); witness_mode ignore in a second run
      ex_ignore_theft - example: NPCs that ignore thefts while a game variable is set (risen.ai.perceive("theft") answers 0), animated and quick path (mods/example_ignore_theft)
      extplay      - the generated mod's play checks: a conversation runs added GiveXP / RunScript commands, Sam teaches past the game's cap and refuses at it, the melee table's styles (risen.npc.combat_style), a fight with a two-hander; skipped when the generated mod is absent
      extmod       - the generated Risen Extended mod (mods/risen_extended, made by risen-pack2mod or tools/extmod_gen.py; docs/EXTENDED_MOD_ANALYSIS.md): applied entries, the archive numbers in the engine, new items spawn / equip, recipes craft, monster actors, loose assets, save + load in a second process; skipped when the generated mod is absent
      hardening    - every public function and method called with ~1.6 million wrong-typed / hostile argument combinations (mods/hardening): the game must survive and keep ticking

.DESCRIPTION
    Needs a built sentry.dll and the test copy (see CLAUDE.md). The [risen.test] scenarios end as soon as they are done
    (the mod layer writes spass\test-result.json, see docs/DEVELOPMENT.md "Testing and tracing"): about a minute each. The
    [log checks] scenarios end themselves too (the driver mod calls risen.test.finish after its last step) and additionally assert on
    regexes over spass.log: 60-90 s each.
    Every scenario runs under the watchdog of test-run.ps1 (no new log line for -StallSeconds, or over its time limit: the
    game is killed and the last log lines and "where it stopped" are printed). Exit code 0 = every selected scenario passed.

.PARAMETER GameDir   Test copy of Risen (never the real install).
.PARAMETER Seconds   Upper time limit of the [log checks] scenarios (fetchquest and branch get 60 s more, commands 20 s less).
.PARAMETER Scenario  One or more scenario names (comma separated), or all.
.PARAMETER List      Print the scenarios and exit.
.PARAMETER FailFast  Stop after the first failing scenario.
.PARAMETER TotalSeconds  Time budget of the whole run: a scenario that cannot start with at least 90 s left is skipped (and counts as a failure).
.PARAMETER StallSeconds  Watchdog: kill the game after this long without a new log line.
.PARAMETER Parallel  Number of game instances running at the same time (default 1 = one after the other in the main test copy). With N > 1
                    the scenarios are a work queue over the test copies risen-test, risen-test2 .. risen-testN (created on first use); the
                    wall time drops to roughly max(longest scenario, sum / N). See docs/DEVELOPMENT.md "Parallel instances".
.PARAMETER Instance  Internal (used by -Parallel for its child runs): run in the test copy of this parallel instance (default 0 = main copy).
.PARAMETER ResultsFile  Internal: write the result rows as JSON to this file.
.PARAMETER VerboseChildren  With -Parallel: print the complete output of every scenario (default: only of the failed ones, one line for a passed one).
#>
param(
    [string]$GameDir = '',
    [int]$Seconds = 200,
    [ValidateSet('persistence', 'fetchquest', 'commands', 'branch', 'templates', 'balance', 'npcs', 'newtemplates', 'newassets', 'events', 'spells', 'ex_spell', 'recipes', 'ex_recipe', 'ex_template_patch', 'ex_balance', 'ex_custom_npc', 'ex_fetch_turnin', 'ex_fetch_pickup', 'ex_kill_quest', 'ex_quest_rewards', 'ex_quest_tracker', 'ex_material', 'ex_voice', 'quickloot', 'persistnpc', 'spawnwhen', 'ex_spawn_when', 'dialogcond', 'ex_dialog_when', 'routines', 'ex_routines', 'aistates', 'ex_ai', 'hardening', 'showcase', 'geometry', 'ex_unseen_spawn', 'damagehooks', 'ex_combat', 'quickloot_react', 'ex_ignore_theft', 'hotreload', 'suite', 'updsafe', 'gui', 'modmanager', 'options', 'extmod', 'extplay', 'all')][string[]]$Scenario = @('all'),
    [switch]$List,
    [switch]$FailFast,
    [int]$TotalSeconds = 3600,
    [int]$StallSeconds = 90,
    [int]$Parallel = 1,
    [int]$Instance = 0,
    [string]$ResultsFile = '',
    # -Parallel: print the complete output of every child, not only of the failed ones
    [switch]$VerboseChildren
)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
if ($GameDir -eq '') { $GameDir = if ($Instance -ge 2) { "B:\DevDrive\risen-test$Instance" } else { 'B:\DevDrive\risen-test' } }
$log = Join-Path $GameDir 'bin\spass\spass.log'

# ---- the regex checks of the scenarios that still assert on spass.log: description, regex that must match the log ----
$persistence = @(
    @('mod layer started',                     'game ready: starting Lua runtime'),
    @('all engine symbols resolved',           'symbols: \d+ resolved in this pass, 0 waiting'),
    @('session.start(new) fired',              'eventlog session\.start\(new\)'),
    @('world.changed fired',                   'eventlog world\.changed\(World\)'),
    @('runtime content created',               'persistdemo \[created\]: entity=Entity\(SPASS_Pers.*quest=true.*info=true.*runs=1 marker=from-first-session'),
    @('quest.status_changed fired',            'eventlog quest\.status_changed\(Q_SPASS_Pers, Running, Open\)'),
    @('savegame written',                      'save: saved ''QuickSave'''),
    @('save.after reported success',           'eventlog save\.after\(QuickSave, true\)'),
    @('mod data was changed before the load',  'persistdemo \[before load\].*marker=changed-after-save'),
    @('load.before fired before load.after',   '(?s)eventlog load\.before\(QuickSave\).*eventlog load\.after\(QuickSave, true\)'),
    @('mod data rolled back by the load',      'persistdemo \[load\.after QuickSave ok=true\].*marker=from-first-session'),
    @('quest state survived the load',         'persistdemo \[load\.after QuickSave ok=true\].*quest=true status=gEQuestStatus_Running log_entries=1'),
    @('info "given" flag survived the load',   'persistdemo \[load\.after QuickSave ok=true\].*given=true'),
    @('old entity handle is gone after load',  'persistdemo \[load\.after QuickSave ok=true\]: entity=nil'),
    @('session.start(load) fired',             'eventlog session\.start\(load\)'),
    @('new game removes runtime quests',       'persistdemo \[session\.start\(new\) before ensure\]: entity=nil quest=false status=nil.*runs=nil'),
    @('no Lua errors',                         '^(?!(?s).*\[error\])')
)
$fetch = @(
    @('Marta was spawned',                      'driver: Marta exists, quest status = gEQuestStatus_Open'),
    @('the work option started the quest',      'driver: quest running, log entries = 1'),
    @('Marta is spawned again after a load',    'driver: after load: Marta respawned=true same_spot=true'),
    @('quest and dialogue options survive',     'driver: after load: .*quest=gEQuestStatus_Running infos=true/true'),
    @('hand-in with too few apples is refused', 'driver: not enough apples: status=gEQuestStatus_Running .*apples=1'),
    @('hand-in with 3 apples completes it',     'driver: DONE status=gEQuestStatus_Success apples=0 machetes=1'),
    @('no Lua errors',                          '^(?!(?s).*\[error\])')
)
$cmds = @(
    @('invalid command lines are refused',     'cmds: invalid specs refused: true'),
    @('unknown command names the problem',     'unknown command ''NoSuchCommand'''),
    @('unknown parameter names the problem',   'command GiveXP has no parameter ''Count'''),
    @('command parameters read back',          'cmds: command 2 Amount=2 Item=It_Apple'),
    @('Give delivered the items',              'cmds: after: apples=2 event=true'),
    @('no Lua errors',                         '^(?!(?s).*\[error\])')
)
$branch = @(
    @('only the parent is offered at the top',  '(?m)branch: top-level options: PANKRATZX2_00004,SPASS_Br_Town\r?$'),
    @('the children replace the menu',          'branch: the children are offered: SPASS_Br_Back,SPASS_Br_Harbor,SPASS_Br_Monastery'),
    @('a child answers and the menu stays',     'branch: after the child: SPASS_Br_Back,SPASS_Br_Harbor,SPASS_Br_Monastery running=true'),
    @('Back returns to the top level',          'branch: back at the top level: PANKRATZX2_00004,SPASS_Br_Town'),
    @('no Lua errors',                          '^(?!(?s).*\[error\])')
)

# name -> definition. Kind 'log': fixed run time, regex checks. Kind 'result': a risen.test scenario that ends itself.
$scenarios = [ordered]@{
    persistence = @{ Est = 95; Title = 'persistence and events'; Kind = 'log'; Mods = @('persistdemo', 'eventlog'); Limit = $Seconds; Checks = $persistence }
    fetchquest  = @{ Est = 90; Title = 'fetch quest example'; Kind = 'log'; Mods = @('fetchquest', 'fetchquest_driver'); Limit = $Seconds + 60; Checks = $fetch }
    commands    = @{ Est = 60; Title = 'dialogue commands'; Kind = 'log'; Mods = @('dialogcmds'); Limit = $Seconds - 20; Checks = $cmds }
    branch      = @{ Est = 80; Title = 'branching dialogue'; Kind = 'log'; Mods = @('dialogbranch'); Limit = $Seconds + 60; Checks = $branch }
    templates   = @{ Est = 60; Title = 'template editing'; Kind = 'result'; Mods = @('templatepatch', 'templatepatch_driver'); Limit = 400 }
    balance     = @{ Est = 60; Title = 'item balancing'; Kind = 'result'; Mods = @('balance_items', 'balance_driver'); Limit = 400 }
    events      = @{ Est = 75; Title = 'gameplay events (M5b)'; Kind = 'result'; Mods = @('events_demo'); Limit = 400 }
    npcs        = @{ Est = 70; Title = 'NPC balancing'; Kind = 'result'; Mods = @('balance_npcs', 'balance_npcs_driver'); Limit = 500 }
    # docs/EXAMPLES.md: the example mods, each one a self-contained risen.test scenario
    ex_template_patch = @{ Est = 50; Title = 'example: patch templates'; Kind = 'result'; Mods = @('example_template_patch'); Limit = 300 }
    ex_balance        = @{ Est = 45; Title = 'example: balancing'; Kind = 'result'; Mods = @('example_balance'); Limit = 300 }
    ex_custom_npc     = @{ Est = 70; Title = 'example: custom NPC and quest'; Kind = 'result'; Mods = @('example_custom_npc'); Limit = 300 }
    ex_fetch_turnin   = @{ Est = 80; Title = 'example: fetch quest, hand-in'; Kind = 'result'; Mods = @('example_fetch_turnin'); Limit = 300 }
    ex_fetch_pickup   = @{ Est = 60; Title = 'example: fetch quest, collect'; Kind = 'result'; Mods = @('example_fetch_pickup'); Limit = 300 }
    ex_kill_quest     = @{ Est = 75; Title = 'example: kill quests'; Kind = 'result'; Mods = @('example_kill_quest'); Limit = 300 }
    ex_quest_rewards  = @{ Est = 100; Title = 'example: quest completion triggers'; Kind = 'result'; Mods = @('example_quest_rewards'); Limit = 300 }
    ex_quest_tracker  = @{ Est = 95; Title = 'example: dialogue/item/kill quest tracker'; Kind = 'result'; Mods = @('example_quest_tracker'); Limit = 300 }
    ex_material       = @{ Est = 50; Title = 'example: material variants and a loose texture'; Kind = 'result'; Mods = @('example_material'); Limit = 300 }
    # custom voice lines (M5c): two game processes, the voice language of the game is English, then German (test-run.ps1 -VoiceLanguage); the second proves the variant choice
    ex_voice          = @{ Est = 140; Title = 'example: voiced dialogue lines (en, de)'; Kind = 'result'; Mods = @('example_voice'); Limit = 200
                           Phases = @(
                               @{ Name = 'english'; Param = @('lang=English'); Start = 'new'; Keep = $false; Limit = 200 }
                               @{ Name = 'german'; Param = @('lang=German'); VoiceLanguage = 'German'; Start = 'new'; Keep = $false; Limit = 200 }
                           ) }
    # spell balancing and new spells (M6): the player really casts (PS_Magic -> PS_MagicBall_Shoot); "save" plays the scenario and saves, "load" casts a saved scroll in a fresh process
    spells      = @{ Est = 125; Title = 'spells (M6)'; Kind = 'result'; Mods = @('balance_spells'); Limit = 200
                     Phases = @(
                         @{ Name = 'save'; Param = @('phase=save'); Start = 'new'; Keep = $false; Limit = 200 }
                         @{ Name = 'load'; Param = @('phase=load'); Start = 'load'; Keep = $true; Limit = 120 }
                     ) }
    ex_spell          = @{ Est = 60; Title = 'example: spells (edit, new crystal and scroll)'; Kind = 'result'; Mods = @('example_spell'); Limit = 300 }
    # crafting recipes (M6b): the real smithing anvil and alchemy table are driven through the engine's interaction scripts (risen.interact.begin + the page's click script)
    recipes           = @{ Est = 75; Title = 'recipe balancing (M6b)'; Kind = 'result'; Mods = @('balance_recipes'); Limit = 300 }
    # persistent NPCs (M7): "save" plays new game -> dialogue/quest -> state changes -> quick save -> quick load -> new game; the others load that quick save in a fresh process
    # with the same declaration ("load"), a changed one ("load_changed": version bump, an NPC no longer declared) and without the mod's NPC templates ("load_missing")
    persistnpc        = @{ Est = 190; Title = 'persistent NPCs (M7: save, load, changed, missing)'; Kind = 'result'; Mods = @('persistnpc'); Limit = 200
                           Phases = @(
                               @{ Name = 'save'; Param = @('phase=save'); Start = 'new'; Keep = $false; Limit = 200 }
                               @{ Name = 'load'; Param = @('phase=load'); Start = 'load'; Keep = $true; Limit = 150 }
                               @{ Name = 'load_changed'; Param = @('phase=load_changed'); Start = 'load'; Keep = $true; Limit = 150 }
                               @{ Name = 'load_missing'; Param = @('phase=load_missing'); Start = 'load'; Keep = $true; Limit = 150 }
                           ) }
    # conditional spawning (M8): "save_before" (new game, a quest completion creates an NPC, a new game removes it, quick save before any trigger), "main" (fresh process loads that earlier save, every kind
    # of trigger, in-process loads of an earlier and a later save, leaves the later save), "load_after" (fresh process: the later save), "load_changed" (fresh process, edited rules)
    spawnwhen         = @{ Est = 230; Title = 'conditional spawning (M8: quest, items, variable, death; save before/after)'; Kind = 'result'; Mods = @('spawnwhen'); Limit = 200
                           Phases = @(
                               @{ Name = 'save_before'; Param = @('phase=save_before'); Start = 'new'; Keep = $false; Limit = 200 }
                               @{ Name = 'main'; Param = @('phase=main'); Start = 'load'; Keep = $true; Limit = 300 }
                               @{ Name = 'load_after'; Param = @('phase=load_after'); Start = 'load'; Keep = $true; Limit = 150 }
                               @{ Name = 'load_changed'; Param = @('phase=load_changed'); Start = 'load'; Keep = $true; Limit = 150 }
                           ) }
    ex_spawn_when     = @{ Est = 100; Title = 'example: NPCs that appear and disappear'; Kind = 'result'; Mods = @('example_spawn_when'); Limit = 300 }
    # dialogue conditions (M9): "save" (new game: every kind of condition against the engine, the world changed step by step, conversations, call counts, bad specs; leaves a quick save),
    # "load" (a fresh process loads it: the options defined in load.before offer what the saved world says, the state is switched again)
    dialogcond        = @{ Est = 120; Title = 'dialogue conditions (M9: engine and Lua script conditions; save, fresh-process load)'; Kind = 'result'; Mods = @('dialogcond'); Limit = 200
                           Phases = @(
                               @{ Name = 'save'; Param = @('phase=save'); Start = 'new'; Keep = $false; Limit = 200 }
                               @{ Name = 'load'; Param = @('phase=load'); Start = 'load'; Keep = $true; Limit = 150 }
                           ) }
    ex_dialog_when    = @{ Est = 90; Title = 'example: dialogue options that depend on items, a variable and a guild'; Kind = 'result'; Mods = @('example_dialog_when'); Limit = 300 }
    # NPC routines (M10): "save" (new game: validation, stock points, guard + displacement, day/night, patrol ring, idempotence; quick save; quick load in the same process), "load" (a fresh process loads that
    # quick save: points, patrol anchors renamed from the mod's record, persistent NPC not re-made), "load_changed" (a fresh process whose persistent NPC has another routine: old post removed, new one used)
    routines          = @{ Est = 190; Title = 'NPC routines (M10: guard, day/night, patrol; save, fresh-process load, changed declaration)'; Kind = 'result'; Mods = @('routines'); Limit = 300
                           Phases = @(
                               @{ Name = 'save'; Param = @('phase=save'); Start = 'new'; Keep = $false; Limit = 300 }
                               @{ Name = 'load'; Param = @('phase=load'); Start = 'load'; Keep = $true; Limit = 150 }
                               @{ Name = 'load_changed'; Param = @('phase=load_changed'); Start = 'load'; Keep = $true; Limit = 150 }
                           ) }
    ex_routines       = @{ Est = 100; Title = 'example: a gatekeeper (day, evening, night) and a patrol'; Kind = 'result'; Mods = @('example_routines'); Limit = 300 }
    # Lua AI states (M11): "save" (new game: registry, refusals, a Lua state on real NPCs, errors in every callback kind, task callbacks, a damage handler that sends a bitten NPC running, the follower; quick save,
    # quick load in the same process), "load" (a fresh process resumes the saved state), "load_missing" (a fresh process whose mod defines no states: the engine drops the unknown state)
    aistates          = @{ Est = 270; Title = 'Lua AI states and perception (M11: begin/loop/finish, walking, errors, damage; save, fresh-process load, removed mod)'; Kind = 'result'; Mods = @('aistates'); Limit = 400
                           Phases = @(
                               @{ Name = 'save'; Param = @('phase=save'); Start = 'new'; Keep = $false; Limit = 400 }
                               @{ Name = 'load'; Param = @('phase=load'); Start = 'load'; Keep = $true; Limit = 150 }
                               @{ Name = 'load_missing'; Param = @('phase=load_missing'); Start = 'load'; Keep = $true; Limit = 150 }
                           ) }
    ex_ai             = @{ Est = 110; Title = 'example: a companion, a sentry and a coward (Lua AI states)'; Kind = 'result'; Mods = @('example_ai'); Limit = 300 }
    # the showcase mod (M12): "play" (new game: every feature, quick save, quick load in the same process, a second save), "load" (a fresh process loads that savegame)
    showcase          = @{ Est = 200; Title = 'showcase mod (M12: balance, new items, recipe, quest, apprentice, routine, AI; save, fresh-process load)'; Kind = 'result'; Mods = @('balance_example'); Limit = 400
                           Phases = @(
                               @{ Name = 'play'; Param = @('phase=play'); Start = 'new'; Keep = $false; Limit = 400 }
                               @{ Name = 'load'; Param = @('phase=load'); Start = 'load'; Keep = $true; Limit = 150 }
                           ) }
    # geometry (M13): one process: conventions against real entities, rays (air, terrain, rock, characters), can_see against the AI's CanSee, camera, risen.random.persist across a quick save + load
    geometry          = @{ Est = 60; Title = 'geometry (M13: vectors, line of sight, field of view, camera, random streams)'; Kind = 'result'; Mods = @('geometry'); Limit = 300 }
    ex_unseen_spawn   = @{ Est = 45; Title = 'example: an enemy where the player cannot see it'; Kind = 'result'; Mods = @('example_unseen_spawn'); Limit = 300 }
    # damage hooks (M14): "save" plays everything in a new game and leaves a quick save, "load" (a fresh process) registers the handler at start and casts again
    damagehooks       = @{ Est = 175; Title = 'damage hooks (M14: real casts, bites and blows; save, fresh-process load)'; Kind = 'result'; Mods = @('damagehooks'); Limit = 300
                           Phases = @(
                               @{ Name = 'save'; Param = @('phase=save'); Start = 'new'; Keep = $false; Limit = 300 }
                               @{ Name = 'load'; Param = @('phase=load'); Start = 'load'; Keep = $true; Limit = 150 }
                           ) }
    ex_combat         = @{ Est = 120; Title = 'example: a Fireball that hits harder every time (counters survive a load)'; Kind = 'result'; Mods = @('example_stacking_fireball'); Limit = 300
                           Phases = @(
                               @{ Name = 'save'; Param = @('phase=save'); Start = 'new'; Keep = $false; Limit = 300 }
                               @{ Name = 'load'; Param = @('phase=load'); Start = 'load'; Keep = $true; Limit = 150 }
                           ) }
    # hardening: the fuzz scenario (docs/DEVELOPMENT.md "Hardening"); one process, about a minute
    hardening         = @{ Est = 70; Title = 'hardening: wrong-typed arguments to every API function'; Kind = 'result'; Mods = @('hardening'); Limit = 300 }
    # quick loot (M5d): the standalone mod `quickloot` against the engine's own interaction; phase `config` repeats the key path with another config.toml (items = false, key = PrimaryAction) copied into the mod
    quickloot         = @{ Est = 120; Title = 'quick loot (M5d: default config, then config.toml options)'; Kind = 'result'; Mods = @('quickloot', 'quickloot_driver'); Limit = 300
                           Phases = @(
                               @{ Name = 'default'; Param = @('phase=default'); Start = 'new'; Keep = $false; Limit = 300 }
                               @{ Name = 'config'; Param = @('phase=config'); Start = 'new'; Keep = $false; Limit = 200; CopyInto = @('quickloot', 'mods\quickloot_driver\fixtures\config') }
                           ) }
    # quick loot and the NPCs (M5e): phase `react` (default config: assess), phase `ignore` (config.toml with witness_mode = "ignore" copied into the mod)
    quickloot_react   = @{ Est = 130; Title = 'quick loot reactions (M5e: the engine theft assessment on the quick path, ignore mode)'; Kind = 'result'; Mods = @('quickloot', 'quickloot_driver'); Limit = 300
                           Phases = @(
                               @{ Name = 'react'; Param = @('phase=react'); Start = 'new'; Keep = $false; Limit = 400 }
                               @{ Name = 'ignore'; Param = @('phase=ignore'); Start = 'new'; Keep = $false; Limit = 200; CopyInto = @('quickloot', 'mods\quickloot_driver\fixtures\ignore') }
                           ) }
    # example: NPCs that ignore thefts while a game variable is set (risen.ai.perceive("theft")), animated and quick path
    ex_ignore_theft   = @{ Est = 100; Title = 'example: NPCs that ignore thefts while a variable is set'; Kind = 'result'; Mods = @('example_ignore_theft', 'quickloot'); Limit = 300 }
    # a one-process suite (M15, mods/testsuite): four scenarios in ONE game process, each one's mods loaded with risen.mods.load into a fresh new game and unloaded again; the combined result names the checks "<scenario>: <check>"
    suite             = @{ Est = 75; Title = 'one-process suite (M15: testdemo, ex_balance, ex_template_patch, ex_custom_npc in one game)'; Kind = 'result'; Mods = @('testsuite', 'testdemo', 'example_balance', 'example_template_patch', 'example_custom_npc'); Limit = 400
                           Disable = @('testdemo', 'example_balance', 'example_template_patch', 'example_custom_npc')
                           Phases = @(
                               @{ Name = ''; Param = @('suite=testdemo:testdemo,ex_balance:example_balance,ex_template_patch:example_template_patch,ex_custom_npc:example_custom_npc', 'scenario_seconds=200'); Start = 'new'; Keep = $false; Limit = 400 }
                           ) }
    # safe updates and removals (M15): "save" plays the old version of the mod upd_content (NPCs, a quest line, dialogue options, items, a recipe, a spell, saved data) and keeps a pristine copy of the savegame;
    # every other phase is a FRESH PROCESS that restores that copy and loads it with an updated version of the mod or without it: it must load, tick 600 frames, quick save and load again
    updsafe           = @{ Est = 420; Title = 'safe updates and removals (M15: same, empty, renamed, retire, no sidecar, mod gone)'; Kind = 'result'; Mods = @('upd_check', 'upd_content'); Limit = 300
                           Phases = @(
                               @{ Name = 'save'; Param = @('phase=save', 'variant=v1'); Start = 'new'; Keep = $false; Limit = 300; Unsafe = $true }
                               @{ Name = 'same'; Param = @('phase=same', 'variant=same'); Start = 'load'; Keep = $true; Limit = 200; Unsafe = $true }
                               @{ Name = 'empty'; Param = @('phase=empty', 'variant=empty'); Start = 'load'; Keep = $true; Limit = 200; Unsafe = $true }
                               @{ Name = 'renamed'; Param = @('phase=renamed', 'variant=renamed'); Start = 'load'; Keep = $true; Limit = 200; Unsafe = $true }
                               @{ Name = 'retire'; Param = @('phase=retire', 'variant=retire'); Start = 'load'; Keep = $true; Limit = 200; Unsafe = $true }
                               @{ Name = 'nosidecar'; Param = @('phase=nosidecar', 'variant=same'); Start = 'load'; Keep = $true; Limit = 200; Unsafe = $true }
                               @{ Name = 'gone'; Param = @('phase=gone'); Start = 'load'; Keep = $true; Limit = 200; Unsafe = $true; Mods = @('upd_check') }
                               @{ Name = 'gone_nosidecar'; Param = @('phase=gone_nosidecar'); Start = 'load'; Keep = $true; Limit = 200; Unsafe = $true; Mods = @('upd_check') }
                           ) }
    # hot reload (M15): "main" edits the files of hr_target on disk while the game runs (the sandbox is off: -Unsafe) and reloads it, failed reloads, watcher, unload/disable/enable, a save round trip;
    # "save_v1" leaves a save written by version 1.0.0, "load_v2" is a fresh process whose hr_target is version 1.1.0 (the fixture copied over it): the migration runs at load.before
    hotreload         = @{ Est = 150; Title = 'hot reload (M15: edit on disk, reload, nothing doubled, nothing left; migrate)'; Kind = 'result'; Mods = @('hotreload', 'hr_target', 'hr_dependent'); Limit = 300
                           Phases = @(
                               @{ Name = 'main'; Param = @('phase=main'); Start = 'new'; Keep = $false; Limit = 300; Unsafe = $true }
                               @{ Name = 'save_v1'; Param = @('phase=save_v1'); Start = 'new'; Keep = $false; Limit = 200; Unsafe = $true }
                               @{ Name = 'load_v2'; Param = @('phase=load_v2'); Start = 'load'; Keep = $true; Limit = 150; Unsafe = $true; CopyInto = @('hr_target', 'mods\hotreload\fixtures\v2\hr_target') }
                           ) }
    # the engine's GUI as building blocks (M16): pages, windows, clone, a Lua script as OnClick, window messages (ESC, mouse), a leaf page, ui.frame / ui.page, risen.debug.screenshot
    gui               = @{ Est = 60; Title = 'GUI building blocks (M16: pages, windows, clone, clicks, ESC, leaf page, screenshot)'; Kind = 'result'; Mods = @('gui_test'); Limit = 200
                           Phases = @(
                               @{ Name = ''; Param = @(); Start = 'new'; Keep = $false; Limit = 200; Unsafe = $true }
                           ) }
    # the in-game mod manager (M16): "main" drives the menu page through ESC and real clicks (reload, reorder with refusals, cascades, data prompt, quick save + load), "restart" is a fresh process that
    # keeps mods.toml (what the page chose is in force), "menu" runs at the main menu without a game
    modmanager        = @{ Est = 200; Title = 'in-game mod manager (M16: menu tab and page, enable/disable, reload, order, cascades, restart, no game)'; Kind = 'result'; Mods = @('modmanager', 'mm_driver', 'mm_base', 'mm_ext', 'mm_broken', 'mm_skipped', 'mm_data'); Limit = 300
                           Phases = @(
                               @{ Name = 'main'; Param = @('phase=main'); Start = 'new'; Keep = $false; Limit = 300; Unsafe = $true }
                               @{ Name = 'restart'; Param = @('phase=restart'); Start = 'new'; Keep = $false; Limit = 200; Unsafe = $true; KeepModsToml = $true }
                               @{ Name = 'menu'; Param = @('phase=menu'); Start = 'new'; Keep = $false; Limit = 200; Unsafe = $true; NoAutostart = $true }
                           ) }
    # the options page of the mod manager (risen.options) against the mod quickloot: toggle / drop-down / slider / pages / defaults / back, live effect, the store file, a reload; "restart" keeps spass\options
    options           = @{ Est = 150; Title = 'options menu (mod manager: the options of a mod, live changes, the stored choices, restart)'; Kind = 'result'; Mods = @('modmanager', 'quickloot', 'opt_driver'); Limit = 300
                           Phases = @(
                               @{ Name = 'main'; Param = @('phase=main'); Start = 'new'; Keep = $false; Limit = 300; Unsafe = $true }
                               @{ Name = 'restart'; Param = @('phase=restart'); Start = 'new'; Keep = $false; Limit = 200; Unsafe = $true; KeepModsToml = $true }
                               @{ Name = 'language'; Param = @('phase=language'); Start = 'new'; Keep = $false; Limit = 200; Unsafe = $true }
                           ) }
    # one process per phase: "save" crafts at the anvil (twice) and saves, "load" crafts again in a fresh process, "load_missing" runs without the mod's content, "load_again" has it again
    ex_recipe         = @{ Est = 250; Title = 'example: a new smithing recipe (save, load, missing, again)'; Kind = 'result'; Mods = @('example_recipe'); Limit = 300
                           Phases = @(
                               @{ Name = 'save'; Param = @('phase=save'); Start = 'new'; Keep = $false; Limit = 300 }
                               @{ Name = 'load'; Param = @('phase=load'); Start = 'load'; Keep = $true; Limit = 200 }
                               @{ Name = 'load_missing'; Param = @('phase=load_missing'); Start = 'load'; Keep = $true; Limit = 150 }
                               @{ Name = 'load_again'; Param = @('phase=load_again'); Start = 'load'; Keep = $true; Limit = 150 }
                           ) }
    # one game process per phase (-Param phase=...): "save" plays the whole scenario in a new game and leaves a quick save, the others load it in a fresh process
    newtemplates = @{ Est = 210; Title = 'new templates'; Kind = 'result'; Mods = @('newtemplates', 'newtemplates_driver'); Limit = 200
                      Phases = @(
                          @{ Name = 'save'; Param = @('phase=save'); Start = 'new'; Keep = $false; Limit = 200 }
                          @{ Name = 'load'; Param = @('phase=load'); Start = 'load'; Keep = $true; Limit = 120 }
                          @{ Name = 'load_missing'; Param = @('phase=load_missing'); Start = 'load'; Keep = $true; Limit = 120 }
                          @{ Name = 'load_again'; Param = @('phase=load_again'); Start = 'load'; Keep = $true; Limit = 120 }
                          @{ Name = 'load_late'; Param = @('phase=load_late'); Start = 'load'; Keep = $true; Limit = 120 }
                      ) }
    # loose meshes/textures/icons: "build" derives the files from shipped ones (risen.assets.write) and plays the scenario, "use" starts a fresh process in which
    # the generated directory is already mounted at startup (the files live in <Risen>\bin\spass\generated, which test-run.ps1 does not clean)
    newassets   = @{ Est = 90; Title = 'new meshes, textures, icons'; Kind = 'result'; Mods = @('newassets'); Limit = 120
                      Phases = @(
                          @{ Name = 'build'; Param = @('phase=build'); Start = 'new'; Keep = $false; Limit = 120 }
                          @{ Name = 'use'; Param = @('phase=use'); Start = 'new'; Keep = $false; Limit = 120 }
                      ) }
    # the generated Risen Extended mod (private, git-ignored): skipped when mods/risen_extended is absent. main plays the whole check and saves, load quick-loads that save in a fresh process
    extmod      = @{ Est = 150; Title = 'Risen Extended (generated mod)'; Kind = 'result'; Mods = @('risen_extended'); Limit = 300; Requires = 'mods/risen_extended/manifest.json'
                     Phases = @(
                         @{ Name = 'main'; Param = @('check=extmod', 'phase=main'); Start = 'new'; Keep = $false; Limit = 300 }
                         @{ Name = 'load'; Param = @('check=extmod', 'phase=load'); Start = 'load'; Keep = $true; Limit = 200 }
                     ) }
    # the real engine paths of the generated mod (conversations, teaching, melee styles, a fight): needs the generated mod too
    extplay     = @{ Est = 220; Title = 'Risen Extended: play checks (generated mod)'; Kind = 'result'; Mods = @('risen_extended'); Limit = 420; Requires = 'mods/risen_extended/manifest.json'
                     Phases = @(
                         @{ Name = 'play'; Param = @('check=extplay'); Start = 'new'; Keep = $false; Limit = 420 }
                     ) }
}

if ($List) {
    foreach ($name in $scenarios.Keys) {
        $s = $scenarios[$name]
        Write-Host ("{0,-12} {1,-24} {2,-10} mods: {3}" -f $name, $s.Title, $(if ($s.Kind -eq 'result') { 'risen.test' } else { 'log' }), ($s.Mods -join ', '))
    }
    return
}

$selected = if ($Scenario -contains 'all') { @($scenarios.Keys) } else { @($scenarios.Keys | Where-Object { $Scenario -contains $_ }) }
# scenarios that need a private, generated mod (Requires = a file below the repository): skipped when it is absent (not a failure unless asked for by name)
$absent = @($selected | Where-Object { $scenarios[$_].Requires -and -not (Test-Path (Join-Path (Split-Path -Parent $here) $scenarios[$_].Requires)) })
foreach ($n in $absent) { Write-Host "== ${n}: SKIPPED, the generated mod ($($scenarios[$n].Requires)) is absent (risen-pack2mod or python tools\extmod_gen.py makes it, docs/PACK2MOD.md)" -ForegroundColor Yellow }
if ($absent.Count -gt 0 -and $Scenario -notcontains 'all') { throw "scenario(s) $($absent -join ', ') need the generated mod (tools\extmod_gen.py)" }
$selected = @($selected | Where-Object { $absent -notcontains $_ })
$rows = @()
$runStart = Get-Date
$failures = 0

# ---- -Parallel N: a work queue over N game instances (test copies 1..N, see docs/DEVELOPMENT.md "Parallel instances") ----
# Every scenario runs as a child "smoke-test.ps1 -Scenario <name> -Instance <slot>" (its own process: own watchdog, own test copy,
# own log); the longest scenarios start first and a slot that finishes picks the next one. The children's output is printed as one
# block per scenario when it ends, the result rows come back through a JSON file.
if ($Parallel -gt 1 -and $selected.Count -gt 1) {
    $slots = [math]::Min($Parallel, $selected.Count)
    $queue = [System.Collections.Generic.List[string]]::new()
    foreach ($n in ($selected | Sort-Object { -[int]$scenarios[$_].Est })) { $queue.Add($n) }
    $running = @{}
    $tmp = Join-Path $env:TEMP "spass-smoke-$PID"
    New-Item -ItemType Directory -Force $tmp | Out-Null
    $pwsh = (Get-Process -Id $PID).Path
    $stop = $false
    Write-Host "running $($selected.Count) scenario(s) on $slots parallel game instance(s)"
    try {
        while ($queue.Count -gt 0 -or $running.Count -gt 0) {
            foreach ($slot in 1..$slots) {
                if ($running.ContainsKey($slot) -or $queue.Count -eq 0 -or $stop) { continue }
                $left = $TotalSeconds - ((Get-Date) - $runStart).TotalSeconds
                $name = $queue[0]; $queue.RemoveAt(0)
                if ($left -lt 90) {
                    Write-Host "== ${name}: SKIPPED, only $([int]$left) s of the $TotalSeconds s budget are left" -ForegroundColor Yellow
                    $rows += [pscustomobject]@{ Scenario = $name; Slot = '-'; Result = 'SKIPPED'; Checks = '-'; Seconds = 0; Note = 'time budget used up' }
                    $failures++
                    continue
                }
                $s = $scenarios[$name]
                $out = Join-Path $tmp "$name.out"; $json = Join-Path $tmp "$name.json"
                Remove-Item $out, $json -ErrorAction SilentlyContinue
                $childArgs = @('-NoProfile', '-File', $PSCommandPath, '-Scenario', $name, '-Instance', $slot, '-Seconds', $Seconds,
                               '-StallSeconds', $StallSeconds, '-TotalSeconds', [int]$left, '-ResultsFile', $json)
                $proc = Start-Process $pwsh -ArgumentList $childArgs -RedirectStandardOutput $out -WindowStyle Hidden -PassThru
                $cap = $(if ($s.Phases) { ($s.Phases | ForEach-Object { $_.Limit } | Measure-Object -Sum).Sum } else { $s.Limit }) + 180
                $running[$slot] = @{ Name = $name; Proc = $proc; Out = $out; Json = $json; T0 = Get-Date; Cap = $cap }
                Write-Host ("[{0:N0} s] slot {1}: started {2}" -f ((Get-Date) - $runStart).TotalSeconds, $slot, $name)
            }
            Start-Sleep -Milliseconds 500
            foreach ($slot in @($running.Keys)) {
                $r = $running[$slot]
                $age = ((Get-Date) - $r.T0).TotalSeconds
                $hung = -not $r.Proc.HasExited -and $age -gt $r.Cap
                if ($hung) { Stop-Process -Id $r.Proc.Id -Force -ErrorAction SilentlyContinue }
                if (-not $r.Proc.HasExited -and -not $hung) { continue }
                $r.Proc.WaitForExit(10000) | Out-Null
                Write-Host ("[{0:N0} s] slot {1}: {2} ended" -f ((Get-Date) - $runStart).TotalSeconds, $slot, $r.Name)
                $row = $null
                if (-not $hung -and (Test-Path $r.Json)) { $row = @(Get-Content $r.Json -Raw | ConvertFrom-Json)[0] }
                # a passed scenario is one line; a failed one prints everything
                if ($row -and $row.Result -eq 'PASS' -and -not $VerboseChildren) { Write-Host ("  [{0} {1}] PASS {2} checks, {3} s" -f $slot, $r.Name, $row.Checks, $row.Seconds) }
                elseif (Test-Path $r.Out) { Get-Content $r.Out | ForEach-Object { Write-Host "  [$slot $($r.Name)] $_" } }
                if ($null -eq $row) {
                    $row = [pscustomobject]@{ Scenario = $r.Name; Result = 'FAIL'; Checks = '-'; Seconds = [int]$age; Note = $(if ($hung) { "child did not end within $([int]$r.Cap) s, killed" } else { 'child ended without result rows' }) }
                }
                $row | Add-Member -NotePropertyName Slot -NotePropertyValue $slot -Force
                $rows += $row
                if ($row.Result -ne 'PASS') { $failures++; if ($FailFast) { $stop = $true; $queue.Clear() } }
                $running.Remove($slot)
            }
        }
    }
    finally {
        # never leave a child or a game behind (Ctrl-C, error): only the processes of the instances started here
        foreach ($slot in @($running.Keys)) { Stop-Process -Id $running[$slot].Proc.Id -Force -ErrorAction SilentlyContinue }
        foreach ($slot in 1..$slots) {
            $copy = if ($slot -ge 2) { "B:\DevDrive\risen-test$slot\bin\" } else { 'B:\DevDrive\risen-test\bin\' }
            Get-Process Risen -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path -like "$copy*" } | Stop-Process -Force -ErrorAction SilentlyContinue
        }
        Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
    }
    if ($stop) { Write-Host 'stopping after the first failure (-FailFast)' -ForegroundColor Yellow }
    $selected = @()
}
foreach ($name in $selected) {
    $s = $scenarios[$name]
    $elapsed = ((Get-Date) - $runStart).TotalSeconds
    $left = $TotalSeconds - $elapsed
    if ($left -lt 90) {
        Write-Host "== ${name}: SKIPPED, only $([int]$left) s of the $TotalSeconds s budget are left" -ForegroundColor Yellow
        $rows += [pscustomobject]@{ Scenario = $name; Result = 'SKIPPED'; Checks = '-'; Seconds = 0; Note = 'time budget used up' }
        $failures++
        continue
    }
    $limit = [int][math]::Min($s.Limit, $left)
    Write-Host "== $name ($($s.Title)), limit $limit s"
    $t0 = Get-Date
    $row = [pscustomobject]@{ Scenario = $name; Result = 'FAIL'; Checks = '-'; Seconds = 0; Note = '' }
    # every run of the scenario: one entry (a single run) or one per phase (several game processes)
    $phases = if ($s.Phases) { @($s.Phases) } else { @(@{ Name = ''; Param = @(); Start = 'new'; Keep = $false; Limit = $limit }) }
    $passedChecks = 0; $totalChecks = 0; $badPhases = @(); $notes = @()
    foreach ($ph in $phases) {
        $phLimit = [int][math]::Min($ph.Limit, [math]::Max($TotalSeconds - ((Get-Date) - $runStart).TotalSeconds, 60))
        $label = if ($ph.Name) { "$name/$($ph.Name)" } else { $name }
        if ($ph.Name) { Write-Host "-- phase $($ph.Name), limit $phLimit s" }
        $testArgs = @{ GameDir = $GameDir; Instance = $Instance; Mods = $(if ($ph.Mods) { $ph.Mods } else { $s.Mods }); Seconds = $phLimit; Filter = 'NOTHING'; StallSeconds = $StallSeconds }
        # every scenario ends itself (risen.test.finish writes the result file); the [log checks] ones additionally assert on the log below
        $testArgs.WaitForResult = $true; $testArgs.Scenario = $name
        if ($s.Kind -eq 'result') {
            if ($ph.Param -and $ph.Param.Count -gt 0) { $testArgs.Param = $ph.Param }
            if ($ph.Start -eq 'load') { $testArgs.Start = 'load' }
            if ($ph.Keep) { $testArgs.Keep = $true }
            if ($ph.VoiceLanguage) { $testArgs.VoiceLanguage = $ph.VoiceLanguage }
            if ($ph.Unsafe) { $testArgs.Unsafe = $true }
            if ($ph.KeepModsToml) { $testArgs.KeepModsToml = $true }
            if ($ph.NoAutostart) { $testArgs.NoAutostart = $true }
            if ($s.Disable) { $testArgs.Disable = $s.Disable }
            if ($ph.CopyInto) { $testArgs.CopyInto = "$($ph.CopyInto[0])=$(Join-Path (Split-Path -Parent $here) $ph.CopyInto[1])" }
        }
        $global:SpassRun = [pscustomobject]@{ Reason = 'error'; Result = $null }
        try { & (Join-Path $here 'test-run.ps1') @testArgs | Out-Null }
        catch { Write-Host "  test-run.ps1 failed: $_" -ForegroundColor Red }
        $run = $global:SpassRun

        if ($s.Kind -eq 'result') {
            $res = $run.Result
            if ($null -eq $res) {
                $note = switch ($run.Reason) { 'stall' { "stalled (no log line for $StallSeconds s), killed" } 'seconds' { "no result within $phLimit s, killed" } 'exited' { $(if ($run.Summary) { "the game exited without a result: $($run.Summary)" } else { 'the game exited without a result' }) } default { "no result ($($run.Reason))" } }
                Write-Host "  FAIL  $note" -ForegroundColor Red
                $badPhases += $label; $notes += "${label}: $note"
            } else {
                foreach ($c in $res.checks) {
                    if ($c.ok) { Write-Host ("  ok    " + $c.name) } else { Write-Host ("  FAIL  " + $c.name + ": " + $c.detail) -ForegroundColor Red }
                }
                foreach ($u in $res.unfinished) { Write-Host "  UNFINISHED $u" -ForegroundColor Red }
                $passedChecks += $res.counts.passed; $totalChecks += $res.counts.total
                if ($res.status -ne 'pass') { $badPhases += $label; $notes += "${label}: $($res.reason)" }
                Write-Host ("{0} of {1} checks passed" -f $res.counts.passed, $res.counts.total)
            }
        } else {
            if (-not (Test-Path $log)) { throw "no log at $log" }
            $text = Get-Content $log -Raw
            $failed = 0
            foreach ($c in $s.Checks) {
                if ($text -match $c[1]) { Write-Host ("  ok    " + $c[0]) }
                else { Write-Host ("  FAIL  " + $c[0]) -ForegroundColor Red; $failed++ }
            }
            $passedChecks = $s.Checks.Count - $failed; $totalChecks = $s.Checks.Count
            # the driver mod ends the run with risen.test.finish; the log regexes decide. A missing result only gets a note, a failed one fails the scenario.
            $resultBad = $null -ne $run.Result -and $run.Result.status -ne 'pass'
            if ($null -eq $run.Result) { $notes += "no result file (run ended by '$($run.Reason)', the driver did not finish)" }
            elseif ($resultBad) { $notes += "result: $($run.Result.reason)" }
            if ($failed -gt 0 -or $resultBad) {
                $badPhases += $label
                if ($failed -gt 0) { $notes += $(if ($run.Reason -eq 'stall') { "$failed check(s) failed; the run stalled (no log line for $StallSeconds s) and was killed" } else { "$failed check(s) failed, see $log" }) }
            }
            Write-Host ("{0} of {1} checks passed" -f ($s.Checks.Count - $failed), $s.Checks.Count)
        }
        if ($badPhases.Count -gt 0 -and $FailFast) { break }
    }
    $row.Seconds = [int]((Get-Date) - $t0).TotalSeconds
    $row.Checks = "$passedChecks/$totalChecks"
    $row.Note = ($notes -join '; ')
    if ($badPhases.Count -eq 0) { $row.Result = 'PASS' }
    if ($row.Result -ne 'PASS') { $failures++ }
    $rows += $row
    if ($failures -gt 0 -and $FailFast) { Write-Host 'stopping after the first failure (-FailFast)' -ForegroundColor Yellow; break }
}

Write-Host ''
if ($ResultsFile -ne '') { ConvertTo-Json -InputObject @($rows) -Depth 3 | Set-Content $ResultsFile -Encoding UTF8 }
if ($Parallel -gt 1) { $rows | Format-Table Scenario, Slot, Result, Checks, Seconds, Note -AutoSize | Out-String -Width 200 | Write-Host }
else { $rows | Format-Table Scenario, Result, Checks, Seconds, Note -AutoSize | Out-String -Width 200 | Write-Host }
Write-Host ("total {0:N0} s, {1} scenario(s), {2} not passed" -f ((Get-Date) - $runStart).TotalSeconds, $rows.Count, $failures)
if ($failures -gt 0) { exit 1 }
Write-Host 'all scenarios passed'
