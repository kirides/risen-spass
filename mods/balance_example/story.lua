-- story.lua -- the people: a smith with a voiced dialogue and a quest, an apprentice who appears when it is done, a watchman with behaviour written in Lua.
--
--   Brogar the Smith   risen.npc.persistent: exists from the start, in every game, once (never twice after a load). His dialogue options are created in `define`,
--                      which runs on load.before and on every session.start (the engine restores quest state and "option used" flags only into objects that already exist).
--                      "Any work for me?" starts the quest: bring him 3 iron ore.  "Here is the ore" is offered by the ENGINE only while the quest runs AND the player carries the ore
--                      (a dialogue condition, risen.cond); it takes the ore, gives the Ember Club and finishes the quest.  "Look at my sword" appears when the player carries the
--                      sword from the recipe.  His first reply is spoken: a tone made by Lua (a real mod ships ogg/wav/mp3 files, see mods/example_voice).
--   Tilda              risen.spawn.when: does not exist until the quest has SUCCEEDED, then she is there for good (remembered per savegame, restored after a load, never made
--                      twice). She has a daily routine: guards the forge by day, looks into the fire in the evening, sleeps on the ground at night.
--   Watchman Rolf      risen.npc.persistent with a Lua AI state (`risen.ai.state`): turns to the player and warns him once when he comes close. `persist = true`
--                      makes the layer start the state again after every load (a Lua state is not part of a savegame).
local M = {}

M.BROGAR, M.TILDA, M.WATCHMAN = "BX_Brogar", "BX_Tilda", "BX_Watchman"
M.QUEST = "Q_BX_Ore"
M.ORE_NEEDED = 3

local balance = require("balance")
local C = risen.cond

-- what the live check looks at
M.stats = { warnings = 0, tilda_fired = {} }

-- ---- a spoken line: a tone as a WAV file, made by Lua (16 bit mono, 22.05 kHz) -------------------------------------------------------------------------------
local function le(n, bytes)
    local t = {}
    for i = 1, bytes do
        t[i] = string.char(n % 256)
        n = math.floor(n / 256)
    end
    return table.concat(t)
end

