<#
.SYNOPSIS
  Live check of the material and mesh codecs (docs/RE.md sections 60 and 61): a shipped material (and optionally mesh), changed with risen-material, is loaded
  by the engine as a loose file. Runs mods/matprobe_shots twice (the shipped files, then the changed ones); look at bin\spass\screenshots\matprobe_*.png.
.EXAMPLE
  .\scripts\matprobe.ps1 -Material Nat_Stone_Objects_02_Diffuse_01._xmat -Template Obj_Nat_Stone_Small_Flat_01 -Distance 300 -Mesh Obj_Nat_Stone_Small_Flat_01._xmsh -Scale 2
#>
param(
    [string]$Material = 'ItWpn_SwordMisc_01_Diffuse_01._xmat',
    [string]$Template = 'It_1H_Club',
    [string]$Distance = '150',
    [string]$Image = 'ItWpn_SwordBlades_01_Diffuse_01.tga',
    [string]$Mesh = '',   # also changes this mesh: scaled by -Scale
    [string]$Scale = '2'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$data = 'B:\SteamLibrary\steamapps\common\Risen\data'
$tool = Join-Path $root 'build\src\modkit\RelWithDebInfo\risen-material.exe'
$fix = Join-Path $env:TEMP 'matprobe_fixture'
Remove-Item $fix -Recurse -Force -ErrorAction SilentlyContinue
# files of earlier runs stay in the deployed mod (CopyInto only adds): a stale loose file would shadow the packed one again
Remove-Item 'B:\DevDrive\risen-test2\bin\spass\mods\matprobe' -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$fix\assets" | Out-Null
& $tool $data patch $Material "$fix\assets\$Material" --slot "Diffuse=$Image" --set eCShaderDefault.EnableSpecular=0
if ($LASTEXITCODE) { throw 'risen-material failed' }
if ($Mesh) {
    & $tool $data mesh $Mesh "$fix\assets\$Mesh" --scale $Scale
    if ($LASTEXITCODE) { throw 'risen-material mesh failed' }
}
$common = @("template=$Template", "dist=$Distance", "material=$Material")
& "$PSScriptRoot\test-run.ps1" -Mods matprobe_shots -Instance 2 -WaitForResult -Seconds 200 -Param (@('prefix=matprobe_base') + $common)
$base = $LASTEXITCODE
& "$PSScriptRoot\test-run.ps1" -Mods matprobe, matprobe_shots -CopyInto "matprobe=$fix" -Instance 2 -WaitForResult -Seconds 200 -Param (@('prefix=matprobe_mod') + $common) -Filter 'matprobe|\[error\]'
Write-Host "base exit $base, mod exit $LASTEXITCODE"
