# Next steps

State at the time of writing (commit `de9b9d9`): hook layer, script bridge, localization overlay, entities, items,
NPC spawning/configuration, runtime dialogues and runtime quests all work in a live game on one machine (Risen
Enhanced x64, English text). Findings are in [`RE.md`](RE.md), the API in [`LUA_API.md`](LUA_API.md).

Priorities are ordered by how much they block real mods. "Verify" means: do it in the isolated test copy
(`scripts/run-windowed.ps1`) and confirm through engine state, as the existing demos do.

## 1. Must-do before mods can rely on it

### 1.1 Persistence across save / load
Nothing has been tested with saving or loading. Open questions and plan:
- Do runtime-created entities (`risen.entity.spawn`, `risen.npc.spawn`) end up in the savegame? Spawn an NPC, save
  (`World` has save/load calls in Script.dll: `QuickSave`, `LoadGame`), reload, and check `risen.entity.get(name)`.
- Do runtime-created quests survive? `gCQuestManager::ReadSaveGame` (`0x961f00`) runs at load: probably the quest
  list is rebuilt from data files, so a Lua-defined quest has to be recreated on every session start **before** the
  save is read. Find out when `ReadSaveGame` runs relative to our `mods.loaded`/`tick`, and add an event
  (`game.loading`, `game.loaded`) so mods can (re)create quests/dialogues at the right moment.
- Do runtime infos persist? `gCInfoModule::ReadSaveGame` (`0x8ae3a0`) restores "given" flags by info name, so infos
  must exist under the same names *before* it runs.
- Quest status and game variables are saved by the engine; the Lua state is not. Provide a small persistent store
  (`risen.save.get/set`, backed by game variables or a file next to the savegame).
- Guard pattern for mods: "create only if missing" (`risen.entity.get`, `risen.quest.exists`).

### 1.2 Robust events
Today there are `mods.loaded`, `tick`, `shutdown`. Add, with hooks on the engine functions noted in `RE.md`:
`session.start` (`gCSession::StartGame`/`Start`), `session.stop`, `world.changed`, `save.before/after`, `load.before/after`,
`npc.killed`/`npc.defeated` (`gCQuestManager::OnNPCKilled/OnNPCDefeated`), `info.started/finished`
(`gCInfoSystem::OnInfoFinished`, `ExecuteDialogInfo`), `quest.status_changed`, `item.picked_up`.
Hot-reload of `main.lua` (`restoreAll()` for script slots exists) is a development nicety worth adding.

### 1.3 Safety of the dialogue/quest helpers
- `risen.dialog.create` with a quest-state condition and a missing quest triggers a blocking engine message box
  (see `RE.md` §11). Refuse that combination in Lua (`risen.quest.exists(spec.quest)`) and surface a clear error.
- Validate loc keys and command lists before touching the engine; return `nil, "reason"` instead of logging only.
- Make `risen.entity` handles safer: store the entity's unique name next to the pointer and re-resolve on use.

## 2. Dialogue features
- **Branching conversations**: `Parent` infos (`gEInfoType_Parent`, `AccessParent`) with child options; verify
  `gCInfoSystem::AreChildInfosAvailable` and `GetChildInfoList`.
- **Conditions beyond quest state**: attach condition objects (`gCInfoConditionPlayerKnows`, `QuestStatus`, `ItemAmount`,
  `SkillValue`, `Script`) with `gCInfo::InsertCondSeqItem`; create them through their `CreateObject()` factories like
  `RunScript`. A Lua-script condition (`gCInfoConditionScript`) would make arbitrary conditions possible.
- **Engine-side effects without Lua**: `SetGameEvent/ClearGameEvent`, `Give`, `GiveXP`, `RunQuest`, `SucceedQuest`,
  `SetRoutine`, `MoveTo`, `Trigger`, `DoTrade` commands: expose their parameters (need the per-class `Access*`
  accessors; the property-by-name mechanism may already cover them).
- **Voice/sound**: `SaySound`/`SaySVM` and where audio is looked up (`data/compiled/speech_*`); needs the file-system
  work in section 4.
- **Comments / news / hello** infos (`gEInfoType_Comment/News`, `gEInfoCondType_Hello`) so NPCs speak unprompted.
- Trade and teaching setup (`SetTradeEnabled`, `SetTeachEnabled`, `DoTrade`, `PSInventory` stock) for shop NPCs.

## 3. NPCs
- **Reliable behaviour**: `set_state` gets overridden by the AI within seconds. Understand `gCScriptRoutine_PS` (current
  task, `AISetTask`, routine scripts `Rtn_*` and the NPC routine data in the world) so a spawned NPC can stay put,
  patrol, sit, sleep, follow the player or guard a point (`PSNpc:SetGuardPoint`, `GuardStatus`).
- **Relationships**: guild values beyond 0/1/3/5/7 (enum names via the property-by-name mechanism:
  read `PSNpc::PropertyGuild` as text), attitude lock, hostility/"enemy of player", party membership (`PSParty`).
- **Appearance**: head/body templates (`Hero_Head_*`), per-NPC overrides, armor templates; scale/rotation of the spawn pose.
- **Despawn / cleanup**: `Entity::Decay`, `Kill`, removal from the scene admin; avoid leaking runtime NPCs between sessions.
- **Spawn points**: resolve named waypoints/areas (`Entity::GetEntity`, roaming points) so mods place NPCs without raw coordinates.
- Find real NPC template naming: dump `data/common/templates.pak` properly (see 4.1) instead of a regex over the raw file.