local function tone_wav(freq, seconds)
    local rate = 22050
    local n = math.floor(rate * seconds)
    local samples = {}
    for i = 0, n - 1 do
        local env = math.min(1, i / 200, (n - i) / 400)
        local v = math.floor(10000 * env * math.sin(2 * math.pi * freq * i / rate))
        samples[#samples + 1] = le(v < 0 and v + 65536 or v, 2)
    end
    local data = table.concat(samples)
    return "RIFF" .. le(36 + #data, 4) .. "WAVEfmt " .. le(16, 4) .. le(1, 2) .. le(1, 2) .. le(rate, 4) .. le(rate * 2, 4) .. le(2, 2) .. le(16, 2) .. "data" .. le(#data, 4) .. data
end

-- ---- what the dialogue lines do ------------------------------------------------------------------------------------------------------------------------------
risen.script.define("BX_Quest_Start", function()
    risen.quest.open(M.QUEST)
    risen.quest.run(M.QUEST)
    risen.quest.add_log(M.QUEST, ("Brogar needs %d pieces of iron ore for the forge. Bring them to him."):format(M.ORE_NEEDED))
    return 0
end)
risen.script.define("BX_Quest_Reward", function()
    local player = risen.entity.player()
    if player then risen.item.give(player, balance.CLUB, 1) end
    risen.quest.add_log(M.QUEST, "Brogar paid with a club of his own making. Tilda will help him at the forge now.")
    risen.quest.succeed(M.QUEST)
    return 0
end)

-- ---- the quest and the dialogue options (idempotent: called on load.before AND session.start) ---------------------------------------------------------------------
local function define_content()
    risen.quest.define({ name = M.QUEST, title = "Ore for the Forge", type = "Report", xp = 60 })

    -- plain conversation, always available; the reply is spoken (a `sound` on the line; the subtitle stays)
    risen.dialog.create({
        name = "BX_Brogar_Who", owner = M.BROGAR, permanent = true,
        lines = {
            { speaker = "player", listener = "npc", text = "Who are you?" },
            { speaker = "npc", listener = "player", text = "Brogar. I forge what the island breaks.", sound = { default = { data = tone_wav(330, 0.6) } } },
            { command = "End" },
        },
    })
    -- the quest giver: not permanent, so it is gone once used
    risen.dialog.create({
        name = "BX_Brogar_Work", owner = M.BROGAR,
        lines = {
            { speaker = "player", listener = "npc", text = "Any work for me?" },
            { speaker = "npc", listener = "player", text = ("My forge is cold. Bring me %d pieces of iron ore."):format(M.ORE_NEEDED) },
            { script = "BX_Quest_Start" },
            { command = "End" },
        },
    })
    -- offered by the engine only while the quest RUNS and the player carries the ore: `when` takes the conditions of risen.cond (a dialogue condition)
    risen.dialog.create({
        name = "BX_Brogar_Ore", owner = M.BROGAR,
        when = C.all { C.quest(M.QUEST, "Running"), C.item("It_IronOre", M.ORE_NEEDED) },
        lines = {
            { speaker = "player", listener = "npc", text = ("Here are your %d pieces of iron ore."):format(M.ORE_NEEDED) },
            { speaker = "npc", listener = "player", text = "Good ore. Take this club; the fire is still in it." },
            { command = "Give", Entity1 = "player", Entity2 = "npc", Item = "It_IronOre", Amount = M.ORE_NEEDED },   -- the ore changes hands
            { script = "BX_Quest_Reward" },
            { command = "End" },
        },
    })
    -- appears when the player carries the sword the recipe makes
    risen.dialog.create({
        name = "BX_Brogar_Sword", owner = M.BROGAR, permanent = true,
        when = C.item(balance.SWORD, 1),
        lines = {
            { speaker = "player", listener = "npc", text = "Look at my sword." },
            { speaker = "npc", listener = "player", text = "My plan, my ore, your arm. It will do." },
            { command = "End" },
        },
    })
end

-- ---- NPCs ------------------------------------------------------------------------------------------------------------------------------------------------------------------
risen.npc.persistent({
    name = M.BROGAR, template = "Spass_BX_Brogar", offset = { 150, 20, 150 }, label = "Brogar the Smith", peaceful = true,
    define = define_content,
})

-- The apprentice: made by the layer when the quest has succeeded. `routine` is applied once and checked on every load. Positions are relative to her home.
risen.spawn.when({
    name = M.TILDA, template = "Spass_BX_Tilda", offset = { 250, 20, 350 }, label = "Tilda the Apprentice", peaceful = true,
    when = C.quest(M.QUEST, "Success"),
    -- (A post the NPC cannot walk to -- across a rock, over water -- leaves her standing in ZS_Interact_PreInteract: pick positions near her home that you can see are connected.)
    routine = {
        work  = { dx = 350, dy = 0, dz = -100, kind = "guard", yaw = 90 },       -- by day she stands guard at the forge, facing east
        relax = { dx = 500, dy = 0, dz = 150, kind = "lookat" },              -- in the evening she looks into the fire
        sleep = { dx = -100, dy = 0, dz = 250, kind = "sleep" },              -- at night she sleeps on the ground
    },
    on_fire = function(rule, why) M.stats.tilda_fired[#M.stats.tilda_fired + 1] = why end,    -- why: "condition" (it happened now) | "load" (found true when a game was loaded)
})

-- ---- the watchman: behaviour in Lua -----------------------------------------------------------------------------------------------------------------------------
-- A state is run by the engine's AI like one of its own `ZS_*`: `begin` once, `loop` every `interval` seconds, `finish` at the end. `task = "OnRoutine"` keeps the engine's
-- perception going, and `on.notice` is called about once a second for everything he perceives.
risen.ai.state("ZS_BX_Watch", {
    interval = 0.5,
    task = "OnRoutine",
    begin = function(ctx) ctx.data.home = { ctx.self:position() } end,
    loop = function(ctx)
        local player = risen.entity.player()
        if not player then return end
        local d = ctx.self:distance_to(player)
        if d < 450 then ctx.self:turn_to(player) end            -- keeps an eye on him
        if d > 700 then ctx.data.warned = false end             -- warns again when the player comes back
    end,
    on = {
        notice = function(ctx)
            if ctx.other and ctx.other:is_player() and not ctx.data.warned and ctx.self:distance_to(ctx.other) < 450 then
                ctx.data.warned = true
                M.stats.warnings = M.stats.warnings + 1
                risen.hud.log("Watchman Rolf: Halt! The forge is closed to strangers.")
            end
        end,
    },
})

risen.npc.persistent({
    name = M.WATCHMAN, template = "Spass_BX_Watchman", offset = { -300, 20, 200 }, label = "Watchman Rolf", peaceful = true,
    on_ready = function(npc, reason)
        if reason == "spawned" then risen.ai.start(npc, "ZS_BX_Watch", nil, { persist = true }) end   -- a restored NPC is started again by `persist`
    end,
})

return M
