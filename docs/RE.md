# Risen (Enhanced, x64) – Reverse-Engineering Notes

Goal: understand enough of `Risen.exe` and its DLLs to build a hook-based Lua (LuaJIT)
scripting layer for new quests, NPCs, items, models and localization.

All addresses are **RVAs** (image base subtracted) unless noted. Every Risen binary is ASLR-relocated
(`DYNAMIC_BASE`), and all DLLs share the preferred base `0x180000000` (`Risen.exe`: `0x140000000`).
Ghidra shows `base + RVA`.

Method: Ghidra 12.0 (MCP + headless `analyzeHeadless`), PE export tables (`tools/dump_exports.py`,
`tools/demangle_exports.py`), PE header inspection (`tools/pe_info.py`), and
`tools/ghidra/DumpDecomp.java` for bulk decompilation. Status legend: **[confirmed]** read in
decompiler/disassembly, **[export]** derived from a symbol name only, **[todo]** not yet verified.

---------------------------------------------------------------------------------------------------

## 1. Edition and installation layout

| Item | Value |
|---|---|
| Edition | Risen Enhanced (Steam), 64-bit, builds dated 2023-01-19 (PE timestamps `1674162214`…) |
| Install used | `B:\SteamLibrary\steamapps\common\Risen` |
| Source tree (from PDB paths) | `C:\Perforce\Risen\Risen1\Project\platform\pc_dx11\…` (configuration `Goldmaster_DX9`) |
| Steam AppID passed to `SteamAPI_RestartAppIfNecessary` | `0x1937A0` = **1652640** [confirmed] |
| `bin\steam_appid.txt` | `40300` (legacy Risen AppID) – while this file exists the Steam API does not re-launch via Steam, so direct launching works [todo: verify at runtime] |
| Risen.exe SHA-256 | `a0c0e1a69c711d41971696506c80243fc29799b8570b642789e6be0f50122f89` |
| Risen.exe MD5 | `9ba69363ec60b8fcf75aa18ea2dbb630` |

Directory layout (`<Risen>`):

```
bin\                       Risen.exe + engine DLLs (below)
bin\scripts\Script_Game.dll    compiled game scripts (native code!)
bin\mountlist_packed.ini   virtual file system mount description
data\{common,compiled,extern,ini}   (+ data\raw packed archives, see §6)
```

### Binaries (all x64, no packer, no CFG, no Authenticode signature)

| Binary | Size | Exports | Role |
|---|---|---|---|
| `Risen.exe` | 1.0 MB | 2 (`LoadProjectFile`, `entry`) | thin launcher, 763 functions; class `aCProtoTypeApp : gCGameApp` |
| `SharedBase.dll` | 1.2 MB | 4 807 | `bC*` base classes: strings, memory, property system, streams, containers |
| `FFCore.dll` / `FFC.dll` / `FileSystem2.dll` / `GUI2.dll` | – | 133 / 1 153 / 1 / 1 | FF-Core, virtual file system (`CFXVirtualFileSystem`), GUI framework |
| `Engine.dll` | 13.6 MB | 14 836 | `eC*` engine: entities, property sets, renderer (D3D9), input, script proxy base |
| `Game.dll` | 19.9 MB | 10 858 | `gC*` game logic: session, world, quests, infos/dialogs, NPC/inventory/… property sets, **`gCScriptAdmin`** |
| `Script.dll` | 4.6 MB | 2 601 | script **API** exposed to scripts: `Entity`, `World`, `QuestManager`, `InfoSystem`, `PS*` wrappers |
| `scripts\Script_Game.dll` | 3.0 MB | 1 789 | the game's scripts, compiled to native code; each is an exported function |
| `Importer.dll` | 1.2 MB | 243 | resource compilers (`iCResourceCompiler`, `iCSceneCompiler`, `iCDemoCompiler`) |
| `Music.dll`, `fmod.dll`, `binkw64.dll` | – | – | audio/video |
| `steam_api64.dll`, `discord_game_sdk.dll`, `sentry.dll` | – | – | platform SDKs / crash reporting |

Dependency graph (import tables): `Risen.exe → {Engine, Game, FFC, Importer, SharedBase, steam_api64, sentry}`;
`Game → {Engine, FFC, FFCore, Music, SharedBase, steam_api64, discord_game_sdk}`;
`Script_Game → {Engine, FFC, Game, Script, SharedBase}`; `Script → {Engine, Game, SharedBase}`.
`Script_Game.dll` and `Script.dll` are **not** imported by the exe – they are loaded by `gCScriptAdmin` (§4).