## 4. New content (items, models, sounds, strings)

### 4.1 Read the game archives
`*.pak` files are `FFC` packed volumes (`G3V0` magic). Write a reader (or call the engine's own:
`CFXVirtualFileSystem::MountPackFile`, `FindFirstFileA`, `CreateFileA`) to list templates, quests, infos, strings, meshes.
This unlocks documented template/quest/info names for modders and a `risen.vfs.list/read` API.

### 4.2 Virtual file system mounts
`CFXVirtualFileSystem::MountDirectory(virtual, physical, flags)` is exported. Still unknown: the meaning of `flags`,
mount priority (can a mod directory shadow a packed file?), and the mountlist syntax in `mountlist_packed.ini`.
Goal: `<Risen>\bin\spass\mods\<mod>\data\...` is mounted automatically so loose meshes/textures/sounds/templates/strings
work. Investigate when the mounts are created (`Engine`'s VFS init) and install the hook before it.

### 4.3 New item and NPC templates
Spawning existing templates works. New templates need either loose template files through 4.2, or creating
`eCTemplateEntity` objects at runtime (`CreateNewEntity`, `CreateEntityCopy`) and registering them with the scene admin.
Cloning an existing template with changed properties (name, damage, value, mesh) is the realistic first milestone;
`PSItem` accessors (`GetGoldValue`, `GetIconImage`, `GetModSkills`) show what an item property set holds.

### 4.4 Localization completeness
The overlay is keyed by exact id and language. Add: CSV/JSON string files per mod (`strings/<lang>.csv`) loaded at start,
language-aware fallbacks, and the id conventions per table (`FO_`, `TUT_`, `HUD2_`, `GO_`, `QUEST_`, info ids) documented from
a trace of a full playthrough. Voice-language switching is separate (`SetVoiceLanguage`).

## 5. Platform and robustness
- **Other editions**: the layer targets Risen Enhanced x64. Check Risen Classic (32-bit, different DLL set) and decide whether
  to support it; symbol resolution fails closed, and a version report (`risen.version`, game build, missing symbols)
  should be logged and exposed.
- **Multiple Script DLL conflicts**: `LoadAllScriptGame` registers "first writer wins". Decide how Lua overrides interact with
  other native `Script_*.dll` mods (override after load is already used; document load order).
- **AI scripts**: `RegisterScriptAI{State,Function,Callback}` have different signatures and are not bridged; overriding AI
  states in Lua (e.g. `ZS_Ambient`) would give modders real behaviour control.
- **Sandbox**: audit what mods can reach (`risen.script.call`, file system via future `risen.vfs`); consider per-mod
  permissions and a manifest `api_version`. Today `unsafe = true` in `config.toml` lifts every restriction.
- **Threading**: hooks run on several threads; the Lua state is guarded by one recursive mutex. Measure per-frame cost of
  `tick` handlers and the loc hooks on large scenes.
- **Binary hygiene**: the proxy exports ~177 extra LuaJIT symbols (static `lua51.lib` carries export directives); strip them.
- **Tests**: unit tests for `mods.cpp` (load order, manifests) and the Lua runtime without the game; a CI build (Windows,
  vcpkg binary cache); a smoke-test script that launches the test copy and asserts on `spass.log` lines.

## 6. Tooling and documentation
- Generate `docs/SYMBOLS.md` from `src/game/symbols.h` (name, module, RVA, signature, verification status).
- Persist the Ghidra findings: export a Ghidra project symbol database or a script that applies the names/types used in
  `RE.md`, so the analysis can be reproduced without the long headless imports.
- A visual verification path: screen capture of the Direct3D 9 window returned black. Options: hook `Present` and dump the
  back buffer on demand, or use the engine's screenshot function (`ScreenShots` mount) from Lua.
- Developer guide: how to find and bind a new export (`tools/dump_exports.py` → `symbols.h` → binding), how to read a
  function with `tools/ghidra/DumpDecomp.java`, how to run the demos and interpret logs.
- Example mods that teach one thing each (hello, a fetch quest, a shop NPC, a translated string pack), and a mod template.
- Packaging: a single `install.ps1`-based release zip with the built `sentry.dll`, the sandboxed example mods and
  a "disable everything" switch (`config.toml` `enabled = false`) described in the README.

## 7. Known issues to fix or document
- `LogTopic` cannot be set through property strings (needs `set_topic`); other loc-string properties on engine objects
  (`gCInfoLocString`, `gCQuestLocString`, `eCLocString` members) behave the same way and need the `+8` base rule.
- `gESkillModifier` value `0` is *additive*; other modifier values (temporary buffs, permanent modifications) are unexplored.
- The first test of `risen.dialog` scanned condition values blindly and froze the game on an engine message box: keep all
  engine enum probing by name only.
- Spawned NPCs next to hostile guilds fight immediately; spawn helpers should offer a "peaceful" option (guild/attitude lock).
- Test environment: the game rewrites `%LOCALAPPDATA%\Risen\ConfigUser.xml` even for the test copy (the redirection through
  the mount list did not take effect). Solve by hooking the known-folder lookup, or document a backup step.
- A Steam relaunch of the real install was observed once during testing (see `RE.md` §13); `run-windowed.ps1` now sets
  `SteamAppId`. Ship the same safeguard guidance for modders who launch a copied install.
