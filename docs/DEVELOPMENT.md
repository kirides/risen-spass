# Developer guide

How the project is organised and how to extend it. (`CLAUDE.md` holds the same rules in the form an AI assistant needs.)

## Layout

| Path | Role |
|---|---|
| `src/proxy`, `src/bootstrap.cpp` | `sentry.dll` entry point (the game statically imports this crash-reporter DLL, we forward its exports to `sentry_orig.dll`), config, hook installation, Lua start after `gCScriptAdmin::PostInitialize` |
| `src/game/symbols.h` | the table of **every engine function** the layer calls or hooks (X-macro: field, module, decorated name, type, required) |
| `src/game/abi.h` | helpers for the MSVC-x64 conventions of the engine's classes (`bCString`, hidden return pointers, by-value parameters the callee destroys) and `guarded()` (SEH) |
| `src/game/events.cpp`, `hooks.cpp` | lifecycle hooks that feed the Lua events |
| `src/game/inventory_events.cpp`, `entity_events.cpp`, `event_bus.cpp` | gameplay events (inventory, items, deaths, spawns): scoped hooks + a queue dispatched at the end of the frame (RE.md §27) |
| `src/game/lua_*.cpp`, `bindings.cpp`, `loc_*.cpp`, `script_bridge.cpp` | the Lua API |
| `src/game/lua_ai.cpp`, `ai_names.h`, `ai_lua.h` | the AI bridge (M11): Lua AI states and callbacks registered in the script registry's AIState / AICallback maps (thunk tables), the exit script behind `finish`, native perception interceptors for the `Assess*` scripts, `risen.ai.*`; `ai_lua.h` is the per-mod Lua part (`persist`) (RE.md §36) |
| `src/game/lua_combat.cpp`, `combat_names.h` | the damage hooks (M14): a native interceptor on the engine script `DoDamage` (via `scripts::installNative`), the handlers of `risen.combat.on_damage`, the pure classification / result / filter logic (`combat_names.h`, unit tests `tests/combat_tests.inc`; RE.md §39) |
| `src/game/lua_interact.cpp`, `lua_input.cpp`, `lua_hud.cpp`, `lua_config.cpp`, `call_trace.cpp` | focus/interaction info, keys and the `input.action` event, HUD log, per-mod `config.toml`, the `calls`/`hud` trace channels (RE.md §31) |
| `src/lua` | LuaJIT state, per-mod sandbox instances (torn down and run again by hot reload), events, mod discovery, the load plan (`mods.toml`: enabled flags, custom order, validation), version helpers |
| `src/game/lua_mods.cpp`, `migrate_lua.h` | `risen.mods` (list/reload/enable/order/orphans), `risen.migrate`, `risen.retire` (M15, RE.md §41); the registries tear down per owner through `Runtime::addTeardown` (`scripts::releaseOwner`, `aiReleaseOwner`, `combatReleaseOwner`) |
| `types/risen.d.lua` | LuaLS definitions of the API — **must always match the bindings** |
| `mods/` | example mods and the real mod `quickloot` ([QUICKLOOT.md](QUICKLOOT.md));  `*_driver`, `vfsprobe`, `templatedump`, `itemdump`, `npcdump`, `spelldump`, `recipedump`, `persistdemo`, `persistnpc` (the live test of `risen.npc.persistent`), `spawnwhen` (the live test of `risen.spawn.when`), `dialogcond` (the live test of dialogue conditions), `aistates` (the live test of `risen.ai`), `hardening` (the fuzz scenario, "Hardening" below) are development tools that drive or probe the game; `balance_example` is the showcase mod ([MODDING_GUIDE.md](MODDING_GUIDE.md)) |
| `tests/` | unit tests that run without the game |
| `tools/` | export dumping/demangling, Ghidra helpers (`ghidra/DumpDecomp.java`, `DumpVtable.java`), doc generators, API consistency check |
| `docs/` | `MODDING_GUIDE.md` (task-oriented guide for modders), `LUA_API.md` (API), `RE.md` (findings per topic), `SYMBOLS.md`/`TEMPLATES.md`/`ITEM_PROPERTIES.md`/`NPC_PROPERTIES.md`/`SPELL_PROPERTIES.md`/`RECIPE_PROPERTIES.md` (generated), `NEXT_STEPS.md` (roadmap) |

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
* **Test copy**: never test in the real install. Make a copy of `bin\` next to junctions to the real `data\common` and `data\compiled`, with a private `data\ini` whose `ConfigLocal.xml`
  has `FullScreen="false"` (the game is always started windowed), and keep the stock `$(localappdata)/Risen/...` lines in its `mountlist_packed.ini`.
  Further copies for parallel runs: see "Parallel instances" below.
  `data\extern` is a real directory of the copy, not a junction: `test-run.ps1` creates it with an **empty** `videos\logo.ini` (the file `PlayIntros` reads), so no intro video can ever
  add start-up time or block a run (the real install keeps its own `logo.ini`; its `.bik` files are renamed `.off`, so nothing plays there today either). The junction itself is
  removed with `rmdir`, the real install is never touched.
* `scripts\test-run.ps1 -Mods <mod>[,<mod>] -Seconds 120 -Filter '<regex>'` deploys the build and the chosen mods into the copy, starts a new game
  (`-Start load` quick-loads, `-NoAutostart` stays in the menu), and prints the matching log lines. `config.toml [debug] app_data` (set by the script)
  keeps savegames and `ConfigUser.xml` inside the copy.
* `scripts\smoke-test.ps1` runs every scenario (persistence, fetchquest, commands, branch, templates, balance, npcs, newtemplates, newassets, events, spells, `persistnpc` (persistent NPCs: four processes, save + in-process load + new game, fresh-process load, changed declaration, lost templates), `spawnwhen` (conditional spawning: four processes, quest / item / game variable / death triggers, saves before and after them, in-process and fresh-process loads, edited rules), `dialogcond` (dialogue conditions, M9: two processes, every condition kind against the engine after each world change, a fresh-process load), `routines` (NPC routines, M10: three processes, guard / day-night / patrol, save + in-process load, fresh-process load, changed declaration), `aistates` (Lua AI states, M11: three processes, begin / loop / finish, walking, perception, errors, a wolf bite, save + in-process load, fresh-process load, a removed mod) and the example scenarios (`quickloot` compares the standalone mod with the engine path in two processes, `recipes` and `ex_recipe` drive the real smithing anvil and the alchemy table; `ex_recipe` is four processes: save, load, load_missing, load_again; `ex_voice` plays the voice language English and then German; `spells` plays the player's real casts and then loads the save in a fresh process); each ends itself; the suite grows with every milestone: with M8 there were 28 scenarios (`-Parallel 4` took 737 s on 2026-10-16), with M9 there are 30 (`-Parallel 4` took 808 s, all passed), with M10 there are 32 (`-Parallel 4` took 869 s, all passed), with M11 there are 34 (`-Parallel 4` took about 940 s); the 8-scenario table in "Parallel instances" below is from before M5), prints a pass/fail table with timings and returns a
  nonzero exit code on failure: `-List`, `-Scenario templates,npcs`, `-FailFast`, `-TotalSeconds` (time budget), `-Parallel N` (N game instances at once, see "Parallel instances"). Run it before committing changes to events, saving,
  quests, dialogues or items. See "Testing and tracing" below.
* Probe mods drive the game from `tick`: create a `mods/<name>` that logs what the engine reports, run it with `test-run.ps1`, read the log.
  `-Unsafe` lifts the Lua sandbox (to write fixture files), `-TraceFiles <text>` logs the engine's file opens from process start, `-VoiceLanguage German` makes the game speak that voice language (writes `<Language VoiceLanguage=...>` into the copy's `ConfigUser.xml`; removed again on the next run without it).
  Files a mod makes at run time (`risen.assets.write`) live in `<Risen>\bin\spass\generated\<mod>`, which `test-run.ps1` does not clean: the `newassets` scenario relies on that for its second phase (`use`) and starts its first phase with `risen.assets.clear()`.

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
(`"all"`; `trace_file = true` also writes `trace.log`). Lines: `[12:31:09.513] [trace:events] session.start(new)`. Channels: `events` (every gameplay event the moment the hook queued it, with arguments, also when no mod listens), `template`, `balance`, `save`
(save/load phases with durations), `spawn`, `script`, `lua` (errors with traceback), `test`, `voice` (every sample title the engine asks for, with the answer; M5c), `ai` (Lua AI states: definitions, starts, begin / finish, every perception handler call; M11), `combat` (every hit a damage handler saw, with the damage before and after, cancelled hits, handler failures; M14), `calls` (the scripts, AI functions and AI states the ENGINE runs whose name contains one of the substrings of `-TraceScripts "^PreInteract,^Interact_,Unlock"` = `[debug] trace_scripts`; `^` = starts with; nothing without the list; M5d) and `hud` (every HUD game-log / item-log line, also the engine's own); mods add their own with `risen.trace("mymod", ...)`. Turn channels on
instead of adding temporary `log.info` lines and rerunning.

**Timings** (this machine, the same assertions): templates 53 s (was 240 s fixed), balance 53 s (240 s), npcs 61 s (300 s). The four older scenarios (persistence, fetchquest,
commands, branch) still assert on regexes over `spass.log` (`smoke-test.ps1` evaluates them after the run), but their driver mods end the run themselves: after the last step they call
`if risen.test.is_active() then risen.test.expect(...); risen.test.finish(...) end` (a no-op in a normal game), and `smoke-test.ps1` runs them with `-WaitForResult`. That removed the
90 s quiet tail the stall watchdog had to wait out: 170 s per scenario became persistence 47 s, fetchquest 94 s, commands 51 s, branch 95 s (M4b); the whole smoke test took 678 s (11.3 min) with templates 53, balance 53, npcs 75 and newtemplates 210 s (five processes). A new game loads in ~4 s here; the engine
start up to `session.start` is ~30 s, so a scenario is bounded by its own settle times (ticks), not by fixed waits. A new scenario should be a `risen.test` scenario from the start; a
driver that still wants log regexes only needs the `finish` call.
`-Start load` (quick-load the last quick save instead of a new game) skips the new game but needs a prepared savegame in the copy and cannot test new-game behaviour;
no clean-quit function was needed: `finish` ends the process itself (`TerminateProcess` after 1.5 s unless the script was faster).

**Scenarios over several game processes.** What a savegame does in a *fresh* process (templates created before/after/never, ...) needs one process per step: `smoke-test.ps1` scenarios may list `Phases`
(`@{ Name; Param = @('phase=load'); Start = 'load'; Keep = $true }`): each phase is a `test-run.ps1 -WaitForResult -Param phase=... -Start load -Keep` run of the same mods, the first one leaves the quick save the others load; the mods switch on `risen.test.param("phase")`
(`mods/newtemplates` + `newtemplates_driver`: `save`, `load`, `load_missing`, `load_again`, `load_late`, about 3.5 minutes in all). A phase of your own is run by hand like this:
`.\scripts\test-run.ps1 -Mods a,b -WaitForResult -Seconds 150 -Scenario name -Param phase=load -Start load -Keep` (`-Keep` keeps the savegames; call it from PowerShell, a `-File` call passes `-Mods a,b` as one string).

## Hardening: wrong arguments must never crash the game

A mod is user input. Whatever a Lua mod passes to the API, the answer is an error, `nil, reason` or `false`, never a crashed or hung game.

**Typed natives are checked.** A native bound with typed parameters (`const LuaEntity&`, `int`, `const std::string&`, `sol::table`, ...) used to read whatever
was on the Lua stack: `risen.npc.routine_points(5)` dereferenced a null `LuaEntity&` and killed the game, `int` took a table as 0 and a `std::string`
parameter took it as an empty string. The cause was not the natives but the sol2 configuration: with LuaJIT sol2 assumes that C++ exceptions can travel through Lua's frames
(`SOL_PROPAGATE_EXCEPTIONS`) and **skips the argument check** that `SOL_ALL_SAFETIES_ON` is meant to switch on. `CMakeLists.txt` defines `SOL_EXCEPTIONS_SAFE_PROPAGATION=0`:
every native now checks the type of every parameter before its body runs and raises a normal Lua error ("stack index 1, expected userdata, received number ...", catchable with
`pcall`). Consequences for the API: a `boolean` parameter wants a boolean (not 1), a number parameter a number (not the string `"5"`), an integer parameter a whole number;
`sol::optional<T>` parameters still treat a wrong type as absent. `tests/hardening_tests.inc` has a `#error` if the define is lost and a table of wrong values per parameter kind.
New natives may use typed parameters; take a `sol::object` and check it yourself when the failure should be `nil, reason` instead of an error.

