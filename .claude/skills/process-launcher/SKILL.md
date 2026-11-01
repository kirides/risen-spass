---
name: process-launcher
description: How to start, wait for, watch and stop processes on this machine without the usual mishaps (the game, the editor, test-run.ps1, python helpers). Use before launching Risen.exe, risen-modkit.exe or scripts\test-run.ps1, before waiting on a started process, and before killing anything.
---

# Working with processes here (short form)

## Never
- **Never kill by image name** (`taskkill /IM Risen.exe`, `Stop-Process -Name Risen`): the user may be playing the real game. Kill by **path** of the test copy:
  `Get-Process Risen | Where-Object { $_.Path -like 'B:\DevDrive\risen-test*\*' } | Stop-Process -Force` (parallel copies are `risen-test2`, ...).
- Never start the real installs (`B:\SteamLibrary\...`), never fullscreen, never the original `GenomeMutex` (tests use `[debug] instance`; `test-run.ps1` does it).
- Never poll with `sleep` loops in the foreground (blocked, and wastes turns).

## Starting
- **The game: always through `scripts\test-run.ps1`** (deploys, windowed, muted, background, watchdog, kills only its own copy). Never `Risen.exe` by hand.
  Scenario run: `-WaitForResult -Scenario x -Seconds 240`. Keep it alive for live probing: `-Link -Mods link_target -Seconds 400 -StallSeconds 300`.
- **Several values for an array parameter (`-Mods a,b`) must go through `-Command`**, not `-File` + `Start-Process -ArgumentList`
  (that hands over the single string `"a,b"` and the script fails with "mod 'a,b' not found"):
  `$cmd = "& .\scripts\test-run.ps1 -Link -Mods a,b -Seconds 400"; Start-Process pwsh -ArgumentList '-NoProfile','-Command',$cmd -RedirectStandardOutput out.txt -WindowStyle Hidden`
- Long-running thing -> **run it in the background** (`run_in_background: true`, or `Start-Process ... -WindowStyle Hidden`), then wait with a **Monitor** until-loop or the completion notification. Give every run a time limit; if it takes more than twice the expected time, kill it (by path) and read the log tail.
- The editor in screenshot mode exits by itself: `risen-modkit.exe proj.rmk.json --game-dir B:\DevDrive\risen-test --panel World --select @live --frames 900 --screenshot out.png`
  with `$p = Start-Process ... -PassThru -WindowStyle Hidden; $p.WaitForExit(150000)`; then read the PNG with the Read tool.

## Waiting and checking readiness
- **Wait for a fresh signal, not a stale one.** `spass.log` and `link.json` survive the previous run: delete them before starting
  (`Remove-Item ...\bin\spass\link.json, ...\spass.log`) or the wait fires at once on old content (it did: "game is in a new game" while the game had never started).
  Good signal: `link.json` exists AND `session: start (new)` is in the log, then `sleep 12`.
- Check the process really exists (`tasklist | grep Risen`, or `Get-Process Risen | Select Path`) and read `test-run.ps1`'s output file when something is silent.
- Default test mods include `persistdemo`, which cycles new game / load: for a **stable session use `-Mods link_target`** (or your own quiet mod).

## Talking to a running game
- `python -I tools\link_ask.py B:\DevDrive\risen-test\bin\spass <cmd> '<json args>'` (one request, reply printed). The pipe takes **one client at a time**:
  the editor's link worker holds it while connected, so do not open a second client from Play-like code while the editor is attached.
- Crash reports: `bin\spass\crash.log`; Windows' `Application Error` event log names the faulting module.

## Stopping
- Own copy only, by path (see Never). Scripts already kill stray `Risen.exe` of their own copy at start and end.
- Before parallel runs check free RAM (`Get-CimInstance Win32_OperatingSystem`) and kill a `steamwebhelper.exe` above 5 GiB.
