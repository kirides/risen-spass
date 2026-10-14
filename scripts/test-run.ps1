<#
.SYNOPSIS
    Development loop: deploys the built proxy and chosen example mods into the isolated test copy, launches it
    windowed, and prints the lines of spass.log that match a filter.

.PARAMETER GameDir  Test copy of Risen (default B:\DevDrive\risen-test). Never point this at the real install.
.PARAMETER Mods     Example mods from <repo>\mods to enable (copied); every other mod in the test copy is disabled
                    by renaming it to <name>.off. "autostart" (starts a new game on the first tick) is kept.
.PARAMETER Seconds  How long to let the game run.
.PARAMETER Filter   Regex of log lines to print (default: everything of interest).
.PARAMETER Keep     Do not delete spass.log and saves before the run (to test loading an earlier save).
#>
param(
    [string]$GameDir = 'B:\DevDrive\risen-test',
    [string[]]$Mods = @('persistdemo'),
    [int]$Seconds = 300,
    [string]$Filter = 'lua:|save:|load:|session:|world:|\[error\]|\[warn\]',
    [switch]$Keep,
    [switch]$NoAutostart,
    # What the "autostart" helper mod does on the first tick: start a new game, or quick-load the last quick save.
    [ValidateSet('new', 'load')][string]$Start = 'new'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $GameDir 'bin'
$modsDir = Join-Path $bin 'spass\mods'
if (-not (Test-Path (Join-Path $bin 'Risen.exe'))) { throw "no Risen.exe in $bin" }
if ($GameDir -like 'B:\SteamLibrary*') { throw 'refusing to touch the real install' }

Get-Process Risen -ErrorAction SilentlyContinue | Stop-Process -Force
Copy-Item (Join-Path $repo 'build\RelWithDebInfo\sentry.dll') (Join-Path $bin 'sentry.dll') -Force

New-Item -ItemType Directory -Force $modsDir | Out-Null
foreach ($d in Get-ChildItem $modsDir -Directory) {
    if ($d.Name -eq 'autostart' -or $d.Name -like '*.off') { continue }
    Rename-Item $d.FullName ($d.Name + '.off')
}
foreach ($m in $Mods) {
    $dst = Join-Path $modsDir $m
    Remove-Item "$dst.off" -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item $dst -Recurse -Force -ErrorAction SilentlyContinue
    Copy-Item (Join-Path $repo "mods\$m") $dst -Recurse
}
$auto = Join-Path $modsDir 'autostart'
Remove-Item "$auto.off" -Recurse -Force -ErrorAction SilentlyContinue
if ($NoAutostart) {
    Remove-Item $auto -Recurse -Force -ErrorAction SilentlyContinue
} else {
    New-Item -ItemType Directory -Force $auto | Out-Null
    Set-Content "$auto\mod.toml" "id = `"autostart`"`nname = `"Autostart`"`nversion = `"1.0.0`""
    $call = if ($Start -eq 'load') { 'quick_load' } else { 'new_game' }
    Set-Content "$auto\main.lua" "local done=false`nrisen.on('tick', function() if not done then done=true; risen.log.info('autostart $call -> '..tostring(risen.world.$call())) end end)"
}
# Isolate the game's %LOCALAPPDATA% (savegames, ConfigUser.xml, screenshots) inside the test copy.
$appData = (Join-Path $GameDir 'localappdata').Replace('\', '/')
Set-Content (Join-Path $bin 'spass\config.toml') "[debug]`napp_data = `"$appData`""
if (-not $Keep) {
    Remove-Item (Join-Path $bin 'spass\spass.log') -ErrorAction SilentlyContinue
    Remove-Item (Join-Path $bin 'spass\saves') -Recurse -Force -ErrorAction SilentlyContinue
}

& (Join-Path $PSScriptRoot 'run-windowed.ps1') -GameDir $GameDir -Seconds $Seconds 6>&1 |
    Select-String -Pattern $Filter | ForEach-Object { $_.Line }
