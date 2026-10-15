<#
.SYNOPSIS
    End-to-end smoke test: launches the isolated test copy, plays two scenarios and asserts on the lines the mod layer writes
    to spass.log:
      persistence - new game -> runtime content -> quick save -> change -> quick load -> new game (mods/persistdemo)
      fetchquest  - the example quest played through by mods/fetchquest_driver

.DESCRIPTION
    Needs a built sentry.dll and the test copy (see CLAUDE.md); takes ~3 minutes per scenario. Exit code 0 = every assertion held.

.PARAMETER GameDir   Test copy of Risen (never the real install).
.PARAMETER Seconds   How long the game runs per scenario (fetchquest gets 60 s more).
.PARAMETER Scenario  persistence, fetchquest or all.
#>
param(
    [string]$GameDir = 'B:\DevDrive\risen-test',
    [int]$Seconds = 200,
    # persistence | fetchquest | commands | branch | all
    [ValidateSet('persistence', 'fetchquest', 'commands', 'branch', 'all')][string]$Scenario = 'all'
)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$log = Join-Path $GameDir 'bin\spass\spass.log'

$totalFailed = 0
function Run-Checks($title, $text, $checks) {
    Write-Host "== $title"
    $failed = 0
    foreach ($c in $checks) {
        if ($text -match $c[1]) { Write-Host ("  ok    " + $c[0]) }
        else { Write-Host ("  FAIL  " + $c[0]) -ForegroundColor Red; $failed++ }
    }
    Write-Host ("{0} of {1} checks passed" -f ($checks.Count - $failed), $checks.Count)
    return $failed
}

# Each check: description, regex that must match the log.
$checks = @(
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

if ($Scenario -in 'persistence', 'all') {
    & (Join-Path $here 'test-run.ps1') -GameDir $GameDir -Mods persistdemo, eventlog -Seconds $Seconds -Filter 'NOTHING' | Out-Null
    if (-not (Test-Path $log)) { throw "no log at $log" }
    $totalFailed += Run-Checks 'persistence and events' (Get-Content $log -Raw) $checks
}

if ($Scenario -in 'fetchquest', 'all') {
    & (Join-Path $here 'test-run.ps1') -GameDir $GameDir -Mods fetchquest, fetchquest_driver -Seconds ($Seconds + 60) -Filter 'NOTHING' | Out-Null
    $fetch = @(
        @('Marta was spawned',                      'driver: Marta exists, quest status = gEQuestStatus_Open'),
        @('the work option started the quest',      'driver: quest running, log entries = 1'),
        @('Marta is spawned again after a load',    'driver: after load: Marta respawned=true same_spot=true'),
        @('quest and dialogue options survive',     'driver: after load: .*quest=gEQuestStatus_Running infos=true/true'),
        @('hand-in with too few apples is refused', 'driver: not enough apples: status=gEQuestStatus_Running .*apples=1'),
        @('hand-in with 3 apples completes it',     'driver: DONE status=gEQuestStatus_Success apples=0 machetes=1'),
        @('no Lua errors',                          '^(?!(?s).*\[error\])')
    )
    $totalFailed += Run-Checks 'fetch quest example' (Get-Content $log -Raw) $fetch
}

if ($Scenario -in 'commands', 'all') {
    & (Join-Path $here 'test-run.ps1') -GameDir $GameDir -Mods dialogcmds -Seconds ($Seconds - 20) -Filter 'NOTHING' | Out-Null
    $cmds = @(
        @('invalid command lines are refused',     'cmds: invalid specs refused: true'),
        @('unknown command names the problem',     'unknown command ''NoSuchCommand'''),
        @('unknown parameter names the problem',   'command GiveXP has no parameter ''Count'''),
        @('command parameters read back',          'cmds: command 2 Amount=2 Item=It_Apple'),
        @('Give delivered the items',              'cmds: after: apples=2 event=true'),
        @('no Lua errors',                         '^(?!(?s).*\[error\])')
    )
    $totalFailed += Run-Checks 'dialogue commands' (Get-Content $log -Raw) $cmds
}

if ($Scenario -in 'branch', 'all') {
    & (Join-Path $here 'test-run.ps1') -GameDir $GameDir -Mods dialogbranch -Seconds ($Seconds + 60) -Filter 'NOTHING' | Out-Null
    $branch = @(
        @('only the parent is offered at the top',  '(?m)branch: top-level options: PANKRATZX2_00004,SPASS_Br_Town?$'),
        @('the children replace the menu',          'branch: the children are offered: SPASS_Br_Back,SPASS_Br_Harbor,SPASS_Br_Monastery'),
        @('a child answers and the menu stays',     'branch: after the child: SPASS_Br_Back,SPASS_Br_Harbor,SPASS_Br_Monastery running=true'),
        @('Back returns to the top level',          'branch: back at the top level: PANKRATZX2_00004,SPASS_Br_Town'),
        @('no Lua errors',                          '^(?!(?s).*\[error\])')
    )
    $totalFailed += Run-Checks 'branching dialogue' (Get-Content $log -Raw) $branch
}

if ($totalFailed -gt 0) { Write-Host "$totalFailed check(s) failed, see $log"; exit 1 }
Write-Host 'all scenarios passed'
