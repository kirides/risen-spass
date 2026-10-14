<#
.SYNOPSIS
    Installs risen-spass into a Risen (Enhanced, x64) game directory.

.DESCRIPTION
    Risen.exe statically imports sentry.dll (the crash reporter), so the mod layer ships as a proxy
    DLL with that name which forwards every export to the original, renamed to sentry_orig.dll.
    Nothing else in the game directory is modified. Undo with uninstall.ps1.

.PARAMETER GameDir
    Risen root directory (the folder that contains bin\Risen.exe and data\).

.PARAMETER Config
    Build configuration to install (default RelWithDebInfo).

.PARAMETER WithExamples
    Also copy the example mods from mods\ into <bin>\spass\mods.
#>
param(
    [Parameter(Mandatory = $true)][string]$GameDir,
    [string]$Config = 'RelWithDebInfo',
    [switch]$WithExamples
)
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $GameDir 'bin'
$proxy = Join-Path $repo "build\$Config\sentry.dll"
$sentry = Join-Path $bin 'sentry.dll'
$orig = Join-Path $bin 'sentry_orig.dll'

if (-not (Test-Path (Join-Path $bin 'Risen.exe'))) { throw "Risen.exe not found in $bin" }
if (-not (Test-Path $proxy)) { throw "Build first: $proxy does not exist" }

if (Test-Path $orig) {
    Write-Host "sentry_orig.dll already present - updating the proxy only."
} else {
    if (-not (Test-Path $sentry)) { throw "$sentry not found" }
    Move-Item $sentry $orig
    Write-Host "renamed sentry.dll -> sentry_orig.dll"
}
Copy-Item $proxy $sentry -Force
Write-Host "installed proxy -> $sentry"

$data = Join-Path $bin 'spass'
New-Item -ItemType Directory -Force (Join-Path $data 'mods') | Out-Null
# LuaLS definitions (completion and type checks in the editor): <spass>\types and a .luarc.json next to the mods.
Copy-Item (Join-Path $repo 'types') $data -Recurse -Force
$luarc = Join-Path $data 'mods\.luarc.json'
if (-not (Test-Path $luarc)) { Copy-Item (Join-Path $repo 'mods\.luarc.json') $luarc }
Write-Host "copied the Lua definitions (types\risen.d.lua)"
if ($WithExamples) {
    Copy-Item (Join-Path $repo 'mods\*') (Join-Path $data 'mods') -Recurse -Force
    Write-Host "copied example mods"
}
Write-Host "Done. Log file: $data\spass.log"
