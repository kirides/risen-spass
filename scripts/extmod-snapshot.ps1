<#
.SYNOPSIS
    Takes a milestone snapshot of the Risen Extended Mod re-implementation: a self-contained folder that scripts\play-extmod.ps1 (or the play.ps1 inside it)
    turns into a playable windowed game.

.DESCRIPTION
    B:\DevDrive\extmod-milestones\<Name>\
        bin\sentry.dll          the layer as built right now (build\RelWithDebInfo)
        mods\risen_extended\    the generated mod (risen-pack2mod output, or tools\extmod_gen.py output, in mods\risen_extended, incl. the loose assets)
        mods\modmanager\        the in-game Mods page
        mods\quickloot\         optional quick loot (-NoQuickLoot leaves it out)
        README.md               what is in it, what is known broken / missing, the changelog checklist, how to start
        snapshot.json           machine-readable version of the same (commit, counts, checklist)
        play.ps1                copy of scripts\play-extmod.ps1: starts THIS snapshot in the private play copy B:\DevDrive\risen-play
    B:\DevDrive\extmod-milestones\LATEST.md   updated: the newest snapshot, what it contains, known issues, the play command, the list of all snapshots.
    The repo commit is tagged extmod-<NN> (NN = the number in front of the name). The snapshot folders are private (derived from the mod's content):
    never commit or publish them.

.PARAMETER Name    "NN-short-name", e.g. 01-data.
.PARAMETER Title   One line: what this state is.
.PARAMETER Verified  Free text: what was verified live in the game for this snapshot (copied into the README).
.PARAMETER Known   Known broken / missing, one string per item.
.PARAMETER Force   Replace an existing snapshot folder of that name.
.PARAMETER NoTag   Do not create the git tag.
#>
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [string]$Title = '',
    [string[]]$Verified = @(),
    [string[]]$Known = @(),
    [string]$Root = 'B:\DevDrive\extmod-milestones',
    [switch]$NoQuickLoot,
    [switch]$Force,
    [switch]$NoTag
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if ($Name -notmatch '^(\d\d)-[a-z0-9_-]+$') { throw "-Name must look like 01-data (two digits, a dash, lower-case words)" }
$num = $Matches[1]
$src = Join-Path $repo 'mods\risen_extended'
if (-not (Test-Path (Join-Path $src 'manifest.json'))) { throw "no generated mod at ${src}: run risen-pack2mod (docs/PACK2MOD.md) first" }
$dll = Join-Path $repo 'build\RelWithDebInfo\sentry.dll'
if (-not (Test-Path $dll)) { throw "no ${dll}: build the layer first" }
$dest = Join-Path $Root $Name
if (Test-Path $dest) {
    if (-not $Force) { throw "$dest exists (use -Force to replace it)" }
    Remove-Item $dest -Recurse -Force
}
New-Item -ItemType Directory -Force (Join-Path $dest 'bin'), (Join-Path $dest 'mods') | Out-Null

Copy-Item $dll (Join-Path $dest 'bin\sentry.dll')
$layerMods = @('modmanager')
if (-not $NoQuickLoot) { $layerMods += 'quickloot' }
foreach ($m in $layerMods) {
    & robocopy (Join-Path $repo "mods\$m") (Join-Path $dest "mods\$m") /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy $m failed ($LASTEXITCODE)" }
}
& robocopy $src (Join-Path $dest 'mods\risen_extended') /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy risen_extended failed ($LASTEXITCODE)" }
$LASTEXITCODE = 0
Copy-Item (Join-Path $repo 'scripts\play-extmod.ps1') (Join-Path $dest 'play.ps1')
Copy-Item (Join-Path $repo 'scripts\extmod-features.ps1') (Join-Path $dest 'extmod-features.ps1')

# ---- facts ----
Push-Location $repo
$commit = (git rev-parse --short HEAD).Trim()
$dirty = @(git status --porcelain --untracked-files=no).Count -gt 0
Pop-Location
$manifest = Get-Content (Join-Path $src 'manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$status = Get-Content (Join-Path $repo 'tools\extmod_templates\changelog_status.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$dllInfo = Get-Item $dll
$sha = (Get-FileHash $dll -Algorithm SHA256).Hash.Substring(0, 12)
$date = Get-Date -Format 'yyyy-MM-dd HH:mm'

$counts = $manifest.counts
$sum = @{ done = 0; partial = 0; missing = 0; na = 0 }
foreach ($s in $status) {
    $k = switch ($s.status) { 'verified' { 'done' } 'generated' { 'done' } 'partial' { 'partial' } 'na' { 'na' } default { 'missing' } }
    $sum[$k]++
}
$meta = [ordered]@{
    name = $Name; number = [int]$num; title = $Title; date = $date; commit = $commit; dirty = $dirty
    sentry_sha256_12 = $sha; sentry_built = $dllInfo.LastWriteTime.ToString('yyyy-MM-dd HH:mm')
    counts = $counts; verified = $Verified; known = $Known
    changelog = @($status)
}
$meta | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $dest 'snapshot.json') -Encoding UTF8

# ---- README ----
$sb = [System.Text.StringBuilder]::new()
[void]$sb.AppendLine("# Risen Extended - milestone $Name")
[void]$sb.AppendLine()
[void]$sb.AppendLine("**$Title**")
[void]$sb.AppendLine()
[void]$sb.AppendLine("Snapshot of $date, repo commit ``$commit``$(if ($dirty) { ' (+ uncommitted changes)' }), layer ``sentry.dll`` $sha (built $($dllInfo.LastWriteTime.ToString('yyyy-MM-dd HH:mm'))).")
[void]$sb.AppendLine("Private: this folder contains the converted work of the mod's author. Do not publish it.")
[void]$sb.AppendLine()
[void]$sb.AppendLine('## How to play')
[void]$sb.AppendLine()
[void]$sb.AppendLine('```powershell')
[void]$sb.AppendLine("& '$dest\play.ps1'              # makes / refreshes the private play copy B:\DevDrive\risen-play and starts the game windowed (1600x900) at the main menu")
[void]$sb.AppendLine("& '$dest\play.ps1' -Fresh       # the same with the play copy's savegames deleted")
[void]$sb.AppendLine('```')
[void]$sb.AppendLine()
[void]$sb.AppendLine("The play copy is separate from your real game and its savegames (settings and saves live inside B:\DevDrive\risen-play). Start a **new game** (Neues Spiel); savegames of earlier snapshots load as well (items are created from their names).")
[void]$sb.AppendLine("The ESC menu has a *Mods* page (mod manager). Options of the conversion: ``mods\risen_extended\config.lua`` in the play copy (``B:\DevDrive\risen-play\bin\spass\mods\risen_extended\config.lua``; it is overwritten by the next start, change it in the snapshot folder).")
[void]$sb.AppendLine()
[void]$sb.AppendLine('## What is in it')
[void]$sb.AppendLine()
[void]$sb.AppendLine("* **$($counts.items_changed)** shipped items with the mod's numbers (values, damage, requirements, effects, mana costs, categories, icons)")
[void]$sb.AppendLine("* **$($counts.new_items)** new items (weapons, armor, helmets, rings, amulets, books, smithing stages ...), of them **$($counts.new_recipe_items_edited)** recipe items (smithing chains, scribe plans) and **$($counts.items_that_became_recipes)** extra recipes for pearls / gold / ore / obsidian (learned at the start)")
[void]$sb.AppendLine("* **$($counts.template_patches)** template patches (monster actors / light range)")
[void]$sb.AppendLine("* **$($counts.assets_new + $counts.assets_replaced)** loose assets ($($counts.assets_new) new names, $($counts.assets_replaced) replace shipped resources; $([math]::Round($counts.asset_bytes / 1MB)) MB)")
[void]$sb.AppendLine("* texts for the new items in English, German, French, Italian, Spanish (other languages show English)")
[void]$sb.AppendLine("* layer mods: $($layerMods -join ', ')")
[void]$sb.AppendLine()
if ($Verified.Count -gt 0) {
    [void]$sb.AppendLine('## Verified live in the game')
    [void]$sb.AppendLine()
    foreach ($v in $Verified) { [void]$sb.AppendLine("* $v") }
    [void]$sb.AppendLine()
}
[void]$sb.AppendLine('## Known broken / missing')
[void]$sb.AppendLine()
if ($Known.Count -eq 0) { [void]$sb.AppendLine('* nothing recorded') }
foreach ($k in $Known) { [void]$sb.AppendLine("* $k") }
if ($manifest.unconverted.Count -gt 0) {
    [void]$sb.AppendLine("* $($manifest.unconverted.Count) data entries could not be converted (mods\risen_extended\manifest.json, ``unconverted``: file or entity, reason, which milestone would handle it)")
}
[void]$sb.AppendLine()
[void]$sb.AppendLine("## Changelog checklist ($($sum.done) covered, $($sum.partial) partly, $($sum.missing) not yet, $($sum.na) not identifiable)")
[void]$sb.AppendLine()
[void]$sb.AppendLine('Status: `verified` = checked live, `generated` = converted and loading, `partial`, `missing` (the milestone that adds it), `na` = not found in the archives.')
[void]$sb.AppendLine()
[void]$sb.AppendLine('| | Changelog item | Status | Note |')
[void]$sb.AppendLine('|---|---|---|---|')
foreach ($s in $status) {
    $box = switch ($s.status) { 'verified' { '[x]' } 'generated' { '[x]' } 'partial' { '[~]' } 'na' { '[-]' } default { '[ ]' } }
    $note = $s.note
    if ($s.needs) { $note = ("$note (needs $($s.needs))").Trim() }
    [void]$sb.AppendLine("| $box | $($s.item) | $($s.status) | $note |")
}
[void]$sb.AppendLine()
[void]$sb.AppendLine('Details of every item: docs/EXTENDED_MOD_ANALYSIS.md in the repository.')
Set-Content (Join-Path $dest 'README.md') $sb.ToString() -Encoding UTF8

# ---- LATEST.md ----
$all = Get-ChildItem $Root -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'snapshot.json') } | Sort-Object Name
$lb = [System.Text.StringBuilder]::new()
[void]$lb.AppendLine('# Risen Extended - latest playable snapshot')
[void]$lb.AppendLine()
[void]$lb.AppendLine("**Latest: $Name** - $Title ($date, commit ``$commit``)")
[void]$lb.AppendLine()
[void]$lb.AppendLine('```powershell')
[void]$lb.AppendLine("& '$dest\play.ps1'")
[void]$lb.AppendLine('```')
[void]$lb.AppendLine()
[void]$lb.AppendLine("Contents: $($counts.items_changed) changed items, $($counts.new_items) new items, $($counts.assets_new + $counts.assets_replaced) loose assets, texts in 5 languages. Changelog: $($sum.done) covered, $($sum.partial) partly, $($sum.missing) not yet.")
[void]$lb.AppendLine()
if ($Verified.Count -gt 0) { [void]$lb.AppendLine('Verified live: ' + ($Verified -join '; ')); [void]$lb.AppendLine() }
[void]$lb.AppendLine('Known issues:')
foreach ($k in $Known) { [void]$lb.AppendLine("* $k") }
[void]$lb.AppendLine()
[void]$lb.AppendLine("Full list and checklist: ``$dest\README.md``")
[void]$lb.AppendLine()
[void]$lb.AppendLine('## All snapshots')
[void]$lb.AppendLine()
foreach ($d in $all) {
    $m = Get-Content (Join-Path $d.FullName 'snapshot.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    [void]$lb.AppendLine("* **$($m.name)** ($($m.date), ``$($m.commit)``): $($m.title) - ``& '$($d.FullName)\play.ps1'``")
}
Set-Content (Join-Path $Root 'LATEST.md') $lb.ToString() -Encoding UTF8

# ---- tag ----
if (-not $NoTag) {
    Push-Location $repo
    git tag -f "extmod-$num" HEAD | Out-Null
    Pop-Location
    Write-Host "tagged extmod-$num at $commit$(if ($dirty) { ' (the working tree has uncommitted changes: the tag marks the last commit)' })"
}
Write-Host "snapshot $Name -> $dest"
Write-Host "LATEST.md updated"