**Key observation:** every engine DLL exports its C++ classes with full MSVC-decorated names
(~36 000 symbols). Hooks and calls can therefore be bound with `GetProcAddress(<mangled name>)`
instead of hard-coded addresses, which makes the mod layer robust against patch-level changes.

---------------------------------------------------------------------------------------------------

## 2. `Risen.exe` startup sequence [confirmed]

`entry` (`0x3458`) → CRT → `FUN_140001fc0` (WinMain-like, calls) → **`FUN_140001290` (main, RVA `0x1290`)**:

1. `gCGameApp::gCGameApp(app)`; vtable overwritten with `aCProtoTypeApp::vftable`; `gCGameApp::Create`.
2. Construct `iCSceneCompiler`, `iCResourceCompiler`, `iCDemoCompiler`.
3. `SteamAPI_RestartAppIfNecessary(1652640)`; if it returns false → `SteamAPI_Init()`
   (failure message box: *"SteamAPI_Init failed: Can't init steam."*).
4. Command-line switches looked up in `bCCommandLine::GetOptions()` (a `bCString → bCString` hash map):
   `skipmain`, `compileoptions`, `compilelibrary`, `compilematerials`, `compilenavigation`,
   `importanis`, `importimages`, `importsounds`, `builddemo` (any of them = "compiler mode": silent log,
   no game run), `nosplash`, `config=<name>` (default `"Config"`), `controllermode`, `projectfile=<name>`
   (default `"Projects"`), `-mnt=mountlist_packed.ini`, `-worldfile=World`, `disablecrashdumps`,
   `-keyboardmode`.
5. `eCApplication::CreateSplashScreen()` unless `nosplash`/compiler mode.
6. `eCApplication::Create(app, hInstance, configName, cmdline)` → engine, modules, window created
   (`gCGameApp::OnInitializeEngine` etc. are virtual overrides invoked from here).
7. Virtual path `#G3:/save` is resolved with `CFXVirtualFileSystem::VirtualToPhysicalPath` and the
   result is written to `HKCU\Software\Deep Silver\Risen\SAVEGAME_DIR` (`FUN_140001050`).
8. `aCProtoTypeApp::LoadProjectFile("G3_Startup" world in project "Projects")` (export `0x24F0`):
   `gCWorkspace::GetInstance()->GetProject(name)`, `Load`, `gCProject::GetWorld`/`InsertNewWorld`,
   `SetActiveWorld`.
9. `gCSession::GotoStartPosition`, `eCWindow::SetVisible(true)`, `ConfineCursor`, `PlayIntros`,
   `gCSession::StartGame` (session looked up through `eCModuleAdmin::FindModule(<mangled "gCSession">)`
   + `__RTDynamicCast`), then **`eCApplication::Run`** – the main loop. After it returns: cleanup,
   `SteamAPI_Shutdown`.

Third-party in the exe: Steamworks, **Sentry-native** crash reporter (DSN embedded; database dir
`%LOCALAPPDATA%\Risen\.sentry-native`, release `checkout-app@3.2`).

---------------------------------------------------------------------------------------------------

## 3. Exported class surface (what is hookable by name)

Counts are numbers of exported symbols (methods, ctors, vftables…).

* **Game.dll** – `gCCharacterMovement_PS`(136), `gCInventory_PS`(71), `gCNavigation_PS`(59), `gCInfo`(43),
  `gCInfoSystem`(42), `gCStateGraph_PS`(42), `gCInteraction_PS`(41), `gCScriptRoutine_PS`(37), `gCQuest`(37),
  `gCWorld`(33), `gCSvmAdmin`(32), `gCScriptProcessingUnit`(29), `gCQuestManager`(28), `gCSession`(28),
  `gCNPC_PS`(26), `gCScriptAdmin`(15), `gCGameScript_PS`(18), `gCItem_PS`(18), `gCSaveGameManager`(14), …
* **Script.dll** (script API): `PSParticle`, `Entity`(222), `PSNpc`(130), `PSCharacterMovement`(102),
  `PSInventory`(99), `PSSkills`(86), `PSRoutine`(85), `PSInteraction`(63), `PSCombatSystem`(59),
  `InfoSystem`(52), `PSDialog`(47), `World`(45), `QuestManager`(19), `PSItem`(37), `Achievement`(33), …