**The fuzz scenario `hardening`** (`mods/hardening`, `smoke-test.ps1 -Scenario hardening`, about a minute): after a new game and a quick save + load (so that entity handles from before the
load are stale) it calls **every function it finds at run time** -- the `risen` table (the mod's view, including the per-mod functions) and the methods of the Entity, Info, Quest and
Template objects, ~440 in all, so a new binding is covered without touching the test -- under `pcall`, about 1.6 million times each run: with no argument, with each value of a battery
repeated 1..4 times, with each *object* (the player, an NPC, an item, a handle stale across a load, a handle to a world item the engine has since deleted, an Info, a Quest, a Template) in each
of the first positions combined with each battery value, and with random combinations (including spec tables with plausible keys and wrong-typed values). The battery holds nil, integers
and floats up to 1e300, NaN, +-inf, strings (empty, a 70000-character one, with a NUL, with `%` formats, `../..` paths, a template name), booleans, tables (empty, arrays, maps, cyclic, 3000 levels deep,
proxies whose `__index` / `__len` raise or return themselves), functions (one that raises), `risen` itself and `_G`. Then the same battery is used as the **return values of callbacks** (60 scripts defined with `risen.script.define` and called, 60 `risen.cond.fn` predicates, a `tick` handler, 24 AI states whose `begin` / `loop` / `finish` return garbage and are started on an NPC), because what a mod hands to the engine is input too. The scenario passes when afterwards the game still ticks, the player is still
there and an engine call (spawn) still works; the player is also checked after every function ("the player survived every function" names the one that removed him). A crash writes no result: the last
`[hardening] fuzz <function>` log line names the function, and

```powershell
.\scripts\test-run.ps1 -Mods hardening -WaitForResult -Seconds 400 -Scenario hardening -Param "only=^Entity%.attach$#v"
```

reruns just that one with every call logged before it is made (`only` is a list of Lua patterns separated by `;`, the suffix `#v` is `verbose`; `-Param seed=7`, `-Param random=300` change the random part).
Four instances in parallel (`-Instance 1..4`, each with another `only=`) take a minute for the whole API. **Not called** (listed in `EXCLUDED` at the top of `mods/hardening/main.lua`, each with its reason): `risen.world.new_game / quick_load / quick_save / auto_save`
(the world and the savegames of the test), `risen.off` (a number is a handler id: it would unhook the layer's own handlers; called with non-numbers), `risen.assets.clear`, `risen.log.error` (writes `[error]` lines on purpose),
all of `risen.test.*` (the harness) and every `<Type>.__gc` (the garbage collector's entry, not an API); the functions that write files or mount paths get every wrong *type* but no string. `Entity.kill` is not called with the player (it does what it says).
`risen.test.allow_errors(true)` keeps the error-level lines that bad specs produce on purpose out of the final "no errors logged" check.

**What the first runs found and what was done** (RE.md section 35 has the engine side):

| Crash / hang | Cause | Fix |
|---|---|---|
| any typed native called with the wrong type (`risen.npc.routine_points(5)`, `risen.item.count({})`, ...) | sol2 skipped the argument checks (above) | `SOL_EXCEPTIONS_SAFE_PROPAGATION=0` |
| a method on a handle to a deleted entity (`risen.item.pick_up` of a world item, then `item:is_item()`) -- freed memory | `LuaEntity` kept the raw pointer for the whole world generation | the `eCEntity::Kill` hook counts removals per address (`noteEntityRemoved`), a handle remembers the count: `ptr()` returns nullptr once its entity was removed, even when the engine reused the memory |
| `Entity:attach(child)` with the player, an NPC or an item as the child, or with a parent that is a descendant of the child | the scene tree was corrupted (access violation in the next frame) | refused (`false`) |
| `risen.world.remove_point(player)` | the "point" was removed through the engine's deferred removal: the world went down 40 frames later | only freepoints (`FP_*`), anchors (`Anchor_*`) and helpers (`Hlp_*`) are removed |
| `Entity:can_use()` / `skill_total()` / `requirements()` ... with no argument at all | `sol::variadic_args` after a fixed parameter has a negative size when the call had fewer arguments: the range loop ran for ever | the arguments are collected by index |
| `risen.script.call("")` or of a name that is no script | the engine hangs on an unknown script name | only registered scripts are called, otherwise an error |
| `risen.dialog.new_info(<70000 characters>, ...)` | the engine hangs on an endless info name | names are 1..200 characters |
| `risen.world.nav_point(x, y, z, NaN)` / infinite radius | the engine's search never ends | positions are finite numbers below 1e9, the radius is in (0, 20000] |

**Limits.** The scenario cannot tell a function that silently did the wrong thing from one that did nothing; it looks for crashes, hangs and a broken world. A slow function is capped at four seconds per function (`risen.balance.scale_npcs`, `risen.dialog.rebuild`,
`risen.template.census`, `risen.config` are cut off: they scan the whole template library, which is legitimate). A handle to an entity that was freed without passing `eCEntity::Kill` (the world is unloaded: handled by the world generation) is the one gap known.
Values that are right in type but absurd (an NPC with `hp_max = 1e300`) are the business of the individual validations, which the fuzz exercises with the same battery.

## One-process suites and hot reload (M15)

**Hot reload for development.** Set `[mods] watch = true` in `<Risen>\bin\spass\config.toml` (or call `risen.mods.watch(true)`): saving a `.lua`, `.toml` or `.csv` file of a mod reloads it in the running game (`docs/LUA_API.md` "Hot reload"); the log says what was released and what needs a restart. The live proof is the scenario
`hotreload` (`mods/hotreload`, `mods/hr_target`, `mods/hr_dependent`; needs the sandbox off because it edits files: `smoke-test.ps1` passes `-Unsafe`): three processes, `main` (edit + reload + failures + watcher + unload/disable/enable + a save round trip), `save_v1` and `load_v2` (a fresh process whose mod is the newer version, the fixture
`mods/hotreload/fixtures/v2` copied over it with `-CopyInto`).

**A faster suite: several scenarios in one game process.** `scripts\test-run.ps1 -Disable a,b,c` writes those mods as switched off into `spass\mods.toml` (deleted at the start of every run otherwise); `mods/testsuite` (parameter `suite=name:modA+modB,name2:modC`, optional `fresh=0`, `scenario_seconds`) then, per scenario: `risen.test.reset(name)`, `risen.mods.load` of its mods (they
are told the `session.start` / `world.ready` they missed, and `risen.test.wait_event("session.start")` answers from the lifecycle), waits for `risen.test.finish`, unloads them and starts a new game, so the next scenario meets an untouched world. `risen.test.suite_finish` writes one result whose checks are named `<scenario>: <check>` (an unfinished check or an aborted scenario is a failed check;
`test-run.ps1 -WaitForResult` and the "no errors logged" check work as before, per scenario). `smoke-test.ps1 -Scenario suite` runs testdemo, ex_balance, ex_template_patch and ex_custom_npc in one game: 77 s instead of ~4 minutes. **Limits:** edits of templates and other process-wide state stay (a scenario that asserts shipped numbers must run before one that changes them: `balance` after
`ex_balance` fails on a potion value), scenarios that test the start-up (first-tick timing, `mods.loaded`) or that need a fresh process (fresh-process loads, `newtemplates`) cannot join; a suite needs the scenarios' mods to be loadable late (they get the lifecycle replay, not `mods.loaded` before the game).

**Updates and removals.** The scenario `updsafe` (`mods/upd_content` in several versions chosen by `-Param variant=`, `mods/upd_check` the scenario; sandbox off: it copies the savegame files) plays an old version, keeps a pristine copy of the savegame and loads it, each case in its own fresh process, with the same version, an update that declares nothing, a renamed quest line,
an update with `risen.retire`, a lost sidecar, and without the mod (with and without its sidecar). About 7 minutes serial; the phases cannot run in parallel (they share the pristine copy).

## The GUI, screenshots and the mod manager scenarios (M16)

**Look at it.** `risen.debug.screenshot("name" [, wait_frames])` writes `<Risen>\bin\spass\screenshots\name.png` (in the test copy `B:\DevDrive\risen-test\bin\spass\screenshots`): the layer hooks `IDirect3DDevice9::Present` on the first request and copies the back buffer (docs/RE.md §42). A scenario can take pictures at the points that matter (`mods/mm_driver` does) and a developer opens the PNGs; the old "screenshots are black, nothing about appearance is
verified" note is obsolete for anything drawn by the game's GUI or 3D renderer. The PNG is uncompressed (about 4 MB at 1600 x 900): delete old ones.

**Drive the menu through the engine's input path.** `risen.gui.key(0x1B)` is ESC as the window procedure delivers it, `w:click()` / `risen.gui.click(x, y)` a mouse click, `risen.gui.page(id)` the window tree; while a menu page is open the world is paused (no `tick`) but `ui.frame` fires and the layer keeps `risen.test` waits (`sleep`, `wait_until`) running, so a scenario reads top to bottom as usual. The test start leaves the
"Press START" page (26) open: close it (`risen.gui.close(26)`) before ESC, and wait for the loading page (29) to go. Never resize the window: the GUI and the mouse misalign (the scripts write 1600 x 900 into the copy's `ConfigUser.xml` before the start).

**Scenarios.** `gui` (`mods/gui_test`, 49 checks, ~50 s, needs `-Unsafe`: it reads the screenshot file): pages and windows, reflection, clone, a Lua script as a button handler, ESC, a click on a stock and on a cloned tab, a leaf page with `ui.page`, the screenshot, refusals of bad arguments. `modmanager` (`mods/mm_driver` + the fixture mods `mm_base`, `mm_ext` (requires base), `mm_broken` (raises an error: `failed`),
`mm_skipped` (requires a mod that is not installed), `mm_data` (a persistent NPC and a saved value), and the mod under test `modmanager`; 3 processes, ~2 minutes): `main` (74 checks: the tab, ESC, the click on the tab, the list and details, reload, the order refused / accepted / reset, the cascade questions, the data question, paging, a hot reload and an unload of the manager, closing, quick save + load), `restart` (a fresh process that keeps `mods.toml`:
`test-run.ps1 -KeepModsToml`, the phase attribute `KeepModsToml` of `smoke-test.ps1`), `menu` (the main menu without a game: the phase attribute `NoAutostart`). The fixture mods fail and skip on purpose: the driver calls `risen.test.reset("modmanager")` first so that the errors the layer logged while loading them do not count towards "no errors logged".
The logic of the page is a plain Lua module (`mods/modmanager/model.lua`) with unit tests against a fake `risen.mods` (`tests/modmanager_tests.inc`, 34 checks): navigation, paging, details, refusals with the host's reason, cascades, the data question, a failing host.

## Hardening: natives must not run metamethods
A native that is handed a hostile table must read it with raw iteration only. `sol::table::size()` / `lua_len` runs `__len`; an error raised there is thrown through the native's C++ frame and, in the hardening run, corrupted a registry reference that the harness held (the symptom came much later and somewhere else: "attempt to call a number value" in a `wait_ticks`
callback). If a hardening run fails like that, `-Param "only=risen%.[a-l];risen%.mods"` style subsets plus `#v` (verbose) find the call (RE.md §41).

## Crashes: banner, minidump, exit codes, stress loop (M17)

A game that dies used to leave "exited, no result" and a log that just stops. Now (`src/common/crash.*`, always on unless `[debug] crash_handler = false`):

* **Banner.** A fault outside `guarded()` (vectored handler) and any unhandled exception (top-level filter) writes `spass\crash.log` (complete) and a short form into `spass.log`: exception name and code, access kind and target address, faulting address as `Module+RVA` (`sentry.dll[layer]` is the proxy, `sentry_orig.dll` the game's renamed crash reporter), the thread (game thread or not), **what the layer was doing** (breadcrumbs: Lua event handlers `tick crashprobe`, lifecycle detours `session.start load`, `guarded file.cpp:line` engine calls), registers, the stack unwound with the PE unwind tables, a scan of the stack for return addresses (for a smashed stack or jitted code), the bytes at the instruction pointer, and a verdict "in the layer / in the engine, reached through the layer / engine only". Faults that `guarded()` catches are logged as `guarded call #n faulted: ...` (a swallowed access violation is the early warning of a race). `ExitProcess` / `TerminateProcess` of the own process are logged with their callers. A fast fail (`__fastfail`, exit code `0xC0000409`) cannot be intercepted by any handler: only the exit code shows it.
* **Minidump.** `[debug] crash_dump = "mini"` (the default of `scripts/test-run.ps1`, `-CrashDump off|mini|full`) writes `bin\spass\crash\crash-<time>-pid<pid>.dmp` (~14 MB; `full` is all memory, 1-2 GB). Open it in Visual Studio / WinDbg (modules, all thread stacks, registers). cdb/WinDbg are not installed here; the banner is usually enough: look the `Module+RVA` up in `%TEMP%\risen_re\<Dll>.demangled.txt` (nearest export below the RVA) and decompile it (`docs/RE.md`, the `ghidra-decompiler` agent). Before any of this existed, the Windows `Application Error` events (`Get-WinEvent -FilterHashtable @{LogName='Application'; ProviderName='Application Error'}`) already named faulting module and offset of every crash, and `C:\ProgramData\Microsoft\Windows\WER\ReportArchive\AppCrash_Risen.exe_*\Report.wer` names the test copy: start there for a crash from before the handler (RE.md section 43 did).
* **Test scripts.** `run-windowed.ps1` records the exit code of a game that ended by itself; `test-run.ps1` prints `crashed (exit code 0xC0000005 ACCESS_VIOLATION): CRASH (...) ...; dump <path>` and the banner, sets `$global:SpassRun.Crashed / ExitCode / Banner / Dumps / Summary` and deletes `crash.log` and the dumps at the start of the next run (`-KeepCrash` keeps them); `smoke-test.ps1` puts the summary into the scenario's note instead of "the game exited without a result".
* **Prove it works.** `mods/crashprobe` crashes the game on purpose (sandbox off): `.\scripts\test-run.ps1 -Mods crashprobe -Unsafe -WaitForResult -Seconds 150 -Scenario crashprobe -Param kind=av` (`kind=av` write to 0x18, `exec` call of address 0x10 = the DEP fault of the `ntdll+0x161914` sightings, `exit` ExitProcess(3); `when=session.start` instead of the first tick). Unit tests: `tests/crash_tests.inc` (breadcrumbs, `guarded()` call site and fault counter, banner and minidump of a fault).

**Hunting a rare crash: `scripts\load-stress.ps1`.** Replays a fresh-process savegame load over and over, in `-Parallel 1..4` game instances plus `-Burn n` CPU burner processes, and counts `pass / FAIL / CRASH / EXITED / NORESULT`; everything about a bad iteration (spass.log, crash.log, dump, test-run output) is copied to `B:\DevDrive\risen-stress\<stamp>\bad-<worker>-<n>`. Presets: `showcase` (the whole scenario, ~65 s per load under load), `lean` (the scenario ends 6 s after the world settled, ~46 s), `loop` (one fresh process, then 40 in-process quick loads at random spacing), `newtemplates` (`load_late`). It plays the save phase first (~140 s; `-SkipPrepare` when the copies still hold the savegame of an earlier run) and needs the four test copies to itself. A typical session: `.\scripts\load-stress.ps1 -Preset lean -Iterations 100 -Parallel 4 -Burn 6 -MaxMinutes 30` (about 25 minutes). Statistical power is low for a rare event: 100 loads without a crash only say "less often than about 1 in 30" with 95 % confidence.

**Engine guards and the race probe.** `[debug] engine_guards = "all" | "findroom" | "none"` (`test-run.ps1 -EngineGuards`) selects the guards of `src/game/engine_guards.cpp` (RE.md section 43: the rich-presence thread's world access is answered from a cache of the game thread, `FindPortalRoom` has a fault guard); `"none"` is the engine as shipped. `[debug] race_probe = true` (`-RaceProbe`) starts a thread that calls the same engine function in a tight loop: a test tool to provoke that class of race (it did not crash the unguarded engine in 40 million calls; the race needs one exact interleaving).

## Parallel instances (M4c)

Several `Risen.exe` can run at the same time when each has its **own test copy** and its own id. The engine's single-instance guard is the named mutex `GenomeMutex`
(`SharedBase.dll` `g_CreateMutex`: `CreateMutexA(NULL, TRUE, name)`, a second process gets `ERROR_ALREADY_EXISTS` (0xB7) and exits within a second, confirmed). `config.toml [debug] instance = "<id>"`
(`src/game/instance_mutex.cpp`, hooks `CreateMutexA/W` and `OpenMutexA/W` before the engine runs) renames it to `GenomeMutex_<id>`. Nothing else is shared that matters: the packed archives open with
read sharing (two games read the same `data\common` / `data\compiled` through junctions), Steam is not involved because `SteamAppId` is set (no restart, no dialog), and the savegames, `ConfigUser.xml`,
`achievements.bin`, `.sentry-native` and `spass.log` live inside each copy (`[debug] app_data`, `bin\spass`). A parallel instance behaves exactly like a solo run (testdemo 37 s alone and two at once).

* `test-run.ps1 -Instance <n>`: 1 is the main copy `B:\DevDrive\risen-test`, n >= 2 is `B:\DevDrive\risen-test<n>`, **created on first use** from the main copy (`robocopy` of `bin` without `spass`, 60 MB; `data\ini` copied;
  `data\common` and `data\compiled` as junctions to the same targets as the main copy; the main copy's `ConfigUser.xml` as seed; `data\extern\videos` with the empty `logo.ini` as always). Delete the directory
  (`Remove-Item -Recurse` after `rmdir` of the two junctions, or `cmd /c rmdir`) to drop a copy; they are rebuilt on demand. The script writes `instance = "<n>"`, places the window at an offset of 40 px per
  instance, and **only kills the `Risen.exe` of its own copy** (matched by path; without `-Instance` it is the main copy only, other copies and the real install are never touched).
* `smoke-test.ps1 -Parallel N`: a work queue over the instances 1..N; every scenario runs as a child `smoke-test.ps1 -Scenario x -Instance k` (own watchdog, own log), the longest first (`Est` in the scenario table), a free
  instance takes the next one. The output of a passed scenario is one line, of a failed one the complete child output (`-VerboseChildren` prints all). Exit code and table as before plus the instance column; `-FailFast`
  stops launching, a child that exceeds its limit plus 180 s is killed together with its game, and Ctrl-C leaves no game behind.
* Measured on this machine (Ryzen 7 5700X3D 8c/16t, 64 GB, RX 7900 XTX, Dev Drive), all 8 scenarios, 678 s was the serial time before:

| `-Parallel` | whole smoke | newtemplates (critical path) | result |
|---|---|---|---|
| 1 | 650 s | 204 s | 8/8 pass |
| 2 | 355 s | 220 s | 8/8 pass |
| 3 | 271 s | 249 s | 8/8 pass |
| 4 | 235 s (twice) | 234 s | 8/8 pass |

  The four games used ~4.2 GB RAM together and at most ~59 % GPU (3D engine); every check passed in every run, the per-scenario times grow by about 5-20 % at 3-4 instances. `newtemplates` (save, load,
  load_missing, load_again, load_late) is a chain (`load_missing` saves, `load_again` loads that save), so it cannot be split further and bounds the run at ~3.5-4 minutes; beyond `-Parallel 4` nothing is gained.
  **Recommended: `-Parallel 3`** (4.5 minutes, the machine stays usable) or 4 for the fastest run. Each game starts as a 1600x900 window (`test-run.ps1 -WindowSize`, written into the copy's `ConfigUser.xml` before the start) and is never resized at runtime (a resize misaligns the UI and the mouse; `run-windowed.ps1 -Resize` exists but is not used); windows may overlap. The machine has 64 GB RAM and 24 GB VRAM, so 4 instances are no problem.

## Startup time (M4b measurements)

`scripts\measure-startup.ps1 -Variant baseline,noaudio,... -Repeat 2` runs `mods/startupprobe` (a `risen.test` scenario that logs `[probe] ... game_fps=`) once per variant and repetition, strictly one game at a time and
alternating the variants (A B A B), and prints the times in seconds since the layer loaded (about the process start). `test-run.ps1 -ExtraArgs '-noaudio','-res=640x360'` passes switches to `Risen.exe`.
Where the ~30 s to `session.start(new)` go (file trace `-TraceFiles .` plus CPU sampling): ~1.6 s exe, DLLs and VFS; ~13 s in the **AMD D3D9 driver** (`\.\AmdLog`, `AMD\DxcCache\*.parc` shader cache
reads/writes, `amdsc.ini`; CPU is below one core there, the file reads of the whole start are only ~100 MB, so it is not disk bound); then ~2 s config and achievements, a burst of ~40 CPU-seconds over ~3 s on many
threads (data loading), ~6 s menu world (`G3_Startup`), ~3.5 s for the new game. `gCScriptAdmin::PostInitialize` returns at ~19.5 s, the first `tick` is at ~25.6 s, `session.start(new)` at ~29.3 s.

| Variant (2 alternating runs each, 4 for the baseline) | session.start | result of the probe | game ticks/s | verdict |
|---|---|---|---|---|
| baseline (2560x1440 window override in ConfigUser.xml, ConfigLocal 1280x720 window, audio on) | 29.3-32.6 (mean 30.4) | 40.4-44.4 | 79.4-79.8 | |
| `-noaudio` | 29.0-30.3, earlier pair 29.2-29.3 | 40.7-42.4 | 79.5-79.8 | no gain (a first pair seemed 3 s faster, the interleaved repeat did not confirm it) |
| `-res=640x360` | 30.3, 30.3 | 42.4 | 79.4-79.5 | no gain |
| `<Window>` override removed from ConfigUser.xml | 30.1, 30.2 | 41.8-42.4 | 79.3-79.6 | no gain |
| `<Timer MaxFPS="1000" MinFPS="2"/>` in ConfigLocal.xml | 30.0, 32.0 | 41.9-44.0 | 79.2-79.7 | no gain |
| `<Timer MaxFPS="30"/>` in ConfigLocal.xml or ConfigUser.xml (control) | 28.7, 29.1 | 39.9 | 79.8 | the key is **not honoured**: the tick rate does not change |
| low graphics (ViewDistance 1, all `Quality="Low"` in ConfigUser.xml) | 28.7-33.5 | 40.3-45.6 | 79.7-79.8 | no gain |

The run-to-run noise (about +-2 s, one variant ranged 28.7-33.5 s) is larger than every effect, so **nothing was kept** except the private videos directory (it removes a risk, not time). The frame rate is
~80 `tick`s per second whatever the resolution, window or Timer keys (`gCSession::Process`, once per frame): a tick-based `T.sleep(1200)` costs ~15 s; shortening it means either fewer ticks (risky: the engine needs the
frames to settle) or the turbo work of M4d (time scale + reduced rendering). `run-windowed.ps1` polls every 0.1 s (was 0.5 s) for the result file, the exit and the watchdog.

**Do not end a run right after a save.** With the 0.1 s poll the game was killed so soon after `save.after` that the savegame written by `newtemplates/load_missing` could not be loaded any more (the next two phases
hung without `session.start(load)`; 0.5 s happened to be long enough). The engine keeps writing after `save.after`; a scenario that saves as its last step must `T.sleep(200)` (~2.5 s) before it finishes (`newtemplates_driver` does).

## Releases

`scripts\package.ps1` builds `dist\risen-spass-<version>.zip` (proxy DLL, installer, example mods, definitions, docs). The version is the one in `CMakeLists.txt`.

## Known limits

* Only Risen Enhanced x64 (build timestamps are logged by `risen.engine.build()`). Symbol resolution fails closed: a required symbol that cannot be found keeps
  the layer off, an optional one only disables its feature.
* The proxy DLL exports ~177 extra LuaJIT symbols (the static `lua51.lib` carries export directives); harmless, but not stripped yet.
* Entities created from Lua are saved by the game but lose their unique name; `risen.npc.persistent` (docs/LUA_API.md, `docs/RE.md` §32, `mods/fetchquest`) is the pattern that works.
