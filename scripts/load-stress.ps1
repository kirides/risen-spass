<#
.SYNOPSIS
    Repeats a fresh-process savegame load many times, serially or with several game instances (and a CPU burner) at once, and counts
    the crashes (M17: the rare crash shortly after session.start of a load, docs/DEVELOPMENT.md "Crashes").

.DESCRIPTION
    Every worker owns one test copy (instance k = B:\DevDrive\risen-test[k], see "Parallel instances"). It first plays the preset's save phase
    (a new game that leaves a quick save; skipped with -SkipPrepare when the copy still has the savegame of an earlier run), then runs the
    preset's load phase over and over: a fresh Risen.exe whose autostart mod quick-loads the save, the scenario's checks, the process is
    killed when the result is written. A phase that ends without a result is a failure; one that exited with an NTSTATUS exit code, or
    whose crash handler wrote a banner, is a CRASH. Everything about a bad iteration (spass.log, crash.log, the minidump) is copied to
    <OutDir>\<stamp>\bad-<worker>-<n>\.

    Presets (-Preset): loop (lean, then 40 loads in the same process), showcase (balance_example), newtemplates (newtemplates + newtemplates_driver, the load_late phase), lean (balance_example,
    but the scenario ends 6 s after world.ready instead of running its checks: more loads per hour). -Mods / -SaveParam / -LoadParam override a preset.

.PARAMETER Iterations  Loads in total (split over the workers).
.PARAMETER Parallel    Number of game instances at the same time (1..4).
.PARAMETER Burn        CPU burner processes (each spins one core at below-normal priority) while the loop runs.
.PARAMETER SkipPrepare Do not play the save phase (the savegame of an earlier run is still in the copies).
.PARAMETER MaxMinutes  Time box: workers stop starting iterations after this long.
#>
param(
    [ValidateSet('showcase', 'newtemplates', 'lean', 'loop')][string]$Preset = 'showcase',
    [int]$Iterations = 20,
    [ValidateRange(1, 4)][int]$Parallel = 1,
    [int]$Burn = 0,
    [switch]$SkipPrepare,
    [int]$MaxMinutes = 0,
    [string]$OutDir = 'B:\DevDrive\risen-stress',
    [string[]]$Mods = @(),
    [string]$Scenario = '',
    [string[]]$SaveParam = @(),
    [string[]]$LoadParam = @(),
    [int]$FirstInstance = 1,
    # internal: run as the worker of one instance
    [int]$Worker = 0,
    [string]$Stamp = ''
)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot

