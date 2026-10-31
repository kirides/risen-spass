---@diagnostic disable: need-check-nil, param-type-mismatch, undefined-field
-- PNGs: <Risen>\bin\spass\screenshots\veg_{before,erased,added}_<yaw>.png
local T = risen.test
local GUID = T.param("entity", "777ab89a-c9e2-410c-bc0e-dd6f5ba98157") -- Game_Start_Vegetation
T.plan { "the vegetation entity answers", "erase works", "add works", "pictures taken" }

local function wait_s(s)
    local t0 = T.elapsed()
    T.wait_until(function() return T.elapsed() - t0 >= s end, s + 15, "wait " .. s .. " s")
end
local function shot(name)
    risen.debug.screenshot(name, 8)
    T.wait_until(function() return risen.debug.screenshot_status(name) ~= "pending" end, 20, "screenshot " .. name)
    return risen.debug.screenshot_status(name) == "done"
end

T.run(function()
    T.wait_event("session.start", 240, function(m) return m == "new" end)
    wait_s(2)
    pcall(risen.gui.close, 26)
    wait_s(25)
    pcall(risen.world.set_time, 12, 0)
    wait_s(3)
    local ent = risen.entity.by_id(GUID)
    ---@type boolean?, string?
    local ok, why = nil, "no entity"
    if ent then ok, why = risen.vegetation.info(ent) end
    risen.log.info("vegprobe: info " .. tostring(ok) .. " " .. tostring(why))
    T.expect("the vegetation entity answers", ok == true)
    local p = risen.entity.player()
    local px, py, pz = p:position()
    local done = 0
    if shot("veg_before") then done = done + 1 end
    local eok, ewhy = risen.vegetation.remove(ent, px, pz, 1800)
    risen.vegetation.update(ent)
    risen.log.info("vegprobe: remove " .. tostring(eok) .. " " .. tostring(ewhy))
    T.expect("erase works", eok == true)
    wait_s(2)
    if shot("veg_erased") then done = done + 1 end
    local added, failed = 0, 0
    for i = 1, 400 do
        local a, d = math.random() * 2 * math.pi, math.sqrt(math.random()) * 900
        local x, z = px + math.cos(a) * d, pz + math.sin(a) * d
        local blocked, _, hx, hy, hz = risen.world.trace(x, py + 1400, z, x, py - 1400, z)
        if i <= 3 then risen.log.info(("vegprobe: trace %s %s %s %s %s"):format(tostring(blocked), tostring(_), tostring(hx), tostring(hy), tostring(hz))) end
        if blocked and hy then
            local r, rwhy = risen.vegetation.add(ent, { mesh = 0x00030000 + 0, x = x, y = hy, z = z, yaw = math.random() * 360, scale = 1.0 + math.random() * 0.8, scale_v = 1.0, color = 0xFF154527 })
            if i <= 3 then risen.log.info("vegprobe: add " .. tostring(r) .. " " .. tostring(rwhy)) end
            if r then added = added + 1 else failed = failed + 1 end
        else
            failed = failed + 1
        end
    end
    risen.vegetation.update(ent)
    risen.log.info(("vegprobe: added %d, failed %d"):format(added, failed))
    T.expect("add works", added > 100)
    wait_s(2)
    if shot("veg_added") then done = done + 1 end
    T.expect("pictures taken", done == 3)
    T.finish()
end)
