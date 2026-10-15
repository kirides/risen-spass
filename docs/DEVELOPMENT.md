# Developer guide

How the project is organised and how to extend it. (`CLAUDE.md` holds the same rules in the form an AI assistant needs.)

## Layout

| Path | Role |
|---|---|
| `src/proxy`, `src/bootstrap.cpp` | `sentry.dll` entry point (the game statically imports this crash-reporter DLL, we forward its exports to `sentry_orig.dll`), config, hook installation, Lua start after `gCScriptAdmin::PostInitialize` |
| `src/game/symbols.h` | the table of **every engine function** the layer calls or hooks (X-macro: field, module, decorated name, type, required) |
| `src/game/abi.h` | helpers for the MSVC-x64 conventions of the engine's classes (`bCString`, hidden return pointers, by-value parameters the callee destroys) and `guarded()` (SEH) |
| `src/game/events.cpp`, `hooks.cpp` | lifecycle hooks that feed the Lua events |
| `src/game/lua_*.cpp`, `bindings.cpp`, `loc_*.cpp`, `script_bridge.cpp` | the Lua API |
| `src/lua` | LuaJIT state, per-mod sandbox, events, mod discovery and load order |
| `types/risen.d.lua` | LuaLS definitions of the API — **must always match the bindings** |
| `mods/` | example mods; `*_driver`, `vfsprobe`, `templatedump`, `itemdump`, `persistdemo` are development tools that drive or probe the game |
| `tests/` | unit tests that run without the game |
| `tools/` | export dumping/demangling, Ghidra helper, doc generators, API consistency check |
| `docs/` | `LUA_API.md` (API), `RE.md` (findings per topic), `SYMBOLS.md`/`TEMPLATES.md`/`ITEM_PROPERTIES.md`/`NPC_PROPERTIES.md` (generated), `NEXT_STEPS.md` (roadmap) |

## Adding a Lua binding

1. Find the engine function (below) and add it to `src/game/symbols.h`. Mark it `required = false` unless nothing works without it.
2. Call it through `guarded([&]{ ... })` so a bad pointer or a game that is not ready returns `nil`/`false` instead of crashing. Validate arguments
   in Lua or C++ **before** touching the engine and return `nil, "reason"` for invalid input.
3. Register it (`set_function` on a table published as `risen["ns"]`, or a usertype member).
4. Describe it in `types/risen.d.lua` (parameters, return values, a sentence on what it does) and `docs/LUA_API.md`, then run
   `python tools/check_luadefs.py`. Type-check the example mods: `cd mods; lua-language-server --check . --configpath .luarc.json --checklevel Warning`.
5. Verify it in the running game (below), record what you learned in `docs/RE.md`, regenerate `docs/SYMBOLS.md`
   (`python tools/gen_symbols_doc.py > docs/SYMBOLS.md`).

## Finding and reading engine functions

Every Risen DLL exports its classes with decorated names (~36 000 symbols).

```powershell
python tools/dump_exports.py            # export tables -> %TEMP%\risen_re\<Dll>.exports.txt
python tools/demangle_exports.py        # readable names -> %TEMP%\risen_re\<Dll>.demangled.txt  (RVA, ordinal, signature)
```

Search the demangled files (`gCQuestManager::`, `PSInventory::`, ...), take the decorated name from the `.exports.txt` line. To read what a function
does, decompile it with headless Ghidra (`tools/ghidra/DumpDecomp.java`; the first import and analysis of a DLL takes 2–25 minutes, then a dump takes seconds):

```powershell
analyzeHeadless <projdir> <project> -process Game.dll -noanalysis -readOnly `
    -scriptPath tools\ghidra -postScript DumpDecomp.java out.txt 0x961f00 name:ReadSaveGame strref:"not found"
```

Conventions you need when reading the output (RE.md §6): `this` is the first argument; class return values come back through a hidden pointer;
by-value class parameters are passed as a pointer to a caller-made copy that the **callee destroys**; `PS*` wrappers hold an `eCEntity*` and fetch
property sets through the entity's vtable. The engine's own log function is an empty stub in release builds: failures are silent, so use
`risen.debug.trace_files`, `mods/eventlog`, and `risen.engine.missing()`.

## Testing

* **Unit tests** (no game): `build\RelWithDebInfo\spass_tests.exe` — string-file parser, mod discovery/order, Lua sandbox and events.
* **Test copy**: never test in the real install. Make a copy of `bin\` next to a junction to the real `data\`, with a private `data\ini` whose `ConfigLocal.xml`
  has `FullScreen="false"` (the game is always started windowed), and keep the stock `$(localappdata)/Risen/...` lines in its `mountlist_packed.ini`.
* `scripts\test-run.ps1 -Mods <mod>[,<mod>] -Seconds 120 -Filter '<regex>'` deploys the build and the chosen mods into the copy, starts a new game
  (`-Start load` quick-loads, `-NoAutostart` stays in the menu), and prints the matching log lines. `config.toml [debug] app_data` (set by the script)
  keeps savegames and `ConfigUser.xml` inside the copy.
* `scripts\smoke-test.ps1` replays the persistence and fetch-quest scenarios and asserts on the log (~6 minutes). Run it before committing changes to events, saving, quests,
  dialogues or items.
* Probe mods drive the game from `tick`: create a `mods/<name>` that logs what the engine reports, run it with `test-run.ps1`, read the log.
  `-Unsafe` lifts the Lua sandbox (to write fixture files), `-TraceFiles <text>` logs the engine's file opens from process start.

## Releases

`scripts\package.ps1` builds `dist\risen-spass-<version>.zip` (proxy DLL, installer, example mods, definitions, docs). The version is the one in `CMakeLists.txt`.

## Known limits

* Only Risen Enhanced x64 (build timestamps are logged by `risen.engine.build()`). Symbol resolution fails closed: a required symbol that cannot be found keeps
  the layer off, an optional one only disables its feature.
* The proxy DLL exports ~177 extra LuaJIT symbols (the static `lua51.lib` carries export directives); harmless, but not stripped yet.
* Entities created from Lua are not saved by the game; see `docs/RE.md` §14 and `mods/fetchquest` for the patterns that work.
