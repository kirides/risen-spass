---@diagnostic disable: need-check-nil, param-type-mismatch, undefined-field
-- risen.world.trace against the collision of: A = the shipped rock, B = a template with a 3x scaled mesh (loose Spass_Rock3x), C = B with the variants named in -Param variant=...
local T = risen.test
T.plan { "the rocks exist", "measured" }
local function wait_s(s)
    local t0 = T.elapsed()
    T.wait_until(function() return T.elapsed() - t0 >= s end, s + 15, "wait " .. s .. " s")
end

-- half extent of the collision along x and z at a height above the origin: the rays come from far away toward the origin and stop at the first hit
local function half_extents(e)
    local x, y, z = e:position()
    local res = {}
    for _, h in ipairs({ -100, 60, 200, 400, 700, 1000 }) do
        local out = {}
        for name, d in pairs({ px = { 1, 0 }, nx = { -1, 0 }, pz = { 0, 1 }, nz = { 0, -1 } }) do
            local blocked, _, hx, hy, hz = risen.world.trace(x + d[1] * 1500, y + h, z + d[2] * 1500, x, y + h, z)
            local dist = blocked and math.sqrt((hx - x) ^ 2 + (hz - z) ^ 2) or -1
            out[name] = math.floor(dist)
        end
        res[#res + 1] = ("h%d: +x %d -x %d +z %d -z %d"):format(h, out.px, out.nx, out.pz, out.nz)
    end
    return table.concat(res, " | ")
end

-- watch=<template>: measure the collision of the mod's own world spawns of that template (the runtime of an exported mod fits it)
local watched = {}
if T.param("watch", "") ~= "" then
    risen.on("modkit.world_spawned", function(_, key, e)
        local t = e:template()
        if t and t:name() == T.param("watch", "") then watched[#watched + 1] = e end
    end)
end

T.run(function()
    T.wait_event("session.start", 240, function(m) return m == "new" end)
    wait_s(2)
    pcall(risen.gui.close, 26)
    wait_s(25)
    local p = risen.entity.player()
    local px, py, pz = p:position()
    local base = "Obj_Nat_Stone_Small_Flat_01"
    local variant = T.param("variant", "none")
    local props = {}
    if variant ~= "none" then
        for set, name, value in variant:gmatch("([%w_]+)%.([%w_]+)=([%w_%.%-]+)") do
            props[set] = props[set] or {}
            props[set][name] = value
        end
    end
    local tb, why = risen.template.create("Spass_Rock3x", base, { mesh = "Spass_Rock3x.xmsh" })
    risen.log.info("colprobe: template B " .. tostring(tb) .. " " .. tostring(why))
    if next(props) then
        local ok, err = risen.template.patch("Spass_Rock3x", props)
        risen.log.info("colprobe: patch " .. tostring(ok) .. " " .. tostring(err))
    end
    local a = risen.entity.spawn(base, px + 3000, py + 10, pz)
    local b = risen.entity.spawn("Spass_Rock3x", px - 3000, py + 10, pz)
    T.expect("the rocks exist", a ~= nil and b ~= nil)
    wait_s(4)
    if a then
        local ps = a:list_properties("eCCollisionShape_PS")
        local names = {}
        for _, i in ipairs(ps or {}) do names[#names + 1] = i.name .. "=" .. tostring(a:get_property("eCCollisionShape_PS", i.name)) end
        risen.log.info("colprobe: eCCollisionShape_PS " .. table.concat(names, ", "))
        risen.log.info("colprobe: A shipped (mesh box 549 x 175 x 466 -> half 275 / 233): " .. half_extents(a))
    end
    if b then risen.log.info("colprobe: B 3x mesh (visual half 824 / 699): " .. half_extents(b)) end
    if b and T.param("fit", "no") ~= "no" then
        local ok, why = b:fit_collision(T.param("fit", "tree") ~= "node", tonumber(T.param("fitscale", "1")))
        risen.log.info("colprobe: fit_collision " .. tostring(ok) .. " " .. tostring(why))
        wait_s(2)
        risen.log.info("colprobe: B after fit: " .. half_extents(b))
        local ok2, why2 = b:refit_collision()
        risen.log.info("colprobe: refit " .. tostring(ok2) .. " " .. tostring(why2))
        wait_s(2)
        risen.log.info("colprobe: B after refit: " .. half_extents(b))
        wait_s(3)
        risen.log.info("colprobe: B later: " .. half_extents(b))
    end
    if #watched > 0 then
        -- the physics only runs near the player: walk over (a pose request), then give the engine time to build the shapes and the runtime time to fit them
        local wx0, wy0, wz0 = watched[1]:position()
        local okp, whyp = p:set_pose({ x = wx0 + 900, y = wy0 + 150, z = wz0 })
        risen.log.info("colprobe: moved the player " .. tostring(okp) .. " " .. tostring(whyp))
        wait_s(14)
        for i, e in ipairs(watched) do local wx, wy, wz = e:position()
            risen.log.info(("colprobe: watched %d at %.0f %.0f %.0f: %s"):format(i, wx, wy, wz, half_extents(e))) end
    end
    T.expect("measured", a ~= nil and b ~= nil)
    T.finish()
end)
