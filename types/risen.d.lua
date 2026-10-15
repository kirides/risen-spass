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

---Whether the entity still exists in the world (looked up again by its unique name). Gotcha: items and other spawns are named after their template (`It_Apple`), not uniquely,
---so for them this finds the first entity of that name, which may be another one (and re-points the handle at it): do not use it for items.
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

---Removes the entity from the world (destroys it; not a combat death: no `entity.died`/`npc.killed`, but `entity.destroyed` fires).
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

---Every NPC and monster of the world (`Entity::GetNPCs`; the player is among them), or only those within `radius` game units of `center`. The list is built on every call.
---@param center? Entity
---@param radius? number
---@return Entity[] npcs
function entityApi.npcs(center, radius) end

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

---Whether a spec validated in this process names `name` as a template it is about to create (`new_items`, `new_npcs`, `templates`): later sections (the ingredients and the result
---of `new_recipes`) may refer to such an item although it does not exist yet. Validation of one section runs before the writes of all of them.
---@param name string
---@return boolean
function templateApi.is_planned(name) end

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

---Moves up to `count` items of a template from one inventory to another through the engine's own transfer (`PSInventory::TransferAmountTo`), stack by
---stack (unequipped stacks). Fires `inventory.removed` for `from` and `inventory.added` for `to` (cause `"script"`, each with the other as `other`).
---@param from Entity
---@param to Entity
---@param template string
---@param count? integer Default 1.
---@return integer moved The amount that was really moved.
function itemApi.transfer(from, to, template, count) end

---The engine's loot action (`PSInventory::LootSelections`): everything `container` holds (or `count` of one template when `template` is given) moves to
---`looter`, as when the player loots a corpse or a chest. Events: `inventory.added`/`removed` with cause `"loot"` and `other` set on both.
---@param looter Entity
---@param container Entity
---@param template? string Only this template.
---@param count? integer With `template`: how many (default 1).
---@return boolean ok
function itemApi.loot(looter, container, template, count) end

---The engine's trade action (`PSInventory::TradeSelections`) with `count` of a template selected in `a`: they move to `b` (no gold changes hands: the
---price logic belongs to the trade screen). Events: `inventory.added`/`removed` with cause `"trade"`.
---@param a Entity
---@param b Entity
---@param template string
---@param count? integer Default 1.
---@return boolean ok
function itemApi.trade(a, b, template, count) end

---Drops items of an unequipped stack into the world (`gCInventory_PS::DropItemsFromStack`). Fires `inventory.removed` with cause `"drop"`.
---@param owner Entity
---@param template string
---@param count? integer Default 1 (at most the size of the stack).
---@return Entity|nil item The item entity now lying in the world, `nil` when the owner has no such stack.
function itemApi.drop(owner, template, count) end

---The owner takes an item that lies in the world (`PSInventory::InsertStack`; the world entity is consumed). Fires `item.picked_up` and
---`inventory.added` with cause `"pickup"`, and `entity.destroyed` for the world item.
---@param owner Entity
---@param item Entity An item entity (e.g. the result of `risen.item.drop` or `risen.entity.spawn`).
---@return integer|nil stack Index of the stack the item went to, `nil` on failure.
function itemApi.pick_up(owner, item) end

---Equips the first stack of a template (`PSInventory::EquipStack`, what the inventory screen does). Fires `item.equipped` (and `item.unequipped` for what it replaces).
---@param owner Entity
---@param template string
---@return boolean ok
function itemApi.equip(owner, template) end

---Takes the first stack of a template off (`PSInventory::UnEquipStack`). Fires `item.unequipped`.
---@param owner Entity
---@param template string
---@return boolean ok
function itemApi.unequip(owner, template) end

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
---`"required"` (RequiredSkills) or `"modifiers"` (ModifySkills) of a `gCItem_PS`, or `"recipe_required"` (the RequiredSkills of a recipe, `gCRecipe_PS`, see `risen.recipe`);
---`modifier` is only present for `"modifiers"`.
---@param target string|Entity Template name or item/recipe entity.
---@param kind "required"|"modifiers"|"recipe_required"
---@return { skill: string, amount: integer, modifier?: string }[]|nil entries
---@return string|nil reason
function itemApi.skill_array(target, kind) end

---Raw write of the whole array (engine enum names; `modifier` defaults to `"gESkillModifier_AddValue"`). Names are checked through the engine's
---own enum parser before the item is touched.
---@param target string|Entity Template name or item/recipe entity.
---@param kind "required"|"modifiers"|"recipe_required"
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
---@field spells? table<string, SpellSettings> Edits of the shipped spells: `{ Fireball = { mana_cost = 12, damage = 150 }, Light = { mana_cost = 7 } }` (see `risen.spell.names`, docs/SPELL_PROPERTIES.md).
---@field new_spells? table<string, NewSpell> New spells made of the pieces of shipped ones: `{ Spass_Fireball_Strong = { base = "Fireball", mana_cost = 24, damage_mult = 2, forms = { "crystal", "scroll" } } }`.
---@field recipes? table<string, RecipeEdit> Edits of crafting recipes: `{ It_1H_Steel_Hot = { ingredients = { It_1H_Steel_Hot = 1, It_IronOre = 2 }, skills = { Smith = 2 } } }` (see `risen.recipe.names`, docs/RECIPE_PROPERTIES.md).
---@field new_recipes? table<string, NewRecipe> New recipes (a recipe item each): `{ Spass_Plan = { station = "anvil", ingredients = { It_1H_Sword = 1, It_IronOre = 2 }, result = "Spass_Sword", skills = { Smith = 1 }, learn = true } }`.
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

---Registers a handler for another section of the spec (the shipped sections `npcs`, `spells`, ... are registered this way). `handler(data, opts)` returns
---`jobs, problems`: validated jobs `{ label = string, run = function(opts) -> ok, err, touched end }` and a list of problem strings.
---@param name string
---@param handler fun(data: any, opts: BalanceOptions): table[], string[]
function balanceApi.register_section(name, handler) end

---Names of the registered sections (`{ "item_groups", "items", "new_items", "new_npcs", "new_spells", "npc_groups", "npcs", "spells", "templates" }`).
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

