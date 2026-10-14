# risen-spass

A hook-based **Lua (LuaJIT) scripting layer for Risen Enhanced (x64)**.

Risen's engine exports its C++ classes by name, and its game scripts are native functions looked up by name in a
registry. `risen-spass` binds to those exports (no hard-coded addresses), lets Lua define/override scripts, query
and drive the world, and overlay localized text. See [`docs/RE.md`](docs/RE.md) for the reverse-engineering
notes and [`docs/LUA_API.md`](docs/LUA_API.md) for the Lua API.

## Build

Requirements: Visual Studio 2022/2026 (x64), CMake ≥ 3.25, [vcpkg](https://github.com/microsoft/vcpkg)
(`VCPKG_ROOT` set), Python + `uv` for the RE tools.

```powershell
$env:VCPKG_ROOT = 'path\to\vcpkg'
cmake --preset default
cmake --build build --config RelWithDebInfo     # -> build\RelWithDebInfo\sentry.dll
```

## Install / uninstall

```powershell
scripts\install.ps1   -GameDir 'C:\...\Risen' -WithExamples
scripts\uninstall.ps1 -GameDir 'C:\...\Risen'
```

The proxy is installed as `bin\sentry.dll` (the original is kept as `sentry_orig.dll`). Mods go to
`bin\spass\mods\`, the log is `bin\spass\spass.log`.

## Development

* `scripts/run-windowed.ps1` launches a **test copy** of the game windowed, resizes the window and prints the log.
  Use a copy of `bin\` plus a junction to the real `data\` (with a private `data\ini` whose `ConfigLocal.xml` has
  `FullScreen="false"`); the game rewrites `%LOCALAPPDATA%\Risen\ConfigUser.xml` while running.
* `tools/` — export dumping/demangling, disassembler, Ghidra headless decompile helper (see `docs/RE.md` §8).
* `mods/selftest` exercises every API; set `AUTOSTART = true` in a throw-away install to enter a new game.
