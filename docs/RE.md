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

## 10. NPCs [confirmed in-game]

* **Spawning**: `Entity::Spawn(Template, pose, false)` with an NPC template name (`Peasant_1_01`, `Pirate_S_01`,
  `Inq_Guard_01`, `Don_Henchman_S_01`, `Wolf`, `Boar`, `NPC_Guard`, …) produced simulated NPCs: `IsNPC()` and
  `IsCharacter()` true, alive, falling to the ground, fighting each other within seconds (AI state
  `ZS_Attack_CombatMove`). Template names are plain strings in `data\common\templates.pak`.
* **PS\* wrappers are holders**: `PSNpc`, `PSSkills`, `PSInventory`, `PSRoutine`, … store the `eCEntity*` in their first
  qword and fetch the property set through `eCEntity` vtable slot `+0x198` (`GetPropertySet(type)`); type ids seen:
  `0x1e` gCNPC_PS, `0x1f` gCInventory_PS, `0x24` gCScriptRoutine_PS (`5` is used by `Entity::IsNPC`). An `EntityHolder`
  is therefore a valid `this` for all of them, and a missing property set makes the call a no-op.
* **Property wrappers** (`PSNpc::PropertyGuild`, `…Group`, `…AttitudeLock`, `…Gender`): assignment/conversion operators
  are exported; `gCNPC_PS+0x170` holds the guild. Observed `gEGuild` values: none/animals 0, Don 1, peasants 3,
  Inquisition 5, pirates 7.
* **Skills**: `PSSkills::Set*(value, gESkillModifier)` → `SetBase(skill, value, modifier)`; with modifier `0` the value is
  **added** to the current total (level 5 + 7 = 12; +3 = 15). Skill ids seen: `0` hit points, `2` level.
* **AI**: `PSRoutine::SetState(bCString /*by value*/)` → `gCScriptRoutine_PS::AISetState`. State names are the registered
  `ZS_*` AIState scripts of `Script_Game.dll` (63 found, list in LUA_API.md). The running AI replaces the state on its own
  (a freshly spawned peasant next to hostile pirates was in `ZS_Attack_CombatMove` after ~3 s).
* **Labels**: the name shown when the player focuses an entity is the loc string `FO_<entity name>` (e.g. `FO_Boar`,
  `FO_It_1H_Sword`, `FO_SPASS_Bob`); `Entity::GetDisplayName` is just the entity's own name. A custom NPC therefore needs
  `risen.loc.set("FO_<name>", "Shown name")`.
* **Not done yet**: dialogue (`gCInfo`/`gCInfoSystem` are data objects: custom conversations need either new
  info objects or scripted `Entity::StartSay`/`StartOutput` lines), appearance variants (head/body templates `Hero_Head_*`),
  schedules/routines beyond a single state, party membership (`PSParty`), enemy/friend relations beyond guild.

## 11. Dialogues (gCInfo) [confirmed in-game]

* **Model**: a dialogue option is a `gCInfo` owned by an NPC (`+0x28` owner entity name; `+0x18` name; `+0x30` "already
  given" flag; `+0x38` parent name; `+0x40` quest name; `+0x50` ConditionType; `+0x60` Type). Its body is a command
  sequence (array at `+0x98`, count `+0xa0`). `gCInfo::GetDialogDescription` uses the text of the **first command**
  as the option label. `gCInfoSystem::HasInfos(npc)` counts top-level infos (empty parent) whose Type is not 5 and
  that are available.
* **Creation**: `gCInfoSystem::CreateInfo(const bCString& name)` (Game `0x8b13f0`; system from
  `gCQuestManager::GetInstance()->GetInfoManager()`) allocates, names and registers the info; afterwards
  `gCInfoSystem::BuildMapInfosByOwners()` (`0x8b1e40`, protected but exported) rebuilds the owner → infos map.
* **Properties by name**: `bCAccessorProperty(object, "ConditionType")` + `ConvertStringToValue("gEInfoCondType_General")`
  is the engine's generic, notification-aware setter (the editor uses it). It works on any `bCObjectBase`
  (infos, quests, ...) and is how we set `Type`, `ConditionType`, `Quest`, `LogTopic`-like properties without knowing
  enum numbers. Enum names seen: `gEInfoType_{Info,Important,Comment,News,Refuse,Parent}`,
  `gEInfoCondType_{General,Hello,End,Back,Trade,Heal,Open,Running,Success,Failed,Cancelled,Won,Lost,Ready,Overtime,
  NotYetFinished,Delivery,PartDelivery,Join,Dismiss,MobJoin,MobDismiss,SlaveJoin,Duel,Crime,PoliticalCrime,
  FirstWarn,SecondWarn,PickPocket,MasterThief,Reactivator,Activator,Wait,LongTimeNoSee,NothingToSay,Finished,DoCancel}`.
  Evaluating conditions of a quest-state type for an info **without** an existing quest makes the engine pop up a
  debug message box that blocks the main thread: never scan condition values blindly.
* **Commands**: `gCInfo::InsertComSeqItem(bCString name)` (by-value string; callee destroys) creates a command by its
  editor name for enum values < 0x3b. The engine's own table (59 entries `{int enum; bCString name}` at Game.dll+0x122ef00):
  `Attack=1 Back=2 BoostAttribs=3 ClearGameEvent=4 CloseQuest=5 Description=7 DoLoot=9 DoTrade=10 End=11 EndGame=12
  FullStop=13 Give=14 GiveXP=15 Heal=16 InfoWait=17 MoveTo=21 PickPocket=22 PlayMusic=23 RunQuest=25 Say=27
  SayConfirm=28 SayHoldingPosition=29 SayKeepItUp=30 SaySound=31 SaySVM=32 SetGameEvent=33 SetGuardStatus=34
  SetMobEnabled=35 SetPartyEnabled=37 SetRelaxingPoint=38 SetRoutine=39 SetSlaveryEnabled=40 SetSleepingPoint=41
  SetSectorStatus=42 SetTeachEnabled=43 SetTradeEnabled=44 SetWorkingPoint=45 ShowPicture=46 ShowSubtitle=47 Sleep=48
  StartEffect=49 SucceedQuest=50 Use=51 Wear=52 Trigger=55 RunInfo=56 RunAIScript=64 AddInfoSystemEndScript=65
  StartTutorial=66`.
  **`RunScript` and `AddLogText` are not insertable by name/enum**: create them with their class factories
  (`gCInfoCommandRunScript::CreateObject()`, `gCInfoCommandAddLogText::CreateObject()`) and
  `gCInfo::InsertComSeqItem(gCInfoCommand*)`. `AddLogText::GetCommandType()` returns 0 and its `Execute` is a no-op, i.e.
  it is a dead command; quest log entries do not come from it (see §12).
* **Say**: `gCInfoCommandSay::SetSpeaker/SetListener(bCString)` with keywords `"player"`, `"npc"` (the conversation
  partner that is not the player) or an entity name (`gCInfo::GetEntityByKeyword`); the text is a `gCInfoLocString`
  (derives from `CFXLocString`, vtable first, so the `CFXLocString` base starts at +8; `SetKey(bCString)` on the object
  start corrupts memory and faults). Texts are plain loc keys, served by the loc overlay.
* **Running**: `InfoSystem::StartInfoSystem(Entity, Entity)` (Script.dll, static) opens the conversation;
  `gCInfoSystem::GetAvailableInfoCount/GetAvailableInfo(i)` list the offered options;
  `gCInfoSystem::ExecuteDialogInfo(info)` runs an info only if it is in the system's running list (i.e. while a
  conversation with its owner is open), then plays `Say` lines (~13 s for two lines) and executes `RunScript`.
  After an info was used it becomes unavailable (`InfoGiven`) unless `Permanent`.

## 12. Quests (gCQuest) [confirmed in-game]

* **Creation**: `gCQuest::CreateObject()` (`0x95d030`, allocates 0x188 bytes and runs `Create`) → set the name through
  the object's vtable (`+0x108` SetName, `+0x100` GetName; exactly as `gCQuestManager::AddQuest` does) → set properties by
  name (`Type` = `gEQuestType_*`, `ExperiencePoints`, `Silent`, …) → `gCQuestManager::AddQuest(quest, name)` (`0x960880`;
  refuses duplicate names; also files the quest under its type). Status transitions use the manager
  (`OpenQuest/RunQuest/SucceedQuest/…`, already exposed by Script.dll's `QuestManager`): Open → Running → Success verified.
  The manager knew 370 vanilla quests at the main menu.
* **Property names** (from `Access*`): `Type`, `Status`, `LogTopic`, `LogText`, `ExperiencePoints`, `StartChapter`,
  `EndChapter`, `Silent`, `Actors`, `FinishedQuests`, `DeliveryEntities`, `RunningQuestScript`, `SucceededQuestScript`,
  `FailedQuestScript`, … Quest types: `gEQuestType_{Arena,BringNpc,Defeat,DriveAway,Duel,EnterArea,FollowNpc,HasItems,Kill,
  Plunder,Report,Sparring}`; statuses `gEQuestStatus_{Open,Running,Success,Failed,Cancelled,Lost,Obsolete,Won}`.
* **Title**: `LogTopic` is a `gCQuestLocString` (`AccessLogTopic()`); its string conversion does not accept a plain key
  (reads back empty), so the key is set with `CFXLocString::SetKey` on the base subobject (same +8 rule as above).
  Verified: after `set_topic`, `get_property("LogTopic")` returns the overlay text.
* **Log**: the quest log is the array at `quest+0x50` (`{SLogEntry* data; int count; int capacity}`; `gCQuest::GetLogText`
  returns it, `lea rax,[rcx+0x50]`). `SLogEntry` is **0x30 bytes = two `eCLocString` (0x18 each)**; `DrawQuestLog` draws the
  first string of each entry. Entries are appended natively: grow with `bCMemoryAdmin::Realloc`, zero the new slots, construct
  the text with `eCLocString::eCLocString(const bCString&)` and the second one with the default constructor, increment `count`.
  Verified: `log_count` 0 → 1 without crash. (The array count is **not** at +0x10: that was XP.)
* **Conditioned dialogue**: an info with `ConditionType=gEInfoCondType_Running` and `Quest=<name>` is offered only while the
  quest is running (verified: absent before the quest, present during, and the "start" option disappears after use).

## 13. Test harness facts

* `SteamAPI_RestartAppIfNecessary(1652640)` can make the Steam client relaunch the game **from the real install path**
  (observed once: Steam reported `B:\SteamLibrary\...\Risen.exe` starting at the time a test copy was launched, and that
  instance crashed in `SharedBase.dll+0x7caa9`). `scripts/run-windowed.ps1` therefore sets `SteamAppId`/`SteamGameId`
  before launching, which prevents the restart; no relaunch happened afterwards.
* Screen capture of the Direct3D 9 window (`CopyFromScreen`, `PrintWindow`) returned black frames, so the dialogue/quest
  UI was verified through the engine's own state (available options, quest status, log counts) and the loc-key trace.

## 14. Savegames, session lifecycle and events [confirmed in-game unless marked]

Game.dll RVAs; every one of these is exported with a decorated name, so the hooks bind by name (`src/game/events.cpp`).

* **Save path.** All saves (menu, quick, auto) end in `gCSaveGameManager::Save(name, title, flags, overwrite, gCSaveGame&)`
  (`0x99bc50`, private) -> `WriteSaveGame` (`0x99b4c0`): header, `eCModuleAdmin::WritePreLoadData`, `gCWorld::WriteSaveGame`
  (entities), `eCModuleAdmin::WriteSaveGame` (every module: session, quest manager, info system, ...). `QuickSave` (`0x99c520`) is
  a wrapper that renames an old file to a backup name and calls `Save(name, ..., overwrite = true)`.
  `World::QuickSave/AutoSave/SaveGame/LoadGame/QuickLoad/NewGame` (Script.dll) only *post* requests to `gCHUDFileManager`,
  which performs them from `OnProcessEvent` on the next processing step.
* **Load path.** `gCHUDFileManager::LoadGame` (`0x87aef0`) -> `gCSaveGameManager::Load(name)` (`0x99d080`) -> `Load(name, stream)`
  (`0x99b8f0`, private): reads the header, `eCModuleAdmin::ReadPreLoadData`, reloads the world (`gCWorld::Reload`),
  `gCWorld::ReadSaveGame` (entities), then `eCModuleAdmin::ReadSaveGame` (modules). Afterwards `gCSession::Start(1)` and
  `GotoLoadedPosition` run. New game: `gCHUDFileManager::NewGame` (`0x87afe0`) -> `gCQuestManager::ReadQuests()` + `gCInfoSystem::Reset()` when the
  world is unchanged, `gCWorld::Restart`, `gCSession::Start(0)`.
* **Session modes.** `gCSession::Start(gESession_StartMode)` (`0x973da0`): `0` new game, `1` load, `2` other. It calls `Stop()` (`0x9743c0`)
  first when a player entity exists. `gCWorld::Activate` (`0x994760`) makes a world current; its name is
  `eCProcessibleElement::GetFileBaseName()` (`"World"`).
* **What a savegame contains** (tested with a runtime NPC, quest, info and mod data, loaded in the same process and in a fresh one):
  * **Entities created with `Entity::Spawn` ARE saved, but lose their unique name** (corrected in M7, §32): the NPC looked gone after loading
    because `risen.entity.get(name)` found nothing; the entity itself stood there nameless, with its saved ID. Find it again by ID (`Entity:id()`,
    `risen.entity.by_id`) and name it again, or use `risen.npc.persistent`; a mod that spawns it again creates a duplicate.
  * **Quests**: `gCQuestManager::ReadSaveGame` (`0x961f00`) reads `(name, runtime data)` pairs and looks each name up in the manager;
    an unknown name is streamed into a temporary quest and *discarded* (`"Quest '%s' not found, streaming runtime data into
    temporary Quest!"`). With the quest created beforehand, status **and log entries** were restored. So runtime quests must be
    created in `load.before`, which runs before the module read. `NewGame` calls `ReadQuests()`, which removes runtime quests
    (verified: a quest defined in a loaded session was gone at `session.start(new)`).
  * **Infos**: `gCInfoSystem::ReadSaveGame` (`0x8b3d70`) restores the "given" flag by info name; it only applies to infos that exist
    (verified: `given = true` survived a fresh-process load when the info was created in `load.before`). Runtime infos survive
    `NewGame` (the owner map is kept), so they only need to exist once per process.
  * Game variables (`InfoSystem::Get/SetGameVariable`) are saved by the engine; the Lua state is not -> `risen.save` sidecar files.
* **Events and their engine sources.** `gCQuest::SetStatus(status, bool, bool)` (`0x95d920`; the quest's current status is the int at
  `+0xC8`, `0` open, `1` running, `2` success, `3` failed, `4` obsolete, `5` cancelled, `6` lost, `7` won; it also fires a global script proxy
  with the quest name), `gCQuestManager::OnNPCKilled/OnNPCDefeated(const eCEntity&)` (`0x95f9b0`/`0x95f8c0`, fired by the combat/death code; setting
  hit points to 0 through the script API does *not* trigger them – a real fight does), `gCInfoSystem::ExecuteDialogInfo(gCInfo*)`
  (`0x8b3b70`) for a chosen option and `OnInfoFinished(gCRuntimeInfo*)` (`0x8afcb0`; the `gCInfo` is at `runtimeInfo+0x10`),
  `gCInventory_PS::InsertStack(eCEntity* item)` (`0x8c0cf0`: the inventory takes the item entity and kills it; template name = entity vtable `+0x170`
  -> `GetName` `+0x178`; quantity = `gCItem_PS` (property set `0x27`) `+0x20`; owner = `eCEntityPropertySet::GetEntity()`).
  `gCInfoSystem::OnInfoStarted(bCString)` is *not* the info start: its argument is an AI state name (`IS_2_Running_ProcessInfo`, ...).
* **Pitfall.** The engine's log function (`FUN_1804a5b00`) is an empty stub in this build, so engine error messages never appear anywhere;
  failures such as "Could not create file for write access" return `false` silently. `risen.debug.trace_files(substring)` (hook on
  `CreateFileA/W`) is how to see what the engine really opens.

## 15. Test environment isolation [confirmed]

* The engine expands `$(localappdata)` in `mountlist_packed.ini` and finds `ConfigUser.xml`/`SaveGames` through `SHGetFolderPathA`
  (SharedBase.dll, Risen.exe) and `SHGetSpecialFolderPathA` (FFCore.dll) – **not** through the environment variable. A hard-coded
  absolute path in the mount list makes the `save` mount unusable: every save then fails without a message (§14).
* `config.toml` `[debug] app_data = "<dir>"` hooks both functions for `CSIDL_LOCAL_APPDATA`/`CSIDL_APPDATA` (-> `<dir>\Local`, `<dir>\Roaming`).
  `scripts/test-run.ps1` sets it for the test copy, so savegames, `ConfigUser.xml` and `achievements.bin` stay inside it and the real
  `%LOCALAPPDATA%\Risen` is not touched (checked by comparing file timestamps before and after a run).
* **Single-instance mutex** [confirmed in-game, M4c]: `SharedBase.dll` `g_CreateMutex(name)` (RVA 0xa9100) calls `CreateMutexA(NULL, TRUE, "GenomeMutex")` and treats `ERROR_ALREADY_EXISTS` (0xB7) as "another
  instance": a second `Risen.exe` exits within ~1 s, with no message. `[debug] instance = "<id>"` hooks `CreateMutexA/W` and `OpenMutexA/W` (kernel32, installed before the engine runs, the proxy `sentry.dll` is
  loaded first) and renames `GenomeMutex` to `GenomeMutex_<id>`: two (and four) games from separate copies then run side by side, each as fast as alone. Nothing else collided: the `.pak` volumes of the shared
  `data\common`/`data\compiled` are opened with read sharing, Steam stays out of it because `SteamAppId` is set, the AMD shader cache tolerates parallel processes. What a copy must own: `bin` (log, config, mods,
  `spass\saves`), `app_data` (savegames, `ConfigUser.xml`, `achievements.bin`).

## 16. The virtual file system and the packed data [confirmed in-game]

The engine already mounts every `*.pak` volume (`mountlist_packed.ini`, sections `[Physical]` and `[Packed]`), so the packed content is
listed and read through the engine's own exports instead of parsing the `FFC`/`G3V0` container (`risen.vfs`, `src/game/lua_vfs.cpp`).

* `eCVirtualFileSystem::FindFiles(dir, pattern, bTObjArray<bCString>&)` (Engine `0x734140`), `FindFilesWithExt(dir, ext, array)`
  (`0x733f70`) and `FindDirectories(dir, array)` (`0x733e50`). The array is `{ bCString* data; int count; int capacity }` (same layout as
  §12); the engine constructs `capacity` elements, so cleanup destroys `capacity` strings and frees `data` with `bCMemoryAdmin::Free`
  (exactly what `gCScriptAdmin::LoadAllScriptGame` does). Paths work with a mount prefix (`ini`, `scripts`, `data/common/...`) or with
  the `#G3:/` root; `#G3:/` itself lists the mount roots (`data`, `save`, `snapshots`, `appdata`, `ini`, `scripts`).
* Reading: `eCArchiveManager::GetInstance().GetFile(path, eCArchiveFile&)` -> `eCArchiveFile::Open(0 /*read*/, true)` ->
  `Read(buffer, count)` until it returns 0 -> `Close`. (`eCArchiveFile` is 0x50 bytes; it has a virtual destructor.)
  `CFXVirtualFileSystem::GetFileSize(path)` returned 0 for the files tried (also for plain files in `ini`), so it is not a size source; read
  until EOF instead.
* Layout of the packed data (`data/...`): `compiled/{strings,sounds,images,animations,speech_<language>,library,materials,navigation,...}`,
  `common/{templates,projects,meshes,physics,materials,library,...}`. `common/templates/<Group>/*.tple` are the entity templates (items,
  NPCs, monsters, lights, ...) whose names `Template(const char*)` accepts; `common/projects/<World>/` holds the world data (`*.wrl`, `*.sec`,
  `*.lrent`); `compiled/strings/*.tab` are the compiled string tables behind `loc.ini`.

## 17. Mounting directories and shadowing packed files [partly confirmed in-game]

* **Mounting works at runtime.** `eCVirtualFileSystem::MountDirectory(virtual, physical)` (Engine `0x734390`, protected but exported)
  calls `CFXVirtualFileSystem::MountDirectory(virtual, physical, flags)`; `flags` is `4` when the file system object's byte at `+0x19` is set,
  else `0` (we use the plain variant). The engine's own `Mount(section, EMount)` prepares each entry with `MakeAbsVirtualPath`, `MakeDir` and
  Windows separators; `risen.vfs.mount` does the same. `Unmount(virtual)` (`0x734310`) removes a mount (careful: it also removes the stock ones,
  `unmount("ini")` makes `ini/...` unreadable for the rest of the process).
* **Overlay semantics.** After `mount("vfsprobe", "<dir>")` the files of `<dir>` appear under `vfsprobe/...` in the listings *and* a file
  `<dir>/ini/loc.ini` is returned when the relative path `ini/loc.ini` is read (every spelling: `ini/...`, `#G3:/ini/...`), shadowing the packed
  or stock file; after `unmount` the original is back. New files are readable by their relative path too (`read`), but are not added to the
  stock directory listings (`find`/`exists` of the original directory do not show them).
* **Startup overlay.** `eCVirtualFileSystem::ProcessMountList(const bCString&)` (`0x735360`) runs once, for `mountlist_packed.ini`; hooking it
  and mounting after it makes an overlay active before the engine loads any resource (`mod.toml` `data_overlay = true`, experimental).
