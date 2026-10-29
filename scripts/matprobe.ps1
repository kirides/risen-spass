<#
.SYNOPSIS
  Live check of the material codec (docs/RE.md section 60): the club's material, changed with risen-material, is loaded by the engine as a loose file.
  Runs mods/matprobe_shots twice (the shipped material, then the changed one) and compares the pictures (bin\spass\screenshots\matprobe_*.png).
#>
param(
    [string]$Material = 'ItWpn_SwordMisc_01_Diffuse_01._xmat',
    [string]$Template = 'It_1H_Club',
    [string]$Distance = '150',
    [string]$Image = 'ItWpn_SwordBlades_01_Diffuse_01.tga'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$data = 'B:\SteamLibrary\steamapps\common\Risen\data'
$tool = Join-Path $root 'build\src\modkit\RelWithDebInfo\risen-material.exe'
$fix = Join-Path $env:TEMP 'matprobe_fixture'
Remove-Item $fix -Recurse -Force -ErrorAction SilentlyContinue
# files of earlier runs stay in the deployed mod (CopyInto only adds): a stale loose material would shadow the packed one again
Remove-Item 'B:\DevDriveisen-test2in\spass\mods\matprobe' -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$fix\assets" | Out-Null
& $tool $data patch ItWpn_SwordMisc_01_Diffuse_01._xmat "$fix\assets\ItWpn_SwordMisc_01_Diffuse_01._xmat" --slot Diffuse=ItWpn_SwordBlades_01_Diffuse_01.tga --set eCShaderDefault.EnableSpecular=0
if ($LASTEXITCODE) { throw 'risen-material failed' }
& "$PSScriptRoot\test-run.ps1" -Mods matprobe_shots -Instance 2 -WaitForResult -Seconds 200 -Param prefix=matprobe_base,template=$Template,dist=$Distance,material=$Material
$base = $LASTEXITCODE
& "$PSScriptRoot\test-run.ps1" -Mods matprobe,matprobe_shots -CopyInto "matprobe=$fix" -Instance 2 -WaitForResult -Seconds 200 -Param prefix=matprobe_mod,template=$Template,dist=$Distance,material=$Material -Filter 'matprobe|\[error\]'
Write-Host "base exit $base, mod exit $LASTEXITCODE"
