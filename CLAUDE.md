# risen-spass – guidance for Claude

A hook-based **Lua (LuaJIT) modding layer for Risen Enhanced (x64)**: a proxy `sentry.dll` is loaded by `Risen.exe`, binds the
game's exported C++ API by decorated name, hooks lifecycle functions and runs mods from `<Risen>\bin\spass\mods`.
Docs: `docs/RE.md` (reverse-engineering findings, by section), `docs/LUA_API.md` (API), `docs/NEXT_STEPS.md` (roadmap),
`docs/TEMPLATES.md` (entity template names), `types/README.md` (editor support).

## Hard rules

1. **`types/risen.d.lua` must always match the Lua API.** Every Lua-visible function, usertype method/property, option table,
   event and `risen.*` namespace is described there for LuaLS (`---@param`, `---@return`, one sentence on what it does and any
   gotcha). When you add, rename, remove or change the behaviour/return values of a binding (C++ `set_function`, usertype member,
   or a Lua chunk like `function risen.dialog.create`), update `types/risen.d.lua` **in the same commit**, then run
   `python tools/check_luadefs.py` (fails when the two sides differ) and type-check the example mods:
   `cd mods && lua-language-server --check . --configpath .luarc.json --checklevel Warning`. New events go into the `risen.on`
   overloads and the events table in `docs/LUA_API.md`.
2. **Never touch the real game install** (`B:\SteamLibrary\steamapps\common\Risen`). Test only in the isolated copy
   `B:\DevDrive\risen-test`, **always windowed** (the user does not want a fullscreen game taking over the screen).
3. **Commit at milestones**: small, reviewable commits once a feature is built *and* verified (live in the game where it
   touches the engine). Do not let changes pile up. Commit trailers come from the session's attribution reminder.
4. Verify engine behaviour in the running game instead of assuming. Record findings (RVAs, offsets, what is confirmed vs.
   inferred) in `docs/RE.md`, tick off / extend `docs/NEXT_STEPS.md`.

## Build

```powershell
$env:VCPKG_ROOT = 'E:\git\github\vcpkg'
cmake --preset default                                  # only after CMakeLists.txt changed (new .cpp files!)
cmake --build build --config RelWithDebInfo             # -> build\RelWithDebInfo\sentry.dll
```
C++20, MSVC, vcpkg manifest (`vcpkg.json`: spdlog, minhook, sol2, tomlplusplus, luajit). New source files must be added to
`CMakeLists.txt`. The editor's clang diagnostics ("file not found", "undeclared identifier sol") are noise: only the MSBuild
result counts. If you replace a header's content with a file copy, `touch` it: a preserved old mtime makes MSBuild skip
dependent files and the `Symbols` struct layout then differs between translation units (silent breakage).

## Unit tests (no game needed)

`build\RelWithDebInfo\spass_tests.exe` (or `ctest -C RelWithDebInfo --test-dir build`): string-file parser, mod discovery/load
order, the Lua runtime (sandbox, events, `require`, broken mods). Add a `TEST(name)` to `tests/test_main.cpp` for new pure logic.
A `sol::object` returned from `Runtime::emit*` must not outlive `Runtime::stop()`.

## Test loop

`scripts/test-run.ps1` deploys the build and chosen example mods into `B:\DevDrive\risen-test`, writes its `config.toml`
(`[debug] app_data` keeps savegames/`ConfigUser.xml` inside the copy), launches the game windowed and prints filtered log lines:

```powershell
.\scripts\test-run.ps1 -Mods persistdemo -Seconds 160 -Filter 'persistdemo|\[error\]|\[warn\]'
# -Start load   quick-loads the last quick save instead of starting a new game   -Keep  keep log+saves
# -NoAutostart  stay at the main menu (the `tick` event then fires only once)
```
The log is `B:\DevDrive\risen-test\bin\spass\spass.log`. `scripts\smoke-test.ps1` replays the persistence and fetch-quest scenarios
and asserts on the log (run it before committing changes that touch events, saving, quests, dialogues or items; ~6 minutes). Kill stray `Risen.exe` processes before a run. A new game takes
~60 s to load, so give runs 90-300 s. The tick-driven demos in `mods/` show how to script a whole scenario unattended.
`Risen.exe` calls `SteamAPI_RestartAppIfNecessary`; `run-windowed.ps1` sets `SteamAppId` to prevent a relaunch of the real
install. Screenshots of the D3D9 window are black: verify through engine state and log lines.

