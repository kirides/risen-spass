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
    [ValidateSet('persistence', 'fetchquest', 'commands', 'branch', 'templates', 'balance', 'npcs', 'newtemplates', 'newassets', 'events', 'spells', 'ex_spell', 'recipes', 'ex_recipe', 'ex_template_patch', 'ex_balance', 'ex_custom_npc', 'ex_fetch_turnin', 'ex_fetch_pickup', 'ex_kill_quest', 'ex_quest_rewards', 'ex_quest_tracker', 'ex_material', 'ex_voice', 'quickloot', 'persistnpc', 'spawnwhen', 'ex_spawn_when', 'all')][string[]]$Scenario = @('all'),
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
    # quick loot (M5d): the standalone mod `quickloot` against the engine's own interaction; phase `config` repeats the key path with another config.toml (items = false, key = PrimaryAction) copied into the mod
    quickloot         = @{ Est = 120; Title = 'quick loot (M5d: default config, then config.toml options)'; Kind = 'result'; Mods = @('quickloot', 'quickloot_driver'); Limit = 300
                           Phases = @(
                               @{ Name = 'default'; Param = @('phase=default'); Start = 'new'; Keep = $false; Limit = 300 }
                               @{ Name = 'config'; Param = @('phase=config'); Start = 'new'; Keep = $false; Limit = 200; CopyInto = @('quickloot', 'mods\quickloot_driver\fixtures\config') }
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
}

if ($List) {
    foreach ($name in $scenarios.Keys) {
        $s = $scenarios[$name]
        Write-Host ("{0,-12} {1,-24} {2,-10} mods: {3}" -f $name, $s.Title, $(if ($s.Kind -eq 'result') { 'risen.test' } else { 'log' }), ($s.Mods -join ', '))
    }
    return
}

$selected = if ($Scenario -contains 'all') { @($scenarios.Keys) } else { @($scenarios.Keys | Where-Object { $Scenario -contains $_ }) }
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
        $testArgs = @{ GameDir = $GameDir; Instance = $Instance; Mods = $s.Mods; Seconds = $phLimit; Filter = 'NOTHING'; StallSeconds = $StallSeconds }
        # every scenario ends itself (risen.test.finish writes the result file); the [log checks] ones additionally assert on the log below
        $testArgs.WaitForResult = $true; $testArgs.Scenario = $name
        if ($s.Kind -eq 'result') {
            if ($ph.Param -and $ph.Param.Count -gt 0) { $testArgs.Param = $ph.Param }
            if ($ph.Start -eq 'load') { $testArgs.Start = 'load' }
            if ($ph.Keep) { $testArgs.Keep = $true }
            if ($ph.VoiceLanguage) { $testArgs.VoiceLanguage = $ph.VoiceLanguage }
            if ($ph.CopyInto) { $testArgs.CopyInto = "$($ph.CopyInto[0])=$(Join-Path (Split-Path -Parent $here) $ph.CopyInto[1])" }
        }
        $global:SpassRun = [pscustomobject]@{ Reason = 'error'; Result = $null }
        try { & (Join-Path $here 'test-run.ps1') @testArgs | Out-Null }
        catch { Write-Host "  test-run.ps1 failed: $_" -ForegroundColor Red }
        $run = $global:SpassRun

        if ($s.Kind -eq 'result') {
            $res = $run.Result
            if ($null -eq $res) {
                $note = switch ($run.Reason) { 'stall' { "stalled (no log line for $StallSeconds s), killed" } 'seconds' { "no result within $phLimit s, killed" } 'exited' { 'the game exited without a result' } default { "no result ($($run.Reason))" } }
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
