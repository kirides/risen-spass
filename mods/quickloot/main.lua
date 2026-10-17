-- Quick Loot (standalone): take what you look at with one key press, without walking over, kneeling, the loot window or any animation.
-- Needs nothing but the mod layer. See README.md and docs/QUICKLOOT.md. Options: config.toml (risen.config).
--
--   items on the ground   risen.item.pick_up  (the engine's own InsertStack, what Interact_Player_TakeItem calls) and the pick-up effect with the material's sound (`pickup_sound`)
--   dead / knocked-out    the engine's script AutoLoot (the animation-less loot of the game: only what the loot filter InvFilter_Loot lets through)
--   chests, cupboards,    the same AutoLoot; a locked chest is opened with the key of the inventory first (Status := Unlocked, what the `Unlock` script does)
--   sarcophagi
-- Dead bodies are the humans', the washed-up bodies of the beach (PreInteract_Player_Loot_Comment) and monster/animal carcasses (their species' can script puts the trophies in).
-- Declined (nothing happens, the use key still runs the engine's interaction): anything else (traps, quest sarcophagi, mining, doors, living characters), locks that need a lockpick or whose unlocking runs a script, empty containers, targets too far, a weapon drawn or any other player
-- state than walking around.
--
-- Witnesses (`witness_mode`): taking an item or opening a chest/cupboard is theft to the game's NPCs. The animated path calls the engine's perception
-- broadcast of AssessTheft (every NPC is asked; the scripts test the security zone, the distance and the line of sight; the NPC comments, watches, threatens...).
-- Quick loot calls the very same broadcast before it takes ("assess", the default); "veto" declines instead when a human who sees the player is within
-- `witness_veto` units; "ignore" does nothing (thieving is undetectable). Corpses and sarcophagi are no theft in the game either.
local policy = require("policy")

local DEFAULTS = {
    enabled = true,
    key = "SecondaryAction",
    modifier = "none",
    items = true, corpses = true, chests = true, keys = true, print = true, pickup_sound = true, pickup_sound_gap = 600,
    hold_repeat = true, repeat_ms = 80,
    witness_mode = "assess", witness_veto = 1000, max_distance = 450,
}
local cfg, problems = risen.config(DEFAULTS)
for _, p in ipairs(problems) do risen.log.warn("quickloot: " .. p) end
if not cfg.enabled then
    risen.log.info("quickloot: disabled by config.toml")
    return
end

local witness_mode, why_mode = policy.witness_mode(cfg.witness_mode)
if not witness_mode then
    risen.log.warn("quickloot: " .. why_mode .. ", using assess")
    witness_mode = "assess"
end
local have_broadcast = type(risen.ai) == "table" and type(risen.ai.broadcast) == "function"
if witness_mode == "assess" and not have_broadcast then
    risen.log.warn("quickloot: this mod layer has no risen.ai.broadcast (the engine's theft assessment): witness_mode falls back to veto")
    witness_mode = "veto"
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

-- witness_mode = "veto": a living human within witness_veto units who looks at the player (risen.world.can_see: field of view, range, nothing solid in between)
local function witnesses(player)
    if cfg.witness_veto <= 0 then return nil end
    for _, npc in ipairs(risen.entity.npcs(player, cfg.witness_veto)) do
        if npc ~= player then
            local species = npc:get_property("gCNPC_PS", "Species")
            local aimode = npc:get_property("gCScriptRoutine_PS", "AIMode")
            local down = aimode == "gEAIMode_Down" or aimode == "gEAIMode_Dead"
            local sees = nil
            if risen.world.can_see then sees = risen.world.can_see(npc, player, { range = cfg.witness_veto }) end
            if policy.vetoes(species, npc:is_dead(), down, npc:distance_to(player), cfg.witness_veto, sees) then return npc end
        end
    end
    return nil
end

-- witness_mode = "assess": the engine's own reaction, before the first thing is taken (as in the animated path: the item is still lying there, the chest still shut).
-- Returns true, or false and the reason when the engine could not be asked (nothing is taken then).
local function assess_theft(res, target)
    if witness_mode ~= "assess" then return true end
    local reacted, why = risen.ai.broadcast("AssessTheft", target)
    if reacted == nil then return false, "the theft assessment failed: " .. tostring(why) end
    res.assessed, res.reacted = true, reacted
    return true
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

-- The animated take plays one pick-up sound at a time (every take lasts ~1.3 s). Holding the key takes ~12 items a second and every sound plays to its end, so they would pile up
-- into seconds of noise: after one sound the next waits `pickup_sound_gap` milliseconds (the items are still taken).
local last_pickup_sound = -math.huge
local function pickup_sound(player, item)
    if not cfg.pickup_sound then return end
    local now = os.clock()
    if now - last_pickup_sound < cfg.pickup_sound_gap / 1000 then return end
    local material = item:get_property("gCItem_PS", "EffectMaterial")
    if material and material ~= "" and risen.effect.start("eff_item_" .. material .. "_transfer_01", player) then last_pickup_sound = now end
end

local function take_item(player, item)
    local tpl = item:template()
    local name = tpl and tpl:name() or item:name()
    local amount = item:item_amount() or 1
    -- the take animation's pick-up effect (Interact_Player_TakeItem starts it before the item is inserted): it plays the material's transfer sound, sfx_items_<material>_transfer_NN
    pickup_sound(player, item)
    local stack = risen.item.pick_up(player, item)
    if stack == nil then return false, "the engine refused the pick-up" end
    if cfg.print then risen.hud.item_log(name, amount) end
    risen.hud.tutorial_stop("takeitem")   -- the animated take ends this hint (Interact_Player_TakeItem)
    return true, name .. " x" .. tostring(amount)
end

local function take_corpse(player, body)
    local n = lootable_stacks(body)
    if n == 0 then return false, "nothing the loot filter lets through" end
    local taken = risen.script.call("AutoLoot", player, body)
    if taken > 0 then risen.hud.tutorial_stop("loot") end   -- PreInteract_Player_Loot ends this hint
    return taken > 0, ("%d stack(s)"):format(taken)
end

local LOCK_PROPS = { "Status", "Key", "Difficulty", "OnLockStatusChanged" }
local function take_container(player, box, before_take)
    local lock = nil
    if box:get_property("gCLock_PS", "Status") ~= nil then
        lock = {}
        for _, p in ipairs(LOCK_PROPS) do lock[p] = box:get_property("gCLock_PS", p) end
    end
    local plan, why = policy.lock_plan(lock, function(tpl) return risen.item.count(player, tpl) > 0 end)
    if plan == "needs lockpick" then return false, why end
    if plan == "key" and not cfg.keys then return false, "keys = false" end
    if lootable_stacks(box) == 0 then return false, "nothing to take" end
    if before_take then
        local ok, why2 = before_take()
        if not ok then return false, why2 end
    end
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
-- the hero's remark at a body that has one (the beach: "GamestartLoot"), as the game's script helper does it: the voice message of the player in his own voice,
-- not again while the same line is still playing, and the text as a game log line when the text language is not the voice language.
local function say_comment(player, svm)
    if not (risen.voice and risen.voice.say_svm) or risen.voice.speaking(player) then return false end
    if not risen.voice.say_svm(player, svm) then return false end
    local voice_lang = risen.voice.language and risen.voice.language()
    if voice_lang and risen.loc.language() ~= voice_lang then
        local voice = player:get_property("gCNPC_PS", "Voice")
        local text = voice and voice ~= "" and risen.loc.get(("SVM_%s_%s"):format(voice, svm))
        if text then risen.hud.log(text, 2) end
    end
    return true
end

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
    local theft = policy.assessed_as_theft(chosen)   -- the interactions in which the animated path asks the NPCs (items, chests, cupboards)
    if theft and witness_mode == "veto" then
        local w = witnesses(player)
        if w then return decline("seen by " .. w:name(), kind) end
    end

    local done, detail
    if kind == "item" then
        local ok3, why3 = assess_theft(res, target)
        if not ok3 then return decline(why3, kind) end
        done, detail = take_item(player, target)
    elseif kind == "corpse" then done, detail = take_corpse(player, target)
    else
        done, detail = take_container(player, target, theft and function() return assess_theft(res, target) end or nil)
    end
    res.handled, res.reason = done, detail
    local svm = done and policy.comment_of(chosen)
    if svm then res.comment = say_comment(player, svm) end
    trace("%s %s (%s): %s%s", done and "took" or "declined", target:name(), kind, tostring(detail), res.assessed and (res.reacted and " [an NPC reacted]" or " [assessed, nobody reacted]") or "")
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

risen.log.info(("quickloot: ready (key %s%s, items=%s corpses=%s chests=%s keys=%s, witnesses: %s)"):format(
    key_name, modifier == true and "" or (" + " .. modifier), tostring(cfg.items), tostring(cfg.corpses), tostring(cfg.chests), tostring(cfg.keys), witness_mode))
