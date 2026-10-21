<#
.SYNOPSIS
    Builds every executable and one release zip: the proxy DLL, risen-pack2mod.exe, the mod editor risen-modkit.exe, install/uninstall scripts,
    the example mods, the Lua definitions and the docs.

.DESCRIPTION
    Builds the targets first (`cmake --build build --config <Config> --target spass_proxy risen-pack2mod risen-modkit`; the build folder must be
    configured, see CLAUDE.md "Build"), then stages and zips. The zip contains everything a player or modder needs: the layer, risen-pack2mod.exe
    (the converter for mods that ship .pNN/.pak archives: docs\PACK2MOD.md) and the mod editor in modkit\ (risen-modkit.exe with its language files,
    the example projects, docs\Modding_With_ModKit.md); install the layer with:
        .\install.ps1 -GameDir 'C:\...\Risen' [-WithExamples]      # -WithExamples installs the small mods of mods\ (hello, fetchquest, quickloot, ...); examples\ is for reading and copying
    Nothing in the game directory except bin\sentry.dll is touched (the original is kept as sentry_orig.dll);
    `[ -enabled = false ]` in bin\spass\config.toml switches the whole layer off without uninstalling.

.PARAMETER Config    Build configuration to build and package (default RelWithDebInfo).
.PARAMETER Out       Output directory (default <repo>\dist).
.PARAMETER NoBuild   Package what is already built (CI copies its Ninja outputs into the Visual Studio layout and builds the editor in another job).
.PARAMETER NoModkit  Leave the mod editor out (a build without the vcpkg feature `modkit`).
#>
param(
    [string]$Config = 'RelWithDebInfo',
    [string]$Out = '',
    [switch]$NoBuild,
    [switch]$NoModkit
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if ($Out -eq '') { $Out = Join-Path $repo 'dist' }

if (-not $NoBuild) {
    if (-not (Test-Path (Join-Path $repo 'build\CMakeCache.txt'))) { throw "Configure first: cmake --preset default (build\CMakeCache.txt does not exist)" }
    $targets = @('spass_proxy', 'risen-pack2mod') + $(if ($NoModkit) { @() } else { @('risen-modkit') })
    Write-Host "building $($targets -join ', ') ($Config)"
    cmake --build (Join-Path $repo 'build') --config $Config --target @targets
    if ($LASTEXITCODE -ne 0) { throw "the build failed (exit code $LASTEXITCODE)" }
}

$dll = Join-Path $repo "build\$Config\sentry.dll"
if (-not (Test-Path $dll)) { throw "Build first: $dll does not exist" }
$modkitDir = Join-Path $repo "build\src\modkit\$Config"
$modkitExe = Join-Path $modkitDir 'risen-modkit.exe'
if (-not $NoModkit -and -not (Test-Path $modkitExe)) { throw "Build first: $modkitExe does not exist (or pass -NoModkit)" }
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

# The mod editor (docs\Modding_With_ModKit.md): the executable (statically linked) with its language files, the example projects it opens, its docs
if (-not $NoModkit) {
    $mk = Join-Path $stage 'modkit'
    New-Item -ItemType Directory -Force $mk | Out-Null
    Copy-Item $modkitExe $mk
    Copy-Item (Join-Path $modkitDir 'lang') $mk -Recurse
    Copy-Item (Join-Path $repo 'examples\modkit') (Join-Path $mk 'examples') -Recurse
    Copy-Item (Join-Path $repo 'docs\Modding_With_ModKit.md') (Join-Path $stage 'docs')
    Copy-Item (Join-Path $repo 'docs\modkit') (Join-Path $stage 'docs\modkit') -Recurse
    New-Item -ItemType Directory -Force (Join-Path $stage 'docs\editor') | Out-Null
    foreach ($d in 'USER_GUIDE.md', 'CHRONICLE.md', 'LOCALIZATION.md') {
        Copy-Item (Join-Path $repo "docs\editor\$d") (Join-Path $stage 'docs\editor')
    }
    Copy-Item (Join-Path $repo 'docs\editor\chronicle') (Join-Path $stage 'docs\editor\chronicle') -Recurse -ErrorAction SilentlyContinue
}

New-Item -ItemType Directory -Force $Out | Out-Null
$zip = Join-Path $Out "$name.zip"
Remove-Item $zip -ErrorAction SilentlyContinue
Compress-Archive -Path $stage -DestinationPath $zip
Write-Host "created $zip ($([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB)"
