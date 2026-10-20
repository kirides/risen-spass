# risen-modkit user guide

`risen-modkit` builds mods for the Risen modding layer without writing Lua: you create and change items (later recipes, NPCs, dialogues, quests and
world placements) in a desktop application and export an ordinary mod folder that the layer loads. The plan and the state of the stages are in
[REQUIREMENTS.md](REQUIREMENTS.md); this guide describes what works now.

Contents: [Build and start](#build-and-start) · [First start](#first-start) · [The window](#the-window) · [Items](#items) · [Recipes](#recipes) · [NPCs and monsters](#npcs-and-monsters) · [Dialogues](#dialogues) · [Quests](#quests) · [World](#world) · [Export](#export) · [Import](#import) ·
[The exported mod](#the-exported-mod) · [Command line](#command-line) · [Limits](#limits)

## Build and start

The editor is part of the repository's CMake build (vcpkg manifest feature `modkit`, enabled by every preset):

```powershell
$env:VCPKG_ROOT = 'E:\git\github\vcpkg'
cmake --preset default
cmake --build build --config RelWithDebInfo --target risen-modkit     # -> build\src\modkit\RelWithDebInfo\risen-modkit.exe
```

On Linux and macOS (only the editor and its tests are built there; the layer itself is Windows-only):

```sh
cmake --preset unix && cmake --build --preset unix-release --target risen-modkit
```

It needs OpenGL 3.3. The game itself is not needed on the machine, only a copy of its `data` folder (the editor reads it, never writes it).

## First start

Open **Window > Settings** and choose the game's **data directory**: `<Risen>\data`, the folder that holds `common\*.pak` and `compiled\*.pak`.
The editor reads the archives in under a second (templates, texts in every language, the names of meshes and icons) and remembers the folder.
Also set the **export folder**, normally `<Risen>\bin\spass\mods`.

## The window

Panels can be dragged, docked and tabbed; **Window > Reset layout** brings back the default arrangement.

| Panel | What it does |
|---|---|
| **Mod** | the mod's identity: id (folder name, a-z 0-9 _), name, version, author, description, the languages you write texts in (the first is the fallback for all others), dependencies; an overview of the content |
| **Browser** | every template of the game by group (Items, NPC, Monster, Objects, ...), search by name or label, filter by kind; the inspector shows every property of the selected template. Buttons for the editors: **New item based on this**, **Change this item**. **Include mod** (on by default) lists what the mod makes too, in the group *Mod* (new items, recipes, NPC templates, placed NPCs; blue, a double click opens one in its editor) |
| **Items** | your new items, changes of shipped items and item groups (below) |
| **Recipes** | new crafting recipes and changes of shipped ones (below) |
| **NPCs** | NPC and monster templates, and placed NPCs (below) |
| **NPC groups** | relative changes for every NPC template a selector matches (below) |
| **Spells** | changes of the 21 shipped spells and new spells (below) |
| **Dialogues** | conversations as node graphs (below) |
| **Quests** | quests and their flow as node graphs (below) |
| **World** | the game world in 3D (below) |
| **Assets** | the project's own files: compiled resources and audio (below) |
| **Behaviours** | scripts that run on NPCs, like components in Unity / Unreal (below) |
| **Problems** | what is wrong with the project (errors block the export, warnings do not); click a line to jump to the object |
| **Log** | what the editor did, and problems reading the game data |
| **Settings** | data directory, export folder, UI scale |

**Drag and drop**: drag a row of the Browser (or of the lists in the Items, Recipes and NPCs panels) onto

* a **template field** (base item, NPC template, recipe result, quest and dialogue items, ...): it takes the name when the kind fits (an item field
  takes items, an NPC template field NPCs and monsters; a placed NPC only where a field takes NPC names, e.g. quest targets);
* an **inventory, loot or ingredient list** (placed NPCs, chests in the World panel, NPC template loot, recipes, quest rewards): dropped on a row it
  replaces that row's item, dropped anywhere else on the list it adds the item (or one more of it when the list has it already);
* the **item names of an item group**, the **owner** of a dialogue (placed NPCs);
* the **World view**: an NPC or monster becomes a new placed NPC where you drop it, a placed NPC moves there, items and objects are spawned there.

**File** menu: new / open / save (`*.rmk.json`, plain JSON that works well in git), **Export mod...** (`Ctrl+E`). **Edit > Undo / Redo** (`Ctrl+Z`, `Ctrl+Y`) covers every change.

## Items

* **New item**: a copy of a *base* item (choose it with the arrow next to the field, or press *New item based on this* in the Browser) with its own
  **name** (the template name: letters, digits, `_`; give it a prefix of your mod, it is the item's key in savegames), a **label** and a
  **description** per language and the numbers you want to differ from the base. Everything you do not set is taken from the base.
* **Change a shipped item**: only the values you tick are changed; the game's value is shown in brackets next to each field. A label or description
  entered here replaces the game's text (exported into `strings/*.csv`).
* **Numbers**: value (gold), damage (weapons; melee damage = multiplier * (damage + Strength)), mana cost (runes and scrolls).
* **Requirements / modifiers**: tick *Replace ...* to replace the item's list; the shipped list is copied in as the start. Requirements are what the
  player needs to equip or use the item; modifiers are what the item adds while worn or when used (`HitPoints` for a potion, `ProtEdge` for armour).
* **Kind**: use type and category (the engine's enum names).
* **Appearance**: mesh (any `.xmsh` of the game), icon (any icon a shipped item uses), material switch.
* **Other properties**: any property of the template by name (`docs/ITEM_PROPERTIES.md` lists where the numbers live); *add property* offers the
  template's own properties with their current value.
* **Item groups**: a factor on the value or damage of every item of a group path (`Items/Potions`), a name pattern (a Lua pattern) or a list.
  Factors are computed from the shipped values, so applying them twice never compounds.

## Recipes

A recipe is an **item** (the plan, a "recipe item"): a character knows the recipe while the plan is in his inventory, and the station lists the
plans of its kind.

* **New recipe**: the plan's template name, the **station** (anvil, forge, grindstone, coolweapon, alchemy, goldsmith, scroll, cooking, frying),
  the **result item** (a shipped item or one of your new items), 1 to 16 **ingredients** with their counts, the **skills** the crafter needs
  (`Smith 1`, `Alchemy 2`, ...), and the plan's label and description (empty label: "Recipe: <result>"). **learn** gives the plan to the player once
  per savegame; without it, hand the plan out yourself (an NPC's inventory or a dialogue, coming stages). The engine makes one result item per craft.
* **Change a shipped recipe**: station, ingredients (tick *Replace ingredients*: the shipped list is copied in), skills and result; the shipped
  recipe is shown above. The smithing chain of the game is a sequence of recipe items (raw blade at the forge, hot blank at the anvil, ...,
  see `docs/RECIPE_PROPERTIES.md`).
* The Browser offers **New recipe for this item** on an item and **Change this recipe** on a recipe.

## NPCs and monsters

Two kinds of objects:

* **Templates** are kinds of NPCs and monsters. A **new template** is a clone of a base (a peasant, a wolf, ...) with its own name, label and
  numbers; a **change** of a shipped template rewrites it (new spawns at once, the ones already in the world at the next session start). The shipped
  values are shown in brackets: hit points, level, experience for a kill (10 x level, at least 50: setting it sets the level), mana, the skills
  (attributes 0..200, protection, combat skills 0..10), guild, species, attitude, bravery, damage calculation, combat species, AI group, the actor of a
  monster. Factors (hit points, damage) are computed from the shipped values. **Loot** shows what the template carries as shipped (and what it wears) and sets target amounts of items the template carries ("Start from the shipped loot" copies the shipped stacks into the list; 0 removes one) (reaches NPCs
  made afterwards). A human clone looks like its base: heads and body parts are separate items.
* **Placed NPCs** are persons in the world: a **unique name** (letters, digits, `_`, `-`: dialogues name their owner by it, savegames know the NPC by
  it), a template (shipped or one of yours), a label per language, **where** (an absolute position, or an offset from the player when the NPC is first
  made in a savegame: handy until the World view can place him), peaceful (a citizen nobody attacks) or a guild, level and hit points, an
  **inventory** (tick *equip* for what he wears or holds when he is made) and a **routine**: one post, a day / evening / night point, or a patrol;
  points are relative to his home and have a kind (guard, lookat, sleep, ...) and a facing. *Version* replaces the NPC in existing savegames when
  raised; *respawn* brings a dead one back at the next start.
* **Appears when / Leaves when** (placed NPCs): conditions -- a quest has a status, the player holds items, a game variable or event, a skill, a
  guild, an NPC dead or alive -- all or any of a list. *Appears when* keeps him out of the world until it holds (enemies that ambush the player after
  a quest, a messenger, a new trader), *Leaves when* removes him. The **quest picker** lists your quests and **every quest of the game** (370, with
  their titles, filter by id or title): `Rufo_GoToDruidHut is Success` spawns him once the player solved Rufo's quest of the original game.

The export declares every placed NPC with `risen.npc.persistent` (with conditions: `risen.spawn.when`): made in a new game (or when the condition
first holds), found again after a load, never made twice.

## Spells

A spell is a group of items and projectile templates (docs/SPELL_PROPERTIES.md), so the panel edits those pieces:

* **+ Change shipped**: one of the 21 shipped spells. **Mana cost** (or a **factor**, the two exclude each other), for rune spells the **scroll** and **orb** costs, **requirements**
  (`{ Fireball = 2 }`, `{ Circle = 3 }`), *used up by a cast*; for the projectile spells (Fireball, Frost, Missile) the projectile: **damage** (one value, or one per level), a factor,
  **damage type**, **speed**, **radius**, the flying effect. The shipped numbers are shown next to every field. Other properties of the crystal / rune by name (`gCCastInfo_PS.StartTask`
  re-uses another spell's effect).
* **+ New spell**: made of the pieces of a shipped one: the template name (`<name>`, `<name>_Scroll`, `<name>_Orb`), the forms to make (crystal and scroll for projectile spells, rune,
  scroll and orb for the others), label, description, icon, value and the same numbers. The new items are known to the item pickers (recipes, loot, quest rewards).

Exported as the `spells` and `new_spells` sections of `data/balance.lua`. Settings the panel has no field for (a per-form table like `scroll = { mana_cost = 7 }`, `projectile_properties`)
are kept as they are. A projectile setting on a rune spell, a scroll cost on a projectile spell, an unknown damage type or a damage list longer than the spell's projectile levels is a
problem (the layer would refuse the whole `risen.balance.install`).

## NPC groups

**+ Group** adds relative changes for many NPC and monster templates at once: the selector is a **template group path** (the templates below it, `Monster/Kapitel 2`), a **Lua pattern**
on the name (`^Wolf`), a pattern to **leave out** and extra **names**; empty = every NPC and monster. The panel lists what the selector matches (the layer refuses a selector that
matches nothing, so does the Problems panel). Changes: factors for hit points, mana, level, experience and damage, *level added*, skills added or multiplied (`{ ProtEdge = 1.1 }`), guild,
species, attitude, bravery, damage calculation, AI group. Everything is relative to the shipped values: applying it twice never compounds. Exported as `npc_groups`.

## Dialogues

A dialogue is the conversation of one **owner** NPC (a placed NPC of the project, or the unique name of an NPC in the world) with the player, drawn as
a graph on a canvas: scroll with the right mouse button, zoom with the wheel, **right-click** the background for a new node, **drag from a pin into
empty space** for a new node that is already linked, select and press **Delete** to remove. The selected node's properties are on the right.

| Node | Pins | What it is |
|---|---|---|
| **Option** | in: `parent`, `when`; out: `then`, `choices` | what the player says (one text per language). `then` leads to the lines and commands that follow; `choices` to sub-options shown after this one ran (the engine's sub-menu); `when` takes conditions (several = all). Type (Info, Important, ...), permanent (stays after use), info name (the option's key in savegames) |
| **Line** | in, `next` | a spoken line: the NPC, the player or another NPC (unique name) |
| **Command** | in, `next` | an engine dialogue command with its parameters: Give, GiveXP, SetGameEvent, DoTrade, ... (the parameter list comes from the engine) |
| **Quest** | in, `next` | starts, succeeds, fails or cancels a quest of the project |
| **Log entry** | in, `next` | a line in a quest's log |
| **Script** | in, `next` | calls a script (yours in `custom.lua`, or a game script) |
| **End** | in | ends the conversation |
| **Condition** | out | quest state, items carried, a game variable or event, a skill, a guild, an NPC dead / alive |
| **All of / Any of / Not** | in, out | combine conditions |
| **Comment** | | a note on the canvas |

Links only join pins that fit (the pin shapes and colours tell: triangles for the sequence, blue circles for options, teal for conditions), and the
option tree and the conditions cannot loop. A sequence that does not end with **End** returns to the list of options; an option without
sub-options gets an End by itself (an open conversation blocks saving). The inspector of an option shows the Lua the export writes for it; nodes with a
problem have a red border and the problems are listed under the properties and in the Problems panel.

The quest conditions and item amounts become conditions the engine checks itself; any, not, values of variables and guilds are one Lua condition
behind them (`risen.cond`, see `docs/LUA_API.md`).

**Voiced lines.** Options (the player's line), Lines and SaySound commands have a **Sound**: one row *every* (all voice languages) and one row per
language of the mod (it replaces *every* for that voice language). The arrow opens the sound list: the project's audio files first, then the game's
sounds (speech filtered by voice language, effects and music); **Play** previews a sound in the editor (also in the list and in the Assets panel).
The export copies the files into `sounds\` of the mod (a game sound is unpacked from the game's container as an MP3) and the line gets
`sound = "sounds/<file>"`; the layer compiles it and the line lasts as long as the audio (`docs/LUA_API.md` "Voice lines"). SaySound's
**SoundName** plays a sound the engine already knows (a game sound, or a compiled `._xsnd` of the project) by its raw name, without subtitle.

### Playing a dialogue through

**Play through** in the Dialogues panel (or **Window > Conversation**) plays the dialogue like a player. Pick options, read the lines; the NPC's answers, quest
actions, log entries and commands appear in grey. The right side lists every condition the dialogue's options use (`quest Q_X is Running`, `player has 2 It_IronOre`, ...)
with a control for the fact behind it (quest state, carried amount, variable, event, skill, dead NPC) and a **pin** (as world / yes / no) to force a condition. The
options update at once. **Talk again** starts a new conversation (used-up options stay used, the world stays), **New game** resets everything. Quest actions
and `Give` / `SetGameEvent` / `RunQuest` commands change the pretend world, so a quest started in one answer unlocks the next. The panel rebuilds when you edit the
dialogue and keeps the world. It lists options that can never be reached (their parent is no option of the dialogue).

## Assets

The **Assets** panel holds the project's own files: **Add files...** (several at once), **Remove**, **Reload** (after a file changed outside the editor).
A file inside the project's folder is stored relative to it, so save the project first.

* **Compiled resources** (`._xmsh`, `._xmat`, `._ximg`, `._xmac`, `._xmot`, `._xsnd`): exported into `assets\` and mounted by the layer; they
  replace game files of the same name everywhere (the panel and the Problems panel say so). The world view, the mesh previews and the pickers use them
  at once. The details show a mesh preview, the size of an image, the texture of a material, the length of a sound.
* **Audio files** (Ogg Vorbis, WAV, MP3): exported into `sounds\` for voiced lines; the details show format, channels, rate and length, with Play.
* **Pictures** (PNG, TGA, JPG, BMP): each becomes a texture `<name>._ximg` in `assets\` (DXT1, or DXT5 with transparency, with mip maps; the size is
  brought to powers of two up to 2048). Name a picture like a game texture (`ItWpn_SwordMisc_01_Diffuse_01.png`) to **retexture** everything that uses
  it; the world view and the mesh previews show it at once. Choose a picture as an item's or recipe's **icon** (Items panel, Icon) and it becomes a
  40x40 cell of the mod's icon atlas instead (`<mod>_IconAtlas._ximg` + `<mod>_Icons.ximl`, registered by the runtime before the items are made;
  the item's icon is then `IC_<mod>_<name>`).
* **Image lists** (`.ximl` + the `._ximg` it names): your own icon lists; the runtime registers every one of them.
* **3D models of your own**: **Import 3D model...** reads a Wavefront **OBJ**, a **glTF** (`.gltf` with its `.bin` next to it or embedded, or `.glb`) and writes a compiled mesh
  `<name>._xmsh` into the project's `assets\` folder (save the project first). The model's triangles are grouped by its materials; the mesh names **game materials**
  (`._xmat`, which carry the textures): pick one default material and, for a model with several, one per material of the model (the picker lists every material of the game and
  of the project). **Scale** (a glTF in metres wants 100: the game's units are about centimetres, a human is 180 high), **up axis +Z** (Blender and CAD exports) and
  **Mirror Z** (OBJ and glTF are right-handed, the game left-handed; switch it off if the model looks mirrored) are the options. Normals are computed when the model has none,
  tangents from the texture layout. Import the same file again under the same name to update the mesh. Then use the mesh like any other (an item's *Mesh* field, the World view
  places objects with it). On the command line: `risen-modkit --convert-model model.obj --out My_Mesh._xmsh --material X._xmat [--scale S] [--z-up] [--no-mirror]`.
  Checked in the game with `mods/meshprobe` (`smoke-test.ps1 -Scenario meshprobe`); skeletal animation and your own materials are not part of it.

## Behaviours

A behaviour is a Lua script that runs on every NPC it is attached to (the layer's `risen.behavior`, docs/LUA_API.md "Behaviours"):

* **+ New behaviour** writes `behaviors/<name>.lua` next to the project from a commented starter and opens it in your code editor (the program
  your system uses to edit `.lua` files); **Add a script...** takes an existing file. The script returns its definition: `params` (defaults),
  `interval`, `near`, and functions -- `start`, `update(self, dt)`, `on_player_near` / `far`, `on_talk`, `on_items`, `on_damage`, `on_death`,
  `on_quest` (every quest, the original game's included), `events` for any other event, `stop`. `self.state` is saved with the game.
* The panel shows what the editor read from the file (description, parameters with defaults, handlers) or the error, read again whenever the file
  changes on disk, and where the behaviour is attached.
* **Attach** it by dragging it from the list onto the *Behaviours* section of a placed NPC (just him) or an NPC template (every NPC of it: the
  world's own and later spawns; a *changed shipped* template works too, e.g. every wolf of the game), or with *attach a behaviour...* there. Each
  parameter of the script becomes a field (number, checkbox, text -- a text field also takes a template dropped from the Browser); a value equal
  to the default is not stored, *default* resets one.
* The export checks every script like the layer does (a broken one refuses the export with the reason) and copies it into `behaviors/` of the mod.

## Quests

A quest has a **name** (its id, the key of its state in savegames), a **title** per language (the quest log shows the first language's; the layer
takes one title), a **type** (Report, Kill, HasItems, ...), the **experience** the engine pays on success and *silent* (no "quest started"
notification). Its **flow** is a chain on the canvas:

| Node | What it does |
|---|---|
| **Start** | how the quest begins: `dialog` (a dialogue's Quest node with *start*), `game_start` (every new game), `item` (the player gets an item), `kill` (a template or a named NPC dies), `var` (a game variable reaches a value), `quest` (another quest reaches a status: quest chains) |
| **Objective** | waits until it is done: `items` (the player carries N of an item), `kill` (N deaths of a template or a named NPC, counted from the moment the objective begins), `talk` (a dialogue option finished playing), `var`, `quest`; may add a log entry when it begins |
| **Log entry** | a line in the quest log |
| **Reward** | experience (through the game's own GiveXP), gold, items, a game variable |
| **Outcome** | success, failure or cancelled, a log line, and a follow-up quest to start |
| **Either / or** | several ways to go on: link the first Objective of each way to its output. The quest waits for all of them at once, the first one the player completes decides the path (the others are dropped) and the flow goes on behind it |

Quests of the game count too: a Start or Objective of kind `quest` and every quest condition take any quest id of the game (the picker lists them
with their titles), e.g. start your quest when the player solved `Ash_Defeat` of the original game.

An **Either / or** makes a branching quest: the objectives its paths start with are announced together (each one's log text), and what the player does first picks the way:
kill the bandits or bring the ransom, with a different reward and a different outcome behind each. The paths run top to bottom as they lie on the canvas, they do not join
(every path ends with its own Outcome; a path without one just ends the flow) and an Either / or may sit inside a path (a decision after a decision). The progress of each
path is saved with the game.

Log entries, rewards and the outcome happen the moment the flow reaches them; objectives hold it until the player did them. The progress is part of
every savegame (a load rolls it back with the game), a reward is never paid twice, a quest with a flow is started once per savegame. A quest without a
Start node is started, succeeded and failed by its dialogues alone (their Quest nodes). The inspector shows the exported flow.

## World

The World panel reads a game world from the archives (World, the test levels; the choice is remembered) in the background when the game data is there:
every entity of every layer with its name, ID, template, position, facing and bounding box (about 48000 in the main world). The view starts at the
player's start point.

**Worlds.** Everything you place or change belongs to a world: a spawned item or object, chest items, a moved or removed entity, a placed NPC. It
gets the world the view shows when you place it (the island is `World`; test levels and the worlds of other mods have their own names), the view
shows only the project's things of that world, and the inspector (and the NPC editor's *Where*) can change it. The mod makes or changes each thing
only while its world is active: a mod that adds a world (reached by a teleporter, ...) puts its NPCs and items there, and nothing of it appears on the
island. "the project's world" is the default (`World`).

* **Move**: hold the right mouse button and look around, W A S D to move, Q / E down / up, Shift faster, the wheel changes the speed (without the right
  button it moves forward and back). **Click** an entity to select it, **F** flies to the selection. A click selects what is drawn under the
  cursor: a mesh by its pixels (what stands in front wins), an entity shown as a box by its box; hidden boxes and the level meshes (unless
  *Level meshes* is on) are never selected.
* **Show**: the kinds of entities (the colour of a box is its kind: green NPCs, red monsters, yellow items, orange chests and other interactive objects,
  grey objects; effects, navigation helpers and sounds are off at first), the huge boxes of the level meshes, and the layers by folder (the
  navigation, sound and effect layers start hidden).
* **Find** an entity by name or template; the list flies to the one you choose.
* **Drop** a template or placed NPC from the Browser (or the NPCs / Items panel) onto the view to place it on the ground under the cursor (the tooltip
  says what the drop does); it does the same as the place modes below, without choosing the template first.
* The **inspector** shows the selected entity: its template (*Template in Browser*), its ID (*Copy id*: what `risen.entity.by_id` takes), the position
  (*Copy position*: `{ x, y, z }` for Lua or a placed NPC), the facing, the layer, the mesh, the inventory of chests and NPCs, and every property.
* The project's placed NPCs with a world position are drawn as magenta boxes. Dialogue owners can be picked from the world's NPCs, and the Problems
  panel then checks that the owner exists.

**Meshes and textures**: the level meshes (landscape, towns, dungeons) are always drawn, other meshes within the *mesh distance*; they load in the
background (the count is under Draw). Entities without a mesh the editor can read (NPCs and monsters: they are animated actors; vegetation, lights,
helpers) are drawn as boxes; *Boxes of entities with a mesh* and *Boxes of objects without a mesh* show more boxes. Large textures are loaded at 1024
pixels at most.

The Browser and the item editor show a turning preview of a template's mesh (drag to turn it).

**Placing** (the buttons next to the search field):

* **Place NPC**: choose one of the project's placed NPCs (or *a new NPC of the template* and the template) and click on the ground: the NPC gets
  that world position (instead of an offset from the player). **Place item / object**: choose an item (and an amount) or an object -- a bench, a
  campfire, a barrel, any interactive or decoration template -- and a yaw, and click: the mod spawns it there once per savegame. The point under the
  cursor is read from the rendered depth, so things land on the ground, a roof, a table; a placed item or object is then settled with its
  lowest point onto the surface (see *Align with mesh*).
* **Select** mode: a click on a magenta (NPC) or yellow (spawned) box selects the project's object; the **gizmo** moves it along the axes (*gizmo: move*)
  or turns it about the vertical axis (*gizmo: turn*): drag an arrow (one axis) or a square (two axes). The inspector has the exact position,
  yaw / pitch / roll, the template and the amount. Undo covers every move.
* **Align with mesh** (inspector button, or **End** with the cursor in the view): lets the selected spawned object, placed NPC or moved entity down
  (or up) onto the surface below it -- the apple onto the table, the crate onto the slope. The editor looks straight down from just above the
  object over its footprint and puts its lowest point onto the highest surface there (the object itself does not count). Only what the view
  draws counts: hidden kinds and layers, and meshes beyond the mesh distance, are ignored.
* **The world's own entities**: select one and choose *Move or turn it* (it is then drawn at its new pose, an orange line leads from its old place;
  the gizmo and the inspector work as for spawned things; *Back to its place* drops the move) or *Remove it from the world* (drawn as a red box;
  *Keep it* takes it back). The mod finds them by their ID and applies both on every `world.ready`, so they hold after a load. Moving an NPC of the
  world is possible but his daily routine walks him off again, and removing one can break quests (the Problems panel warns).
* **Chests**: select a chest (or any entity with an inventory) of the world: *Add items to this container* lists items the mod puts into it once per
  savegame (found by the entity's ID).
* The export writes them into `data\world.lua`; the runtime spawns and fills on `world.ready` and remembers it in the savegame's mod data, so a load never
  spawns or fills twice; a spawned object is marked for the savegame (objects are not by themselves).

## Patches, strings, shipped dialogues and teachers

Four small panels for what no other panel owns (a converted Risen mod uses all of them; [PACK2MOD.md](../PACK2MOD.md)):

* **Patches**: write properties of any template of the game (or of your own new templates): the range of a light, a projectile's flight, an item's sort value. Add a patch (or use *Patch this
  template's properties* in the Browser), choose the template and add properties like on the Items panel's *Other properties* (values are typed like the game's: numbers, text, true / false, vectors as
  `x; y; z`). The mod applies them when the game starts and again with every world that loads. A property set the template lacks cannot be added.
* **Strings**: every text of the mod and of the game in one table (next section). Its *string* rows are the free text rows: rows of the game's string tables that belong to no item,
  recipe or NPC of the project (a button's label, a document, a changed line): an id and a text per language (**+ String row**, or *Override in the mod* on a game row). The first
  language is the fallback; a row without a text in it is shown only in the languages that have one.
* **Shipped dialogues**, tab *Dialogue edits*: add commands to a dialogue info of the game: an XP reward (added, or an existing amount changed) and **trader refills** (what a trader gets when the
  info runs: the chapter infos restock the traders; chapter 1's refills are also given when a new game starts) and **property edits**: change a value of the info, of its n-th command or
  condition where it still has the old value (an amount of gold given, the item a command gives), or append a condition (`PlayerKnows`, `ItemAmount`, ...). The new dialogues of the *Dialogues* tab are separate.
* **Shipped dialogues**, tab *Teacher limits*: how far a placed NPC teaches a skill (his entity ID from the World panel, the skill, the highest value).
* **Lookups instead of copying names.** *Info* is a search field over all 4514 dialogue infos of the game: type any part of the name, the speaker, the quest or the words of the first line (the list shows all four; the arrow opens it, free text is still accepted and shown in orange when the game does not know it). *Trader* and *NPC* search the NPCs of the loaded world (the world starts loading in the background when the World panel was not opened yet) and the project's placed NPCs; *Entity ID* lists exactly the entities that carry the chosen name, with layer, position and template (several guards share a name), and picking an ID fills in its name; a name that exists once brings its ID along. In *Property edits* the command or condition is chosen from what the info really has ("3  Say  Speaker = Sara ..."), the property from that command's own properties with their current values (the current value becomes the old value), and an appended condition from the condition classes the game uses with their property names.

Items: the requirement and modifier lists keep their **order** (the ^ button moves a row up) and each modifier has a **mode**: *add* (default), *set* (to the value), *max* (to the skill's maximum),
*percent* (of the maximum).

## Strings and voice recording

The **Strings** panel lists every text of your mod *and* of the game in one table: the lines of your dialogues (option, NPC line, SaySound), quest titles and log entries, HUD
messages and log texts of event graphs, names and descriptions of items, recipes, spells, NPCs and layers, the free string rows, and all rows of the game's string tables (about 24 000:
15 000 dialogue lines of `infos.tab`, the standard voice messages of `svms.tab`, items, quests, HUD, documents ...). The game's rows appear once the game data is loaded (Settings).

* **Columns**: id, source (`dialogue line`, `item label`, `game: infos`, ...), speaker, *Audio <Language>* for each language of the mod (*recorded*, *missing*, `-` for texts nobody
  speaks), the text in the chosen language, the stage direction and the context (dialogue and option, quest, the game's info / quest / role).
* **Filter**: free text (id, texts, speaker, voice, object, quest), *Mod and game / Mod / Game*, the kind (`dialogue`, `item`, `infos`, `svms` ...), speaker or voice, dialogue / quest / info,
  the language, *Spoken* (only lines someone says), *Missing audio* (spoken lines without a recording in the chosen language), *Overridden* (also game rows a row of the mod replaces).
  The table is a virtual list over a prepared index: filtering 24 000 rows is instant.
* **Editing**: select a row; the box below shows every language. Rows of the mod are edited in place (the change goes into the dialogue node, the item, the quest ...; one undo step per
  edit). Dialogue lines also take a **stage direction** per language ("whispering"): it is only for the voice actor (export below), the game does not see it. **Open** jumps to the object.
  Game rows are read-only; **Override in the mod** copies one into a free string row with the game's texts, which you then change (the game row is hidden from then on).
  Recorded lines have a play button per language (the game's own speech files too).

### Voice recording

The workflow for a voice studio: export the lines, record, import the folder of recordings.

1. **Export**: filter the table to what should be recorded (e.g. *Mod*, *Spoken*, *Missing audio*, or one speaker), then **Export for voice recording...**: choose a folder and the
   languages. Written are
   * `voice_lines.csv`: one row per line and language, UTF-8 with BOM (Excel and LibreOffice open it with the right characters), comma separated, RFC 4180 quoting;
   * `voice_lines.json`: the same rows (`{ "mod", "columns", "lines": [ {...} ] }`) for scripts;
   * `scripts/<speaker>_<Language>.txt`: a plain script per speaker and language (context headings, line id, stage direction, text, the file to deliver) to print for the session.
   *Only lines without a recording* leaves out what is done; switching off *Only spoken lines* exports every text of the selection (item names, descriptions, quest texts, HUD
   messages) for translators, with an empty status.
2. **Record**: the studio delivers one file per line and language, named **`<line_id>_<Language>.<ext>`** (the `expected_file` column, e.g. `my_mod_1a2b3c4d_German.wav`). Any common
   format works (WAV, FLAC, AIFF, Ogg, Opus, MP3, M4A ...). Studios that name files their own way fill the **`file`** column of the CSV instead (a bare file name or a path relative to
   the folder) and send the CSV back.
3. **Import**: **Import recordings...**, choose the folder (a `voice_recordings` or `recordings` folder next to the project is preselected, with its `voice_lines.csv`) and optionally the
   filled CSV. **Preview** lists for every file what happens: *copy* (the game plays it as it is), *convert* (through ffmpeg), *cannot* (no ffmpeg), *unmatched* (no line has that
   name), *ambiguous* (the name or the CSV fits more than one line) and *duplicate* (two files for the same line and language: none is taken until you remove one). **Import** copies /
   converts in the background (progress bar, *Cancel*) into the project's `voice/` folder as `<line_id>_<Language>.ogg` (or the original extension when copied), adds the files to the
   Assets and sets them as the line's sound for that language. One **Ctrl+Z** undoes the assignment (the files stay in `voice/`). The project must be saved first.
   File names that match: `<line_id>_<Language>` (the language as name or code: `English`, `en`, `de` ...), `<line_id>` alone (the language chosen in the dialog), and the name the
   layer gives the line (`INFO_SPASS_<mod>_<info>_<n>_<Language>_%_SPASS`, the `runtime_sound` column). Case does not matter. *Only the shown lines* restricts the import to the rows
   the filter shows (off: every spoken line of the mod).

**Line ids** stay the same across saving, renaming the dialogue and moving nodes: a dialogue node's id is `<mod id>_<the first 8 hex digits of the node's uid>` (an option without an
own info name has the same as its info). Other texts have `<kind>.<name>.<field>` (`item.MK_Sword.label`) or their string id; game rows the id of their table row.

**CSV columns** (`voice_lines.csv`, in this order):

| Column | Meaning |
|---|---|
| `line_id` | the stable id of the line (above) |
| `language` | the language of this row (`English`, `German` ...) |
| `status` | `recorded` / `missing` (for texts nobody speaks: empty) |
| `speaker` | who speaks: the entity (`PC_Hero` for the hero, the NPC's unique name) |
| `voice` | the NPC voice of the speaker's template (`gCNPC_PS.Voice`: `Player`, `Enzo` ...; with game data) |
| `listener` | who is spoken to |
| `text` | the line in this language (falls back to the first language of the mod) |
| `stage_direction` | how to say it (dialogue lines of the mod: the editor's stage direction; game rows: `<Language>_StageDir`) |
| `context` | where it is said: `Dialogue Maren > option 'Who are you?'`, `Quest ...`, the game's info, quest and role |
| `source` | `mod` or `game` |
| `kind` | `dialogue/option`, `dialogue/line`, `dialogue/command`, `quest/log`, `item/label` ... (game rows: the table) |
| `object` | the dialogue / quest / item it belongs to (game: the info) |
| `quest` | the quest of a log entry or of a game line |
| `parent_line` | the line id of the option this line answers |
| `order` | the place in its info (1 = the option's own line) |
| `expected_file` | the file name to deliver: `<line_id>_<Language>.ogg` (game rows: the game's `WaveFile`) |
| `runtime_sound` | the name the game plays the line under: `INFO_SPASS_<mod>_<info>_<n>_<Language>_%_SPASS.wav` (it moves when lines are inserted before it; the line id does not) |
| `current_file` | the recording the line has now |
| `file` | **filled by the studio**: the recording of this row when it is not named `expected_file` |
| `notes` | free, for the studio |

**Audio formats and ffmpeg.** The game plays **Ogg Vorbis, WAV (PCM 8/16/24/32 bit, float, ADPCM) and MP3** as they are (FMOD recognises the format by content; docs/RE.md section 28).
Everything else (FLAC, Opus, AIFF, M4A/AAC, WMA ...) is converted with **ffmpeg** to Ogg Vorbis, by default **mono, 44.1 kHz** like the game's own speech (positioned voices should be
mono), optionally with **loudness normalisation** (EBU R128 to -16 LUFS); *Convert every file* sends files that already play through the same processing. A converted file is checked
with the layer's own audio probe before it is used. ffmpeg is **not bundled**; the editor runs the `ffmpeg` it finds on `PATH` or the one set in **Settings > Audio** (*Test* shows the
version and whether it has the `libvorbis` encoder; without it conversions write 16-bit WAV). Getting ffmpeg:
* Windows: a build from https://www.gyan.dev/ffmpeg/builds/ or https://github.com/BtbN/FFmpeg-Builds/releases (or `winget install Gyan.FFmpeg`); add its `bin` folder to `PATH` or set the path of `ffmpeg.exe`.
* Linux: the package manager (`sudo apt install ffmpeg`, `sudo dnf install ffmpeg` with RPM Fusion, `sudo pacman -S ffmpeg`).
* macOS: `brew install ffmpeg` (Homebrew) or MacPorts.
Without ffmpeg the files that already play are imported and the others are listed with this hint.

From the command line (no window; the import saves the project):
```
risen-modkit my.rmk.json --voice-export rec --language German --missing        the sheets of the German lines without a recording
risen-modkit my.rmk.json --voice-import rec --csv rec/voice_lines.csv --dry-run  the preview as text
risen-modkit my.rmk.json --voice-import rec --csv rec/voice_lines.csv --normalize
```
`examples/modkit/voice.rmk.json` with `voice_recordings/` is a complete example (a stereo FLAC converted, a WAV copied, an AIFF named in the filled CSV); scenario `modkit_voice`
plays it in the game.

## Command palette and wizards

**Ctrl+P** (Edit > Command palette) opens one search box over everything: the objects of the project (items, recipes, NPC templates and placed NPCs, dialogues, quests, spells,
NPC groups, behaviours, asset files), **the game's templates** (a hit opens it in the Browser), the panels and the editor's commands (Play, Export, Save, the wizards). Type
words in any order (`hob knife`), Up / Down choose, Enter opens, Esc closes. Without text it lists the commands and panels.

The **Wizards** menu makes common things from a short form; what it makes is ordinary content you can edit afterwards (and Ctrl+Z takes it back):

* **Fetch quest**: an NPC asks for items. Makes the quest (Start, Objective *items*, Objective *talk*, Reward, Outcome) and two options for the NPC's dialogue (it extends the
  NPC's dialogue when there is one): the first offers the job once and starts the quest, the second turns the items in while the quest runs and the player carries them
  (the NPC takes them), which completes the *talk* objective and pays the reward.
* **New weapon with a recipe**: an item cloned from a shipped one (damage, value, requirements) and the recipe item that crafts it (station, ingredients, skills, known
  from the start or found).
* **Trader with stock**: a placed NPC with his own template (`gCDialog_PS.TradeEnabled` on, so the shipped template stays as it is), the stock in his inventory and a
  dialogue option that runs the `DoTrade` command. He is made in front of the player when the game first makes him. **Not tried in a real trade yet**: test him with Play.

## Events

The **Events** tab holds event graphs on the same canvas as dialogues and quests. A graph starts at a **trigger** (green: On game start / load, On chapter, On kill, On player moved,
On enter / leave area, On quest status, On item, On dialogue option, On time of day, On variable changed, On timer, On signal) and runs along its links; an output linked to several nodes runs them
all (top to bottom). **Condition** nodes test a list of checks (variable, quest, items, chapter, level, skill, NPC alive / dead, guild, game event, time window, player in an area, layer, game sector,
random chance; combined with all / any, optionally negated) and go on at `then` or `else`. **Flow** nodes: Once, After N passes, Cooldown, Delay (survives save and load), Repeat, Sequence, End.
**Actions**: Set variable (a game variable, which dialogues and quests can read, or a mod variable kept in the savegame), Spawn / Remove NPC, Give / Take item, Give experience, Quest, Quest log,
HUD message (`{chapter}`, `{value}`, `{var:name}` ...), Play sound, Teleport, Set chapter, Set time, Heal / hurt, Run script, Send signal, Layer, Game sector, Write to log.
The example `examples/modkit/events.rmk.json` has one graph of each flavour; [CHRONICLE.md](CHRONICLE.md) describes a complete four-chapter mod that uses every feature of the editor (`examples/modkit/chronicle.rmk.json`). `On player moved` fires when the player has moved at least N metres (N >= 1) since it last fired; turning never does.
The first graph of a new project is the help's example: On chapter 2 -> Condition (intro_done = 0) -> Set variable -> End. State (Once, counters, cooldowns, delays, mod variables) lives in the savegame:
a quick load rolls it back with the world.

## Layers

The **Layers** tab: a layer is a named group of placed NPCs, world spawns and container fills that is switched as a whole (like the game's story sectors). Tick **On when a game starts** or not; let it
switch itself the first time a condition holds (**Switches on when** / **Switches off when**, the checks of the Condition node) or switch it from an event graph (Layer node). The plan shows where the
members stand (ring = off at the start). NPCs are made while the layer is on and removed when it goes off (a new body each time), spawns likewise, container fills happen once. The **Game sector**
node switches one of the game's own sectors (`World::SetSectorStatus`; the picker lists the 289 sectors of the world, 21 of which start off); entities that were already made stay when a sector is
switched off.

**In the World view** the section *Mod layers* (left) lists the project's layers with their colour, "on" / "off at start" and the number of members; untick one to hide its members, the context
menu opens it in the Layers panel. Members are drawn in their layer's colour (a cross on top: the layer starts off), the containers a layer fills are outlined in its colour, and the inspector of a
placed NPC, a spawn or a container fill has a *Mod layer* combo to move it into another layer (or none: always there). The project's placed NPCs are drawn as their actors like the game's NPCs.

## Play and the game's errors

Set the **game folder** (the Risen folder with `bin\Risen.exe` and the mod layer) in **Window > Settings > Play**, choose what the game does at start
(**Game > Start with**: main menu, a new game, the last quick save) and press the green **Play** button or **F5**:

* The project is validated (errors stop Play), exported into `<game folder>\bin\spass\mods\<id>` and Risen starts windowed (the setting *Windowed*
  sets `FullScreen="false"` in `data\ini\ConfigLocal.xml`; the first time a backup `ConfigLocal.xml.modkit-backup` is kept). The editor turns on the layer's
  file watcher (`[mods] watch = true` in `bin\spass\config.toml`) and, for "new game" / "quick save", adds the tiny helper mod `modkit_autostart`.
* While that game is running, **Play** only exports again: the layer reloads the mod from the changed files, no restart (the status line says so).
  A game that was started without the watcher has to be restarted.
* The editor reads `bin\spass\spass.log` while a game folder is set. Warnings and errors that carry `[<mod id>]` or name something of the project
  (an item, NPC, quest, a dialogue option's generated name, a `MK_<quest>_<action>` script, ...) appear in the **Problems** panel under *From the game*;
  a click selects the object. They never block the export. **Game > Forget the game's errors** clears the list, **Game > Open the game's log** opens the file.

Command line: `risen-modkit project.rmk.json --game-dir <Risen folder> --play`.

## Export

**File > Export mod...** writes the mod into a folder, normally `<Risen>\bin\spass\mods\<mod id>`. The export

* is refused while the Problems panel has errors,
* writes only into an empty folder or one it wrote before (it recognises its own `modkit\project.json`), never into the game's `data` folder,
* writes only the files that changed and removes generated files that are no longer needed,
* creates `custom.lua` once for your own Lua code and never overwrites it.

Start the game: the layer loads the mod like any other (it is listed in the Mods tab of the ESC menu). With `[mods] watch = true` in the layer's
`config.toml`, a re-export while the game runs reloads the mod (texts and numbers of items; new templates need a restart).

## Import

**File > Import mod** opens an exported mod as a new project (save it to keep it):

* **From its project**: the export put the editor's project into `modkit\project.json`; this reads it back exactly as it was.
* **From its data files**: for a mod whose `data\*.lua`, `strings\*.csv` or `mod.toml` were edited by hand (or that has no `project.json`). The data
  files are plain tables; the editor runs them in an empty Lua state (no libraries, a time limit) and turns them back into items, recipes, NPCs,
  dialogues and quests. Dialogue and quest graphs get a new layout. Balance sections the editor has no panel for (spells, new spells, NPC groups) are
  kept and written again by the next export; the report lists them. `custom.lua` stays in the mod folder (export into the same folder again).

Command line: `risen-modkit --import <mod folder> --save project.rmk.json [--from-data]`.

**A Risen mod's archives** (`.p00` / `.pak` volumes, e.g. the Risen Extended Mod) become a project with `risen-pack2mod <mod folder> --baseline <Risen data> --project my_mod\my_mod.rmk.json`
(then **File > Open**): the changed and new items (with the order and modes of their lists), the recipes, monster actor swaps, template patches, container fills, XP and trader edits of shipped
dialogue lines, teacher limits, texts in every language (and the text rows no object owns) and the loose meshes, textures and actors, ready to edit; the Problems panel shows the editor's checks
(stock textures the mod replaces are warnings). The few things the engine cannot hold, and the world moves that cannot be told from the base game's own differences (`--world-edits` carries them),
are listed in the project folder's `report.md`. [PACK2MOD.md](../PACK2MOD.md) "Editor project" has the table of what ends up where.

## The exported mod

```
<mods>\<id>\
  mod.toml              generated from the Mod panel
  main.lua              generated: requires the runtime, installs the data, then requires custom.lua
  custom.lua            yours: created once, never overwritten
  data\balance.lua      items, recipes, NPC and monster templates as a risen.balance.install spec (readable, plain data)
  data\npcs.lua         the placed NPCs (risen.npc.persistent declarations)
  data\quests.lua       the quests (risen.quest.define)
  data\dialogs.lua      the dialogue options (risen.dialog.create, parents first) and the quest actions their lines call
  data\world.lua        items spawned at a position and items added to containers, once per savegame
  assets\               the project's compiled resources and the textures and icon atlas made from pictures (mounted: mod.toml assets = "assets")
  data\assets.lua       the icon lists the runtime registers
  sounds\               the audio of voiced lines (the project's files, game sounds unpacked)
  strings\all.csv       texts of shipped items you renamed (first language = fallback), strings\<Language>.csv for the others
  modkit\runtime.lua    the generic code, the same for every exported mod
  modkit\project.json   the editor project the mod was made from (File > Open it, or keep it for a later import)
```

The runtime applies the data with the layer's savegame-safe calls (`risen.balance.install`: on the first tick and on every session start) and emits
the event `modkit.applied` (`mod id, when, report`) after every application, and `modkit.npc_ready` (`mod id, NPC name, reason`) whenever a
placed NPC is in the world, and `modkit.quest_action` (`mod id, quest, action`) after a dialogue line started, succeeded, failed or
cancelled a quest, `modkit.quest_started` / `modkit.objective` / `modkit.quest_done` for quest flows, and `modkit.world_spawned` /
`modkit.container_filled` / `modkit.world_removed` / `modkit.world_moved` for the world edits, so other mods and tests can react. Quests and dialogue options are made on `load.before` and on every session start (the engine
restores their saved state only into objects that exist).

## Command line

```
risen-modkit [project.rmk.json] [--data DIR]                       open the editor
risen-modkit project.rmk.json --export DIR [--data DIR]            validate and export without a window (exit code 0 = exported)
risen-modkit ... --screenshot FILE [--frames N] [--panel TITLE] [--select NAME] [--size WxH] [--camera x,y,z,yaw,pitch] [--click x,y ...]
                    [--drag x1,y1,x2,y2 ...]                       render (after the given clicks and drags, in order), save a PNG, exit (tests)
--verbose                                                          debug lines on stderr too
risen-modkit project.rmk.json --game-dir <Risen folder> --play    the Play button (export, start the game or hot reload); --palette TEXT and --wizard fetch|weapon|trader open the palette / a wizard (screenshots)
risen-modkit --convert-model model.(obj|gltf|glb) --out My_Mesh._xmsh --material X._xmat [--scale S] [--z-up] [--no-mirror]
                                                                   convert a 3D model into a compiled mesh without a window
risen-modkit --import MODDIR --save project.rmk.json [--from-data] import an exported mod (or only its data files) as a project
risen-modkit project.rmk.json --voice-export DIR [--language L]... [--missing] [--all-text] [--with-game] [--no-scripts] [--data DIR]
                                                                   the voice recording sheets (voice_lines.csv / .json, scripts/) without a window
risen-modkit project.rmk.json --voice-import DIR [--csv FILE] [--language L] [--normalize] [--convert-all] [--stereo] [--ffmpeg PATH] [--dry-run]
                                                                   match, convert and assign recordings, save the project (--dry-run: only the report)
```

## Limits

* Monsters and animals are drawn as their actor in the world view (the bind pose, textured, no animation: 1253 of the 1306 monsters of the game); NPCs (humans are assembled from body parts, heads and armour) and vegetation (SpeedTrees) are still boxes.
* A spawned object's collision was not checked; level meshes (the landscape, houses that are part of a level mesh) cannot be moved or removed, only
  entities.
* Icon previews show the game's own icons; a picture of the project used as an item's icon is not previewed (only its name).
* Models are imported from OBJ and glTF/GLB (not FBX), as static meshes with one UV set and the game's own materials: the editor does not write materials, so a new texture on a
  new mesh needs a material of the game (or a compiled `._xmat` file in the assets) that names it. Whether the game's axes are the mirror image of OBJ/glTF (the default *Mirror Z*) was
  inferred from D3D being left-handed, not measured; the triangle winding was (a first version rendered inside out in the game).
* A picture replaces a game texture only where the engine has not loaded it yet when the mod is mounted (at start: always).
* Lip sync: a voiced line of the mod has no lip-sync motion (the face does not move); a data-only import turns game sounds the export unpacked into
  project audio files (an import from `modkit\project.json` keeps them as game sounds).
* A quest flow branches only by *doing* (Either / or: the first objective completed decides); the paths do not join, and a branch cannot be chosen by a condition (use dialogues for that).
* The Conversation panel follows the layer's documented rules for dialogue options, it does not run the engine: commands other than End, Give, Say, quest and game-event commands are only listed; test the finished mod with Play.
* Shipped loot is read from the template files: stacks naming an item no template of the game has (six in the whole game) are skipped like the engine does; the worn items (armour, helmet) are listed but not editable.
* Icons are offered from the shipped items' icons.
* Linux and macOS builds are part of the CMake setup but were not built yet (no such host in the development setup).
