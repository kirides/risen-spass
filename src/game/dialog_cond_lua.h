#pragma once

// Dialogue conditions beyond quest state (M9, docs/RE.md section 34, docs/LUA_API.md "Dialogue conditions"): `risen.dialog.create{ when = <condition> }` takes the condition vocabulary of
// conditional spawning (`risen.cond`: quest, item, var, event, skill, guild, dead, alive, fn, all, any, negate) and makes it the engine's own business where the engine has an equivalent.
//
//   * Engine conditions: an info owns a sequence of gCInfoCondition objects (`gCInfo::InsertCondSeqItem`) that must ALL hold on top of its ConditionType. `ItemAmount`, `PlayerKnows` /
//     `PlayerNotKnows` (a game variable exists), `SkillValue`, `NPCStatus` (dead / alive), `QuestStatus` cover quest / item / event / skill / dead / alive leaves and the negation of some of them, and
//     `all{}` is the sequence itself. They are evaluated natively, with no Lua involved.
//   * Everything else (`any`, `negate` of the rest, `var` with a value, `guild`, `fn`, a quest status without an engine twin, an item of a named holder) is ONE `gCInfoConditionScript` at the end of the sequence:
//     the engine calls a script registered with `risen.script.define` (the script bridge swaps the function slot), which evaluates the remaining condition in Lua with `risen.cond.test`. It runs only when
//     the engine looks at the option (when it builds the offered list for a conversation, `Info:available()`, `risen.dialog.has_infos`), and the engine's own conditions in front of it can short-circuit it.
//   * An error in a Lua condition is a warning once and counts as false: the option is not offered.
//   * `engine = { { class = "OwnerNearEntity", Entity = "PC_Hero" }, ... }` adds raw engine conditions by class name and property (the ones with no vocabulary entry: OwnerNearEntity, OwnerInArea, TeachEnabled, ...).
// The info is created by the shared `risen.dialog.create` first (so the voice wrapper and the validation keep working), the conditions are attached right after, in the same frame. When a condition cannot be attached
// the option is marked as already given (never offered) and `create` returns nil + reason.
// Arguments: the per-mod `risen` table and the mod id (installed after risen.cond).
namespace spass::game {

inline const char* kDialogCondChunk = R"LUA(
local api, modid = ...
local cond = api.cond
local log = api.log

local PLAYER = "PC_Hero"        -- the unique name of the player's entity: the engine's item / status conditions name their entity
local inner = api.dialog        -- the per-mod table of risen.dialog (the voice wrapper): its `create` is the one this chunk wraps
local own = setmetatable({}, { __index = inner })
api.dialog = own

local plans = {}                -- info name -> the plan it was created with

-- The properties of every class of engine condition that a spec may set (the reflection of the classes: docs/RE.md section 34). `Skill` and `Amount` are the members of the nested gCSkillValue.
local CLASSES = {
    ItemAmount      = { Owner = true, Item = true, ItemAmount = true },
    NPCStatus       = { SecondaryNPC = true, SecondaryNPCStatus = true },
    OwnerInArea     = { AreaPropertyName = true },
    OwnerNearEntity = { Entity = true },
    PlayerKnows     = { PlayerKnows = true },
    PlayerNotKnows  = { PlayerNotKnows = true },
    QuestStatus     = { Quest = true, CondType = true, IsCurrentStatus = true },
    Script          = { ScriptName = true, Self = true, Other = true, Param = true },
    SkillValue      = { Entity = true, CompareOperation = true, Skill = true, Amount = true },
    TeachEnabled    = { TeachEnabled = true },
    CanTeachSkill   = { Skill = true, Amount = true },
}
local CLASS_LIST = "ItemAmount, NPCStatus, OwnerInArea, OwnerNearEntity, PlayerKnows, PlayerNotKnows, QuestStatus, Script, SkillValue, TeachEnabled, CanTeachSkill"
local COMPARE = { ["=="] = "Equal", ["~="] = "NotEqual", [">="] = "GreaterEqual", ["<="] = "LessEqual", [">"] = "Greater", ["<"] = "Less" }
local INVERSE = { ["=="] = "~=", ["~="] = "==", [">="] = "<", ["<="] = ">", [">"] = "<=", ["<"] = ">=" }
-- The quest states the engine has a condition for (gEInfoCondType_*); the others (Open, Obsolete, None, a list of states) are evaluated in Lua.
local QUEST_NATIVE = { Running = true, Success = true, Failed = true, Cancelled = true, Won = true, Lost = true }

local function fail(what, msg)
    local text = what .. ": " .. msg
    log.warn(text)
    return nil, text
end

-- ---- compiling a condition to engine conditions + a remainder ---------------------------------------------------------------------------------------------------------------------------
local function entity_for(who, owner)
    if who == "player" then return PLAYER end
    if who == "npc" then return owner end
    return nil
end

local function single(set)
    local only
    for k in pairs(set) do
        if only then return nil end
        only = k
    end
    return only
end

-- The engine condition that says what a leaf says, or nil. `negated`: what the negation of the leaf says.
local function native_of(n, owner, negated)
    local k = n.kind
    if k == "quest" then
        local status = single(n.set)
        if negated or not status or not QUEST_NATIVE[status] then return nil end
        return { class = "QuestStatus", props = { Quest = n.quest, CondType = "gEInfoCondType_" .. status, IsCurrentStatus = "true" } }
    elseif k == "item" then
        local holder = entity_for(n.holder, owner)
        if not holder then return nil end
        if negated then
            if n.count ~= 1 then return nil end          -- "fewer than n" has no engine form, "none" (ItemAmount 0) has
            return { class = "ItemAmount", props = { Owner = holder, Item = n.template, ItemAmount = "0" } }
        end
        return { class = "ItemAmount", props = { Owner = holder, Item = n.template, ItemAmount = tostring(n.count) } }
    elseif k == "event" then
        return { class = negated and "PlayerNotKnows" or "PlayerKnows", props = { [negated and "PlayerNotKnows" or "PlayerKnows"] = n.var } }
    elseif k == "skill" then
        local who = n.who == "player" and "player" or n.who == "npc" and owner or n.who
        local op = negated and INVERSE[n.op] or n.op
        return { class = "SkillValue", props = { Entity = who, CompareOperation = "gECompareOperation_" .. COMPARE[op] }, skill = { member = "SkillValue", enum = n.engine, amount = n.value } }
    elseif k == "dead" or k == "alive" then
        if negated then return nil end
        -- a persistent NPC may be dead without a corpse (its record says so): only Lua knows
        if k == "dead" and api.npc.persistent_get and api.npc.persistent_get(n.who) then return nil end
        return { class = "NPCStatus", props = { SecondaryNPC = n.who, SecondaryNPCStatus = k == "dead" and "gEInfoNPCStatus_Dead" or "gEInfoNPCStatus_Alive" } }
    end
    return nil
end

local function collect(n, owner, natives, rest)
    if n.kind == "all" then
        for _, item in ipairs(n.items) do collect(item, owner, natives, rest) end
        return
    end
    local spec
    if n.kind == "not" then spec = native_of(n.item, owner, true) else spec = native_of(n, owner, false) end
    if spec then natives[#natives + 1] = spec else rest[#rest + 1] = n end
end

local function props_text(spec)
    local keys = {}
    for k in pairs(spec.props) do keys[#keys + 1] = k end
    table.sort(keys)
    local parts = {}
    for _, k in ipairs(keys) do parts[#parts + 1] = k .. "=" .. tostring(spec.props[k]) end
    if spec.skill then parts[#parts + 1] = ("%s=%s %d"):format(spec.skill.member, spec.skill.enum, spec.skill.amount) end
    return ("%s{%s}"):format(spec.class, table.concat(parts, ", "))
end

-- A raw engine condition of spec.engine: { class = "OwnerNearEntity", Entity = "PC_Hero" } (SkillValue / CanTeachSkill: Skill = "Strength", Amount = 5; CompareOperation: "GreaterEqual" or the enum name)
local function raw_of(item, index)
    if type(item) ~= "table" or type(item.class) ~= "string" then return nil, ("engine[%d]: expected { class = ..., <property> = ... }"):format(index) end
    local known = CLASSES[item.class]
    if not known then return nil, ("engine[%d]: unknown class '%s' (%s)"):format(index, item.class, CLASS_LIST) end
    local spec = { class = item.class, props = {} }
    for key, value in pairs(item) do
        if key ~= "class" then
            if not known[key] then return nil, ("engine[%d]: %s has no property '%s'"):format(index, item.class, tostring(key)) end
            if type(value) ~= "string" and type(value) ~= "number" and type(value) ~= "boolean" then return nil, ("engine[%d]: '%s' must be a string, number or boolean"):format(index, key) end
            if key ~= "Skill" and key ~= "Amount" then spec.props[key] = tostring(value) end
        end
    end
    if item.class == "SkillValue" or item.class == "CanTeachSkill" then
        local enum = type(item.Skill) == "string" and api.balance.skill_enum(item.Skill)
        if not enum then return nil, ("engine[%d]: Skill must be a skill name (risen.balance.skills())"):format(index) end
        if type(item.Amount) ~= "number" or item.Amount ~= math.floor(item.Amount) then return nil, ("engine[%d]: Amount must be an integer"):format(index) end
        spec.skill = { member = item.class == "SkillValue" and "SkillValue" or "TeachSkill", enum = enum, amount = item.Amount }
        local op = spec.props.CompareOperation
        if op and not op:find("^gECompareOperation_") then spec.props.CompareOperation = "gECompareOperation_" .. op end
    end
    return spec
end

local function script_name(name)
    return ("SPASS_Cond_%s_%s"):format(modid:gsub("[^%w_]", "_"), name)
end

-- compile(when, owner, engine_items, name) -> plan | nil, reason
-- plan = { natives = { { class, props, skill }, ... }, rest = condition | nil, text = { engine = { ... }, lua = "..." | nil } }
local function compile(when, owner, engine_items, name)
    local natives, rest = {}, {}
    if when ~= nil then
        local node, why = cond.normalize(when)
        if not node then return nil, "when: " .. why end
        collect(node, owner, natives, rest)
    end
    for i, item in ipairs(engine_items or {}) do
        local spec, why = raw_of(item, i)
        if not spec then return nil, why end
        natives[#natives + 1] = spec
    end
    local residual
    if #rest == 1 then residual = rest[1] elseif #rest > 1 then residual = cond.all(rest) end
    local plan = { natives = natives, rest = residual, text = { engine = {}, lua = residual and residual.text or nil } }
    for i, spec in ipairs(natives) do plan.text.engine[i] = props_text(spec) end
    plan.script = residual and script_name(name or "condition") or nil
    return plan
end

-- risen.dialog.compile(when [, owner]) -> { engine = { "ItemAmount{...}", ... }, lua = "any(...)" | nil } | nil, reason: what create would attach: the engine conditions and the description
-- of what is left for the Lua script (nil: nothing). Changes nothing.
function own.compile(when, owner)
    local plan, why = compile(when, owner or "npc", nil, nil)
    if not plan then return nil, why end
    return plan.text
end

-- risen.dialog.describe(name) -> the same table for an option this mod created with `when` / `engine` (nil for others)
function own.describe(name)
    local plan = plans[name]
    return plan and plan.text or nil
end

-- ---- attaching the plan to an info -------------------------------------------------------------------------------------------------------------------------------------------------------
local warned = {}
local function define_script(plan, name, owner)
    local rest = plan.rest
    api.script.define(plan.script, function()
        local r = cond.test(rest, { npc = owner, name = name, level = "warn" })
        if r == nil then
            if not warned[name] then
                warned[name] = true
                log.warn(("dialog '%s': a Lua condition could not be evaluated (see the warning above); the option is not offered"):format(name))
            end
            return 0
        end
        return r and 1 or 0
    end)
end

local function attach(info, plan, owner)
    local function add(spec)
        local idx, why = info:add_condition(spec.class)
        if not idx then return nil, why end
        for key, value in pairs(spec.props) do
            if not info:set_condition_property(idx, key, value) then return nil, ("%s has no property '%s'"):format(spec.class, key) end
            local back = info:get_condition_property(idx, key)
            if type(back) ~= "string" or back:lower() ~= value:lower() then
                return nil, ("%s.%s: the engine did not take '%s' (it reads '%s')"):format(spec.class, key, value, tostring(back))
            end
        end
        if spec.skill and not info:set_condition_skill(idx, spec.skill.member, spec.skill.enum, spec.skill.amount) then
            return nil, ("%s: the engine did not take the skill %s"):format(spec.class, spec.skill.enum)
        end
        return idx
    end
    for _, spec in ipairs(plan.natives) do
        local idx, why = add(spec)
        if not idx then return false, why end
    end
    if plan.rest then
        local idx, why = add({ class = "Script", props = { ScriptName = plan.script, Self = owner, Other = "player", Param = "0" } })
        if not idx then return false, why end
    end
    return true
end

-- risen.dialog.create(spec): the shared function plus `when` (a condition of risen.cond, or a function(npc, player) -> boolean) and `engine` (raw engine conditions): all of them must hold, together with
-- `condition` / `quest`. Idempotent like the shared function: an option that exists is returned as it is.
local function copy_without(spec)
    local c = {}
    for k, v in pairs(spec) do
        if k ~= "when" and k ~= "engine" then c[k] = v end
    end
    return c
end

function own.create(spec)
    if type(spec) ~= "table" or (spec.when == nil and spec.engine == nil) then return inner.create(spec) end
    if type(spec.name) ~= "string" or spec.name == "" then return inner.create(spec) end   -- the shared function names the problem
    if spec.engine ~= nil and type(spec.engine) ~= "table" then return fail("dialog.create", "'engine' must be a list of { class = ..., ... }") end
    local owner = type(spec.owner) == "string" and spec.owner or nil
    if not owner then return inner.create(spec) end
    local plan, why = compile(spec.when, owner, spec.engine, spec.name)
    if not plan then return fail("dialog.create", why) end

    local existing = own.get(spec.name)
    if existing then return existing end
    -- the shared function validates the rest of the spec before anything is created
    local info, err = inner.create(copy_without(spec))
    if not info then return nil, err end
    if plan.rest then
        local ok, e = pcall(define_script, plan, spec.name, owner)
        if not ok then
            info:set_given(true)
            return fail("dialog.create", ("'%s': the condition script could not be registered (%s); the option is not offered"):format(spec.name, tostring(e)))
        end
    end
    local ok, e = attach(info, plan, owner)
    if not ok then
        info:set_given(true)     -- an option whose conditions are missing must not be offered unconditionally
        info:clear_conditions()
        return fail("dialog.create", ("'%s': %s; the option is not offered"):format(spec.name, tostring(e)))
    end
    plans[spec.name] = plan
    return info
end
)LUA";

}  // namespace spass::game
