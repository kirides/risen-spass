---@meta
-- LuaLS (lua-language-server) definitions for the risen-spass modding API.
-- Add this directory to `workspace.library` (see types/README.md) to get completion, hover docs and type checks.
-- Reference: docs/LUA_API.md. tools/check_luadefs.py verifies that every binding is described here.

---------------------------------------------------------------------------------------------------------------------
-- Entities
---------------------------------------------------------------------------------------------------------------------

---One property of an engine object, as listed by `list_properties`.
---@class PropertyInfo
---@field name string Property name for `get_property`/`set_property`.
---@field type string The engine's type name: `"bool"`, `"long"`, `"float"`, `"bCString"`, `"bTPropertyContainer<enum gEGuild>"` (an enum, set it by value name), ...

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

---Guild number (`gEGuild_*`): see `risen.npc.guilds` (`None` 0, `Don` 1, `Dig` 2, `Grd` 3, `Cit` 4, `Inq` 5, `Mag` 6, `Pir` 7).
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

---Reads any property of any of the entity's property sets by name, in the engine's text format (enums as `"gEGuild_..."`,
---numbers and booleans as text). The set is named by its engine class: `"gCNPC_PS"` (guild, species, attitude lock, ...),
---`"gCInventory_PS"`, `"gCSkills_PS"`, `"gCItem_PS"`, `"gCScriptRoutine_PS"`, ...
---@param set string
---@param name string
---@return string|nil value `nil` if the entity lacks the set or the property.
function Entity:get_property(set, name) end

---Lists the properties of one of the entity's property sets (names and types), so you can see what `get_property`/`set_property` accept.
---@param set string
---@return PropertyInfo[]|nil list `nil` if the entity lacks the set.
function Entity:list_properties(set) end

---Writes a property of a property set by name (see `get_property`). Enum values must be spelled as the engine does
---(`"gEGuild_..."`); an invalid value is ignored by the engine.
---@param set string
---@param name string
---@param value string
---@return boolean ok `false` if the entity lacks the set or the property.
function Entity:set_property(set, name, value) end

---Number of inventory stacks.
---@return integer|nil
function Entity:stack_count() end

---The item that represents inventory stack `index` (0-based). Inventories keep *references to templates* plus an amount, not item
---entities: this returns the template entity itself (so `item_value()`, `item_amount()`, ... show the template's current values, and
---`name()` is the template name). Do not `kill()` it.
---@param index integer
---@return Entity|nil item `nil` for an invalid stack or entity.
function Entity:stack_item(index) end

