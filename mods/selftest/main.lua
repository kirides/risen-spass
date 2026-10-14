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
local spawned_npcs = {}
risen.on("tick", function()
    frames = frames + 1
    if frames == 1 then
        log.info("first tick; world.is_game_running = " .. tostring(risen.world.is_game_running()))
        if AUTOSTART then
            log.info("world.new_game -> " .. tostring(risen.world.new_game()))
        end
    end
    if frames == 900 or frames == 1200 then
        local bob = spawned_npcs[#spawned_npcs]
        if bob then
            log.info(("[%d] bob state=%s level=%s hp=%s/%s mana=%s"):format(frames, tostring(bob:state()), tostring(bob:level()),
                tostring(bob:hp()), tostring(bob:hp_max()), tostring(bob:mana())))
            if frames == 900 then
                bob:set_level(3)
                bob:set_hp_max(5)
                log.info(("[900] after set_level(3), set_hp_max(5): level=%s hp_max=%s"):format(tostring(bob:level()), tostring(bob:hp_max())))
            end
        end
    end
    if frames == 900 then
        for _, npc in ipairs(spawned_npcs) do
            local x, y, z = npc:position()
            log.info(("npc %s at %s %s %s dead=%s"):format(tostring(npc:name()), tostring(x), tostring(y), tostring(z), tostring(npc:is_dead())))
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
            x, y, z = x or 0, y or 0, z or 0
            log.info(("player position %s %s %s npc=%s"):format(tostring(x), tostring(y), tostring(z), tostring(p:is_npc())))
            if not reported then
                reported = true
                -- templates, inventory, spawning
                log.info("template.exists(It_1H_Machete) = " .. tostring(risen.template.exists("It_1H_Machete")))
                log.info("template.exists(It_No_Such) = " .. tostring(risen.template.exists("It_No_Such")))
                local before = risen.item.count(p, "It_1H_Machete")
                local created = risen.item.give(p, "It_1H_Machete", 2)
                log.info(("item.give -> %s, count %s -> %s, money %s"):format(tostring(created), tostring(before),
                    tostring(risen.item.count(p, "It_1H_Machete")), tostring(risen.item.money(p))))
                -- NPC spawning: report what the engine says about each spawned entity
                spawned_npcs = {}
                for i, tname in ipairs({ "Peasant_1_01", "Pirate_S_01", "Inq_Guard_01", "Don_Henchman_S_01", "Wolf", "Boar", "NPC_Guard" }) do
                    local ok = risen.template.exists(tname)
                    local npc = ok and risen.entity.spawn(tname, x + 200 + 120 * i, y + 40, z + 200) or nil
                    if npc then spawned_npcs[#spawned_npcs + 1] = npc end
                    log.info(("spawn %-18s exists=%s -> %s is_npc=%s is_char=%s dead=%s name=%s"):format(tname, tostring(ok),
                        tostring(npc), tostring(npc and npc:is_npc()), tostring(npc and npc:is_character()),
                        tostring(npc and npc:is_dead()), tostring(npc and npc:name())))
                    if npc then
                        log.info(("   guild=%s group=%s hp=%s/%s level=%s mana=%s state=%s stacks=%s"):format(tostring(npc:guild()),
                            tostring(npc:group()), tostring(npc:hp()), tostring(npc:hp_max()), tostring(npc:level()),
                            tostring(npc:mana()), tostring(npc:state()), tostring(npc:stack_count())))
                    end
                end
                -- A custom NPC built from a peasant archetype
                local bob = risen.npc.spawn("Peasant_1_01", x + 300, y + 40, z + 300, {
                    name = "SPASS_Bob", level = 7, hp_max = 777, group = "SPASS",
                    items = { { "It_1H_Machete", 1 } }, equip = true, state = "ZS_HangAround",
                })
                if bob then
                    spawned_npcs[#spawned_npcs + 1] = bob
                    log.info(("custom npc: name=%s guild=%s group=%s hp=%s/%s level=%s state=%s stacks=%s"):format(
                        tostring(bob:name()), tostring(bob:guild()), tostring(bob:group()), tostring(bob:hp()),
                        tostring(bob:hp_max()), tostring(bob:level()), tostring(bob:state()), tostring(bob:stack_count())))
                else
                    log.info("custom npc: spawn failed")
                end
                local dropped = risen.entity.spawn("It_1H_Sword", x + 150, y + 30, z)
                log.info(("entity.spawn -> %s is_item=%s name=%s"):format(tostring(dropped), tostring(dropped and dropped:is_item()),
                    tostring(dropped and dropped:name())))
                log.info("script.call(SPASS_SelfTest, 21) -> " .. tostring(risen.script.call("SPASS_SelfTest", p, nil, 21)))
            end
        end
        log.info("gamevar.get(SPASS_Counter) = " .. tostring(risen.gamevar.get("SPASS_Counter")))
        log.info("quest.is_running(Q_None) = " .. tostring(risen.quest.is_running("Q_None")))
    end
end)

risen.on("mods.loaded", function() log.info("selftest: mods.loaded received") end)
