# Analysis of the Risen Extended Mod and a plan to re-implement it

Static analysis of the German "Risen Extended Mod" (a compilation of the author's Risen Balance v2 and Gothic Flair, plus skins, vegetation, combat and item changes) for the layer's
framework: what it changes, what it adds, and how each part can be rebuilt on top of `risen.balance` / `risen.template` / `risen.assets` / `risen.dialog` or what needs new engine work.
**Nothing here ran the game.** The mod was read as data only (never executed), with the offline readers `tools/g3pak.py`, `g3tple.py`, `g3tab.py` (formats: [RE.md](RE.md) section 43)
and `tools/extmod_diff.py` (the diff behind every number below). The long version with all tables is the author-side report `B:\DevDrive\risen-ghidra\extmod\REPORT.md` (outside the repo).

## 0. Read this first: baseline, layers, caveats

* **Baseline = Risen Enhanced x64** (the game data of the test copy). **No data of the old 32-bit game is on this machine** (searched `B:\` for other Risen installs: none). So "changed from the original" here means "changed
  from Enhanced". Where Enhanced itself differs from the classic game, the diff shows both effects mixed. Detectable cases are named: the mod's dialogue / world files lack Enhanced's bug fixes (section 4),
  the mod's string tables have 5 languages (Enhanced: 7), 17 templates differ only by re-saved runtime state (`noise_only` in `templates.json`).
* **The mod is a stack of numbered patch volumes** in `common/` and `compiled/`: `*.p00`, `*.p01`, `*.p02` (the container is the engine's `G3V0` format, the same as the game's `*.pak`; the engine's mount code calls `MountPackFileChain`,
  later volumes shadow earlier ones). In order: `templates.p00` (363 files) -> `templates.p01` (377, 14 more + 5 corrected: Uriziel, gold/crystals, keys, minerals); `strings.p00` -> `strings.p01` (items / focus texts for 3 more
  items); `library.p01` -> `library.p02` and `projects.p01` -> `projects.p02` (full replacements, p02 = p01 plus one dialogue tweak / one world file); `images.p00` (149) + `p01` (2); `meshes.p00`, `materials.p00`, `animations.p01`, `common/library.p01` (one file).
  The effective state is "last wins" (`extmod_diff.py` applies it). The volumes are mostly **flat** (bare file names); 11 monster templates in `templates.p01` keep directories.
* **Two versions of the combat table.** `compiled_combatspecies.bin` of `library.p01` is a *restructured* melee table (new 2H styles, Sword<->Axe skill swap, reach, no backwards hop); the one of `library.p02` (the newest, wins)
  is **Enhanced's table plus 16 edited values** and has none of that. `compiled_infos.bin` is the same in both (p02 adds 3 trader entries). Which of the two the author meant for Enhanced is not stated: **ask the user / the author**.
  This document reports both; the re-implementation plan covers the rich p01 variant as an option behind a switch.
* **What the archives do not contain:** no compiled scripts (`Script_Game.dll`), no `.sec` / `.wrl` world geometry, no sounds, no animations of the melee moves (`._xmot`). Changelog claims that would need those are marked below.

## 1. What is in the archives (14 volumes, 105 MB, 1122 files)

| Volume (kind) | Files | Content | Share of the changelog |
|---|---|---|---|
| `templates.p00/p01` (common) | 363 + 377 (376 distinct templates) | 245 templates the game has (228 with real changes, 17 only re-saved), **131 new**: 64 weapons, 42 smithing-chain / recipe stages, 11 books, armor / helmets / rings / amulets / shield / 3 crystals | items, recipes, mana, potions, monsters' skins |
| `strings.p00/p01` (compiled) | 5 + 5 `.tab` | `skills`, `items`, `focus` (names), `documents`, `hud2`; 5 languages | names of the 126 new items, skill texts, hud |
| `images.p00/p01` (compiled) | 149 + 2 | 151 textures, 87.5 MB (47 new, 104 replaced) | skins, heads, dungeons, vegetation, weapons |
| `meshes.p00`, `materials.p00` (common) | 88, 46 | 87 weapon / shield / helmet meshes (66 new, 21 replaced) + `compiled_meshes.bin` (6.3 MB resource header table), 46 materials (40 new) | new weapons, lizard skins |
| `animations.p01` (compiled) | 48 `._xmac` | actors: 15 lizard variants, 9 female body actors, Crawler / heavy guard armor, undead ashbeast, 19 bow / crossbow actors | skins, female mesh, ranged weapons |
| `library.p01/p02` (compiled) | 2 + 2 | `compiled_infos.bin` (dialogues), `compiled_combatspecies.bin` (melee styles) | pickpocket XP, traders, melee system |
| `projects.p01/p02` (common) | 17 + 17 `.lrent` | world entity layers: NPC placements and event / container layers | teacher limits, items placed in containers |
| `library.p01` (common) | 1 | `WeatherStates.g3ws` | only `WindPower` (0.55 / 0.65 / 0.7 -> 0.3) |

## 2. Findings by changelog item

Status: **C** confirmed in the data, **P** partly (data shows part of it), **N** no evidence in the archives. Class (section 3): a data / b assets / c Lua behaviour / d new engine work / e risky.

| Changelog item | Evidence (files, entities, numbers) | St | Class |
|---|---|---|---|
| Lizard skins | 35 new + 6 replaced lizard textures, 34 new lizard materials, 14 new actors + 2 replaced (`Ani_Lizard_Monster_Warrior_Guard`, `_Priest_High`, `_Scout_Hunter`, ...); 24 monster templates (Kapitel 2-4) now point `eCAnimation_PS.ResourceFilePath` at them (e.g. `LizardWarrior_Keeper` `Ani_Lizard_Monster_Warrior.xac` -> `..._Warrior_Guard.xac`); `LizardPriest_Undead_StoryUnikat` -> `_Priest_Zul_Utur`; `AshBeast_Undead_StoryUnikat` -> `..._Zel_Zeret` | C | a + b |
| Small vegetation update | 4 textures: `Nat_Veg_Herbs_02`, `ST_Composite_01` (SpeedTree atlas), `Nat_SwampStuff_01`, `Arch_Obj_MixSet_01` | C | b |
| Female mesh changed | 9 replaced actors `Ani_Hero_Armor_Woman_{Dress,Old,Orient,Peasant,Slave,Slave_Ghost}`, `_Landlady`, `_Titanlord(_Ghost)` | C | b (skeleton compatibility untested) |
| Armor / monster skins, head textures | 33 replaced armor body textures, 19 other monster textures, 6 helmet, 7 head textures (`Ani_Hero_Head01_01`, `_Alchemist_01`, `_Rocker_01`), 9 new for `It_Armor_Guard_Heavy` | C | b |
| Darker caves / dungeons (torches, light spells needed) | **18 lightmap textures** `Levelmesh_Dungeons_Caves/Vulkan/Small`, `_Dungeon_Lizard_0..3`, `_OldMine_0..1`, `_Blue_Dungeon_*`, `_Gyrger`, `_Druidjail` replaced (re-compressed DXT1; crude endpoint luminance 2.5-4x lower); `It_Cast_Light` point-light range 1000 -> 1500 | C | b + a |
| Pickpocket gives XP by required level | the game has 66 pickpocket infos (`gCInfoCommandPickPocket`, `Difficulty` 1 / 2 / 3); the Enhanced file gives XP in exactly one (Cathy, 10). The mod adds `GiveXP` to 65: difficulty 1 -> **100** (27, Cathy's 10 -> 150), 2 -> **200** (27), 3 -> **400** (10) | C | c |
| Doyle XP when found later | besides his pickpocket info (`QATEST_00046`, +100) a second Doyle info `FILLERX2_01360` gets +50 | C | c |
| Wisdom useful for non-mages | 3 books `It_INTBook(2,3)` (Illumar, +12 / +15 / +12 INT when transcribed at the writing desk; recipes need `Circle` 1-3), 5 `It_EXPBook_01..05` (transcription recipes need INT 40..200, give +200..+1000 XP), `It_Po_EXP_Special` (+500 XP). No other use of INT was found | P | a |
| Teacher limits: ogres to 200 STR, Sam 200 DEX, others 150 | `gCDialog_PS.TeachSkills[]` of **placed NPC instances** in `Harbour_Town_NPC_01`, `Don_Camp_NPC_01`, `Gyrger_Island_NPC_01`: STR 100 -> 150 (x3), -> 200 (x2, the axe teachers), DEX 100 -> 150 (x3), -> 200 (x1, a Bow 10 teacher). Not in the templates | C | d (small) |
| New smithing recipes | 11 weapons with the whole chain (recipe -> hot -> shaped -> dull -> sharp): Ruby Blade, Slaughtersword, Dark Wind, Cleaver, Godhammer, Obsidian axe / staff / bow / crossbow, Amber staff, Simple crossbow; 42 templates with `gCRecipe_PS` (forge 6 / anvil 7 / cooling 8 / grindstone 9) | C | a |
| Pearl necklaces craftable; gold smithing | goldsmith (4): 8 `It_Pearl` -> `It_Necklace_Pearl`; 10 pearls + 1 black pearl -> `It_Necklace_Pearls` (new, +20 HP/MP, +3 STR/DEX); 30 `It_Gold` -> `It_GoldOre`; 5 ore -> `It_Necklace_Gold`; ore + obsidian -> `It_Am_Obsidian` (new); pearl gold value 50 -> 35, necklace 300 -> 800 | C | a |
| Big gems earlier | traders get gems: Costa `It_Ruby`, Jack `It_Sapphire`, Oscar `It_Emerald` (infos, no chapter field set: gating is elsewhere) | P | c |
| **Melee system: 1H / 2H / staff**, axe skill ~ 2H, sword skill ~ 1H | (1) `skills.tab`: `Sword fighting` -> `Onehanded fighting`, `Axe fighting` -> `Twohanded fighting`, new skill-level texts; (2) templates: weapons re-typed (section 2.1); (3) `library.p01` combat table: 15 styles removed (`None_1H_S3..S8`, `Shield_1H_S2..S10`), 7 added (`None_Axe_2H_S1..S5`, `None_2H_S6`, `Shield_Axe_S1`), `WeaponConfigs` skill 16 (Sword) <-> 17 (Axe) swapped for the 2H / Axe styles (36 styles changed); (4) `library.p02`: only `None_Axe_S2..S4` use the `S5` attack / counter sets and lose `EnablePowerRaise` | C (p01) / P (p02) | d |
| Two-hand / 2H swords and big axes not one-handed | `It_Axe_TitanHammer`, `_CrowsBeak`, `_Beard` `UseType` Axe -> BS; lumberjack axe, Cleaver, Smithhammer = 1H use type with axe hold; `Ogreslayer`, `Slaughtersword`, `Slaughteraxe`, boarding sword, katana BS | C | a |
| New weapon sorting | `SortValue` of 58 templates (1H -> 400, 2H 118..161, axes 500 ...) | C | a |
| Backwards hop removed, side dodge kept | `library.p01`: species 0 (`Human`) `JumpBack` `None_None_JumpBack_0` -> empty; `StrafeLeft/Right` untouched. **Absent in `library.p02`** | C (p01 only) | d |
| 2H reach higher from a skill | `library.p01`: `HitRange` 180 -> 200 (`None_2H_S1..S5`), 220 (`S6..S8`), 250 (`None_1H_S9/S10` now 2H sets); staff 200 -> 210 / 230; texts "+10% attack range" (Axe 6/9, Staff 7). Not in p02 | C (p01 only) | d |
| Ogre club / pickaxe equippable | new `It_Axe_Ogreaxe` (UseType Axe, damage 140, STR200/DEX120), new `It_Axe_Pickaxe` (Axe, 15, STR10), new `It_Axe_Heavy_Club` | C | a |
| All weapon values / requirements | 69 damage, 70 gold, 86 requirement changes among existing items (every weapon needs DEX besides STR now: Katana 60 -> 80 damage, DEX50 -> DEX140 + STR60; Berserk 2H 80 -> 102, STR80 -> STR143 + DEX64, 1500 -> 2450 gold); arrows and bolts `DamageAmount` 20 -> 0 | C | a |
| Many new weapons incl. DEX and mage weapons | 64 new: 16 1H, 6 2H, 5 BS, 7 axes, 12 staves (mage staves `Staff_Mage / _Balance / _FlameScepter`: damage type Magic, need `Circle` 2-4 and 150-300 max mana), 7 bows, 11 crossbows | C | a + b |
| New / reworked helmets and shields; shields and some armors need STR | new `It_Bandit_Helmet`, `It_Mage_Hood`, `It_Shield_Knight` (STR90); shields' damage 1-10 -> 0 and STR requirements Buckler 20, Guard 45, Rune 60, Knight 90, Round 120, Titan 160; Guard / Leather helmet meshes replaced | C | a + b |
| A few armors for guildless players | `It_Armor_Crawler` (STR50, 3600 g, protection 33-53), `It_Armor_Guard_Heavy` (STR120, 6200 g); nothing in the data says "guildless" (scripts check guilds) | P | a |
| Captain armor only in chapter 4 | `It_Armor_Guard_Heavy` is stocked by Leto via a trade info; the chapter gate (`StartChapter` of that info is -1) is not in the file | P | d |
| Mage robes: armor lowered, magic protection raised | Novice 10/10/10 + 5/5/5 -> 8/8/8 + 8/8/8 (+3 max mana), Low 20 + 10 -> 16 + 24 (+5), High 60 + 30 -> 40 + 48 (+10); Antimagic amulet 15 -> 24 | C | a |
| Berserker / rune sword purchasable; other items at the old places, insert codes differ | `It_Axe_Trollfist` ("Berserker sword", 130 Edge, STR184 / DEX84) and `It_2H_Berserk` renamed "Mighty axe"; Oscar's trade info lists `It_BS_RuneSword_01`, but **no template of that name is in the archives** (a gap of the mod, or it relies on a missing file). The "insert codes" are the trader entries (`OnTradeInfo_RefillTradeItems`), 106 of them (below) | P | c |
| Mana costs of runes / scrolls (partly strongly up), some runes need minimum mana | 38 `ManaCost` changes: runes Beast 60 -> 200, Berserker 40 -> 100, Inferno 80 -> 100, Levitate 20 -> 60, Nautilus 20 -> 80, Light 10 -> 20, Protection 60 -> 30, Speed 40 -> 20; scrolls 0..40 -> 20..180; rune `RequiredSkills` gain `MaxMP` 160..280 (and Circle changes) | C | a |
| Crystal mana cost / damage | Fire 8 -> 16, Frost 10 -> 24, Magic 5 -> 9; `It_Projectile_Frost_S0..S2` damage 50 -> 5; 3 new "blessed" crystals (12 / 18 / 7 mana, need Circle1) sold by Illumar / Cyrus | C | a |
| Gold as an item that can be put into chests | `It_Gold`: `Category` None (0) -> Misc (6), coin mesh `Item_GoldCoin._xmsh` / material switch 1, goldsmith recipe (above) | C | a (+ UI check) |
| Unfinished lizard busts replaced | 8 `It_Bust_*` templates now use `Item_Statue_LizardKing_01._xmsh` and new icons | C | a |
| Bow amulet bonus lower; Acrobat / Sneak rings need DEX; some amulets need a seal | `It_Am_Emerald` Bow +2 -> +1, DEX +10 -> +8; rings Acrobat DEX90, Sneak DEX50, Barry / Monastery / Nelson DEX 50 / 80 / 100; `It_Am_MP` Circle2, `It_Am_Prot` Circle3 | C | a |
| Permanent potions +3 / HP +15; meat in bread coat +4 HP | `It_Po_STR/DEX/MP_Max` 5 -> 3, `It_Po_HP_Max` 10 -> 15; `It_FriedStew_HP` 2 -> 4; Coffee, Stew / Soup gold values up (50 -> 155..275) | C | a |
| Recipes baked plaice / hunter's pan buyable from chapter 2 | `It_Recipe_FriedStew_Dex/_MP` gold value 10 -> 300; sold by Jasmin (+ `It_Am_DEX`) / Illumar; the chapter gate not found | P | a + d |
| Uriziel, Trollfaust, crawler armor, guard armors in the game | new `It_2H_Uriziel` (Ice, 90, STR130 / DEX58, 2150 g), `It_Axe_Trollfist`, `It_Armor_Crawler`, `It_Armor_Guard_Heavy`; changed `It_Armor_Guard`. Where Uriziel lies is not in the data (placed by a script or a world file not shipped) | P | a |
| Gothic easter eggs (2) | not identified (candidates: `It_Rods_Boots` "Who the hell is Rod..", `It_Ri_Thief` "Dexter", `TheOneRing` Tolkien quote; the `QATEST_*` dialogues of the NPCs Tinky / Dipsy / Lala are leftover test chats) | N | - |
| Trader stock ("insert codes") | 4 trade infos get 25 + 19 + 27 + 35 `RunScript OnTradeInfo_RefillTradeItems(trader, item, count)` calls: Alvaro, Oscar, Karlsen (weapons, blanks, recipes), Cyrus (mage staves / hood / crystals), Doug (knives, bows), Illumar (runes, books, mage crystals), Jasmin, Leto, Costa, Jack, ... | C | c |
| XP rewards of quests | besides the 65 pickpocket infos: Caspar +25, Doyle +50, Cole +350, Esteban XP 150 -> 300 and the amount of his `Give` 100 -> 400 (`PANKRATZX2_00964`) | C | c |
| Items placed in the world | `.lrent`: EXP book recipes 1..5 (Monastery, Pirate Coast, Graveyard, Druid, Vulcan), `It_Rods_Boots` (swamp), `It_Recipe_Reg`, `It_Po_XP` + gold bag + pearl (old mine), `It_BS_Old_Bastard` (caves), `It_Po_EXP_Special` + black pearl + `It_Ri_Thief` (farm), `It_Crossbow_Simple_Recipe` | C | c |

### 2.1 Weapon classes after the mod (existing items, `gCItem_PS.UseType` / `HoldType`)
`UseType` decides the equip slot and the combat style. The mod keeps swords on 1H / 2H / BS, puts the big axes on BS (`TitanHammer`, `CrowsBeak`, `Beard`: Axe 18 -> BS 3) and the light axes on 1H with axe hold (`Lumberjack`, new
`Cleaver`, `Smithhammer`); `RequiredSkills` of heavy weapons now always list STR and DEX, mage staves list `MaxMP` (and `Circle`); a few items change their granted skill (`TitanHammer` Axe+6 -> Sword+5, obsidian blades
`Sword+1` / `Axe+1`). 2H sword prices: Obsidian 1000 -> 2750, TitanWing 2000 -> 3350, SoulDrinker 1000 -> 2000.

### 2.2 What the dialogue file adds (`compiled_infos.bin`, 4514 -> 4538 infos, 104 changed)
69 `GiveXP` additions (65 of them pickpocket infos), 106 trader refills, 24 new test infos (`QATEST_00001..25`, Tinky / Dipsy / Lala, harmless), and 32 further changes that look like **Enhanced fixes missing from the mod's classic-based file** (section 4).

## 3. Classification summary

| Class | What | Items |
|---|---|---|
| **a** data, existing API (`risen.balance.install` `items` / `new_items` / `recipes` / `new_recipes` / `spells`, `risen.template.patch` for `eCAnimation_PS.ResourceFilePath` / `Category` / `HoldType` / light range, `risen.loc` CSV) | 228 changed + 131 new templates, recipes, mana, potions, gold category, monster actor swap, 269 new text ids (126 item descriptions, 129 names, 14 documents) | ~80 % of the changelog; runtime-only and savegame-safe |
| **b** loose assets (`mod.toml assets`, `risen.assets`; the layer mounts them before the engine loads anything, RE.md section 26) | 46 materials, 87 meshes, 151 textures, 48 actors = **332 files, 108 MB** (lizard, armor, heads, female actors, weapons, bows, 18 dungeon lightmaps, vegetation) | verified for new names and for shadowing; **a stock name replaced at startup is not yet run** (probe 1 of M19) |
| **c** Lua behaviour on existing API | XP on the 69 infos (`risen.dialog.get(name)` + `Info:add_command("GiveXP")`, or the `PickPocket` command's finish), trader stock (`risen.item.give` into the trader's inventory when its trade info runs / on a trade event, or `risen.script.call("OnTradeInfo_RefillTradeItems", ...)` from an added `RunScript` command), items placed in containers (`world.ready` + `risen.save` markers) | the stock-info edit paths are API-complete on paper, **not yet run on stock infos** |
| **d** new engine work | melee styles (`gCCombatStyleMelee` list of the combat species: `HitRange`, `Attack*`, `EnablePowerRaise`, `WeaponConfigs[]`, and `gCCombatSpecies.JumpBack`), world-NPC teacher arrays (`gCDialog_PS.TeachSkills` of placed NPCs), chapter gating of trades, weather states (`WindPower`) | `risen.npc.combat_species` reaches the species object today (RE.md section 24); the style list and the arrays are the research |
| **e** impossible / not worth it | `compiled_meshes.bin` (6.3 MB resource header table; the engine does not need it for new names), anything compiled into `Script_Game.dll` (not in the archives), the missing `It_BS_RuneSword_01`, the mod's GUIDs (our templates derive IDs from names: **savegames of the original mod do not load**) | |

### Option: ship the mod's archives as they are (next to our layer)
Possible in principle on x64 if the engine's pack-chain mount picks `*.p00` files up (inferred from `MountPackFileChain`, **not run**; the test copy's data directories are junctions into the real install, so
the probe needs a separate real copy). **Risks, measured with the diff:** every volume replaces *whole files*: (1) `strings`: 5 languages only (Polish / Russian lost), `hud2.tab` loses 130 rows, the item texts of French / Italian / Spanish all differ; (2) `compiled_infos.bin` and the 17 world files are classic versions: they **probably drop Enhanced's fixes**
(`PALLAS_BUGFIX` events, `Illumar_SellsScrolls` -> `Illumar_SellScrolls`, 8 `CloseQuest` commands of Pallas / Sergio / Romanov, `INQ` conditions of Magnus / Leto / the town guards, `StartChapter` of Carlos / Walter, `Dork` dismissal script) -- quest-flow regressions;
(3) the 245 template files replace Enhanced's, so Enhanced-only changes inside them vanish and runtime patches of other mods lose their baseline assumptions; (4) a removed or updated mod strands savegames (items of templates the next start does not have; our layer solved exactly this for runtime templates, M15);
(5) it cannot be combined with another mod that also ships volumes (numbers collide) or with our hot reload / mod manager. **Recommendation: do not ship archives; convert (M18).** The archive route stays a cheap experiment (E1 in M19) to learn what the engine does with patch chains.

## 4. Evidence of "classic vs Enhanced" noise (what the diff cannot attribute)
Differences in the mod's `compiled_infos.bin` that are most likely Enhanced additions the classic-based file never had (the 32 "other changes" of `infos.json`; the data cannot prove it without the classic file): `PALLAS_BUGFIX*` game events removed, `Cormac_Sends2Illumar` / `Scordo_PackageForDon` / `Romanov_BringHisGoldchest` `CloseQuest` commands removed,
`Illumar_SellsScrolls` typo restored, `OnQuestSuccess_FindMurder / _AddQuillForTrade / _DismissDork` scripts removed, `Player_HasTwoDiscs` / `Don_WantsBackHisHarbour` quest conditions removed. The converter must therefore **not** carry these (they are not the mod's intent); it only extracts what the mod *adds* (XP, refills), and warns about every removal it sees.

## 5. Re-implementation plan (proposed NEXT_STEPS milestones M18+)

Dependencies: M18 -> M19 -> (M20, M21, M22 independent); effort in focused working days (one engineer-agent, including live verification in the isolated windowed copy and docs).

| M | Title | Content | Effort |
|---|---|---|---|
| **M18** | **`risen-pack2mod`: a generic converter for mods that ship `.pNN` archives** | (a) the pack reader (`G3V0`, zlib) and the property-stream scanner, ported to C++ from `tools/g3pak.py` / `g3tple.py` / `g3tab.py` (the Python files stay as the oracle for a cross-check test); (b) layering + baseline diff, (c) emitters, (d) report; design below | 10-12 (pack/template/tab readers 3, template + recipe + string emitters 3, asset extractor 1, infos/world/combat extractors to JSON 2, report + unit tests + golden test against the Python oracle 2, docs 1) |
| **M19** | **`mods/risen_extended` (generated, private)**: the data half | run the converter on the user's copy; scenario `extmod` (`risen.test`): spot-check ~40 entities per class against the diff (weapons, armor, runes, recipes incl. a craft at the anvil, potions, gold category, strings), monster actors load (`Entity:dependencies()`), traders sell; probes: **P1** a loose texture / actor replacing a stock name at startup, **P2** `Category = Misc` makes gold visible / storable (UI), **E1** the archive route on a real copy | 5-6 |
| **M20** | **Dialogue and world behaviour** | XP additions and trader refills on stock infos (`risen.dialog.get` + `add_command`; extend `add_command` by `RunScript` / `PickPocket` readers if missing), items placed in containers, the teacher arrays (`risen.npc.teach(name)` / `set_teach` for `gCDialog_PS.TeachSkills` of templates and placed instances, like `item.skill_array`), chapter-gated stock | 4-5 |
| **M21** | **Melee style table at runtime** (research, then data) | reach the style list of `gCCombatSpecies`/`gCCombatAdmin` (RE.md section 24 gets the species object): `risen.combat.style(name):get/set` incl. `WeaponConfigs[]`, `risen.npc.set_combat_species_property` for `JumpBack`; try live: a player 1H fight with `HitRange` changed, `JumpBack` cleared, styles swapped. Fallback if the objects are not reflected: **overlay of a patched `compiled_combatspecies.bin`** that the converter writes from the user's baseline (needs a property-stream *writer*, ~1 day) and `data_overlay` for library files (probe) | 5-7 (risk: styles are cached per NPC; the p01 table is a large change) |
| **M22** | **Lighting and weather** | the 18 lightmaps are assets (M19); wind (`eCWeatherStates`) through the same reflection route as M21 or the overlay of `WeatherStates.g3ws` | 1-2 |

**The converter (M18) in detail.** `risen-pack2mod <mod dir | .pNN files...> --baseline <Risen>\data --out mods/<name> [--name N] [--only templates,assets,strings,world,library] [--dry-run]`.
1. *Discover and layer*: every `<kind>/<base>.pNN` (any folder depth), grouped by base name and applied in number order, later wins; collisions between layers are logged; files are keyed by lower-case bare name (the engine's key); unknown kinds are reported, never guessed.
2. *Baseline*: the user's own `.pak` (and any official `.pNN` of the baseline) -- read only; **identical files are dropped**, only differences are written.
3. *Templates* -> `data/items.lua`, `data/new_items.lua`, `data/recipes.lua`, `data/templates.lua`, composed by a generated `main.lua` through `risen.balance.install`: property-level diff of each existing template (the same `Set.Name` keys as `risen.template.patch`; `value` / `damage` / `requires` / `modifies` / `mana_cost` / `use_type` for items), new templates as `new_items` / `new_npcs` with the closest baseline template as `base` and the property diff as `properties` / `mesh` / `icon`, GUID references resolved to names (baseline + mod), recipes as `recipes` / `new_recipes`, noise (collision, navigation, routine state, inventory snapshots) dropped. Only differences are carried, so the result **stacks with other balance mods** and is applied the usual way (first `tick` + every `session.start`, savegame-safe IDs derived from names).
4. *Assets* -> `assets/` (flat, original file name; the engine's cache is keyed by bare name and a later mount shadows an earlier one, RE.md section 26): materials, meshes, textures, actors, motions, collision files; byte-identical-to-baseline files skipped; name collisions inside the mod are last-wins with a warning; `compiled_*.bin` resource tables are not extracted.
5. *Strings* -> `strings/<Language>.csv` (+ `all.csv` fallback = English for the languages the mod lacks): only new or changed rows after normalising the spreadsheet artefacts; `id;text` with quoting as `risen.loc.load_file` expects.
6. *Library / world files* (`compiled_infos.bin`, `compiled_combatspecies.bin`, `*.lrent`, `.g3ws`): parsed and diffed to `data/*.json` (XP, refills, teacher limits, placed items, style table) that M20-M22 modules read; the converter writes `main.lua` calls only for kinds whose API exists (a stage table in the converter marks each kind with its milestone).
7. *Report* `CONVERSION_REPORT.md` (+ `report.json`): counts per class, every file or change **not** converted with the reason and the milestone that would handle it, warnings (name mismatches such as `It_Bow_Eaglehunter` vs `It_Bow_WEaglehunter`, missing templates such as `It_BS_RuneSword_01`, trader entries for unknown templates, reversals of baseline fixes), and the mod-manager hints (`restart_required`).

*Asset rules the converter must follow (RE.md section 26).* The engine finds a resource by the **lower-cased bare file name of its compiled form** (`it_wpn_axe_waraxe._xmsh`, `levelmesh_dungeons_caves_0_1._ximg`, `ani_lizard_monster_priest._xmac`) in one flat hash fed by every mounted pack volume and directory: (1) the converter keeps the compiled names and writes **one flat directory** (a sub-directory per kind would probably work, recursion of the cache is unverified), never a path or a raw `.xmsh` name; (2) a loose file with a shipped name **shadows** the shipped one, but only for resources the engine has not loaded yet -- the `assets` directory is mounted in the `ProcessMountList` hook before anything loads, so replacing stock skins / lightmaps / actors is the intended case (still to be run once, probe P1 of M19); a `reload` or a later runtime mount does not replace data already in memory; (3) two mods that ship the same name: the later mount wins (the mount order follows the mod load order), so the report lists every name that another converted mod also ships and the converter drops files that are byte-identical to the baseline (the common case for re-saved files); (4) inside one mod the later `.pNN` wins, a duplicate name from two directories of one volume is reported; (5) `compiled_meshes.bin`-style header tables are not extracted (not needed for names the cache does not know); (6) the NPC / monster actor is selected by the **template** (`eCAnimation_PS.ResourceFilePath`, a `risen.template.patch` / `mesh` override), the texture by the actor's material chain -- so lizard skins need the template patch **and** the assets; (7) icons must exist in the stock image lists (the mod reuses stock icon names, no new atlas cells were found).
*Report format.* `CONVERSION_REPORT.md` lists per class `converted / skipped (identical) / not converted`, each not-converted line as `file or entity | reason | needs`, with `needs` one of `M19` (data, nothing missing), `M20` (dialogue / world behaviour), `M21` (combat table), `M22` (lighting / weather), `engine` (no plan), `author` (data missing in the mod, e.g. `It_BS_RuneSword_01`).

*Delivery options.* (a) **Standalone C++ tool** built from this repo (new target in `CMakeLists.txt`, `zlib` from vcpkg, reuses `tomlplusplus` and the logging; ships in the release zip next to the installer): no runtime dependency, testable in `spass_tests`, deterministic, same code for the formats the layer will need later (`risen.vfs` readers). (b) **Inside the layer through `risen.vfs`**: needs a running game (30 s start, the user's input), the engine's VFS only reads what is mounted (a mod's archives are not), writes only into `generated/`; useful solely as a cross-check, not as the converter. (c) **Python script**: fastest (it exists), but users without Python cannot run it, a frozen exe gets antivirus false positives. **Recommendation: (a)**, built in two steps (M18 starts by porting the Python readers and keeps `tools/*.py` as test oracles and as the format reference).

## 6. Licensing -- flag for the user
The mod's art (meshes, textures, materials, actors), texts, item and recipe design and the numbers are the author's work. **The repository should ship only the converter and its documentation, never the mod's content.** The user runs the converter on their own copy of the mod and their own game; the output (`mods/<name>/assets/**`,
`data/*.lua`, `strings/*.csv`, the report) is a derived work and **must not be redistributed or committed** (the converter writes a `NOTICE.txt` and a `.gitignore` into the output; `mods/_generated/` is ignored in the repo). This document quotes facts (counts, file names, a few values) only. Redistribution of the converted mod, or of a "balance-only" extract, needs
the author's permission -- **the user should ask the author** (the changelog is German, the mod's origin is the Gothic / Risen community on World of Gothic / Steam). The mod's own readme / license was not part of the folder.

## 7. Reproduce

```
python -I tools/extmod_diff.py --mod <path to the mod> --baseline <Risen test copy or install>\data --out <dir> --lua
python -I tools/g3pak.py list <archive> --long        # also: cat, extract
python -I tools/g3tple.py dump <archive> <path in archive>  |  python -I tools/g3tab.py dump <archive> items.tab
```
`python -I tools/test_g3.py` is a data-free self-test of the three readers (synthetic archive, table and template). Run Python with `-I` on untrusted data and keep the scripts outside the mod folder (as done here). The diff took 20 s on the full mod.

## 8. Phase A result: the generated mod and the milestone snapshots

`tools/extmod_gen.py` (Python, not the C++ `risen-pack2mod` of M18: the user wanted a playable state within hours; the generator is the oracle that a later C++ converter must reproduce) turns the archives into the private mod `mods/risen_extended/` (git-ignored, see [DEVELOPMENT.md](DEVELOPMENT.md) "Risen Extended: generated mod and milestone snapshots"). Everything in it is a diff against the user's Enhanced data; nothing is typed in by hand.

| Part | Generated | Mechanism | Verified live (scenario `extmod`, 20 checks, two game processes) |
|---|---|---|---|
| changed shipped items | 198 (value, damage, mana cost, requirements, effects, use type, category, sort value, icon, mesh, material, gold coin interaction) | `risen.balance` `items` entry by entry (a bad entry costs only itself) | every number read back from the engine; second pass (`session.start`, live entities) keeps them |
| projectile-like templates | 4 (`It_Projectile_Frost_S0..2`, ...) | `risen.template.patch` (they have no `gCItem_PS`) | numbers read back |
| new items | 131 (64 weapons, armor, helmets, rings, amulets, books, crystals, smithing stages) | `risen.template.create` from the closest shipped item (`base` chosen by use type, hold type, category, property sets, scripts), then the remaining properties one by one | all exist with their value / damage, are labelled (`FO_` / `ITEMDESC_` from the mod's tables), six weapons spawn and load their loose mesh (resource state 3), equip after the requirements are met |
| recipe items | 43 edited (11 smithing chains: plan -> hot -> shaped -> dull -> weapon, scribe plans for the XP / INT books) + 5 new (pearls, gold, ore, obsidian: a shipped item cannot gain a `gCRecipe_PS` at run time, so the player learns a recipe item at the start) | `recipes` / `new_recipes` sections | a goldsmith recipe (8 pearls -> necklace) and a complete smithing chain (forge -> anvil -> cooling) craft through the engine's own checks |
| monster actors | 26 | `risen.template.patch` `eCAnimation_PS.ResourceFilePath` (first tick + `session.start` with live entities) | applied, `LizardWarrior_Keeper` spawns with the new actor, its resource is known |
| texts | 332 English (fallback for every language) + German / French / Italian / Spanish where the mod has real text | `strings/*.csv` | labels resolve |
| assets | 332 files (187 new names, 145 that replace shipped resources), 108 MB | `assets/`, mounted at startup | **P1:** the engine's name cache resolves all 332 names to the mod directory, including the 145 stock names (a loose file shadows the packed one); what they look like on screen is not verifiable (black screenshots) |
| save + load | | the items are created from their names | a fresh process quick-loads: the new items are still in the inventory, the numbers are applied again |

Deferred (in `manifest.json`): dialogue and world data (M20), combat table and skill names (M21), wind (M22). Not convertible: `It_2H_Dark_Wind_Hot` and `It_Staff_Obsidian_Dull` reference templates that are in neither archive (the mod's own error: those two stages do not chain), the weapon trail velocities of `It_Axe_Slaughteraxe` (the base has no such property).
Engine enum names are learned from the game itself (`tools/extmod_enums.py` joins the `mods/itemdump` log with the numbers in the archives -> `tools/extmod_enums.json`; the `gEItemHoldType` values, the `gERecipeCategory` names and the skill numbers 26 = Mining, 27 = Lockpick come from there).

**Snapshots.** Playable states are kept in `B:\DevDrive\extmod-milestones\<NN>-<name>\` (README with the changelog checklist, `play.ps1`), `LATEST.md` says which one is the newest; the repo commit is tagged `extmod-<NN>`. Start: `& 'B:\DevDrive\extmod-milestones\01-data\play.ps1'`.

### Phases B and C (same run): dialogue, world, teachers, combat table

| Part | Mechanism | Verified live (scenario `extmod`, 25 + 4 checks) |
|---|---|---|
| 67 XP rewards + 2 changed amounts on shipped dialogue infos (pickpocket XP, Doyle, Caspar, Cole, Esteban ...) | `risen.dialog.get` + `add_command("GiveXP")` / `set_command_property` on the first tick | read back (last command of an info); the pickpocket flow itself was not played |
| 105 trader refills on the four chapter infos | `add_script_command` + `RunScript OnTradeInfo_RefillTradeItems` (`Self`, `Other`, `OtherType` template entity, `Param`) | read back; the 25 chapter-1 entries are also run at the start of a new game (script call with a spawned template entity as `other`): Costa holds `It_Bow_Cole_01` as a trade stack. Chapters 2-4 depend on the engine running those infos (not played that far) |
| 9 teacher limits (STR / DEX to 150-200) | new natives `risen.npc.teach_skills` / `set_teach_limit` on the placed NPC (`gCDialog_PS.TeachSkills`, RE.md section 44) | all nine read back; a real teaching dialogue was not played |
| 11 chests + 4 NPC equipment slots | `risen.entity.by_id` + `give` / `equip` on `world.ready` (once per savegame) | a chest holds its new item |
| melee system | `assets/compiled_combatspecies.bin` (RE.md section 45), `skills.tab` names | the loose table is read (`JumpBack` empty), a fight runs |

Three trader entries of the mod name templates that do not exist and are mapped when exactly one template fits (`It_Crystal_Frost_Mage` -> `...Magei`, `It_BS_RuneSword_01` -> `It_BS_RuneSword`, `It_Bow_Eaglehunter` -> `It_Bow_WEaglehunter`); each is a warning in `manifest.json`.

### Phase D: looked at, played, gaps closed (snapshot `03-play`)

| Question | Result |
|---|---|
| Do replaced stock textures / meshes show? | yes: with and without the mod in the same scene, armor textures (Don Fighter, Druid), the leather helmet mesh, shields and foliage differ visibly; lizards and women render with their replaced actors; no missing, white or pink model among the 76 equippable new items |
| Darker caves | yes, in four dungeons the loose lightmaps give 0.56-0.95 of the unmodded brightness (the blue ambient glow of the caves is nearly gone) |
| Pickpocket XP | the 65 "pickpocket" infos are the **lessons** of the trainers (a topic "I have a question about picking pockets", condition type PickPocket); the engine pays an added `GiveXP` through a conversation (+100); the shipped lessons are offered only in pickpocket mode and their voiced lines never end in the test setup, so they were not played |
| Teaching | played: Sam teaches DEX +5 at 100 (the game stops at 100) and refuses at the old and near the new limit |
| Chapter refills | the chapter infos are the gates (cooking recipes 2, Crawler armor and gems 3, Trollfist and Heavy Guard armor 4); an added `RunScript` refill stocks a trader through a conversation; the shipped chapter infos stall on their first voiced line here |
| Melee styles | `risen.npc.combat_style`: p01 confirmed (Sword -> one-hand styles, Axe -> two-hand styles, staff 2.1, no style for a two-hander without Axe skill); a fight lands blows; the blow steps towards its target, so the reach was not measured |
| Gold | Misc category: shows in the inventory with its coin icon, picked up, a chest accepts it |
| Gaps | hud2: 4 real rows applied (the other 167 differences are classic-vs-Enhanced noise); wind: the loose `WeatherStates.g3ws` is read by the engine; two broken smithing chains repaired (Dark Wind hot -> dull, obsidian staff dull consumes itself and makes the Slicer staff); Gothic easter eggs found (Dexter's ring, Rod's boots); Uriziel placed by nothing in the archives; guildless armors = the Crawler / Heavy Guard armors sold in chapters 3 / 4 |
| Switches | `config.lua` run-time groups, `scripts/extmod-features.ps1` for the file groups (combat table variants p01 / p02 / off, wind, caves, vegetation, skins) |

### Phase E: the C++ converter (M18, snapshot `04-cpp`)

`risen-pack2mod` ([PACK2MOD.md](PACK2MOD.md)) now does what `tools/extmod_gen.py` did, for any mod, and also derives the world data (teacher limits, container items, NPC equipment) and the chapter numbers from the archives instead of the research file. Its output is identical to the Python generator's except two explained differences in `data/world.lua` (a second chest with the same name that the name-keyed research overlooked; Karakos' axe: the mod's `It_Axe_Ogreaxe` is a rename of the game's `It_Axe_Ogre` by GUID, which the tool does not apply to placed entities). The scenarios `extmod` (32 checks) and `extplay` (13) pass on the C++ output unchanged.
