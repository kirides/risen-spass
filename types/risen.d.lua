---@meta
-- LuaLS (lua-language-server) definitions for the risen-spass modding API.
-- Add this directory to `workspace.library` (see types/README.md) to get completion, hover docs and type checks.
-- Reference: docs/LUA_API.md. tools/check_luadefs.py verifies that every binding is described here.

---------------------------------------------------------------------------------------------------------------------
-- Entities
---------------------------------------------------------------------------------------------------------------------

---An engine entity (NPC, item, the player, ...). The handle remembers the entity's unique name: after a savegame load,
---a session start/stop or a world change it looks the entity up by name again, so a handle kept across those points
---either refers to the right entity or fails safely (methods return `nil`/`false`; see `Entity:valid`).
---Two handles are `==` when they refer to the same entity. Spawned NPCs need `risen.loc.set("FO_<name>", ...)` for the label
---shown when the player focuses them.
---@class Entity
---@field address integer Raw engine address (for logging/debugging only).
local Entity = {}

---Unique entity name (the scene admin's key).
---@return string|nil name `nil` if the handle is stale.
function Entity:name() end

---The name shown to the player: currently the same as `name`; the focus label is the localization string `FO_<name>`.
---@return string|nil
function Entity:display_name() end

---Whether the entity still exists in the world (looked up again by its unique name).
---@return boolean
function Entity:valid() end

---Renames the entity. The handle keeps working under the new name.
---@param name string Must be unique.
---@return boolean ok
function Entity:set_name(name) end

---World position.
---@return number|nil x
---@return number|nil y
---@return number|nil z
function Entity:position() end

---Distance to another entity in engine units (centimetres).
---@param other Entity
---@return number|nil
function Entity:distance_to(other) end

---@return boolean
function Entity:is_npc() end
---@return boolean
function Entity:is_player() end
---@return boolean
function Entity:is_dead() end
---@return boolean
function Entity:is_item() end
---@return boolean
function Entity:is_character() end

---Removes the entity from the world (destroys it; not a combat death, no `npc.killed` event).
---@return boolean ok
function Entity:kill() end

---Freezes or unfreezes the entity's simulation.
---@param on boolean
---@return boolean ok
function Entity:set_frozen(on) end

---@param on boolean
---@return boolean ok
function Entity:enable_rendering(on) end

---Teleports this entity to a named entity (waypoint, NPC, ...) or onto another entity.
---@param target string|Entity
---@return boolean ok
function Entity:teleport(target) end

-- Item properties (entities made from item templates) ------------------------------------------------------------------

---Whether the entity has item data (is an item).
---@return boolean
function Entity:is_item_valid() end
---Gold value of an item.
---@return integer|nil
function Entity:item_value() end
---Stack amount of an item entity.
---@return number|nil
function Entity:item_amount() end
---@param amount integer
---@return boolean ok
function Entity:set_item_amount(amount) end
---Item category (`gEItemCategory` value).
---@return integer|nil
function Entity:item_category() end
---How the item is used (`gEItemUseType` value).
---@return integer|nil
function Entity:item_use_type() end
---Name of the item's inventory icon image.
---@return string|nil
function Entity:item_icon() end
---Quest/mission items cannot be dropped or sold.
---@return boolean
function Entity:is_mission_item() end

-- NPC configuration ---------------------------------------------------------------------------------------------------

---Guild id: `0` none/animals, `1` Don's camp, `3` peasants, `5` Inquisition, `7` pirates/bandits (observed; others exist).
---@return integer|nil
function Entity:guild() end
---@param guild integer
---@return boolean ok
function Entity:set_guild(guild) end

---Free-form group name the AI uses to find allies.
---@return string|nil
function Entity:group() end
---@param group string
---@return boolean ok
function Entity:set_group(group) end

---@return integer|nil
function Entity:hp() end
---@return integer|nil
function Entity:hp_max() end
---@return integer|nil
function Entity:level() end
---@return integer|nil
function Entity:mana() end
---@return integer|nil
function Entity:mana_max() end

---Adds `delta` (may be negative) to the current hit points.
---@param delta integer
---@return boolean ok
function Entity:add_hp(delta) end
---@param delta integer
---@return boolean ok
function Entity:add_hp_max(delta) end
---@param delta integer
---@return boolean ok
function Entity:add_level(delta) end
---@param delta integer
---@return boolean ok
function Entity:add_mana(delta) end
---@param delta integer
---@return boolean ok
function Entity:add_mana_max(delta) end

---Sets the hit point maximum (absolute). Call this before `set_hp`.
---@param value integer
---@return boolean ok
function Entity:set_hp_max(value) end
---Sets the hit points (absolute).
---@param value integer
---@return boolean ok
function Entity:set_hp(value) end
---@param value integer
---@return boolean ok
function Entity:set_level(value) end
---@param value integer
---@return boolean ok
function Entity:set_mana(value) end
---@param value integer
---@return boolean ok
function Entity:set_mana_max(value) end

---The NPC's current AI state script, e.g. `"ZS_HangAround"`.
---@return string|nil
function Entity:state() end

---Switches the AI state. The running AI may replace it within seconds. States are registered `ZS_*` scripts:
---`ZS_Ambient`, `ZS_AmbientObserve`, `ZS_AmbientSleep`, `ZS_HangAround`, `ZS_Housewife`, `ZS_Observe`, `ZS_Watch`,
---`ZS_Study`, `ZS_Smith`, `ZS_Trader`, `ZS_Talk`, `ZS_FollowPlayer`, `ZS_GuidePlayer`, `ZS_WaitForPlayer`, `ZS_Attack`,
---`ZS_Dead`, `ZS_Monster`, ...
---@param state string
---@return boolean ok
function Entity:set_state(state) end

---Cancels the current AI action.
---@return boolean ok
function Entity:full_stop() end

---Walks to another entity.
---@param target Entity
---@param mode? integer Engine walk-mode enum (0 = default).
---@return boolean started
function Entity:goto_entity(target, mode) end

---Walks to a position.
---@param x number
---@param y number
---@param z number
---@param mode? integer Engine walk-mode enum (0 = default).
---@return boolean started
function Entity:goto_pos(x, y, z, mode) end

---@return boolean ok
function Entity:stop_goto() end

---Turns to face another entity.
---@param target Entity
---@return boolean started
function Entity:turn_to(target) end

---Equips every equippable inventory stack.
---@return integer equipped How many stacks were equipped.
function Entity:equip_all() end

---Number of inventory stacks.
---@return integer|nil
function Entity:stack_count() end

---------------------------------------------------------------------------------------------------------------------
-- risen.entity / risen.template / risen.item / risen.npc
---------------------------------------------------------------------------------------------------------------------

---@class RisenEntityApi
local entityApi = {}

---Looks up an entity by its unique name.
---@param name string
---@return Entity|nil
function entityApi.get(name) end

---The player character.
---@return Entity|nil
function entityApi.player() end

---Spawns an entity from a template. Needs a running game. Spawned entities are **not** saved in savegames: spawn them
---again on `session.start` / `load.after`.
---@param template string Template name, see docs/TEMPLATES.md (`risen.template.list`).
---@param x number
---@param y number
---@param z number
---@param temporary? boolean Spawn as a temporary entity.
---@return Entity|nil
function entityApi.spawn(template, x, y, z, temporary) end

---@class RisenTemplateApi
local templateApi = {}

---Whether a template with that name exists.
---@param name string
---@return boolean
function templateApi.exists(name) end

---Names of all entity templates found in the packed data.
---@param group? string Sub-directory of `data/common/templates` (`"NPC"`, `"Items"`, `"NPC/Harbour"`, ...); all if omitted.
---@return string[]
function templateApi.list(group) end

---@class RisenItemApi
local itemApi = {}

---Creates items in an entity's inventory.
---@param owner Entity
---@param template string
---@param count? integer Default 1.
---@param stack_type? integer `gEStackType` (0 default).
---@return integer created Created amount, `-1` on failure.
function itemApi.give(owner, template, count, stack_type) end

---Amount of a template in an inventory.
---@param owner Entity
---@param template string
---@return integer
function itemApi.count(owner, template) end

---Gold in an inventory.
---@param owner Entity
---@return number
function itemApi.money(owner) end

---@class NpcSpawnOptions
---@field name? string Unique entity name; also the key of its label (`FO_<name>`).
---@field level? integer
---@field hp_max? integer
---@field hp? integer Defaults to `hp_max` when that is given.
---@field mana? integer
---@field guild? integer
---@field group? string
---@field items? {[1]: string, [2]?: integer}[] `{ {"It_1H_Machete", 1}, ... }`
---@field equip? boolean Equip the items.
---@field state? string Initial AI state (the AI may switch immediately).

---@class RisenNpcApi
local npcApi = {}

---Spawns a template and configures it in one call.
---@param template string
---@param x number
---@param y number
---@param z number
---@param opts? NpcSpawnOptions
---@return Entity|nil
function npcApi.spawn(template, x, y, z, opts) end

---------------------------------------------------------------------------------------------------------------------
-- Quests
---------------------------------------------------------------------------------------------------------------------

---A quest object of the engine's quest manager.
---@class Quest
local Quest = {}

---Sets any engine property by name, e.g. `"Type"`, `"ExperiencePoints"`, `"Silent"`. Enum values by name (`"gEQuestType_Kill"`).
---@param name string
---@param value string
---@return boolean ok
function Quest:set_property(name, value) end

---Reads any engine property as text (`"Status"` -> `"gEQuestStatus_Running"`).
---@param name string
---@return string|nil
function Quest:get_property(name) end

---@return string|nil
function Quest:name() end

---Sets the quest's title in the log to a localization key (serve its text with `risen.loc.set`).
---@param key string
---@return boolean ok
function Quest:set_topic(key) end

---Appends a log entry whose text is a localization key.
---@param key string
---@return boolean ok
function Quest:add_log_entry(key) end

---@return integer
function Quest:log_count() end

---Adds the quest to the quest manager (refused when the name already exists).
---@return boolean ok
function Quest:register() end

---@alias QuestType
---| "Report" Talk to someone.
---| "Kill"
---| "HasItems"
---| "EnterArea"
---| "BringNpc"
---| "FollowNpc"
---| "DriveAway"
---| "Defeat"
---| "Duel"
---| "Arena"
---| "Plunder"
---| "Sparring"

---@class QuestSpec
---@field name string Unique quest id.
---@field title? string Title in the quest log.
---@field type? QuestType Default `"Report"`.
---@field xp? number Experience points reward.
---@field silent? boolean No "quest started" notifications.

---@class RisenQuestApi
local questApi = {}

---Status transitions use the quest manager; each returns whether the engine accepted it.
---@param name string
---@return boolean
function questApi.open(name) end
---@param name string
---@return boolean
function questApi.run(name) end
---@param name string
---@return boolean
function questApi.close(name) end
---@param name string
---@return boolean
function questApi.fail(name) end
---@param name string
---@return boolean
function questApi.succeed(name) end
---@param name string
---@return boolean
function questApi.is_running(name) end

---Creates a quest object without registering it (see `risen.quest.define` for the usual way).
---@param name string
---@return Quest|nil
function questApi.new(name) end

---@param name string
---@return Quest|nil
function questApi.get(name) end

---Number of quests known to the manager.
---@return integer
function questApi.count() end

---@param index integer 1-based.
---@return Quest|nil
function questApi.at(index) end

---@param name string
---@return boolean
function questApi.exists(name) end

---@param name string
---@return boolean
function questApi.remove(name) end

---Creates and registers a quest. Validates the spec and is idempotent: an existing quest is returned unchanged.
---Define quests on `load.before` and `session.start`: the engine restores a quest's state only into quests that exist.
---@param spec QuestSpec
---@return Quest|nil quest
---@return string|nil error
function questApi.define(spec) end

---Appends a text entry to a quest's log.
---@param quest_name string
---@param text string
---@return boolean ok
---@return string|nil error
function questApi.add_log(quest_name, text) end

---------------------------------------------------------------------------------------------------------------------
-- Dialogues
---------------------------------------------------------------------------------------------------------------------

---A dialogue option (the engine's `gCInfo`), owned by an NPC.
---@class Info
local Info = {}

---@param name string
---@param value string
---@return boolean ok
function Info:set_property(name, value) end
---@param name string
---@return string|nil
function Info:get_property(name) end

---@param owner string NPC entity name.
---@return boolean ok
function Info:set_owner(owner) end
---@param parent string Name of the parent info.
---@return boolean ok
function Info:set_parent(parent) end
---@param quest string
---@return boolean ok
function Info:set_quest(quest) end
---@param type integer `gEInfoType` value (prefer `set_property("Type", "gEInfoType_...")`).
---@return boolean ok
function Info:set_type(type) end
---@param condition integer `gEInfoCondType` value (prefer `set_property("ConditionType", ...)`).
---@return boolean ok
function Info:set_condition(condition) end
---Permanent infos stay available after they were used.
---@param permanent boolean
---@return boolean ok
function Info:set_permanent(permanent) end
---Marks the info as already given (used) or not.
---@param given boolean
---@return boolean ok
function Info:set_given(given) end

---@return string|nil
function Info:name() end
---@return string|nil
function Info:owner() end
---@return integer|nil
function Info:type() end
---@return integer|nil
function Info:condition() end
---@return boolean|nil
function Info:given() end
---@return boolean
function Info:conditions_fulfilled() end
---@return boolean
function Info:available() end
---@return integer
function Info:command_count() end

---Appends a command by its editor name (`"Say"`, `"Description"`, `"End"`, `"Back"`, `"SetGameEvent"`, `"ClearGameEvent"`,
---`"GiveXP"`, `"Give"`, `"RunQuest"`, `"SucceedQuest"`, `"CloseQuest"`, `"Attack"`, `"MoveTo"`, `"StartEffect"`,
---`"SetRoutine"`, `"DoTrade"`, `"Heal"`, `"FullStop"`, `"InfoWait"`, `"RunInfo"`, `"SaySVM"`, `"SaySound"`, `"ShowSubtitle"`).
---@param name string
---@return integer|nil index 1-based index of the new command, `nil` for an unknown name.
function Info:add_command(name) end
---Appends a `RunScript` command (configure it with `set_script`).
---@return integer|nil index
function Info:add_script_command() end
---Appends an `AddLogText` command (configure it with `set_log`).
---@return integer|nil index
function Info:add_log_command() end
---Appends a command by the engine's `gEInfoCommand` number (see `risen.dialog.command_table`).
---@param type integer
---@return integer|nil index
function Info:add_command_type(type) end

---Configures a `Say` command.
---@param index integer Command index returned by `add_command`.
---@param speaker string `"player"`, `"npc"` (the other party) or an entity name.
---@param listener string
---@param text_key string Localization key (serve its text with `risen.loc.set`).
---@return boolean ok
function Info:set_say(index, speaker, listener, text_key) end
---Configures a `RunScript` command.
---@param index integer
---@param script string Script defined with `risen.script.define`.
---@return boolean ok
function Info:set_script(index, script) end
---Configures an `AddLogText` command.
---@param index integer
---@param quest string
---@param text_key string
---@return boolean ok
function Info:set_log(index, quest, text_key) end

---Runs the info now, as if the player had chosen it. Only works for infos the running conversation already knows.
---@return boolean ok
function Info:run() end

---@alias InfoType "Info"|"Important"|"Comment"|"News"|"Refuse"|"Parent"
---Conditions under which an info is offered (the engine's `gEInfoCondType_*`). The quest-state ones need `quest`, and that quest must exist.
---@alias InfoCondition
---| "General" # always offered (default)
---| "Hello"
---| "End"
---| "Back"
---| "Trade"
---| "Heal"
---| "Join"
---| "Dismiss"
---| "MobJoin"
---| "MobDismiss"
---| "SlaveJoin"
---| "Duel"
---| "Crime"
---| "PoliticalCrime"
---| "FirstWarn"
---| "SecondWarn"
---| "PickPocket"
---| "MasterThief"
---| "Reactivator"
---| "Activator"
---| "Wait"
---| "LongTimeNoSee"
---| "NothingToSay"
---| "Finished"
---| "DoCancel"
---| "Open" # quest state
---| "Running" # quest state
---| "Success" # quest state
---| "Failed" # quest state
---| "Cancelled" # quest state
---| "Won" # quest state
---| "Lost" # quest state
---| "Ready" # quest state
---| "Overtime" # quest state
---| "NotYetFinished" # quest state
---| "Delivery" # quest state
---| "PartDelivery" # quest state

---One line of a dialogue. Exactly one of `text`, `script` or `log`.
---@class DialogLine
---@field speaker? string `"player"`, `"npc"` (default) or an entity name.
---@field listener? string Default `"player"`.
---@field text? string The spoken text. The first line's text is the label of the option.
---@field script? string Name of a script registered with `risen.script.define`.
---@field log? {quest: string, text: string} Adds a quest log entry.

---@class DialogSpec
---@field name string Unique info name.
---@field owner string NPC entity name.
---@field type? InfoType Default `"Info"`.
---@field condition? InfoCondition Default `"General"`.
---@field quest? string Required for quest-state conditions; must exist.
---@field parent? string Parent info (branching).
---@field permanent? boolean Stays available after use.
---@field lines DialogLine[]

---@class RisenDialogApi
local dialogApi = {}

---Creates the info and its command sequence. Validates the whole spec before touching the engine, is idempotent
---(an existing info is returned unchanged) and logs the reason when it returns `nil`.
---@param spec DialogSpec
---@return Info|nil info
---@return string|nil error
function dialogApi.create(spec) end

---Low-level: creates an empty info.
---@param name string
---@param owner string
---@return Info|nil
function dialogApi.new_info(name, owner) end

---@param name string
---@return Info|nil
function dialogApi.get(name) end

---Rebuilds the engine's owner -> infos map (done by `create`).
---@return boolean ok
function dialogApi.rebuild() end

---Whether the NPC has any available top-level info.
---@param npc Entity
---@return boolean
function dialogApi.has_infos(npc) end

---The engine's command names -> `gEInfoCommand` values (version specific).
---@return table<string, integer>
function dialogApi.command_table() end

---Options currently offered by the running conversation.
---@return Info[]
function dialogApi.available() end

---@return boolean
function dialogApi.is_running() end

---Opens the conversation between the player and an NPC.
---@param player Entity
---@param npc Entity
---@return boolean ok
function dialogApi.start(player, npc) end

---------------------------------------------------------------------------------------------------------------------
-- Scripts
---------------------------------------------------------------------------------------------------------------------

---Context passed to a Lua script.
---@class ScriptContext
---@field self Entity|nil The script's `self` entity.
---@field other Entity|nil
---@field arg integer Integer argument.
---@field a integer Raw argument (script-to-script calls).
---@field b integer Raw argument (script-to-script calls).
---@field name string Script name.
local ScriptContext = {}

---Runs the implementation that was registered before `risen.script.override` (0 for scripts made with `define`).
---@return integer
function ScriptContext:call_original() end

---@alias ScriptFunction fun(ctx: ScriptContext): (number|boolean|nil)

---The game's scripts are native functions looked up by name.
---@class RisenScriptApi
local scriptApi = {}

---Registers a new script usable from game data by name (quests, infos). Limit: 1024 Lua scripts.
---@param name string
---@param fn ScriptFunction
function scriptApi.define(name, fn) end

---Replaces an existing script. On a Lua error the original runs.
---@param name string
---@param fn ScriptFunction
function scriptApi.override(name, fn) end

---@param name string
---@return boolean
function scriptApi.exists(name) end

---Calls a script (needs a running game).
---@param name string
---@param self? Entity|nil
---@param other? Entity|nil
---@param arg? integer
---@return integer
function scriptApi.call(name, self, other, arg) end

---------------------------------------------------------------------------------------------------------------------
-- Game variables, world, localization
---------------------------------------------------------------------------------------------------------------------

---Integer flags that dialogue/info scripts read and write; saved by the engine.
---@class RisenGamevarApi
local gamevarApi = {}
---@param name string
---@return integer|nil
function gamevarApi.get(name) end
---@param name string
---@param value integer
---@return boolean ok
function gamevarApi.set(name, value) end

---Session-level actions. `new_game`, `quick_load`, `quick_save` and `auto_save` are requests the engine performs on its
---next processing step: watch `session.start`, `load.after`, `save.after`.
---@class RisenWorldApi
local worldApi = {}
---@return boolean
function worldApi.is_game_running() end
---@return boolean requested
function worldApi.new_game() end
---Loads the last quick save.
---@return boolean requested
function worldApi.quick_load() end
---@return boolean requested
function worldApi.quick_save() end
---@param name string
---@return boolean requested
function worldApi.auto_save(name) end
---@return boolean
function worldApi.is_save_allowed() end

---Localization overlay: the game's own string tables are read-only, so mod texts are served on top. Ids are matched
---case-insensitively.
---@class RisenLocApi
local locApi = {}
---Defines the text for an id (UTF-8).
---@param id string e.g. `"FO_SPASS_Bob"`, `"GO_QuestRunning"`.
---@param text string
---@param language? string Only this language (`"English"`, `"German"`, ...); all languages if omitted.
---@return boolean ok
function locApi.set(id, text, language) end
---Removes an overlay entry.
---@param id string
---@return boolean removed
function locApi.clear(id) end
---The overlay text, else the game's text, else nil.
---@param id string
---@return string|nil
function locApi.get(id) end
---@param id string
---@return boolean
function locApi.exists(id) end
---Active text language, e.g. `"English"`.
---@return string
function locApi.language() end
---Logs every distinct string id the game asks for (find ids to override).
---@param on boolean
function locApi.trace(on) end

---------------------------------------------------------------------------------------------------------------------
-- Persistence, file system, debugging, engine info
---------------------------------------------------------------------------------------------------------------------

---Persistent key/value store of this mod, saved next to every savegame and restored when it is loaded. A new game starts
---empty. Values: booleans, numbers, strings and acyclic tables of those; keys: strings or numbers.
---@class RisenSaveApi
local saveApi = {}
---@param key string|number
---@param default? any Returned when the key is not set.
---@return any
function saveApi.get(key, default) end
---Stores a value; `nil` removes the key. Raises an error for functions, userdata, cycles and non-finite numbers.
---@param key string|number
---@param value boolean|number|string|table|nil
function saveApi.set(key, value) end
---@param key string|number
function saveApi.remove(key) end
---Sorted list of the stored keys.
---@return string[]
function saveApi.keys() end
function saveApi.clear() end

---The engine's virtual file system, including everything inside the packed archives.
---Paths are mount-relative (`ini`, `scripts`, `data/common/...`) or start with `#G3:/`.
---@class RisenVfsApi
local vfsApi = {}
---Physical path a virtual path is mounted at.
---@param path string
---@return string|nil
function vfsApi.physical(path) end
---@param path string
---@return boolean
function vfsApi.exists(path) end
---File names in a directory matching a pattern (`"*"`, `"*.dll"`, `"It_*"`).
---@param dir string
---@param pattern string
---@return string[]|nil
function vfsApi.find(dir, pattern) end
---File names with an extension (without the dot).
---@param dir string
---@param ext string
---@return string[]|nil
function vfsApi.find_ext(dir, ext) end
---Sub-directory names.
---@param dir string
---@return string[]|nil
function vfsApi.dirs(dir) end
---Every file below a directory as `"dir/sub/name"`, recursively.
---@param dir string
---@param pattern? string Default `"*"`.
---@return string[]
function vfsApi.walk(dir, pattern) end
---Mounts a physical directory into the virtual file system (the engine's `MountDirectory`, as used for the entries of
---`mountlist_packed.ini`). Its files show up under `virtual_path`, and a file `<dir>/a/b.txt` is also what `read("a/b.txt")` returns
---(it shadows a packed file with that path until `unmount`). Not usable for entity templates: the engine terminates when it loads a
---loose template (docs/RE.md §17).
---@param virtual_path string e.g. `"mymod"` or an existing virtual directory.
---@param physical_path string Absolute path of a directory (forward slashes are fine).
---@return boolean ok
function vfsApi.mount(virtual_path, physical_path) end
---Removes a mount made with `mount`.
---@param virtual_path string
---@return boolean ok
function vfsApi.unmount(virtual_path) end
---Size in bytes; unreliable for packed files (often 0): read the file instead.
---@param path string
---@return number|nil
function vfsApi.size(path) end
---File contents (binary-safe string), `nil` if it cannot be opened.
---@param path string
---@param max_bytes? number Default 16 MiB.
---@return string|nil
function vfsApi.read(path, max_bytes) end

---Development aids.
---@class RisenDebugApi
local debugApi = {}
---Logs every file the engine opens whose path contains the text (case-insensitive); an empty string stops.
---@param filter string
function debugApi.trace_files(filter) end

---What the layer found in the running game.
---@class RisenEngineApi
local engineApi = {}
---Engine functions that could not be bound: `"symbol (required|optional)"`.
---@return string[]
function engineApi.missing() end
---@return table<string, boolean>
function engineApi.symbols() end
---Per module (`"Game.dll"`, `"Engine.dll"`, ...): PE link time, image size and load address. Include it in bug reports.
---@return table<string, {timestamp: number, size: number, base: string}>
function engineApi.build() end

---------------------------------------------------------------------------------------------------------------------
-- Logging and the risen table
---------------------------------------------------------------------------------------------------------------------

---Writes to `spass.log`. Arguments are `tostring`-ed and joined with spaces.
---@class RisenLogApi
local logApi = {}
---@param ... any
function logApi.trace(...) end
---@param ... any
function logApi.debug(...) end
---@param ... any
function logApi.info(...) end
---@param ... any
function logApi.warn(...) end
---@param ... any
function logApi.error(...) end

---@alias EventHandle integer

---The modding API. Every mod gets its own `risen` view with its mod id baked in.
---@class RisenApi
---@field version string Version of risen-spass.
---@field log RisenLogApi
---@field entity RisenEntityApi
---@field template RisenTemplateApi
---@field item RisenItemApi
---@field npc RisenNpcApi
---@field quest RisenQuestApi
---@field dialog RisenDialogApi
---@field script RisenScriptApi
---@field gamevar RisenGamevarApi
---@field world RisenWorldApi
---@field loc RisenLocApi
---@field save RisenSaveApi
---@field vfs RisenVfsApi
---@field debug RisenDebugApi
---@field engine RisenEngineApi
local risen_ = {}

---Registers an event handler. Higher priority runs first. Handlers that raise are logged and skipped.
---
---Engine events: `session.start(mode)` (`"new"`, `"load"`, `"other"`), `session.stop()`, `world.changed(name)`,
---`save.before(name)`, `save.after(name, ok)`, `load.before(name)` (runs before the engine reads quests and infos),
---`load.after(name, ok)`, `quest.status_changed(quest, status, previous)`, `npc.killed(entity)`, `npc.defeated(entity)`,
---`info.started(info_name, owner)`, `info.finished(info_name, owner)`, `item.picked_up(owner, template, amount)`.
---@param event string
---@param handler function
---@param priority? integer
---@return EventHandle
---@overload fun(event: "mods.loaded", handler: fun(), priority?: integer): EventHandle
---@overload fun(event: "tick", handler: fun(), priority?: integer): EventHandle
---@overload fun(event: "shutdown", handler: fun(), priority?: integer): EventHandle
---@overload fun(event: "session.start", handler: fun(mode: "new"|"load"|"other"), priority?: integer): EventHandle
---@overload fun(event: "session.stop", handler: fun(), priority?: integer): EventHandle
---@overload fun(event: "world.changed", handler: fun(name: string), priority?: integer): EventHandle
---@overload fun(event: "save.before", handler: fun(name: string), priority?: integer): EventHandle
---@overload fun(event: "save.after", handler: fun(name: string, ok: boolean), priority?: integer): EventHandle
---@overload fun(event: "load.before", handler: fun(name: string), priority?: integer): EventHandle
---@overload fun(event: "load.after", handler: fun(name: string, ok: boolean), priority?: integer): EventHandle
---@overload fun(event: "quest.status_changed", handler: fun(quest: string, status: "Open"|"Running"|"Success"|"Failed"|"Obsolete"|"Cancelled"|"Lost"|"Won", previous: string), priority?: integer): EventHandle
---@overload fun(event: "npc.killed", handler: fun(entity: Entity|nil), priority?: integer): EventHandle
---@overload fun(event: "npc.defeated", handler: fun(entity: Entity|nil), priority?: integer): EventHandle
---@overload fun(event: "info.started", handler: fun(info_name: string, owner: string), priority?: integer): EventHandle
---@overload fun(event: "info.finished", handler: fun(info_name: string, owner: string), priority?: integer): EventHandle
---@overload fun(event: "item.picked_up", handler: fun(owner: Entity|nil, template: string, amount: integer), priority?: integer): EventHandle
function risen_.on(event, handler, priority) end

---Removes a handler.
---@param handle EventHandle
function risen_.off(handle) end

---Emits a mod-to-mod event; the arguments are passed to the handlers unchanged.
---@param event string
---@param ... any
function risen_.emit(event, ...) end

---@type RisenApi
risen = risen_

---Information about the running mod.
---@class ModInfo
---@field id string
---@field name string
---@field version string
---@field author string
---@field dir string Directory of the mod (forward slashes).
---@type ModInfo
mod = {}

---Mod-local `require`: `require("a.b")` loads `<mod dir>/a/b.lua` inside the same sandbox.
---@param name string
---@return any
function require(name) end
