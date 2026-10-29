<#
.SYNOPSIS
  Measures the collision of a shipped rock and of a template with a scaled copy of its mesh with rays (docs/RE.md section 61 / 65).
  .\scripts\colprobe.ps1 [-Variant 'eCCollisionShape.IsLazyGenerated=True']    property overrides of the scaled template (Set.Name=value, comma separated)
#>
param([string]$Variant = 'none', [string]$Scale = '3', [string]$Fit = 'no', [string]$FitScale = '1')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$data = 'B:\SteamLibrary\steamapps\common\Risen\data'
$fix = Join-Path $env:TEMP 'colprobe_fixture'
Remove-Item $fix -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item 'B:\DevDrive\risen-test2\bin\spass\mods\colprobe' -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$fix\assets" | Out-Null
& (Join-Path $root 'build\src\modkit\RelWithDebInfo\risen-material.exe') $data mesh Obj_Nat_Stone_Small_Flat_01._xmsh "$fix\assets\Spass_Rock3x._xmsh" --scale $Scale | Out-Null
& "$PSScriptRoot\test-run.ps1" -Mods colprobe -CopyInto "colprobe=$fix" -Instance 2 -WaitForResult -Seconds 200 -Param "variant=$Variant","fit=$Fit","fitscale=$FitScale" -Filter 'colprobe'
