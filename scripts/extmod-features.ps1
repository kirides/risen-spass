<#
.SYNOPSIS
    Switches the file based feature groups of the generated risen_extended mod (docs/DEVELOPMENT.md "Risen Extended: generated mod and milestone snapshots").

.DESCRIPTION
    The engine mounts <mod>\assets at start and reads the combat table / weather file once, so these groups are files: this script moves the files of a group between
    <mod>\assets (on) and <mod>\assets_off (off), or swaps the combat table variant. features.json (written by tools/extmod_gen.py) lists the groups. The runtime groups
    (balance, new_items, dialogue, world, teachers) are options in the mod's config.lua. Restart the game after a change.

    Groups: combat (changelog = p01 table, shipped = p02, off), wind, caves, vegetation, skins, weapon_meshes (needs the new items: leave it on).

.EXAMPLE
    .\scripts\extmod-features.ps1 -ModDir B:\DevDrive\risen-play\bin\spass\mods\risen_extended -List
    .\scripts\extmod-features.ps1 -ModDir B:\DevDrive\risen-play\bin\spass\mods\risen_extended -Set combat=off,caves=off,wind=on
#>
param(
    [Parameter(Mandatory = $true)][string]$ModDir,
    [string]$Set = '',
    [switch]$List
)
$ErrorActionPreference = 'Stop'
$ModDir = $ModDir.TrimEnd('\')
if ($ModDir -like 'B:\SteamLibrary*') { throw 'refusing to touch the real install' }
$fj = Join-Path $ModDir 'features.json'
if (-not (Test-Path $fj)) { throw "no features.json in $ModDir (is this the generated risen_extended mod?)" }
$f = Get-Content $fj -Raw | ConvertFrom-Json
$assets = Join-Path $ModDir 'assets'
$off = Join-Path $ModDir 'assets_off'

function Get-State($name) {
    $g = $f.groups.$name
    if ($name -eq 'combat') {
        $cur = Join-Path $assets 'compiled_combatspecies.bin'
        if (-not (Test-Path $cur)) { return 'off' }
        $h = (Get-FileHash $cur -Algorithm SHA1).Hash
        foreach ($k in 'changelog', 'shipped') {
            $v = Join-Path $ModDir $f.combat_variants.$k
            if ((Test-Path $v) -and ((Get-FileHash $v -Algorithm SHA1).Hash -eq $h)) { return $k }
        }
        return 'on (unknown table)'
    }
    $files = @($g.files)
    if ($files.Count -eq 0) { return 'n/a' }
    $in = @($files | Where-Object { Test-Path (Join-Path $assets $_) }).Count
    if ($in -eq $files.Count) { return 'on' }
    if ($in -eq 0) { return 'off' }
    return "partly ($in of $($files.Count))"
}

function Set-Group($name, $value) {
    if (-not $f.groups.PSObject.Properties.Name.Contains($name)) { throw "unknown group '$name' (groups: $($f.groups.PSObject.Properties.Name -join ', '))" }
    if ($name -eq 'combat') {
        $dest = Join-Path $assets 'compiled_combatspecies.bin'
        switch ($value) {
            'off' { Remove-Item $dest -ErrorAction SilentlyContinue }
            { $_ -in 'changelog', 'shipped' } { Copy-Item (Join-Path $ModDir $f.combat_variants.$value) $dest -Force }
            default { throw "combat must be changelog, shipped or off, not '$value'" }
        }
        return
    }
    if ($value -notin 'on', 'off') { throw "$name must be on or off, not '$value'" }
    foreach ($file in @($f.groups.$name.files)) {
        $a = Join-Path $assets $file
        $b = Join-Path $off $file
        if ($value -eq 'off' -and (Test-Path $a)) { New-Item -ItemType Directory -Force $off | Out-Null; Move-Item $a $b -Force }
        elseif ($value -eq 'on' -and (Test-Path $b)) { Move-Item $b $a -Force }
    }
}

if ($Set -ne '') {
    foreach ($pair in $Set.Split(',')) {
        $kv = $pair.Split('=')
        if ($kv.Count -ne 2) { throw "expected name=value, got '$pair'" }
        Set-Group $kv[0].Trim() $kv[1].Trim()
        Write-Host ("{0} -> {1}" -f $kv[0].Trim(), $kv[1].Trim())
    }
}
if ($List -or $Set -eq '') {
    foreach ($n in $f.groups.PSObject.Properties.Name) {
        '{0,-14} {1,-22} {2}' -f $n, (Get-State $n), $f.groups.$n.about
    }
}
if ($Set -ne '') { Write-Host 'Restart the game for the change to take effect.' }
