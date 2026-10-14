<#
.SYNOPSIS
    Removes risen-spass from a Risen game directory and restores the original sentry.dll.
    The bin\spass folder (logs, config, your mods) is left in place.
#>
param([Parameter(Mandatory = $true)][string]$GameDir)
$ErrorActionPreference = 'Stop'

$bin = Join-Path $GameDir 'bin'
$sentry = Join-Path $bin 'sentry.dll'
$orig = Join-Path $bin 'sentry_orig.dll'

if (-not (Test-Path $orig)) { Write-Host "Nothing to do: sentry_orig.dll not found."; return }
if (Test-Path $sentry) { Remove-Item $sentry -Force }
Move-Item $orig $sentry
Write-Host "restored original sentry.dll"
