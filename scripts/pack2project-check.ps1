<#
.SYNOPSIS
    Check of risen-pack2mod --project on a real mod: the editor project it writes exports to the same game data as the layer mod it writes.

.DESCRIPTION
    Needs game data and the mod, so it is not part of the unit tests (those use made-up archives: tests/modkit/pack2project_tests.inc). Steps:
      1. risen-pack2mod <mod> --out <Work>\mod --project <Work>\proj\<id>.rmk.json --verify
         (--verify exports the project with the editor's exporter and compares it with the converted mod: the effective settings of every template, the recipes, the
         texts per language, the container fills and the bytes of every asset file; docs/PACK2MOD.md "Editor project")
      2. risen-modkit <project> --data <Baseline> --export <Work>\export   (the editor's validation with the game data, then the export)
    It prints the tool's summary line and the number of editor errors / warnings. Exit code 0 = equivalent and no validation error, 1 = differences or errors,
    0 with "skipped" when the mod, the game data or the build is missing (like the golden test). The outputs stay in -Work and are derived from the mod: never commit them.

.PARAMETER Mod       The folder with the mod's .pNN archives.
.PARAMETER Baseline  The Risen data directory (the user's own game; read only).
.PARAMETER Work      Scratch directory (default B:\TMP\p2p).
.PARAMETER Profile   risen-pack2mod profile (default tools\pack2mod\profiles\risen_extended.json).
#>
param(
    [string]$Mod = 'B:\TMP\Risen_Extended_Mod',
    [string]$Baseline = 'B:\DevDrive\risen-test\data',
    [string]$Work = 'B:\TMP\p2p',
    [string]$Profile = '',
    [string]$Config = 'RelWithDebInfo'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if ($Profile -eq '') { $Profile = Join-Path $repo 'tools\pack2mod\profiles\risen_extended.json' }
$exe = Join-Path $repo "build\$Config\risen-pack2mod.exe"
$kit = Join-Path $repo "build\src\modkit\$Config\risen-modkit.exe"
if ($Work -like 'B:\SteamLibrary*' -or $Baseline -like 'B:\SteamLibrary*') { throw 'refusing to work on the real install: use the isolated test copy B:\DevDrive\risen-test' }
if (-not (Test-Path $Mod) -or -not (Test-Path (Join-Path $Baseline 'common')) -or -not (Test-Path $exe) -or -not (Test-Path $kit)) {
    Write-Host "skipped: needs the mod ($Mod), the game data ($Baseline), $exe and $kit"
    exit 0
}

Remove-Item $Work -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $Work | Out-Null
$project = Join-Path $Work 'proj\converted.rmk.json'
Write-Host '== risen-pack2mod --out --project --verify'
$out = & $exe $Mod --baseline $Baseline --out (Join-Path $Work 'mod') --project $project --profile $Profile --verify 2>&1
$rc = $LASTEXITCODE
$out | Select-Object -Last 4 | ForEach-Object { Write-Host "   $_" }
if ($rc -ne 0) { Write-Host 'FAIL: the conversion or the verification failed (see report.md in the project folder)'; exit 1 }

Write-Host '== risen-modkit --export (validation with the game data, then the export)'
$log = & $kit $project --data $Baseline --export (Join-Path $Work 'export') 2>&1 | ForEach-Object { "$_" }
$krc = $LASTEXITCODE
$errors = @($log | Select-String '\[error\]').Count
$warnings = @($log | Select-String '\[warn\]').Count
$log | Select-String '\[error\]' | Select-Object -First 10 | ForEach-Object { Write-Host "   $_" }
Write-Host "   editor: $errors error(s), $warnings warning(s), exit code $krc"
if ($krc -ne 0 -or $errors -gt 0) { Write-Host 'FAIL: the editor refuses the project'; exit 1 }
Write-Host 'OK: the project opens without errors and its export equals the converted mod'
exit 0