# a worker gets its lists as comma separated text
$Mods = @($Mods | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$SaveParam = @($SaveParam | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$LoadParam = @($LoadParam | ForEach-Object { $_ -split ',' } | Where-Object { $_ })

# preset -> mods, scenario name, params of the save phase and of the load phase
$presets = @{
    showcase     = @{ Mods = @('balance_example'); Scenario = 'showcase'; Save = @('phase=play'); Load = @('phase=load') }
    newtemplates = @{ Mods = @('newtemplates', 'newtemplates_driver'); Scenario = 'newtemplates'; Save = @('phase=save'); Load = @('phase=load_late') }
    lean         = @{ Mods = @('balance_example'); Scenario = 'showcase'; Save = @('phase=play'); Load = @('phase=load', 'lean=1') }
    # one fresh process, then 40 loads of the same savegame inside it (each ~5 s): many more session.start / world teardown cycles per hour
    loop         = @{ Mods = @('balance_example'); Scenario = 'showcase'; Save = @('phase=play'); Load = @('phase=load', 'lean=1', 'loops=40') }
}
$p = $presets[$Preset]
if ($Mods.Count -eq 0) { $Mods = $p.Mods }
if ($Scenario -eq '') { $Scenario = $p.Scenario }
if ($SaveParam.Count -eq 0) { $SaveParam = $p.Save }
if ($LoadParam.Count -eq 0) { $LoadParam = $p.Load }

function Get-LogSeconds([string]$line) {
    if ($line -match '^\[(\d\d):(\d\d):(\d\d)\.(\d+)\]') { return [int]$Matches[1] * 3600 + [int]$Matches[2] * 60 + [int]$Matches[3] + [double]("0." + $Matches[4]) }
    return $null
}

# ---------------------------------------------------------------------------------------------------------------------------------
# worker: one instance, its share of the iterations; one JSON line per iteration into <run>\results-<worker>.jsonl
# ---------------------------------------------------------------------------------------------------------------------------------
if ($Worker -gt 0) {
    $run = Join-Path $OutDir $Stamp
    $copy = if ($Worker -ge 2) { "B:\DevDrive\risen-test$Worker" } else { 'B:\DevDrive\risen-test' }
    $logFile = Join-Path $copy 'bin\spass\spass.log'
    $crashLog = Join-Path $copy 'bin\spass\crash.log'
    $deadline = if ($MaxMinutes -gt 0) { (Get-Date).AddMinutes($MaxMinutes) } else { [datetime]::MaxValue }
    $out = Join-Path $run "results-$Worker.jsonl"

    $common = @{ Instance = $Worker; Mods = $Mods; WaitForResult = $true; Scenario = $Scenario; Seconds = 200; StallSeconds = 75 }
    if (-not $SkipPrepare) {
        Write-Host "[worker $Worker] save phase ($($SaveParam -join ' '))"
        $a = $common.Clone(); $a.Param = $SaveParam; $a.Seconds = 420
        & (Join-Path $here 'test-run.ps1') @a *> (Join-Path $run "prepare-$Worker.txt")
        $r = $global:SpassRun
        if ($null -eq $r.Result -or $r.Result.status -ne 'pass') {
            Write-Host "[worker $Worker] the save phase did not pass ($($r.Verdict), $($r.Summary)); see prepare-$Worker.txt" -ForegroundColor Red
            exit 3
        }
    }
    $n = 0
    while ($n -lt $Iterations -and (Get-Date) -lt $deadline) {
        $n++
        $a = $common.Clone(); $a.Param = $LoadParam; $a.Start = 'load'; $a.Keep = $true
        $loops = 0; foreach ($lp in $LoadParam) { if ($lp -match '^loops=(\d+)$') { $loops = [int]$Matches[1] } }
        if ($loops -gt 0) { $a.Seconds = 200 + 14 * $loops; $a.StallSeconds = 90 }
        $t0 = Get-Date
        $text = & (Join-Path $here 'test-run.ps1') @a *>&1 | Out-String
        $r = $global:SpassRun
        $log = if (Test-Path $logFile) { @(Get-Content $logFile -ErrorAction SilentlyContinue) } else { @() }
        $sessionLine = $log | Where-Object { $_ -match 'session: start' } | Select-Object -First 1
        $lastLine = $log | Select-Object -Last 1
        $sessionAt = if ($sessionLine) { Get-LogSeconds $sessionLine } else { $null }
        $lastAt = if ($lastLine) { Get-LogSeconds $lastLine } else { $null }
        $afterSession = if ($null -ne $sessionAt -and $null -ne $lastAt) { [math]::Round(($lastAt - $sessionAt + 86400) % 86400, 2) } else { $null }
        $status = if ($r.Crashed) { 'CRASH' } elseif ($null -ne $r.Result -and $r.Result.status -eq 'pass') { 'pass' } elseif ($null -ne $r.Result) { 'FAIL' } elseif ($r.Reason -eq 'exited') { 'EXITED' } else { 'NORESULT' }
        $row = [ordered]@{
            worker = $Worker; n = $n; status = $status; reason = $r.Reason; exit = $(if ($null -ne $r.ExitCode) { '0x{0:X8}' -f $r.ExitCode } else { '' })
            seconds = [math]::Round(((Get-Date) - $t0).TotalSeconds, 1); session_started = [bool]$sessionLine; seconds_after_session_start = $afterSession
            summary = $r.Summary; checks_failed = $(if ($r.Result) { $r.Result.counts.failed } else { $null })
            guarded_faults = @($log | Where-Object { $_ -match 'guarded call #\d+ faulted' }).Count
        }
        Add-Content $out (($row | ConvertTo-Json -Compress))
        Write-Host ("[worker {0}] #{1}: {2} {3} {4}" -f $Worker, $n, $status, $row.exit, $(if ($status -ne 'pass') { $r.Summary } else { '' }))
        if ($status -ne 'pass') {
            $bad = Join-Path $run ("bad-{0}-{1}" -f $Worker, $n)
            New-Item -ItemType Directory -Force $bad | Out-Null
            Copy-Item $logFile (Join-Path $bad 'spass.log') -ErrorAction SilentlyContinue
            if (Test-Path $crashLog) { Copy-Item $crashLog (Join-Path $bad 'crash.log') }
            foreach ($d in $r.Dumps) { Copy-Item $d $bad }
            Set-Content (Join-Path $bad 'test-run.txt') $text
        }
    }
    exit 0
}

# ---------------------------------------------------------------------------------------------------------------------------------
# main: start the workers (and the burners), wait, summarise
# ---------------------------------------------------------------------------------------------------------------------------------
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$run = Join-Path $OutDir $stamp
New-Item -ItemType Directory -Force $run | Out-Null
Get-Process Risen -ErrorAction SilentlyContinue | Where-Object { $_.Path -like 'B:\DevDrive\risen-test*' } | ForEach-Object { Write-Host "warning: a Risen.exe of a test copy is already running (pid $($_.Id))" -ForegroundColor Yellow }
$perWorker = [int][math]::Ceiling($Iterations / $Parallel)
Write-Host "load-stress: preset $Preset, $Iterations load(s) = $Parallel worker(s) x $perWorker, burn $Burn, output $run"
$pwsh = (Get-Process -Id $PID).Path
$burners = @()
$workers = @()
try {
    for ($i = 0; $i -lt $Burn; $i++) {
        $b = Start-Process $pwsh -ArgumentList '-NoProfile', '-Command', '$x = 0; while ($true) { $x++ }' -WindowStyle Hidden -PassThru
        try { $b.PriorityClass = 'BelowNormal' } catch { }
        $burners += $b
    }
    $started = Get-Date
    for ($k = 0; $k -lt $Parallel; $k++) {
        $inst = $FirstInstance + $k
        $wargs = @('-NoProfile', '-File', $PSCommandPath, '-Worker', $inst, '-Stamp', $stamp, '-Preset', $Preset, '-Iterations', $perWorker, '-OutDir', $OutDir,
                   '-Scenario', $Scenario, '-Mods', ($Mods -join ','), '-SaveParam', ($SaveParam -join ','), '-LoadParam', ($LoadParam -join ','))
        if ($SkipPrepare) { $wargs += '-SkipPrepare' }
        if ($MaxMinutes -gt 0) { $wargs += @('-MaxMinutes', $MaxMinutes) }
        $workers += Start-Process $pwsh -ArgumentList $wargs -RedirectStandardOutput (Join-Path $run "worker-$inst.txt") -RedirectStandardError (Join-Path $run "worker-$inst.err") -WindowStyle Hidden -PassThru
    }
    while (@($workers | Where-Object { -not $_.HasExited }).Count -gt 0) {
        Start-Sleep -Seconds 30
        $rows = @(Get-ChildItem $run -Filter 'results-*.jsonl' -ErrorAction SilentlyContinue | ForEach-Object { Get-Content $_.FullName | ForEach-Object { $_ | ConvertFrom-Json } })
        Write-Host ("[{0,5:N0} s] {1} load(s) done: {2}" -f ((Get-Date) - $started).TotalSeconds, $rows.Count, (($rows | Group-Object status | ForEach-Object { "$($_.Name)=$($_.Count)" }) -join ' '))
        Write-Host ""
    }
}
finally {
    foreach ($w in $workers) { if (-not $w.HasExited) { Stop-Process -Id $w.Id -Force -ErrorAction SilentlyContinue } }
    foreach ($b in $burners) { Stop-Process -Id $b.Id -Force -ErrorAction SilentlyContinue }
    foreach ($k in 0..($Parallel - 1)) {
        $inst = $FirstInstance + $k
        $bin = if ($inst -ge 2) { "B:\DevDrive\risen-test$inst\bin\" } else { 'B:\DevDrive\risen-test\bin\' }
        Get-Process Risen -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path -like "$bin*" } | Stop-Process -Force -ErrorAction SilentlyContinue
    }
}
Write-Host ''
$rows = @(Get-ChildItem $run -Filter 'results-*.jsonl' -ErrorAction SilentlyContinue | ForEach-Object { Get-Content $_.FullName | ForEach-Object { $_ | ConvertFrom-Json } })
$rows | Format-Table worker, n, status, exit, seconds, seconds_after_session_start, guarded_faults, summary -AutoSize | Out-String -Width 250 | Write-Host
$by = $rows | Group-Object status
Write-Host ("load-stress {0}: {1} load(s) in {2:N0} s: {3}" -f $Preset, $rows.Count, ((Get-Date) - $started).TotalSeconds, (($by | ForEach-Object { "$($_.Name)=$($_.Count)" }) -join ', '))
Write-Host "guarded() faults logged in these runs: $(($rows | Measure-Object guarded_faults -Sum).Sum)"
Write-Host "details of the bad ones: $run\bad-*"
$bad = @($rows | Where-Object { $_.status -ne 'pass' }).Count
if ($bad -gt 0) { exit 1 }