## Architecture (src/)

| Path | Role |
|---|---|
| `proxy/`, `bootstrap.cpp` | DllMain entry, config (`config.toml`), install hooks, start Lua after `gCScriptAdmin::PostInitialize` |
| `game/symbols.h` | **X-macro table of every engine function** (module, decorated name, pointer type, required). Add engine calls here |
| `game/abi.h` | `BString`, `ByValueString`, `OutString`, `EntityHolder`, `guarded()` (SEH) – the MSVC-x64 conventions, see RE.md §6 |
| `game/hooks.cpp`, `events.cpp` | lifecycle hooks -> Lua events (`session.*`, `save.*`, `load.*`, `quest.status_changed`, ...) |
| `game/lua_*.cpp`, `bindings.cpp`, `loc_overlay.cpp`, `script_bridge.cpp` | the Lua API (`risen.*`) |
| `lua/runtime.*`, `lua/mods.*` | LuaJIT state, sandboxed per-mod environments, event dispatch, mod discovery/load order |
| `hook/` | MinHook wrapper (`Hook<Fn>`), export resolution by name |
| `types/risen.d.lua` | LuaLS definitions (see rule 1) |

Conventions: every engine call from Lua goes through `guarded(...)` and returns `nil`/`false` on failure instead of crashing;
Lua-facing validation happens before the engine is touched and returns `nil, "reason"`; native entity handles are `LuaEntity`
(use `ptr(h)` to get the pointer, never `h.e`); optional symbols (`required = false`) only disable their own feature.
Helper chunks written in Lua live inside the `.cpp` file next to the binding (`const char* chunk = R"LUA(...)LUA"`).

## Reverse engineering

- Every Risen DLL exports its classes with decorated names (~36 000 symbols): look functions up in
  `%TEMP%\risen_re\<Dll>.demangled.txt` (RVA, ordinal, name), regenerate with `tools/dump_exports.py` +
  `tools/demangle_exports.py`. Bind with `GetProcAddress(decorated name)`, not addresses.
- Decompile with headless Ghidra (projects in `B:\DevDrive\risen-ghidra`, Ghidra at `B:\D\ghidra_12.0_PUBLIC`, JDK 21):
  use the **`ghidra-decompiler` subagent** (`.claude/agents/ghidra-decompiler.md`) or `tools/ghidra/DumpDecomp.java`.
  The Ghidra MCP server only has `Risen.exe` loaded.
- The engine's log function is an empty stub in this build; failures are silent. Use `risen.debug.trace_files`, the event log
  (`mods/eventlog`), `risen.engine.missing()` and decompilation to find out why something returns `false`.
- Never probe enum values blindly (e.g. info condition types): quest-state conditions without a quest open a blocking
  message box. Use names (`gEInfoCondType_Running`) through the property-by-name mechanism.

## Verified facts worth remembering (details in docs/RE.md)

- Scripts are native functions in a registry; Lua scripts are registered/overridden by swapping the function slot.
- Runtime-spawned entities are **not** saved; quests/infos must exist before the engine reads a savegame
  (`load.before`), a new game removes runtime quests; mod data persists through `risen.save` sidecar files.
- The test copy's `mountlist_packed.ini` must use the stock `$(localappdata)/Risen/...` lines; absolute paths there make all
  saves fail silently. `$(localappdata)` is expanded via `SHGetFolderPathA`, not the environment variable.
- Entity templates are editable at runtime (`risen.template.get/patch`, RE.md §22): apply on the first `tick` (not `mods.loaded`) and again with `instances = true` on
  `session.start`; existing entities and savegame/new-game worlds keep their own property copies, inventory stacks and new spawns follow the template.
- `risen.vfs` lists/reads the packed archives through the engine's own VFS; template names come from
  `data/common/templates/**/*.tple`.

## Tooling gotchas (this machine)

- Windows, files in the working tree are CRLF (`autocrlf`); Python text-mode edits convert line endings, which is fine.
- In the Bash tool, long multi-line heredocs containing quotes/backslashes can fail or lose backslashes: write scripts with the
  Write tool and run them, or use Edit for files with backslashes.
- `pefile` needs `max_symbol_exports` for the big DLLs or export names beyond 8192 are lost.
