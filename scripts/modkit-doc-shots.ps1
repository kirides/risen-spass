# Renders the screenshots of docs/Modding_With_ModKit.md with the editor's own --screenshot mode.
#   .\scripts\modkit-doc-shots.ps1 [-Only name,name] [-Data <Risen data dir>]
param(
    [string[]]$Only = @(),
    [string]$Data = 'B:\DevDrive\risen-test\data',
    [string]$Size = '1920x1080'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root 'build\src\modkit\RelWithDebInfo\risen-modkit.exe'
$out = Join-Path $root 'docs\modkit'
$chronicle = Join-Path $root 'examples\modkit\chronicle.rmk.json'
$showcase = Join-Path $root 'examples\modkit\showcase.rmk.json'
$extensions = Join-Path $root 'examples\modkit\extensions.rmk.json'
New-Item -ItemType Directory -Force $out | Out-Null

# name = project, extra arguments
$shots = [ordered]@{
    'items'        = @($chronicle, '--panel', 'Items', '--select', 'MKC_Chronicle_Blade')
    'recipes'      = @($chronicle, '--panel', 'Recipes', '--select', 'MKC_Recipe_Chronicle_Blade')
    'spells'       = @($chronicle, '--panel', 'Spells', '--select', 'MKC_Ember_Whisper')
    'npcs'         = @($chronicle, '--panel', 'NPCs', '--select', 'MKC_Ash_Warlord_T')
    'dialogues'    = @($chronicle, '--panel', 'Dialogues')
    'conversation' = @($chronicle, '--panel', 'Conversation', '--click', '712,562')  # "1. Who are you?"
    'quests'       = @($chronicle, '--panel', 'Quests', '--select', 'Q_MKC_Camp')
    'events'       = @($chronicle, '--panel', 'Events', '--select', 'Camp falls')
    'layers'       = @($chronicle, '--panel', 'Layers', '--select', 'AshCamp')
    'world'        = @($chronicle, '--panel', 'World', '--camera', '-33400,1500,-5000,225,-25', '--frames', '500')
    'browser'      = @($chronicle, '--panel', 'Browser', '--select', 'Wolf')
    'strings'      = @($chronicle, '--panel', 'Strings')
    'palette'      = @($chronicle, '--palette', 'tide')
    'wizard'       = @($showcase, '--wizard', 'weapon')
    'lua'          = @($extensions, '--panel', 'Lua', '--select', 'tweaks')
    'npc_routine'  = @($chronicle, '--panel', 'NPCs', '--select', 'MKC_Ash_Raider_2')
    'npc_groups'   = @($chronicle, '--panel', 'NPC groups')
    'behaviours'   = @($chronicle, '--panel', 'Behaviours', '--select', 'chronicler')
    'patches'      = @($chronicle, '--panel', 'Patches')
    'assets'       = @($chronicle, '--panel', 'Assets')
    'paint'        = @($chronicle, '--panel', 'World', '--camera', '-35080,1500,-6646,0,-40', '--frames', '500')
    'problems'     = @($chronicle, '--panel', 'Problems')
    # needs a running game with [link] enabled (scripts	est-run.ps1 -Link -KeepRunning) and -GameDir: skipped without it
    'live'         = @($chronicle, '--panel', 'Live', '--select', 'Sara', '--frames', '700', '--game-dir', 'B:\DevDrive\risen-test')
    'german'       = @($chronicle, '--panel', 'Items', '--select', 'MKC_Tide_Potion', '--lang', 'de')
}
foreach ($name in $shots.Keys) {
    if ($Only.Count -and $Only -notcontains $name) { continue }
    $png = Join-Path $out "$name.png"
    $extra = @($shots[$name])
    if ($name -eq 'live' -and -not (Test-Path (Join-Path ($extra[$extra.IndexOf('--game-dir') + 1]) 'bin\spass\link.json'))) { "$name skipped (no running game with the live link)"; continue }
    if ($name -ne 'browser') { $extra += '--collapse-side' }  # the main panels get the whole width
    $frames = if ($extra -contains '--frames') { @() } else { @('--frames', '30') }
    $args = $extra + $frames + @('--data', $Data, '--size', $Size, '--screenshot', $png)
    & $exe @args 2>&1 | Out-Null
    if (Test-Path $png) { "$name ok" } else { "$name FAILED" }
}