---Validates a friendly skill table (`{ Smith = 1 }` or a list of `{ skill =, amount = }`) the way the `requires` setting of items is and converts it to engine entries (the
---input of `risen.item.set_skill_array`). Nothing is written.
---@param spec table
---@param what? string Label for the problem messages (default `"skills"`).
---@return { skill: string, amount: integer }[]|nil entries
---@return string[]|nil problems
function balanceApi.skill_entries(spec, what) end

---Engine skill entries (as `risen.item.skill_array` returns them) as a friendly table `{ Smith = 1 }`; the highest amount wins when a skill is listed twice.
---@param entries { skill: string, amount: integer }[]
---@return table<string, integer>
function balanceApi.skill_map(entries) end

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
-- Spells (docs/RE.md section 29, docs/SPELL_PROPERTIES.md)
---------------------------------------------------------------------------------------------------------------------

---One entry of the `spells` section: edits a shipped spell. The settings of an item apply to its crystal (Fireball, Frost, Missile) or rune; scroll and orb costs have
---their own keys. Projectile settings (`damage` ... `projectile_properties`) only exist for Fireball, Frost and Missile and change the projectile templates
---(`It_Projectile_Fireball_S0..S2`, ...); `*_mult` settings are relative to the shipped values (applying twice never compounds).
---@class SpellSettings
---@field mana_cost? integer `gCCastInfo_PS.ManaCost` of the crystal / rune (what the engine's cast takes from the caster's mana).
---@field mana_mult? number Relative: crystal, rune and scroll cost = shipped * factor (an orb stays free). Excludes `mana_cost`.
---@field scroll_mana_cost? integer Cost of the scroll (rune spells only).
---@field orb_mana_cost? integer Cost of the orb (rune spells only).
---@field requires? ItemRequirements Equip requirement of the crystal / rune, e.g. `{ Fireball = 2 }` or `{ Circle = 3 }`.
---@field consume? boolean `gCCastInfo_PS.ConsumeItem`: the item is used up by a cast.
---@field properties? TemplateEdits Any other property of the crystal / rune items (`{ gCItem_PS = { GoldValue = 900 } }`).
---@field damage? integer|integer[] `gCDamage_PS.DamageAmount` of the projectiles, one number for all levels or one per projectile level (Fireball and Frost have three, Missile one). A hit takes `damage + 25 * Fireball skill - protection` (see docs/SPELL_PROPERTIES.md).
---@field damage_mult? number Relative damage.
---@field damage_type? string `gCDamage_PS.DamageType`: `"Magic"`, `"Fire"`, `"Ice"`, `"Blunt"`, `"Edge"`, `"Point"`, `"Physics"`, `"None"` (picks the protection skill that reduces the hit).
---@field speed? number `gCProjectile2_PS.StartVelocity`.
---@field speed_mult? number
---@field radius? number `gCProjectile2_PS.SweepTestRadius`.
---@field radius_mult? number
---@field effect? string `gCEffect_PS.Effect` of the projectile (the flying effect, e.g. `"eff_projectile_Frostball_01"`).
---@field projectile_properties? TemplateEdits Any other property of the projectile templates.

---One item form of a new spell: the settings of that item. `name`/`description` are label texts (a string or `{ English = "...", German = "..." }`).
---@class NewSpellItem
---@field name? string|table<string,string>
---@field description? string|table<string,string>
---@field icon? string Inventory icon (`gCItem_PS.IconImage`).
---@field value? integer Gold value.
---@field mana_cost? integer
---@field requires? ItemRequirements
---@field consume? boolean
---@field properties? TemplateEdits

---One entry of the `new_spells` section / the argument of `risen.spell.create`: a new spell built from the pieces of the shipped spell `base`. Every form is a new item
---template (`risen.template.create`, deterministic ID: savegame-safe): crystal = `<name>`, rune = `<name>`, scroll = `<name>_Scroll`, orb = `<name>_Orb`. The item keys
---(`name` ... `properties`) apply to the main form (crystal / rune) and `mana_mult` to every form; `crystal`, `rune`, `scroll`, `orb` tables override single forms. The
---projectile keys of a Fireball/Frost/Missile spell are applied to the projectile of **every shot** of these items (the engine picks the projectile per spell type, not per
---crystal), relative to the projectile's current values.
---@class NewSpell: SpellSettings
---@field base string The shipped spell: `risen.spell.names()`.
---@field forms? string[] `{ "crystal", "scroll" }` for Fireball/Frost/Missile (default `{ "crystal" }`), `{ "rune", "scroll", "orb" }` for the rune spells (default `{ "rune" }`).
---@field name? string|table<string,string>
---@field description? string|table<string,string>
---@field icon? string
---@field value? integer
---@field crystal? NewSpellItem
---@field rune? NewSpellItem
---@field scroll? NewSpellItem Scroll of a projectile spell: the crystal that is used up (`consume`), no requirements, half the cost by default.
---@field orb? NewSpellItem

---What the catalogue knows about a spell.
---@class SpellInfo
---@field name string
---@field kind string `"projectile"` (Fireball, Frost, Missile: cast with a crystal) or `"rune"` (rune, scroll and orb items).
---@field skill? string The magic skill of a projectile spell (`Fireball`, `Frost`, `Missile`).
---@field items table<string,string> Item templates by form: `crystal` or `rune`, `scroll`, `orb`.
---@field projectiles string[] The projectile templates the engine spawns (empty for rune spells).

---The numbers of a spell as the templates hold them now.
---@class SpellNumbers
---@field name string
---@field kind string
---@field items table<string, { template: string, mana_cost: number|nil, consume: boolean, requires: ItemRequirements, value: number|nil }>
---@field projectiles { template: string, damage: number|nil, damage_type: string|nil, speed: number|nil, radius: number|nil, effect: string|nil }[]

---@class RisenSpellApi
local spellApi = {}

---Equip slot of runes and scrolls (UseType Cast).
---@type integer
spellApi.SLOT_CAST = 10

---Equip slot of the crystals (and of scrolls of projectile spells).
---@type integer
spellApi.SLOT_MAGIC = 11

