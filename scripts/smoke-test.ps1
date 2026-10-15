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

.DESCRIPTION
    Needs a built sentry.dll and the test copy (see CLAUDE.md). The [risen.test] scenarios end as soon as they are done
    (the mod layer writes spass\test-result.json, see docs/DEVELOPMENT.md "Testing and tracing"): about a minute each. The
    [log checks] scenarios still run for a fixed time and assert on regexes over spass.log (~3 minutes each).
    Every scenario runs under the watchdog of test-run.ps1 (no new log line for -StallSeconds, or over its time limit: the
    game is killed and the last log lines and "where it stopped" are printed). Exit code 0 = every selected scenario passed.

.PARAMETER GameDir   Test copy of Risen (never the real install).
.PARAMETER Seconds   Time limit of the [log checks] scenarios (fetchquest and branch get 60 s more, commands 20 s less).
.PARAMETER Scenario  One or more scenario names (comma separated), or all.
.PARAMETER List      Print the scenarios and exit.
.PARAMETER FailFast  Stop after the first failing scenario.
.PARAMETER TotalSeconds  Time budget of the whole run: a scenario that cannot start with at least 90 s left is skipped (and counts as a failure).
.PARAMETER StallSeconds  Watchdog: kill the game after this long without a new log line.
#>
param(
    [string]$GameDir = 'B:\DevDrive\risen-test',
    [int]$Seconds = 200,
    [ValidateSet('persistence', 'fetchquest', 'commands', 'branch', 'templates', 'balance', 'npcs', 'all')][string[]]$Scenario = @('all'),
    [switch]$List,
    [switch]$FailFast,
    [int]$TotalSeconds = 3600,
    [int]$StallSeconds = 90
)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
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
    persistence = @{ Title = 'persistence and events'; Kind = 'log'; Mods = @('persistdemo', 'eventlog'); Limit = $Seconds; Checks = $persistence }
    fetchquest  = @{ Title = 'fetch quest example'; Kind = 'log'; Mods = @('fetchquest', 'fetchquest_driver'); Limit = $Seconds + 60; Checks = $fetch }
    commands    = @{ Title = 'dialogue commands'; Kind = 'log'; Mods = @('dialogcmds'); Limit = $Seconds - 20; Checks = $cmds }
    branch      = @{ Title = 'branching dialogue'; Kind = 'log'; Mods = @('dialogbranch'); Limit = $Seconds + 60; Checks = $branch }
    templates   = @{ Title = 'template editing'; Kind = 'result'; Mods = @('templatepatch', 'templatepatch_driver'); Limit = 400 }
    balance     = @{ Title = 'item balancing'; Kind = 'result'; Mods = @('balance_items', 'balance_driver'); Limit = 400 }
    npcs        = @{ Title = 'NPC balancing'; Kind = 'result'; Mods = @('balance_npcs', 'balance_npcs_driver'); Limit = 500 }
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
    $testArgs = @{ GameDir = $GameDir; Mods = $s.Mods; Seconds = $limit; Filter = 'NOTHING'; StallSeconds = $StallSeconds }
    if ($s.Kind -eq 'result') { $testArgs.WaitForResult = $true; $testArgs.Scenario = $name }
    $global:SpassRun = [pscustomobject]@{ Reason = 'error'; Result = $null }
    try { & (Join-Path $here 'test-run.ps1') @testArgs | Out-Null }
    catch { Write-Host "  test-run.ps1 failed: $_" -ForegroundColor Red }
    $run = $global:SpassRun
    $took = ((Get-Date) - $t0).TotalSeconds
    $row = [pscustomobject]@{ Scenario = $name; Result = 'FAIL'; Checks = '-'; Seconds = [int]$took; Note = '' }

    if ($s.Kind -eq 'result') {
        $res = $run.Result
        if ($null -eq $res) {
            $row.Note = switch ($run.Reason) { 'stall' { "stalled (no log line for $StallSeconds s), killed" } 'seconds' { "no result within $limit s, killed" } 'exited' { 'the game exited without a result' } default { "no result ($($run.Reason))" } }
            Write-Host "  FAIL  $($row.Note)" -ForegroundColor Red
        } else {
            foreach ($c in $res.checks) {
                if ($c.ok) { Write-Host ("  ok    " + $c.name) } else { Write-Host ("  FAIL  " + $c.name + ": " + $c.detail) -ForegroundColor Red }
            }
            foreach ($u in $res.unfinished) { Write-Host "  UNFINISHED $u" -ForegroundColor Red }
            $row.Checks = "$($res.counts.passed)/$($res.counts.total)"
            if ($res.status -eq 'pass') { $row.Result = 'PASS' } else { $row.Note = $res.reason }
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
        $row.Checks = "$($s.Checks.Count - $failed)/$($s.Checks.Count)"
        # these scenarios end quietly: the stall watchdog only cuts the fixed wait short, the checks decide
        if ($failed -eq 0) { $row.Result = 'PASS'; if ($run.Reason -eq 'stall') { $row.Note = 'quiet after the last step: ended by the watchdog' } }
        elseif ($run.Reason -eq 'stall') { $row.Note = "$failed check(s) failed; the run stalled (no log line for $StallSeconds s) and was killed" }
        else { $row.Note = "$failed check(s) failed, see $log" }
        Write-Host ("{0} of {1} checks passed" -f ($s.Checks.Count - $failed), $s.Checks.Count)
    }
    if ($row.Result -ne 'PASS') { $failures++ }
    $rows += $row
    if ($failures -gt 0 -and $FailFast) { Write-Host 'stopping after the first failure (-FailFast)' -ForegroundColor Yellow; break }
}

Write-Host ''
$rows | Format-Table Scenario, Result, Checks, Seconds, Note -AutoSize | Out-String -Width 200 | Write-Host
Write-Host ("total {0:N0} s, {1} scenario(s), {2} not passed" -f ((Get-Date) - $runStart).TotalSeconds, $rows.Count, $failures)
if ($failures -gt 0) { exit 1 }
Write-Host 'all scenarios passed'
