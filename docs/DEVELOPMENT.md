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
* `scripts\smoke-test.ps1` runs every scenario (persistence, fetchquest, commands, branch, templates, balance, npcs), prints a pass/fail table with timings and returns a
  nonzero exit code on failure: `-List`, `-Scenario templates,npcs`, `-FailFast`, `-TotalSeconds` (time budget). Run it before committing changes to events, saving,
  quests, dialogues or items. See "Testing and tracing" below.
* Probe mods drive the game from `tick`: create a `mods/<name>` that logs what the engine reports, run it with `test-run.ps1`, read the log.
  `-Unsafe` lifts the Lua sandbox (to write fixture files), `-TraceFiles <text>` logs the engine's file opens from process start.

## Testing and tracing

**Write a scenario with `risen.test`** (`mods/testdemo` is the smallest one). A scenario mod plans its checks, runs as a coroutine and records them; the mod
layer writes a result file and ends the game the moment the scenario is done, so a run lasts as long as the scenario, not a fixed number of seconds:

```lua
local T = risen.test
T.plan { "player exists" }                                  -- optional: a check that never ran fails the run
T.run(function()
    T.wait_event("session.start", 180, function(m) return m == "new" end)   -- the autostart helper starts the new game on the first tick
    T.sleep(150)                                            -- frames
    T.expect("player exists", risen.entity.player() ~= nil, "detail")
    T.eq("hit points", risen.entity.player():hp(), 80)      -- "expected 80, got 70" on a mismatch
end, "my_scenario")                                         -- returning finishes the run
```
```powershell
.\scripts\test-run.ps1 -Mods mymod -WaitForResult -Seconds 240 -Scenario my_scenario   # prints the failed checks, exit code 0 = pass, 1 = failed, 2 = no result
#   -ShowChecks  every check, not only the failed ones     -Param key=value  -> risen.test.param("key")
#   -StallSeconds 90  watchdog: no new log line for that long = kill      -DeadlineSeconds  the layer's own deadline (default -Seconds minus 10)
```
Requests like `risen.world.new_game()/quick_save()/quick_load()` are performed by the engine on its next step: register the waiting handler (or use a flag set by an
`risen.on` handler) *before* the request, or use `T.wait_event` right after it (the event cannot fire before the coroutine yields).
Several mods may record into one run (the migrated smoke scenarios do: a mod verifies, its `_driver` plays the save/load/new-game steps and finishes);
list every check name in `T.plan` so nothing is silently skipped. Use a gamevar as a cross-mod flag (`risen.gamevar.set("X_DONE", 1)`).

**Result file** `<Risen>\bin\spass\test-result.json`: `scenario`, `status` (`pass`|`fail`), `reason`, `summary`, `elapsed_s`, `section` (where it ended), `marks`
(`first_tick`, `session.start`: seconds since the layer started), `counts {total, passed, failed}`, `unfinished` (planned checks that never ran, waits still pending),
`checks[]` (`name`, `ok`, `detail`, `section`, `t`). It is written atomically (the script never sees half a file). `scripts\test-run.ps1` leaves the parsed result in
`$global:SpassRun` (`Reason`, `Seconds`, `Result`, `Timing`, `Verdict`).

**Watchdogs.** (1) Layer: `[test] deadline_s` (set by `test-run.ps1`) - a thread writes a FAIL `timeout` result, listing the unfinished checks and the section, even when the game
thread is stuck in an endless Lua loop. (2) Script: `test-run.ps1` kills the game when `spass.log` gets no new line for `-StallSeconds` (default 90; the engine start and a new game
are quiet for up to ~20-30 s here, the layer logs a `[test] alive` heartbeat every 15 s while a scenario ticks) or when `-Seconds` ran out, prints the last 25 log lines and
"where it stopped" (phase, last section/check/heartbeat/trace/error). `Risen.exe` is killed at the start and in a `finally` at the end (also on Ctrl-C). The window code never
blocks on a hung game (`SetWindowPos` is asynchronous). Failure drills: `-Mods testdemo -Param mode=fail|never|hang`.

**Trace channels** (opt-in, zero cost when off): `.\scripts\test-run.ps1 -Trace template,balance,save,events [-TraceFile]` or `config.toml [debug] trace = "..."`
(`"all"`; `trace_file = true` also writes `trace.log`). Lines: `[12:31:09.513] [trace:events] session.start(new)`. Channels: `events`, `template`, `balance`, `save`
(save/load phases with durations), `spawn`, `script`, `lua` (errors with traceback), `test`; mods add their own with `risen.trace("mymod", ...)`. Turn channels on
instead of adding temporary `log.info` lines and rerunning.

**Timings** (this machine, the same assertions): templates 53 s (was 240 s fixed), balance 53 s (240 s), npcs 61 s (300 s); the whole smoke test 14 min instead of ~28 min (the four log-regex scenarios also end 30-100 s early when the stall watchdog sees their quiet tail). A new game loads in ~5 s here; the engine
start up to `session.start` is ~30 s, so a scenario is bounded by its own settle times (ticks), not by fixed waits. The legacy log-regex scenarios (persistence,
fetchquest, commands, branch) still run for a fixed time; migrating one means moving its `driver:` lines into `risen.test` checks as done for the other three.
`-Start load` (quick-load the last quick save instead of a new game) skips the new game but needs a prepared savegame in the copy and cannot test new-game behaviour;
no clean-quit function was needed: `finish` ends the process itself (`TerminateProcess` after 1.5 s unless the script was faster).

## Releases

`scripts\package.ps1` builds `dist\risen-spass-<version>.zip` (proxy DLL, installer, example mods, definitions, docs). The version is the one in `CMakeLists.txt`.

## Known limits

* Only Risen Enhanced x64 (build timestamps are logged by `risen.engine.build()`). Symbol resolution fails closed: a required symbol that cannot be found keeps
  the layer off, an optional one only disables its feature.
* The proxy DLL exports ~177 extra LuaJIT symbols (the static `lua51.lib` carries export directives); harmless, but not stripped yet.
* Entities created from Lua are not saved by the game; see `docs/RE.md` §14 and `mods/fetchquest` for the patterns that work.
