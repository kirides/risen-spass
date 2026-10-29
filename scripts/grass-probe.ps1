<#
.SYNOPSIS
  Live check of painted grass (docs/RE.md section 64): exports examples/modkit/grass.rmk.json (one erase stroke, one add stroke) and photographs the start
  (bin\spass\screenshots\grass_*.png).
#>
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$data = 'B:\SteamLibrary\steamapps\common\Risen\data'
& (Join-Path $root 'build\src\modkit\RelWithDebInfo\risen-modkit.exe') (Join-Path $root 'examples\modkit\grass.rmk.json') --data $data --export (Join-Path $root 'mods\modkit_grass')
if ($LASTEXITCODE) { throw 'export failed' }
& "$PSScriptRoot\test-run.ps1" -Mods modkit_grass, matprobe_shots -Instance 2 -WaitForResult -Seconds 200 -Param prefix=grass, template=It_Apple, dist=200, material=Special_Water_Island_01_Diffuse_01._xmat -Filter 'vegetation|matprobe: |\[error\]'
