<#
.SYNOPSIS
  Live check of painted water (docs/RE.md section 63): exports examples/modkit/water.rmk.json (a pond polygon with the island water material), plays it and
  photographs the spot next to the start (bin\spass\screenshots\water_*.png). The generated mesh is not committed (mods/modkit_water/assets is ignored).
#>
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$data = 'B:\SteamLibrary\steamapps\common\Risen\data'
& (Join-Path $root 'build\src\modkit\RelWithDebInfo\risen-modkit.exe') (Join-Path $root 'examples\modkit\water.rmk.json') --data $data --export (Join-Path $root 'mods\modkit_water')
if ($LASTEXITCODE) { throw 'export failed' }
& "$PSScriptRoot\test-run.ps1" -Mods modkit_water, matprobe_shots -Instance 2 -WaitForResult -Seconds 200 -Param prefix=water, template=It_Apple, dist=250, material=Special_Water_Island_01_Diffuse_01._xmat -Filter 'matprobe: |water|\[error\]'