* **Script_Game.dll**: no classes; 1 789 free functions, all with the *same* signature
  `__int64 f(gCScriptProcessingUnit*, void*, void*, __int64)`. Examples: `AssessActive`, `AssessAttacker`,
  `AssessDamage`, `AI_JumpUpExit`, `Arrow_OnHit`, `Arrow_Activate_Physics`.

Dumps of the full tables live outside the repo (`%TEMP%\risen_re\*.demangled.txt`) and are regenerated by
`tools/dump_exports.py` + `tools/demangle_exports.py`.

---------------------------------------------------------------------------------------------------

## 4. Script system  [confirmed by decompilation of Game.dll]

### 4.1 Big picture

Game logic (AI, dialogue conditions/effects, quest scripts, item effects, …) is **native C++ compiled into
`scripts\Script_*.dll`**. There is no bytecode VM. Each script is an exported function

```c
__int64 Script(gCScriptProcessingUnit* unit, void* a, void* b, __int64 arg);   // Script_Game.dll exports
```

and the engine finds scripts **by name** through a global registry, `eCScriptFunctionMap` (Engine.dll
singleton, `eCScriptFunctionMap::GetInstance()` = `0x6b5230`). The registry has four independent name
spaces: *Script*, *AIFunction*, *AICallback*, *AIState* (the last three are used by the NPC AI state machine,
`gCScriptProcessingUnit::CallAIFunction`, `RunScriptState`, …). Game data (quests, infos/dialogues,
effects, property sets such as `gCGameScript_PS`) stores only the **script name**
(`eCScriptProxyScript`, wraps an `eSScriptBase*` looked up by name).

### 4.2 Loading

`gCScriptAdmin::PostInitialize` (`0x962450`) → `LoadAllScriptGame` (`0x9633d0`):

1. read a config value (path of the scripts directory, `DAT_181288ee0`; resolves to `bin\scripts`),
2. enumerate `*.dll` through the virtual file system (`eCVirtualFileSystem::FindFiles`),
3. for each file whose **lower-cased name contains `script_`** call `LoadScriptDLL(name)`.

`LoadScriptDLL` (`0x962bb0`):

1. returns true if already loaded (`IsScriptDLLLoaded`: name comparison against `this+0x200` list),
2. `eCArchiveManager::GetFile` → physical path → `g_LoadModule` (≈ `LoadLibrary`),
   on failure a message box *"Could not load script module: %s. Enter debugger?"*,
3. `GetProcAddress(module, "ScriptInit")` → `ScriptInit()` returns a pointer to a descriptor with
   **four arrays** `{ptr, int count}` at offsets `0x00/0x08` (AIStates), `0x10/0x18` (AIFunctions),
   `0x20/0x28` (AICallbacks), `0x30/0x38` (Scripts). Each element is **0x18 bytes**:

   ```
   +0x00  bCString name           (registry key)
   +0x08  bCString text           (copied into the registry entry, informational)
   +0x10  void*    function
   ```
4. For every element: `eCScriptFunctionMap::RegisterScript[AIState|AIFunction|AICallback](map, name)`
   (get-or-create `eSScriptBase*`), then **only if the entry's function slot is still empty**:

   ```
   eSScriptBase  (0x28+ bytes, layout inferred from LoadScriptDLL / RunScript)
   +0x08  bCString* name
   +0x10  bCString  text
   +0x18  module record* (owning DLL)
   +0x20  void*     function          <-- what RunScript calls
   ```
   i.e. **the first DLL that registers a name wins** (VFS enumeration order); later duplicates are ignored.

The module record (`0x70` bytes) holds the HMODULE, the file name and four `bTValArray`s of the registered
entries; it is appended to the array at `gCScriptAdmin+0x200` (count `+0x208`).

### 4.3 Calling

