<#
.SYNOPSIS
    Builds a release zip: the proxy DLL, install/uninstall scripts, the example mods, the Lua definitions and the docs.

.DESCRIPTION
    Run after `cmake --build build --config RelWithDebInfo` (and the unit tests). The zip contains everything a player or
    modder needs (also risen-pack2mod.exe, the converter for mods that ship .pNN/.pak archives: docs\PACK2MOD.md); install it with:
        .\install.ps1 -GameDir 'C:\...\Risen' [-WithExamples]      # -WithExamples installs the small mods of mods\ (hello, fetchquest, quickloot, ...); examples\ is for reading and copying
    Nothing in the game directory except bin\sentry.dll is touched (the original is kept as sentry_orig.dll);
    `[ -enabled = false ]` in bin\spass\config.toml switches the whole layer off without uninstalling.

.PARAMETER Config  Build configuration to package (default RelWithDebInfo).
.PARAMETER Out     Output directory (default <repo>\dist).
#>
param(
    [string]$Config = 'RelWithDebInfo',
    [string]$Out = ''
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if ($Out -eq '') { $Out = Join-Path $repo 'dist' }

$dll = Join-Path $repo "build\$Config\sentry.dll"
if (-not (Test-Path $dll)) { throw "Build first: $dll does not exist" }
$cmake = Get-Content (Join-Path $repo 'CMakeLists.txt') -Raw
if ($cmake -notmatch 'project\(risen_spass VERSION ([0-9.]+)') { throw 'cannot read the version from CMakeLists.txt' }
$version = $Matches[1]
$name = "risen-spass-$version"

$stage = Join-Path $Out $name
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Item $dll (Join-Path $stage 'sentry.dll')
# the converter for archive mods (docs/PACK2MOD.md) goes next to the layer, with its profiles
$p2m = Join-Path $repo "build\$Config\risen-pack2mod.exe"
if (-not (Test-Path $p2m)) { throw "Build first: $p2m does not exist" }
Copy-Item $p2m (Join-Path $stage 'risen-pack2mod.exe')
New-Item -ItemType Directory -Force (Join-Path $stage 'pack2mod-profiles') | Out-Null
Copy-Item (Join-Path $repo 'tools\pack2mod\profiles\*.json') (Join-Path $stage 'pack2mod-profiles')
Copy-Item (Join-Path $repo 'scripts\install.ps1'), (Join-Path $repo 'scripts\uninstall.ps1') $stage
Copy-Item (Join-Path $repo 'types') $stage -Recurse
New-Item -ItemType Directory -Force (Join-Path $stage 'docs') | Out-Null
foreach ($d in 'MODDING_GUIDE.md', 'LUA_API.md', 'EXAMPLES.md', 'QUICKLOOT.md', 'TEMPLATES.md', 'ITEM_PROPERTIES.md', 'NPC_PROPERTIES.md', 'SPELL_PROPERTIES.md', 'RECIPE_PROPERTIES.md', 'PACK2MOD.md', 'PROPERTIES.md', 'SYMBOLS.md', 'DEVELOPMENT.md', 'RE.md') {
    Copy-Item (Join-Path $repo "docs\$d") (Join-Path $stage 'docs') -ErrorAction SilentlyContinue
}
Copy-Item (Join-Path $repo 'README.md') $stage

# The Mod Manager (the Mods tab of the game's menu) is installed by install.ps1 itself; the rest are example mods that make sense for end users; the *_driver/probe/dump mods are development tools.
$mods = Join-Path $stage 'mods'
New-Item -ItemType Directory -Force $mods | Out-Null
Copy-Item (Join-Path $repo 'mods\.luarc.json') $mods
foreach ($m in 'hello', 'fetchquest', 'stringsdemo', 'eventlog', 'quickloot', 'modmanager', '_template') {
    Copy-Item (Join-Path $repo "mods\$m") $mods -Recurse
}

# Mods to READ and COPY, not to install: they change the game's balance (balance_example) or only demonstrate one pattern each (example_*), so they go to examples\, which
# install.ps1 -WithExamples does not touch. Copy a folder into <Risen>\bin\spass\mods to use it. The *_driver mods and their scenarios are development tools and stay out.
$examples = Join-Path $stage 'examples'
New-Item -ItemType Directory -Force $examples | Out-Null
Copy-Item (Join-Path $repo 'mods\.luarc.json') $examples
foreach ($m in @('balance_example') + @(Get-ChildItem (Join-Path $repo 'mods') -Directory -Filter 'example_*' | ForEach-Object { $_.Name })) {
    Copy-Item (Join-Path $repo "mods\$m") $examples -Recurse
}

New-Item -ItemType Directory -Force $Out | Out-Null
$zip = Join-Path $Out "$name.zip"
Remove-Item $zip -ErrorAction SilentlyContinue
Compress-Archive -Path $stage -DestinationPath $zip
Write-Host "created $zip ($([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB)"
