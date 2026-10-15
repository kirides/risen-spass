-- Quick Loot (standalone): take what you look at with one key press, without walking over, kneeling, the loot window or any animation.
-- Needs nothing but the mod layer. See README.md and docs/QUICKLOOT.md. Options: config.toml (risen.config).
--
--   items on the ground   risen.item.pick_up  (the engine's own InsertStack, what Interact_Player_TakeItem calls)
--   dead / knocked-out    the engine's script AutoLoot (the animation-less loot of the game: only what the loot filter InvFilter_Loot lets through)
--   chests, cupboards,    the same AutoLoot; a locked chest is opened with the key of the inventory first (Status := Unlocked, what the `Unlock` script does)
--   sarcophagi
-- Declined (nothing happens, the use key still runs the engine's interaction): anything else (traps, quest sarcophagi, mining, doors, living characters,
-- monster trophies), locks that need a lockpick or whose unlocking runs a script, empty containers, targets too far, a weapon drawn or any other player
-- state than walking around, witnesses within `witness_veto` units.
local policy = require("policy")

local DEFAULTS = {
    enabled = true,
    key = "SecondaryAction",
    modifier = "none",
    items = true, corpses = true, chests = true, keys = true, print = true,
    hold_repeat = true, repeat_ms = 80,
    witness_veto = 1000, max_distance = 450,
}
local cfg, problems = risen.config(DEFAULTS)
for _, p in ipairs(problems) do risen.log.warn("quickloot: " .. p) end
if not cfg.enabled then
    risen.log.info("quickloot: disabled by config.toml")
    return
end

local function trace(fmt, ...)
    if risen.trace.enabled("quickloot") then risen.trace("quickloot", fmt:format(...)) end
end

-- ---------------------------------------------------------------------------------------------------------------------------------------------
-- the key
-- ---------------------------------------------------------------------------------------------------------------------------------------------
local key_name = cfg.key
local is_action = risen.input.action_key(key_name) ~= nil      -- an engine action (events can be consumed) or a raw key (polled)
local known, why_key = risen.input.pressed(key_name)
if known == nil then
    risen.log.error(("quickloot: key '%s' is neither an action nor a key name (%s): the key press does nothing"):format(key_name, tostring(why_key)))
end
local MODS = { none = true, shift = "shift", ctrl = "ctrl", alt = "alt" }
local modifier = MODS[tostring(cfg.modifier):lower()]
if modifier == nil then
    risen.log.warn(("quickloot: modifier '%s' is not none/shift/ctrl/alt, using none"):format(tostring(cfg.modifier)))
    modifier = true
end
local function modifier_ok()
    if modifier == true then return true end
    return risen.input.modifiers()[modifier] == true
end

-- ---------------------------------------------------------------------------------------------------------------------------------------------
-- guards
-- ---------------------------------------------------------------------------------------------------------------------------------------------
-- Only while walking around with no weapon drawn: the player's state is PS_Normal / PS_Normal_Loop. A drawn weapon is PS_Melee*, PS_Bow*, PS_CrossBow*,
-- PS_Cast* / PS_Magic*; PS_Inventory*, PS_Talk*, ZS_Interact_* (an interaction runs), PS_Routine, PS_Gamestart, ... are other states.
local function player_ready(player)
    if player:is_dead() then return false, "the player is dead" end
    local state = player:state() or ""
    if state:sub(1, 9) ~= "PS_Normal" then return false, "player state " .. state end
    if risen.interact.current(player) ~= nil then return false, "an interaction runs" end
    local hud = risen.hud.state()
    if hud and hud.loot then return false, "the loot window is open" end
    return true
end

local function witnesses(player)
    if cfg.witness_veto <= 0 then return nil end
    for _, npc in ipairs(risen.entity.npcs(player, cfg.witness_veto)) do
        if npc ~= player then
            local species = npc:get_property("gCNPC_PS", "Species")
            local aimode = npc:get_property("gCScriptRoutine_PS", "AIMode")
            if policy.is_witness(species, npc:is_dead(), aimode == "gEAIMode_Down" or aimode == "gEAIMode_Dead") then return npc end
        end
    end
    return nil
end

-- ---------------------------------------------------------------------------------------------------------------------------------------------
-- the three ways to take something
-- ---------------------------------------------------------------------------------------------------------------------------------------------
local function lootable_stacks(container)
    local n = 0
    for i = 0, (container:stack_count() or 0) - 1 do
        if (risen.script.call("InvFilter_Loot", container, nil, i) or 0) > 0 then n = n + 1 end
    end
    return n
end

local function take_item(player, item)
    local tpl = item:template()
    local name = tpl and tpl:name() or item:name()
    local amount = item:item_amount() or 1
    local stack = risen.item.pick_up(player, item)
    if stack == nil then return false, "the engine refused the pick-up" end
    if cfg.print then risen.hud.item_log(name, amount) end
    return true, name .. " x" .. tostring(amount)
end

local function take_corpse(player, body)
    local n = lootable_stacks(body)
    if n == 0 then return false, "nothing the loot filter lets through" end
    local taken = risen.script.call("AutoLoot", player, body)
    return taken > 0, ("%d stack(s)"):format(taken)
end

local LOCK_PROPS = { "Status", "Key", "Difficulty", "OnLockStatusChanged" }
local function take_container(player, box)
    local lock = nil
    if box:get_property("gCLock_PS", "Status") ~= nil then
        lock = {}
        for _, p in ipairs(LOCK_PROPS) do lock[p] = box:get_property("gCLock_PS", p) end
    end
    local plan, why = policy.lock_plan(lock, function(tpl) return risen.item.count(player, tpl) > 0 end)
    if plan == "needs lockpick" then return false, why end
    if plan == "key" and not cfg.keys then return false, "keys = false" end
    if lootable_stacks(box) == 0 then return false, "nothing to take" end
    if plan == "key" then
        -- what the `Unlock` script does for a player with the key (the key stays in the inventory)
        if not box:set_property("gCLock_PS", "Status", "gELockStatus_Unlocked") or box:get_property("gCLock_PS", "Status") ~= "gELockStatus_Unlocked" then
            return false, "could not unlock"
        end
    end
    local taken = risen.script.call("AutoLoot", player, box)
    return taken > 0, ("%d stack(s)%s"):format(taken, plan == "key" and " (unlocked with the key)" or "")
end

-- ---------------------------------------------------------------------------------------------------------------------------------------------
-- one attempt on one target; returns result = { handled, kind, target, reason }
-- ---------------------------------------------------------------------------------------------------------------------------------------------
local function attempt(target, source)
    local res = { handled = false, source = source }
    local function decline(reason, kind)
        res.reason, res.kind = reason, kind
        trace("declined %s (%s): %s", target and target:name() or "-", source, reason)
        return res
    end
    local player = risen.entity.player()
    if not player or not target or target:position() == nil then return decline("no target") end
    res.target = target:name()
    local ok, why = player_ready(player)
    if not ok then return decline(why) end
    local dist = player:distance_to(target)
    if dist == nil or dist > cfg.max_distance then return decline(("too far (%s, limit %s)"):format(tostring(dist), tostring(cfg.max_distance))) end

    local info = risen.interact.info(target)
    if not info then return decline("not interactive") end
    -- the interaction the use key would run (its can scripts may print a HUD message, so lock problems are screened first, silently)
    local chosen = nil
    for _, it in ipairs(policy.player_interactions(info)) do
        local kind = policy.kind_of(it)
        local accept
        if kind == nil and it.can ~= "" and not policy.SILENT_CAN[it.can] then
            return decline("unknown interaction " .. it.interact .. " (its can script " .. it.can .. " may print a message: not asked)")
        end
        if it.can == "" then
            accept = true
        elseif kind == "container" and target:get_property("gCLock_PS", "Status") == "gELockStatus_Locked" then
            local lock = { Status = "gELockStatus_Locked", Key = target:get_property("gCLock_PS", "Key"), OnLockStatusChanged = target:get_property("gCLock_PS", "OnLockStatusChanged") }
            local plan = policy.lock_plan(lock, function(tpl) return risen.item.count(player, tpl) > 0 end)
            if plan == "needs lockpick" then return decline("locked: needs a lockpick or runs a script", "container") end
            accept = risen.script.call(it.can, player, target) ~= 0
        else
            accept = risen.script.call(it.can, player, target) ~= 0
        end
        if accept then chosen = it; break end
    end
    if not chosen then return decline("no interaction accepts the player") end
    local kind, why2 = policy.kind_of(chosen)
    if not kind then return decline(why2) end
    res.kind = kind
    if (kind == "item" and not cfg.items) or (kind == "corpse" and not cfg.corpses) or (kind == "container" and not cfg.chests) then
        return decline("switched off in config.toml", kind)
    end
    if kind ~= "corpse" then   -- taking from a corpse is no crime (AssessPlayerLooting); items and containers are
        local w = witnesses(player)
        if w then return decline("seen by " .. w:name(), kind) end
    end

    local done, detail
    if kind == "item" then done, detail = take_item(player, target)
    elseif kind == "corpse" then done, detail = take_corpse(player, target)
    else done, detail = take_container(player, target) end
    res.handled, res.reason = done, detail
    trace("%s %s (%s): %s", done and "took" or "declined", target:name(), kind, tostring(detail))
    return res
end

local function report(res)
    if res.target then risen.emit("quickloot.result", res) end
    return res.handled
end

-- try the focused target (the key)
local function try_focus(source)
    if not modifier_ok() then return false end
    return report(attempt(risen.interact.focus(), source))
end

-- programmatic: risen.emit("quickloot.try", entity) behaves like the key on that entity (used by tests and other mods)
risen.on("quickloot.try", function(target)
    if target then report(attempt(target, "api")) end
end)

-- ---------------------------------------------------------------------------------------------------------------------------------------------
-- input
-- ---------------------------------------------------------------------------------------------------------------------------------------------
local consuming = false   -- the engine does not see the action events of a press that quick loot used
local last_step = 0

if is_action then
    risen.on("input.action", function(ev)
        if ev.action:lower() ~= key_name:lower() then return nil end
        if ev.phase == "pressed" then
            last_step = 0
            consuming = try_focus("key")
            return consuming or nil
        elseif ev.phase == "held" then
            return consuming or nil
        elseif ev.phase == "released" then
            local c = consuming
            consuming = false
            return c or nil
        end
    end)
end

risen.on("tick", function()
    if not is_action and risen.input.was_pressed(key_name) then
        last_step = 0
        try_focus("key")
    end
    if cfg.hold_repeat and risen.input.pressed(key_name) then
        local step = math.floor(risen.input.held_for(key_name) * 1000 / math.max(cfg.repeat_ms, 20))
        if step > last_step then
            last_step = step
            if try_focus("repeat") and is_action then consuming = true end
        end
    else
        last_step = 0
    end
end)

risen.log.info(("quickloot: ready (key %s%s, items=%s corpses=%s chests=%s keys=%s)"):format(
    key_name, modifier == true and "" or (" + " .. modifier), tostring(cfg.items), tostring(cfg.corpses), tostring(cfg.chests), tostring(cfg.keys)))