| RVA | Function | Notes |
|---|---|---|
| `0x964460` | `__int64 gCScriptAdmin::CallScript(const bCString& name, eCEntity* self, eCEntity* other, __int64 arg)` *(protected)* | no-ops (returns 0) unless the app is a `gCGameApp` **and** the game is running **and** `eCEntityAdmin::IsProcessingEnabled()`. Stores `self`/`other` into `eCEntityProxy`s at `this+0x178/+0x188`, `arg` at `this+0x198`, then `RunScript(name, this+0x38)` |
| `0x964540` | same with `gSScript*` *(protected virtual)* | |
| `0x963770` / `0x963880` | `RunScript(name\|gSScript*, gCScriptProcessingUnit*)` | looks up the registry, then `fn(unit, 0, 0, unit->+0x160)`. Missing script ⇒ debug message box *"Script is not registered"* (suppressed once the user clicks Ignore, flag `DAT_181110214`) |
| `0x9642e0` / `0x9643a0` | `CallScriptFromScript(name\|gSScript*, void*, void*, __int64)` | script → script calls: `fn(unit(this+0x38), a, b, arg)` |
| `0x9635c0` / `0x963640` | `RunScriptFromScript(...)` | |
| `0x963a30`, `0x963cf0`, `0x963fe0` | `RunScriptCallback / RunScriptFunction / RunScriptState` | AI callback/function/state namespaces |
| `0x9625d0` | `const gSScript* GetScript(const bCString&) const` | forwards to `eCScriptFunctionMap::GetScript` |
| `0x962750…0x9627e0` | `RegisterScript / …AICallback / …AIFunction / …AIState` | forward to the map; **Engine.dll exports `eCScriptFunctionMap::RegisterScript` (`0x6b48a0`), `…AICallback` `0x6b4940`, `…AIFunction` `0x6b49e0`, `…AIState` `0x6b4a80`, `UnRegisterScript*` `0x6b47f0…0x6b4880`** |

`gCScriptProcessingUnit` layout (script "thread" state, one embedded in `gCScriptAdmin` at `+0x38`):

| Offset | Meaning |
|---|---|
| `+0x140` | `eCEntityProxy` **self** (`GetSelfEntity` = `0x96c7c0`) |
| `+0x150` | `eCEntityProxy` **other** (`GetOtherEntity` = `0x96c740`) |
| `+0x160` | `__int64` **int parameter** (`GetIntParameter` = `0x96d960`) |
| – | `GetTargetEntity` (`0x96c760`) = `self->vfunc(0x198)(0x1e)` → property set → its `+0xE8` proxy |

### 4.4 Consequences for modding / the Lua layer

* **Adding a script** = `eCScriptFunctionMap::RegisterScript(name)` + store a function pointer in the entry's
  `+0x20` slot. **No inline hook needed**; data that refers to the script by name (quests, infos, effects)
  immediately works.
* **Overriding a script** = replace the `+0x20` slot (keep the old pointer for "call original"). Because every
  script has the same signature, one native trampoline per script is enough (we use a table of
  pre-instantiated thunks, `src/game/script_bridge.cpp`).
* **Calling a script from Lua** = `gCScriptAdmin::CallScript(name, self, other, arg)` (must be on the game thread
  while the game is running) or call the registry function pointer directly.
* Hooking is only needed for *lifecycle* events: `gCScriptAdmin::PostInitialize` (script DLLs are being
  loaded – register Lua scripts right after), `gCSession::Process` (per-frame, `0x9711e0`),
  `gCSession::StartGame` (`0x9712d0`).
* Scripts can also be added the "official" way: any `bin\scripts\Script_*.dll` exporting `ScriptInit`.

### 4.5 Other lifecycle functions

* `gCSession::Process()` `0x9711e0` (virtual, per frame): early-outs without a current world; updates weather
  position, runs the *player process* script proxy (`DAT_18127d650`), `gCSessionInfo::Process`.
* `gCSession::StartGame()` `0x9712d0`: HUD/input/music setup after the world is loaded (called once from
  `main`, see §2).
* `gCGameScript_PS::OnGameInit/Start/Process/End/PauseStart/PauseProcess/PauseEnd` – script slots on the game-script
  property set (names are stored in the world data).

---------------------------------------------------------------------------------------------------

## 5. Other systems relevant to modding  [export-level, partially verified]

* **Quests**: `gCQuestManager` (singleton, `GetInstance` `0x961a60`) `OpenQuest/RunQuest/CloseQuest/FailQuest/
  GetQuest/GetQuestAt/AddQuest`; `gCQuest` has status at `+0xC8` (`1` = running, from `QuestManager::IsRunning`).
  `Script.dll` wraps these as `static bool QuestManager::OpenQuest(const bCString&)` etc.
* **Game variables** (integer flags used by dialogue conditions): `InfoSystem::Get/SetGameVariable`
  (`Script.dll`, static) → `gCInfoModule::GetInstance()->GetSystem()->…`.
