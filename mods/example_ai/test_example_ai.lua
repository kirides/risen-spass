---@diagnostic disable: need-check-nil, param-type-mismatch, cast-local-type, assign-type-mismatch
-- Live scenario `example_ai` (only loaded in a test run, see main.lua): the companion follows, the sentry warns, the coward runs when a wolf bites him, a quick save and load.
local T = risen.test

return function(stats)
    local function dist(a, b) return (a and b) and a:distance_to(b) or 1e9 end
    local function await(pred, seconds)
        for _ = 1, math.floor(seconds * 8) do
            if pred() then return true end
            T.sleep(10)
        end
        return pred() and true or false
    end
    local function lua_state(e) local c = e and risen.ai.current(e) return c and c.state end
    local function save_game()
        T.wait_until(function() return risen.world.is_save_allowed() end, 30, "saving allowed")
        local saved = false
        risen.on("save.after", function(_, ok) saved = ok end)
        risen.world.quick_save()
        T.wait_until(function() return saved end, 60, "savegame written")
        return saved
    end
    local function names_exist()
        return risen.entity.get("EX_Companion") and risen.entity.get("EX_Sentry") and risen.entity.get("EX_Coward")
    end

    T.run(function()
        T.wait_event("session.start", 180, function(m) return m == "new" end)
        T.wait_until(names_exist, 60, "the three NPCs exist")
        T.sleep(60)
        local player = assert(risen.entity.player())
        local companion, sentry, coward = risen.entity.get("EX_Companion"), risen.entity.get("EX_Sentry"), risen.entity.get("EX_Coward")

        T.section("the NPCs start in their states")
        T.expect("companion and sentry are in their Lua states, the coward is not", await(function() return lua_state(companion) == "ZS_EX_Follow" and lua_state(sentry) == "ZS_EX_Sentry" end, 15) and lua_state(coward) == nil,
            tostring(lua_state(companion)) .. " " .. tostring(lua_state(sentry)) .. " " .. tostring(lua_state(coward)))
        T.expect("the engine reports the states by name", companion:state() == "ZS_EX_Follow" and sentry:state() == "ZS_EX_Sentry", tostring(companion:state()))

        T.section("the companion")
        local start_d = dist(companion, player)
        T.expect("the companion walks up to the player", start_d > 600 and await(function() return dist(companion, player) < 450 end, 90), ("from %.0f to %.0f"):format(start_d, dist(companion, player)))

        T.section("the sentry")
        T.expect("he warns the player when he is close (once)", await(function() return stats.warnings == 1 end, 15), tostring(stats.warnings))
        T.sleep(400)
        T.eq("... and not again while the player stays", stats.warnings, 1)
        player:teleport(coward)       -- 900 units from the sentry
        T.sleep(320)
        player:teleport(sentry)
        T.expect("... he warns again when the player comes back", await(function() return stats.warnings == 2 end, 15), tostring(stats.warnings))

        T.section("the coward")
        local cx, cy, cz = coward:position()
        local wolf = assert(risen.npc.spawn("Wolf", cx + 70, cy, cz, { name = "EX_Wolf" }))
        T.expect("a bite sends the coward running (ZS_EX_Flee, away from the wolf)", await(function() return lua_state(coward) == "ZS_EX_Flee" end, 40) and stats.fled_from == "EX_Wolf", tostring(lua_state(coward)) .. " " .. tostring(stats.fled_from))
        local bx, bz = coward:position()
        wolf:kill()
        player:set_hp(player:hp_max())
        T.expect("he really runs", await(function()
            local x, _, z = coward:position()
            return math.sqrt((x - bx) ^ 2 + (z - bz) ^ 2) > 250
        end, 20))
        T.expect("after ten seconds he calms down and is back at his routine", await(function() return lua_state(coward) == nil and coward:state() ~= "ZS_EX_Flee" end, 40), tostring(coward:state()))

        T.section("save and load")
        T.expect("saved", save_game())
        local warnings_before_load = stats.warnings
        T.sleep(100)
        risen.world.quick_load()
        T.wait_event("session.start", 120, function(m) return m == "load" end)
        T.wait_until(names_exist, 60, "the three NPCs exist after the load")
        companion, sentry, coward = risen.entity.get("EX_Companion"), risen.entity.get("EX_Sentry"), risen.entity.get("EX_Coward")
        T.expect("the saved states are started again (resumed), the coward is not in one", await(function()
            local c, s = risen.ai.current(companion), risen.ai.current(sentry)
            return c and c.state == "ZS_EX_Follow" and c.resumed and s and s.state == "ZS_EX_Sentry" and s.resumed
        end, 30) and lua_state(coward) == nil, tostring(lua_state(companion)) .. " " .. tostring(lua_state(sentry)) .. " " .. tostring(lua_state(coward)))
        T.expect("the sentry watches again: he warns the player standing at his post (a new run has not warned yet)", await(function() return stats.warnings > warnings_before_load end, 25),
            ("%d warnings, %.0f units apart"):format(stats.warnings - warnings_before_load, dist(sentry, risen.entity.player())))
    end, "example_ai")
end
