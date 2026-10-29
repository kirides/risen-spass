<#
.SYNOPSIS
  Live check of object looks (docs/RE.md section 62): exports examples/modkit/looks.rmk.json (a rock with its own twice-as-big mesh and its own material) with
  risen-modkit, plays it and photographs the look next to the shipped rock (bin\spass\screenshots\looks_*.png). The generated files derive from the game: they are
  made here and not committed (mods/modkit_looks/assets is ignored).
#>
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$data = 'B:\SteamLibrary\steamapps\common\Risen\data'
& (Join-Path $root 'build\src\modkit\RelWithDebInfo\risen-modkit.exe') (Join-Path $root 'examples\modkit\looks.rmk.json') --data $data --export (Join-Path $root 'mods\modkit_looks')
if ($LASTEXITCODE) { throw 'export failed' }
& "$PSScriptRoot\test-run.ps1" -Mods modkit_looks, matprobe_shots -Instance 2 -WaitForResult -Seconds 200 -Param prefix=looks, template=MKL_BigRock, dist=450, material=MKL_BigRock_0._xmat -Filter 'matprobe: |look|\[error\]'