* **Entities**: `Script.dll`'s `Entity` is an 8-byte pointer wrapper (`Entity::Entity()` just zeroes it;
  returned through a hidden pointer, e.g. `World::GetPlayer`). `World::GetPlayer` → `gCSession::GetPlayer()`.
* **Localization** [confirmed in-game]: `eCLocAdmin2` (Engine.dll singleton, `GetInstance` `0x738c30`) is a thin
  facade over `CFXLocTable : CFXTableMap` (FFC.dll). Tables are declared in `data\ini\loc.ini` (`default`,
  `document`, `focus`, `hud`, `info`, `quest`, `svm`, `skills`, `item`, `npcInfo`, `storyprints`, `TUT`, `ACH`, …;
  CSV source + compiled `.tab` in `data\compiled\strings.pak`) and registered under a *prefix*;
  `CFXTableMap::FindTable(id)` splits an id on `':'` to pick the table.
  * **Writing does not work at runtime**: `eCLocAdmin2::SetString` → `CFXTableMap::SetValue` returns `false`
    for every id we tried (with and without prefix, before/after the world is running). Don't build on it.
  * **Reading**: text objects (`eCLocString`, `gCInfoLocString`, `gCQuestLocString`, … all derive from
    `CFXLocString`/`CFXLocStringEx`) call `CFXLocString::GetText/GetPlainText/operator bCString/…`, which use
    `CFXTableMap::LookupItem`. `CFXTableMap::LookupValue` has almost no callers, so hooking it alone does nothing.
  * **What works** (`src/game/loc_overlay.cpp`): hook the *accessors* (`GetText()` `0x7480`/`0x7560`/`0x39650`,
    `GetPlainText()` `0x74f0`/`0x75b0`/`0x39740`, `operator bCString` `0x7800`, and the `…Ex` variants
    `0x399f0`/`0x39980`/`0xadc0`/`0xae40`), fetch the key with `GetKey()` (`0x7460`) and answer from a
    mod-supplied overlay. Verified: `GO_QuestRunning` and `TUT_Desc_FirstPlayerMission` were replaced.
  * Real ids seen at runtime look like `TUT_LoadingHint_29`, `HUD2_CONTEXT_AREA_OUTSIDE_EN`, `GO_QuestRunning`,
    `FO_Beach_Officer_Dead`, `QUEST_PANKRATZX2_2009-02-12_09-58-28_00` (`risen.loc.trace(true)` lists them).
  * Active text language: `eCLocAdmin2::GetTextLanguage()` (`"English"`).
* **Virtual file system**: `CFXVirtualFileSystem` (FFC.dll) `MountDirectory(virt, phys, flags)`,
  `MountPackFile`, `UnmountDirectory`, `CreateFileA`, … Mounts are configured by `bin\mountlist_packed.ini`
  (`[Physical]`, `[Packed]` sections; the exe is started with `-mnt=mountlist_packed.ini`).
* **Templates** (NPC/item/… blueprints): `eCTemplateEntity` (`GetName`, `CreateEntityCopy`, `CreateNewEntity`),
  stored in `data\common\templates.pak`.
* **Data archives** (`*.pak`): `data\common` (templates, meshes, materials, physics, projects, strings, …),
  `data\compiled` (animations, images, library, sounds, speech_*, strings, …).

---------------------------------------------------------------------------------------------------

## 6. Calling-convention facts (MSVC x64) verified by decompilation / in-game

* `this` is the first argument. A class return value that is **not trivially copyable** (`bCString`,
  `bCUnicodeString`, `Entity`, `bCVector`, …) is returned through a **hidden pointer**: `RCX` for static
  functions, `RDX` (after `this`) for member functions. The callee *constructs* into it; the caller must
  destroy it.
* A non-trivial class passed **by value** (`Entity::GetEntity(bCString)`, `Entity::StartSay(…, bCString, …)`)
  is passed as a pointer to a caller-made copy, and **the callee destroys it** (`~bCString(param)` appears in
  the callee). Never destroy such an argument after the call (double free).
* `bCString` and `bCUnicodeString` are 8 bytes: one pointer to a ref-counted buffer. Construct/destroy only via
  the SharedBase exports (`??0bCString@@QEAA@PEBD@Z`, `??1bCString@@QEAA@XZ`, …).
* `Entity` (Script.dll) is 8 bytes: an `eCEntity*`. Member functions take the address of that holder as `this`
  (`*(eCEntity**)this`). `bCVector` is three floats; `GetPosition` writes them through the hidden pointer.
