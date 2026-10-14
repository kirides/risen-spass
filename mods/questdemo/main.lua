-- Defines "Q_SPASS_Demo" about 5 s into a game session, then walks it through its lifecycle, logging what the
-- engine reports after each step.
local log = risen.log
local Q = "Q_SPASS_Demo"

local frames = 0
local function state(label)
    local q = risen.quest.get(Q)
    log.info(("questdemo [%s]: exists=%s running=%s Status=%s log_entries=%s"):format(label, tostring(q ~= nil),
        tostring(risen.quest.is_running(Q)), tostring(q and q:get_property("Status")), tostring(q and q:log_count())))
end

-- Dev aid: show how the engine renders its own quests' properties as text
local function dump_vanilla()
    local n = risen.quest.count()
    log.info("questdemo: quest manager knows " .. n .. " quests")
    local shown = 0
    for i = 1, n do
        local q = risen.quest.at(i)
        if q and q:log_count() > 0 and shown < 2 then
            shown = shown + 1
            local function p(name) local v = q:get_property(name); return v and v:sub(1, 400) or "nil" end
            log.info(("questdemo: vanilla quest %s: Type=%s Status=%s LogTopic=%s log_entries=%d"):format(tostring(q:name()), p("Type"), p("Status"), p("LogTopic"), q:log_count()))
            log.info(("questdemo:   LogText=%s"):format(p("LogText")))
        end
    end
end

risen.on("tick", function()
    frames = frames + 1
    if frames == 250 then dump_vanilla() end
    if frames == 300 then
        local q = risen.quest.define({ name = Q, title = "A Quest Made of Lua", type = "Report", xp = 50 })
        if not q then
            log.info("questdemo: define failed")
            return
        end
        log.info(("questdemo: defined %s name=%s Type=%s LogTopic=%s XP=%s"):format(tostring(q), tostring(q:name()),
            tostring(q:get_property("Type")), tostring(q:get_property("LogTopic")), tostring(q:get_property("ExperiencePoints"))))
        state("defined")
    elseif frames == 360 then
        log.info("questdemo: quest.open -> " .. tostring(risen.quest.open(Q)))
        log.info("questdemo: quest.run -> " .. tostring(risen.quest.run(Q)))
        state("running")
    elseif frames == 420 then
        log.info("questdemo: add_log -> " .. tostring(risen.quest.add_log(Q, "Talk to the nearest peasant.")))
        state("after add_log")
    elseif frames == 600 then
        state("later")
        log.info("questdemo: quest.succeed -> " .. tostring(risen.quest.succeed(Q)))
        state("succeeded")
    end
end)
