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
  * **Entities created with `Entity::Spawn` are not saved**: the NPC was gone after loading (also in the same process, where the
    world is reloaded). Mods must respawn them (`session.start`, `load.after`).
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