---The shipped spells: the projectile spells `Fireball`, `Frost`, `Missile` and the rune spells `Beast`, `Berserker`, `BlueBarrier`, `EnhancedHeal`, `Illusion`, `Inferno`, `Joke`,
---`Levitate`, `Light`, `MediumHeal`, `MinorHeal`, `Nautilus`, `OpenLock`, `Protection`, `RedBarrier`, `Skeleton`, `Speed`, `Telekinesis`. Sorted.
---@return string[]
function spellApi.names() end

---The item and projectile templates of a spell (docs/SPELL_PROPERTIES.md).
---@param name string
---@return SpellInfo|nil
---@return string|nil reason The valid names when the spell is unknown.
function spellApi.info(name) end

---Mana cost, requirements, consume flag and value of every item of a spell and the numbers of its projectiles, read from the templates (so a `spells` edit shows).
---@param name string
---@return SpellNumbers|nil
---@return string|nil reason
function spellApi.get(name) end

---Edits one shipped spell: `risen.balance.apply { spells = { [name] = settings } }` (validated first, nothing is written on a problem). Templates only on the first
---`tick`; use `risen.balance.install` to apply it on every `session.start` too.
---@param name string
---@param settings SpellSettings
---@param opts? BalanceOptions
---@return BalanceReport|nil report
---@return string|nil reason
---@return string[]|nil problems
function spellApi.modify(name, settings, opts) end

---Creates one new spell (the `new_spells` section for a single entry) and enables its per-shot overrides for the calling mod. Same timing rules as `risen.template.create`
---(first `tick` at the earliest, every process again).
---@param name string Name of the crystal / rune template (letters, digits, `_`).
---@param spec NewSpell
---@param opts? BalanceOptions
---@return BalanceReport|nil report
---@return string|nil reason
---@return string[]|nil problems
function spellApi.create(name, spec, opts) end

---The registered per-shot overrides of the new projectile spells: `{ [item template] = { spell, base, projectiles, settings, scroll } }`.
---@return table<string, table>
function spellApi.overrides() end

---Registers this mod's handlers for new projectile spells: `entity.spawned` (writes the per-shot numbers into the projectile of a shot) and `tick` (takes the last scroll of
---a projectile spell away once its shot is over: the engine would keep it). `risen.spell.create` and `risen.balance.install` for a spec with `new_spells` call it for you.
---@return boolean enabled `false` when it was enabled before.
function spellApi.enable_overrides() end

---The template name of the item in an equip slot of a character (`nil` when empty): slot 11 (default, `risen.spell.SLOT_MAGIC`) holds the crystal or fireball/frost/missile scroll,
---slot 10 (`SLOT_CAST`) the rune or scroll of a rune spell.
---@param who Entity
---@param slot? integer
---@return string|nil template
function spellApi.equipped(who, slot) end

---The engine's own mana check for casting with `item` (a template name; `PSCastInfo::CanCast`): its `ManaCost` is at most the caster's total mana.
---@param caster Entity
---@param item string
---@return boolean|nil can
---@return string|nil reason
function spellApi.can_cast(caster, item) end

---What the cast scripts do when a spell is cast (`PSCastInfo::Consume`): takes the item's `ManaCost` from the caster's base mana and, when `ConsumeItem` is set, uses up one
---item of the stack (a rune or scroll of Cast type; the last item of a scroll cast from the magic slot stays, see `discard`). The caster has to carry the item.
---@param caster Entity
---@param item string
---@return boolean|nil ok
---@return string|nil reason
function spellApi.consume(caster, item) end

---Deletes the whole stack of `item` from an inventory (`gCInventory_PS::DeleteStack`), also a stack the engine emptied but left behind (`risen.item.take` cannot remove that).
---@param owner Entity
---@param item string
---@return boolean ok
function spellApi.discard(owner, item) end

---Who shot a projectile (`PSProjectile::GetShooter`), `nil` for anything that was not shot.
---@param projectile Entity
---@return Entity|nil
function spellApi.shooter(projectile) end

---------------------------------------------------------------------------------------------------------------------
-- Recipes
---------------------------------------------------------------------------------------------------------------------

---One ingredient or result of a recipe: an item template and an amount.
---@class RecipeItem
---@field item string Template name (docs/TEMPLATES.md).
---@field count integer

---What a recipe is (docs/RECIPE_PROPERTIES.md).
---@class RecipeInfo
---@field name string The recipe item's template.
---@field label string|nil What the recipe item is called.
---@field station string `"anvil"`, `"forge"`, `"grindstone"`, `"coolweapon"`, `"alchemy"`, `"goldsmith"`, `"scroll"`, `"cooking"`, `"frying"`.
---@field category string The engine enum `gERecipeCategory_...`.
---@field ingredients RecipeItem[]
---@field result RecipeItem The engine creates exactly one result item per craft (`count` is always 1).
---@field skills table<string, integer> The crafter's skills, e.g. `{ Smith = 1 }`.
---@field shipped boolean `false` for a recipe made by `risen.recipe.create` / `new_recipes`.