* **Templates cannot be shadowed this way.** The engine does open a loose `data/common/templates/Items/Weapons_Swords/It_1H_Sword.tple` from the overlay
  (seen with `risen.debug.trace_files`), but then terminates with a `Fatal error` dialog ("can't load ge3dialog resource dll, terminating
  process" -- the generic fatal-error handler) **whatever the content is**: a corrupt file, a copy of another template, a copy with one byte
  patched, and even a byte-identical copy of the stock file. Mounting after startup has no effect on templates (they are already loaded; a corrupt
  shadow file did not matter). So loose overlays are fine for files read through the archive manager (text, ini), not for the template database.
* **Template files** (`GENOMETP`): 8-byte magic, 0xDB-versioned header with the template GUID at `+0x10`, then a typed property stream that carries
  its own schema (names of property sets and properties as length-prefixed strings, e.g. `gCItem_PS`, `GoldValue`, `IconImage`, `eCMesh_PS`,
  `MeshFileName`). Values sit in an earlier block: the gold value of `It_1H_Sword` (50) is the int at byte offset 1261, the club's (8) at the same
  offset. Editing them is therefore possible offline, but the engine must be told about the file differently (not yet found).
* **Item data** (Script.dll `PSItem`, holders like the other `PS*`): `GetGoldValue`, `GetAmount/SetAmount`, `GetCategory`, `GetUseType`, `GetIconImage`,
  `IsMissionItem`, `IsValid` -> `Entity:item_value()`, `item_amount()`, ... (e.g. sword: value 50, icon `ic_it_1h_sword`; club: 8, `ic_it_1h_club`).

## 18. Inventory removal, quest log entries [confirmed in-game]

* **Removing items**: `PSInventory::FindStackIndex(const Template&, int start, bool skipEquipped)` (`0x223960`) returns the first stack of that template
  *after* index `start` (so the search begins with `-1`; `0` skips stack 0 and returns -1 for a one-stack inventory), `PSInventory::GetAmount(int stack)` (`0x226370`)
  the stack's amount and `DeleteItems(int stack, int count)` (`0x223fe0`) removes items from it -> `risen.item.take`. `PSInventory::CreateItems` returns the **index of the
  stack** the items went to (0 for the first stack), not an amount.
* **Quest log**: the log array of a `gCQuest` (`+0x50`) also receives entries from the engine itself: when a dialogue option that is bound to a quest (`Quest=<name>`)
  runs, the labels of its speakers (`FO_PC_Hero`, `FO_<NPC>`) are appended. `Quest:log_keys()` lists the localization key of every entry
  (our own entries are `<Quest>_Log<N>`).
* **A complete example** that exercises all of this lives in `mods/fetchquest` (play-through and save/load check: `mods/fetchquest_driver`,
  `scripts/smoke-test.ps1 -Scenario fetchquest`).

## 19. NPC properties, guilds and peaceful NPCs [confirmed in-game]

* **Property sets by name**: `eCEntity::GetPropertySet(const bCString& className)` (Engine `0x21e4a0`) returns an entity's property set by its engine class
  (`gCNPC_PS`, `gCInventory_PS`, `gCSkills_PS`, `gCItem_PS`, `gCScriptRoutine_PS`, ...); the property set is a `bCObjectBase`, so the generic
  `bCAccessorProperty` mechanism (§11) reads and writes its properties in the engine's text format: `Entity:get_property(set, name)` / `set_property`.
  `gCNPC_PS` properties seen: `Guild`, `Species`, `AttitudeLock`, `Gender`, `GuardStatus`, `Group`, `Discovered`, `Ransacked`, `DefeatedByPlayer`, `BraveryOverride`,
  `DamageCalculationType`, `LastFightAgainstPlayer`, `Voice`. Enum value names are plain strings in `Game.dll` (`gEAttitude_{None,Neutral,Friendly,Angry,Hostile}`,
  `gEGuardStatus_{Active,Inactive,Behind,FirstWarnGiven,SecondWarnGiven}`, `gEBraveryOverride_{None,Brave,Coward}`, `gESpecies_*`, `gEFight_*`).
* **Guilds** (`gEGuild_*` = the number `PSNpc::PropertyGuild` stores): None 0, Don 1, Dig 2, Grd 3, Cit 4, Inq 5, Mag 6, Pir 7 (plus `Count`).
  Spawned `Peasant_1_01` is a `Grd`. A pirate spawned next to one NPC of each guild attacked Don (knocked out), Grd, Inq and Mag; Dig, Cit and None stayed in `ZS_Watch_Loop`.
* **`AttitudeLock = gEAttitude_Friendly` does not make an NPC peaceful** (it was still attacked and fought back). Use the guild: `risen.npc.spawn{ peaceful = true }`
  sets `Cit`.
* **`Entity::Kill`** removes the entity from the world completely (`risen.entity.get` no longer finds it); it is not a combat death (no `npc.killed`).

## 20. Reflection and dialogue commands [confirmed in-game]

* **Property reflection**: constructing `bCAccessorProperty(const bCObjectBase*)` (SharedBase) yields the object's *first* property; `IsValid()`, `GetPropertyName()`,
  `GetPropertyType()` and the prefix `operator++` walk all of them. This works on any `bCObjectBase`: entity property sets, quests, infos and info commands
  (`list_properties`, `Info:list_command_properties`). The complete result for the entity sets, quests, infos and all commands is [`PROPERTIES.md`](PROPERTIES.md)
  (generated by `tools/gen_properties_doc.py` from `mods/propdump`).
* **Dialogue commands** are configured through those properties: `Give` = `Entity1, Entity2, Item, Amount` (`Item` is the plain template name as text, `Entity1/2` accept the keywords
  `player` / `npc` or an entity name; verified: an option with `Give(npc -> player, It_Apple, 2)` delivers two apples), `GiveXP` = `Amount`, `SetGameEvent` / `ClearGameEvent` =
  `GameEvent` (SetGameEvent creates the game variable of that name with value 0; ClearGameEvent removes it), `RunQuest/SucceedQuest/CloseQuest` = `QuestName`, `MoveTo`, `Sleep`, `SetTradeEnabled`
  (`Entity1`, `TradeEnabled`), `SetRoutine` (`Entity1`, `RoutineName`), ... Texts (`gCInfoLocString`) are not settable through the text format; they need `CFXLocString::SetKey` on the member
  returned by the command's `AccessText` (three variants: `Say`, `Description/ShowSubtitle/AddLogText`, `SaySVM/PickPocket`). Commands are executed one after the other over time,
  not all at once (the apples arrive a few seconds after the option was chosen).

## 21. Branching conversations [confirmed in-game]

* `gEInfoType` numbers: Refuse 0, Important 1, News 2, **Info 3**, Parent 4, Comment 5. `gCInfoSystem::HasInfos` ignores Comment (5), so an NPC whose only infos
  are comments is not talkable; a parent option must be a normal `Info`.
* `Parent` is a name (`gCInfo+0x38`); the parent's child list (`+0xd8`, count `+0xe0`) is filled by `gCInfoSystem::RefreshParentChildInfos()` (Game `0x8b1790`)
  (`risen.dialog.rebuild` calls it). `gCInfoSystem::OnInfoFinished` (`0x8afcb0`): after an info ran, if it has available children (`AreChildInfosAvailable`) the menu is
  rebuilt from `GetChildInfoList` (children with Type 3 whose conditions hold), otherwise the owner's top-level list is rebuilt. Children stay offered after a permanent
  child; the `Back` command (child of a parent -> its parent's list becomes the top level again) returns to the top-level list. The children appear only *after the parent's
  lines finished playing* (about 11 seconds for two lines).

## 22. Entity templates at runtime [confirmed in-game unless marked]

Everything below is Engine.dll (RVAs of the 64-bit Risen Enhanced build), driven by `src/game/lua_template.cpp`; live tests: `mods/templatepatch` (the recommended
pattern) and `mods/templatepatch_driver` (the savegame/instance scenarios, run by `scripts/smoke-test.ps1 -Scenario templates`).

**Reaching a template.** The engine keeps one `eCTemplateEntity` (an `eCEntity`, vftable `0x9d14d0`, size `0xa8`) per template name. `Template(name)` (Script.dll, §9)
is an `eCTemplateEntityProxy`: its first qword points to an `eSEntityID`, whose first qword is the `eCNode*`. `eCTemplateEntityProxy::GetEntity()` (`0x6b46b0`) returns the
`eCTemplateEntity*` (null for a *multi template*). The pointer stays valid for the whole process; we still resolve it per call. `Template::IsValid` is **false before the
first `tick`** (the template database does not exist yet at `mods.loaded`; it does at the first `tick`, which fires once at the main menu, and in `load.before`). Related
(decompiled, not needed): `eCSceneAdmin::GetTemplateEntityByName` (`0x6996e0`), `eCTemplateEntityProxy::SetEntity(bCString)` (`0x6b44e0`), `GetRefTemplate` (`0x6adfc0`; none of ~300
sampled item templates has a reference template).

**Layout seen in the decompilation** (`eCTemplateEntity`, offsets from `this`): `+0x10` reference template (parent), `+0x20/+0x18` child/sibling list (multi templates), `+0x28`
entity flag word (`& 8` makes `PatchEntity` refuse), `+0x30` property-set type table (byte per `eEPropertySetType`, `0xff` = none), `+0x40` array of `eCEntityPropertySet*`,
`+0x48` count, `+0x50` `bCDateTime` "modified" stamp (a FILETIME), `+0x58` flags (bit 1 = content loaded, `IsContentLoaded` `0x223030`), `+0x60` reference template ID, `+0x68` name
info (`GetName` `0x223040` returns `**(this+0x68)`), `+0xa0` `eSEntityID*`.

**Lazy loading.** Item templates are loaded at startup; **NPC and monster templates are not** (`IsContentLoaded` is false for `Peasant_1_01`, `Wolf` and 102 of 103 monsters at the main
menu). `GetPropertySetCount/At` are bare array reads and load nothing, `GetPropertySet(className)` goes through a virtual loader (slot `0x110`) and does;
`eCTemplateEntity::ReadAll()` (`0x6ad6d0`, exported) loads the `.tple` file and sets the flag. `Template:property_sets()/list_properties/get_property/set_property` call `ReadAll` first.

**Property sets are plain `bCObjectBase`s.** `eCEntity::GetPropertySetAt(i)` (`0x21e430`) + `eCEntityPropertySet::GetPropertySetClassName` (`0x6b0600`, vtable `+0xf0`) list the
class names; the §11/§20 accessor mechanism (`bCAccessorProperty`) reads and writes them exactly as it does for live entities. Examples: `It_1H_Sword` = `eCMesh_PS,
eCRigidBody_PS, eCCollisionShape_PS, gCItem_PS, gCInteraction_PS, gCDamage_PS, eCIlluminated_PS, eCStrip_PS`; `Peasant_1_01` = `gCNavigation_PS, gCDynamicCollisionCircle_PS,
eCRigidBody_PS, eCCollisionShape_PS, gCCharacterMovement_PS, gCNPC_PS, gCInventory_PS, gCScriptRoutine_PS, gCInteraction_PS, gCDamage_PS, gCDialog_PS, eCIlluminated_PS,
gCParty_PS, gCEffect_PS, eCAnimation_PS, gCSkills_PS, gCCombatSystem_PS`. `gCItem_PS` properties: `Amount, GoldValue, MissionItem, Permanent, SortValue, Category, IconImage,
HoldOffset, Dropped, ItemWorld, ItemInventory, Spell, RequiredSkills (bTObjArray<gCSkillValue>), ModifySkills (bTObjArray<gCModifySkill>), UseType, CanEquipScript, EquipScript,
UnEquipScript, EffectMaterial, HoldType, IsDangerousWeapon, CombatHitRangeOffset`. **`gCSkills_PS` has no reflected properties** (hit points, level and skills cannot be edited through the
property mechanism; M3 needs another route). Array properties read back only as `Array[n]` (solved natively in M2: section 23).

**What an edit affects** (all verified with `It_1H_Sword.GoldValue` 50 -> 777 and the `gCNPC_PS.Guild` of `Peasant_1_01`/`Wolf`):
* **new spawns** (`Entity::Spawn`): yes, at once.
* **items given to an inventory** (`PSInventory::CreateItems`): yes. An inventory holds *references to templates plus an amount*, not item entities:
  `PSInventory::GetTemplateItem(stack)` (`0x2262e0`, `Entity:stack_item`) returns the **template entity itself** (its `GetCreator()` is null, its name is the template name), so
  every stack, including ones that existed before the edit, shows the new value immediately.
* **entities that already exist** (world items, NPCs, earlier spawns): **no**, each entity owns *copies* of the property sets (the sword spawned before the edit kept 50).
* **a world restored from a savegame, and the world of a new game**: **no**, the saved/level values win (see below).

**Updating existing entities.** The engine's `eCTemplateEntity::PatchInstances()` (`0x6ae510`, bool) walks the scene (roots = the scene admin's dynamic entity contexts) and calls
`eCEntity::PatchWithTemplate(tpl, false)` for every entity whose `GetCreator()` (virtual, vtable `+0x170`; `eCDynamicEntity::GetCreator` `0x21cd10` reads the creator proxy at `+0x1d8`)
is the template. After a plain edit that does **nothing**: `eCEntity::OnPatchWithTemplate` (`0x6a56b0`, vtable `+0x1b8`) first compares the entity's `+0x50` stamp with the template's
and skips the patch when they are equal (an editor/versioning mechanism: a template saved with a new date re-patches old entities). Writing a different stamp into the template makes
`PatchInstances` work (tested, then dropped from the API in favour of the direct call). `eCTemplateEntity::PatchEntity(entity, bool replaceAll, bool)` (`0x6af050`, exported) does the real work
and can be called directly (it ignores the stamp): for every template property set, a set with `IsReferencedByTemplate()` (set vtable `+0x110`; default **true**) or `replaceAll` is **replaced by
`Clone()`** (`+0x188`) of the template's set, a set the entity lacks is cloned in if `IsCopyable()` (`+0x120`), otherwise `bCObjectBase::CopyFrom(set, true)` (vtable `+0x68`) merges.
Verified effect of the full patch (`Template:patch_instances()` = `PatchEntity` on every instance): the edited value arrives, **but per-instance state inside the template's sets is reset**:
a wolf's hit points 7/180 -> 180 and `gCNPC_PS.Group` "TPG" -> "" (its inventory stack stayed), an `It_Gold` with `Amount` 25 -> 1. Hence the recommended propagation is **targeted**: write only
the edited properties into each instance with the ordinary property accessor (`risen.template.patch(name, sets, { instances = true })`); verified: on all 97 wolves `Guild` changed while the
damaged wolf kept `hp=7/180` and `grp=TPG`.

**Savegames** (same process and fresh process both tested, `It_Plant_Berry.GoldValue` on 149 world items and `Wolf` `Guild`):
* A savegame stores the property values of its entities. After a load they hold **the saved values, not the current template's**, whether the template still carries the edit or not and
  whatever the stamps are (template edited to 555 while the saved world held 999: the world stays 999; the engine only copies the template's stamp into the entity). The same holds for the
  world of a **new game**: after editing the template, a second new game still created the 149 berries with the original value.
* Consequently values written into entities (also with `instances = true`) are **saved with the game** and are still there in a fresh process even if the mod does nothing at load; a mod that
  is removed later leaves its patched values in old savegames.
* Templates keep their edits across `quick_load` and new games inside one process (they are not reloaded); a new process starts with the shipped values, so **re-apply on every start**.
* Entities created *after* an edit (spawns, inventory stacks) need nothing.

**Counting and listing instances** (`risen.template.census`, `Template:instances/instance_count`): `eCSceneAdmin::GetDynamicEntityContextCount/At` (exports) give the contexts (~315 in the
start world, ~48 000 entities, 1165 templates in use, about a millisecond per walk); the scene admin is `eCModuleAdmin::GetInstance().FindModule("eCSceneAdmin")` (`0x4cd1f0`, class-name
overload; the engine's own accessor `FUN_1800957e0` does that plus an RTTI cast). Per context: root entity at `+0x40`, first child `+0x20`, next sibling `+0x18`. (An earlier walk capped at 64
contexts saw only part of the world: never cap the contexts.) Without a world the walk finds nothing (0 / empty table).

**Recommended pattern** (`mods/templatepatch`): apply the edits (`risen.template.patch(name, sets)`) on the **first `tick`**, the earliest hook the Lua API offers (it also covers a savegame
loaded from the menu), and again with `{ instances = true }` on every `session.start` (new game *and* load: the world entities of both carry their own copies). `mods.loaded` is too early.

**Not done / inferred:** (cloning a template under a new name is section 25); array properties
(`RequiredSkills`, `ModifySkills`: section 23) and non-reflected sets are untested; which property sets a savegame persists is only known for `gCItem_PS` and `gCNPC_PS`; multi templates
(`GetEntity` null) are refused with a message.


## 23. Item balancing: skill arrays, equip conditions, damage [confirmed in-game unless marked]

Driven by `src/game/lua_balance.cpp`; live tests `mods/balance_items` (the recommended pattern, verification lines) and `mods/balance_driver` (save/load), run by
`scripts/smoke-test.ps1 -Scenario balance`; the full per-property catalogue of all 606 item templates is [`ITEM_PROPERTIES.md`](ITEM_PROPERTIES.md)
(`mods/itemdump` + `tools/gen_item_properties_doc.py`). RVAs are of the 64-bit Risen Enhanced build.

**Where an item's numbers live** (every item template: `gCItem_PS` + `gCInteraction_PS` + visual/physics sets; weapons add `gCDamage_PS`, runes/scrolls `gCCastInfo_PS`, recipes `gCRecipe_PS`):

| What | Where |
|---|---|
| gold value | `gCItem_PS.GoldValue` (long) |
| weapon damage | `gCDamage_PS.DamageAmount` (long; 10..100 on the shipped weapons, arrows 20) + `DamageMultiplier` (float, always 1) + `DamageBonus` (0) + `DamageType` (`gEDamageType_Edge/Blunt/Point`...) + `DamageScript` (`DoSwordDamage`, `DoAxeDamage`, `DoStaffDamage`, `DoFistDamage`, `DoProjectileDamage`) |
| equip condition | `gCItem_PS.RequiredSkills` (array of `gCSkillValue`: weapons Strength 5..100, bows Dexterity 10..80, runes `Circle` 1..4) + `CanEquipScript` (`CanEquipMeleeWeapon`, `CanEquipRangedWeapon`, `CanEquipShield`, `CanEquipHasRequieredStats`) |
| armor/shield/helmet/ring/amulet protection and bonuses | `gCItem_PS.ModifySkills` (array of `gCModifySkill`): `Prot_Edge/Blunt/Point/Fire/Ice/Magic` (armor 5..80, shield 2..10, helmet 1..10), attributes, `Combat_*` |
| potion/food effect | `gCItem_PS.ModifySkills`: `Atrib_HP`/`Atrib_MP` = **current** hit/mana points (heal: potions 50..300, food 5..200), `Stat_HP`/`Stat_MP` = **maximum** (permanent potions +10/+5), `Stat_STR`, `Stat_XP`... |
| spell cost | `gCCastInfo_PS.ManaCost` (runes 0..80, teleport runes 0; scrolls about half of the rune) |
| use type / category / hold type | `gCItem_PS.UseType`, `Category`, `HoldType` (enums); `EquipScript`/`UnEquipScript` (`OnEquipApplayStats`, `OnEquipRing`, `OnEquipMeleeWeapon`, ...) |

There is **no durability and no weight** property on any item. All 606 `Items/*` templates were read through `Template:list_properties` (`mods/itemdump`, 53 872 property lines): 598 have `gCItem_PS`
(the 8 without are `It_Cast_Light` and the `It_Projectile_*` objects), 107 `gCDamage_PS`, 71 `gCCastInfo_PS`, 86 `gCRecipe_PS`, 17 `gCProjectile2_PS`; the rest is visual/physics (`eCMesh_PS`, ...).

**The arrays** (Game.dll). `bTObjArray<T>` is `{ T* data; int count; int capacity; }` with the elements stored **inline** (no pointers). `gCItem_PS` layout: `RequiredSkills` at `+0x70`, `ModifySkills` at `+0x80`
(`GetRequiredSkills` `0x4a7aa0` / `GetModifySkills` `0x4a8c60` just return `this + offset`; `gCRecipe_PS` and `gCQuest.ModifySkillsSuccess` use the same element types).
* `gCSkillValue` (size `0x20`): vftable `+0`, `bTPropertyContainer<gESkill>` sub-object vftable `+8`, `Skill` (int) `+0x10`, `Amount` (int) `+0x18`. `gCModifySkill` (size `0x30`): the same plus a `bTPropertyContainer<gESkillModifier>` vftable `+0x20` and `Modifier` (int) `+0x28`.
  Constructors/destructors are exported (`gCSkillValue::gCSkillValue()` `0x976a60`, `~` `0x4b0630`, `gCModifySkill()` `0x976ab0`, `~` `0x4b0990`; also `(skill, amount[, modifier])` constructors).
  Both are `bCObjectBase`s with the reflected properties **`Skill`, `Amount`** (and **`Modifier`**), so `bCAccessorProperty` reads/writes an element's fields and **parses/prints the enum names** (`gESkill_Stat_STR`, `gESkillModifier_AddValue`) -- no enum table is needed.
  The text format of the array property itself (`Array[n]`) is read-only; there is no element syntax.
* **Reading**: take `data`/`count` from `gCItem_PS + offset` and run the accessor on `data + i * stride`. **Writing**: build the elements in a scratch buffer with the engine's own constructors (default ctor + accessor `Skill`/`Amount`/`Modifier`, then read back to validate the names),
  put `{ data, count, count }` in a stack `bTObjArray` header and call **`gCItem_PS::SetRequiredSkills`** (`0x4fd310`) / **`SetModifySkills`** (`0x4fd360`): they call the property-set change hooks (vtable `+0xb0`/`+0xb8` with the property name) around the array copy-assignment
  (`FUN_18050b1a0` for `gCSkillValue` is decompiled: resizes the destination, re-constructs each element in place and copies `Skill`/`Amount`; `FUN_1804b7f00` for `gCModifySkill` has the same shape, not read, and round-trips `Modifier` in the tests). The source array is only read, so a fake header over our buffer works; afterwards our elements are destroyed with the exported destructors.
  Verified on templates, on spawned item entities and on every live entity of a template (149 berries).
* `gESkill` values (read back through the accessor: `risen.item.skill_id`): 0 `Atrib_HP` (current hit points), 1 `Atrib_MP` (current mana), 2 `Stat_LV`, 3 `Stat_XP`, 4 `Stat_LP`, 5 `Stat_HP` (maximum), 6 `Stat_MP`, 7 `Stat_STR`, 8 `Stat_DEX`, 9 `Stat_INT`,
  10 `Prot_Edge`, 11 `Prot_Blunt`, 12 `Prot_Point`, 13 `Prot_Fire`, 14 `Prot_Ice`, 15 `Prot_Magic`, 16 `Combat_Sword`, 17 `Combat_Axe`, 18 `Combat_Staff`, 19 `Combat_Bow`, 20 `Combat_CrossBow`, 21 `Magic_Circle`, 22 `Magic_Fireball`, 23 `Magic_Frost`,
  24 `Magic_Missile`, 25 `Misc_Smith`, 26 `Misc_Mining`, 27 `Misc_Lockpick`, 28 `Misc_Pickpocket`, 29 `Misc_Sneak`, 30 `Misc_Acrobat`, 31 `Misc_Alchemy`, 32 `Misc_Scribe`, 33 `Misc_Trophy`; `gESkill_None` = -1 (the engine rejects anything `>= 0x22`).
  Names found as strings in Game.dll `0xc29c08..`. `gESkillModifier`: 0 `AddValue`, 1 `SetToMax`, 2 `SetToValue`, 3 `AddPercentOfMax` (`gCSkills_PS::ModifySkillValue` `0x977d30`: `AddValue` adds `Amount` to the skill's base value, `SetToMax` the maximum, `SetToValue` the amount, `AddPercentOfMax` percent of the maximum).
* `gCSkills_PS` keeps per skill a base value (`+0x20 + 4*skill`) and a "modification" value (`+0xa8 + 4*skill`); `PSSkills::GetTotal(skill)` (`0x2350f0`, Script.dll) = base + modification (`gCSkills_PS::GetSkillValue`; `HasRequiredSkills` additionally clamps to the skill's min/max from `ms_arrSkillInfos`). `PSSkills::GetHitPoints` is `GetTotal(0)`, `GetHitPointsMax` `GetTotal(5)`, `GetStrength` `GetTotal(7)`,
  `GetProtection(damageType)` `GetTotal(10..15)`. This is what `Entity:skill_total(name)` returns (total, base via `GetBase`).

**How the numbers are used** (decompiled, Game.dll / Script.dll / Script_Game.dll -- the third was imported into its own Ghidra project `B:\DevDrive\risen-ghidra\ScriptGame`, helper `run-dump-sg.ps1`):
* *Equip check*: `gCSkills_PS::HasRequiredSkills(required array, missing*)` (`0x977960`): for each element `skill < 0x22`, total = clamp(base + modification, min, max); missing when `Amount - total > 0`; `missing == nullptr` makes it a plain bool. `gCItem_PS::HasRequiredSkills(entity, missing&)` (`0x8c5540`)
  forwards `this + 0x70`; Script.dll `PSItem::HasRequiredSkills` (`0x226b60`) wraps it. The script `CanEquipHasRequieredStats` (Script_Game `0xf2190`; `CanEquipMeleeWeapon`/`CanEquipRangedWeapon`/`CanEquipShield` delegate to it) runs the check **only for the player**
  (`Entity::IsPlayer`) and prints the HUD message `HUD_MissingSkill` ("$(name)") per missing skill; **NPCs always pass**. `gCInventory_PS::CanEquipStackToSlot` (`0x8c4d70`) calls the item's `CanEquipScript`. Verified live: the same `risen.item.can_use(player, item)` flips with the edited requirement
  (500 -> false, 1 -> true, none -> true) and `Entity:equip_all()` (which goes through the CanEquip scripts) skips a two-hander that needs 999 Strength and equips it (+1) once the requirement is lowered.
* *Equip effect*: `OnEquipApplayStats` / `OnEquipRing` etc. call `PSItem::ApplyModSkills` -> `gCItem_PS::ApplyModSkills(entity)` (`0x8c54a0`): for every `ModifySkills` element `SetSkillModifyValue(skill, modification[skill] + Amount)` (the `Modifier` is ignored here); `OnUnEquipUnApplayStats` -> `UnApplyModSkills` (`0x8c5400`) **subtracts the item's current
  amounts**. Hence editing the modifiers of an item that is currently worn makes the next unequip drift by the difference (the world keeps the applied sums in the character's skill values, which are saved): change modifiers before anything is worn (first `tick`), or take the item off and put it on again around the edit. Verified: wearing the edited
  `It_Armor_Guard` (`ProtEdge 35`) raised the player's `Prot_Edge` total from 0 to 35.
* *Use effect*: `gCInventory_PS::ConsumeItems(stack, count)` (`0x8c5140`) calls `gCSkills_PS::ModifySkillValues(item ModifySkills, true)` (`0x977f30`; base value changes with the `Modifier` semantics above, HUD game-log line for the player) and then `DeleteItems`. `PSInventory::UseStack` (`0x224120`) is only the equip/unequip
  toggle; the player's "use" from the inventory goes through `PSInteraction::PlayerUseStack` (`0x221c40`, `gCInteraction_PS::BeginInteraction(item, 2, stack)`). `risen.item.consume` calls `PSInventory::ConsumeItems`: a potion edited to `HitPoints = 40` healed 10 -> 50 (max 80), shipped value 75.
* *Weapon damage*: `DoSwordDamage` (Script_Game `0xd90c0`), `DoAxeDamage` (`0xd92c0`), `DoStaffDamage` (`0xd94c0`), `DoFistDamage` (`0xd96c0`) all compute `int(DamageMultiplier * (DamageAmount + attacker.GetTotal(Strength)))` with +50 (`0x32`) while the attacker has the "Berserker" status effect, then pass it on to the generic damage script
  (which applies the protection skill of the `DamageType`: `PSSkills::GetProtection`). `DoProjectileDamage` (`0xdacc0`) forwards through script chains using the projectile's `DamageMultiplier` and the shot strength (`(arg + 400) * 5`); the ranged formula is **not fully decoded** (inferred: bow `DamageAmount` + arrow `DamageAmount`, Dexterity-related).
  The damage numbers themselves were **not** measured in a fight (only the template/instance values were read back).

**Behaviour of edits** (same as RE.md §22 because the arrays are just more properties of the template's `gCItem_PS`): new spawns, items given and inventory stacks follow the template at once (stack items are the template entity); existing entities, savegame worlds and new-game level data keep their own copies,
so `instances = true` rewrites them. Round trip verified with `mods/balance_driver` (`It_Plant`, 144 world instances, `ModifySkills` `HitPoints +5` -> `Strength +7` and value 10 -> 77 on template and instances): after a quick save, editing the **template back** and quick-loading, the loaded world still holds the saved values
(`77 | Strength+7` x144, template `1 | -`): arrays are saved with the entities like plain properties; a new game afterwards has the level's shipped values (`10 | HitPoints+5` x144) while the template keeps the edit. So a balancing mod must write the instances on every `session.start`, and a removed mod leaves its values in old saves.
`risen.balance.install` does exactly this (first `tick` templates, every `session.start` instances); `mods/balance_items` shows it end to end (spawn, inventory stack, 149 world berries, engine check, equip, consume).

**Implementation notes.** Entity handle → `gCItem_PS` via `eCEntity::GetPropertySet("gCItem_PS")`, template name → `templatePropertySet` (lua_template.cpp). `SetRequiredSkills` takes the array by const reference, so the scratch header lives on the stack; maximum 64 entries (scratch buffer).
`sol::variadic_args` must not be default-constructed (iterating it crashes). New symbols: `skillvalue_ctor/dtor`, `modifyskill_ctor/dtor`, `item_set_required_skills/modify_skills`, `skills_has_required`, `inv_consume_items`, `sk_get_total/base` (all optional).

**For the next milestones.** M3 (NPCs): `gCSkills_PS` is not reflected, but the same skill ids apply: `PSSkills::GetTotal/GetBase/SetBase`-style functions exist in Script.dll (`SetBase(skill, value, modifier)` `0x235180`, `SetCombatSword...`), `gCSkills_PS::SetSkillBaseValue`/`ModifySkillValue(gCModifySkill, bool)` (Game.dll, `0x977d30`) take a `gCModifySkill` element,
which the element construction here can build; `Entity:skill_total` is the read side. Quests (`gCQuest.ModifySkillsSuccess`) and recipes (`gCRecipe_PS.RequiredSkills` at a different offset, `SetRequiredSkills` `0x5d17a0`) use the same array types. M6 (spells): `gCCastInfo_PS.ManaCost` is a plain property (`mana_cost` in the items section);
the required circle of a rune is its `RequiredSkills` (`Circle`). The `risen.balance.register_section` hook is where `npcs`/`spells` plug in.

## 24. NPC and monster balancing: gCSkills_PS, kill experience, damage [confirmed in-game unless marked]

Driven by `src/game/lua_npc_balance.cpp` (natives) and `src/game/npc_balance_lua.h` (the Lua part); live tests `mods/balance_npcs` (templates, spawns, world, validation, combat) and `mods/balance_npcs_driver` (save/load),
run by `scripts/smoke-test.ps1 -Scenario npcs`; the catalogue of all 363 NPC/monster templates is [`NPC_PROPERTIES.md`](NPC_PROPERTIES.md) (`mods/npcdump` + `tools/gen_npc_properties_doc.py`). RVAs are of the 64-bit Risen Enhanced build.

**Where an NPC's numbers live.** Every NPC/monster template carries (`Peasant_1_01`, `Wolf`, ...): `gCNavigation_PS, gCDynamicCollisionCircle_PS, eCRigidBody_PS, eCCollisionShape_PS, gCCharacterMovement_PS, gCNPC_PS, gCInventory_PS,
gCScriptRoutine_PS, gCInteraction_PS, gCDamage_PS, gCDialog_PS (humans), eCIlluminated_PS, gCParty_PS, gCEffect_PS, eCAnimation_PS, gCSkills_PS, gCCombatSystem_PS`.

| What | Where |
|---|---|
| hit points (current, maximum), mana, level, attributes, protection, combat/magic/misc skills | `gCSkills_PS` -- **not reflected**, see below |
| guild, species, attitude lock, bravery override, damage calculation type, AI group | `gCNPC_PS` (`Guild`, `Species`, `AttitudeLock`, `BraveryOverride`, `DamageCalculationType` = `Normal`/`Monster`/`Immortal`, `Group`; plus run-time state: targets, timestamps, `GuardStatus`) |
| fight style | `gCCombatSystem_PS.CombatSpecies` (a name into `gCCombatAdmin`: `GetInstance()` + `GetSpecies(bCString)` -> a reflected `gCCombatSpecies` with `LeftFistItem`, `RightFistItem`, distances, animations), `ActiveFightAI`, `PassiveFightAI` |
| the NPC's own damage | `gCDamage_PS` (`DamageAmount`, `DamageMultiplier`, `DamageType`, `DamageScript`): 0 / none for almost everybody, `Titan` 100 Edge `DoTitanDamage`, `Inferno` 200 Fire `DoInfernoDamage` |
| starting inventory | `gCInventory_PS` stacks (not reflected): `PSInventory` works on the *template entity* as on a live NPC (`GetStackCount`, `GetTemplateItem`, `GetAmount`, `CreateItems`, `FindStackIndex`, `DeleteItems`), a spawn clones the set. Only 67 of 363 templates carry items (`Gnome`: `It_1H_Sickle`, ...) |
| movement | `gCCharacterMovement_PS` (`ForwardSpeedMax` 400 for humans, jump/climb/water values), plain properties, not tested |

**gCSkills_PS storage** (Game.dll, decompiled: `SetToDefaults` `0x9770f0`, `OnRead/OnWrite` `0x976c70/0x976c20`, `Read/WriteSaveGame` `0x976bb0/0x976b30`, `OnCopyFrom` `0x976d60`, `SetSkillBaseValue` `0x977b60`, `SetSkillModifyValue` `0x977410`, `GetSkillValue` `0x978070`,
`GetSkillMaxValue` `0x9780d0`, `GetNativePropertyCount` returns 34): the property set has **no reflected properties** (`GetNativePropertyAt` builds one converter per skill, the generic accessor does not list them), but the object is plain data:
* **base values** `int[34]` at `this + 0x20 + 4 * skill`, **modifications** `int[34]` at `this + 0xa8 + 4 * skill` (worn items, effects). `.tple` files hold the base block (`OnRead` copies `0x88` bytes into `+0x20`; version 4 format), a savegame stores both blocks
  (`WriteSaveGame`, `GetSaveGameRelevance` 1: saved with the entity, like the other relevant sets), `OnCopyFrom` (used when a spawn clones the template's set) copies the **base block only**. `gESkill` ids as in section 23 (0 current hit points, 1 current mana, 2 level, 3 experience, 5/6 maximum hit/mana points, 7..9 attributes, 10..15 protection, 16..20 combat skills, 21..24 magic circles, 25..33 misc).
* **limits** `gCSkills_PS::ms_arrSkillInfos` (exported data, array of `0x80`-byte `gSSkillInfo`: `+0` min, `+4` max, `+8` default, `+0xc` category, `+0x18` tag string, `+0x20` loc string): attributes 0..200, combat skills 0..10, `Circle` 0..4, misc skills 0..1 or 0..3, level min 1,
  protection min -1, hit/mana unlimited maxima, experience max 14000. `GetSkillMaxValue`: current hit points are bounded by `GetSkillValue(Stat_HP)`, mana by `Stat_MP`, `Stat_XP` by a function of the level, everything else by the table (`<= 0` = unlimited).
  `GetSkillValue` = clamp(base + modification).
* `SetSkillBaseValue(skill, value)` clamps to the limits, ignores no-op writes, tells a script about changes of `PC_Hero` (the player), calls `ms_pSkillChangedCallback`, and **re-clamps the current hit/mana points when the maximum changed** (lowering `Stat_HP` lowers `Atrib_HP`; raising it does not raise it).
* **Native route used by the API**: for an NPC *template* the property set is the real object, so `risen.npc.skill_raw/set_skill_base(name, ...)` read/write the two arrays directly (writes clamped like the engine does); for a *live entity* the exported `SetSkillBaseValue` (`?SetSkillBaseValue@gCSkills_PS@@QEAAXW4gESkill@@J@Z`) is called on its set.
  Verified against `PSSkills` getters on spawns (`hp_max`, `level`, `skill_total("Strength")`): a spawn from an edited template has exactly the template's base values.

**Template edits survive the spawn** (verified): `Wolf` hit points 180 -> 500, level 6 -> 20, Strength 30 -> 90, Prot_Edge 24 -> 60 (template storage), then `Entity::Spawn("Wolf")`: 500/500, 20, 90, 60 immediately and still 500/500 after 300 frames (checked by the smoke test) while the wolf ran its AI state
(`ZS_Attack_CombatMove`; an earlier manual run also saw `ZS_GotoBody_Eat_Loop` and fights, not re-measured). The spawn path clones the property sets (`gCSkills_PS::OnCopyFrom`); no init script (`Assess*`, `ZS_*`) rewrites hit points or attributes. Inventory items put into the template (`PSInventory::CreateItems` on the template entity)
arrive in spawns (`Wolf` + 3 apples: `risen.item.count(wolf, "It_Apple") == 3`).

**Kill experience** (Script_Game.dll, decompiled): `Kill` (`0xc9f70`) runs the script `GiveXP(killed, killer, -1)` for a kill by the player/party. `GiveXP` (`0xb1f10`): with amount -1 the reward is `10 * max(level, 1)` where level = `PSSkills::GetTotal(killed, Stat_LV)`; at least 50, or at least 10 when
the script `IsSmall` (`0xb5c30`: species `Chicken` or `Nautilus`) holds; the amount goes to the player through `PSSkills::SetExperience`. Verified by running the real script: `risen.script.call("GiveXP", wolf, player, -1)` with a level-20 wolf raised the player's experience 0 -> 200. So **the experience of a kill is derived from the level**;
there is no stored reward. The level is read by the XP scripts (`GiveXP`, `GetXPNext`, `GetXPMax`); the damage scripts decompiled for this section do not read it.
**Not measured:** a kill by a real combat death (the same script, called directly).

**Damage** (decompiled `DoDamage` `0xd8060`, `DoSwordDamage`/`DoFistDamage` etc. section 23, `DoSpecialMonsterDamage` `0xd98c0`, `DownOrDeadByMeleeWeapon` `0xd89d0`): the target's hit points drop by `max(dmg - Protection(damage type), DamageMultiplier * minimum-fraction)` (`PSSkills::GetProtection`); `DamageCalculationType` 2 (`Immortal`; decompiled, not tried in a fight) makes the damage 0,
a party member of the player takes half. Melee `dmg = DamageMultiplier * (DamageAmount + attacker Strength)`; the weapon is the *item*: for animals and monsters the fist item of their combat species (`Wolf`/`Boar`/... `RightFistItem` = `It_Fist_Monster`, `DamageAmount` 0, `Edge`; `Ghul` also `It_Fist_Monster_Frost` 100 Ice;
humans `It_Fist`/`It_Fist_Left` 0 Blunt; lizards `It_2H_Obsidian_Sharp` 90; ogres `It_Axe_Ogre` 100), so a monster's bite is its **Strength** minus the victim's protection of that damage type. `DoSpecialMonsterDamage` uses the NPC's own `gCDamage_PS.DamageAmount * DamageMultiplier` (Titan, Inferno).
**Verified in a fight** (`mods/balance_npcs`): a wolf with Strength 30 against `Peasant_1_02` (Prot_Edge 20) took off 10 hit points with its first bite (30 - 20), a wolf with Strength 90 took 70 (150 -> 80). Combat species edits (`risen.npc.set_combat_species_property`, e.g. `Goblin.RightFistItem`) read back correctly; their effect on a running fight was not measured.

**Propagation, savegames** are the same as for items (section 22/23) and were verified with `mods/balance_npcs_driver` on `LizardWarrior` (78 world NPCs): `set_stats(..., { instances = true })` rewrote all 78 (a damaged one kept its 3 hit points); after a quick save, editing the **template back** and quick-loading, the loaded NPCs
still held the saved values (`700/700 level 30 Strength 100` x77, `3/700` x1; template `600/25/90`): gCSkills_PS is saved with the entity; a **new game** afterwards had the level's shipped values (`600/600|25|90` x78) while the template still carried the edit (`700|30|100`); writing the instances again fixed them. A mod must therefore re-apply on every `session.start`
(`risen.balance.install` does: first `tick` into the templates, every `session.start` into the live NPCs). About 616 live entities (NPCs of the demo specs) are rewritten per `session.start` without noticeable delay (not timed precisely).
**Relative edits** (`hp_mult`, `level_add`, `damage_mult`, ...) must not compound across those re-applications, so the Lua layer computes them from the template values captured the first time it touches a template in the process (the shipped values) and writes absolutes; for live NPCs the same absolutes
(idempotent: verified by applying the spec twice: Boar 180 -> 360 -> 360, damaged wolf stays 7/500, full wolf 500/500).

**Implementation notes.** New symbols (all optional): `skills_set_base`, `skills_get_value`, `skills_get_max`, `skills_infos` (data export), `combat_admin_instance`, `combat_get_species`. `templateEntityPtr(name)` (lua_template.cpp) gives the loaded `eCTemplateEntity` as a `PSInventory`/`PSNpc` holder. `gCSkills_PS` on a template exists only after the template content
was loaded (the API forces it). The `group` setting of `gCNPC_PS.Group` is called `ai_group` in specs because `group` selects templates in `npc_groups`. `IsSmall` species list (Chicken, Nautilus) found by calling the script on one sample of every species. Defaults seen: most monsters have `ManaPoints` 0, Titan/Ghul/Inq_Guard 100.

**For the next milestones.** M4 (cloning NPC templates): a clone's `gCSkills_PS` and `gCInventory_PS` are copied by the engine's own `Clone`; `risen.npc.set_stats(newName, ...)` then works on the clone like on any template (the capture of "shipped" values happens on first touch, i.e. the clone's initial values). M7/M8 (spawning): `risen.npc.spawn` options
`hp_max/level/...` are still applied per entity after the spawn; for a type of NPC edit the template once instead. M10 (routines): `gCScriptRoutine_PS` is reflected (dump in NPC_PROPERTIES.md) but was not touched here. Existing NPCs' *inventories* are not propagated by `loot`.


## 25. New templates at runtime: cloning, names, IDs, savegames [confirmed in-game unless marked]

Driven by `src/game/lua_template.cpp` (natives `risen.template.clone/remove/id_for/created`, `Template:id/info/focus_name/description`, `Entity:dependencies/focus_name/description`), `src/game/template_create_lua.h` (`risen.template.create`, the `new_items`/`new_npcs`/`templates` sections)
and `src/common/template_id.h` (name -> ID); live tests `mods/newtemplates` + `mods/newtemplates_driver`, run by `scripts/smoke-test.ps1 -Scenario newtemplates` (five game processes: `save`, then `load`, `load_missing`, `load_again`, `load_late`). RVAs are of the 64-bit Risen Enhanced build.

**A template is a pair of entities.** `Load` (`0x6abc20`) reads a `.tple` file as a list of `eCTemplateEntity` objects: the first becomes the **root**, registered under the *file name* (`"It_1H_Sword.tple"`, flags `+0x58` bit 0 = `IsHelperParent`, no property sets, its own ID),
the next one is the **template entity** `"It_1H_Sword"` (a child of the root: `+0x10` parent, root `+0x20` first child, `+0x18` sibling), which carries the property sets and the ID that everybody uses. Flags at `+0x58`: bit 0 helper parent (root), bit 1 `IsContentLoaded` (`ReadAll` `0x6ad6d0` returns at once when set; `CreateEntityCopy` sets it),
bit 2 `GetIsGameRelevant`; observed: shipped template entity `54` (`0b110110`), shipped root `83`/`35`. `Template(name)` = `eCTemplateEntityProxy::SetEntity(bCString)` (`0x6b44e0`): among the entities registered under the name it takes the first that is **not** a helper parent and whose root is a *single template*
(`IsSingleTemplate` `0x223100`: the root's only child has no sibling and no children), then stores that entity's `eSEntityID` (the proxy's first qword; `eSEntityID` = `{ eCNode* at +0, short refcount at +8, entity type byte at +0xa (2 template, 3 dynamic entity), bCPropertyID* key at +0x10 }`). So a **single, parent-less clone is never found by name**: it needs a root with exactly one child.
(`GetEntity` `0x6b46b0` returns the child when the proxy points at a root, null for multi templates.)

**Cloning** (`cloneTemplate`): `root' = CreateEntityCopy(root)`, `child' = CreateEntityCopy(child)`, `SetName(root', "<name>.tple")`, `SetID(root', id(name + ".tple"))`, `SetName(child', name)`, `SetID(child', id(name))`, `eCNode::AttachChild(root', child')` (`0x69d3e0`), then `Template(name).GetEntity() == child'` is verified.
* `eCTemplateEntity::CreateEntityCopy(src)` (`0x6aeae0`) = `CreateNewEntity` (`0x6aec90`, `eCTemplateEntity` constructor `0x6aefc0`: **random** ID registered with the scene admin, no name) + virtual `OnCopyFrom` (`0x6ae730`: `Invalidate`; `eCEntity::OnCopyFrom` `0x6a45f0` drops the entity's referenced sets and **clones every copyable property set**
  of the source into the copy, `AddPropertySet`, then each set's `OnPostCopy`; copies the reference-template ID, the helper-parent and game-relevant bits; **registers the copy under the source's name**, so a fresh copy answers to the same name until `SetName`) + flag `|= 2`. Children are not copied (multi templates are refused).
* `SetName` (`0x6ae020`) unregisters the copy's name info (`UnregisterNameInfo` `0x698330`, which only removes *that entity* from the name's entity list) and registers the new name (`RegisterNameInfo` `0x6984f0`). `SetID` (`0x6ae450`) unregisters the random ID and registers the new one (`RegisterEntity` `0x697ed0`).
  **`RegisterEntity` falls back to a random ID when the requested ID already has a registered entity**, hence the collision check *before* cloning (`eCSceneAdmin::GetEntity(id, type)` `0x697b80`; an ID that is only known to a pending proxy has no node and counts as free) and the ID comparison after.
* The result is a full independent copy: same property sets, same values (verified for items, wolves, humans: mesh, icon, skills, stacks of the template inventory), edits of the clone leave the base untouched and the other way round (a **snapshot** at creation).
  `GetFileName`/`IsReadOnly` follow the parent chain to the root and the archive manager: for a clone the "file" `"<name>.tple"` does not exist (nothing ever saves or reloads a runtime template).
* **Late NPC cloning crashed** until `gCNavigation_PS::OnPostCopy` (`0x92b830`, `Reset(false, true, true)`) was skipped: with a world loaded it raises an access violation for a *template's* navigation set (found by cloning the sets one by one: `Clone` and `AddPropertySet` of every set are fine, `OnPostCopy` of `gCNavigation_PS` is not).
  Items have no such set and could always be cloned; NPC/monster templates cloned **before** the first world (first `tick`) never failed. The layer hooks that function and returns early only while it clones a template (`g_skipNavPostCopy`); after the hook all of Wolf, Boar, Peasant_1_01, Gnome, Titan clone and spawn correctly in a loaded world.
  A clone attempt that throws leaves nothing behind: the half-made entities are taken out of the name map again.

**The ID of a clone** is derived from its name, so that every process (and every machine) gives the same template the same ID, which is what a savegame stores. `bCPropertyID` is **20 bytes**: a 16 byte GUID and a 4 byte counter that is always 0 (SharedBase: constructors `0xa6150/0xa6210`, `CreateRandom` `0xa5ca0` = `CoCreateGuid`,
`CreateFromString` `0xa5a80`: the **MD5 digest of the string** in bytes 0..15; stream form `>>` `0xa58a0`: the 16 bytes then the counter; `GetText` = the GUID in lower case). Scheme used here: `id(name) = MD5("spass.template:" + name)` (own MD5 in `template_id.h`, unit-tested against the RFC vectors),
`id(name + ".tple")` for the root, printed as a GUID in memory order (`Data1` little endian: `risen.template.id_for("Spass_Flame_Sword")` = `c095059b-0585-7a31-efea-da3f9de97a06`). Shipped templates have random GUIDs, so a collision needs a name whose digest equals one of them (practically impossible); a collision with *anything registered* is detected and refused (`..is already used by another entity`).
The ID registry maps ID -> `eSEntityID` (the scene admin's hash at `+0x18`); entries carry a refcount: `RegisterEntityProxy(id)` (`0x697d10`) creates an entry **without node** when something refers to an ID that no entity owns yet (a proxy read from a savegame); a later `RegisterEntity` with that ID fills the node in (`peVar4->node == 0` branch).

**What the engine stores.** `eCTemplateEntityProxy::operator>>` (`0x6b3ff0`) writes `version 2`, a bool "has entity" and the **bCPropertyID of the entity** (`*(proxy) + 0x10`); an inventory stack and the creator of an entity are such proxies, dialogue `Give` commands hold the item as an `eCTemplateEntityProxy` property (`Item`), i.e. also by ID.
Names are only used by scripts, `Template("name")`, spawn calls and loot/inventory *tables of our API*.

**Savegame behaviour** (fresh game processes, `mods/newtemplates_driver`; 3 swords + 2 berries in the player's inventory and 2 swords in the inventory of a world Peasant_1_01, quick save):
* `load` -- the mod creates its templates on the first `tick` (before `load.before`): counts 3/2/2, item value from the clone (900), same ID, label shown. **Works.**
* `load_missing` -- the mod creates nothing: the load succeeds, `risen.template.exists` is false, `risen.item.count(player, name)` is 0, `stack_item(i)` is nil for that stack (the stack still *exists*: `stack_count` stays 1), the game ran 400+ frames with AI and inventory scripts without a problem, and a **quick save in that state succeeds and keeps the stack** (the pending ID is written again).
  Not tried: opening the inventory screen in that state (inferred: the item name lookup of the HUD gets a null template).
* `load_again` -- the templates are back: all items are there again (3/2/2, value 900): **items survive a session without the mod's templates**.
* `load_late` -- the mod creates the templates only at `session.start` (after the savegame was read): the pending proxies resolve, counts 3/2/2. **Works too**; the ordering rule is therefore "before the items are *used*", with "before the load" as the safe choice.
* Same process: quick save, taking all items away, quick load: 3/2/2 again; new game afterwards: the clones are still registered, same IDs and numbers, the player starts without them. `session.start` re-applies the spec (5 of 5 entries, idempotent).
* **Entities created by `Entity::Spawn` are not saved (RE.md §14), so only inventories (and world entities that existed before) carry clone items.** A saved world entity whose creator is a clone would refer to the clone by ID in the same way (inferred, not testable without the engine saving a clone-made world entity).

**Labels.** `gCInteraction_PS::GetFocusName` (`0x8bf290`) / `GetPlainFocusName` (`0x8ba3b0`, wrapped by `Template:focus_name/Entity:focus_name`): the loc id `focus_unique:FO_<name of the owning entity>`, else `focus:FO_<name>`; the owner of a template's set is the template entity (its name = the template name, so inventory stacks show `FO_<template>`); for a spawned entity the entity's own
(unique) name, which defaults to the template name. `gCItem_PS::GetDescription/GetPlainDescription` (`0x8c5ae0/0x8c5b90`): `item:ITEMDESC_<owner name>`. Both check `eCLocString::ExistsID` first (`CFXLocString::IsKeyValid` -> `CFXTableMap::HasKey` `0x41110`, which splits the prefix and looks the key up in that table's rows);
`HasKey` is hooked by the overlay, but it was asked for the *prefixed* id, which the overlay stored without prefix: **`risen.loc.set("FO_X")` made `risen.loc.exists("FO_X")` true but the engine's `focus:FO_X` lookup empty**. The overlay now answers a prefixed id from an entry stored without the prefix as well (`findEntry`), verified: the label of a clone reads back through the engine's own `GetPlainFocusName`.

**Mesh, icon and appearance.** Items: mesh = `eCMesh_PS.MeshFileName` (`It_Wpn_1H_Club.xmsh`), icon = `gCItem_PS.IconImage` (`ic_it_1h_club`), `ItemWorld`/`ItemInventory` are template proxies (`(no template)` for all shipped weapons). A clone with the club's mesh and icon spawned with
`eCMesh_PS::GetDependencies` (`0x5c5500`, the engine's own resource list of what the set loaded: it also queues/caches the resources) = `It_Wpn_1H_Club._xmsh`, the same as a real club (the sword's is `It_Wpn_1H_Sword._xmsh`), and `item_icon()` `ic_it_1h_club` (`Entity:dependencies()`). NPCs/monsters: `eCAnimation_PS.ResourceFilePath` is the *actor* (`Ani_Wolf_Monster_Wolf.xac`, `Ani_Boar_Monster_Boar.xac`;
`eCAnimation_PS::GetDependencies` `0x4d8a20`); a wolf clone with the boar's actor spawned with `Ani_Boar_Monster_Boar.xac` in its dependency list. **Humans all share `Ani_Hero_Skeleton.xac`**; heads (and body parts) are *item templates* with an `eCBodyPart_PS` set (`Hero_Head_Achmet_S1`: `ResourceFilePath` `Ani_Hero_Head_Achmet_01.xac`, `gCItem_PS`, `gCInteraction_PS`; 64 below `NPC/_HumanParts/Head`),
equipped into slots (`PSAnimation::GetBodyPart(gESlot)`). The shipped human templates have **empty inventories** and spawned/world NPCs have no child entities or slot items we could find, so the head is not part of the NPC template (where it comes from for world NPCs was not found; screenshots are black, so what a spawned clone looks like was not seen).
Consequence: a human clone looks exactly like its base; giving it another look means another `ResourceFilePath` (a different species) or equipping a part item (not explored). Whether the engine can *load* a mesh that does not exist in the packed data (M5) is not touched here: the names are not validated against the archives.

**Removing.** `risen.template.remove(name)` (clones of this layer only, refused while `Template:instance_count() > 0`) unregisters the names of the child and the root (`UnregisterNameInfo`): `Template(name)`, spawn, give and the dialogue commands no longer find it. The entities and the ID **stay registered until the process ends** (stacks, items already handed out and saved games keep working);
`create` with the same base registers the names again (`SetName` on the kept entities) and gives back the same ID. Verified: remove -> name free -> create -> same ID, remove refused while a spawned item exists. Freeing the engine objects was not attempted (the scene admin keeps raw pointers; nothing is gained).

**An open conversation blocks saving** (found while writing the driver): `World::IsSaveAllowed` is false while a conversation is open, and a `quick_save` (and later the new game) after a `Give` dialogue command crashed the game process, with or without clones, whether the NPC was killed or not. Ending the conversation (an `End` command as the last line of the info) fixes it. (The fetchquest scenario saves inside an open conversation without a crash,
so the exact condition -- probably the `Give` hand-over state -- is not known; `risen.world.quick_save` does not check `is_save_allowed`.)

**For the next milestones.**
* M5 (assets): `mesh`/`icon` take any name the engine can resolve; whether a loose mod file is found is exactly the open M5 question. `Entity:dependencies()` is the check (compare with a shipped entity), `eCMesh_PS::GetDependencies` also tells which resource name the engine *tried*. A clone is the way to attach a new mesh to a new item: `risen.template.create("X", "It_1H_Sword", { mesh = "NewMesh.xmsh" })`.
* M6 (spells): a spell is an item (rune/scroll) with `gCCastInfo_PS`/`gCItem_PS.Spell`: clone a rune or scroll with `create` and edit `properties`; `mana_cost` and `requires` already work in the `new_items` overrides; nothing about the spell objects themselves was touched.
* M7/M8 (spawning): `risen.npc.spawn("MyClone", ...)` works for NPC clones, also late (after the world exists) thanks to the `OnPostCopy` hook; only **inventories of existing entities** carry clone items through a savegame; a spawned clone NPC must still be re-spawned on `session.start`/`load.after`. Entities hold their template by ID, so a clone that is created after the load still adopts the stacks.
  Template `loot` of an NPC clone is part of the clone (`gCInventory_PS` is cloned) and can reference clone items created earlier in the same spec.

## 26. New meshes, textures, icons from loose files [confirmed in-game unless marked]

Driven by `src/game/lua_assets.cpp` (`risen.assets`), `src/game/vfs_mount.cpp` (startup mounts), `src/common/asset_names.h` (name mapping, unit-tested) and `mods/newassets` (scenario `newassets`, two game processes). RVAs are of the 64-bit Risen Enhanced build.
Decompiled: `Engine.dll` (`eCResourceCache2`, `eCResourceModule2`, `eCResource2`, `eCGui*`), `FFC.dll` (`CFXVirtualFileSystem`, a thin wrapper) and `FileSystem2.dll` (the real file system, imported into its own Ghidra project; `tools/ghidra/DumpVtable.java` lists vtable slots).

**Answer to the open question of 4.2: loose files work for resources.** Entity templates cannot be shadowed (§17, the template database does not accept loose entries), but every resource the engine loads *by name* -- meshes, materials, images, actors -- is found through a name cache that includes every mounted directory.

**How a resource name is resolved.**
* A property or another resource refers to a *raw* name: `eCMesh_PS.MeshFileName` `It_Wpn_1H_Club.xmsh`, a material's `ImageFilePath` `ItWpn_SwordMisc_01_Diffuse_01.tga`, a mesh's `MaterialName` `ItWpn_SwordMisc_01_Diffuse_01._xmat` (already compiled), `eCAnimation_PS.ResourceFilePath` `Ani_Wolf_Monster_Wolf.xac`. The engine loads the *compiled* file, with an underscore before the extension (`eCResource2::GetCompiledExt`): `.xmsh` -> `._xmsh` (packed in `data/common/meshes.pak/<group>/`), `.xmat` -> `._xmat` (`data/common/materials.pak`), `.tga/.dds/.png` -> `._ximg` (`data/compiled/images.pak/<group>/`), `.xac` -> `._xmac` (`data/compiled/animations.pak/_emfx36/...`), motions `._xmot`, collision `._xcom`. `Entity:dependencies()` (the engine's `GetDependencies`) lists the compiled names.
* `eCResourceModule2::GetResource(name)` (`0x6817e0`; static `eCResource2::GetResource(name)` `0x684160`, exported) picks the cache of the extension and calls `eCResourceCache2::RequestResource` (`0x687f00`): a hash lookup by name (`FindResourceA`), else `SpawnResource` + `RegisterResource` + `LoadResource` -> `eCResource2::Load` (`0x683540`). The caches are pre-filled with the *headers* of the shipped resources from `data/common/meshes/compiled_meshes.bin` (6.3 MB), `compiled_images.bin`, `compiled_materials.bin`, ... (`LoadResources(SCompiledFile&)` `0x686690`, magic `GH04`: name, size, two file times). **A name that is not in these tables does not matter**: the resource is created on the first request and loaded from whatever file the name resolves to.
* `Load` / `GetFileState` / `LoadData` find the file with **`CFXVirtualFileSystem::LookupCachedVirtualPath(compiledName)`** (FFC `0x37fa0`, exported; `LookupCachedPhysicalPath` `0x37f80`): a flat hash (FileSystem2 root node `+0x48`, key = lower-cased *file name*, `0x118c0`) from the bare file name to the file node, built from **every mounted pack volume and directory** (`CFFCachedFolder::CacheAdd`/`UpdateCache`). So `It_Wpn_1H_Club._xmsh` resolves to `#G3:/data/common/meshes/Items_Weapons_Swords_01/It_Wpn_1H_Club._xmsh` (physical `...\data\common\meshes.pak\Items_Weapons_Swords_01\...`), whatever the directory; a path or a raw name is not a key (`risen.vfs.lookup`, `risen.vfs.lookup_physical`). Then the archive manager opens the file like any other and `Load` checks the 40-byte header (below) and reads the object.
* **Mounting a directory adds its files to that cache at once** (`MountDirectory`, also at runtime: verified, no restart, no `ProcessMountList` needed); files created *after* the mount are not seen until the directory is mounted again (the mount list says `NoWatch=true`; `risen.assets` unmounts/mounts it lazily, `SyncCache(path)` returned false for every path tried and is not used). A file whose name already exists **shadows** the shipped one in the cache (verified: loose `Item_Apple._xmsh` -> `#G3:/mods/<mod>/generated/...`, removing the file and remounting gives the pack file back); it matters only for resources that are not loaded yet (a loaded resource keeps its data). The virtual name of a mount does not matter for resources (`mods/<mod>/assets`, `.../generated`, `.../runtime`); `eCVirtualFileSystem::ExistsFile("<mount-relative path>")` of a new file is false (the earlier §17 note about shadowing by relative path concerns files that exist in the packed tree, e.g. `ini/loc.ini`).
* Verified live (`risen.assets.resource(name)` = `eCResource2::GetResource` + `GetResourceState` at `+0x38`: 0 missing/failed, 1 header known, 2 queued, 3 data cached + `GetFilePath`): a **loose copy of the club mesh under a new name** (`Spass_Club._xmsh`) in a mounted directory; `risen.template.create("X", "It_1H_Club", { mesh = "Spass_Club.xmsh" })` and a spawn: `Entity:dependencies()` = `Spass_Club._xmsh`, resource **state 3** from the loose path (the whole object parsed and cached). The same for a **new material** (`Spass_SwordMisc_01_Diffuse_01._xmat`, state 3), a **new texture** (`._ximg`, state 3), and a **new actor** (`Spass_Wolf._xmac`, state 1; the spawned wolf stayed alive for 300 ticks with AI and animation running). Files that do not exist give state 0 (and leave an empty resource entry registered). What a mesh looks like on screen cannot be checked (the D3D9 window is black in screenshots): the evidence is the load state of every link of the chain mesh -> material -> image.

**The resource file container** (all `._x*` files; from the files in the archives and by a patched copy that loaded): `"GR01"` + a 4-character content version (`MS02` mesh, `SM01` material, `IM04` image, ...), `u32 0x28` header size, three `u32` (inferred: size of the schema section, offset of the payload, payload size), a file time (8 bytes), the extension of the source file (`.tga`, `.dds`, `.xmsh` ...), then a property-object stream with its own schema (`eCMeshResource2`, `SubMeshes`, `MaterialName`, `Extends`, ... / `eCMaterialResource2`, `eCColorSrcSampler.ImageFilePath` ... / `eCImageResource2`: `Width`, `Height`, `SkipMips`, `PixelFormat` as `DXT1`, then **an ordinary DDS file** at offset `0x109`; `GUI_IconSets01._ximg` is an uncompressed A8R8G8B8 DDS). The file does **not** contain its own name: a byte copy under another name loads. It does contain the names of what it uses, as strings with length prefixes inside sections that carry sizes: renaming the material name in a mesh or the image name in a material to a name **of the same length** loaded; names of another length were not tried (inferred: the sizes in front of the strings would have to change, so `risen.assets` documents the same-length rule and leaves other edits to tools).
* The mesh of the club names exactly one material, the sword material names a diffuse and a normal map (`...Diffuse_01.tga`, `...Normal_01.tga`); a loose texture is used by naming it in a (loose) material, which is used by naming it in a (loose) mesh -- the chain `mods/newassets` builds. `eCMesh_PS.MaterialSwitch` is only a number (variant), not a name, so `material = 2` does not select a new material.
* `mods/newassets` makes its files from shipped ones at run time (`risen.assets.read` + string patches + `risen.assets.write`): the mesh with a same-length material name, the material with a same-length image name, the texture with red and blue swapped in every DXT1 colour (so it differs from the shipped one and keeps its size and header), the actor as a plain copy. Nothing of the game is stored in the repository.

**Mod directories.** `ModInfo.assets` (`mod.toml` key `assets`, default `assets`, `false` = none) and `<spass>\generated\<mod>` are mounted by the hook on `eCVirtualFileSystem::ProcessMountList` (the same place as `data_overlay`: before the engine loads anything) as `mods/<mod>/assets` and `mods/<mod>/generated` (skipped when `data_overlay` mounts the whole mod directory). `risen.assets.mount(reldir)` mounts later (`mods/<mod>/runtime`), `risen.assets.write/remove/clear` change the generated directory (the only place a sandboxed mod may write). Verified: startup mount of `assets` and of `generated` (second process), runtime mount, write + lazy remount, shadowing in both directions.

**Icons.** `gCItem_PS.IconImage` is an `eCGuiBitmapProxy2` = a *name*. `operator<<`/`Resolve` (`0x52ef80`) -> `eCGuiDevice2::QueryBitmapA(name)` (`0x517720`): lower-cased file title -> a hash (bitmap cache at device `+0xa8`) of **deferred bitmaps** (`eCDeferredGuiBitmap2`, `GetBitmap` `0x52a380` is null until a real bitmap with that name is registered; an unknown name gets an unattached placeholder). Shipped icons live in `data/compiled/library/compiled_imagelists.bin` (`V001`, loaded once by `eCGuiDevice2::LoadResources` `0x516690` through `eCDocArchive::ReadBinary`; the raw XML sources `data/raw/gui2/imagelists/*.ximl|*.ximc` are only imported in editor mode): image collections `IMC_Icons`, `IMC_Maps`, `IMC_Elements`, `IMC_X360`, `IMC_OLD`, `IMC_GUI`, `IMC_Achievements`; a layer is an `eCGuiImageList2` with a `TextureName` (`GUI_IconSets01.dds` -> `GUI_IconSets01._ximg`, 1024x1024) and images `{ ImageName "IC_It_1H_Club", RectOnTexture "123; 534; 163; 574", BlendMode }`; item icons are 40x40 cells. Two ways to register new icons at run time, both **verified** (`bitmap(name).attached` false -> true, `Entity:item_icon()` returns the new name; how it is drawn is not visible):
1. **An XML image list**: `eCGuiDevice2::LoadImageListA(name)` (`0x5194a0`, virtual) finds `<name>.ximl` through the *same name cache* (`FUN_18052db10`), parses it (`eCXMLFile::Load`, `eCGuiImageList2::OnDeserialize` `0x532bc0`), registers the list and attaches its images to the deferred bitmaps. The format is what `eCGuiImageCollection2::SaveXml` writes (`risen.assets.dump_collection("IMC_Icons", "x.xml")` -> `<spass>\generated\_dump\x.xml`): `<ImageList ClassName="eCGuiImageList2" ImageSize="0; 0" TextureName="Spass_IconAtlas.dds"><Images><Image ClassName="eCGuiImage2" ImageName="IC_Spass_Club" RectOnTexture="123; 534; 163; 574" BlendMode="eEImageBlend_AlphaBlend"></Image></Images></ImageList>`; `SplitImage`, `AnimImage`, `LayeredImage` nodes exist too. The texture is a `._ximg` found as any other resource (here a copy of the stock icon atlas).
2. **A whole image file**: `eCGuiDevice2::LoadBitmapA("Spass_Icon.ximg")` (`0x5179c0`) loads the image resource as an `eCGuiTexture2` (`ReloadTexture` -> `eCResource2::GetResource(image, name)`) and registers it under its file name; `icon = "Spass_Icon.ximg"` then attaches (the picture is scaled into the slot; the engine's own use are GUI backgrounds such as `GUI_MainMenueBack`).
* Calling convention (decompiled constructor `0x516cf0`): `eCGuiDevice2` has its `IFFGuiDevice2` sub-object at **`+0x250`**; the *virtual* `LoadBitmapA`/`LoadImageListA` implementations (and the exported `QEAA` wrappers `0x9ce50`/`0x9cf10`, which pass `this` on unchanged) expect that pointer (`this - 0x1a8` is the bitmap cache), while `QueryBitmapA`, `LoadImageCollectionA` and `eCGuiModule2::GetDevice` (static) use the device itself. Passing the device to the wrappers raises an access violation (found first: `guarded` turned it into `false`).

**What `risen.template.create` checks now.** `mesh` (item: a mesh, NPC: an actor) must resolve in the name cache (`risen.assets.check`), `icon` must be a bitmap the GUI knows (`attached`); `check_assets = false` opts out (the names are then written as given, like in M4). The shipped icon names are case-insensitive (`ic_it_1h_club` and `IC_It_1H_Club` both attach).

**Not verified / limits.** What the new meshes and textures look like (screenshots are black; load state and the chain of resources are the evidence); a name of another length inside a patched file; building a `._xmsh` from an authoring format (there is no exporter here: the files come from the game or from the game's own tools, `Importer.dll` holds the resource compilers); the icon rendering in the inventory screen; savegames with loose-asset items (the asset names live in the template, which every process creates again, exactly like M4); a loose file replacing a resource that was already loaded (the loaded data stays until the cache flushes it); loose files with the *name of a template* are still impossible (§17). Motions (`._xmot`) are keyed by animation names, so a new actor reuses the species' motions only as far as the engine finds them by the actor's name (the cloned wolf lived and ran its AI; its animations were not inspected).

## 27. Reliable gameplay events: inventories, deaths, spawns [confirmed in-game unless marked]

Driven by `src/game/inventory_events.cpp`, `entity_events.cpp`, `event_bus.cpp` (queue + dispatch) and the hooks in `events.cpp`; verified by `mods/events_demo` (scenario `events`, 41 checks with exact counts, `smoke-test.ps1 -Scenario events`).
Decompiled (headless Ghidra, `tools/ghidra/Callers.java` lists the callers of a function): `Game.dll` (`gCInventory_PS`, the stack list, `gCDamage_PS`, `gCInfoCommand*`), `Script_Game.dll` (the scripts `Kill`, `Defeat`, `OnCheatKill`), `Script.dll` (`PSInventory`). RVAs of Game.dll unless noted.

**The inventory is a stack list.** `gCInventory_PS+0x30` is an array pointer, `+0x38` the stack count (stride `0x60`); a stack has the template proxy at `+8` (`eCTemplateEntityProxy::GetEntity` gives the `eCTemplateEntity`, `GetName` its name),
the equip slot (`gEEquipSlot`) at `+0x48`, the runtime id at `+0x50` and one qword at `+0x58`: amount in bits 0..20, select limit in 21..41, selection in 42..62 (selecting more than the limit is clamped: `PSInventory::SetMaxSelectLimit` before
`SetSelection`/`SetMaxSelection`). The list reports to **one** listener (`list+0x10`: the `gCInventoryListener` base at `gCInventory_PS+0x20`) through virtual slots 0/1/2 = `OnStackCreate/OnStackChange/OnStackDelete(index, stack)`; `gCInventory_PS`
forwards them to its own listener array (+0x288/+0x2a8: UI, `gCQuest::OnInventoryStackChange`). `Create` is announced after the stack exists, `Change` after the amount was written (**the old amount is never given**), `Delete` *before* the stack is removed.
The stack-list functions themselves are not exported (`0x8ca5c0` create items, `0x8ca750` delete items, `0x8ca020` delete stack, `0x8cb010` drop whole stack, `0x8cb220` merge, `0x8cb420` delete if unequipped); `TransferItemsTo` (`0x8c4100`),
`SetAmount` (`0x8c3140`), `SetMoneyAmount` (`0x8c3990`), `LootSelections`/`TradeSelections` (`0x8c3b80`/`0x8c3ce0`: loops of `TransferItemsTo` over the selected stacks) change amounts inline.
Every change a script, a dialogue or the UI can make ends in one of the **exported** operations: `CreateItems` (2 overloads, `0x8c35a0`/`0x8c21f0`), `AssureItems`, `InsertStack` (`0x8c0cf0`, the ground pick-up, kills the item entity), `DeleteItems`, `ConsumeItems`
(`0x8c5140`: applies the item's `ModifySkills`, then `DeleteItems`), `DeleteStack`, `SetAmount`, `SetMoneyAmount`, `TransferItemsTo/StackTo/AllTo`, `LootSelections`, `TradeSelections`, `DropItemsFromStack` (`0x8c0aa0`), `DropStack`,
`MergeStacks`, `SplitStack`; `EquipStackToSlot` (`0x8c4bc0`, `EquipStack` calls it) and `UnEquipStack` (`0x8c4a70`) for equipment. The only caller that bypasses them is the dialogue command `gCInfoCommandCreateItem::Execute` (`0x8882d0`, calls the stack list directly).
Gold is the item `It_Gold` (`SetMoneyAmount` = `AssureItems` + delete/set).

**The hooks (all by decorated name, `symbols.h` section "reliable gameplay events").** Because `Change` carries no old amount and a loot is many small changes, an *outermost hooked operation* opens a scope that names the cause; every hooked operation,
nested or not, snapshots the inventories it touches (template -> total amount) before it runs; when the outermost one returns the touched inventories are compared with their snapshots: one `inventory.added/removed` pair per inventory and template
whose total changed, with the counterpart of a transfer as `other` (an opposite change of the same template in another touched inventory). Nested operations (`ConsumeItems` -> `DeleteItems`, `LootSelections` -> `TransferItemsTo`) therefore
never fire twice. Causes by outermost operation: `CreateItems/AssureItems/DeleteItems/DeleteStack/SetAmount/SetMoneyAmount/TransferItemsTo...` "script", the `Give`/`CreateItem`/`DestroyItem` dialogue commands "dialogue" (the `CreateItem` command also snapshots
its target through the command's virtual `GetEntityProxy1` (`+0x118`) because it bypasses the exported operations), `InsertStack` "pickup", `LootSelections` "loot", `TradeSelections` "trade", `DropItemsFromStack/DropStack` "drop", `ConsumeItems` "consume";
`MergeStacks`/`SplitStack` open a scope too (no net change). The listener hooks `OnStackCreate/OnStackDelete` report what no hooked operation announced (cause "other"; not seen in the tests; a `Change` without a scope cannot be reported).
`item.equipped/unequipped(owner, template, slot)` read the stack before the call (`UnEquipStack` is also called by the engine before a stack is dropped or deleted), `item.consumed` comes from `ConsumeItems`, `item.picked_up` from `InsertStack`.

**Why the events are queued.** The hooks run inside inventory loops (`LootSelections` reads the stack count again after every transfer), dialogue scripts and the AI. A Lua handler that creates items in the same inventory there could corrupt a loop the engine
is in the middle of. So `bus::post` copies everything (names, counts; an entity as pointer + name) into a queue and `bus::flush` (called from the `gCSession::Process` hook, before `tick`) dispatches it in the same frame, up to four rounds when handlers
raise events. A handle is rebuilt at dispatch (name read again: `risen.npc.spawn` renames the entity right after `Entity::Spawn` returned). Nothing is posted while no session runs (`gCSession::Start` .. `Stop`/load begin), and the queue is dropped at load.
**Settling:** right after `gCSession::Start(0)` the engine fills the inventories of the whole world (scripts call `CreateItems` for every NPC: hundreds of events in the first frames) and creates their equipment (`entity.destroyed` of equipment copies);
inventory/item events of anyone but the player and `entity.destroyed` are dropped until the player entity exists (`world.ready`) plus 30 frames.

**Deaths.** The decompiled script `Kill` (`Script_Game.dll` `0xc9f70`; `self` = attacker, `other` = victim, both `Entity*` holders or null = the script unit's self/other entity) plays the death effect, sets the victim's hit points to 0 (`SetHitPoints(0, 2)`),
sets the AI mode of `gCScriptRoutine_PS` (property set `0x24`, `+0xC0`) to **12 = dead** (that is what `Entity::IsDead` / `gCNPC_PS::IsDead` test), calls `QuestManager::OnNPCKilled(victim)` (-> `gCQuestManager::OnNPCKilled`, hooked since §14) and, for the
player, opens the game-over screen. `Defeat` (`0xd60e0`) does the same with hit points 5 and AI mode 11 (knocked out; `Kill` sets 12 = dead), ends in `OnNPCDefeated`. `OnCheatKill` (`0xdc850`, the kill cheat) sets the attacker to the player and starts the AI state `ZS_RagDollDead`, whose
script calls `Kill` synchronously. The combat damage scripts (`DownOrDeadByMeleeWeapon` etc.) choose by the victim's `DamageCalculationType`: monsters die (`ZS_RagDollDead`), humans fall unconscious (`ZS_Unconscious` -> `Defeat`). The layer wraps `Kill`, `Defeat`
and `OnCheatKill` (module `Script_Game.dll`, hooked after `PostInitialize` loaded it) to learn the attacker and the cheat; `OnNPCKilled/OnNPCDefeated` post `npc.killed/npc.defeated(entity, attacker)` and `entity.died/entity.defeated`.
`entity.died` is posted once per entity name (a set, cleared when the entity is removed and at session boundaries) and only when `Entity::IsDead` is true; cause: `"cheat"` (the victim's name was registered by the `OnCheatKill` hook, which runs *around* the death),
`"script"` (`risen.script.call` is on the stack: the layer counts them), `"combat"` (an attacker is known), else `"other"`. Setting hit points to 0 with `PSSkills::SetHitPoints` kills nothing: the AI keeps fighting (a wolf at 0 hp bit the player to death
before it got "killed"). Verified: `Kill` with and without attacker, twice, `Defeat`, `OnCheatKill`, a real wolf killing a 1-hp player and, in another run, a villager killing the wolf (`entity.died(wolf, villager, "combat")`).
`gCDamage_PS::DoDamage(victim)` (`0x8081b0`; the two-argument overload sets the Agressor proxy at `+0x30` and calls it) is the single entry of weapon, fist and floor damage (`HitTargets`, `ContributeFloorActions`): `entity.damaged(victim, attacker, lost, raw)`
reads the victim's hit points (`PSSkills::GetHitPoints`) before and after, `raw` = `(DamageAmount + DamageBonus) * DamageMultiplier` (`+0x40/+0x44/+0x48`; monster bites do 0 there: their damage is the Strength term of the script). Hits that take nothing (corpses) are not reported.

**Spawns and removals.** `entity.spawned` hooks `Entity::Spawn(Template, matrix, bool)` and `Spawn(Template, Entity, bool, gEDirection)` (`Script.dll` `0x20e4d0`/`0x20e240`; the by-value `Template` is read before the callee destroys it): every script spawn, including
`risen.entity.spawn` and `risen.npc.spawn`. Engine-internal creations (`eCEntity::CreateFromTemplate`: dropped items, equipment copies, effects) are not reported. `entity.destroyed` hooks `eCEntity::Kill` (`Engine.dll` `0x6a5dd0`, the one entry for removing an entity:
item pick-ups, `Entity:kill()`, despawns, equipment replaced); template and kind are read before the call (the entity is gone afterwards); templates starting `VFX_`/`PFX_` (effects, hundreds per second near water) are skipped.

**Not found / not done.** `item.used` of food: the use scripts (`InventoryUse_Player_Consume*`) start an AI action and the consumption itself is not traced to `ConsumeItems` (assumed; `risen.item.consume` is verified). Crafting shares the create/delete operations (cause
"script" in M5b; since M6b `PSInventory::Synthesize` is hooked: cause `"crafting"` and a `crafting` event, section 30). A "container opened" event: opening a chest is an interaction (`gCInteraction_PS`) that moves nothing by itself, its loot is reported as "loot". The buy/sell screen and the interactive loot/pick-up end in the same functions as `risen.item.trade/loot/pick_up`
(read in the decompilation) but cannot be driven from a script.

## 28. Custom voice lines: spoken dialogue, the sound container, languages [confirmed in-game unless marked]

Driven by `src/game/lua_voice.cpp` (`risen.voice`, `risen.assets.import_sound/sound`, the `Entity::GetSampleTitle` hook), `src/common/sound_file.h` (format probing and the container, unit-tested) and `mods/example_voice` (scenario `ex_voice`, two game processes: voice language English, then German).
Decompiled (headless Ghidra): `Game.dll` (`gCInfoCommandSay/SaySound`, `gCInfoCommand::GetTableValue`, `gCRuntimeInfo`, `gCScriptProcessingUnit::StartOutput/sAIOutputInstr`), `Script.dll` (`Entity::StartSay/StartSayEx/GetSampleTitle/StartFacialAni/StartSaySVM/StartOutput`), `Script_Game.dll`
(`IS_2_Running_ProcessInfo` `0xe2a80`, `_AI_Say` `0xad9d0`, `_AI_Say_Ex` `0xadda0`; `tools/ghidra/DumpDecomp.java` on the `Script_Game.dll` project), `Engine.dll` (`eCResource2::GetSound`, `eCSoundResource2`). RVAs of the 64-bit Risen Enhanced build.

**How a spoken line finds its sound.**
* The dialogue runner `IS_2_Running_ProcessInfo` (Script_Game) switches on the command type of the running info: `Say` (0x1b) calls the AI function `_AI_Say(speaker, listener, textID, gesture...)` where `textID` is `InfComSay::GetTextID` = the *loc string id* of the line (shipped: `INFO_FILLERX2_00007314`, runtime dialogues of this layer: `<info name>_<line number>`).
  `_AI_Say` asks **`Entity::GetSampleTitle(speaker, key)`** (Script.dll `0x20a9e0`, by-value `bCString` key, result through the hidden pointer) for the sample title and hands it to `_AI_Say_Ex`, which plays the "say" animation, `StartFacialAni(title)` and
  `gCScriptProcessingUnit::sAIOutputInstr`. The title is composed as `"%s_%s_%s_%%_%s.wav"` = **`<key>_<NPC voice>_<voice language>_%_<speaker entity name>.wav`**: the NPC voice is `gCNPC_PS.Voice` (`+0x20` of the set: the template's voice name, `Enzo` for `Peasant_1_01`, `Player` for the hero), the language
  `eCLocAdmin2::GetVoiceLanguage` (Engine `0x737f30`, the `VoiceLanguage` attribute of `<Language>` in `ConfigUser.xml`/`ConfigDefault.xml`, `English`, `German`, `French`, `Russian` ship; the *subtitle* language is `SubtitleLanguage`, independent), the entity name the speaker's unique name (`PC_Hero`, `Magnus`, `SPASS_VoiceProbe`).
  Seen live for a runtime option without audio: `VP_Silent_1_Enzo_English_%_SPASS_VoiceProbe.wav` (and `..._German_...` with the German voice language). `Entity::StartSay` (comments, thoughts) does the same through `StartSayEx`.
* `sAIOutputInstr` -> `gCScriptProcessingUnit::StartOutput(name, type)` (`0x96d330`) -> **`eCResource2::GetSound(name)`** (Engine `0xa0700`: resource type 5) + `RequestData`; `+0x188` of the unit holds the sound resource while it plays (`gCScriptRoutine_PS::IsOutputPlaying` `0x96aac0` reads it, `StopOutput` `0x966180`).
  The name is a raw `.wav` name; the compiled file `..._xsnd` is found by its bare file name in the flat name cache of everything mounted (§26), so **a loose file in a mounted directory is the sound, and shadows a shipped one of the same name**.
* The output type is `0` (NPC), `2` (the player speaks), `3` (comments, `SaySound`). `SaySound` (command 0x1f; `gCInfoCommandSaySound` has `Speaker` and `SoundName`, its `Execute` only reports the info start) plays a "say" animation and then starts the output with the **raw `SoundName`** (no key, no language).
* **Line duration = audio length.** The AI instruction finishes when the unit's sound resource is cleared (the channel ended). Without a sound (`StartOutput` failed) it waits `(length of the name + 10) * 100` ms (`sAIOutputInstr`, `+0x2c`): 3.4 s for `Spass_Voice_missing.wav`, 5.9 s for `VP_Silent_1_Enzo_English_%_SPASS_VoiceProbe.wav` (a line is *not* timed by its text). Live: a 1.16 s clip made an option last 1.33 s (a missing sound 3.42 s), a 0.4 s tone 53 frames
  (80 frames/s) against 467 for the unvoiced control line; `gCScriptRoutine_PS::IsOutputPlaying` was true for 35 of those frames.

**What the shipped files look like.** `data/compiled/speech_<language>/infos/<chapter>/INFO_<QUEST/NPC>_<id>_<voice>_<Language>_%_<entity>._xsnd` plus a `._xmot` of the same name (the lip-sync motion, an EMotionFX `XPM` stream made by the "Motion FX - Lipsync Batch Generator"), `.../SVM/<voice>/SVM_<voice>_<phrase>_<Language>._xsnd/._xmot`
(set phrases like `Attack00`), plus `compiled_speech_<language>_sounds.bin` / `_motions.bin` (header tables, `GH04`, 2.5 MB). `data/compiled/sounds/<group>/sfx_*._xsnd` are the sound effects (names end with `%_SW` / `_2D` flags). The raw sources (`data/raw/speech_*`, `*.wav`) are not shipped; `Importer.dll` ("compiler mode": `importsounds`) holds the converters.
`StartFacialAni(title)` plays `<title without path>.xpm` as a body-part animation (part 10, the face): **a line without `._xmot` plays fine** (no error, no crash; the face just does not move: inferred, not visible).

**The compiled sound container** (`._xsnd`; shipped speech files and a file made by this layer behave the same): `"GR01"` `"SN04"` (content version, checked by `eCSoundResource2::OnLoadData`), `u32 0x28` header size, `u32` size of the property stream (151), `u32` offset of the payload (`0x28 + 151 = 0xbf`), `u32` payload size, `FILETIME` (8), the extension of the source file
(`.wav\0` + `fe fe fe`: 8 bytes), then the property stream of an `eCSoundResource2` (the object-stream format of §26: class name, property schema `Duration long`, `Flags char`, `Links bTObjArray<eCSoundResource2::SLink>`, values), then **the audio file unchanged**. Properties: `Duration` (u32, **milliseconds**, `+0x58`), `Flags`
(u8, `+0x5c`: bit0 = 2D, bit1 = software; `OnImportData` sets them from the part of the file title after `%`: `_2d` -> 3, `_sw` -> 2; speech has 0), `Links` (sync points of 2D sounds, empty for speech). The 151 stream bytes are identical in every shipped speech file except for `Duration`, so `buildXsnd` emits them as a constant.
`OnLoadData` hands the payload to FMOD (`fmod.dll` **2.1.10**, `FMOD5_*` exports) as a memory file (`eCAudioSound::Create(SCreateFromMemory)`, the FMOD `Sound*` is at `+0x80` of the resource: `+0x70` the `eCAudioSound` member, `+0x10` in it) and **FMOD recognises the format by content**: the shipped payloads are MP3,
and the user's samples as **Ogg Vorbis (FMOD sound type 10), MP3 (9) and WAV (8-bit and 16-bit PCM, float32)** were all found, opened, loaded (resource state 3) and played (`risen.assets.sound`: `fmod` true, FMOD length 1160 ms for a 1161 ms `Duration`). So no transcoding is needed. `OnImportData` (`0x6938b0`, the editor/compiler path) copies the file only when FMOD reports one of two
sound types (constants of the original build, which do not match FMOD 2's numbering: inferred stale) and otherwise tries to decode and re-encode (`"Encoding: ... @ 0x80-0xc0 kbps [VBR]"`), whose encoder is missing in this build (`"Could not encode wave data"`): not used. The file time and the extension label are not checked.
The layer measures the length itself (Ogg: granule position of the last page; WAV: data size / byte rate; MP3: sum of the frames) and stores it as `Duration`.
* **`Duration` of a shadowed shipped resource stays the shipped value**: for names that are in the header tables (`state 1` before anything loads them) the resource keeps the `Duration` of the table entry; the *audio* is read from the loose file (`risen.assets.resource(name).path` names it, FMOD plays 300 ms where the shipped line has 1188 ms). A name that is not in the tables (everything this layer makes) takes the `Duration` of its own file.
* Shadowing only works while the resource is **not loaded yet**: the hero's own lines (`PC_Hero`, `Interacts`) are prefetched at the start of a new game and kept the shipped audio; an NPC line (`Chapter3`, `Eldric`) took the loose file. The same limit as the meshes of §26.

**What the layer does.** `Entity::GetSampleTitle` (`?GetSampleTitle@Entity@@QEBA?AVbCString@@V2@@Z`) is hooked in `installScriptEventHooks` (Script.dll is loaded then): the key is read before the call (the callee destroys the by-value string), the original runs, and for a registered key the returned `bCString` is destroyed and rebuilt in place with `INFO_SPASS_<mod>_<key>_<Language>_%_SPASS.wav`
(the variant for the current voice language: voice language, else `default`, else English, else the first by name). The compiled file of that name lives in `<spass>\generated\<mod>\voice` (mounted at startup in every later process; written and remounted at once by `import`); the file is deterministic (fixed `FILETIME`), an identical file is not rewritten. The title keeps the shipped shape `INFO_..._%_...` (the engine reads `INFO_`/`SVM_`
in `OnLoadData` to choose hardware/software playback). Keys of dialogues made with `risen.dialog.create` are `<option name>_<line number>`; `risen.voice.title(key)` gives the raw name for a `SaySound` line. `trace` channel `voice` logs every title the engine asked for and what was returned.
`risen.voice.say(entity, key_or_sound)` calls `gCScriptRoutine_PS::StartOutput(name, type)` on the entity's `gCScriptRoutine_PS` directly: the sound starts at once (`speaking` true, then false after the audio length), without animation, subtitle or lip sync.

**Verified live** (`ex_voice`, 22 checks in each of English and German; probes before it): title composition and replacement; ogg/mp3/wav(float32, 8 and 16 bit) found + opened (`-TraceFiles`) + loaded + FMOD-created; per-language variant by voice language (`-VoiceLanguage German` writes `<Language VoiceLanguage="German">` into the test copy's `ConfigUser.xml`; the engine then asked `..._German_...`);
fallback order; duration/pacing as above; `SaySound` with a loose mp3; bark; refusal of unusable files before anything is created; a shipped NPC speech file shadowed by a generated one (audio from the loose path). **Not verified:** what it sounds like beyond the user's report that the test tones are audible ("beep-boop"); lip sync; stereo or high-rate files (any file FMOD opens should work); Ogg Opus/FLAC (refused, not tried); speech
in a savegame-restored conversation (dialogues are re-created on `load.before`/`session.start` like all runtime infos, which also re-registers the voice keys).


## 29. Spells: items, the cast path, projectiles, hit scripts [confirmed in-game unless marked]

Driven by `src/game/lua_spell.cpp` (natives of `risen.spell`), `src/game/spell_lua.h` (catalogue, the `spells` / `new_spells` sections, per-shot overrides), verified by `mods/balance_spells` (scenario `spells`, two game processes) and `mods/example_spell` (`ex_spell`), the dump by `mods/spelldump` + `tools/gen_spell_properties_doc.py` ([SPELL_PROPERTIES.md](SPELL_PROPERTIES.md)).
Decompiled (headless Ghidra): `Script_Game.dll` (the spell scripts; the project `B:\DevDrive\risen-ghidra\ScriptGame`, `run-dump-sg.ps1`), `Script.dll` (`PSCastInfo`, `PSProjectile`, `PSInteraction::PlayerUseStack`), `Game.dll` (`gCProjectile2_PS::Shoot`, `gCDamage_PS::DoDamage`); `tools/ghidra/DataInit.java` names the script behind a global
`eCScriptProxyScript` (`DAT_1802cf2b0` = `DoFireballAreaDamage`: the dynamic initialiser constructs it from a string), `Callers.java` lists callers (made robust against data references). RVAs of the 64-bit build.

**There is no spell object.** `gCItem_PS.Spell` and `gCInteraction_PS.Spell` (`eCTemplateEntityProxy`) are `(no template)` in all 598 / 605 shipped templates. A spell is a group of items and scripts (full tables: SPELL_PROPERTIES.md):
* The item in the hand carries a `gCCastInfo_PS` (71 templates: 3 crystals, 18 runes + 14 teleport runes, 18 scrolls, 18 orbs ...): `ManaCost`, `ConsumeItem`, `CastItem`/`CastAmmo`/`CastFail` (item templates shown while holding / aiming / failing), `StartTask`, `FocusMode`, `CastInfo`. `PSCastInfo` (Script.dll) is an `Entity`-like holder of the *template entity* of the item and reads the set through the entity's vtable (`+0x198`, set id `0x55`);
  offsets in the set: `ConsumeItem` `+0x38` (bool), `ManaCost` `+0x3c` (int), proxies `CastItem` `+0x20`, `CastAmmo` `+0x28`, `CastFail` `+0x30`.
* **`PSCastInfo::CanCast(caster)`** (`0x219760`) = `ManaCost <= PSSkills::GetTotal(caster, Atrib_MP)`. **`PSCastInfo::Consume(caster, stack)`** (`0x2194f0`): with a valid item and stack `!= -1` it subtracts `ManaCost` from the caster's **base** mana (`gCSkills_PS::SetSkillBaseValue(1, base - cost)`) and, when `ConsumeItem`, sets the stack's amount to `amount - 1` (when that reaches 0 it first `UnEquipStackFromSlot(10)` -- the *rune* slot --, clears the hands if they hold the stack, then `gCInventory_PS::SetAmount(stack, 0, true)`).
  Every cast script calls it: `OnShootMagicFireball/Frostball/MagicMissileLeft/Right`, `PS_Summon_Skeleton/Inferno/Illusion`, `PS_Cast_Interact`, `PS_Cast_Joke`, `InventoryUse_Player_HealMinor/Medium/Enhanced/Light/Protection/Berserker/Beast/Levitate/Nautilus`. The script `CanCast` (`0x1257f0`) wraps the check with the anti-magic zone and the log line `NoMana`. `GetManaGoldCost` (`0xe2410`) is a trader script.
* **Skills**: `gESkill` `Circle` 21 (`0x15`), `Fireball` 22, `Frost` 23, `Missile` 24. `IsMagicUser` (`0xc3d70`) = `Circle > 0`; `CanFightWithMagic` (`0xc3e40`) = a stack in equip slot `0xb` (11). `EquipBestMagicWeapon` (`0xb1b20`): the best of Fireball/Missile/Frost decides which crystal `AssureItems` creates and equips (so magic NPCs do not list crystals in their templates: only `Skeleton_SitDead_Berengir` carries a spell item, one scroll).
* **Equip slots** (`gEEquipSlot`, seen as `item.equipped(owner, template, slot)`): crystals and the scrolls cloned from them: **11** (`InventoryUse_Player_Magic` -> `EquipStackToSlot(stack, 0xb)`, `PS_Magic`); runes and scrolls of `UseType` `Cast`: **10** (`InventoryUse_Player_Cast`, `PS_Cast`) -- slot 10 is inferred from the decompilation, slot 11 was seen live.
  `PSInventory::FindStackIndex(gEEquipSlot)` (`0x223c30`) gives the stack of a slot; `PSInventory::GetTemplateItem(stack)` the item's template entity.

**The projectile cast (Fireball, Frost, Missile), decompiled and driven live.**
* `InventoryUse_Player_Magic`/`QuickUse` equip the crystal and start the player state **`PS_Magic`** (draw), whose own transitions lead to `PS_MagicBall` -> `PS_MagicBall_Reload` (`OnReloadMagicBall` `0x18db90`: `HoldRightStack(AssureItems(CastAmmo))`, the failure ammo `CastFail` when `CanCast` is false) -> **`PS_MagicBall_Loop`** (`0x18ebf0`, polls the input events; the shoot input leads to `PS_MagicBall_Shoot`
  (or `..._Fail` without mana) which runs the shoot animation (`sAICombatMoveInstr`) and then sets the state `StartTask` of the crystal again. The animation event calls **`OnShootMagicFireball`** (`0x18dcc0`; Frost `0x18e1f0`; Missile `OnShootMagicMissileLeft/Right` `0x1906a0/0x190830`): `PSCastInfo::Consume(castinfo, caster, FindStackIndex(0xb))`, then `Entity::Spawn(template of the "other" entity, pose of the Slot_Ammo bone)`, aim (the camera ray for the player, `PSProjectile::GetShootDirection` for NPCs),
  `PSProjectile::Shoot(projectile, caster)` -> `gCProjectile2_PS::Shoot` (`0x954170`: needs the projectile property set `0x46`, starts the flight, calls the projectile's `ShootScript`). Frost also sets the projectile's `DamageBonus` to `12 * Frost skill`. The player's fireball speed factor is `10 * Fireball skill` (`OnMagicFireBallGetSpeedFactor`).
* **The "other" entity is not the ammo item.** The spawned template is `GetTemplate(other)` where `other` is the projectile of the **combat move** (`gCCombatMoveShoot` has `AccessProjectile` (`eCTemplateEntityProxy`), `OnShoot`, `OnCalcShootAngle`, `ShotPower`, `ShootPoint`: part of the combat species data, not of any item). Calling `OnShootMagicFireball(player, ammo_item_entity)` by hand spawned an `It_MagicAmmo_Fireball` that
  just lay there (it has no `gCProjectile2_PS`); the real shot always spawned **`It_Projectile_Fireball_S0`** (Fireball skill 1, 2 and 3 alike: `S1`/`S2` exist with other velocity/radius but were never spawned). Patching `CastAmmo` of the crystal to another ammo did **not** change the spawned template; patching `CastItem` to `It_Cast_Frost` broke the shot (nothing spawned).
  Consequence: the projectile numbers are per *spell type*; a **new crystal** cannot have its own projectile template -- the layer writes the numbers into the *projectile entity* of each shot instead (`entity.spawned`, shooter = `PSProjectile::GetShooter` `0x2309e0`, equipped crystal = `FindStackIndex(11)`).
* **Hit scripts.** `gCProjectile2_PS.OnHitScript` (`OnHitFireball_S0/S1/S2` `0x1945c0/0x1947b0/0x1949a0`): effect, then **player shooter**: `DoFireballAreaDamage(projectile, victim, 100/200/300)` (`0x1942d0`; the argument is the radius): for every NPC (`Entity::GetNPCs`) except the shooter whose bounding sphere intersects the sphere at the projectile's position, the projectile's `DamageMultiplier` is set to
  `min(1, 1 - (distance - victim radius) / radius) * original` (never below 0) and `PSDamage::DoDamage(projectile, victim, shooter)` runs; **NPC shooter**: `PSDamage::DoDamage(projectile, victim, shooter)` directly. `PSDamage::DoDamage` = `gCDamage_PS::DoDamage` (`0x8081b0`): script `DamageScript(self = projectile, other = victim, arg = (int)((DamageBonus + DamageAmount) * DamageMultiplier))`
  (`+0x44 + +0x40`, `+0x48`). `DoMagicFireballDamage` (`0xdb260`): `CanDoProjectileDamage` (**needs an aggressor**: a projectile spawned by `risen.entity.spawn` has none and does no damage), then, when the multiplier is at least 1, `arg += 25 * Fireball skill of the aggressor`, the effect `eff_projectile_fireball_damage_01`, `StopFrozen`, the generic **`DoDamage`**
  (`0xd8060`: `arg - victim's protection(DamageType)`, at least `DamageMultiplier * DAT_18027f4bc`, 0 for an `Immortal` victim, halved for party members, `PSSkills::SetHitPoints(-amount)`), a stumble (`DoMagicFireBallStumble`) by `(arg + 600) * 5`. `DoMagicFrostDamage` (`0xdb4b0`): `arg + 15 * Frost` and the freeze (`StartFrozen`) for `3 * (Frost - ProtIce / 10 + 3)`;
  `DoMagicMissileDamage` (`0xdb7a0`): `arg + 15 * Missile`, a stumble by `(arg + 300) * 5`.
* **Verified live** (`spells`, `ex_spell`): with the player's Fireball skill 3 and a victim with 24 magic protection standing where the projectile is, `DoFireballAreaDamage(projectile, nil, 100)` -- exactly what the hit script calls -- took **151** (shipped: 100 + 25 * 3 - 24), **201** with `DamageAmount` 150, **351** with 300; the `entity.damaged` events agree (`raw` = `DamageAmount`).
* **Driving a real cast from a script** (the discovery that made all this testable): `risen.item.equip(player, crystal)`, `player:set_state("PS_Magic")`, wait for the state `PS_MagicBall_Loop` (~360 frames = 4.5 s), `player:set_state("PS_MagicBall_Shoot")`: one shot takes ~80 frames, the mana drops by `ManaCost` exactly (200 -> 192 for 8, then 184, 176), exactly one `It_Projectile_Fireball_S0` is spawned per shot (`entity.spawned`), and the projectile decays after ~2 s.
  A `Shoot` state set while the player is still drawing is swallowed (the state machine goes on to `PS_MagicBall`/`Reload`): wait for the loop first.

**Rune spells.** The use scripts `InventoryUse_Player_*` and the cast states (see above) are selected by the interaction machinery (`PSInteraction::PlayerUseStack(stack)` `0x221c40` -> `gCInteraction_PS::BeginInteraction(item template entity, 2, stack)`; the script name composition for a given item template was **not** found: a *cloned* rune/scroll template (new name) was only checked for its numbers and the engine's cost accounting, not cast through the interaction).
Fixed numbers in the scripts: heals 50 / 100 / 150 (`HealParty(amount)`), `StartProtection(180)` (+20 to skills 10..15 = ProtEdge..ProtMagic via `SetModification`), `StartBerserker(60)`, `StartTransformBeast/Nautilus(180)`, `StartLevitating(180)` (a duration argument; unit not verified).
**Verified:** `risen.spell.can_cast` follows the edited cost (mana 5: rune cost 7 false, scroll cost 3 true), `risen.spell.consume` (`PSCastInfo::Consume`) takes exactly the cost (7, 3, 20, 10, 15, 7, 0), a rune stays (`ConsumeItem` False), a scroll / orb loses one item (2 -> 1).

**Engine quirk: the last scroll of a spell cast from slot 11 stays.** `Consume` unequips from slot 10 only; for a clone of a crystal (`UseType MagicFireball`, slot 11) the stack stays equipped, `SetAmount(0)` leaves an **emptied stack behind** (`risen.item.count` 1, `risen.item.take` "removes" the amount again and again and changes nothing, `DeleteItems` returns true,
no `inventory.removed`), and the scroll could be cast forever at a mana cost. `gCInventory_PS::DeleteStack` (`risen.spell.discard`) removes it (the stack count of the player went 12 -> 11). The layer therefore removes the last scroll of a *new projectile spell* when the shot is over (unequip + `PS_Normal` + `discard`, 30 frames later).

**Layer facts.** The per-shot handler needs the shooter at the moment the `entity.spawned` event is dispatched (end of the frame of the shot): `PSProjectile::GetShooter` is set by then. The amount decrement of a scroll is seen as `inventory.changed(delta -1)` before `entity.spawned` of the same shot (FIFO queue), so a shot without a decrement is the last one.
New spells are `risen.template.create` clones (§25): crystal/rune `<name>`, scroll `<name>_Scroll`, orb `<name>_Orb`. **Savegames** (verified in a fresh process): a crystal, scrolls, a rune and orbs of new spells in the player's inventory resolve again after a restart (templates are created on the first `tick`), the saved scroll casts with the new cost and double damage and is used up one by one.

**Not verified / limits.** NPC shooters (a mage never cast on its own in the tests: it picked the club; forcing the NPC shoot state needs the aim argument of `OnShootMagicFireball`); the effect of a changed `effect`/`SweepTestRadius`/`StartVelocity` on the flight (only read back: the projectile entity of a shot of the new crystal carried `StartVelocity` 30 and `SweepTestRadius` 15, the plain crystal's 15 and 10; whether the flight code reads the entity's copy was not measured -- screenshots are black);
the Frost freeze and Missile damage in a fight (read, template numbers verified, hit not driven); heal/summon/status effects of the runes; `S1`/`S2` projectiles (never spawned: edits are applied to all three); what a rune spell clone does when used from the inventory screen.


## 30. Crafting recipes: stations, the craft functions, knowing a recipe [confirmed in-game unless marked]

Driven by `src/game/lua_recipe.cpp` (natives of `risen.recipe`, `risen.interact`), `src/game/recipe_lua.h` (catalogue, `recipes` / `new_recipes` sections), the `PSInventory::Synthesize` hook in `src/game/inventory_events.cpp`; verified by `mods/example_recipe`
(scenario `ex_recipe`, four game processes) and `mods/balance_recipes` (scenario `recipes`); the dump by `mods/recipedump` + `tools/gen_recipe_properties_doc.py` ([RECIPE_PROPERTIES.md](RECIPE_PROPERTIES.md)). Decompiled (headless Ghidra): `Script.dll`
(`PSInventory::CanSynthesize/Synthesize`, `PSRecipe::*`, `PSInteraction::BeginInteraction`), `Script_Game.dll` (`Interact_Player_Anvil/Forge/CoolWeapon/GrindStone/Goldsmith/Alchemy`, `PreInteract_Player_Anvil`, `CanInteract_Player*`, `InvFilter_Craft_*`,
`InvEvent_OnClick_CraftSelect`, `HasRecipes`, `OnPlayerSynthRecipe`), `Game.dll` (`gCRecipe_PS`, `gCCraftIngredient`, `gCInteraction::BeginInteraction`). RVAs of the 64-bit build.

**A recipe is an item.** 86 item templates carry a `gCRecipe_PS` (property set id `0x5a`): `Craft` (enum `gERecipeCategory`, +0x28), `CraftIngredients` (`bTObjArray<gCCraftIngredient>` at **+0x30**: `{ data*, count, capacity }`, elements inline, stride **0x18** = vftable, an
`eCTemplateEntityProxy` at +8 (`ItemTemplate`) and the amount (`ItemAmount`, unsigned long) at +0x10), `RequiredSkills` (`bTObjArray<gCSkillValue>` at **+0x40**, the element type of §23), `ResultItem` (proxy at **+0x50**), `ResultAmount` (+0x58). The ingredient element is a
reflected `bCObjectBase` (constructor `0x9a1d90`, destructor `0x9a1dd0`), so the accessor mechanism reads and writes `ItemTemplate` (by template name) and `ItemAmount`; writing builds elements with the constructor in a scratch buffer and calls
`gCRecipe_PS::SetCraftIngredients` (`0x5d1750`) / `SetRequiredSkills` (`0x5d17a0`) exactly like the item arrays of §23 (`risen.recipe.set_ingredients_raw`, `risen.item.set_skill_array(.., "recipe_required")`; a name the engine does not resolve is read back
different and refused before the array is touched; verified). The 86 recipes: smithing (8 each at forge, anvil, quench "cool weapon", grindstone), goldsmith 12, alchemy 11, scrolls 18, cooking 5, frying 8 (full list in RECIPE_PROPERTIES.md).

**"Known recipe" = the recipe item is in the inventory.** There is no learned-recipe list and no game variable. `InvFilter_Craft_<Station>` (Script_Game `0x162ed0` anvil, `0x162d90` forge, `0x163010` cool weapon, `0x163150` grindstone, `0x162b50` goldsmith, `0x1627f0` alchemy, `0x162c70` scroll, `0x162910`
cooking, `0x162a30` frying) is called for every **stack** of the inventory page: when the stack's template is a recipe of the station's category it sets the stack's select limit to `CanSynthesize` (anvil: at most 1) and returns it, otherwise -1 (not listed).
`HasRecipes(category)` (`0x177780`) asks whether any stack of type != 2 has such a template; the station's `CanInteract_Player_*` uses it ("NoRedHotRawBlade" / "NoRecipe"). Consequences: learning is giving the item (books, trainers, loot: ordinary item transfers), forgetting is losing it, a
savegame carries the knowledge as the item stack. **The shipped smithing recipes are the intermediate blades themselves**: `It_1H_Steel_Raw` (forge) has itself as the only ingredient and `It_1H_Steel_Hot` as the result, which is the anvil recipe for `It_1H_Steel_Shaped`, then `..._Shaped` (cool weapon)
-> `..._Dull` (grindstone) -> `It_1H_Steel_Sharp`; gems are the goldsmith recipes of their own ingredient (+ gold ore); no shipped recipe uses `It_IronOre`. A new recipe therefore needs its own recipe item (a clone of `It_Recipe_Health_01`, a written item).

**The check and the craft** (Script.dll): `PSInventory::CanSynthesize(recipe Entity)` (`0x222af0`): 0 unless `PSRecipe::HasRequiredSkills` (`0x2320a0`: every `RequiredSkills` entry `PSSkills::GetTotal >= Amount`), else the minimum over the ingredients of
`PSInventory::GetItemAmount(template) / ItemAmount` (all stacks count). `PSInventory::Synthesize(recipe, count)` (`0x222d80`): for each ingredient with amount != 0 `FindStackIndex` + `gCInventory_PS::DeleteItems(stack, ItemAmount * count)`, a HUD item-log line, then
`gCInventory_PS::CreateItems(result template, count, 0)`. **It checks nothing and never reads `ResultAmount`** (live: a recipe with `ResultAmount` 3 made one item per craft). `OnPlayerSynthRecipe` (`0x176d80`, a script) wraps it with the HUD messages "MissingIngredients" (count 0) / "MissingSkill" (count < 0) / "HUD_Production".
`risen.recipe.craft` = the station's check (`CanSynthesize >= count`) + `Synthesize`.

**The stations are interactions** (`Object_Interact_Anvil` (use type Anvil), `..._Forge`, `..._Cooling`, `..._Animated_GrindStone`, `..._Goldsmith`, `..._Alchemy_Table`; the NPC smith version is `ZS_Smith`/`Interact_NPC_*`). The use key is `gCInteraction_PS::BeginInteraction`
(`0x8ba7f0`): the engine's `gCInteraction::BeginInteraction(user, mob)` (`0x8b7ee0`) runs the mob's `CanInteract_*` script (`CanInteract_Player` first: **a smith recipe item in the left hand** (`PSInventory::GetLeftHoldItem`) makes every mob but the matching station refuse with "CarryToAnvil"/
"CarryToForge"/"CarryToCoolWeapon"/"CarryToGrindStone", categories 6/7/8/9; then `CanInteract_Player_Anvil`: `PSSkills::GetTotal(Smith) >= 1` ("NoSkillSmithing"), `HasRecipes(7)` ("NoRedHotRawBlade"), `It_SmithHammer` in the inventory ("NoSmithHammer"; alchemy: Alchemy >= 1, `HasRecipes(0)`, "NoRecipe")) and then sets the **user's AI state
`ZS_Interact_PreInteract`**; that state runs `PreInteract_Player_*` (walk to the mob through `PreInteract_Player_Goto`, `GUI2::OpenAnvil` (page mode 4) which shows the inventory filtered by `InvFilter_Craft_*`, camera), `Interact_Player_*` (waits while `GUI2::IsBrowsingCraftSelect`, finds the stack
with `PSInventory::GetSelection > 0`, holds the hammer and the result as **temporary stacks** (`AssureItems(.., 1, 2)`, stack type 2), plays the `Anvil` animation phases, then `PSInventory::Synthesize(recipe item, selected amount)`), and `PostInteract`. The click on a recipe in the page is the script
`InvEvent_OnClick_CraftSelect(self = player, arg = stack)` (`0x151760`): `SetSelection(stack, 1)` and `GUI2::Close(6)`; **when the select limit is above 1 it opens a modal "how many?" dialog instead** (`GUI2::ModalStackAmount`; the anvil's limit is capped at 1, the alchemy table's is not).
**All of it can be driven from Lua**: `PSInteraction::BeginInteraction(Entity target)` (`0x221b50`, `this` = the character, the by-value Entity the mob; it selects the interaction type by "is the user the player") = `risen.interact.begin(user, mob)`, then `risen.script.call("InvEvent_OnClick_CraftSelect", player, nil, stack)`.
Live (anvil, ~13 s: ~500 frames to walk, ~550 for the animation, state back to `PS_Normal`; alchemy table ~16 s): the ingredients leave, the result arrives, one `crafting` event. Not driven: forge, cool weapon, grindstone, goldsmith (the same `Interact_Player_*` shape, decompiled), scroll writing, cooking, frying, NPC smiths.

**Temporary hold stacks.** The animations create copies of the hammer, the recipe item and the result item as stacks of **type 2** (`gCInventoryStack +0x38`) and leave them in the inventory afterwards (observed: an extra `It_SmithHammer`, recipe and result stack after every craft; the stack list grows, `HasRecipes` skips them, `risen.item.count`
does not count them, a quick save and load did not bring a duplicate back: counts were exact after loading). The inventory event hooks therefore ignore stacks of type 2 (before: spurious `inventory.added` events for the temporary copies). A smith blank in the left hand stays there after the craft ("carry it to the next station").

**The `crafting` cause and event.** `PSInventory::Synthesize` is hooked (installed with the Script.dll hooks): its scope names the cause `"crafting"` for the `DeleteItems`/`CreateItems` inside (one `inventory.removed` per ingredient, one `inventory.added`), then `crafting(crafter, recipe, result, count, ingredients)` is queued
(ingredients = the recipe's list times `count`, read from the recipe set before the call). Every station and `risen.recipe.craft` go through it, so it fires exactly once per craft (verified: counts and order in both scenarios, also for the real anvil and alchemy table). New symbols: `craft_ingredient_ctor/dtor`, `recipe_set_ingredients`, `recipe_set_required_skills`, `psinv_can_synthesize`, `psinv_synthesize`,
`psinter_begin/can/end` (all optional).

**Persistence** (verified in fresh processes, `ex_recipe`: save, load, load_missing, load_again): the recipe item and the crafted item are template clones with deterministic IDs (§25), created on the first `tick`; after a quick load the crafted swords (one equipped), the recipe item (known) and the `SPASS_RECIPE_<name>` game variable (given-once flag of `learn = true`, set by `risen.recipe.learn_once`)
are there with the numbers and labels of the mod; one more real craft works in the fresh process. Without the mod's content the stacks stay dormant (count 0, the game runs 400+ frames, a save works) and everything resolves again with the mod. Edits of shipped recipes (`recipes` section) are per process (the template data), re-applied on the first tick and on every `session.start`.

**Not found / not done / inferred.** Which recipes a vanilla player owns at the start and where recipe items are found/bought was not investigated (items in the world and trader inventories like any other). Recipes of cooking, frying, scrolls and goldsmith only exercised through `risen.recipe.craft` / the shared check (alchemy and anvil through the real station). `ZS_Smith` (NPC smiths) and
the NPCs' use of recipes were not looked at. The recipe base `It_Recipe_Health_01` was the only base tried for new recipes; the page's hover text of a recipe item (its description, `ITEMDESC_<name>`) was not seen (screenshots are black).


## 31. Quick loot: interactions, focus, input, HUD, loot scripts [confirmed in-game unless marked]

Driven by `src/game/lua_interact.cpp` (`risen.interact.focus/set_focus/info/current`, `risen.entity.npcs`), `lua_input.cpp` (`risen.input`, the `input.action` event), `lua_hud.cpp`, `lua_config.cpp`, `call_trace.cpp` (trace channels `calls`, `hud`); verified by `mods/quickloot_driver`
(scenario `quickloot`, 42 + 6 checks, two game processes) against the standalone mod `mods/quickloot` ([QUICKLOOT.md](QUICKLOOT.md)). Decompiled (headless Ghidra, dumps in `B:\DevDrive\risen-ghidra\kwickloot\`, outside the repo): `Game.dll` (`gCInteraction`, `gCInteraction_PS`, `gCFocusModule2`,
`gCActionAdmin`, `gCHudSystem2`), `Script.dll` (`PSInteraction`, `Focus`, `Input`, `GUI2`), `Script_Game.dll` (the interaction scripts, `AutoLoot`, `InvFilter_Loot`, `Unlock`, `CanOpenLock`, `OnNormalHandleInput`, `PS_Normal_Loop`, `AssessTheft`, `AssessPlayerLooting`). RVAs of the 64-bit build.
Reference for the Gothic 2 mod this follows (Kwickloot): `B:\DevDrive\risen-ghidra\kwickloot\REPORT.md`.

**Interactions.** Every interactive entity has a `gCInteraction_PS` (property set id `0x28`; `GetInteractionCount/At` `0x8baec0/0x8baea0`) with the property `UseType` (`gEInteractionUseType_Chest`, `_Cupboard`, `_Item`, `_NPC`, `_Anvil`, `_Bed`, `_PickOre`, `_PirateTreasure`, `_None`
for sarcophagi) and a list of `gCInteraction` objects (reflected: `Type`, `CanInteractScript`, `PreInteractScript`, `InteractScript`, `PostInteractScript`; `risen.interact.info`). `PSInteraction::BeginInteraction(mob)` (`0x221b50`) -> `gCInteraction_PS::BeginInteraction(user, mob, type, 0)` (`0x8ba7f0`: type 1 =
the user is the player) walks the **mob's** list for entries of that type and runs `gCInteraction::BeginInteraction(inter, user)` (`0x8b7ee0`) on each until one returns true: the mob must have a free user slot, the `CanInteractScript` (a plain script, `fn(user, mob, int)`, empty = yes) must accept,
then the user's own `gCInteraction_PS` remembers the mob (`+0x1b8` proxy = `GetCurrentTarget` `0x8bf270`, the mob's `+0x1a8` = `GetCurrentUser`) and the user's AI state becomes `ZS_Interact_PreInteract` (-> `ZS_Interact_Interact` -> `EndInteraction` `0x8b7e10`: the `PostInteractScript`, targets cleared).
So the use key runs the **first player interaction whose can script accepts**: a living human has `Talk` (`CanInteract_Player_Talk`) before `Loot`, a bed `Sleep` before `Talk`, a pirate treasure `Interact_Player_PirateTreasure` (digging) before the chest interaction.
`PreInteract_*` and `Interact_*` are **AI functions** (`bool fn(bTObjStack<gScriptRunTimeSingleState>&, gCScriptProcessingUnit*)`): resumable scripts that count their steps in the stack frame and return false while an animation/walk runs (`sAIPlayAniInstr`, `CallAIFunction("PreInteract_Player_Goto")`), so they
cannot be run to the end synchronously; plain scripts (`CanInteract_*`, `PostInteract_*`, `AutoLoot`, `InvFilter_Loot`, `AssessTheft`, ...) can (`risen.script.call`).
**Census** (scenario probe over every live world instance of every container-like template of a new game, ~400 containers): all chests (`Object_Interact_Animated_Chest_Lizard/Medium/Poor/Rich/Sailor/Ship`, 268 instances) and cupboards (`..._Cupboard`, `_Cupboard_Lizard`, 61) have **one** player interaction each,
`CanInteract_Player_Chest|Cupboard` / `PreInteract_Player_Chest|Cupboard` / `Interact_Player_Chest|Cupboard` and **no `PostInteractScript`**; plain sarcophagi (`Object_Interact_Sarcophagus`, 21) `CanInteract_Player_Sarcophagus` / `PreInteract_Player_Sarcophagus` / `Interact_Player_Sarcophagus` (no lock);
`Object_Interact_Sarcophagus_Titanlord` (1) the same with `CanInteract_Player_SarcophagusTitanlord` (needs the quest `Ursegor_Freedom`, else the message "BadIdea"); `Object_Interact_Sarcophagus_Trap` (14, **no inventory**) has no PreInteract and `Interact_Player_Sarcophagus_Trap`, which sets the state graph to "Destroyed", spawns a hidden
`Skeleton` and gives it the routine `Rtn_Sarcophagus_Spawn` -> `ZS_Sarcophagus_Trap` (the lid animation, then the skeleton attacks); `Object_Interact_Animated_PirateTreasure` (19): two player interactions (the first `CanInteract_Player_PirateTreasure` / `Interact_Player_PirateTreasure` = digging, the second the chest);
grave stones `Interact_Player_Tombstone`. **There is no chest "state script" or trap in the chest interactions**: what a chest does besides moving items is the lock (below), the NPC theft assessment (below) and the animation. The only locks with a script are `OnLockStatusChanged = OnQuestSuccess_UnlockGregsTreasureChests`
(the chest `..._Sailor_Tarras` and the pirate treasure `..._Barnabas`, key `It_Key_PattysPassepartout`); 16 chests are locked with a named key (`It_Key_Alvaro/Jack/Leonardo/Monastery_Storage/Eldric/Gamestart/Gamestart_Sara/Buddy/GregsHouseHarbor/Berengir/RomanovsChest/PattysPassepartout`), the rest have `Difficulty` 1-3
(lockpick) or are unlocked. Item entities: `Interact_Player_TakeItem` (+ an `InventoryUse_*` and a Telekinesis entry); corpses: `CanInteract_Player_Loot` (AI mode `Down` (11, knocked out) or `Dead` (12)) / `PreInteract_Player_Loot` / `Interact_Player_Loot`, monsters `CanInteract_Player_Wolf` etc. (trophies) with the same pre/interact.

**What the animated paths do** (traces `calls,events`): *item*: `ZS_Interact_PreInteract` -> `ZS_Interact_Interact` (~80 frames: slow down, take animation), then `AssessTheft(npc, item)` for each witness, `entity.destroyed(item)`, `item.picked_up`, `inventory.added(cause pickup)`, `ResumePlayerTask` (the player state is
`PS_Normal` again); `Interact_Player_TakeItem` also stops the tutorial hint `takeitem` and plays `eff_item_<EffectMaterial>`. *Chest*: walk to the chest (`PreInteract_Player_Goto`), kneel, `FUN_1800ed3e0(chest, AssessTheft)` (the perception broadcast to the NPCs, see below), `Unlock`, the open animation, `GUI2::OpenLoot()`
(the loot window; `ZS_Interact_Interact` waits while `GUI2::IsBrowsingLoot`), the close animation, `ResumePlayerTask`; "take all" in the window is the script `BtnEvent_OnClick_LootAll` (`SetMaxSelection` + `PSInventory::LootSelections` on the interaction target: events with cause `"loot"`). *Corpse*: `PreInteract_Player_Loot`
(the player's AI mode becomes 5 = talk for the duration, the loot window opens at once, "NothingToGet" when `OpenLoot` finds nothing), the window, `ResumePlayerTask`. No HUD log line is printed by any of the three (trace channel `hud`: only quest/experience lines appear); the game's loot window and the pick-up have no item-log call.
**`Unlock`** (AI function, `0x10e8b0`): for the player and a `gELockStatus_Locked` lock: with the key template (`gCLock_PS.Key`) in the inventory (`PSInventory::HasItems`) **or `Difficulty` 0** it sets `Status := Unlocked` (the key stays) and plays `eff_ani_human_interact_lockpick_end_01`, otherwise it runs the lockpick mini-game `UseLockPick`.
`CanOpenLock` (`0x10ebf0`, part of `CanInteract_Player_Chest`) prints the HUD messages `NoKey` (difficulty out of 1..3 or no skill path), `NoSkillLockpick`, `TooDifficult`, `NoLockpickOrKey`/`NoLockpick`, `Impossible` and refuses. **Writing `gCLock_PS.Status` through the property mechanism does not run `OnLockStatusChanged`** (a script defined from Lua was never called):
the engine's own setter does, so a mod that unlocks must not do it for a lock that has such a script.
**Witnesses.** The player's current interaction target is what NPCs react to: `AssessPlayerLooting(npc, player)` (`0xbe900`, runs for every NPC that perceives the player) returns 0 for targets in AI mode 11/12 (corpses are no crime) and, for a target whose use type is chest/cupboard (`0x2e`/`0x32`) while the player stands at it (`PSNavigation.IsOnDestination`), calls
`AssessTheft(npc, target)` (`0xc1740`: monsters answer 0; humans may comment/attack/become hostile). `PerceiveItem` (`0xbc5a0`) accepts items within **1000 units**. A pick-up or chest opening that never sets the interaction target and does not run the broadcast `FUN_1800ed3e0(target, AssessTheft)` is therefore **invisible to NPCs**: that is the
safety property a shortcut gives up, so the standalone mod declines while a living human is within 1000 units.

**Loot functions.** `AutoLoot(self = looter, other = container)` (`0x1321e0`; the engine's animation-less loot, used by `Interact_Player_AutoLoot` for corpses that sank in water): for every stack (last to first) it calls `InvFilter_Loot(container, none, stack)` and, when that is > 0, `GUI2::PrintItemLog(item, amount)` and
`PSInventory::TransferAmountTo(stack, amount, looter)`; it returns the number of stacks. `InvFilter_Loot` (`0x163750`) lets a stack through when its category is not 0 or it is a mission item or `It_Gold` (stack type 0), or when it is a mission item (other stack types), and sets its select limit: the same filter decides what the loot window lists.
Live: a dead `Peasant_1_01` with an equipped club carries three stacks (`It_1H_Club`, `It_Fist_Left` (body part), `It_Joint`); `AutoLoot` took the club only (events `inventory.removed/added` with cause `"script"` and `other` set, 1 pair), the window's "take all" took the club only (cause `"loot"`), while **`risen.item.loot` (which raises the select limit of ALL stacks)
also took `It_Fist_Left` and `It_Joint`**: use `AutoLoot` (or the window's script) for corpses and containers. Knocked-out humans (`Defeat`) behave like dead ones (`CanInteract_Player_Talk` and `_Loot` answer 0 / 1 for both). The HUD lines of `AutoLoot` come from its own `PrintItemLog` calls (trace `hud`).
`risen.item.pick_up` (= `PSInventory::InsertStack`) from a Lua handler is safe inside the key event handler and inside `tick`; it gives the same events as the animated pick-up (`entity.destroyed`, `item.picked_up`, `inventory.added` cause `"pickup"`, `inventory.changed`), compared event by event in the scenario.

**Focus.** `gCFocusModule2` (`GetInstance` `0x82ebc0`, `GetFocusEntity` `0x82e3a0`, `SetFocusEntity` `0x82e2a0`: clears the list and inserts one entry; `Process` `0x82e4d0` rebuilds the list every frame) is what `Focus::GetFocusEntity` (Script `0x20faa0`) returns to `PS_Normal_Loop`, which
runs `PSInteraction::BeginInteraction(Focus::GetFocusEntity())` for the player event `0x16` and then `Input::SetActionKeyScript(OnInteractHandleInput)`. The auto-update flag (`EnableAutoUpdate`) is switched back by the engine, so `SetFocusEntity` alone lasts one frame: `risen.interact.set_focus(e, true)` hooks `GetFocusEntity` and pins the entity.
**A pinned entity that was removed made the game crash (`PSItem::IsValid` on freed memory, access violation in Script.dll `0x1e7041`)**: the pin is dropped from the `eCEntity::Kill` hook. `Entity:valid()` re-resolves by *name*; items are named after their template (`It_Apple`), so it finds another entity of that name and re-points the handle (observed: "too far (58772)"): do not use it for them.

**Input.** The input map (`gCActionAdmin`, found with `FindModule("gCActionAdmin")`; vftable layout: `eCEngineComponentBase` at +0, the `eIInputReceiver` base at **+0x18**: the exported `KeyPressed/KeyReleased(ulong key)` have `this` = that base, calling them on the module crashed the game) turns scan codes into the 88 `gEActionKey` values; the names are the table of
`gSActionKeyInfo` (`0x28` bytes, a `bCString` first) at Game RVA `0x11c1fb0` (`GetActionKey(name)` static, `GetActionKeyInfo(key)` static): 15 `WeaponMode`, 16 `PrimaryAction`, 17 `SecondaryAction`, 62 `Selection`, 29 `Inventory`, ... (`risen.input.actions()`). The state is `admin+0x60+key`;
`Input::IsKeyPressed/WasKeyPressedThisFrame/GetKeyPressDuration/GetCurrentActionEvent` (Script.dll, `this` = the exported data object `input`) read it. The default map (`risen.input.bindings()`, identical walking and with a weapon drawn): **left mouse button (scan 512) = `PrimaryAction`, right (513) = `SecondaryAction`, middle (514) = `WeaponMode`**, `Selection` = a virtual
scan code (789) that the options screen maps to a key. `gCActionAdmin::ProcessBegin` (every frame) sends each queued event through `SendActionEvent(const gSAction&)` (`0x802b40`; `gSAction` = `{ int key; int phase; float duration; float amount }`): phase 1 pressed (once), 3 held (**every frame** while down), 2 released, 4 analog (mouse look/move: ~2 per frame
on this machine even without input); it runs `gCCameraAdmin::OnAction`, then the key script of the player's current state (`+0x3a0`, replaced by `Input::SetActionKeyScript`: `OnNormalHandleInput`, `OnMeleeHandleInput`, `OnInteractHandleInput`, ...; each calls `OnPlayerGameKeyPressed` first for the common keys) and, when the script returns 0, `gCCharacterControl::OnAction`.
`risen.on("input.action", ...)` hooks `SendActionEvent`: a handler value other than nil/false skips the original completely (verified: the consumed press/held/release of an action never reached a later handler or the player; a declined one reached both). `OnNormalHandleInput` (walking, no weapon): `PrimaryAction` and `Selection` pressed (when `GUI2::IsBrowsing` is false or the map is open) -> player event `0x16` = begin the
interaction with the focus; **`SecondaryAction` pressed -> event `0x17`: `PS_Normal_Loop` runs `_AI_HoldInventoryItems` only when something is held in the left hand (a torch, a carried item), otherwise nothing happens** (so the right mouse button is free in normal mode and a mod may take it). With the weapon drawn (`PS_Melee*`) it is the parry/block action (`PS_Melee_Move`). `WeaponMode` toggles `PS_Normal_Loop` <-> `PS_Melee`.
`risen.input.simulate(name, down)` calls `KeyPressed/KeyReleased` at the input admin: the next frame delivers the same events as a physical press (verified with all four phases and the state changes). **Not driven: the OS level (a real mouse click, `eCInputAdmin`'s scan code translation).** Raw keys (`GetAsyncKeyState`, only while the game window is foreground) are only checked by the layer's own unit test of the key names.
`GUI2::IsBrowsing` (`0x2106a0`) is true while a page with level > 1 is visible; in the test copy the visible pages are `1` and `26` (always), so the game's own use key is ignored there (a simulated `PrimaryAction` starts nothing: the scenario checks only the delivery of the event); whether it is false during normal play on a real desktop was not measured.

**Player states.** `Entity:state()`: `PS_Gamestart` (the intro, AI mode `Talk`), `PS_Normal` / `PS_Normal_Loop` (walking, no weapon: `AIMode` `gEAIMode_Routine`), a drawn weapon is `PS_Melee*` (`PS_Melee`, `_Loop`, `_Move`), a bow `PS_Bow*`/`PS_CrossBow*`, magic `PS_Magic*`/`PS_Cast*`, further `PS_Inventory*`, `PS_Talk*`, `PS_Beast*`, `PS_Levitate*`, `PS_Routine`,
`ZS_Interact_PreInteract`/`ZS_Interact_Interact` during an interaction. **"No weapon drawn and free" = the state starts with `PS_Normal`.** `PSCombatSystem::GetCombatMode()` only reflects what is held, not the state.
NPC AI modes (`gCScriptRoutine_PS.AIMode`): `gEAIMode_Down` = knocked out (`Defeat`), `gEAIMode_Dead` (`Kill`), `Routine`, `Attack`. A monster killed with the `Kill` script stays in its attack state and keeps biting: set `ZS_Dead` and freeze it for tests. `Defeat`/`Kill` on spawned NPCs also fires `entity.defeated/died`.

**HUD.** `GUI2::PrintGameLog(text, type, flag, extra)` (Script `0x213b10`; the first byte of the exported `gui2` object must be 1) and `PrintItemLog(Entity, amount)` (`0x20fd60`): `risen.hud.log/item_log`. Types seen: 1 notes ("NothingToGet"), 2 experience ("Experience + 50") and warnings; there is no item-log call in the animated pick-up, chest or loot window path.

**Other fixes found on the way.** `Entity::Spawn(near)` (`Entity::Spawn(Template, Entity, bool, gEDirection)`, what `Interact_Player_Sarcophagus_Trap` uses) is built on `Spawn(at)`: `entity.spawned` fired twice for one spawn; the inner hook is muted now (the trap's skeleton is reported exactly once). `Entity::GetNPCs` returns a `bTObjArray<Entity>` (`{ data*, int count, int capacity }`), freed with `bCMemoryAdmin::Free`.

**Trace channels.** `calls` (hooks `eCScriptProxyScript::operator()` Engine `0x92cb0`, `gCScriptAdmin::RunScriptFunction(name, ...)` `0x963cf0`, `gCScriptProcessingUnit::SetCurrentAIState` `0x96bed0`) shows the script/AI function/state sequence of the engine for names matching `[debug] trace_scripts`; `hud` hooks the two `GUI2::Print*` functions. Both install only when their
channel is on at start. The focus system calls `CanInteract_Player_Loot`, `TE_Condition_*` and others for every candidate every frame: filter with `^` prefixes.

**Limits / inferred.** Not driven live: a physical mouse click and the HUD's look; weapon states other than melee (`PS_Bow*`, `PS_Magic*` are assumed to start with something other than `PS_Normal`: read from the state list of Script_Game); the engine's theft reaction itself (no human was made to see a quick pick-up: the mod vetoes instead, which the scenario checks); chests with `OnLockStatusChanged` scripts are declined, so what the engine passes to
that script (`other`) is unknown; `Interact_Player_PirateTreasure`/`ChestPirateTreasure` and `SarcophagusTitanlord` were only read.

## 32. Spawned entities are saved: persistent NPCs [confirmed in-game unless marked]

**Correction of §14.** "Entities created with `Entity::Spawn` are not saved" was wrong: they are saved and come back, **only their unique name is lost** (§14 looked them up by name). The M7 question "can spawned
entities be made part of the savegame?" therefore needs no trick: they already are. (Engine DLL RVAs below are `Engine.dll` / `Game.dll` / `Script.dll` as named.)

**Where a spawned entity lives.** `Entity::Spawn(Template, pose, temporary)` (Script `0x20e4d0`, read in the decompilation) does `gCWorld::GetCurrentWorld()->CreateDynamicEntity()` (Game `0x99a3e0`): the entity is
created in the world's **system layer** (`gCWorld::GetSystemLayer` `0x998be0`; the layer file name in a savegame is `SysDyn_{GUID}`), under the layer's context root, then `eCEntity::CreateFromTemplate`, `SetInsertType(0)`,
`SetToWorldMatrix`; `temporary = true` additionally calls `eCEntity::SetTemporary` (Engine `0x6a5d40`: flag `0x100000` in `eCEntity+0x28`, recursively). Hero, root and spawned entities share that layer.

**How it is written.** `gCSaveGameManager::WriteSaveGame` -> `gCWorld::WriteSaveGame` (Game `0x9960b0`: header, the list of removed native entities, then every sector with `+0x31` set -> `gCSector::WriteSaveGame`
`0x990ca0` -> every `gCDynamicLayer::WriteSaveGame` `0x8cfc20` (skips the temp layer `..._Temp`) -> `eCEntityDynamicContext::WriteSaveGame` Engine `0x69c330/0x69c530`). An entity is written only when **none of the flags
`0x100000` (temporary) / `0x8` is set and `0x80000` ("save game relevant", `eCEntity::IsSaveGameRelevant` `0x6a5830`, `SetSaveGameRelevant` `0x6a35c0`) is set**; a spawned entity has the last one by default. Each record is
`0x41` bytes of header (the entity's ID, its parent's ID, the ID of the creator template, a flag byte whose bit 0 is "native" = came from the world file, and the byte size of the data) followed by
`eCDynamicEntity::WriteSaveGame` (`0x6a0550`: version `0xd6`, matrices/boxes, then every property set that `IsPersistable`). **Neither the unique name nor anything but the IDs identifies the entity.** The container is
zlib (`RISNSG01` header, deflate stream at offset `0x1c`); in a savegame of a new game the `SysDyn` layer held three records (the layer root and the hero, both native, and our spawned peasant, non-native, parent = the root).

**How it is read.** `gCWorld::ReadSaveGame` (`0x995b80`) first kills the entities named by the saved "removed native entities" list (this is how dead world NPCs stay gone), then every layer reads its records
(`eCEntityDynamicContext::ReadSaveGame` `0x69bde0`): a **native** record is looked up by ID in the loaded world and read into it; a **non-native** one makes a new entity (`CreateSpecificEntity`), looks the *creator
template* up by ID, `eCEntity::CreateFromTemplate`, `eCDynamicEntity::ReadSaveGame`, restores the ID and attaches it under the saved parent. **If the creator template does not exist the new entity is killed**
(`eCEntity::Kill` in the same function): confirmed live, a process without the mod's cloned NPC template loads the savegame and the NPC is simply gone (no crash). A clone made by `risen.template.create`
resolves because its ID derives from its name (§25). The load does not go through `Entity::Spawn`, so **no `entity.spawned` event fires for a restored entity**.

**Verified live** (scenario `persistnpc`, `Entity:id()` / `risen.entity.by_id`; unit-tested logic in `tests/persistent_npc_tests.inc`):
* a spawned NPC comes back after quick save + quick load in the same process **and in a fresh process** with the same entity ID, the same position (also 2500 units away from where it was made), its inventory (items given
  after the spawn), its group, guild and AI state (`ZS_HangAround_Stand`), its dead state (a corpse comes back as a corpse with the same ID); hit points are saved too but NPCs regenerate within seconds (a peasant went from 200 to 500 in 2.5 s), so
  they cannot be compared. `add_level` did not survive (a level changed with `Entity:add_level` was the template's again after the load: skills are not a reliable place for state).
* the unique name is **not** saved: the restored entity's name is the template name (`risen.entity.get("MyNpc")` is nil, `Entity:name()` is `Peasant_1_01`), also for entities that were named with `Entity:set_name`
  (searching the decompressed savegame for the name finds nothing). `Entity:set_name` after the load works and quest/dialogue owners resolve through it again.
* `risen.entity.spawn(..., temporary = true)` is **not saved** (flag `0x100000`); `Entity:set_save_relevant(false)` (`eCEntity::SetSaveGameRelevant(false)`) makes the engine skip a normal spawn: both gone after a load.
  `Entity:save_relevant()` reads the `0x80000` bit (true for every normal spawn; also true for a temporary one, whose other flag does the work).
* `Entity:id()` reads the virtual `eCEntity::GetID` (vtable slot `0x148`/8, as `WriteSaveGame` does) = a 16 byte GUID + 4 byte counter; `risen.entity.by_id` is `eCSceneAdmin::GetEntity(id, 0)`
  (type `0` finds dynamic entities; `2` is for templates).
* a new game (`gCWorld::Restart`) and a quick load in the same process both drop the previous world's runtime entities, so nothing duplicates; items spawned by mods (`risen.entity.spawn("It_Plant_Berry", ...)`)
  are saved the same way. **Consequence for older mods**: every mod that "respawned its NPC on `session.start` when `risen.entity.get(name)` was nil" produced one more nameless copy of the NPC per load.

**`risen.npc.persistent`** (src/game/persistent_npc_lua.h, `docs/LUA_API.md` "Persistent NPCs") is built on exactly that and adds no engine hook: the record `{ id, template, version, home, last, dead, deaths, removed }` per NPC lives in the
mod's `risen.save` (the sidecar belongs to the savegame, so entity and record always match), the NPC is created only if the savegame does not know it, found by ID and renamed otherwise, 30 frames after `world.ready`
(the player exists a moment earlier). `entity.died` and a `save.before` check mark the dead state; a dead NPC is kept as a corpse unless the declaration's `respawn` says to replace it (the corpse is renamed to a throw-away
name and killed first, because a killed entity can keep its name until the end of the frame). A declaration that changes `template`/`version` replaces the saved entity; a record whose declaration is gone is
pruned (its entity killed). Verified live in four processes (save with a quick load and a new game inside, a fresh-process load, a load with a version bump + an undeclared NPC, a load without the NPC templates).

**Limits / inferred.** Verified once by a throw-away probe: a savegame loaded in a process where the NPC templates exist but nothing declares the NPCs (mod removed) shows all four as ordinary nameless NPCs (one live entity each). *Inferred (not run):* adoption of an entity that already carries the name and the
removal of a second entity with the name were only run against the unit-test mock. The record lives in the sidecar file `bin/spass/saves/<savegame>.lua`: a savegame copied without it makes the layer create the NPC
again next to the unnamed saved one. `gCSector::SaveGameRelevant` and the `0x8` flag in the write condition (`eCEntity+0x28`) were not investigated further (the default flags of a spawned entity pass them).
Only entities in the system layer were examined; native world entities are saved by their own mechanism (the "removed native entities" list, property sets `IsPersistable`).

## 33. Game variables, game events and conditional spawning (M8) [confirmed in-game unless marked]

**Game variables.** `gCInfoSystem::SetGameVariable(name, value)` (Game.dll `0x8b3570`) looks `name` up in the system's `bTArrayMap<bCString,long>` (`this + 0x78`, elements of 16 bytes: the `bCString` and the int at `+8`), overwrites the value or appends
a new element and sets a dirty byte at `this + 0x88`; `RemoveGameVariable(name)` (`0x8b3670`) deletes the element (read in the decompilation). The Script.dll statics `InfoSystem::Set/RemoveGameVariable`
(`0x215a50` / `0x215100`, what `risen.gamevar.set/remove` and the game's scripts call) fetch `gCInfoModule::GetInstance()->GetSystem()` and tail-call these members.
**The dialogue commands call the members directly**: `gCInfoCommandSetGameEvent::Execute` (Game `0x896580`) is `gCInfoSystem::SetGameVariable(this + 0x10 /* GameEvent */, 0)` and `gCInfoCommandClearGameEvent::Execute` (`0x887e00`)
is `RemoveGameVariable`; both first run `OnInfoStarted("IS_2_Running_ProcessInfo")` when the second argument is false. So **a "game event" is a game variable with the value 0** (the dialogue-commands note in §11 "creates the game
variable with value 0" was right: it exists but is not non-zero). A first hook on the Script.dll statics did **not** see `SetGameEvent` (verified: variable existed with value 0, no event); hooking the two Game.dll members covers every route.
`gamevar.changed(name, value, previous)` is posted through the event bus (queued, dispatched at the end of the frame) from detours of those members (`src/game/entity_events.cpp`; symbols `gamevar_sys_set`, `gamevar_sys_remove`, optional);
it is skipped when nobody listens, when the value did not change and while a savegame loads. Verified live: the real dialogue command (`SW_Open:0:nil`), `risen.gamevar.set`, `risen.gamevar.remove` (`SW_Open:nil:0`).
Variables restored by a savegame load are written by the load itself and not reported.

**Conditional spawning** (`src/game/spawn_when_lua.h`, `docs/LUA_API.md` "Conditional spawning") adds no engine hook besides the one above. It is pure Lua over the persistent NPC layer (§32), `risen.save` and the events:
* `risen.spawn.when{...}` declares a persistent NPC with the new `enabled` gate (`persistent_npc_lua.h`: a closed gate means reconcile neither creates, restores nor replaces the NPC; `Handle:ensure()` reconciles one NPC now and brings a
  `remove()`d one back). Without the gate a rule that was waiting would have created its NPC at the first reconciliation of every load; with it, nothing of the NPC exists while the rule waits, and a later in-process load of an
  earlier savegame (record rolled back, world rebuilt without the NPC) stays consistent although the Lua declaration of the persistent NPC survives the load.
* The rule state `{ state = "waiting" | "present" | "gone", spawns }` lives in the mod's `risen.save` (key `spawn_rules`), i.e. in the same sidecar as the persistent record, so the engine's saved entity, the persistent record and the rule state always come from the same savegame.
* Conditions are small Lua nodes with the events that can change them (`quest.status_changed`, `inventory.changed`, `gamevar.changed`, `entity.died/destroyed`, mod events, an internal "a rule's NPC changed"); an event only marks the rules
  that use it and the next `tick` evaluates them (`quest.status_changed` fires inside `gCQuest::SetStatus`; evaluating there would run engine calls inside engine code). Rules with a `poll` predicate are evaluated every n frames.
  Three-valued logic: a predicate that raises or cannot tell makes the answer "unknown" and the rule does nothing.
* After every `world.ready` the rules wait 40 frames (the persistent NPCs reconcile after 30) and are all evaluated once (`why = "load"`); a failed load restores readiness (`load.after(name, false)`).

**Verified live** (scenario `spawnwhen`, four game processes, 19 + 37 + 10 + 9 = 75 checks; `ex_spawn_when`, 22 checks), every statement with exact counts of live entities per template:
* *new game*: no rule NPC exists, the rules are `waiting`, `risen.save` has no record; the quest completed through the giver's real dialogue options creates the reward NPC exactly once (`on_fire` reason `condition`); a new game in the same process
  removes it, the quest and the records again; quick save "A" with nothing fired.
* *fresh process loads A* (an earlier save): nothing exists, every rule waits, no record. Then: quest (dialogue), items (2 tokens: nothing; 3: the tracking guard appears; 7: still the same entity; 2: removed; 3: a **new** entity at the same home),
  a game event (the dialogue command `SetGameEvent`: `gamevar.changed` seen, the visitor appears; `risen.gamevar.set("SW_Gone")` removes it for good; setting the spawn condition again does nothing; `reset()` lets it fire again),
  a death (`Kill` script) combined with `all{ dead(), negate(var()) }` (not while the variable is set, fires when it is cleared).
* *in-process quick load of the earlier save*: every rule NPC disappears (the world is the saved one), every rule waits, the records are gone, the victim lives again; the same triggers fire again, one NPC per rule, one `entity.spawned` each.
* *in-process quick load of the later save B*: one entity each, the same entity IDs as before the save, no `entity.spawned`, the rules `present` with `fired() == 1`, no `on_fire` call.
* *fresh process loads B*: the same; then the tracking rule removes its NPC when a token is taken away, `SW_Gone` removes the visitor, the one-shot reward stays.
* *fresh process loads B with edited rules*: a tracking threshold raised so the condition is false at the load removes the NPC (`on_despawn` reason `load`); a new `despawn_when` that already holds removes a one-shot NPC (`gone`);
  a rule added after the savegame was made fires at the load because its quest is already `Success` in the savegame (`on_fire` reason `load`); unchanged rules keep their entities (same IDs).
* Timing: 62 s (new game phase), 99 s (main), 33 s per load phase; the unit tests (`tests/spawn_when_tests.inc`, 10 tests) cover the same state machine against a mock world plus validation, three-valued logic, predicates with `on`/`poll`.

**Limits / inferred.** Not run live: `cond.item` with another holder than the player, `cond.alive`, a rule declared inside a running session (unit-tested only), `fn` conditions with `on` / `poll` (unit-tested only), a failed savegame load (the readiness
restore is inferred), a rule whose template is missing (the persistent layer warns once and tries again at the next session start: shared behaviour of §32). A savegame whose sidecar is missing knows no rule state: the rule waits and fires again when its condition holds.
Events are evaluated one frame after they happen. `Entity:kill()`ed rule NPCs are treated like vanished persistent NPCs (made again at the next session start); use `despawn_when` to remove one on purpose.

## 34. Dialogue conditions beyond quest state (M9) [confirmed in-game unless marked]

**The condition sequence.** A `gCInfo` owns, besides its `ConditionType` (`+0x50`, the quest-state switch of section 11), an array of `gCInfoCondition*` objects (`+0xa8`: data `+0xb0`, count `+0xb8`; `InsertCondSeqItem(gCInfoCondition*)` Game `0x8adad0` calls the object's vtable
slot `+0x80`, appends it and returns the index; `GetCondSeqItem(i)` `0x8adc40`, `GetCondSeqItemCount`, `RemoveAllCondSeqItems` `0x8adb90`). `gCInfo::AreConditionsFulfilled` (`0x8a6eb0`; read in the decompilation) switches on the ConditionType first (and fails with a message box when a quest-state type has no quest: section 11), then calls
**`IsSatisfied(gCInfo)` (vtable `+0xf0`) of every object in order and returns false at the first that fails** (an AND with short-circuit), then, for a `Parent` type, asks `AreChildInfosAvailable`. `gCInfo::IsAvailable` (`0x8a85e0`) is "not given, inside the chapter range (`+0x88` / `+0x8c`), `AreConditionsFulfilled`".
Everything is evaluated **synchronously, on the main thread, when the engine asks**: `gCInfoSystem` builds its list of offered options when a conversation opens and after an option finished; `Info:available()` / `conditions_fulfilled()` evaluate on their own, with or without a conversation. There is no caching and no event.

**The classes** (Game.dll exports `?CreateObject@gCInfoCondition<Class>@@SAPEAVbCObjectBase@@XZ` for each; `IsSatisfied` RVAs read in the decompilation; property names and types from `list_condition_properties` of a live object):

| Class | Properties (type) | `IsSatisfied` |
|---|---|---|
| `ItemAmount` `0x8a1b80` | `Owner` (eCEntityStringProxy `+0x10`), `Item` (eCTemplateEntityProxy `+0x20`), `ItemAmount` (long `+0x28`) | owner entity by **unique name** (`PC_Hero` for the player; the keyword `player` was not tried); `FindStackIndex(template)` -> the stack's amount (`& 0x1fffff`) `>= ItemAmount`; **`ItemAmount` 0 = the owner has none**; **an owner that is not found counts as satisfied** (no log) |
| `NPCStatus` `0x8a21c0` | `SecondaryNPC` (eCEntityStringProxy `+0x10`), `SecondaryNPCStatus` (`gEInfoNPCStatus` `+0x28`: `Alive` 0, `UnHarmed` 1, `Defeated` 2, `Dead` 3, `TalkedToPlayer` 4, `NotTalkedToPlayer` 5) | entity by name (missing: logged, false); `Dead` = `gCInfo::IsDead`, `Alive` = not dead; `UnHarmed` / `Defeated` read a byte of the NPC property set (`gCNPC_PS+0xe4`), `TalkedToPlayer` / `NotTalkedToPlayer` a flag of the partner's dialogue property set (`+0x40`) (the last four: read, not evaluated in a game) |
| `OwnerInArea` `0x8a2ab0` | `AreaPropertyName` (bCString) | the option's owner `IsInAreaWithProperty` |
| `OwnerNearEntity` `0x8a2e10` | `Entity` (eCEntityStringProxy) | distance owner - entity `<=` a float constant of the engine (`DAT_180f63378`) |
| `PlayerKnows` `0x8a31f0` / `PlayerNotKnows` `0x8a3530` | `PlayerKnows` / `PlayerNotKnows` (bCString: a **game variable name**) | the variable **exists** in the info system's `bTArrayMap` (`this + 0x78`, section 33) / does not |
| `QuestStatus` `0x8a37e0` | `Quest` (bCString), `CondType` (`gEInfoCondType` `+0x20`), `IsCurrentStatus` (bool `+0x28`) | quest by name (**missing quest: false, no message box**); the same cases as the ConditionType switch (the quest status ints 1, 2, 3, 5, 6, 7 are Running, Success, Failed, Cancelled, Lost, Won: verified live by setting each status); **a CondType it has no case for is satisfied** (default branch): never use `General` here |
| `Script` `0x8a4660` | `ScriptName` (`+0x10`), `Self` (`+0x18`), `Other` (`+0x20`), `Param` (int `+0x28`, default -1) | `GetEntityProxy1/2` resolve `Self` / `Other` with `gCInfo::GetEntityByKeyword` (`player`, `npc` or an entity name; cached in the proxies `+0x30` / `+0x40`), the `eCScriptProxy` at `+0x50` is resolved from `ScriptName` on first use (and again while it fails), `eCScriptProxyScript::operator()(self, other, Param)` **returns non-zero = satisfied** (`(int)` of the result) |
| `SkillValue` `0x8a4e10` | `Entity` (bCString: keyword or name), `CompareOperation` (`gECompareOperation` `+0x20`: `Equal` 0, `NotEqual` 1, `Less` 2, `LessEqual` 3, `Greater` 4, `GreaterEqual` 5), `SkillValue` (`gCSkillValue`, `Skill` `+0x38`, `Amount` `+0x40`) | `gCSkills_PS::GetSkillValue(skill)` (the total) compared with `Amount`; no entity / no skills: false |
| `TeachEnabled` `0x8a5770` | `TeachEnabled` (bool) | the conversation partner's `gCDialog_PS.TeachEnabled` equals it (inferred: a byte of the partner's dialogue property set) |
| `CanTeachSkill` `0x8a1630` | `TeachSkill` (`gCSkillValue`) | the player can learn that skill from the partner: the partner's teach list has that skill and the player's value plus the amount lies in its range (inferred) |

`SkillValue` / `CanTeachSkill` hold a **nested `gCSkillValue`**: the text format only prints it (`gCSkillValue`), so the member is reached through the exported `AccessSkillValue` / `AccessTeachSkill` and set with the property mechanism of **that** object (`Skill` = `gESkill_Stat_STR`, `Amount`; `gESkill` and `gECompareOperation`
names `gECompareOperation_{Equal,NotEqual,Less,LessEqual,Greater,GreaterEqual}` are strings in Game.dll). `bCAccessorProperty::SetString` returns normally for a value it cannot parse (the old value stays), so the layer reads every property back and compares.

**The Lua condition is a `Script` condition.** `gCInfoConditionScript` calls a script **by registry name**, exactly like `RunScript`: the script bridge (section 4) replaces the function slot of a registry entry created by `risen.script.define`, so the engine calls Lua when it evaluates the condition.
Verified: `ctx.self` is the entity named by `Self`, `ctx.other` the player for `Other = "player"`, `ctx.arg` = `Param`, the return value non-zero holds. A script error makes the bridge log an `[error]` line and answer 0 (the original of a `define`d script): the layer wraps the Lua part itself (`pcall` inside `risen.cond.test`) so a faulty condition is one
warning and "false" instead.

**What `risen.dialog.create{ when }` builds** (`src/game/dialog_cond_lua.h`; a per-mod wrapper around the shared `create`, so the voice wrapper and the validation keep working): the `risen.cond` node tree is split into engine conditions (item amount of the player or the owner, game-variable existence, skill value, NPC dead/alive, one quest state of six, and the negations of item-count-1 / event / skill)
and **one** `Script` condition for the rest, appended **last** so that the engine's own conditions short-circuit it. A persistent NPC's `dead` stays in Lua (its record can say dead although no corpse exists). On any failure while attaching, the info is marked `given` (never offered) and `create` returns `nil, reason`.

**Verified live** (scenario `dialogcond`, two game processes, 58 + 20 checks; `ex_dialog_when`, 19 checks):
* 32 options of every kind (item, none-of-item, owner's item, event, no event, var, skill and its negation, guild of the owner, dead and alive corpse (engine), dead persistent NPC (Lua), eight quest statuses plus a list, a quest that does not exist, combinations all / any / nested, predicates, a raw `OwnerNearEntity`, a base ConditionType combined with `when`).
  After each of a dozen world changes (ore given / taken, quest opened and run, a game variable set / removed, `Strength` set, the owner's guild changed, an item given to the owner, two kills, every quest status set through its `Status` property) **the engine's `Info:available()` equals the same condition evaluated in Lua for every option** (and the expected options are in and out); the conversation (`risen.dialog.start`) lists the same set, and the six native quest states
  each select exactly their option (so Running / Success / Failed / Cancelled / Lost / Won mean what their names say).
* Calls of the Lua script: none while nothing looks (150 frames), one per `available()` / `conditions_fulfilled()`, **one per option when a conversation opens**, **zero while an engine condition in front of it fails** (`Mixed`: ore and event first), one when they hold; the list a conversation shows does not change while it is open (`risen.dialog.available()` stayed
  the same after the world changed; `Info:available()` followed at once).
* A predicate that raises an error: false, one warning (`dialogue 'DC_FnError': condition 'predicate' raised an error ...`) and one summary warning, asked again on every look, no crash, the other options unaffected, no `[error]` line.
* Bad specs: `when = 5`, an unknown engine class or property, an unknown skill are refused before an info exists; an engine value that is not taken (`ItemAmount = "abc"`) leaves the option existing but never offered.
* **Save and load**: the options are defined in `load.before` through the persistent NPC's `define`; a **fresh process** that loads the quick save offers exactly what the saved world says (ore, variable, quest running, skill, apple on the owner, dead persistent victim; the unnamed corpse is gone, so `Dead` and `Alive` of it are both false in the engine and in Lua),
  then switches again when ore is taken / given back, the variable removed, the skill lowered; the Lua script is again not called while an engine condition fails.
* The example: ore AND trust AND guild: with ore but no bandit guild, the option is hidden; with the guild set it is offered, chosen it takes the ore (`Give` player -> npc), gives apples and XP and is gone (also after a quick save / load in the same process).

**Limits / inferred.** Not evaluated in a game: `OwnerInArea`, `TeachEnabled`, `CanTeachSkill` (attached, properties read back), `NPCStatus` `UnHarmed` / `Defeated` / `TalkedToPlayer` / `NotTalkedToPlayer`, the operators `==`, `~=`, `<=`, `>` of `SkillValue`, a named holder other than the player or the owner (Lua only), `OwnerNearEntity` being false (it was true:
the NPC stood 200 units away). `ItemAmount` uses the first stack of the template (equal to `risen.item.count` in every case tried: stackable items). The `item` of the player names `PC_Hero`; a mod that renames the player's entity breaks it. The decompiled conditions log through a stub, so a missing entity or quest is silent.
The old quest-state ConditionType still blocks with a message box when the quest is missing (`create` refuses it); the `QuestStatus` **condition object** does not.
