-- Branching conversation: a parent option opens a sub-menu of child options. Logs "branch ..." lines (smoke test).
--   "Tell me about the town" (parent) -> "...the harbor" / "...the monastery" / "Never mind" (children, `parent = <info name>`)
--   a child with a Back command returns to the top level; the other children keep the sub-menu open.
local log = risen.log
local NPC = "SPASS_Branch"
local frames, step, wait = 0, "idle", 0
local function go(s) step, wait = s, 0 end

local function define_content()
    risen.dialog.create({
        name = "SPASS_Br_Town", owner = NPC, permanent = true,
        lines = { { speaker = "player", listener = "npc", text = "Tell me about the town." },
                  { speaker = "npc", listener = "player", text = "What do you want to know?" } },
    })
    risen.dialog.create({
        name = "SPASS_Br_Harbor", owner = NPC, parent = "SPASS_Br_Town", permanent = true,
        lines = { { speaker = "player", listener = "npc", text = "What about the harbor?" },
                  { speaker = "npc", listener = "player", text = "Ships come and go. Mostly go." } },
    })
    risen.dialog.create({
        name = "SPASS_Br_Monastery", owner = NPC, parent = "SPASS_Br_Town", permanent = true,
        lines = { { speaker = "player", listener = "npc", text = "And the monastery?" },
                  { speaker = "npc", listener = "player", text = "The Inquisition keeps to itself." } },
    })
    risen.dialog.create({
        name = "SPASS_Br_Back", owner = NPC, parent = "SPASS_Br_Town", permanent = true,
        lines = { { speaker = "player", listener = "npc", text = "Never mind." },
                  { command = "Back" } },   -- back to the top level
    })
end
risen.on("load.before", define_content)
risen.on("session.start", function(mode) define_content(); if mode == "new" then go("spawn") end end)

local function options()
    local names = {}
    for _, o in ipairs(risen.dialog.available()) do names[#names + 1] = tostring(o:name()) end
    table.sort(names)
    return table.concat(names, ",")
end

risen.on("tick", function()
    frames = frames + 1
    wait = wait + 1
    local player = risen.entity.player()
    if not player then return end   -- no running game yet
    if step == "spawn" and wait == 300 and player then
        local x, y, z = player:position()
        risen.npc.spawn("Peasant_1_01", x + 150, y + 20, z + 150, { name = NPC, peaceful = true })
        go("talk")
    elseif step == "talk" and wait == 120 then
        local npc = risen.entity.get(NPC)
        log.info("branch: dialog.start -> " .. tostring(npc and risen.dialog.start(player, npc)))
        go("level1")
    elseif step == "level1" and wait == 90 then
        log.info("branch: top-level options: " .. options())
        local parent = risen.dialog.get("SPASS_Br_Town")
        log.info("branch: choosing the parent -> " .. tostring(parent and parent:run()))
        go("wait_children")
    elseif step == "wait_children" then
        if options():find("SPASS_Br_Harbor", 1, true) then
            log.info("branch: the children are offered: " .. options())
            local child = risen.dialog.get("SPASS_Br_Harbor")
            log.info("branch: choosing a child -> " .. tostring(child and child:run()))
            go("wait_back")
        elseif wait > 3000 then
            log.info("branch: TIMEOUT waiting for the children, options: " .. options())
            go("end")
        end
    elseif step == "wait_back" then
        if options():find("SPASS_Br_Town", 1, true) or wait > 3000 then
            log.info("branch: after the child: " .. options() .. " running=" .. tostring(risen.dialog.is_running()))
            local back = risen.dialog.get("SPASS_Br_Back")
            log.info("branch: choosing Back -> " .. tostring(back and back:run()))
            go("wait_top")
        end
    elseif step == "wait_top" then
        if options():find("SPASS_Br_Town", 1, true) or wait > 3000 then
            log.info("branch: back at the top level: " .. options())
            go("end")
        end
    elseif step == "end" and wait == 60 and risen.test.is_active() then
        -- the run ends here instead of waiting out the stall watchdog; the assertions are the regexes over spass.log
        risen.test.expect("branching dialogue ran to its last step", true)
        risen.test.finish("dialogbranch done")
    end
end)
