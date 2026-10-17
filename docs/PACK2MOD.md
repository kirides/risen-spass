# risen-pack2mod: converting a Risen mod's archives into a mod for this layer

`risen-pack2mod.exe` reads the packed archives of a Risen mod (the classic `.pNN` patch volumes and `.pak` files, container `G3V0`), compares them with **your own copy of the game's archives** and writes a normal mod for this layer: loose resource files, generated Lua data that stacks with other mods (through `risen.template`, `risen.balance`, `risen.dialog`, `risen.entity`), string files and a report that lists everything it could not convert.

Why: a mod that ships `.p00`/`.p01`/`.p02` volumes replaces whole template files (and with them everything other mods patched), cannot be combined with another such mod, is not savegame-safe when it changes, and needs a 32-bit-era install. The converted form carries only the differences, is applied at run time, and can be switched on and off in the Mods tab. See [EXTENDED_MOD_ANALYSIS.md](EXTENDED_MOD_ANALYSIS.md) for the analysis that led here.

**Licensing.** The repository ships only the converter. The output contains the work of the mod's author (numbers, texts, item and recipe design, meshes, textures, actors): it is a derived work, for private use on your own machine. **Do not commit it, do not redistribute it** (not even a "numbers only" extract) without the author's permission. The tool writes a `.gitignore` (`*`) and a `NOTICE.txt` into every output folder; `mods/_generated/` and `mods/risen_extended/` are git-ignored in this repository. The tool reads the mod as data (nothing in it is executed) and never writes to the game directory.

## Usage

```powershell
risen-pack2mod <mod folder | archive files...> --baseline <Risen data dir> --out <mods\name> [options]

risen-pack2mod C:\Mods\MyMod --baseline 'C:\...\Risen\data' --out 'C:\...\Risen\bin\spass\mods\my_mod' --id my_mod
risen-pack2mod C:\Mods\MyMod\common\templates.p00 C:\Mods\MyMod\common\templates.p01 --baseline ... --out ... --dry-run
risen-pack2mod C:\Mods\MyMod --baseline ... --list                      # every file of every archive: new / changed / same
```

