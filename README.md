# risen-spass

A **Lua (LuaJIT) modding layer for Risen Enhanced (x64)**: write new quests, dialogues, NPCs, items and texts in Lua, react to
what happens in the game, and keep your mod's state in the savegame.

```lua
-- bin\spass\mods\hello_marta\main.lua
risen.loc.set("FO_Marta", "Marta the Orchardkeeper")

local function content()                       -- runs on a new game and before every savegame is read
    risen.quest.define{ name = "Q_Apples", title = "Marta's Apples", type = "Report" }
    risen.dialog.create{ name = "Marta_Work", owner = "Marta", lines = {
        { speaker = "player", text = "Do you have work for me?" },
        { speaker = "npc",    text = "Bring me three apples." },
    } }
end
risen.on("load.before", content)
risen.on("session.start", function() content(); risen.npc.spawn("Peasant_1_01", 0, 0, 0, { name = "Marta" }) end)
```

Risen's engine exports its C++ classes by name and its game scripts are native functions looked up by name in a registry.
`risen-spass` binds to those exports (no hard-coded addresses, so it survives rebuilds of the same game version), hooks the
engine's lifecycle, and exposes it to Lua with a sandbox per mod.

## What you get

| | |
|---|---|
| Events | session/world/save/load lifecycle, quest status changes, kills, dialogue start/finish, item pick-ups |
| Content | quests, dialogue options (conditions, scripts, log entries, **voiced lines from ogg/wav/mp3 per language**), NPC spawning and configuration, items |
| State | `risen.save`: per-mod data stored with every savegame; entity handles that survive loads |
| Texts | `risen.loc` overlay and `strings/*.csv` files per mod and language |
| Scripts | define or override the game's native scripts from Lua |
| Tooling | editor completion and type checks (LuaLS `types/risen.d.lua`), virtual file system access, template list ([docs/TEMPLATES.md](docs/TEMPLATES.md)) |

Reference: [Lua API](docs/LUA_API.md) · [entity templates](docs/TEMPLATES.md) · [item properties](docs/ITEM_PROPERTIES.md) · [NPC properties](docs/NPC_PROPERTIES.md) · [engine notes](docs/RE.md) ·
[developer guide](docs/DEVELOPMENT.md) · [roadmap](docs/NEXT_STEPS.md). Examples: `mods/fetchquest` (a complete, savegame-safe
quest), `mods/stringsdemo`, `mods/_template`.

## Install

Download the release zip (or build it with `scripts\package.ps1`), then:

```powershell
.\install.ps1   -GameDir 'C:\...\Risen' -WithExamples
.\uninstall.ps1 -GameDir 'C:\...\Risen'
```

The proxy is installed as `bin\sentry.dll` (the original is kept as `sentry_orig.dll`; nothing else in the game folder changes).
Mods go to `bin\spass\mods\<mod>\` (each with a `mod.toml`), the log is `bin\spass\spass.log`.
To switch the layer off without uninstalling, create `bin\spass\config.toml` with `enabled = false`.

Open `bin\spass\mods` in VS Code with the *Lua* extension for completion and type checks; see [types/README.md](types/README.md).

## Build

Requirements: Visual Studio 2022/2026 (x64), CMake ≥ 3.25, [vcpkg](https://github.com/microsoft/vcpkg) (`VCPKG_ROOT` set).

```powershell
$env:VCPKG_ROOT = 'path\to\vcpkg'
cmake --preset default
cmake --build build --config RelWithDebInfo     # -> build\RelWithDebInfo\sentry.dll, build\RelWithDebInfo\spass_tests.exe
.\build\RelWithDebInfo\spass_tests.exe           # unit tests, no game needed
```

Developing and testing against a real game: see the [developer guide](docs/DEVELOPMENT.md) (`scripts/test-run.ps1`,
`scripts/smoke-test.ps1`, how to bind a new engine function, how to decompile).
