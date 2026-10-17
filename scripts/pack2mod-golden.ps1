<#
.SYNOPSIS
    Golden test of risen-pack2mod: converts a real mod with the C++ tool and with the Python oracle (tools/extmod_gen.py) and compares the two outputs.

.DESCRIPTION
    Needs game data and the mod, so it is not part of spass_tests. Compared (tools/pack2mod_golden.py): the file set, data/*.lua, strings/*.csv, features.json,
    assets/ and variants/ byte by byte, main.lua / config.lua, the manifest counts / statistics / unconverted entries / warnings. Explained differences are
    listed by the compare script (docs/PACK2MOD.md "Golden test"). Exit code 0 = identical apart from the explained differences, 1 = unexplained differences.
    The outputs stay in -Work for inspection (they are derived from the mod: never commit them).

.PARAMETER Mod       The folder with the mod's .pNN archives.
.PARAMETER Baseline  The Risen data directory (the user's own game; read only).
.PARAMETER Work      Scratch directory for the two outputs (default B:\TMP\golden).
.PARAMETER Profile   risen-pack2mod profile (default tools\pack2mod\profiles\risen_extended.json).
.PARAMETER KeepPython  Reuse an existing Python output in <Work>\py (it takes about 15 s; only valid for the same -Combat).
.PARAMETER Combat      changelog (default, Python --combat p01), shipped (p02) or off: the combat table choice of both generators.
#>
param(
    [string]$Mod = 'B:\TMP\Risen_Extended_Mod',
    [string]$Baseline = 'B:\DevDrive\risen-test\data',
    [string]$Work = 'B:\TMP\golden',
    [string]$Profile = '',
    [switch]$KeepPython,
    [ValidateSet('changelog', 'shipped', 'off')][string]$Combat = 'changelog',
    [string]$Config = 'RelWithDebInfo'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if ($Profile -eq '') { $Profile = Join-Path $repo 'tools\pack2mod\profiles\risen_extended.json' }
$exe = Join-Path $repo "build\$Config\risen-pack2mod.exe"
if (-not (Test-Path $exe)) { throw "build first: $exe does not exist" }
if ($Work -like 'B:\SteamLibrary*' -or $Baseline -like 'B:\SteamLibrary*') { throw 'refusing to work on the real install: use the isolated test copy B:\DevDrive\risen-test' }

New-Item -ItemType Directory -Force $Work | Out-Null
$pyCombat = @{ changelog = 'p01'; shipped = 'p02'; off = 'off' }[$Combat]
$py = Join-Path $Work 'py'
$cpp = Join-Path $Work 'cpp'
if (-not ($KeepPython -and (Test-Path (Join-Path $py 'manifest.json')))) {
    Remove-Item $py -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host '== Python oracle (tools/extmod_gen.py)'
    $t = Measure-Command { & python -I (Join-Path $repo 'tools\extmod_gen.py') --mod $Mod --baseline $Baseline --out $py --combat $pyCombat | Select-Object -Last 3 }
    Write-Host ("   {0:N1} s" -f $t.TotalSeconds)
    if ($LASTEXITCODE -ne 0) { throw 'extmod_gen.py failed' }
}
Remove-Item $cpp -Recurse -Force -ErrorAction SilentlyContinue
Write-Host '== C++ tool (risen-pack2mod)'
$t = Measure-Command { & $exe $Mod --baseline $Baseline --out $cpp --profile $Profile --combat $Combat | Select-Object -Last 4 }
Write-Host ("   {0:N1} s" -f $t.TotalSeconds)
if ($LASTEXITCODE -ne 0) { throw 'risen-pack2mod failed' }
Write-Host '== compare'
& python -I (Join-Path $repo 'tools\pack2mod_golden.py') $py $cpp
exit $LASTEXITCODE