---The template this entity was created from (the engine's `GetCreator`). Entities that are templates themselves, such as the objects
---returned by `stack_item`, have none.
---@return Template|nil template
---@return string|nil reason
function Entity:template() end

---The label the engine shows for this entity (`gCInteraction_PS:GetPlainFocusName`: the localization id `focus_unique:FO_<entity name>`, else
---`focus:FO_<entity name>`; `""` when neither exists). Shows what a mod's `risen.loc.set("FO_<name>", ...)` / strings file made of it.
---@return string|nil text
---@return string|nil reason
function Entity:focus_name() end

---The item description the engine shows (`gCItem_PS:GetPlainDescription`: localization id `item:ITEMDESC_<entity name>`; `""` or `"-"` when there is none).
---@return string|nil text
---@return string|nil reason `"entity has no gCItem_PS"` for non-items.
function Entity:description() end

---The resource files the entity's visual property sets loaded, as the engine's own `GetDependencies` reports them (`eCMesh_PS`: the compiled mesh,
---`It_Wpn_1H_Sword._xmsh`; `eCAnimation_PS`: the actor file, `Ani_Wolf_Monster_Wolf.xac`; `eCBodyPart_PS`). This is what is really rendered, as opposed to the
---`MeshFileName` property text: `risen.template.create(..., { mesh = ... })` is verified with it. Only entities (spawns) have loaded resources, templates report nothing.
---@return string[]|nil files
---@return string|nil reason
function Entity:dependencies() end

---The equip requirements of this item entity (or a template entity such as `stack_item(i)`), same as `risen.item.requirements`:
---`{ Strength = 10 }`. Only the player is held to them: NPCs equip anything.
---@return table<string, integer>|nil requirements
---@return string|nil reason
function Entity:requirements() end

---Replaces the equip requirements of this item entity only (the template and other entities are untouched), same as `risen.item.set_requirements`.
---@param spec ItemRequirements
---@return boolean|nil ok
---@return string|nil reason
function Entity:set_requirements(spec) end

---The skill modifiers of this item entity (armor protection, potion effects, ring bonuses), same as `risen.item.modifiers`.
---@return ItemModifier[]|nil modifiers
---@return string|nil reason
function Entity:modifiers() end

---Replaces the skill modifiers of this item entity only, same as `risen.item.set_modifiers`.
---@param spec ItemModifiers
---@return boolean|nil ok
---@return string|nil reason
function Entity:set_modifiers(spec) end

---The engine's own equip-condition check for this character: does it meet the requirements of `item` (a template name or an item entity)?
---Same as `risen.item.can_use(self, item)`.
---@param item string|Entity
---@return boolean|nil can
---@return string|nil reason
function Entity:can_use(item) end

---The character's current value of a skill or attribute (`"Strength"`, `"ProtEdge"`, `"HitPoints"`, ... see `risen.balance.skills()`), including
---bonuses of equipped items. Same as `risen.item.skill_total(self, skill)`.
---@param skill string
---@return integer|nil total
---@return integer|string|nil base The base value without item/temporary modifications (or the reason when `total` is `nil`).
function Entity:skill_total(skill) end

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

---Overrides of `risen.template.create` (and one entry of the `new_items` / `new_npcs` / `templates` balancing sections, which adds `base`). An item base accepts
---the keys of `BalanceItem`, an NPC/monster base those of `BalanceNpc`; every other base only `name`, `description`, `mesh`, `icon`, `material` and `properties`.
---@class TemplateOverrides: BalanceItem, BalanceNpc
---@field base? string Only in a balancing section: the template to clone.
---@field name? string|table<string,string> Label shown for the item/NPC (localization id `FO_<name>`): one text for every language or `{ English = "...", German = "..." }`.
---@field description? string|table<string,string> Item description (`ITEMDESC_<name>`), same forms. Items only.
---@field mesh? string Mesh of an item (`eCMesh_PS.MeshFileName`, `"It_Wpn_1H_Club.xmsh"`) or the actor file of an NPC (`eCAnimation_PS.ResourceFilePath`, `"Ani_Boar_Monster_Boar.xac"`). The file has to exist in the game archives or in a mounted asset directory (`risen.assets`), otherwise the request is refused (`check_assets = false` skips the check).
---@field icon? string Inventory icon of an item (`gCItem_PS.IconImage`): the name of a registered GUI bitmap, `"ic_it_1h_club"` / `"IC_It_1H_Club"` or one a mod registered (`risen.assets.load_icons`, `risen.assets.load_bitmap`). Checked like `mesh`.
---@field material? integer Material switch of the mesh/actor (`MaterialSwitch`), a number. New materials come with a new mesh file that names them (`risen.assets`).
---@field check_assets? boolean Default `true`: refuse a `mesh` / `icon` that the engine cannot resolve. `false` uses the names as given.

---@class TemplateCreateOptions
---@field instances? boolean Also write the overrides into live entities of the clone (default `false`).

---Creates a **new template** at runtime: a full independent copy of the item/NPC/monster template `base` (property sets, mesh, icon, skills, inventory)
---with the new unique name `name` and the **deterministic ID of that name** (`risen.template.id_for(name)`), then applies `overrides` (the machinery of
---`risen.balance`: `value`, `requires`, `modifies`, `damage`, `hp_max`, `skills`, `loot`, `properties`, ... plus `name`, `description`, `mesh`, `icon`,
---`material`). Everything is validated first; on any problem nothing is created and `nil, reason` is returned. The clone is a snapshot of the base at creation
---time. Afterwards `Template(name)`, `risen.entity.spawn`, `risen.npc.spawn`, `risen.item.give`, dialogue `Give` commands, `loot` and the balancing sections accept
---the new name. Calling it again for an existing clone of the same base re-applies the overrides (`created = false`).
---
---Savegames store the ID of the template of every inventory stack, so create the template **in every process** before the savegame is read (on the first `tick`,
---as `risen.balance.install` does); stacks of a template that does not exist (yet) stay dormant and resolve as soon as it is created, also after the load.
---Items and NPC templates can be cloned at any time after the first `tick`; the clone lives until the process ends (see `risen.template.remove`).
---```lua
---risen.template.create("My_Sword", "It_1H_Sword", { value = 900, requires = { Strength = 25 }, name = "Flame Tongue", mesh = "It_Wpn_1H_Club.xmsh" })
---```
---@param name string New template name: letters, digits and `_`, not starting with a digit, at most 63 characters (`FO_<name>` is its label).
---@param base string Existing template (`risen.template.list`, docs/TEMPLATES.md). Multi templates are refused.
---@param overrides? TemplateOverrides
---@param opts? TemplateCreateOptions
---@return Template|nil template `nil` on failure.
---@return boolean|string created `true` when the template was created now, `false` when it existed and only the overrides were applied; the reason when `template` is `nil`.
function templateApi.create(name, base, overrides, opts) end

---The low-level part of `create`: clones `base` as `name` (new name, ID derived from the name) without applying any overrides.
---@param name string
---@param base string
---@return Template|nil template
---@return string|nil reason
function templateApi.clone(name, base) end

---The ID a clone called `name` gets (`xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx`): the MD5 of `"spass.template:<name>"`, printed as a GUID. Pure function.
---@param name string
---@return string id
function templateApi.id_for(name) end

---Why `name` cannot be the name of a new template, or `nil` when it can.
---@param name string
---@return string|nil problem
function templateApi.name_problem(name) end

---The templates this process created: `{ [name] = base name }` (removed ones are not listed).
---@return table<string,string>
function templateApi.created() end

---Gives a template created by `risen.template.create` its names back (`Template(name)`, spawn, give and the dialogue commands no longer find it; `create` with the
---same base brings it back with the same ID). Refused while live entities of it exist and for templates this layer did not create. Saved stacks and items that
---were handed out keep working: the engine object and its ID stay registered until the process ends.
---@param name string
---@return boolean|nil ok
---@return string|nil reason
function templateApi.remove(name) end

---Whether a template with that name exists.
---@param name string
---@return boolean
function templateApi.exists(name) end

---Names of all entity templates found in the packed data.
---@param group? string Sub-directory of `data/common/templates` (`"NPC"`, `"Items"`, `"NPC/Harbour"`, ...); all if omitted.
---@return string[]
function templateApi.list(group) end

---Handle of an engine entity template, which can be read and **edited at runtime** (docs/RE.md §22). New spawns, items given to an
---inventory and inventory stacks use the edited values at once; entities that already exist keep their own copy of the property
---values (see `Template:patch_instances` and `risen.template.patch`'s `instances` option). Edits live in memory only: apply them again
---in every game process, on the first `tick` at the earliest (`mods.loaded` is too early: no template resolves yet). The handle stores
---only the name and resolves the engine object on every call. Methods return `nil, reason` on failure.
---@class Template
local Template = {}

---Template name (`"It_1H_Sword"`).
---@return string
function Template:name() end

---Whether the engine already read the template's content from its `.tple` file. Property access loads it on demand.
---@return boolean|nil loaded
---@return string|nil reason
function Template:is_loaded() end

---Name of the template this one is derived from (`GetRefTemplate`), or `nil`. Most game templates have none.
---@return string|nil ref
function Template:ref_template() end

---Class names of the template's property sets, e.g. `{ "eCMesh_PS", "gCItem_PS", ... }` for an item or `{ "gCNPC_PS", "gCSkills_PS", ... }`
---for an NPC. Note that `gCSkills_PS` has no reflected properties.
---@return string[]|nil sets
---@return string|nil reason
function Template:property_sets() end

---The properties of one property set (names and engine types), like `Entity:list_properties`.
---@param set string
---@return PropertyInfo[]|nil list
---@return string|nil reason
function Template:list_properties(set) end

---Reads a property in the engine's text format (same as `Entity:get_property`): `"50"`, `"gEItemCategory_Weapon"`, `"True"`.
---@param set string
---@param name string
---@return string|nil value
---@return string|nil reason
function Template:get_property(set, name) end

---Writes a property of the template (text format as for `Entity:set_property`; unknown sets/properties are reported instead of ignored).
---Affects what is created from the template afterwards; use `patch_instances` or `risen.template.patch` to update existing entities.
---@param set string
---@param name string
---@param value string
---@return boolean|nil ok
---@return string|nil reason
function Template:set_property(set, name, value) end

---Copies **all** property sets of the template into every live entity created from it (`eCTemplateEntity::PatchEntity`). Destructive:
---per-instance values that live in those sets are reset to the template's (hit points, `gCNPC_PS.Group`, item `Amount`, ...); inventory
---contents are kept. Prefer `risen.template.patch(name, sets, { instances = true })`, which writes only the properties you list.
---@return integer|nil patched Number of entities patched.
---@return string|nil reason
function Template:patch_instances() end

---Same as `patch_instances` for one entity (it should have been created from this template).
---@param entity Entity
---@return boolean|nil ok
---@return string|nil reason
function Template:patch_entity(entity) end

---The live entities created from this template (world entities, spawned ones, NPCs; not inventory stacks), found by walking the scene.
---@param limit? integer Default 200.
---@return Entity[]|nil list
---@return string|nil reason Only when the engine's scene admin is unavailable; without a world the list is simply empty.
function Template:instances(limit) end

---Number of live entities created from this template.
---@return integer|nil count
---@return string|nil reason
function Template:instance_count() end

---The template's ID as `xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx` (a GUID). Savegames store the ID of the template of every item stack. For a clone made by
---`risen.template.create` it is `risen.template.id_for(name)`.
---@return string|nil id
---@return string|nil reason
function Template:id() end

---Engine bookkeeping for diagnostics: `{ id, created (a clone of this layer), base (its base's name), flags, has_parent, has_children, ref_id, property_sets,
---own_name, parent = { name, id, flags, ... } }`. Every template is a pair: a hidden root `"<name>.tple"` and the template entity `"<name>"` below it.
---@return table|nil info
---@return string|nil reason
function Template:info() end

---The label the engine shows for items/NPCs of this template (localization id `FO_<name>`; `""` when there is none).
---@return string|nil text
---@return string|nil reason
function Template:focus_name() end

---The item description the engine shows (localization id `ITEMDESC_<name>`; `""` or `"-"` when there is none). Items only.
---@return string|nil text
---@return string|nil reason
function Template:description() end

---Handle of an entity template by name.
---@param name string
---@return Template|nil template
---@return string|nil reason `"unknown template '...'"`, or the engine is not ready (before the first `tick`).
function templateApi.get(name) end

---`{ [property set] = { [property] = value } }`: `value` may be a number, boolean or string (text format of the engine).
---@alias TemplateEdits table<string, table<string, number|boolean|string>>

---@class TemplatePatchOptions
---@field instances? boolean Also write the listed properties into every live entity created from the template (default `false`).
---@field dry_run? boolean Only validate: returns `true, <number of values that would be written>` and writes nothing (default `false`).

---Edits several template properties declaratively. Everything is validated first (the template, every property set, every property,
---the value types): on any problem nothing is written and `nil, reason` is returned (all problems, separated by `;`). With
---`instances = true` the same values are also written into the existing entities (only the listed properties, so hit points, amounts
---etc. of those entities are preserved).
---```lua
---risen.template.patch("It_1H_Sword", { gCItem_PS = { GoldValue = 100 } }, { instances = true })
---```
---@param name string
---@param sets TemplateEdits
---@param opts? TemplatePatchOptions
---@return boolean|nil ok `true` on success.
---@return integer|string applied Number of values written (or the reason when `ok` is `nil`).
---@return integer|nil touched With `instances = true`: number of entities that received at least one value.
function templateApi.patch(name, sets, opts) end

---Counts the live entities per template in one pass over the scene (`{ It_Gold = 107, Wolf = 97, ... }`) plus the total number of entities
---visited (a fresh world holds ~50 000; an empty table and 0 at the main menu). Entities without a template are only part of the total.
---@return table<string, integer>|nil counts
---@return integer|string total Total entity count (or the reason when `counts` is `nil`).
function templateApi.census() end

---@class RisenItemApi
local itemApi = {}

---Creates items in an entity's inventory.
---@param owner Entity
---@param template string
---@param count? integer Default 1.
---@param stack_type? integer `gEStackType` (0 default).
---@return integer stack Index of the inventory stack the items went to, `-1` on failure.
function itemApi.give(owner, template, count, stack_type) end

---Removes up to `count` items of a template from an inventory (unequipped stacks).
---@param owner Entity
---@param template string
---@param count? integer Default 1.
---@return integer removed The amount that was really removed.
function itemApi.take(owner, template, count) end

---Amount of a template in an inventory.
---@param owner Entity
---@param template string
---@return integer
function itemApi.count(owner, template) end

---Gold in an inventory.
---@param owner Entity
---@return number
function itemApi.money(owner) end

-- Item balancing (docs/RE.md section 23). An item's numbers: `gCItem_PS.GoldValue`, weapon damage `gCDamage_PS.DamageAmount`, the equip
-- condition `gCItem_PS.RequiredSkills` and the effects `gCItem_PS.ModifySkills` (armor protection, potion healing, ring bonuses) -- the
-- two arrays are handled by the functions below, everything else by `risen.template.patch`. See docs/ITEM_PROPERTIES.md.

---Friendly skill name (`risen.balance.skills()`), the engine suffix (`"Stat_STR"`) or the full enum name (`"gESkill_Stat_STR"`).
---@alias SkillName string

---`{ Strength = 15, Dexterity = 10 }`, or a list `{ { skill = "Strength", amount = 15 } }`.
---@alias ItemRequirements table<SkillName, integer>|{ skill: SkillName, amount: integer }[]

---One skill modifier of an item. `mode`: `"add"` (adds `amount`; the engine's `gESkillModifier_AddValue`), `"set"` (sets the value), `"max"`
---(sets it to the maximum), `"percent"` (adds a percentage of the maximum).
---@class ItemModifier
---@field skill SkillName
---@field amount integer
---@field mode? "add"|"set"|"max"|"percent" Default `"add"`.

---`{ ProtEdge = 35, ProtBlunt = 25 }` (all `"add"`), values may be `{ amount = 5, mode = "set" }`, or a list of `ItemModifier`.
---@alias ItemModifiers table<SkillName, integer|{ amount: integer, mode?: string }>|ItemModifier[]

---@class ItemWriteOptions
---@field instances? boolean Also write into every live entity created from the template (default `false`; `risen.balance.apply` defaults to `true`).

---@alias ItemTarget string|Template|Entity Template name, `Template` handle or item entity (a template entity from `stack_item` works too).

---The equip requirements of an item (`gCItem_PS.RequiredSkills`): `{ Strength = 10 }`. Only the **player** is held to them (the engine's
---`CanEquipHasRequieredStats` lets NPCs equip anything).
---@param item ItemTarget
---@return table<string, integer>|nil requirements
---@return string|nil reason
function itemApi.requirements(item) end

---Replaces the whole requirement list (`{}` removes every requirement). Validated before anything is written: unknown skill names are
---reported together with the list of valid ones. Writing a template affects new spawns and inventory stacks at once; `instances = true`
---also rewrites the live entities of the template.
---@param item ItemTarget
---@param spec ItemRequirements
---@param opts? ItemWriteOptions
---@return boolean|nil ok
---@return integer|string|nil touched Number of live entities written (or the reason when `ok` is `nil`).
function itemApi.set_requirements(item, spec, opts) end

---The skill modifiers of an item (`gCItem_PS.ModifySkills`). Armor: the six `Prot*` skills. Potions/food: `HitPoints`/`ManaPoints` (heal on
---consume), `MaxHitPoints`/`MaxManaPoints`, `Strength`, `Experience`. Rings/amulets: attributes or protection while worn.
---@param item ItemTarget
---@return ItemModifier[]|nil modifiers
---@return string|nil reason
function itemApi.modifiers(item) end

---Replaces the whole modifier list. Worn items apply their modifiers when equipped and subtract the *current* list when unequipped: change
---modifiers before anything is worn (first `tick`), or characters that already wear the item drift by the difference (docs/RE.md section 23).
---@param item ItemTarget
---@param spec ItemModifiers
---@param opts? ItemWriteOptions
---@return boolean|nil ok
---@return integer|string|nil touched Number of live entities written (or the reason when `ok` is `nil`).
function itemApi.set_modifiers(item, spec, opts) end

---Raw array access (engine enum names): `{ { skill = "gESkill_Stat_STR", amount = 10, modifier = "gESkillModifier_AddValue" } }`. `kind` is
---`"required"` (RequiredSkills) or `"modifiers"` (ModifySkills); `modifier` is only present for `"modifiers"`.
---@param target string|Entity Template name or item entity.
---@param kind "required"|"modifiers"
---@return { skill: string, amount: integer, modifier?: string }[]|nil entries
---@return string|nil reason
function itemApi.skill_array(target, kind) end

---Raw write of the whole array (engine enum names; `modifier` defaults to `"gESkillModifier_AddValue"`). Names are checked through the engine's
---own enum parser before the item is touched.
---@param target string|Entity Template name or item entity.
---@param kind "required"|"modifiers"
---@param entries { skill: string, amount: integer, modifier?: string }[]
---@return boolean|nil ok
---@return string|nil reason
function itemApi.set_skill_array(target, kind, entries) end

---The engine's own equip check (`gCSkills_PS::HasRequiredSkills`, the first step of `CanEquipHasRequieredStats`): does `who` meet the item's
---requirements right now? Unlike the equip scripts it also answers for NPCs.
---@param who Entity A character.
---@param item string|Entity Template name or item entity.
---@return boolean|nil can
---@return string|nil reason
function itemApi.can_use(who, item) end

---What the engine does when an item is used up (`gCInventory_PS::ConsumeItems`): applies the item's modifiers to the owner (for `AddValue` as a
---permanent change of the base value: `HitPoints +75` heals, `MaxHitPoints +10` raises the maximum) and removes `count` items from the first
---unequipped stack of that template.
---@param owner Entity
---@param template string
---@param count? integer Default 1.
---@return boolean|nil ok
---@return string|nil reason
function itemApi.consume(owner, template, count) end

---A character's current value of a skill (`"Strength"`, `"ProtEdge"`, ...; names as in `risen.balance.skills()`).
---@param who Entity
---@param skill SkillName
---@return integer|nil total Includes bonuses of worn items.
---@return integer|string|nil base The base value (or the reason when `total` is `nil`).
function itemApi.skill_total(who, skill) end

---Same with an engine enum name (`"gESkill_Stat_STR"`).
---@param who Entity
---@param skill string
---@return integer|nil total
---@return integer|string|nil base
function itemApi.skill_total_raw(who, skill) end

---The numeric `gESkill` value of an engine enum name (diagnostics).
---@param name string
---@return integer|nil
function itemApi.skill_id(name) end

---@class RisenBalanceApi
local balanceApi = {}

---One entry of `items` in a balancing spec. Every key is optional.
---@class BalanceItem
---@field value? integer `gCItem_PS.GoldValue`.
---@field damage? integer `gCDamage_PS.DamageAmount` (weapons; melee damage = multiplier * (DamageAmount + the attacker's Strength)).
---@field requires? ItemRequirements Replaces the equip requirements.
---@field modifies? ItemModifiers Replaces the skill modifiers.
---@field use_type? string `gCItem_PS.UseType` (`"gEItemUseType_1H"`, ...).
---@field category? string `gCItem_PS.Category` (`"gEItemCategory_Weapon"`, ...).
---@field mana_cost? integer `gCCastInfo_PS.ManaCost` (runes and scrolls).
---@field properties? TemplateEdits Any other property: `{ gCDamage_PS = { DamageMultiplier = 1.2 } }`; validated like `risen.template.patch`.

---A balancing spec. `items` is `{ [template name] = BalanceItem }`, `npcs` is `{ [template name] = BalanceNpc }`; `npc_groups` and `item_groups` apply relative or
---absolute settings to every template a selector matches; other sections are registered with `risen.balance.register_section`.
---@class BalanceSpec
---@field items? table<string, BalanceItem>
---@field npcs? table<string, BalanceNpc> NPC and monster templates (see `BalanceNpc`, docs/NPC_PROPERTIES.md).
---@field npc_groups? BalanceNpcGroup[] `{ { match = "^Wolf", hp_mult = 1.5 }, { group = "Monster/Kapitel 2", level_add = 2 } }`
---@field item_groups? BalanceItemGroup[] `{ { group = "Items/Potions", value_mult = 0.5 } }`
---@field new_items? table<string, TemplateOverrides> New item templates: `{ My_Sword = { base = "It_1H_Sword", value = 900, name = "Flame Tongue" } }` (see `risen.template.create`).
---@field new_npcs? table<string, TemplateOverrides> New NPC/monster templates: `{ My_Wolf = { base = "Wolf", hp_max = 500, name = "Dire Wolf" } }`; `loot` may name items of `new_items`.
---@field templates? table<string, TemplateOverrides> Like `new_items` and `new_npcs` for either kind (and other templates: only `name`, `mesh`, `properties`, ...).

---@class BalanceOptions
---@field instances? boolean Also write the values into the live entities of each template (default `true`).
---@field dry_run? boolean Only validate and report what would be applied (default `false`).

---@class BalanceReport
---@field ok boolean `false` when an entry could not be written (see `failed`).
---@field applied string[] One label per applied entry (`"It_1H_Sword (damage, requires, value)"`).
---@field failed string[] `"label: reason"` for entries the engine refused.
---@field values integer Number of entries applied.
---@field instances integer Number of live entities written.
---@field dry_run boolean

---Applies a whole balancing spec. Everything in every section is validated first (templates, setting names, skill names, amounts, property sets
---and properties); on any problem nothing is written and `nil, reason, problems` is returned (unknown names come with the list of valid ones).
---Edits are runtime-only: call it on the first `tick` (templates) and on every `session.start` (live entities) -- or use `risen.balance.install`.
---```lua
---risen.balance.apply({ items = { It_1H_Sword = { value = 120, requires = { Strength = 15 } } } })
---```
---@param spec BalanceSpec
---@param opts? BalanceOptions
---@return BalanceReport|nil report
---@return string|nil reason
---@return string[]|nil problems
function balanceApi.apply(spec, opts) end

---Applies `spec` for you at the right moments and logs a summary (`balance: first tick: applied 6 of 6 entries`): on the first `tick` into the
---templates, and on every `session.start` again including the live entities (`instances = false` in `opts` to skip the latter). Idempotent
---per spec table. Problems are logged as errors. Per mod: handlers are registered for the calling mod.
---@param spec BalanceSpec
---`opts.on_applied(when, report)` is called after each application with `when` = `"first tick"`, `"session.start (new)"` or `"session.start (load)"` and the
---`BalanceReport` (a test scenario asserts on it, see `mods/balance_items`).
---@param opts? { instances?: boolean, on_applied?: fun(when: string, report: BalanceReport) }
---@return boolean installed `false` when this spec table was installed before.
function balanceApi.install(spec, opts) end

---Registers a handler for another section of the spec (later milestones add `npcs`, `spells`). `handler(data, opts)` returns
---`jobs, problems`: validated jobs `{ label = string, run = function(opts) -> ok, err, touched end }` and a list of problem strings.
---@param name string
---@param handler fun(data: any, opts: BalanceOptions): table[], string[]
function balanceApi.register_section(name, handler) end

---Names of the registered sections (`{ "item_groups", "items", "new_items", "new_npcs", "npc_groups", "npcs", "templates" }`).
---@return string[]
function balanceApi.section_names() end

---One-call relative balancing of NPCs (`risen.balance.apply` with an `npc_groups` entry). Factors: `hp`, `mana`, `level`, `xp` (reward), `damage`
---(Strength and the NPC's own damage), `skills = { Strength = 1.2 }`. `filter` is a template group (`"Monster"`, `"NPC/Harbour"`, `"Monster/Kapitel 2"`) or else a Lua
---pattern on the name; `group`, `match`, `exclude`, `names` work as in `BalanceNpcGroup`. Factors are relative to the shipped values: calling it twice does not compound.
---`instances` (default `true`) also writes the live NPCs.
---```lua
---risen.balance.scale_npcs{ filter = "Monster", hp = 1.5, damage = 1.2 }
---```
---@param opts { filter?: string, group?: string, match?: string, exclude?: string, names?: string[], hp?: number, mana?: number, level?: number, xp?: number, damage?: number, skills?: table<SkillName, number>, instances?: boolean }
---@param apply_opts? BalanceOptions
---@return BalanceReport|nil report
---@return string|nil reason
---@return string[]|nil problems
function balanceApi.scale_npcs(opts, apply_opts) end

---The valid skill names for requirements and modifiers: `Strength`, `Dexterity`, `Intelligence`, `MaxHitPoints`, `MaxManaPoints`, `LearnPoints`,
---`Experience`, `Level`, `HitPoints`, `ManaPoints` (current values: potions heal), `Axe`, `Sword`, `Staff`, `Bow`, `CrossBow`, `ProtEdge`, `ProtBlunt`,
---`ProtPoint`, `ProtFire`, `ProtIce`, `ProtMagic`, `Missile`, `Frost`, `Fireball`, `Circle`, `Scribe`, `Trophy`, `Acrobat`, `Pickpocket`, `Lockpick`,
---`Sneak`, `Mining`, `Smith`, `Alchemy`.
---@return string[]
function balanceApi.skills() end

---The valid modifier modes: `add`, `set`, `max`, `percent`.
---@return string[]
function balanceApi.modes() end

---@class NpcSpawnOptions
---@field name? string Unique entity name; also the key of its label (`FO_<name>`).
---@field level? integer
---@field hp_max? integer
---@field hp? integer Defaults to `hp_max` when that is given.
---@field mana? integer
---@field guild? integer|string Guild number or name (`"Pir"`, `"Cit"`, ... see `risen.npc.guilds`).
---@field peaceful? boolean Citizen guild unless `guild` is given: pirates, guards and monsters leave the NPC alone (a shopkeeper, quest giver).
---@field group? string
---@field items? {[1]: string, [2]?: integer}[] `{ {"It_1H_Machete", 1}, ... }`
---@field equip? boolean Equip the items.
---@field state? string Initial AI state (the AI may switch immediately).

---@class RisenNpcApi
local npcApi = {}

---Guild numbers by name: `None` 0, `Don` 1 (Don's camp), `Dig` 2 (diggers), `Grd` 3 (guards), `Cit` 4 (citizens), `Inq` 5 (Inquisition),
---`Mag` 6 (mages), `Pir` 7 (pirates/bandits). Pirates attack Don, Grd, Inq and Mag, but not Cit, Dig or None.
---@type table<string, integer>
npcApi.guilds = {}

---Spawns a template and configures it in one call.
---@param template string
---@param x number
---@param y number
---@param z number
---@param opts? NpcSpawnOptions
---@return Entity|nil
function npcApi.spawn(template, x, y, z, opts) end

---------------------------------------------------------------------------------------------------------------------
-- NPC and monster balancing (docs/RE.md section 24, docs/NPC_PROPERTIES.md)
---------------------------------------------------------------------------------------------------------------------

---Settings of one NPC or monster template (`risen.balance` section `npcs`, `risen.npc.set_stats`). Every key is optional. **Numbers are base values**
---(what the template stores); relative keys (`*_mult`, `*_add`) are computed from the template's *shipped* values and written as absolutes, so applying
---a spec again (first tick, every `session.start`, on live NPCs that were saved with the first result) never compounds. An absolute and a relative key of
---the same quantity exclude each other.
---@class BalanceNpc
---@field hp_max? integer Maximum hit points (a template/NPC at full health stays at full health; a damaged NPC keeps its hit points, clamped to the new maximum).
---@field hp? integer Starting/current hit points (`<= hp_max`). Written to templates; to live NPCs only when they are at full health (a single entity named by the caller: always).
---@field hp_mult? number Factor on the shipped maximum hit points.
---@field mana_max? integer
---@field mana? integer
---@field mana_mult? number
---@field level? integer Level (1 or more). The level only drives the experience reward of a kill.
---@field level_mult? number
---@field level_add? integer
---@field xp? integer Experience reward of a kill, set through the level: the engine pays `10 * level`, at least 50 (exactly `10 * level` for the species Chicken and Nautilus).
---@field xp_mult? number Same as `level_mult` (the reward is proportional to the level).
---@field skills? table<SkillName, integer> Base values: attributes (`Strength` 0..200, `Dexterity`, `Intelligence`), protection (`ProtEdge` ...), combat skills (`Sword`, `Axe`, `Staff`, `Bow`, `CrossBow` 0..10), magic. Out-of-range absolutes are errors.
---@field skills_mult? table<SkillName, number> Factors on the shipped values (clamped to the skill's range, noted in the report).
---@field skills_add? table<SkillName, integer> Added to the shipped values.
---@field damage_mult? number Scales the NPC-side terms of the melee damage formula: `Strength` and, if the NPC has one, its own `gCDamage_PS.DamageAmount`. Animals and monsters hit with fist items of damage 0 (`It_Fist_Monster`), so their bite equals Strength; for armed NPCs scale the weapon through `items`.
---@field damage? integer `gCDamage_PS.DamageAmount` of the NPC itself (only special monsters such as Titan and Inferno use it).
---@field damage_type? string `gCDamage_PS.DamageType`: `"Edge"`, `"Blunt"`, `"Point"`, `"Fire"`, `"Ice"`, `"Magic"`, `"Physics"`, `"None"`.
---@field guild? integer|string `"None"`, `"Don"`, `"Dig"`, `"Grd"`, `"Cit"`, `"Inq"`, `"Mag"`, `"Pir"` (or the number).
---@field species? string `gCNPC_PS.Species` (`"Human"`, `"Wolf"`, `"Lizard"`, `"Skeleton"`, ...).
---@field attitude? string `gCNPC_PS.AttitudeLock`: `"None"`, `"Neutral"`, `"Friendly"`, `"Angry"`, `"Hostile"`.
---@field bravery? string `gCNPC_PS.BraveryOverride`: `"None"`, `"Brave"`, `"Coward"`.
---@field damage_calc? string `gCNPC_PS.DamageCalculationType`: `"Normal"`, `"Monster"`, `"Immortal"` (damage 0 per the decompiled scripts, untested in a fight).
---@field combat_species? string `gCCombatSystem_PS.CombatSpecies` (the move set and fist items, see `risen.npc.combat_species`).
---@field ai_group? string `gCNPC_PS.Group` (the free-form group name AI uses to find allies).
---@field loot? table<string, integer> Items the template carries: `{ It_Apple = 3 }` is a *target amount* (idempotent; 0 removes). Reaches NPCs spawned afterwards, not existing ones.
---@field properties? TemplateEdits Any other property: `{ gCNPC_PS = { RoleDescription = "x" } }`; validated like `risen.template.patch`.

---Selects NPC templates for `npc_groups` (and items for `item_groups`). Without `group`, `match`, `exclude` and `names` it selects every template below
---`NPC` and `Monster` (never `PC_Hero`, the player's template, unless named in `names`; templates that are no NPCs are skipped).
---@class BalanceGroupSelector
---@field group? string A template group path (`"Monster"`, `"Monster/Kapitel 2"`, `"NPC/Harbour"`, `"Items/Potions"`; see docs/TEMPLATES.md).
---@field match? string Lua pattern on the template name (`"^Wolf"`).
---@field exclude? string Lua pattern; matching names are dropped.
---@field names? string[] Exact template names (added whatever the patterns say).

---@class BalanceNpcGroup : BalanceGroupSelector, BalanceNpc

---@class BalanceItemGroup : BalanceGroupSelector
---@field value_mult? number Factor on the shipped `gCItem_PS.GoldValue`.
---@field damage_mult? number Factor on the shipped `gCDamage_PS.DamageAmount` (items that have one).

---The numbers of an NPC: `risen.npc.stats`.
---@class NpcStats
---@field template? string The template (name) the NPC/template belongs to.
---@field base boolean Whether `base` values were requested (no worn-item bonuses).
---@field hp integer Current hit points.
---@field hp_max integer
---@field mana integer
---@field mana_max integer
---@field level integer
---@field experience integer The experience the character itself holds (`Stat_XP`).
---@field xp_reward integer What killing it gives: `10 * level`, at least 50 (exactly `10 * level` for the species Chicken and Nautilus).
---@field skills table<string, integer> `Strength`, `Dexterity`, `Intelligence`, `ProtEdge` ... (names of `risen.balance.skills()` without hit/mana/level/experience).
---@field guild string `"Pir"`, `"Cit"`, ...
---@field species string
---@field bravery string
---@field attitude string
---@field damage_calc string
---@field ai_group string
---@field damage { amount: integer, type: string } The NPC's own `gCDamage_PS`.
---@field combat { species?: string, right_fist?: string, left_fist?: string } The combat species and the fist item templates it hits with (their `gCDamage_PS.DamageAmount` is added to Strength).
---@field loot? { item: string, amount: integer }[] Templates: the starting inventory.
---@field stacks? integer Entities: the number of inventory stacks.

---@class NpcStatsOptions
---@field base? boolean Report base values instead of totals (the numbers `set_stats` writes).

---@class NpcSetStatsOptions
---@field instances? boolean With a template target: also write into every live NPC made from it (default `false`).
---@field dry_run? boolean Only validate.

---@class NpcSetStatsReport
---@field template boolean `true` when a template was written.
---@field written integer Values written into the template or entity.
---@field instances integer Live NPCs written.
---@field notes string[] Clamped values and other remarks.
---@field dry_run boolean

---Everything readable about an NPC or monster: hit points, mana, level, the experience a kill gives, attributes, protection and combat skills, guild,
---species, bravery, damage, combat species and fist items, inventory/loot. `target` is a template name, a `Template` handle or an NPC entity.
---(hit points, level, attributes and skills are not reflected properties: the engine's `gCSkills_PS` storage is read natively.)
---@param target string|Template|Entity
---@param opts? NpcStatsOptions
---@return NpcStats|nil stats
---@return string|nil reason
function npcApi.stats(target, opts) end

---Writes settings (see `BalanceNpc`) into an NPC template (every future spawn gets them; `instances = true` also the live NPCs of that template: only the
---listed values, damaged NPCs keep their hit points) or into one NPC entity (relative keys relative to its own values; explicit `hp` always written).
---Validated first: on any problem nothing is written. Template edits are runtime-only (see `risen.balance.install`); values written into entities are saved with the game.
---```lua
---risen.npc.set_stats("Wolf", { hp_max = 300, xp = 120, skills = { Strength = 60 } }, { instances = true })
---```
---@param target string|Template|Entity
---@param spec BalanceNpc
---@param opts? NpcSetStatsOptions
---@return NpcSetStatsReport|nil report
---@return string|nil reason
function npcApi.set_stats(target, spec, opts) end

---Low level: the stored base value, modification (worn items/effects), total (clamped), minimum and maximum of one skill (`gESkill` 0..33, RE.md section 23).
---`max <= 0` means unlimited. `target` is a template name (its `gCSkills_PS` storage) or an entity.
---@param target string|Entity
---@param skill integer
---@return integer|nil base
---@return integer|string|nil modification The reason when `base` is `nil`.
---@return integer|nil total
---@return integer|nil min
---@return integer|nil max
function npcApi.skill_raw(target, skill) end

---Low level: writes the BASE value of a skill. A template name writes the template's storage (clamped to the skill's range); an entity goes through the engine's
---`gCSkills_PS::SetSkillBaseValue` (range, hit/mana point coupling, change callback). Returns the stored base and the resulting total.
---@param target string|Entity
---@param skill integer
---@param value number
---@return integer|nil base
---@return integer|string|nil total The reason when `base` is `nil`.
function npcApi.set_skill_base(target, skill, value) end

---The properties of a combat species (`gCCombatAdmin`): attack/strafe distances, animations and the fist items (`RightFistItem`, `LeftFistItem`) monsters hit with.
---@param name string `"Human"`, `"Wolf"`, `"Boar"`, `"Lizard"`, `"Ogre"`, `"Goblin"`, ...
---@return { name: string, type: string, value: string }[]|nil properties
---@return string|nil reason
function npcApi.combat_species(name) end

---Edits one property of a combat species (shared by every NPC that uses it).
---@param name string
---@param property string
---@param value string
---@return boolean|nil ok
---@return string|nil reason
function npcApi.set_combat_species_property(name, property, value) end

---The inventory of an NPC template (what each new spawn starts with): `{ { item = "It_Apple", amount = 3 } }`.
---@param name string
---@return { item: string, amount: integer }[]|nil stacks
---@return string|nil reason
function npcApi.template_inventory(name) end

---Puts `count` items (default 1) into the inventory of an NPC template: NPCs spawned afterwards carry them. (`loot` in a spec is the idempotent form.)
---@param name string
---@param item string
---@param count? integer
---@return boolean|nil ok
---@return string|nil reason
function npcApi.template_give(name, item, count) end

---Removes up to `count` items (default all) of a template from an NPC template's inventory.
---@param name string
---@param item string
---@param count? integer
---@return integer|nil removed
---@return string|nil reason
function npcApi.template_take(name, item, count) end

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
---Lists the quest's properties (names and types).
---@return PropertyInfo[]
function Quest:list_properties() end

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

---The localization key of every log entry (development aid; an empty string means the entry has no text).
---@return string[]
function Quest:log_keys() end

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
---Lists the info's properties (names and types).
---@return PropertyInfo[]
function Info:list_properties() end

---Lists the parameters of a command of the info (names and types), e.g. what a `GiveXP` or `SetGameEvent` command takes.
---@param index integer Command index returned by `add_command`.
---@return PropertyInfo[]|nil
function Info:list_command_properties(index) end
---Reads a parameter of a command by name (enums as value names, numbers/bools as text).
---@param index integer
---@param name string
---@return string|nil
function Info:get_command_property(index, name) end
---Sets a parameter of a command by name, e.g. `info:set_command_property(i, "Amount", "50")`.
---@param index integer
---@param name string
---@param value string
---@return boolean ok
function Info:set_command_property(index, name, value) end

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

---The engine's `gEInfoType` (numbers: Refuse 0, Important 1, News 2, Info 3, Parent 4, Comment 5). `"Info"` is a normal menu option (and the type of child
---options); `"Comment"` infos are not offered as options (the NPC says them unprompted).
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

---Configures the localized text of a command (`"Say"`, `"SaySVM"`, `"PickPocket"`, `"Description"`, `"ShowSubtitle"`).
---@param index integer Command index returned by `add_command`.
---@param command string The command's name, as given to `add_command`.
---@param text_key string Localization key (serve its text with `risen.loc.set`).
---@return boolean ok
function Info:set_command_text(index, command, text_key) end

---One line of a dialogue. One of: a spoken `text`; a `script`; a `log` entry; or an engine `command` with its parameters.
---@class DialogLine
---@field speaker? string `"player"`, `"npc"` (default) or an entity name.
---@field listener? string Default `"player"`.
---@field text? string The spoken text. The first line's text is the label of the option.
---@field script? string Name of a script registered with `risen.script.define`.
---@field log? {quest: string, text: string} Adds a quest log entry.
---@field command? string Any engine dialogue command by name (`"Give"`, `"GiveXP"`, `"SetGameEvent"`, `"RunQuest"`, `"MoveTo"`, `"SetTradeEnabled"`, ...).
---Its parameters are extra fields named as in docs/PROPERTIES.md, e.g. `{ command = "Give", Entity1 = "npc", Entity2 = "player", Item = "It_Apple", Amount = 3 }`
---or `{ command = "GiveXP", Amount = 50 }`; `text` sets the text of commands that have one (`ShowSubtitle`, `Description`, ...). Unknown commands or
---parameters make `risen.dialog.create` return `nil, "reason"`. Entity parameters accept `"player"`, `"npc"` or an entity name.

---@class DialogSpec
---@field name string Unique info name.
---@field owner string NPC entity name.
---@field type? InfoType Default `"Info"`.
---@field condition? InfoCondition Default `"General"`.
---@field quest? string Required for quest-state conditions; must exist.
---@field parent? string Name of the parent option: this option becomes a child shown in the sub-menu after the parent ran (branching conversations, `mods/dialogbranch`).
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
---Whether the variable exists. The dialogue command `SetGameEvent` creates the variable of that name (value 0).
---@param name string
---@return boolean
function gamevarApi.exists(name) end
---Removes the variable (what `ClearGameEvent` does).
---@param name string
---@return boolean removed
function gamevarApi.remove(name) end

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
---Defines the text for an id (UTF-8). An id set without table prefix (`"FO_SPASS_Bob"`) also answers the prefixed id the engine asks for (`"focus:FO_SPASS_Bob"`).
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
---Loads a string file that lives inside the mod directory: UTF-8 CSV, one `id;text` per line (`,` also works, fields may be
---quoted, `#` starts a comment line, a first row `id;text` is skipped). `strings/all.csv` and `strings/<Language>.csv`
---(e.g. `strings/German.csv`) of every mod are loaded automatically before `main.lua`; use this for other files.
---@param path string Relative to the mod directory (no `..`, no absolute paths).
---@param language? string Only for this language; all languages if omitted.
---@return integer|nil count Number of strings loaded, `nil` if the file does not exist.
function locApi.load_file(path, language) end
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
---The virtual path the engine's **name cache** resolves a bare file name to (`CFXVirtualFileSystem::LookupCachedVirtualPath`): resources are found this way, by the
---case-insensitive name of the compiled file, whatever directory or `.pak` it is in. `lookup("It_Wpn_1H_Club._xmsh")` is `"#G3:/data/common/meshes/Items_Weapons_Swords_01/It_Wpn_1H_Club._xmsh"`;
---paths and raw names (`.xmsh`) are not keys. See `risen.assets.info` for names as they appear in properties.
---@param file_name string
---@return string|nil
function vfsApi.lookup(file_name) end
---Like `lookup`, but the path on disk: inside `<...>.pak\...` for packed files, the loose file for mounted directories.
---@param file_name string
---@return string|nil
function vfsApi.lookup_physical(file_name) end
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
---`mountlist_packed.ini`). Its files show up under `virtual_path` and in the name cache that resources are looked up in (`lookup`), where a loose file
---shadows a packed file of the same name. Not usable for entity templates: the engine terminates when it loads a loose template (docs/RE.md §17).
---Mods should prefer `risen.assets.mount` / the mod's `assets` directory (docs/RE.md §26).
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

---Loose resource files: new meshes, materials, textures, actors and icons (docs/RE.md §26, docs/LUA_API.md "New meshes, textures and icons").
---
---The engine turns a raw name (`X.xmsh`, `X.tga`, `X.xac`) into the compiled file name (`X._xmsh`, `X._ximg`, `X._xmac`) and finds it by that bare name, case
---insensitive, in a cache of everything mounted. A mod's `assets` directory (`mod.toml` key `assets`, default `assets`) and the directory `risen.assets.write`
---fills (`<Risen>\bin\spass\generated\<mod>`) are mounted at startup; `mount` adds a directory later. A loose file shadows a shipped file of the same name
---(for resources that are not loaded yet). Item `mesh`/`icon` and NPC `mesh` (actor) names of `risen.template.create` are checked against all of this.
---@class RisenAssetsApi
local assetsApi = {}

---Where a resource name comes from.
---@class AssetInfo
---@field name string The name asked for.
---@field compiled? string File name the engine looks up (`It_Wpn_1H_Club._xmsh`); missing when `name` is not a resource name (`found` is `false`).
---@field kind? string `"mesh"`, `"material"`, `"image"`, `"actor"`, `"motion"` or `"collision"`.
---@field found boolean A file with that name exists in the game archives or a mounted directory.
---@field virtual? string Virtual path (`#G3:/data/common/meshes/...`).
---@field physical? string Path on disk (inside a `.pak` for packed files).
---@field loose? boolean `true` for a real file (not inside a `.pak`).
---@field source? string `"game"` for packed files, `"<mod>:assets"`, `"<mod>:generated"` or `"<mod>:runtime"` for the directories of this layer, `"loose"` otherwise.

---The engine's resource object of a name (`eCResource2`).
---@class AssetResource
---@field exists boolean The engine knows the file (`state` is not 0).
---@field state integer `0` missing or failed to load, `1` header known, `2` queued, `3` data loaded and cached (a mesh/material is loaded the first time something uses it).
---@field path string Virtual path of the file it was loaded from.

---A mounted asset directory.
---@class AssetMountInfo
---@field mod string
---@field kind string `"assets"`, `"generated"` or `"runtime"`.
---@field virtual string
---@field physical string

---Name mapping: `"It_Wpn_1H_Club.xmsh"` -> `"It_Wpn_1H_Club._xmsh"`, `.tga/.dds/.png/.ximg` -> `._ximg`, `.xmat` -> `._xmat`, `.xac` -> `._xmac`; an already compiled name stays.
---@param name string A bare file name (no directory).
---@return string|nil compiled
---@return string kind The kind, or the reason when `compiled` is `nil`.
function assetsApi.compiled_name(name) end
---Looks a resource name up in the name cache. Pending files written by `write` are mounted first.
---@param name string `"Spass_Club.xmsh"`, `"X.tga"`, `"X._ximg"`, ...
---@return AssetInfo
function assetsApi.info(name) end
---`true` when `info(name).found`.
---@param name string
---@return boolean
function assetsApi.exists(name) end
---The bytes of the file a resource name resolves to (a packed or a loose one), for deriving new assets from existing ones:
---`risen.assets.write("meshes/Mine._xmsh", risen.assets.read("It_Wpn_1H_Club.xmsh"))`.
---@param name string
---@return string|nil data
---@return string|nil reason
function assetsApi.read(name) end
---Checks a name the way `risen.template.create` does: `kind` is `"mesh"`, `"actor"`, `"image"`, `"material"` or `"icon"` (a registered GUI bitmap). Returns `true` and the `AssetInfo`
---(nothing for icons), or `nil, reason`.
---@param kind string
---@param name string
---@return boolean|nil ok
---@return AssetInfo|string|nil infoOrReason
function assetsApi.check(kind, name) end
---The engine's resource object for a name: registers it and loads its header when it is new (a missing file gives `exists = false`, the entry stays registered).
---`state` 3 means the data was loaded, which happens when an entity that uses it exists (spawn an item, wait a moment).
---@param name string
---@return AssetResource|nil
function assetsApi.resource(name) end
---Directories mounted for mods (startup and runtime).
---@return AssetMountInfo[]
function assetsApi.mounted() end
---Remounts generated directories that received or lost files since the last mount (the name cache is built when a directory is mounted). Done automatically by `info`, `resource`, `read`, `load_icons`.
---@return integer remounted
function assetsApi.refresh() end

---Writes a file below `<Risen>\bin\spass\generated\<mod>\` (nothing else can be written; `relpath` has no `..`, drive or empty parts). Files of that directory are mounted at startup in every later
---process, so a mod can derive assets once and keep them. A file whose name already exists shadows it.
---@param relpath string e.g. `"meshes/Spass_Club._xmsh"`.
---@param data string Binary-safe, up to 256 MB.
---@return boolean|nil ok
---@return string|nil reason
function assetsApi.write(relpath, data) end
---Deletes a file or directory tree that `write` created.
---@param relpath string
---@return integer|nil removed
---@return string|nil reason
function assetsApi.remove(relpath) end
---Deletes everything this mod generated.
---@return integer removed
function assetsApi.clear() end
---Mounts a directory of this mod (relative to the mod directory) so that its files are found as resources.
---@param reldir string
---@return string|nil virtual_name
---@return string|nil reason
function assetsApi.mount(reldir) end

---Registers the images of the XML image list `<list>.ximl` (found like any asset file) as named GUI bitmaps, so that their `ImageName` values work as `icon`.
---The file looks like `<ImageList ClassName="eCGuiImageList2" ImageSize="0; 0" TextureName="My_Atlas.dds"><Images><Image ClassName="eCGuiImage2" ImageName="IC_My_Sword"
---RectOnTexture="123; 534; 163; 574" BlendMode="eEImageBlend_AlphaBlend"></Image></Images></ImageList>` with `My_Atlas._ximg` next to it (`dump_collection` shows the stock lists).
---Safe to call again; do it before the templates that use the icons (the first `tick`).
---@param list string e.g. `"Spass_Icons"`
---@return boolean|nil ok
---@return string|nil reason
function assetsApi.load_icons(list) end
---Lower level of `load_icons`: `eCGuiDevice2::LoadImageListA`.
---@param list string
---@return boolean ok
function assetsApi.load_image_list(list) end
---Registers a whole image file (`Spass_Icon.ximg`, a `_ximg` found as an asset) as a GUI bitmap whose name is the file name; use `icon = "Spass_Icon.ximg"`. The picture is scaled into the icon slot.
---@param name string
---@return boolean ok
function assetsApi.load_bitmap(name) end
---Whether the GUI knows a bitmap name (item icons are `IC_It_...`, case-insensitive); `attached` is `false` for a name nothing registered. Asking registers an empty placeholder for an unknown name.
---@param name string
---@return {attached: boolean}|nil
function assetsApi.bitmap(name) end
---Development aid: writes a stock image collection (`"IMC_Icons"`, `"IMC_GUI"`, ...) as XML to `<Risen>\bin\spass\generated\_dump\<relpath>`; shows the `.ximl` format.
---@param collection string
---@param relpath string
---@return boolean ok
function assetsApi.dump_collection(collection, relpath) end

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
-- Live verification and tracing
---------------------------------------------------------------------------------------------------------------------

---Checks for scenarios that are verified in the running game (docs/DEVELOPMENT.md "Testing and tracing"). Every function exists in every
---run and only logs (`[test] ok|FAIL name: detail`) unless `config.toml` has `[test] enabled = true` (set by `scripts/test-run.ps1 -WaitForResult`)
---or `[lua] unsafe = true`: then `finish` also writes `<Risen>\bin\spass\test-result.json`, the `[test] deadline_s` applies and (with `enabled`)
---the process ends shortly after `finish`. A mod may therefore ship its checks.
---@class RisenTestApi
local testApi = {}

---Records a check and logs `[test] ok|FAIL name: detail`. Never raises.
---@param name string Identifies the check in the result file (and in `plan`).
---@param cond any Truthy = pass (`nil` and `false` fail).
---@param detail? any Shown in the log and the result file; put the values in it.
---@return boolean ok
function testApi.expect(name, cond, detail) end

---Records a check that `actual` equals `expected` (tables are compared by content); the detail reads `expected {1, 3}, got {1, 2}` on a mismatch.
---@param name string
---@param actual any
---@param expected any
---@param tolerance? number For numbers: the largest allowed difference.
---@return boolean ok
function testApi.eq(name, actual, expected, tolerance) end

---Records a check that `tostring(text)` matches the Lua pattern (`string.find`, not a regex).
---@param name string
---@param text any
---@param pattern string
---@return boolean ok
function testApi.matches(name, text, pattern) end

---Starts a named section: later checks carry its name, and a hang or timeout is reported "in section X".
---@param name string
function testApi.section(name) end

---Declares the checks the scenario must record. `finish` fails the run when a planned check never ran (nothing gets dropped silently).
---@param names string[]
function testApi.plan(names) end

---Gets, or with an argument sets, the scenario name shown in the result file (default: `[test] scenario` of the config).
---@param name? string
---@return string
function testApi.scenario(name) end

---A scenario parameter from `[test.params]` of the config (`scripts/test-run.ps1 -Param key=value`); strings.
---@param name string
---@param default? any Returned when the parameter is not set.
---@return string|any
function testApi.param(name, default) end

---Whether the run writes a result file and has a deadline (`[test] enabled` or `[lua] unsafe`).
---@return boolean
function testApi.is_active() end

---Seconds since the mod layer started.
---@return number
function testApi.elapsed() end

---Ends the run: writes `test-result.json` (scenario, every check with its detail and time, timings, unfinished checks; status `pass` needs at
---least one check, none failed, no planned check missing and no error line in `spass.log`) and, with `[test] enabled`, ends the game process. The script
---`test-run.ps1 -WaitForResult` returns as soon as it sees the file. Only the first call counts.
---@param summary? string
---@return boolean
function testApi.finish(summary) end

---Records a failed check `fail` and ends the run as `aborted: <reason>`.
---@param reason string
---@return boolean
function testApi.fail(reason) end

---Polls `predicate()` once per frame (the `tick` event, not at the main menu). `on_done(true)` when it returned a truthy value; after `timeout_s`
---seconds the check `wait <name>` fails and `on_done(false, "timeout")` is called (also when the predicate raised an error).
---@param predicate fun(): any
---@param timeout_s number
---@param on_done? fun(ok: boolean, reason?: string)
---@param name? string Shown in the failure and in the list of unfinished work of a timed-out run.
function testApi.wait_for(predicate, timeout_s, on_done, name) end

---Calls `on_done(true)` after `n` frames.
---@param n integer
---@param on_done fun(ok: boolean)
function testApi.wait_ticks(n, on_done) end

---Runs `fn` as a coroutine from the next frame on, so a scenario reads top to bottom: inside it `sleep`, `wait_until` and `wait_event` pause it.
---An error in `fn` fails the run (with the traceback); when `fn` returns, `finish()` is called. One scenario per mod layer.
---@param fn fun()
---@param scenario_name? string
function testApi.run(fn, scenario_name) end

---Inside `run`: continues after `ticks` frames (default 1).
---@param ticks? integer
function testApi.sleep(ticks) end

---Inside `run`: continues when `predicate()` is truthy. After `timeout_s` seconds (default 60) the check `wait <name>` fails and the run ends.
---@param predicate fun(): any
---@param timeout_s? number
---@param name? string
---@return true
function testApi.wait_until(predicate, timeout_s, name) end

---Inside `run`: waits for an event (and `filter(...)` returning true, when given) and returns its arguments. Registers its own handler, so call it
---before the event can fire, e.g. after `risen.world.new_game()` (a request the engine performs later). Times out like `wait_until`.
---@param event string
---@param timeout_s? number Default 120.
---@param filter? fun(...): boolean
---@return any ...
function testApi.wait_event(event, timeout_s, filter) end

---Opt-in trace channels (`config.toml` `[debug] trace = "template,balance,save,events"`, `"all"`, or at run time `risen.trace.enable`). A disabled channel
---costs one bit test. Lines read `[HH:MM:SS.mmm] [trace:channel] ...` in `spass.log` (and in `trace.log` with `[debug] trace_file = true`).
---Built-in channels: `events` (every event the layer emits, with its arguments), `template`, `balance` (every applied job), `save` (save/load phases with
---durations), `spawn`, `script` (define/override/call), `lua` (handler errors with traceback), `test`. A mod may use any other name.
---Calling `risen.trace(channel, ...)` writes the `tostring`-ed arguments, joined by spaces, when the channel is on.
---@class RisenTraceApi
---@overload fun(channel: string, ...: any)
local traceApi = {}

---Turns channels on (comma separated; `all` or `*` for every one).
---@param channels string
---@return string enabled All enabled channels.
function traceApi.enable(channels) end

---Turns channels off.
---@param channels string
---@return string enabled
function traceApi.disable(channels) end

---Whether a channel is on: guard expensive message building with it.
---@param channel string
---@return boolean
function traceApi.enabled(channel) end

---The enabled channels, comma separated.
---@return string
function traceApi.channels() end

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
---@field api_version integer Generation of this Lua API; a mod can require one with `api_version` in mod.toml.
---@field log RisenLogApi
---@field trace RisenTraceApi
---@field test RisenTestApi
---@field entity RisenEntityApi
---@field template RisenTemplateApi
---@field item RisenItemApi
---@field balance RisenBalanceApi
---@field npc RisenNpcApi
---@field quest RisenQuestApi
---@field dialog RisenDialogApi
---@field script RisenScriptApi
---@field gamevar RisenGamevarApi
---@field world RisenWorldApi
---@field loc RisenLocApi
---@field save RisenSaveApi
---@field vfs RisenVfsApi
---@field assets RisenAssetsApi
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
