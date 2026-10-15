-- tracker.lua -- a small quest objective tracker (module of example_quest_tracker, loaded with require("tracker")).
--
-- One table per quest says HOW it completes; the tracker feeds all of them from the engine's events and finishes the quest:
--
--   { quest = "Q_A", kind = "talk",  info = "Some_Info_Name" }                 completes when that dialogue option finished playing
--   { quest = "Q_B", kind = "items", item = "It_Apple", count = 3 }             completes when the player CARRIES `count` more than at the start
--   { quest = "Q_C", kind = "kills", template = "Wolf", count = 5 }             completes after `count` deaths of entities made from that template
--
-- Progress lives in risen.save (key "tracker.<quest>"), so it is part of every savegame. The quest status itself is the engine's.
-- Each objective kind has ONE small handler (`talk_finished`, `items_changed`, `entity_killed`) fed by an event block and, where the engine has
-- no event yet, a polling block -- see the markers below. `tracker.on_progress(fn(quest, n, wanted))` and `tracker.on_complete(fn(quest))` register the mod's reactions.
local M = {}

local objectives = {}     -- quest name -> objective table
local counted = {}        -- quest name -> { [entity name] = true }: a death may be reported twice, count it once
local callbacks = {
    progress = function(quest, n, wanted) end,   -- set with M.on_progress(fn)
    complete = function(quest) end,              -- set with M.on_complete(fn)
}
function M.on_progress(fn) callbacks.progress = fn end
function M.on_complete(fn) callbacks.complete = fn end

local function key(quest) return "tracker." .. quest end
local function state(quest) return risen.save.get(key(quest), { n = 0, base = 0, done = false }) end

function M.add(objective) objectives[objective.quest] = objective end
function M.progress(quest) return state(quest).n end
function M.objective(quest) return objectives[quest] end

local function complete(quest)
    local s = state(quest)
    if s.done then return end
    s.done = true
    risen.save.set(key(quest), s)
    risen.quest.succeed(quest)
    callbacks.complete(quest)
end

local function advance(quest, n)
    local o, s = objectives[quest], state(quest)
    if s.done or n == s.n then return end
    s.n = n
    risen.save.set(key(quest), s)
    callbacks.progress(quest, n, o.count or 1)
    if n >= (o.count or 1) then complete(quest) end
end

-- Starts the quest and takes the baseline of the objective (items the player already carries do not count).
function M.start(quest)
    local o = assert(objectives[quest], "tracker: unknown quest " .. quest)
    local player = risen.entity.player()
    risen.save.set(key(quest), { n = 0, base = (o.kind == "items" and player) and risen.item.count(player, o.item) or 0, done = false })
    counted[quest] = {}
    risen.quest.open(quest)
    risen.quest.run(quest)
end

local function running(quest) return risen.quest.is_running(quest) and not state(quest).done end

-- ---- objective handlers: the logic, independent of how it is triggered ---------------------------------------------------------------
local function talk_finished(info_name)
    for quest, o in pairs(objectives) do
        if o.kind == "talk" and o.info == info_name and running(quest) then advance(quest, 1) end
    end
end

local function items_changed()
    local player = risen.entity.player()
    if not player then return end
    for quest, o in pairs(objectives) do
        if o.kind == "items" and running(quest) then
            advance(quest, math.max(0, risen.item.count(player, o.item) - state(quest).base))
        end
    end
end

local function entity_killed(entity)
    local template = entity:template()
    local tname, ename = template and template:name(), entity:name()
    if not (tname and ename) then return end
    for quest, o in pairs(objectives) do
        local seen = counted[quest]
        if o.kind == "kills" and o.template == tname and running(quest) and seen and not seen[ename] then
            seen[ename] = true
            advance(quest, state(quest).n + 1)
        end
    end
end

-- >>> EVENT BLOCK: the engine tells us.
risen.on("info.finished", function(info_name) talk_finished(info_name) end)   -- a dialogue option finished playing (any NPC)
risen.on("npc.killed", function(entity) if entity then entity_killed(entity) end end)   -- a real death
-- <<<

-- >>> POLLING BLOCK: no event exists for "the player's inventory changed" that also covers loot, trade and scripted gifts
--     (item.picked_up only covers pickups from the ground), so look every 20 frames. Delete it once such an event exists.
local frames = 0
risen.on("tick", function()
    frames = frames + 1
    if frames % 20 == 0 then items_changed() end
end)
risen.on("item.picked_up", function() items_changed() end)   -- instant feedback for the common case
-- <<<

-- After a load the Lua-side kill dedupe table is gone: quests that run keep their saved progress, counted names start empty.
risen.on("session.start", function() counted = {} for quest in pairs(objectives) do counted[quest] = {} end end)

return M
