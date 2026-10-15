-- Quick Loot: what may be taken without the engine's animation, decided from the target's interaction data (`risen.interact.info`).
--
-- The engine picks the interaction of the use key like this (gCInteraction_PS::BeginInteraction): the target holds one gCInteraction per user kind, each with
-- a CanInteract script, a PreInteract and an Interact script (AI functions that walk, play animations and open the loot window) and a PostInteract script.
-- The FIRST interaction of the player kind (gEInteractionType_Interact_Player) whose CanInteract script says yes is run. Quick loot only takes what that
-- interaction would do and that it knows to have no effect besides moving the items (RE.md section 31); everything else is declined, which means: nothing
-- happens and the normal use key still does the engine's full interaction.
local M = {}

M.PLAYER = "gEInteractionType_Interact_Player"

-- script names (docs/RE.md section 31): the interaction scripts of the plain things
local ITEM_INTERACT = "Interact_Player_TakeItem"
local CORPSE = { can = "CanInteract_Player_Loot", pre = "PreInteract_Player_Loot", interact = "Interact_Player_Loot" }
local CONTAINERS = {
    -- by interact script: which pre script and which can scripts it has when it is a plain container
    Interact_Player_Chest = { pre = "PreInteract_Player_Chest", can = { CanInteract_Player_Chest = true, CanInteract_Player_ChestPirateTreasure = true } },
    Interact_Player_Cupboard = { pre = "PreInteract_Player_Cupboard", can = { CanInteract_Player_Cupboard = true } },
    Interact_Player_Sarcophagus = { pre = "PreInteract_Player_Sarcophagus", can = { CanInteract_Player_Sarcophagus = true } },
}

-- The player interactions of an `info` table, in the engine's order.
function M.player_interactions(info)
    local out = {}
    for _, it in ipairs(info.interactions or {}) do
        if it.type == M.PLAYER then out[#out + 1] = it end
    end
    return out
end

-- kind of one interaction: "item" | "corpse" | "container" | nil (and a reason)
function M.kind_of(it)
    if it.interact == ITEM_INTERACT and it.pre == "" and it.post == "" then
        return "item"
    end
    if it.can == CORPSE.can and it.pre == CORPSE.pre and it.interact == CORPSE.interact and it.post == "" then
        return "corpse"
    end
    local c = CONTAINERS[it.interact]
    if c and it.pre == c.pre and c.can[it.can] and it.post == "" then
        return "container"
    end
    return nil, ("interaction %s/%s/%s is not a plain take/loot"):format(it.can, it.pre, it.interact)
end

-- Which of the interactions the engine would run: the first whose can script (empty = always) accepts. `can(name)` asks the script.
-- Returns the interaction, or nil.
function M.engine_choice(info, can)
    for _, it in ipairs(M.player_interactions(info)) do
        if it.can == "" or can(it.can) then return it end
    end
    return nil
end

-- can scripts that are known to answer without a HUD message: they may be asked for an interaction quick loot does not handle (a living character's
-- `Talk` comes before its `Loot`). Any other unknown can script could print ("the door is locked"), so such a target is declined unasked.
M.SILENT_CAN = { CanInteract_Player_Talk = true }

-- A lock as the engine stores it (gCLock_PS): returns "none" | "open" | "key" | "needs lockpick" and, for "key", why it must not be taken.
-- `props` = { Status, Key, Difficulty, OnLockStatusChanged } as text, `has(templateName)` -> bool.
function M.lock_plan(props, has)
    if props == nil or props.Status == nil then return "none" end
    if props.Status ~= "gELockStatus_Locked" then return "open" end
    local key = props.Key
    if key ~= nil and key ~= "" and key ~= "(no template)" and has(key) then
        if props.OnLockStatusChanged ~= nil and props.OnLockStatusChanged ~= "" then
            return "needs lockpick", "unlocking runs the script " .. props.OnLockStatusChanged
        end
        return "key"
    end
    return "needs lockpick", "locked and no key in the inventory"
end

-- Is this entity a living human that could see a theft? (`props` = species text, alive, down flags)
function M.is_witness(species, dead, down)
    return species == "gESpecies_Human" and not dead and not down
end

return M