---Edit of a shipped (or created) recipe, one entry of the `recipes` section / the argument of `risen.recipe.modify`. Every key is optional.
---@class RecipeEdit
---@field station? string Moves the recipe to another station (`risen.recipe.stations()`).
---@field ingredients? RecipeItem[]|table<string,integer> Replaces the ingredient list: `{ It_IronOre = 2 }` or `{ { item = "It_IronOre", count = 2 }, "It_1H_Sword" }` (a bare name is one piece).
---@field counts? table<string,integer> Changes the amount of existing ingredients: `{ It_IronOre = 3 }`. Excludes `ingredients`.
---@field skills? table<string,integer> Replaces the required skills (`{}` removes them): `{ Smith = 2 }`.
---@field result? string|RecipeItem The result item (count must be 1: the engine ignores `ResultAmount`).
---@field properties? TemplateEdits Other properties (not the recipe's own parts: use the keys above).

---One new recipe, an entry of the `new_recipes` section / the argument of `risen.recipe.create`: a recipe item (a new template cloned from a shipped recipe item, with the
---deterministic ID of its name, so savegames that hold it load again). A character knows the recipe while the recipe item is in its inventory.
---@class NewRecipe
---@field station string Required: `risen.recipe.stations()`.
---@field ingredients RecipeItem[]|table<string,integer> Required, 1 to 16 ingredients; items of the same spec's `new_items` are accepted.
---@field result string|RecipeItem Required: the item that is made (count 1).
---@field skills? table<string,integer> Required skills of the crafter, e.g. `{ Smith = 1 }` (none by default).
---@field base? string Recipe item to clone (default `"It_Recipe_Health_01"`: a written recipe with the paper icon); needs a `gCRecipe_PS`.
---@field name? string|table<string,string> Label of the recipe item (default `"Recipe: <result label>"`).
---@field description? string|table<string,string> Description text (default: the station, the ingredients and the result).
---@field icon? string Inventory icon of the recipe item (`gCItem_PS.IconImage`).
---@field mesh? string Mesh of the recipe item.
---@field value? integer Gold value of the recipe item.
---@field properties? TemplateEdits Other properties of the recipe item.
---@field learn? boolean The player is given the recipe item once per savegame (on `world.ready` of a new game or a load; a game variable remembers it). `risen.balance.install` and `risen.recipe.create` of a mod arrange it.

---@class RisenRecipeApi
local recipeApi = {}

---The valid values of `station`: `"alchemy"`, `"anvil"`, `"cooking"`, `"coolweapon"`, `"forge"`, `"frying"`, `"goldsmith"`, `"grindstone"`, `"scroll"` (engine enums
---`gERecipeCategory_Alchemy`, `..._Smith_Anvil`, ... are accepted as well). The anvil, forge, grindstone and cool-weapon (quench) stations belong to smithing.
---@return string[]
function recipeApi.stations() end

---Every recipe template (shipped ones and those created by this layer), sorted; with a station only that station's. Scans the item templates once.
---@param station? string
---@return string[]|nil names
---@return string|nil reason The valid stations when `station` is unknown.
function recipeApi.names(station) end

---What a recipe is: station, ingredients, result, required skills (read from the templates now, so an edit shows).
---@param name string Template name of the recipe item.
---@return RecipeInfo|nil
---@return string|nil reason
function recipeApi.info(name) end

---Same as `risen.recipe.info` (for symmetry with `risen.spell.get`).
---@param name string
---@return RecipeInfo|nil
---@return string|nil reason
function recipeApi.get(name) end

---Whether the character knows the recipe. The engine has no list of learned recipes: a character knows a recipe while a stack of the recipe item is in its inventory (the
---stations list the recipe items of their category from the inventory). The smithing blanks (`It_1H_Steel_Hot`, ...) are recipe items too: holding one is knowing its recipe.
---@param who Entity
---@param name string
---@return boolean|nil known
---@return string|nil reason
function recipeApi.known(who, name) end

---Teaches the recipe: gives the character the recipe item unless it carries one. `true` = given, `false` = already known.
---@param who Entity
---@param name string
---@return boolean|nil given
---@return string|nil reason
function recipeApi.learn(who, name) end

---Takes the recipe item away. `true` = taken, `false` = it was not known.
---@param who Entity
---@param name string
---@return boolean|nil taken
---@return string|nil reason
function recipeApi.forget(who, name) end

---`learn` once per savegame: gives the recipe item the first time and remembers that in the game variable `SPASS_RECIPE_<name>` (saved with the game), so a character that sold
---or dropped the item is not given it again. `false` = was given before.
---@param who Entity
---@param name string
---@return boolean|nil given
---@return string|nil reason
function recipeApi.learn_once(who, name) end

---Registers this mod's `world.ready` handler that runs `learn_once` for the player: the recipe item is given on a new game and on a load of a savegame that never had it.
---`risen.balance.install` (for `new_recipes` entries with `learn = true`) and `risen.recipe.create` call it for you.
---@param name string
---@return boolean registered
function recipeApi.learn_on_start(name) end

---The engine's own craft check (`PSInventory::CanSynthesize`): how many times the character can craft the recipe with the ingredients in its inventory and its skills (0 comes with
---a second value saying what is missing). The station and the recipe item are not part of it.
---@param who Entity
---@param name string
---@return integer|nil times
---@return string|nil reasonOrMissing
function recipeApi.can_craft(who, name) end

---Crafts through the engine: checks like a station (`CanSynthesize`), then `PSInventory::Synthesize` deletes the ingredients and creates `count` result items (events `inventory.removed`,
---`inventory.added` with cause `"crafting"`, then one `crafting`). Nothing is touched when the check fails. Needs the recipe item in the inventory (like a station) unless
---`opts.unknown`. No animation, no station: use `risen.interact.begin` for the real anvil.
---@param who Entity
---@param name string
---@param count? integer 1 to 99, default 1.
---@param opts? { unknown?: boolean }
---@return boolean|nil ok
---@return string|nil resultOrReason The result item's template name, or why not.
function recipeApi.craft(who, name, count, opts) end

---Edits one recipe: `risen.balance.apply { recipes = { [name] = settings } }` (validated first, nothing is written on a problem). Templates only on the first `tick`; use
---`risen.balance.install` to apply it on every `session.start` too.
---@param name string
---@param settings RecipeEdit
---@param opts? BalanceOptions
---@return BalanceReport|nil report
---@return string|nil reason
---@return string[]|nil problems
function recipeApi.modify(name, settings, opts) end

---Creates one new recipe (the `new_recipes` section for a single entry) and, with `learn`, arranges that the calling mod's player is taught it. Same timing rules as
---`risen.template.create` (first `tick` at the earliest, every process again).
---@param name string Name of the recipe item (letters, digits, `_`).
---@param spec NewRecipe
---@param opts? BalanceOptions
---@return BalanceReport|nil report
---@return string|nil reason
---@return string[]|nil problems
function recipeApi.create(name, spec, opts) end

---Natives (engine names): the ingredient array of a recipe, `{ { item = "It_IronOre", count = 2 }, ... }`. Use `risen.recipe.info`.
---@param target string|Entity Template name or recipe entity.
---@return RecipeItem[]|nil
---@return string|nil reason
function recipeApi.ingredients_raw(target) end

