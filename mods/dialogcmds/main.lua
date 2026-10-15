-- Dialogue commands by name (docs/PROPERTIES.md): an option that gives items, XP and sets a game event, played through unattended.
-- Logs "cmds ..." lines that scripts/smoke-test.ps1 checks.
local log = risen.log
local NPC, INFO = "SPASS_Cmds", "SPASS_Cmds_Gift"
local frames, step, wait = 0, "idle", 0
local function go(s) step, wait = s, 0 end

-- invalid specs are refused before the engine is touched
do
    local bad1, e1 = risen.dialog.create({ name = "SPASS_Cmds_Bad1", owner = NPC, lines = { { command = "NoSuchCommand" } } })
    local bad2, e2 = risen.dialog.create({ name = "SPASS_Cmds_Bad2", owner = NPC, lines = { { command = "GiveXP", Count = 5 } } })
    log.info("cmds: invalid specs refused: " .. tostring(bad1 == nil and bad2 == nil))
    log.info("cmds:   " .. tostring(e1))
    log.info("cmds:   " .. tostring(e2))
end

local function define_content()
    risen.dialog.create({
        name = INFO, owner = NPC, permanent = true,
        lines = {
            { speaker = "player", listener = "npc", text = "Got anything for me?" },
            { command = "Give", Entity1 = "npc", Entity2 = "player", Item = "It_Apple", Amount = 2 },
            { command = "GiveXP", Amount = 20 },
            { command = "SetGameEvent", GameEvent = "SPASS_Cmds_Gifted" },
            { command = "ShowSubtitle", text = "Here, take these apples." },
        },
    })
end
risen.on("load.before", define_content)

risen.on("session.start", function(mode)
    define_content()
    if mode == "new" then go("spawn") end
end)

risen.on("tick", function()
    frames = frames + 1
    wait = wait + 1
    local player = risen.entity.player()
    if step == "spawn" and wait == 300 and player then
        local x, y, z = player:position()
        risen.npc.spawn("Peasant_1_01", x + 150, y + 20, z + 150, { name = NPC, peaceful = true })
        local info = risen.dialog.get(INFO)
        for i = 1, (info and info:command_count() or 0) do
            log.info(("cmds: command %d Amount=%s Item=%s GameEvent=%s"):format(i, tostring(info:get_command_property(i, "Amount")),
                tostring(info:get_command_property(i, "Item")), tostring(info:get_command_property(i, "GameEvent"))))
        end
        go("talk")
    elseif step == "talk" and wait == 120 then
        local npc = risen.entity.get(NPC)
        if npc then
            log.info(("cmds: before: apples=%d event=%s"):format(risen.item.count(player, "It_Apple"), tostring(risen.gamevar.exists("SPASS_Cmds_Gifted"))))
            log.info("cmds: dialog.start -> " .. tostring(risen.dialog.start(player, npc)))
        end
        go("choose")
    elseif step == "choose" and wait == 90 then
        local info = risen.dialog.get(INFO)
        log.info("cmds: choosing the option -> " .. tostring(info and info:run()))
        go("check")
    elseif step == "check" and wait == 900 then
        log.info(("cmds: after: apples=%d event=%s"):format(player and risen.item.count(player, "It_Apple") or -1, tostring(risen.gamevar.exists("SPASS_Cmds_Gifted"))))
        go("done")
    end
end)