* Protected members (`gCScriptAdmin::CallScript(const bCString&,…)`, `LoadAllScriptGame`, …) are still exported
  and callable through `GetProcAddress`.

## 7. Injection and runtime facts

* **Proxy**: `Risen.exe` statically imports `sentry.dll`. A DLL with that name that forwards all 169 exports to a
  renamed `sentry_orig.dll` is loaded by the OS loader before `main()`. MSVC's linker only accepts
  `name = module.name` forwarders when it can find an import library for `module`, so the build generates
  `sentry_orig.lib` from the original's export list (`lib /DEF:`); without it every forwarder fails with LNK2001.
* **Timing** (measured): `DllMain` of the proxy runs on the main thread during process start. Engine/Game/FFC/
  SharedBase are already mapped, so exports can be resolved and MinHook trampolines created there.
  `Script.dll` is *not* mapped yet (only `Script_Game.dll` depends on it); it appears during
  `gCScriptAdmin::PostInitialize`, ≈ 30 s after launch on the test machine, so the Script.dll-dependent
  symbols are resolved in a second pass inside that detour.
* **Self-test**: after `PostInitialize` the bridge compares the registry's function slot (`+0x20`) of three
  well-known scripts with `GetProcAddress(Script_Game.dll, …)`; it matched in every run, which validates the
  `eSScriptBase` layout from §4.2 against the real binary.
* `gCSession::Process` is only invoked once a *game session* is running; at the main menu it fires once.
  `World::NewGame()` (a request posted to `gCHUDFileManager`) takes ≈ 60 s to load on the test machine.
* Windowed mode: `ConfigLocal.xml` `<Window FullScreen="false">`; the per-user
  `%LOCALAPPDATA%\Risen\ConfigUser.xml` overrides the window size, and the game **rewrites that file** while running.

## 8. Tooling pitfalls

* `pefile` stops resolving export *names* after 8192 symbols unless `PE(..., max_symbol_exports=N)` is given;
  Engine.dll (14 836) and Game.dll (10 858) were silently truncated before. See `tools/dump_exports.py`.
* Ghidra headless needs JDK 21 (`analyzeHeadless`); the first import+analysis of Game.dll takes ~25 min,
  Engine.dll ~20 min, Script.dll ~2 min, FFC.dll ~2 min. `tools/ghidra/DumpDecomp.java` then decompiles any list
  of RVAs / name substrings in seconds (`-process … -noanalysis -readOnly`).

## 9. Open questions / next steps

* **Spawning** [confirmed in-game]: `Template::Template(const char*)` (`0x235c40`) builds a template reference from a
  name (`Template` derives from `eCTemplateEntityProxy`; we reserve 128 bytes); `Template::IsValid` (`0x235cb0`)
  tells whether it exists. `Entity::Spawn(Template /*by value, callee destroys*/, const bCMatrix&, bool)`
  (`0x20e4d0`) created an item entity (`It_1H_Sword`); the pose is a 4×4 row-major float matrix with the translation in
  the last row. `PSInventory::CreateItems(const Template&, int, gEStackType)` (`0x223820`) added items to the
  player's inventory (`PSInventory` is an `eCEntity*` holder like `Entity`). Template names are plain strings in
  `data\common\templates.pak` (809 `It_*`/`NPC_*`-like names found by a naive scan). Still to do: NPC templates
  (names, AI/routine setup after spawn) and the meaning of `gEStackType`.
* **New templates (items/NPC archetypes)**: stored in `data\common\templates.pak`; find out whether
  `eCTemplateEntity::CreateNewEntity/CreateEntityCopy` + registration with the scene admin can create new
  templates at runtime, or whether a loose-file mount (`CFXVirtualFileSystem::MountDirectory`) is the better way.
* **Models/textures/sounds**: mount a mod directory through the VFS (`MountDirectory(virtual, physical, flags)`,
  semantics of `flags` and mount priority still unknown) before the engine asks for the resource.
* **Quests/dialogues** (`gCQuest`, `gCInfo*`) are data objects (`data\common\projects.pak`); creating them at
  runtime means constructing property-set objects. Cheaper first step: drive existing quests/dialogues from Lua
  (`risen.quest.*`, `risen.gamevar.*`, script overrides).
* AI states/functions/callbacks (`RegisterScriptAI*`) use different signatures and are not bridged yet.
