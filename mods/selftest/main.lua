-- Development self test: every API is called at least once and the outcome is logged.
local log = risen.log

-- 1. Script registry ------------------------------------------------------------------------------
log.info("script.exists(AssessActive) = " .. tostring(risen.script.exists("AssessActive")))
log.info("script.exists(NoSuchScript) = " .. tostring(risen.script.exists("NoSuchScript")))

-- Override an existing game script: observe every call, then run the original.
local calls = 0
risen.script.override("AssessActive", function(ctx)
    calls = calls + 1
    if calls <= 5 or calls % 1000 == 0 then
        local self_name = ctx.self and ctx.self:name() or "nil"
        log.info(("AssessActive #%d self=%s arg=%d"):format(calls, self_name, ctx.arg))
    end
    return ctx:call_original()
end)

-- Define a brand-new script that game data (or other scripts) could reference by name.
risen.script.define("SPASS_SelfTest", function(ctx)
    log.info("SPASS_SelfTest called with arg " .. ctx.arg)
    return ctx.arg * 2
end)
log.info("script.exists(SPASS_SelfTest) = " .. tostring(risen.script.exists("SPASS_SelfTest")))

-- 2. Localization -----------------------------------------------------------------------------------
-- String ids are "<prefix>:<key>"; the prefix selects the table (data/ini/loc.ini): default, info, quest, item, ...
-- risen.loc.set() installs an overlay that answers every lookup of that id; trace() logs the ids the game asks for.
risen.loc.trace(true)
local function loc_test(when)
    log.info(("[%s] loc.language = %s"):format(when, tostring(risen.loc.language())))
    local id = "default:SPASS_Test"
    log.info(("[%s] loc.set(%s) -> %s, get -> %s, exists -> %s"):format(when, id,
        tostring(risen.loc.set(id, "Hello from Lua - äöü")), tostring(risen.loc.get(id)), tostring(risen.loc.exists(id))))
end
loc_test("load")
-- Real ids seen with risen.loc.trace(true) in a running game; the game now shows these texts instead.
risen.loc.set("TUT_Desc_FirstPlayerMission", "[Lua] The first mission text was replaced by the selftest mod.")
risen.loc.set("GO_QuestRunning", "[Lua] Quest running")

-- 3. Per-frame tick ---------------------------------------------------------------------------------
-- Set to true in a throw-away test install to enter a new game automatically on the first tick.
local AUTOSTART = false

local frames, reported = 0, false
risen.on("tick", function()
    frames = frames + 1
    if frames == 1 then
        log.info("first tick; world.is_game_running = " .. tostring(risen.world.is_game_running()))
        if AUTOSTART then
            log.info("world.new_game -> " .. tostring(risen.world.new_game()))
        end
    end
    -- Report a few times, then go quiet (these APIs need a running world).
    if frames % 300 == 0 and frames <= 900 then
        if frames == 300 then
            loc_test("tick300")
        end
        local p = risen.entity.player()
        log.info(("tick %d player=%s"):format(frames, p and tostring(p:name()) or "nil"))
        if p then
            local x, y, z = p:position()
            log.info(("player position %s %s %s npc=%s"):format(tostring(x), tostring(y), tostring(z), tostring(p:is_npc())))
            if not reported then
                reported = true
                log.info("script.call(SPASS_SelfTest, 21) -> " .. tostring(risen.script.call("SPASS_SelfTest", p, nil, 21)))
            end
        end
        log.info("gamevar.get(SPASS_Counter) = " .. tostring(risen.gamevar.get("SPASS_Counter")))
        log.info("quest.is_running(Q_None) = " .. tostring(risen.quest.is_running("Q_None")))
    end
end)

risen.on("mods.loaded", function() log.info("selftest: mods.loaded received") end)
