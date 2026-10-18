<#
.SYNOPSIS
    Live check of risen-pack2mod --project on a real mod: the converted editor project, exported by the editor, runs in the game.

.DESCRIPTION
    Needs the mod, the game data and a build (it skips cleanly otherwise). Steps: convert the mod to an editor project (risen-pack2mod --project), export it with
    risen-modkit into <repo>\mods\extproj (git-ignored), and run mods\extproj_check (items, modifier order, texts, template edits, XP and refills on shipped infos,
    a teaching limit, a chest) in a windowed, muted test copy with the risen.test scenario `extproj`. Exit code 0 = all checks passed.

.PARAMETER Mod       The folder with the mod's .pNN archives.
.PARAMETER Baseline  The Risen data directory (the user's own game; read only).
.PARAMETER Classic   The classic game's data directory (read only; default: the Steam install). With it the converter measures the mod's changes against the classic game.
.PARAMETER Instance  The parallel test copy to use (default 5: B:\DevDrive\risen-test5).
#>
param(
    [string]$Mod = 'B:\TMP\Risen_Extended_Mod',
    [string]$Baseline = 'B:\DevDrive\risen-test\data',
    [string]$Work = 'B:\TMP\p2p-live',
    [string]$Classic = 'B:\SteamLibrary\steamapps\common\Risen Classic 2009\data',
    [int]$Instance = 5,
    [string]$Config = 'RelWithDebInfo'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repo "build\$Config\risen-pack2mod.exe"
$kit = Join-Path $repo "build\src\modkit\$Config\risen-modkit.exe"
$profile = Join-Path $repo 'tools\pack2mod\profiles\risen_extended.json'
if ($Work -like 'B:\SteamLibrary*' -or $Baseline -like 'B:\SteamLibrary*') { throw 'refusing to work on the real install: use the isolated test copy B:\DevDrive\risen-test' }
if (-not (Test-Path $Mod) -or -not (Test-Path (Join-Path $Baseline 'common')) -or -not (Test-Path $exe) -or -not (Test-Path $kit)) {
    Write-Host "skipped: needs the mod ($Mod), the game data ($Baseline), $exe and $kit"
    exit 0
}
Remove-Item $Work -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $Work | Out-Null
$project = Join-Path $Work 'proj\extproj.rmk.json'
$more = @()
if (Test-Path (Join-Path $Classic 'common')) { $more = @('--classic', $Classic) } else { Write-Host '   (no classic game: converting against the baseline alone)' }
& $exe $Mod --baseline $Baseline @more --project $project --profile $profile --id extproj | Select-Object -Last 2 | ForEach-Object { Write-Host "   $_" }
if ($LASTEXITCODE -ne 0) { throw 'risen-pack2mod failed' }
$target = Join-Path $repo 'mods\extproj'
Remove-Item $target -Recurse -Force -ErrorAction SilentlyContinue
$out = & $kit $project --data $Baseline --export $target 2>&1 | ForEach-Object { "$_" }
@($out | Where-Object { $_ -match '\[error\]' }) | ForEach-Object { Write-Host "   $_" }
Write-Host ("   editor: {0} warning(s)" -f @($out | Where-Object { $_ -match '\[warn\]' }).Count)
if (-not (Test-Path (Join-Path $target 'mod.toml'))) { throw 'the editor refused the project' }
& (Join-Path $repo 'scripts\test-run.ps1') -Instance $Instance -Mods extproj, extproj_check -WaitForResult -Seconds 400 -Scenario extproj -ShowChecks
exit $LASTEXITCODE