---Native: replaces the whole ingredient array (`gCRecipe_PS::SetCraftIngredients`; the items are checked by the engine's own template parser first). Use the `recipes` section.
---@param target string|Entity Template name or recipe entity.
---@param list RecipeItem[]
---@return boolean|nil ok
---@return string|nil reason
function recipeApi.set_ingredients_raw(target, list) end

---Native: `PSInventory::CanSynthesize`, how many times the character can craft (0 when a skill or an ingredient is missing). Use `risen.recipe.can_craft`.
---@param who Entity
---@param name string
---@return integer|nil
---@return string|nil reason
function recipeApi.can_craft_raw(who, name) end

---Native: `PSInventory::Synthesize` **without the check** (what every station calls once the player chose an amount): deletes `count` times the ingredients that exist and creates
---`count` result items. Use `risen.recipe.craft`.
---@param who Entity
---@param name string
---@param count integer
---@return integer|nil stack Stack index of the result.
---@return string|nil reason
function recipeApi.craft_raw(who, name, count) end

---------------------------------------------------------------------------------------------------------------------
-- Interactions
---------------------------------------------------------------------------------------------------------------------

---The engine's interaction of a character with a mob (anvil, forge, alchemy table, chest, ...): what the use key does (docs/RE.md section 30).
---@class RisenInteractApi
local interactApi = {}

---`gCInteraction::CanInteractWith`: runs the mob's `CanInteract_*` script for this character (the station refuses without the skill, the tool or a recipe, and may print a HUD message).
---@param user Entity The character.
---@param target Entity The mob.
---@return boolean|nil can
---@return string|nil reason
function interactApi.can(user, target) end

---Starts the interaction like the use key: the check above, then the user's AI state becomes `ZS_Interact_PreInteract`, which runs the `PreInteract_*`, `Interact_*` and
---`PostInteract_*` scripts of the mob's use type (walk to the mob, open the station's inventory page, animation). At the anvil or the alchemy table the page waits for the recipe
---choice: call the engine's own click handler `risen.script.call("InvEvent_OnClick_CraftSelect", player, nil, stack_index_of_the_recipe_item)` (with ingredients for more than one
---craft the page first asks for the amount in a modal dialog); the animation then crafts and the state returns to `PS_Normal`. A smith recipe item in the left hand makes the
---engine refuse every other station ("CarryToAnvil"). Mobs: `Object_Interact_Anvil`, `Object_Interact_Alchemy_Table`, `Object_Interact_Forge`, ... (`risen.entity.spawn`).
---@param user Entity The character.
---@param target Entity The mob.
---@return boolean|nil began
---@return string|nil reason
function interactApi.begin(user, target) end

---`PSInteraction::EndInteraction` of the character (leaves a running interaction).
---@param user Entity
---@return boolean ok
function interactApi.finish(user) end

---The entity the engine's focus module has selected right now: what is under the crosshair and what the use key would act on (`gCFocusModule2::GetFocusEntity`, the value the
---game's own `Focus::GetFocusEntity` hands to `BeginInteraction`). `nil` when nothing is focused. Honours `set_focus(entity, true)`.
---@return Entity|nil focus
function interactApi.focus() end

---Makes `entity` the focus (`nil` clears it). The engine rebuilds its candidate list every frame, so without `pin` this lasts one frame; with `pin = true` the entity stays
---the answer of `focus()` and of the use key until `set_focus(nil)` or `set_focus(x, false)` (or until it is removed from the world). A test aid, also for guided interactions.
---@param entity Entity|nil
---@param pin? boolean
---@return boolean ok
function interactApi.set_focus(entity, pin) end

---What the engine knows about the interactions of `entity` (its `gCInteraction_PS`): the use type, every `gCInteraction` object (one per user kind: player, NPC, magic, ...) with
---the names of its scripts, and who uses it right now. The use key runs the FIRST interaction of type `gEInteractionType_Interact_Player` whose `can` script (empty = always) says yes:
---`can` is a plain script (`risen.script.call(can, player, mob)`), `pre` and `interact` are AI functions (resumable scripts that walk, play animations and open windows: not callable
---from Lua), `post` a plain script run when the interaction ends. `props` holds every property of the `gCInteraction` object as text.
---@param entity Entity
---@return InteractionInfo|nil info
---@return string|nil reason `"entity has no gCInteraction_PS"`, `"entity is gone"`.
function interactApi.info(entity) end

