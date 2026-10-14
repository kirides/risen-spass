-- Development aid: plays mods/fetchquest through unattended and logs "driver ..." lines that scripts/smoke-test.ps1 checks.
--   1. wait for Marta, open the conversation, choose "Do you have any work for me?"  -> quest runs
--   2. quick save, quick load: Marta must be spawned again at the same spot, the quest must still run
--   3. hand in with only 1 apple      -> still running, log entry "You only have 1 of the 3 apples"
--   4. hand in with 3 apples          -> quest succeeds, apples gone, machete received
local log = risen.log
local NPC, QUEST = "SPASS_Marta", "Q_SPASS_Apples"

local step, frames, wait = "idle", 0, 0
local saved_pos, saved_ok, loaded = nil, false, false
local function go(s) step, wait = s, 0 end
local function player() return assert(risen.entity.player(), "no player") end
local function talk_and_choose(info_name)
    local marta = risen.entity.get(NPC)
    local info = risen.dialog.get(info_name)
    if not (marta and info) then log.info("driver: missing " .. (marta and "" or "Marta ") .. (info and "" or info_name)) return false end
    log.info("driver: dialog.start -> " .. tostring(risen.dialog.start(player(), marta)))
    return info
end

local function quest_log_count() local q = risen.quest.get(QUEST); return q and q:log_count() or -1 end
local function quest_status() local q = risen.quest.get(QUEST); return q and q:get_property("Status") or "missing" end

risen.on("session.start", function(mode)
    if mode == "new" then go("wait_npc") end
    if mode == "load" then loaded = true end
end)
risen.on("save.after", function(_, ok) saved_ok = ok end)

local last_count = -2
risen.on("tick", function()
    frames = frames + 1
    local c = quest_log_count()
    if c ~= last_count then
        log.info(("driver: quest log entries %d -> %d (step %s) keys: %s"):format(last_count, c, step,
            c > 0 and table.concat(risen.quest.get(QUEST):log_keys(), " | ") or "-"))
        last_count = c
    end
    wait = wait + 1
    if step == "wait_npc" then
        if risen.entity.get(NPC) and wait > 200 then
            log.info("driver: Marta exists, quest status = " .. quest_status())
            go("talk1")
        end
    elseif step == "talk1" and wait == 1 then
        local info = talk_and_choose("SPASS_Marta_Work")
        if info then go("choose1") end
    elseif step == "choose1" and wait == 90 then
        log.info("driver: choosing the work option -> " .. tostring(risen.dialog.get("SPASS_Marta_Work"):run()))
        go("wait_running")
    elseif step == "wait_running" then
        if risen.quest.is_running(QUEST) then
            log.info(("driver: quest running, log entries = %d"):format(quest_log_count()))
            go("save")
        elseif wait > 2400 then
            log.info("driver: TIMEOUT waiting for the quest to run")
            go("done")
        end
    elseif step == "save" and wait == 120 then
        saved_pos = { risen.entity.get(NPC):position() }
        log.info("driver: quick_save -> " .. tostring(risen.world.quick_save()))
        go("wait_saved")
    elseif step == "wait_saved" then
        if saved_ok then
            log.info("driver: saved; quick_load -> " .. tostring(risen.world.quick_load()))
            go("wait_loaded")
        elseif wait > 1200 then
            log.info("driver: TIMEOUT waiting for the save")
            go("done")
        end
    elseif step == "wait_loaded" then
        local marta = loaded and risen.entity.get(NPC)
        if marta and wait > 600 then
            local x, y, z = marta:position()
            local same = saved_pos[1] and math.abs(x - saved_pos[1]) < 1 and math.abs(y - saved_pos[2]) < 5 and math.abs(z - saved_pos[3]) < 1
            log.info(("driver: after load: Marta respawned=true same_spot=%s quest=%s infos=%s/%s log entries=%d"):format(tostring(same),
                quest_status(), tostring(risen.dialog.get("SPASS_Marta_Work") ~= nil), tostring(risen.dialog.get("SPASS_Marta_Apples") ~= nil),
                quest_log_count()))
            go("settle1")
        elseif wait > 3000 then
            log.info("driver: TIMEOUT after load, Marta=" .. tostring(marta))
            go("done")
        end
    elseif step == "settle1" and wait == 120 then
        log.info("driver: giving 1 apple -> " .. tostring(risen.item.give(player(), "It_Apple", 1)))
        if talk_and_choose("SPASS_Marta_Apples") then go("choose2") end
    elseif step == "choose2" and wait == 90 then
        log.info("driver: handing in 1 apple -> " .. tostring(risen.dialog.get("SPASS_Marta_Apples"):run()))
        go("wait_not_enough")
    elseif step == "wait_not_enough" then
        if quest_log_count() >= 2 then
            log.info(("driver: not enough apples: status=%s log entries=%d apples=%d"):format(quest_status(), quest_log_count(),
                risen.item.count(player(), "It_Apple")))
            log.info("driver: LogText = " .. tostring(risen.quest.get(QUEST):get_property("LogText")):sub(1, 700))
            go("settle2")
        elseif wait > 2400 then
            log.info("driver: TIMEOUT waiting for the not-enough message")
            go("done")
        end
    elseif step == "settle2" and wait == 240 then
        log.info("driver: giving 2 more apples -> " .. tostring(risen.item.give(player(), "It_Apple", 2)) ..
            ", apples=" .. risen.item.count(player(), "It_Apple"))
        if talk_and_choose("SPASS_Marta_Apples") then go("choose3") end
    elseif step == "choose3" and wait == 90 then
        log.info("driver: handing in 3 apples -> " .. tostring(risen.dialog.get("SPASS_Marta_Apples"):run()))
        go("wait_success")
    elseif step == "wait_success" then
        if quest_status():find("Success") then
            log.info(("driver: DONE status=%s apples=%d machetes=%d"):format(quest_status(),
                risen.item.count(player(), "It_Apple"), risen.item.count(player(), "It_1H_Machete")))
            go("done")
        elseif wait > 2400 then
            log.info("driver: TIMEOUT waiting for success, status=" .. quest_status())
            go("done")
        end
    end
end)