| Option | Meaning |
|---|---|
| `--baseline DIR` | the Risen **data** directory of your own game (holds `common\*.pak` and `compiled\*.pak`; Risen Enhanced or the classic game; official `.pNN` volumes of the baseline are applied too). Read only. |
| `--out DIR` | the mod folder to write. The generated sub-folders (`data`, `strings`, `assets`, `assets_off`, `variants`) are replaced on a rerun, `config.lua` is kept. The tool **refuses** a non-empty folder it did not create (no `manifest.json`) and any folder inside the game's data directory. |
| `--id ID`, `--name TEXT` | mod id (default: made from the folder name) and display name |
| `--combat changelog\|shipped\|off` | which `compiled_combatspecies.bin` becomes the loose file: the oldest volume that ships one (the author's changelog state, default), the newest (what the last volume ships) or none |
| `--profile FILE` | JSON profile with knowledge about one specific mod (see Profiles) |
| `--strings all` | apply every differing string row (default: new rows and the rows of converted items) |
| `--no-assets`, `--no-world` | skip the (large) resource copy / the dialogue and world extraction |
| `--report FILE` | where to write `report.md` (default `<out>\report.md`) |
| `--list` / `--dry-run` | list the archives and files and stop / analyse and print the summary, write nothing |

Input: a folder (searched recursively for `*.pNN` and `*.pak`; the parent folder name `common` or `compiled` or a baseline archive of the same name decides the kind) or archive files. Volumes are applied in number order across all base names (`.pak` first, `.p00 < .p01 < .p02`), **later wins**; files are matched by their **bare lower-case name** (the mod's volumes are usually flat, the game's keep directories; the engine finds resources by that name, RE.md section 26). Files byte-identical to the baseline are dropped.

The tool is built with the layer (`cmake --build build --config RelWithDebInfo` makes `build\RelWithDebInfo\risen-pack2mod.exe`, zlib from vcpkg) and `scripts\package.ps1` puts it next to `sentry.dll` in the release zip. It needs the Visual C++ runtime that Risen itself needs, nothing else.

## What is converted

| Mod content | Output | How the generated `main.lua` applies it |
|---|---|---|
| **Existing item templates** that differ (`GoldValue`, `DamageAmount`, `ManaCost`, requirements, effect lists, use type, category, any other scalar property whose enum / vector / string form is known) | `data/items.lua`: only the differences, entry by entry (`value`, `damage`, `mana_cost`, `requires`, `modifies`, `use_type`, `category`, `properties = { gCItem_PS = { SortValue = 400 } }`) | `risen.balance.apply` per entry; a bad entry is logged and costs only itself; on the first `tick` and again with `instances = true` on every `session.start` |
| **New item templates** | `data/new_items.lua`: `base` = the shipped item that is most like it (use type, hold type, category, property sets, scripts, then the number of differing properties), the differing numbers, `mesh`, `icon`, `material`, other properties | `risen.template.create` (savegame-safe: created again from the name at every start) |
| **Recipes** (new recipe items; shipped items that became recipes) | `data/recipes.lua`: `edit` (station, ingredients, skills, result) and `new` (a recipe item given to the player, because a shipped template cannot gain a `gCRecipe_PS` at run time) | `risen.balance` recipes / new_recipes, `risen.recipe.learn_once` |
| **Other template patches** (monster actors `eCAnimation_PS.ResourceFilePath`, projectile-like templates) | `data/patches.lua` | `risen.template.patch` |
| **String tables** (`compiled/strings/*.tab`) | `strings/all.csv` (English, the fallback for every language) and `strings/<Language>.csv`: new rows, rows of converted items (`FO_<name>`, `ITEMDESC_<name>`), spreadsheet artefacts removed (`'...'`, doubled quotes, backtick and acute for apostrophes, `-` and `...` placeholders) | the layer's `strings/*.csv` loader (`risen.loc`) |
| **Resource files** (`._xmsh`, `._ximg`, `._xmat`, `._xmac` and the other compiled `._x*` names) | flat `assets/` with the original file names; identical files skipped; replaced stock names reported | mounted at start (`mod.toml` `assets`); a loose file shadows the packed one with the same name |
| **Combat table** `compiled_combatspecies.bin` | `assets/compiled_combatspecies.bin`: **your own table** with every species whose styles, moves, fight AIs or properties differ from the mod's replaced by the mod's (the file is parsed and written back losslessly); both variants (oldest / newest volume) in `variants/` | loose file, read once at start (RE.md section 45) |
| **Weather files** (`*.g3ws`, same size as the game's) | `assets/<name>`, copy in `variants/` | loose file |
| **Dialogue** (`compiled_infos.bin`): `GiveXP` commands added to or changed on existing infos, `RunScript OnTradeInfo_RefillTradeItems` trader refills (the chapter number is read from the info's `Special_Chapter<N>` picture) | `data/dialog.lua` | `risen.dialog.get` + `add_command` / `add_script_command`; the first chapter's stock also at the start of a new game (RE.md section 44) |
| **World layers** (`*.lrent`, matched with the game's layer by file name and entity by id): items added to containers / objects / dead bodies, new items equipped by NPCs, changed `TeachSkills` limits | `data/world.lua` | `risen.entity.by_id` + `risen.item.give` / `equip`, `risen.npc.set_teach_limit`; once per savegame |

Every number, name and text comes from the archives and the enum names from the data table in `src/pack2mod/enums_data.inc` (generated by `tools/gen_pack2mod_enums.py` from `tools/extmod_enums.json`, which was learned from the engine's own reflection, docs/RE.md section 43); nothing is typed in per mod. The output is deterministic (sorted keys, no time stamps): the same inputs give byte-identical files.

Besides the data: `mod.toml`, `main.lua` and `config.lua` (switches `balance`, `new_items`, `dialogue`, `world`, `teachers`, `starter_kit`; the generic code lives in `tools/extmod_templates/` and is embedded in the tool), `features.json` (groups of loose files that `scripts\extmod-features.ps1` can switch off: one group per `--profile` asset group or `assets_new` / `assets_replaced`, `combat`, weather), `manifest.json` (counts, names, every unconverted entry with the reason, warnings), `report.md`, `NOTICE.txt`, `.gitignore`.

## What is not converted

Listed with the reason in `report.md` (and `manifest.json`: `unconverted`, `not_converted`, `warnings`, `notes`). `Needs` is `engine` (the layer has no API for it), `author` (the mod's own data is incomplete or wrong: a recipe that names a template that is in neither archive, a trader entry for a misspelled template: the tool maps a misspelling when exactly one template fits and warns), `tool` (not translated by the tool).

* **Template differences that are not data**: runtime state saved with a template (collision, rigid body, navigation, routine state, inventory snapshots, NPC state) is dropped; a property set the shipped template lacks cannot be added at run time (`engine`); new templates that are not items (NPCs, spells, effects) are listed, not converted; changed non-item templates are converted only for the monster actor swap and projectile-like numbers.
* **Dialogue**: only what the mod *adds*: XP rewards and trader refills. Other differences of existing infos (commands or conditions added or removed: in the Extended Mod mostly the classic-vs-Enhanced noise of its base) and **new infos** are counted and listed, not converted.
* **World**: teacher limits, container items and NPC equipment as above. Moved or new placed entities, changed routines, new layers are not translated. A mod template that takes over the GUID of a game template (a rename: 8 in the Extended Mod, e.g. `It_BS_RuneSword` -> `It_2H_Uriziel`, `It_Axe_Ogre` -> `It_Axe_Ogreaxe`) changes what the game's world placements mean: the tool resolves recipes to the mod's template and compares world layers by the game's names, so the rename is not applied to entities that are already placed (listed as a note in the report).
* **Library and project files**: `compiled_meshes.bin`-style resource tables and every `*.bin` / `*.xxx` file the tool has no reader for are listed per kind. Animation sets are resource files and are copied as assets; their binding to a monster is done by the template patch.
* **String rows that are not about converted entities**: counted per table and not applied unless `--strings all` (a classic-based mod's tables differ from Enhanced's in hundreds of rows that are base noise, not changes).
* **Behaviour that needs scripts or hooks**: anything a mod does with game scripts (the 32-bit mod SDKs), `.dll`s, `.ini` changes and other loose files in the mod folder are listed as "other files" and not converted.

## Profiles

A profile is JSON with knowledge about **one specific mod** that cannot be derived from its archives: which of two volumes is the changelog state, which string rows are real changes and which spreadsheet noise, which of the author's recipes name templates that do not exist and what they obviously meant, how the loose files group for switching. `tools\pack2mod\profiles\risen_extended.json` is the profile of the Risen Extended Mod (facts about the mod's own mistakes and which string rows matter; no content of the mod). Keys: `id`, `name`, `recipe_fixes.result` / `.ingredient`, `strings.only_rows` (table -> the only rows applied), `strings.changed_with_changelog_combat`, `strings.changed_all`, `asset_groups` (`prefix`, `exclude_prefix`), `combat_about`, `weather_group`. Without a profile the tool is fully generic and reports the same problems as `unconverted`.

## Tests

* **Unit tests** (`spass_tests`, `tests/pack2mod_tests.inc`, no game data): archive writer / reader round trip (compressed, stored, empty members, directories, damaged archives), string tables, a template builder and the scanner (properties, enums, arrays, GUID references), the lossless combat table round trip, text helpers, Lua / JSON emitters (layout identical to `tools/extmod_gen.py`), the JSON reader, and an **end-to-end conversion** of synthetic archives (changed item, new item with its base, identical file skipped, strings with artefacts, assets, layering of three volumes, refusal to write into foreign folders or the game, determinism, `--list`, `--dry-run`).
* **Golden test** (`scripts\pack2mod-golden.ps1 [-Combat changelog|shipped|off]`, needs the mod and the game): runs the C++ tool and the Python oracle `tools/extmod_gen.py` (which stays in the repository for exactly this) on `B:\TMP\Risen_Extended_Mod` against `B:\DevDrive\risen-test\data` and compares with `tools/pack2mod_golden.py`: 353 files identical (all `data/*.lua`, 5 string files, `features.json`, the 332 assets and the combat table / weather variants byte by byte, `main.lua`, `config.lua`), the manifest counts, statistics, unconverted entries and warnings. Two explained differences remain in `data/world.lua` (the Python generator reads a research file that was made with name-keyed lookups): the tool finds a **second chest** with the same name in `BD_Farm_Events.lrent` (entity id `28e2d7f0-...`: `It_Po_EXP_Special`, `It_BlackPearl`), and it does not equip **Karakos** with `It_Axe_Ogreaxe`: the mod's template is a rename of the game's `It_Axe_Ogre` (same GUIDs), Karakos' world reference did not change, only its meaning, and the research took the re-serialised equipment slot for a new item. Everything else (198 changed items, 131 new items, 45 + 5 recipe items, 30 patches, 69 XP infos, 105 trader entries, 9 teacher limits, 3 equipment items, 332 assets, 5 languages) is identical.
* **Live check**: the tool's output installed as `mods/risen_extended` passes the scenarios `extmod` (32 checks, two game processes) and `extplay` (13 checks) unchanged: `scripts\smoke-test.ps1 -Scenario extmod,extplay -Parallel 2`. (`check.lua` and `play.lua` of `tools/extmod_templates/` are test files of that mod and are copied in by hand; a generated mod does not contain them.)

## How it fits together

```
risen-pack2mod  ->  <mod>\mod.toml, main.lua, config.lua      the mod (generic code, embedded)
                    <mod>\data\*.lua                          the numbers (only differences)
                    <mod>\strings\*.csv                       the texts
                    <mod>\assets\*                            loose resources (flat, original names)
                    <mod>\variants\, features.json            switchable files
                    <mod>\manifest.json, report.md            what was done and what was not
```

The generated `main.lua` is the same generic code for every mod: it applies the data stages in the order *new templates, item changes, recipes, patches* on the first `tick` and on every `session.start`, the dialogue edits once per process, the world edits once per savegame, the teacher limits on every `world.ready`. A bad entry is logged (`<id>: ...`) and counted, never fatal; `config.lua` switches the groups.

Formats the tool reads and writes are in [RE.md](RE.md) sections 43 (archives, templates, string tables, library files) and 47 (what the converter learned: world layer entities, GUID collisions, chapter infos); the Python readers `tools/g3pak.py`, `g3tple.py`, `g3tab.py`, `g3cs.py` and the generator `tools/extmod_gen.py` remain as the test oracle.

## Limits

Measured on the Risen Extended Mod: 7 s for the whole conversion (the archives are 105 MB), 102 MB of assets. The template scanner is tolerant, not a full parser: a property of a type it does not know is carried as hex and reported (`enum value ... has no known name`) rather than guessed. Enum names exist only for the enums item templates use (`src/pack2mod/enums_data.inc`); a mod that changes other enums gets `unconverted` entries with `needs = tool` until the table is extended (`tools/extmod_enums.py` learns more from a game run of `mods/itemdump`). Archives of kinds other than `common` and `compiled` (sound and speech volumes, GUI packs) are not read.