---The mob a character is interacting with right now (between `begin` and the end of the interaction: the player's AI state is `ZS_Interact_*`), or `nil`.
---@param user Entity
---@return Entity|nil target
function interactApi.current(user) end

---One `gCInteraction` of a mob.
---@class InteractionEntry
---@field type string `"gEInteractionType_Interact_Player"`, `"gEInteractionType_Interact_NPC"`, `"gEInteractionType_Magic"`, `"gEInteractionType_InventoryUse_Player"`, ...
---@field can string Name of the `CanInteractScript` (`""` = none).
---@field pre string Name of the `PreInteractScript` (an AI function).
---@field interact string Name of the `InteractScript` (an AI function).
---@field post string Name of the `PostInteractScript`.
---@field props table<string,string> All properties of the object, engine text format.

---@class InteractionInfo
---@field use_type string `gCInteraction_PS.UseType`, e.g. `"gEInteractionUseType_Chest"`, `"gEInteractionUseType_Item"`, `"gEInteractionUseType_NPC"`.
---@field interactions InteractionEntry[] In the engine's order.
---@field user Entity|nil The character currently using the mob.
---@field target Entity|nil The mob this entity is currently using (set on characters).

---------------------------------------------------------------------------------------------------------------------
-- Input and HUD
---------------------------------------------------------------------------------------------------------------------

---Keys. A key is named either by an engine ACTION (`"SecondaryAction"` = the right mouse button in the default map, `"PrimaryAction"` = left, `"Selection"` = the use key, `"WeaponMode"`,
---`"Inventory"`, ...: `risen.input.actions()`; the player's current input map decides which scan code triggers it) or by a Windows virtual-key name (`"RButton"`, `"V"`, `"F5"`, `"Shift"`,
---`"Ctrl"`, `"Space"`, ...), read from the OS only while the game window is in the foreground. `"action:<name>"` / `"vk:<name>"` force one reading. `pressed`/`was_pressed`/`was_released`
---compare the key states sampled once per frame before the `tick` event (sampling starts with the first query).
---@class RisenInputApi
local inputApi = {}

---Names of the engine's action keys; the index is the key number (`input.action` events carry it).
---@return string[] names
function inputApi.actions() end

---The `gEActionKey` number of an action name.
---@param name string
---@return integer|nil key `nil` for raw keys and unknown names.
---@return string|nil reason
function inputApi.action_key(name) end

---The key is held down right now (as of the last frame).
---@param name string
---@return boolean|nil down `nil` for an unknown key name.
---@return string|nil reason
function inputApi.pressed(name) end

---The key went down since the previous frame.
---@param name string
---@return boolean|nil
---@return string|nil reason
function inputApi.was_pressed(name) end

---The key went up since the previous frame.
---@param name string
---@return boolean|nil
---@return string|nil reason
function inputApi.was_released(name) end

---Seconds the key has been held (0 when it is up); use `math.floor(seconds * 1000 / ms)` for a repeat timer.
---@param name string
---@return number|nil seconds
---@return string|nil reason
function inputApi.held_for(name) end

---Shift, Ctrl and Alt (raw keys; all false while the game window is not in the foreground).
---@return { shift: boolean, ctrl: boolean, alt: boolean } modifiers
function inputApi.modifiers() end

---Whether the game window has the keyboard focus (raw keys are only read then).
---@return boolean
function inputApi.window_active() end

---The scan codes (keys, mouse buttons, pad buttons) the engine's CURRENT input map turns into an action, with the engine's description text (`"Right Mouse Button"` in the player's
---language). With a name: a list for that action (empty = it cannot be triggered now); without: `{ [action] = list }` for every bound action.
---@param name? string
---@return table|nil bindings `{ { scan = integer, text = string }, ... }` or a table of those lists.
---@return string|nil reason
function inputApi.bindings(name) end

---Test aid: presses (`down = true`) or releases an action as the input device would (`gCActionAdmin::KeyPressed/KeyReleased`): the engine handles it in its next frame exactly like a
---real key (`input.action`, the player state's key script, ...). For a raw key name only `pressed`/`was_pressed` read it as down (no game code sees it).
---@param name string
---@param down boolean
---@return boolean|nil ok
---@return string|nil reason
function inputApi.simulate(name, down) end

---Event `input.action(ev)` (`risen.on("input.action", fn)`): every action event the engine delivers (a press, a hold every frame, a release, analog look/move) before the camera, the player
---state and the character control see it. The handler returns a value other than `nil`/`false` to CONSUME the event (the engine never sees it): that is how a mod can take over a key
---for an action without the game doing it too. Use it sparingly: it runs for every action event, also the analog mouse-look ones.
---@class InputActionEvent
---@field key integer `gEActionKey` number.
---@field action string Action name (`"SecondaryAction"`).
---@field phase "pressed"|"released"|"held"|"analog"|"unknown" `held` repeats every frame while the key is down.
---@field value number Amount of an analog event, 1 for keys.
---@field duration number Seconds the key had been held / released.

---The HUD: the engine's on-screen game log and item log (what quest, level and loot messages use).
---@class RisenHudApi
local hudApi = {}

---Prints a line into the HUD's game log (`GUI2::PrintGameLog`).
---@param text string
---@param type? integer The engine's `gELogMessageType` (default 0; the game uses 1 for notes, 2 for experience and warnings).
---@return boolean|nil ok
---@return string|nil reason
function hudApi.log(text, type) end

---Prints an item line (icon, name, amount; `GUI2::PrintItemLog`).
---@param item string|Entity A template name (`"It_Apple"`) or an item entity.
---@param amount? integer Default 1.
---@return boolean|nil ok
---@return string|nil reason
function hudApi.item_log(item, amount) end

---What the GUI shows: `browsing` (`GUI2::IsBrowsing`: some page that is more than the plain game view is visible; the game's own use key is ignored while it is true), `game` (the plain
---game view), `loot` (the loot window), `mode` (`gEHudMode`), `pages` (the visible `gEHudPage` numbers).
---@return { browsing: boolean, game: boolean, loot: boolean, mode: integer, pages: integer[] }|nil state
function hudApi.state() end

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
---@field text? string|table<string, string> The spoken text; a table gives the text per language (`{ default = "...", de = "..." }`). The first line's text is the label of the option.
---@field sound? VoiceVariants Voice of a `Say` line (spoken while the subtitle shows; the line lasts as long as the audio) or, on a `{ command = "SaySound", Speaker = ... }` line, its sound (the language is chosen when the
---option is created). Without `sound`, files registered for the text id `<option name>_<line number>` (`risen.voice.load_dir`) are used. A file that cannot be used makes `create` return `nil, reason` before anything is created.
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
---@field kind? string `"mesh"`, `"material"`, `"image"`, `"actor"`, `"motion"`, `"collision"` or `"sound"`.
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

---Name mapping: `"It_Wpn_1H_Club.xmsh"` -> `"It_Wpn_1H_Club._xmsh"`, `.tga/.dds/.png/.ximg` -> `._ximg`, `.xmat` -> `._xmat`, `.xac` -> `._xmac`, `.wav/.ogg/.mp3` -> `._xsnd`; an already compiled name stays.
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

---Compiles an audio file (Ogg Vorbis, WAV or MP3; recognised by content) into the engine's sound resource `<name>._xsnd` below `<Risen>\bin\spass\generated\<mod>\voice` and mounts it,
---so that `name` works as a `SoundName` (dialogue command `SaySound`), for `risen.voice.say` or as a shadow of a shipped sound. `name` is a bare file name (`"Spass_Hello.wav"`; the extension
---is only a label). An identical file is not written again. Returns `nil, reason` (naming the file) for an unusable one.
---@param name string
---@param source string|{file: string}|{data: string} File name relative to the mod directory, or audio bytes.
---@return SoundImportInfo|nil info
---@return string|nil reason
function assetsApi.import_sound(name, source) end
---What the engine and FMOD made of a sound resource: `state` (3 = loaded: a sound loads when it is played the first time), `duration_ms` (the resource's Duration property: for a shipped name
---that a loose file shadows it stays the shipped value), `flags`, `fmod` (FMOD created a playable sound), `fmod_length_ms`, `channels`, `bits`, `sample_rate`, `type` (FMOD sound type:
---10 Ogg Vorbis, 9 MPEG, 15 WAV). `nil` when the engine does not know the name.
---@param name string The raw name, `"Spass_Hello.wav"`.
---@return {state: integer, duration_ms?: number, flags?: integer, fmod?: boolean, fmod_length_ms?: number, channels?: integer, bits?: integer, sample_rate?: number, type?: integer}|nil
function assetsApi.sound(name) end

---Spoken dialogue lines and sounds (docs/RE.md §28, docs/LUA_API.md "Voice lines", `mods/example_voice`).
---
---A spoken line is a `Say` command; the engine asks for the sound of its text id (`<option name>_<line number>` for `risen.dialog.create`) in the voice
---language of the game (`VoiceLanguage` in the options). `risen.voice` registers audio files (Ogg Vorbis, WAV, MP3; compiled into the engine's own sound format)
---for such text ids, one per language, and the line then lasts as long as the audio. The variant that plays is: the voice language, else `default`, else English,
---else the first by language name.
---@class RisenVoiceApi
local voiceApi = {}

---A sound given as a file name relative to the mod directory (`"voice/hello.ogg"`), `{ file = "voice/hello.ogg" }` or audio bytes made by Lua (`{ data = wav_bytes }`).
---@alias VoiceSource string|{file: string}|{data: string}
---One sound for every language (a `VoiceSource`) or a table of languages (`en`/`English`, `de`/`German`, `fr`, `ru`, ... or the engine's names) with an optional `default`.
---@alias VoiceVariants VoiceSource|table<string, VoiceSource>

---What `risen.assets.import_sound` and `risen.voice.set` report about a compiled sound.
---@class SoundImportInfo
---@field name string Raw sound name the engine is given (`INFO_..._%_SPASS.wav`, `Spass_Hello.wav`).
---@field compiled string Compiled file name (`...._xsnd`) in `<Risen>\bin\spass\generated\<mod>\voice`.
---@field format string `"ogg"`, `"wav"` or `"mp3"` (recognised by content, not by the file name).
---@field channels integer
---@field sample_rate integer
---@field bits? integer Bits per sample of a WAV.
---@field duration_ms number Length of the audio in milliseconds.
---@field bytes number Size of the source audio.
---@field written boolean `false` when an identical file was already there (nothing was written).

---A registered voice entry.
---@class VoiceEntry
---@field key string
---@field mod string Mod id that registered it.
---@field requests integer How many times the engine asked for this key.
---@field variants table<string, {title: string, compiled: string, file: string, duration_ms: number}> By language name (`"English"`, `"German"`, ..., `"default"`).

---The last registered key the engine asked a sample title for.
---@class VoiceLast
---@field key string
---@field title string
---@field language string The variant that was chosen.

---Checks a sound or a table of languages without writing anything (every file is read and recognised). `true`, or `nil, reason` naming the file and the problem.
---@param variants VoiceVariants
---@return boolean|nil ok
---@return string|nil reason
function voiceApi.check(variants) end

---Registers audio for a dialogue text id (`"<option name>_<line number>"` for `risen.dialog.create`; any id the engine speaks: shipped ids work too). Validates all files
---first and returns `nil, reason` without changing anything; compiles each file below `<Risen>\bin\spass\generated\<mod>\voice` (the same audio is not written twice).
---Calling it again replaces the entry. The registry lives in the process: register on `load.before` and `session.start` like dialogues.
---@param key string
---@param variants VoiceVariants
---@return {key: string, variants: table<string, SoundImportInfo>}|nil result
---@return string|nil reason
function voiceApi.set(key, variants) end

---Registers every audio file below a directory of the mod: `<dir>/<language>/<key>.<ogg|wav|mp3>` is a variant of `<key>`, `<dir>/<key>.<ext>` the default. Language folders are
---engine names or codes (`English`, `German`, `de`, ...). Returns the number of keys and files, or `nil, reason` (a file that cannot be used names itself).
---@param dir string Relative to the mod directory, e.g. `"voice"`.
---@return integer|nil keys
---@return integer|string files_or_reason
function voiceApi.load_dir(dir) end

---The voice language the game speaks (`"English"`, `"German"`, ...; the `VoiceLanguage` of the options).
---@return string
function voiceApi.language() end

---The engine's name of a language code or name: `"de"` -> `"German"`; unknown names are kept (first letter upper case).
---@param name string
---@return string
function voiceApi.normalize(name) end

---A registered voice entry.
---@param key string
---@return VoiceEntry|nil
function voiceApi.get(key) end

---The registered keys (lower case).
---@return string[]
function voiceApi.keys() end

---Forgets a key (the files stay).
---@param key string
---@return boolean removed
function voiceApi.remove(key) end

---The raw sound name (`....wav`) of the variant of `key` that plays in the current voice language: what the engine is given for the line, and what a `SaySound` command
---takes as `SoundName`.
---@param key string
---@return string|nil
function voiceApi.title(key) end

---The registered key the engine asked a sample title for most recently (the proof that a spoken line took our sound), or `nil`.
---@return VoiceLast|nil
function voiceApi.last() end

---How many sample titles the engine asked for since the start (every spoken line, shipped or not).
---@return integer
function voiceApi.title_calls() end

---Makes an NPC speak a sound outside of a dialogue (a "bark"): a registered key (variant of the current language) or the raw name of any sound the engine finds
---(`"Spass_Hello.wav"`, imported with `risen.assets.import_sound`). Returns `false` when the sound cannot be started. `opts.type`: engine output type, `0` NPC voice
---(default), `2` the player's voice, `3` comment/sound command.
---@param entity Entity
---@param key_or_sound string
---@param opts? {type?: integer}
---@return boolean started
function voiceApi.say(entity, key_or_sound, opts) end

---Whether the entity is playing a speech sound right now.
---@param entity Entity
---@return boolean
function voiceApi.speaking(entity) end

---Stops the speech sound of an entity.
---@param entity Entity
---@return boolean ok
function voiceApi.stop(entity) end

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
---@field spell RisenSpellApi
---@field recipe RisenRecipeApi
---@field interact RisenInteractApi
---@field input RisenInputApi
---@field hud RisenHudApi
---@field quest RisenQuestApi
---@field dialog RisenDialogApi
---@field script RisenScriptApi
---@field gamevar RisenGamevarApi
---@field world RisenWorldApi
---@field loc RisenLocApi
---@field save RisenSaveApi
---@field vfs RisenVfsApi
---@field assets RisenAssetsApi
---@field voice RisenVoiceApi
---@field debug RisenDebugApi
---@field engine RisenEngineApi
local risen_ = {}

---Why an inventory changed (argument `cause` of `inventory.added/removed/changed`): `"script"` (CreateItems/DeleteItems/transfers made by the game's
---scripts or a mod: `risen.item.give/take/transfer`, quest rewards, ...), `"dialogue"` (a dialogue `Give`/`CreateItem`/`DestroyItem` command),
---`"pickup"` (an item taken from the ground), `"loot"` (corpse/chest), `"trade"`, `"drop"`, `"consume"` (food, potions), `"crafting"` (the ingredients taken and the result
---made by a crafting station or `risen.recipe.craft`: `PSInventory::Synthesize`), `"other"` (a change no hooked engine operation announced; only the creation or deletion of
---a stack is seen then). The temporary copies the animations hold in the hands (stack type 2: the anvil's hammer, the recipe, the result) are neither counted nor reported.
---@alias InventoryCause "script"|"dialogue"|"pickup"|"loot"|"trade"|"drop"|"consume"|"crafting"|"other"

---Registers an event handler. Higher priority runs first. Handlers that raise are logged and skipped.
---
---Engine events: `session.start(mode)` (`"new"`, `"load"`, `"other"`), `session.stop()`, `world.changed(name)`,
---`save.before(name)`, `save.after(name, ok)`, `load.before(name)` (runs before the engine reads quests and infos),
---`load.after(name, ok)`, `quest.status_changed(quest, status, previous)`, `info.started(info_name, owner)`, `info.finished(info_name, owner)`.
---
---Gameplay events (docs/LUA_API.md "Gameplay events"): they are **queued** when the engine does the thing and dispatched at the end of the same frame,
---right before `tick`, so a handler may call any API (also on the same inventory or entity) without disturbing the engine code that was running.
---They are produced while a game session runs, and not for the NPC inventories the engine fills right after a session started (see `world.ready`).
---`world.ready(mode)` (the player exists), `inventory.added/removed(owner, template, count, total, cause, other)`,
---`inventory.changed(owner, template, delta, total, cause, other)`, `item.picked_up(owner, template, amount)`,
---`item.equipped/unequipped(owner, template, slot)`, `item.consumed(owner, template, count)`, `entity.died(entity, killer, cause)`,
---`entity.defeated(entity, attacker)`, `npc.killed(entity, killer)`, `npc.defeated(entity, attacker)`, `entity.damaged(entity, attacker, damage, raw)`,
---`entity.spawned(entity, template)`, `entity.destroyed(name, kind, template)`, `crafting(crafter, recipe, result, count, ingredients)` (once per craft, after the `inventory.*`
---events of it; `ingredients` is `{ { item = "It_IronOre", count = 2 }, ... }` for the whole `count`).
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
---@overload fun(event: "world.ready", handler: fun(mode: "new"|"load"|"other"), priority?: integer): EventHandle
---@overload fun(event: "npc.killed", handler: fun(entity: Entity|nil, killer: Entity|nil), priority?: integer): EventHandle
---@overload fun(event: "npc.defeated", handler: fun(entity: Entity|nil, attacker: Entity|nil), priority?: integer): EventHandle
---@overload fun(event: "entity.died", handler: fun(entity: Entity|nil, killer: Entity|nil, cause: "combat"|"script"|"cheat"|"other"), priority?: integer): EventHandle
---@overload fun(event: "entity.defeated", handler: fun(entity: Entity|nil, attacker: Entity|nil), priority?: integer): EventHandle
---@overload fun(event: "entity.damaged", handler: fun(entity: Entity|nil, attacker: Entity|nil, damage: integer, raw: integer), priority?: integer): EventHandle
---@overload fun(event: "entity.spawned", handler: fun(entity: Entity|nil, template: string), priority?: integer): EventHandle
---@overload fun(event: "entity.destroyed", handler: fun(name: string, kind: "npc"|"player"|"item"|"other", template: string), priority?: integer): EventHandle
---@overload fun(event: "input.action", handler: fun(ev: InputActionEvent): boolean|nil, priority?: integer): EventHandle
---@overload fun(event: "crafting", handler: fun(crafter: Entity|nil, recipe: string, result: string, count: integer, ingredients: { item: string, count: integer }[]), priority?: integer): EventHandle
---@overload fun(event: "inventory.added", handler: fun(owner: Entity|nil, template: string, count: integer, total: integer, cause: InventoryCause, other: Entity|nil), priority?: integer): EventHandle
---@overload fun(event: "inventory.removed", handler: fun(owner: Entity|nil, template: string, count: integer, total: integer, cause: InventoryCause, other: Entity|nil), priority?: integer): EventHandle
---@overload fun(event: "inventory.changed", handler: fun(owner: Entity|nil, template: string, delta: integer, total: integer, cause: InventoryCause, other: Entity|nil), priority?: integer): EventHandle
---@overload fun(event: "item.equipped", handler: fun(owner: Entity|nil, template: string, slot: integer), priority?: integer): EventHandle
---@overload fun(event: "item.unequipped", handler: fun(owner: Entity|nil, template: string, slot: integer), priority?: integer): EventHandle
---@overload fun(event: "item.consumed", handler: fun(owner: Entity|nil, template: string, count: integer), priority?: integer): EventHandle
---@overload fun(event: "info.started", handler: fun(info_name: string, owner: string), priority?: integer): EventHandle
---@overload fun(event: "info.finished", handler: fun(info_name: string, owner: string), priority?: integer): EventHandle
---@overload fun(event: "item.picked_up", handler: fun(owner: Entity|nil, template: string, amount: integer), priority?: integer): EventHandle
function risen_.on(event, handler, priority) end

---Removes a handler.
---@param handle EventHandle
function risen_.off(handle) end

---The options of the calling mod: `defaults` is a table of default values that also fixes each option's type; the file `<mod dir>/config.toml` overrides them (an integer fits a number
---default, a nested table merges). A wrong type, an unknown key or a parse error is ignored and listed in the second result (and in `spass.log`); a missing file is no problem.
---`defaults` must not contain `nil` options (use `""`). The defaults table itself is not modified.
---@generic T: table
---@param defaults T
---@return T options
---@return string[] problems
function risen_.config(defaults) end

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
