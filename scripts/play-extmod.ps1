<#
.SYNOPSIS
    Plays a Risen Extended Mod milestone snapshot (docs/EXTENDED_MOD_ANALYSIS.md "Snapshots"): makes or refreshes a private windowed PLAY COPY of the
    game and starts it at the main menu.

.DESCRIPTION
    The play copy lives in B:\DevDrive\risen-play (never the real install). It is made from the main test copy B:\DevDrive\risen-test on first use:
    bin\ is copied (the proxy sentry.dll and the stock mountlist), data\ini is copied, data\common and data\compiled are junctions to the same targets as
    the test copy (read-only game data), the savegames and ConfigUser.xml live inside the copy ([debug] app_data), so nothing touches your real
    savegames or settings.
    Every start refreshes the copy from the snapshot: sentry.dll, the snapshot's mods (mirrored: a mod that is not in the snapshot is removed) and
    config.toml. Savegames of earlier snapshots are kept (use -Fresh to delete them); the generated mod creates its items from their names, so saves
    of an earlier snapshot load in a later one.
    The game starts windowed at 1600x900 (ConfigUser.xml of the copy; never resized), no test autostart, no test deadline: you land in the main menu.

.PARAMETER Snapshot  A snapshot folder (B:\DevDrive\extmod-milestones\NN-name). Default: the folder this script sits in, else the newest in B:\DevDrive\extmod-milestones.
.PARAMETER PlayDir   The play copy (default B:\DevDrive\risen-play). Must not be inside the real install.
.PARAMETER Fresh     Delete the play copy's savegames and settings first.
.PARAMETER NoStart   Only prepare the copy.
.PARAMETER WindowSize  Client size of the window (default 1600x900).
.PARAMETER Trace     Optional [debug] trace channels for the layer's log (bin\spass\spass.log).
#>
param(
    [string]$Snapshot = '',
    [string]$PlayDir = 'B:\DevDrive\risen-play',
    [string]$Template = 'B:\DevDrive\risen-test',
    [switch]$Fresh,
    [switch]$NoStart,
    [string]$WindowSize = '1600x900',
    [string]$Trace = ''
)
$ErrorActionPreference = 'Stop'
$root = 'B:\DevDrive\extmod-milestones'
if ($Snapshot -eq '') {
    if (Test-Path (Join-Path $PSScriptRoot 'snapshot.json')) { $Snapshot = $PSScriptRoot }
    else { $Snapshot = (Get-ChildItem $root -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'snapshot.json') } | Sort-Object Name | Select-Object -Last 1).FullName }
}
if (-not $Snapshot -or -not (Test-Path (Join-Path $Snapshot 'snapshot.json'))) { throw "no snapshot folder (looked for snapshot.json in '$Snapshot')" }
$PlayDir = $PlayDir.TrimEnd('\')
if ($PlayDir -like 'B:\SteamLibrary*') { throw 'refusing to touch the real install' }
if ($PlayDir -notlike 'B:\DevDrive\*') { throw "the play copy must be below B:\DevDrive, not $PlayDir" }
Write-Host "snapshot: $Snapshot"
$meta = Get-Content (Join-Path $Snapshot 'snapshot.json') -Raw | ConvertFrom-Json
Write-Host ("  {0}: {1}" -f $meta.name, $meta.title)

# the copy: made once from the test copy
$bin = Join-Path $PlayDir 'bin'
if (-not (Test-Path (Join-Path $bin 'Risen.exe'))) {
    if (-not (Test-Path (Join-Path $Template 'bin\Risen.exe'))) { throw "the template copy $Template does not exist (run scripts\test-run.ps1 once)" }
    Write-Host "creating the play copy $PlayDir from $Template"
    New-Item -ItemType Directory -Force (Join-Path $PlayDir 'data') | Out-Null
    & robocopy (Join-Path $Template 'bin') $bin /E /XD spass /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE)" }
    Copy-Item (Join-Path $Template 'data\ini') (Join-Path $PlayDir 'data\ini') -Recurse -Force
    foreach ($j in 'common', 'compiled') {
        $target = @((Get-Item (Join-Path $Template "data\$j") -Force).Target)[0]
        if (-not $target -or $target -notlike '*Risen\data\*') { throw "data\$j of the template copy is not a junction into a Risen install ($target)" }
        & cmd.exe /c mklink /J "`"$(Join-Path $PlayDir "data\$j")`"" "`"$target`"" | Out-Null
    }
    $cfgUser = Join-Path $Template 'localappdata\Local\Risen\ConfigUser.xml'
    if (Test-Path $cfgUser) {
        New-Item -ItemType Directory -Force (Join-Path $PlayDir 'localappdata\Local\Risen') | Out-Null
        Copy-Item $cfgUser (Join-Path $PlayDir 'localappdata\Local\Risen\ConfigUser.xml')
    }
    $LASTEXITCODE = 0
}
# no intro videos (data\extern would be a junction into the real install: give the copy its own empty logo.ini)
$externDir = Join-Path $PlayDir 'data\extern'
if ((Test-Path $externDir) -and ((Get-Item $externDir -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { & cmd.exe /c rmdir "`"$externDir`"" | Out-Null }
if (-not (Test-Path (Join-Path $externDir 'videos\logo.ini'))) {
    New-Item -ItemType Directory -Force (Join-Path $externDir 'videos') | Out-Null
    [IO.File]::WriteAllText((Join-Path $externDir 'videos\logo.ini'), '')
}
# a running game of this copy: ask the user instead of killing a game in progress
$running = Get-Process Risen -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path -like ($bin + '\*') }
if ($running) { throw "the play copy is running already (pid $($running.Id -join ',')): close the game first" }

if ($Fresh) {
    Remove-Item (Join-Path $bin 'spass\saves') -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item (Join-Path $PlayDir 'localappdata\Local\Risen\SaveGames') -Recurse -Force -ErrorAction SilentlyContinue
}

# refresh from the snapshot
Copy-Item (Join-Path $Snapshot 'bin\sentry.dll') (Join-Path $bin 'sentry.dll') -Force
$modsDir = Join-Path $bin 'spass\mods'
New-Item -ItemType Directory -Force $modsDir | Out-Null
$want = @(Get-ChildItem (Join-Path $Snapshot 'mods') -Directory | ForEach-Object { $_.Name })
foreach ($d in Get-ChildItem $modsDir -Directory) {
    if ($want -notcontains $d.Name) { Remove-Item $d.FullName -Recurse -Force }
}
foreach ($m in $want) {
    & robocopy (Join-Path $Snapshot "mods\$m") (Join-Path $modsDir $m) /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy of mod $m failed ($LASTEXITCODE)" }
}
$LASTEXITCODE = 0
Remove-Item (Join-Path $bin 'spass\test-result.json') -ErrorAction SilentlyContinue

# window size from ConfigUser.xml (never resized at run time: that misaligns the UI and the mouse)
if ($WindowSize -notmatch '^(\d+)x(\d+)$') { throw "-WindowSize must look like 1600x900, not '$WindowSize'" }
$winW = $Matches[1]; $winH = $Matches[2]
$cfgUserPath = Join-Path $PlayDir 'localappdata\Local\Risen\ConfigUser.xml'
if (Test-Path $cfgUserPath) {
    $xml = [System.IO.File]::ReadAllText($cfgUserPath)
    $new = [regex]::Replace($xml, '(?s)(<Window\b[^>]*?)\bBottom="\d+"', "`$1Bottom=`"$winH`"")
    $new = [regex]::Replace($new, '(?s)(<Window\b[^>]*?)\bRight="\d+"', "`$1Right=`"$winW`"")
    if ($new -ne $xml) { [System.IO.File]::WriteAllText($cfgUserPath, $new) }
}
$appData = (Join-Path $PlayDir 'localappdata').Replace('\', '/')
$cfg = @('log_level = "info"', '[debug]', "app_data = `"$appData`"", 'instance = "play"', "trace = `"$Trace`"", 'crash_dump = "mini"')
Set-Content (Join-Path $bin 'spass\config.toml') $cfg
Write-Host "play copy refreshed: $PlayDir  (mods: $($want -join ', '))"
if ($NoStart) { return }

# Risen calls SteamAPI_RestartAppIfNecessary; SteamAppId prevents a relaunch from the real install
$env:SteamAppId = '40300'
$env:SteamGameId = '40300'
$log = Join-Path $bin 'spass\spass.log'
Remove-Item $log -ErrorAction SilentlyContinue
Write-Host 'starting the game (windowed). Close it from its own menu when you are done.'
$p = Start-Process -FilePath (Join-Path $bin 'Risen.exe') -ArgumentList '-nosplash' -WorkingDirectory $bin -PassThru
$p.WaitForExit()
Write-Host ("the game ended (exit code 0x{0:X8})" -f [uint32]$p.ExitCode)
if (Test-Path $log) {
    $bad = @(Select-String -Path $log -Pattern '\[(error|fatal)\]' | Select-Object -First 10)
    if ($bad.Count -gt 0) { Write-Host 'log lines with errors:'; $bad | ForEach-Object { Write-Host "  $($_.Line)" } }
    Write-Host "log: $log"
}
