---@diagnostic disable: need-check-nil, param-type-mismatch, undefined-field
-- Visual verification driver (screenshots through risen.debug.screenshot), used for the Risen Extended Mod re-implementation:
--   .\scripts\test-run.ps1 -Mods risen_extended,extmod_shots -WaitForResult -Seconds 200 -Scenario extmod_shots -Param scene=ground,items=It_A+It_B,tag=mod
-- It knows no mod content: the templates to show come from the parameters, so the same run works with and without the mod (tag=mod / tag=base).
-- The PNGs are in <Risen>\bin\spass\screenshots\<tag>_<scene>_<n>.png.
--   scene=start                      the start position, 3 views (turns the player)
--   scene=ground  items=A+B+C...     the item templates dropped in a row in front of the camera, 6 per picture
--   scene=npcs    items=A+B+...      one peaceful human per item (armor / helmet / shield / weapon equipped), turned to the player, 4 per picture
--   scene=monsters items=A+B...      the monster templates in a row (frozen), 4 per picture
--   scene=hero    items=A+B+...      the player wears the items (armor, helmet, shield, weapon), pictures from behind and from the side
--   scene=teleport at=x+y+z          move the player first
-- Param "wait" = frames to settle before a picture (default 40).
local T = risen.test
local tag = T.param("tag", "shot")
local scenes = {}
for s in (T.param("scene", "start") .. ","):gmatch("([^,]*),") do if s ~= "" then scenes[#scenes + 1] = s end end
local function list(name)
    local out = {}
    for s in (T.param(name, "") .. "+"):gmatch("([^+]*)%+") do if s ~= "" then out[#out + 1] = s end end
    return out
end
local items = list("items")
-- a scene token is kind or kind/listparam[/face[/npctemplate]]: the item list comes from the parameter listparam (default items)
local function split_token(s)
    local parts = {}
    for p in (s .. "/"):gmatch("([^/]*)/") do parts[#parts + 1] = p end
    return parts[1], parts[2], parts[3], parts[4]
end
local settle = tonumber(T.param("wait", "40")) or 40

T.plan { "scenes ran" }

local function shot(name)
    risen.debug.screenshot(name, 8)
    T.wait_until(function() return risen.debug.screenshot_status(name) ~= "pending" end, 20, "screenshot " .. name)
    local st = risen.debug.screenshot_status(name)
    risen.log.info(("extmod_shots: %s -> %s"):format(name, tostring(st)))
    return st == "done"
end

local function wait_s(s)
    local t0 = T.elapsed()
    T.wait_until(function() return T.elapsed() - t0 >= s end, s + 15, "wait " .. s .. " s")
end

local function slice(t, a, b)
    local o = {}
    for k = a, b do o[#o + 1] = t[k] end
    return table.concat(o, ",")
end

local fixed = false   -- the engine's fixed debug camera is on: place things relative to the camera, not the player

local function view()
    local p = risen.entity.player()
    local cam = risen.world.camera()
    local px, py, pz = p:position()
    if fixed and cam then px, pz = cam.x, cam.z end
    local dx, dz = 0, 1
    if cam then
        local l = math.sqrt(cam.dx * cam.dx + cam.dz * cam.dz)
        if l > 0.01 then dx, dz = cam.dx / l, cam.dz / l end
    end
    return p, px, py, pz, dx, dz
end

local function at(px, py, pz, dx, dz, fwd, right)
    return px + dx * fwd + dz * right, py + 10 + (T.param('lift', '0') + 0), pz + dz * fwd - dx * right
end

T.run(function()
    T.wait_event("session.start", 240, function(m) return m == "new" end)
    wait_s(2)
    pcall(risen.gui.close, 26)   -- the "Press START" splash the test start leaves open
    wait_s(tonumber(T.param("settle_start", "25")) or 25)   -- the intro runs
    pcall(risen.world.set_time, 12, 0)
    wait_s(3)
    local p = risen.entity.player()
    risen.npc.set_stats(p, { skills = { Strength = 200, Dexterity = 200, Intelligence = 200, Circle = 4, Smith = 3, Staff = 10, Sword = 10, Axe = 10, Bow = 10, CrossBow = 10, MaxManaPoints = 400 } })
    p:set_hp_max(5000); p:set_hp(5000)
    local made = {}
    for _, token in ipairs(scenes) do
        local scene, listp, facep, npct = split_token(token)
        if listp and listp ~= "" then items = list(listp) end
        if scene == "teleport" then
            -- to=<entity id>+<entity id>: the player is moved onto each entity (by Entity:id()), two pictures there (before / after a quarter turn)
            local n = 0
            for _, id in ipairs(list(T.param("tpname", "to"))) do
                local e = risen.entity.by_id(id)
                n = n + 1
                if not e then risen.log.warn("extmod_shots: no entity " .. id) else
                    local ok = p:teleport(e)
                    risen.log.info(("extmod_shots: teleport to %s (%s) -> %s"):format(id, tostring(e:name()), tostring(ok)))
                    wait_s(tonumber(T.param("tpwait", "8")) or 8)
                    pcall(function() risen.input.simulate("ResetCamera", true); wait_s(0.3); risen.input.simulate("ResetCamera", false) end)
                    wait_s(2)
                    shot(("%s_tp%d_a"):format(tag, n))
                    risen.input.simulate("TurnRight", true); wait_s(1.0); risen.input.simulate("TurnRight", false)
                    wait_s(1)
                    shot(("%s_tp%d_b"):format(tag, n))
                    risen.input.simulate("TurnRight", true); wait_s(1.0); risen.input.simulate("TurnRight", false)
                    wait_s(1)
                    shot(("%s_tp%d_c"):format(tag, n))
                end
            end
        elseif scene == "camera" then
            -- the follow camera answers to the zoom and turn actions: zoom in, orbit to the front of the player
            local z = tonumber(T.param("zoom", "2.5")) or 2.5
            risen.input.simulate("ZoomIn", true); wait_s(z); risen.input.simulate("ZoomIn", false)
            wait_s(0.5)
            wait_s(1)
        elseif scene == "gold" then
            -- gold as an item of the category Misc: lying on the ground (focus text), picked up, in the inventory, in a chest
            local pl, px, py, pz, dx, dz = view()
            local x, y, z = at(px, py, pz, dx, dz, 160, 0)
            local g = risen.entity.spawn("It_Gold", x, y, z)
            risen.item.give(p, "It_Gold", 237)
            wait_s(2)
            shot(tag .. "_gold_1_ground")
            local before = risen.item.count(p, "It_Gold")
            local ok = g and risen.item.pick_up(p, g)
            wait_s(2)
            risen.log.info(("extmod_shots: gold pick_up -> %s, count %d -> %d, category %s"):format(tostring(ok), before, risen.item.count(p, "It_Gold"), tostring(risen.template.get("It_Gold"):get_property("gCItem_PS", "Category"))))
            shot(tag .. "_gold_2_picked")
            risen.input.simulate("Inventory", true); wait_s(0.2); risen.input.simulate("Inventory", false)
            wait_s(3)
            shot(tag .. "_gold_3_inventory")
            risen.input.simulate("Inventory", true); wait_s(0.2); risen.input.simulate("Inventory", false)
            wait_s(2)
            local chest = risen.entity.spawn("Object_Interact_Animated_Chest_Poor", x + 150 * dz, y, z - 150 * dx)
            if chest then
                local n = risen.item.give(chest, "It_Gold", 100)
                risen.log.info(("extmod_shots: gold into a chest -> %s, chest holds %d"):format(tostring(n), risen.item.count(chest, "It_Gold")))
            end
        elseif scene == "start" then
            for i = 1, 3 do
                shot(("%s_start_%d"):format(tag, i))
                local pl = risen.entity.player()
                local x, y, z = pl:position()
                pl:goto_pos(x + 300 * (i == 1 and 1 or -1), y, z + 300 * (i == 2 and -1 or 1), 0)
                wait_s(3)
            end
        elseif scene == "ground" then
            local n = 0
            for i = 1, #items, 6 do
                local pl, px, py, pz, dx, dz = view()
                local spawned = {}
                for j = i, math.min(i + 5, #items) do
                    local x, y, z = at(px, py, pz, dx, dz, 200, (j - i - 2.5) * 100)
                    local e = risen.entity.spawn(items[j], x, y, z, true)
                    spawned[#spawned + 1] = e
                    if not e then risen.log.warn("extmod_shots: cannot spawn " .. items[j]) end
                end
                wait_s(2)
                n = n + 1
                shot(("%s_ground_%02d"):format(tag, n))
                risen.log.info("extmod_shots: ground_" .. n .. " = " .. slice(items, i, math.min(i + 5, #items)))
                for _, e in ipairs(spawned) do if e then pcall(e.kill, e) end end
            end
        elseif scene == "npcs" then
            -- humans carrying the items, 3 per picture, facing the camera (face=front) or away from it (face=back: what hangs on the back is visible)
            local per = tonumber(T.param("per", "3")) or 3
            local face = (facep and facep ~= "") and facep or T.param("face", "front")
            local fwd = tonumber(T.param("fwd", "260")) or 260
            local n = 0
            for i = 1, #items, per do
                local pl, px, py, pz, dx, dz = view()
                local spawned = {}
                local yaw = (math.deg(math.atan2(dx, dz)) + (face == "front" and 180 or 0)) % 360
                for j = i, math.min(i + per - 1, #items) do
                    local x, y, z = at(px, py, pz, dx, dz, fwd, (j - i - (per - 1) / 2) * 110)
                    local e = risen.npc.spawn((npct and npct ~= "") and npct or "Peasant_1_01", x, y, z, { name = ("SHOT_%s_%d"):format(tag, j), peaceful = true, hp_max = 500, items = { { items[j], 1 } }, equip = true })
                    if e then
                        if T.param("route", "0") == "1" then
                            local ok, why = risen.npc.set_routine(e, { at = { x, y, z, kind = "guard", yaw = yaw } })
                            if not ok then risen.log.warn("extmod_shots: routine: " .. tostring(why)) end
                        end
                    else
                        risen.log.warn("extmod_shots: cannot spawn a human with " .. items[j])
                    end
                    spawned[#spawned + 1] = e
                end
                wait_s(tonumber(T.param("hold", "4")) or 4)
                n = n + 1
                shot(("%s_%s_%s_%02d"):format(tag, listp or "npcs", face, n))
                risen.log.info("extmod_shots: npcs_" .. face .. "_" .. n .. " = " .. slice(items, i, math.min(i + per - 1, #items)))
                for _, e in ipairs(spawned) do if e then pcall(e.kill, e) end end
            end
        elseif scene == "monsters" then
            local per = tonumber(T.param("mper", "3")) or 3
            local fwd = tonumber(T.param("mfwd", "520")) or 520
            local n = 0
            for i = 1, #items, per do
                local pl, px, py, pz, dx, dz = view()
                local spawned = {}
                local yaw = (math.deg(math.atan2(dx, dz)) + 180) % 360
                for j = i, math.min(i + per - 1, #items) do
                    local x, y, z = at(px, py, pz, dx, dz, fwd - (n * 140 * (T.param("mkill", "1") == "1" and 1 or 0)), (j - i - (per - 1) / 2) * 260)
                    local e = risen.npc.spawn(items[j], x, y, z, { name = ("SHOT_%s_m%d"):format(tag, j), hp_max = 9000, peaceful = true })
                    if e then
                        if T.param("mkill", "1") == "1" then e:add_hp(-100000) end   -- a corpse shows the skin without the monster attacking (set_frozen turns it into an ice statue)
                        if T.param("route", "0") == "1" then
                            local ok, why = risen.npc.set_routine(e, { at = { x, y, z, kind = "guard", yaw = yaw } })
                            if not ok then risen.log.warn("extmod_shots: routine: " .. tostring(why)) end
                        end
                    else
                        risen.log.warn("extmod_shots: cannot spawn " .. items[j])
                    end
                    spawned[#spawned + 1] = e
                end
                wait_s(tonumber(T.param("hold", "4")) or 4)
                n = n + 1
                shot(("%s_%s_%02d"):format(tag, listp or "monsters", n))
                risen.log.info("extmod_shots: " .. (listp or "monsters") .. "_" .. n .. " = " .. slice(items, i, math.min(i + per - 1, #items)))
                if T.param("mkill", "1") ~= "1" then for _, e in ipairs(spawned) do if e then pcall(e.kill, e) end end end
            end
        elseif scene == "hero" then
            -- one picture per item: the player wears it (a weapon is drawn)
            local draw = T.param("draw", "melee")
            local prev
            for _, it in ipairs(items) do
                if prev and T.param("cycle", "1") == "1" then pcall(risen.item.unequip, p, prev) end
                risen.item.give(p, it, 1)
                local ok, why = risen.item.equip(p, it)
                prev = it
                risen.log.info(("extmod_shots: hero equip %s -> %s %s"):format(it, tostring(ok), tostring(why)))
                if draw ~= "none" then
                    risen.input.simulate("WeaponMode", true); wait_s(0.3)
                    risen.input.simulate("WeaponMode", false)
                end
                wait_s(tonumber(T.param("hold", "2")) or 2)
                local lb = T.param("lookback", "0") == "1"
                if lb then risen.input.simulate("LookBack", true); wait_s(1.2) end
                shot(("%s_hero_%s"):format(tag, it))
                if lb then risen.input.simulate("LookBack", false); wait_s(0.5) end
            end
        end
        made[#made + 1] = token
    end
    T.expect("scenes ran", #made == #scenes, table.concat(made, ","))
    T.finish("extmod_shots done")
end, "extmod_shots")
