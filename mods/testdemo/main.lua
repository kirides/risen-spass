-- The smallest useful risen.test scenario (docs/DEVELOPMENT.md "Testing and tracing"):
--
--   .\scripts\test-run.ps1 -Mods testdemo -WaitForResult -Seconds 240 -Scenario testdemo
--
-- test-run.ps1 starts a new game (the "autostart" helper mod requests it on the first tick), the scenario below runs as a coroutine
-- (risen.test.run), every risen.test.expect/eq is a check, and when the function returns risen.test.finish() writes
-- <Risen>\bin\spass\test-result.json and ends the game. The script prints the failed checks and returns at once.
--
-- Failure drills (-Param mode=...): "fail" (a failing check), "never" (waits for something that does not happen: the deadline
-- reports the unfinished wait), "hang" (an endless Lua loop on the first tick: the deadline thread / the stall watchdog report it).
local T = risen.test
local mode = T.param("mode", "pass")

-- Everything that must have been checked; a check that never ran fails the run as "unfinished" instead of vanishing.
T.plan { "new game started", "player exists", "player is alive", "hero hit points", "template available", "item spawned" }

if mode == "hang" then
    risen.on("tick", function() while true do end end)
end

T.run(function()
    T.section("new game")
    -- the first call waits for the event; `risen.world.new_game()` itself would be requested here if autostart did not do it
    local started = T.wait_event("session.start", 180, function(m) return m == "new" end)
    T.eq("new game started", started, "new")
    T.sleep(150)  -- let the world settle (ticks, not seconds)

    T.section("player")
    local player = risen.entity.player()
    T.expect("player exists", player ~= nil, "risen.entity.player()")
    if not player then
        T.fail("no player: nothing else can be checked")
        return
    end
    T.expect("player is alive", not player:is_dead())
    T.expect("hero hit points", (player:hp() or 0) > 0, ("hp %s/%s"):format(tostring(player:hp()), tostring(player:hp_max())))

    T.section("templates and spawning")
    T.expect("template available", risen.template.get("It_Apple") ~= nil)
    local x, y, z = player:position()
    x, y, z = x or 0, y or 0, z or 0
    local apple = risen.entity.spawn("It_Apple", x + 100, y + 10, z)
    T.expect("item spawned", apple ~= nil and apple:is_item(), apple and apple:name())

    if mode == "fail" then
        T.eq("deliberate mismatch", { 1, 2, 3 }, { 1, 2, 4 })
    elseif mode == "never" then
        T.section("waiting for something that never happens")
        T.wait_until(function() return false end, 600, "the impossible")
    end
    T.finish("testdemo done")  -- optional here: returning from the function would call it
end, "testdemo")
