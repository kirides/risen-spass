---
name: decompile
description: How to decompile functions of the Risen binaries (Game.dll, Engine.dll, Script.dll, Script_Game.dll, FFC.dll, SharedBase.dll, FileSystem2.dll) with headless Ghidra and read the result. Use it when you need to know what an engine function or a game script (CanInteract_*, AutoLoot, ZS_*...) really does, which fields it touches or who calls it.
---

# Decompile Risen code with headless Ghidra

Answers "what does `<function>` do?" from the real binaries instead of guessing. Everything is **read-only**: never modify
the game install or the Ghidra projects, never delete lock files, never re-import a DLL (2-25 min, the projects are done).
The Ghidra MCP server (`mcp__ghidra-mcp-risen__*`) has only `Risen.exe` open: for the DLLs use the headless route below.
Results go to `B:\DevDrive\risen-ghidra\<topic>_dump.txt` (outside the repo); findings that matter go into `docs/RE.md`.

## 1. Find the RVA

Every Risen DLL exports its classes and scripts under decorated names; the lists are in `%TEMP%\risen_re\`:

| DLL | list |
|---|---|
| Game.dll, Engine.dll, Script.dll, FFC.dll, SharedBase.dll, FileSystem2.dll | `<Dll>.demangled.txt` (`Game.demangled.txt`, ...) |
| Script_Game.dll (all game scripts: `CanInteract_*`, `PreInteract_*`, `ZS_*`, `AutoLoot`, `InvFilter_*`) | `Script_Game.demangled.txt` |

Line format: `RVA(hex, no 0x)  ordinal  signature`. Search with Grep, e.g. `CanInteract_Player_Wolf` or `gCQuest::SetStatus`.
If the files are missing run `python tools/dump_exports.py` then `python tools/demangle_exports.py` (see docs/RE.md).
All DLLs have image base `0x180000000`; **all addresses in docs/RE.md are RVAs**. Not every function is exported: callers and
callees found in a dump show up as `FUN_18xxxxxxx` (subtract `0x180000000` for the RVA).

## 2. Run the dump

One invocation for everything you need (it **appends** to the output file, so delete it first). Use PowerShell:

```powershell
$env:JAVA_HOME = 'C:\Users\johnh\scoop\apps\openjdk21\current'; $env:PATH = "$env:JAVA_HOME\bin;$env:PATH"
$out = 'B:\DevDrive\risen-ghidra\mytopic_dump.txt'; Remove-Item $out -ErrorAction SilentlyContinue
& B:\D\ghidra_12.0_PUBLIC\support\analyzeHeadless.bat B:\DevDrive\risen-ghidra\ScriptGame RisenScriptGame `
  -process Script_Game.dll -noanalysis -readOnly `
  -scriptPath B:\DevDrive\git\github\risen-spass\tools\ghidra `
  -postScript DumpDecomp.java $out 0x134670 0x133b80 name:AutoLoot strref:"NothingToGet" 2>&1 | Select-Object -Last 5
```

Timeout 600000 ms; once the project opens it takes well under a minute. "Unable to lock project" means a Ghidra GUI or another
headless run has the project open: wait or tell the user.

Projects (already imported and analysed):

| Binary | project dir (`-readOnly` path) | project name | `-process` |
|---|---|---|---|
| Game.dll | `B:\DevDrive\risen-ghidra\Game` | `RisenGame` | `Game.dll` |
| Engine.dll | `...\Engine` | `RisenEngine` | `Engine.dll` |
| Script.dll | `...\Script` | `RisenScript` | `Script.dll` |
| Script_Game.dll | `...\ScriptGame` | `RisenScriptGame` | `Script_Game.dll` |
| FFC.dll | `...\FFC` | `RisenFFC` | `FFC.dll` |
| SharedBase.dll | `...\SharedBase` | `RisenSharedBase` | `SharedBase.dll` |
| FileSystem2.dll | `...\FileSystem2` | `RisenFileSystem2` | `FileSystem2.dll` |

Specs after the output file (any number, mixed; one DLL per invocation):

* `0x<RVA>`: the function containing that RVA.
* `name:<substring>`: functions whose (namespaced) name contains it, case-insensitive, **max 20**: be specific.
* `strref:<substring>`: functions that reference a defined string containing the text (find the code behind a HUD message,
  a script name or a property name this way).

Each dumped function has a header with entry address, RVA, signature and up to 12 callers, then the decompiled C. If you need
callees or a function the dump only names `FUN_...`, run a second dump with its RVA.

## 3. Read it (Read tool, `offset`/`limit`, or Grep for `^// ` headers)

* MSVC x64: `this` is the first argument; a non-trivial class returned by value comes back through a hidden pointer; a
  non-trivial class passed by value is a pointer to a caller-made copy that the callee destroys.
* `bCString`/`bCUnicodeString` are 8-byte pointers to ref-counted buffers. `Entity` is a handle class (`Entity::AttachTo`,
  `operator eCEntity*`).
* `PS*` wrappers (`PSInventory`, `PSRoutine`, `PSSkills`...) are holders whose first qword is an `eCEntity*`; property sets are
  fetched through the entity vtable slot `+0x198` with a type id (`0x1e` gCNPC_PS, `0x1f` gCInventory_PS, `0x24`
  gCScriptRoutine_PS, `0x27` gCItem_PS).
* Script_Game.dll **plain scripts** have the signature `__int64 f(gCScriptProcessingUnit*, void* self, void* other, __int64)`
  (callable from Lua with `risen.script.call`). **AI functions** (`PreInteract_*`, `Interact_*`, `ZS_*`) are
  `bool f(bTObjStack<gScriptRunTimeSingleState>&, gCScriptProcessingUnit*)`: resumable, they count steps in the stack frame
  (`*(int*)(frame - 0x58) <= DAT_... ` blocks are steps) and return false while an animation or walk runs.
* `eCScriptProxyScript::operator()(&DAT_..., self, other, arg)` calls another script by name; the name is a string in the
  static initialiser. `FUN_1800efef0(.., "Name", n)` style calls with a bare string are usually HUD messages.
* The decompiler can merge variables, mis-type enums (`gVar2 - 0xb < 2` means "11 or 12") and shows vtable calls only as `+0x178`.
  Say what you **read** versus what you **infer**.

## 4. Report and record

Answer first (1-3 sentences), then the function(s) with RVA and signature, the few decompiled lines that prove it (with
offsets), open questions, and the dump path. Do not paste whole functions. Confirm behaviour in the running game
(CLAUDE.md rule 4) and record RVAs, offsets and confirmed-vs-inferred in `docs/RE.md` in the existing style.
Dumps from earlier work (`B:\DevDrive\risen-ghidra\kwickloot\*.txt`, `ql_monster_dump.txt`) may already contain what you need: grep them first.
