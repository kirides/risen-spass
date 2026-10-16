---@diagnostic disable: need-check-nil, undefined-field
-- One game process, several scenarios (M15; docs/DEVELOPMENT.md "One-process suites"). The scenario mods are deployed but switched off (test-run.ps1 -Disable); this mod
-- starts them one after the other once a game runs:
--
--   .\scripts\test-run.ps1 -Mods testsuite,testdemo,templatepatch,templatepatch_driver -Disable testdemo,templatepatch,templatepatch_driver `
--       -Param "suite=testdemo:testdemo,templates:templatepatch+templatepatch_driver" -WaitForResult -Scenario suite
--
-- `suite` is a comma separated list of  scenario-name:mod+mod+...  (the first mod normally is the scenario, the others its helpers). Per scenario: risen.test.reset(name),
-- risen.mods.load of each mod (a freshly loaded mod is told the session.start / world.ready it missed, and risen.test wait_event answers lifecycle events at once), wait for
-- risen.test.finish, unload the mods again. risen.test.suite_finish writes one result whose checks are named "<scenario>: <check>".
-- The waiting is done here with the tick event, not with risen.test.wait_for: those waits belong to the scenario that is running.
local T = risen.test
if not T.is_active() then return end
local spec = T.param("suite", "")
if spec == "" then return end

local list = {}
for item in spec:gmatch("[^,]+") do
    local name, mods = item:match("^%s*([%w_%-]+)%s*:%s*(.+)$")
    if not name then error("suite: '" .. item .. "' is not scenario:mod+mod") end
    local ids = {}
    for id in mods:gmatch("[^+]+") do ids[#ids + 1] = id:match("^%s*(.-)%s*$") end
    list[#list + 1] = { name = name, mods = ids }
end
local per_scenario = tonumber(T.param("scenario_seconds", "240")) or 240
local fresh = T.param("fresh", "1") ~= "0"     -- a new game before every scenario but the first: each one starts in a world nobody touched

local state = "boot"      -- boot: waiting for a game | run: a scenario runs | pause: frames between scenarios | newgame: waiting for the new game | done
local index, frames, started_at, seen_down = 0, 0, 0, false
local entry

local function next_scenario()
    index = index + 1
    entry = list[index]
    if not entry then
        state = "done"
        T.suite_finish("suite of " .. #list .. " scenario(s) done")
        return
    end
    T.reset(entry.name)
    risen.log.info("suite: scenario '" .. entry.name .. "' (" .. table.concat(entry.mods, ", ") .. ")")
    for _, id in ipairs(entry.mods) do
        local rep, err = risen.mods.load(id)
        if not rep then T.expect("load " .. id, false, tostring(err)) end
    end
    state, started_at = "run", T.elapsed()
end

local function end_scenario(timed_out)
    for i = #entry.mods, 1, -1 do
        local r, err = risen.mods.unload(entry.mods[i], { cascade = true })
        if not r and err ~= "the mod is not loaded" then risen.log.warn("suite: unload " .. entry.mods[i] .. ": " .. tostring(err)) end
    end
    if timed_out then T.fail("scenario '" .. entry.name .. "' did not finish within " .. per_scenario .. " s") end
    if fresh and index < #list then
        risen.log.info("suite: new game for the next scenario")
        risen.world.new_game()
        state, frames, seen_down = "newgame", 0, false
    else
        state, frames = "pause", 0
    end
end

T.suite_begin("suite")
risen.on("session.start", function() if state == "newgame" then seen_down = true end end)
risen.on("tick", function()
    if state == "boot" then
        if risen.entity.player() ~= nil and T.lifecycle().world ~= nil then state, frames = "pause", -90 end   -- let the world settle
    elseif state == "newgame" then
        frames = frames + 1
        if seen_down and T.lifecycle().world ~= nil and risen.entity.player() ~= nil then state, frames = "pause", -90 end
    elseif state == "pause" then
        frames = frames + 1
        if frames >= 30 then next_scenario() end
    elseif state == "run" then
        if T.is_finished() then end_scenario(false)
        elseif T.elapsed() - started_at > per_scenario then end_scenario(true) end
    end
end)
