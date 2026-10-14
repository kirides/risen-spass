-- Verifies persistence and the lifecycle events in a live game (docs/LUA_API.md "Persistence"):
--   * logs every engine event
--   * "create only if missing" content: an NPC, a quest and a conditioned dialogue option
--   * mod data in risen.save
-- Unattended flow: new game -> content created -> quick save -> change state -> quick load -> report what survived.
local log = risen.log
local NAME, QUEST, INFO = "SPASS_Pers", "Q_SPASS_Pers", "SPASS_Pers_Hello"

for _, ev in ipairs({ "session.start", "session.stop", "world.changed", "save.before", "save.after", "load.before",
                      "load.after", "quest.status_changed", "npc.killed", "npc.defeated", "info.started",
                      "info.finished", "item.picked_up" }) do
    risen.on(ev, function(...)
        local args = {}
        for i = 1, select("#", ...) do args[#args + 1] = tostring((select(i, ...))) end
        log.info("persistdemo EVENT " .. ev .. "(" .. table.concat(args, ", ") .. ")")
    end)
end

risen.script.define("SPASS_Pers_Noop", function() return 0 end)

local function report(label)
    local q = risen.quest.get(QUEST)
    local e = risen.entity.get(NAME)
    local i = risen.dialog.get(INFO)
    log.info(("persistdemo [%s]: entity=%s quest=%s status=%s log_entries=%s info=%s given=%s runs=%s marker=%s"):format(label,
        tostring(e), tostring(q ~= nil), tostring(q and q:get_property("Status")), tostring(q and q:log_count()),
        tostring(i ~= nil), tostring(i and i:given()), tostring(risen.save.get("runs")), tostring(risen.save.get("marker"))))
end

local function ensure_quest_and_dialog()
    local q, err = risen.quest.define({ name = QUEST, title = "Persistence Test", type = "Report", xp = 1 })
    if not q then log.info("persistdemo: quest define failed: " .. tostring(err)) end
    local info, derr = risen.dialog.create({ name = INFO, owner = NAME, condition = "Running", quest = QUEST,
        lines = { { speaker = "player", listener = "npc", text = "Still here?" }, { script = "SPASS_Pers_Noop" } } })
    if not info then log.info("persistdemo: dialog create failed: " .. tostring(derr)) end
end

local function ensure_npc()
    if not risen.entity.get(NAME) then
        local p = risen.entity.player()
        if not p then return end
        local x, y, z = p:position()
        local e = risen.npc.spawn("Peasant_1_01", x + 150, y + 20, z + 150, { name = NAME, level = 3, hp_max = 300 })
        log.info("persistdemo: spawned " .. tostring(e))
    end
end

-- Quests and infos must exist before the engine reads the savegame; the NPC can only be created afterwards.
risen.on("load.before", function() ensure_quest_and_dialog() end)

local os_new_game_after_load = true
local frames, phase, phase_at = 0, "wait", 0
local function go(p) phase, phase_at = p, frames end

risen.on("session.start", function(mode)
    report("session.start(" .. mode .. ") before ensure")
    risen.debug.trace_files("save")
    log.info("persistdemo: save dir -> " .. tostring(risen.vfs.physical("#G3:/save")) .. "  save_allowed=" .. tostring(risen.world.is_save_allowed()))
    ensure_quest_and_dialog()
    if mode == "new" and phase == "done" then
        report("new game after a loaded game")   -- what did the engine reset?
    elseif mode == "new" then
        risen.save.set("runs", (risen.save.get("runs", 0)) + 1)
        risen.save.set("marker", "from-first-session")
        go("spawn")
    else
        go("after-load-wait")
    end
end)

risen.on("tick", function()
    frames = frames + 1
    local waited = frames - phase_at
    if phase == "spawn" and waited == 200 then
        ensure_npc()
        report("created")
        risen.quest.open(QUEST); risen.quest.run(QUEST)
        risen.quest.add_log(QUEST, "A persistent log line.")
        report("quest running")
        go("save")
    elseif phase == "save" and waited == 100 then
        local i = risen.dialog.get(INFO)
        if i then log.info("persistdemo: set_given -> " .. tostring(i:set_given(true))) end
        log.info("persistdemo: quick_save -> " .. tostring(risen.world.quick_save()))
        go("mutate")
    elseif phase == "mutate" and waited == 200 then
        risen.save.set("marker", "changed-after-save")   -- must be rolled back by the load
        report("before load")
        log.info("persistdemo: quick_load -> " .. tostring(risen.world.quick_load()))
        go("loading")
    elseif phase == "after-load-wait" and waited == 150 then
        ensure_npc()   -- only creates one if the savegame did not bring it back
        report("after load (+150 frames)")
        go("newgame")
    elseif phase == "newgame" and waited == 100 and os_new_game_after_load then
        log.info("persistdemo: new_game after load -> " .. tostring(risen.world.new_game()))
        phase = "done"
    end
end)

risen.on("load.after", function(name, ok)
    report("load.after " .. tostring(name) .. " ok=" .. tostring(ok))
end)
