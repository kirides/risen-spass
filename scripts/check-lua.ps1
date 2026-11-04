# Runs the Lua checks exactly like the "lua" job of .github/workflows/build.yml:
#   1. python tools/check_luadefs.py   (types/risen.d.lua matches the bindings)
#   2. lua-language-server --check . --configpath .luarc.json --checklevel Warning   (cd mods)
# Findings in git-ignored files (e.g. mods/risen_extended, local only) are dropped: CI never sees them.
# Exit code 0 = clean, 1 = findings. Run it right before every commit that touches *.lua, types/ or mods/.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$want = '3.19.2'   # LUALS_VERSION in build.yml
$ver = (& lua-language-server --version) -join ''
if ($ver.Trim() -ne $want) { Write-Warning "lua-language-server is ${ver}, CI pins ${want}: results may differ" }

python tools/check_luadefs.py
if ($LASTEXITCODE -ne 0) { Write-Host 'check_luadefs.py FAILED'; exit 1 }

$out = Join-Path $env:TEMP 'luals-check'
Remove-Item -Recurse -Force $out -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $out | Out-Null
Push-Location (Join-Path $root 'mods')
& lua-language-server --check . --configpath .luarc.json --checklevel Warning --check_format json --check_out_path (Join-Path $out 'check.json') | Out-Null
Pop-Location

$file = Join-Path $out 'check.json'
if (-not (Test-Path $file)) { Write-Host 'LuaLS: no problems (no report file)'; exit 0 }
$json = Get-Content $file -Raw | ConvertFrom-Json
$bad = 0
foreach ($p in $json.PSObject.Properties) {
    # keys are file:///b%3A/... URIs (or plain paths, depending on the LuaLS version)
    $path = [Uri]::UnescapeDataString(($p.Name -replace '^file:///', ''))
    $path = $path -replace '^/(?=[A-Za-z]:)', '' -replace '/', '\'
    git check-ignore -q -- $path
    if ($LASTEXITCODE -eq 0) { continue }   # ignored by git: not part of CI
    foreach ($d in $p.Value) {
        $bad++
        $rel = [IO.Path]::GetRelativePath($root, $path)
        Write-Host ("{0}:{1}:{2} [{3}] {4}" -f $rel, ($d.range.start.line + 1), ($d.range.start.character + 1), $d.code, $d.message)
    }
}
if ($bad) { Write-Host "LuaLS: $bad problem(s) - CI would fail"; exit 1 }
Write-Host 'LuaLS: no problems (git-ignored files skipped)'
exit 0
