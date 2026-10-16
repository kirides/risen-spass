// ---- Geometry (M13): risen.math, risen.world.raycast / line_of_sight / can_see / is_seen_by (the physics ray test is mocked: boxes), risen.random.
// Included by test_main.cpp inside its anonymous namespace.

void setupGeometryMock(sol::state& lua) {
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math, sol::lib::os, sol::lib::bit32);
    lua.script(R"LUA(
        fails = {}
        function ok(name, cond, detail) if not cond then fails[#fails + 1] = name .. (detail ~= nil and (" (" .. tostring(detail) .. ")") or "") end end
        function near(a, b, eps) return type(a) == "number" and type(b) == "number" and math.abs(a - b) <= (eps or 1e-6) end
        function nearv(v, x, y, z, eps) return type(v) == "table" and near(v.x, x, eps) and near(v.y, y, eps) and near(v.z, z, eps) and near(v[1], x, eps) and near(v[2], y, eps) and near(v[3], z, eps) end
        function refused(...) local r, why = ... return r == nil and type(why) == "string" and why ~= "" end

        risen = { log = {}, world = {}, entity = {} }
        for _, l in ipairs { "info", "warn", "error" } do risen.log[l] = function() end end

        -- entities: userdata whose methods read a table; the physics: a list of axis-aligned boxes
        local M, of = {}, setmetatable({}, { __mode = "k" })
        local function e_of(u) return of[u] end
        function M.position(u) local e = e_of(u) if e.gone then return nil end return e.x, e.y, e.z end
        function M.direction(u)
            local e = e_of(u)
            if e.gone then return nil end
            local y, p = math.rad(e.yaw or 0), math.rad(e.pitch or 0)
            return math.sin(y) * math.cos(p), math.sin(p), math.cos(y) * math.cos(p)
        end
        function M.bounds(u)
            local e = e_of(u)
            if e.gone or not e.height then return nil end
            local hw = e.half or 30
            return e.x - hw, e.y, e.z - hw, e.x + hw, e.y + e.height, e.z + hw
        end
        function M.is_dead(u) return e_of(u).dead == true end
        function M.valid(u) return not e_of(u).gone end
        function M.name(u) return e_of(u).name end
        function make(name, x, y, z, o)
            local e = { name = name, x = x, y = y, z = z }
            for k, v in pairs(o or {}) do e[k] = v end
            local u = newproxy(true)
            getmetatable(u).__index = function(_, k) return M[k] end
            of[u] = e
            e.handle = u
            return u
        end
        function ent(u) return e_of(u) end

        walls, traces = {}, {}
        local function slab(box, x1, y1, z1, dx, dy, dz)
            local tmin, tmax = 0, 1
            local lo, hi, o, d = { box[1], box[2], box[3] }, { box[4], box[5], box[6] }, { x1, y1, z1 }, { dx, dy, dz }
            for i = 1, 3 do
                if math.abs(d[i]) < 1e-12 then
                    if o[i] < lo[i] or o[i] > hi[i] then return nil end
                else
                    local t1, t2 = (lo[i] - o[i]) / d[i], (hi[i] - o[i]) / d[i]
                    if t1 > t2 then t1, t2 = t2, t1 end
                    if t1 > tmin then tmin = t1 end
                    if t2 < tmax then tmax = t2 end
                    if tmin > tmax then return nil end
                end
            end
            return tmin
        end
        function risen.world.trace(x1, y1, z1, x2, y2, z2, ignore, bodies)
            local call = { from = { x1, y1, z1 }, to = { x2, y2, z2 }, ignore = ignore, bodies = bodies }
            traces[#traces + 1] = call
            for _, v in ipairs { x1, y1, z1, x2, y2, z2 } do if v ~= v or v == math.huge or v == -math.huge then return nil, "expected six finite coordinates" end end
            local dx, dy, dz = x2 - x1, y2 - y1, z2 - z1
            local len = math.sqrt(dx * dx + dy * dy + dz * dz)
            local best, who
            for _, w in ipairs(walls) do
                local t = slab(w, x1, y1, z1, dx, dy, dz)
                if t and (not best or t < best) then best, who = t, w.entity end
            end
            if bodies then
                for u, e in pairs(of) do
                    local skip = false
                    for _, i in ipairs(ignore or {}) do if i == u then skip = true end end
                    if not skip and e.solid and e.height then
                        local hw = e.half or 30
                        local t = slab({ e.x - hw, e.y, e.z - hw, e.x + hw, e.y + e.height, e.z + hw }, x1, y1, z1, dx, dy, dz)
                        if t and (not best or t < best) then best, who = t, u end
                    end
                end
            end
            if best then return true, best * len, x1 + dx * best, y1 + dy * best, z1 + dz * best, who end
            return false, len, x2, y2, z2, nil
        end
        camera = { x = 0, y = 170, z = 0, dx = 0, dy = 0, dz = 1, fov = 60, aspect = 0.5625 }
        function risen.world.camera() if not camera then return nil, "there is no camera" end return camera end

        player = make("Player", 0, 0, 0, { yaw = 0, height = 220 })
        function risen.entity.player() return player end
    )LUA");
    spass::game::installGeometryHelpers(lua, lua["risen"]);
}

// Runs a Lua chunk that records its failures in `fails` (see ok()); prints them and counts one CHECK.
static void geometryChecks(sol::state& lua, const char* code) {
    lua.script("fails = {}");
    sol::protected_function_result res = lua.safe_script(code, sol::script_pass_on_error);
    if (!res.valid()) {
        sol::error err = res;
        std::printf("   script error: %s\n", err.what());
        CHECK(false);
        return;
    }
    sol::table fails = lua["fails"];
    for (std::size_t i = 1; i <= fails.size(); ++i) {
        std::printf("   FAILED: %s\n", fails.get<std::string>(i).c_str());
    }
    CHECK(fails.size() == 0);
}

TEST(geometry_vectors_distances_and_bad_input) {
    sol::state lua;
    setupGeometryMock(lua);
    geometryChecks(lua, R"LUA(
        local m = risen.math
        local a, b = { 1, 2, 3 }, { x = 4, y = 6, z = 3 }
        ok("vec has both forms", nearv(m.vec(1, 2, 3), 1, 2, 3))
        ok("point from array", nearv(m.point(a), 1, 2, 3))
        ok("point from named", nearv(m.point(b), 4, 6, 3))
        ok("point from entity", nearv(m.point(make("E", 5, 6, 7)), 5, 6, 7))
        ok("add", nearv(m.add(a, b), 5, 8, 6))
        ok("sub", nearv(m.sub(b, a), 3, 4, 0))
        ok("scale", nearv(m.scale(a, 2), 2, 4, 6))
        ok("dot", near(m.dot(a, b), 4 + 12 + 9))
        ok("cross", nearv(m.cross({ 1, 0, 0 }, { 0, 1, 0 }), 0, 0, 1))
        ok("length", near(m.length({ 3, 4, 12 }), 13))
        ok("normalize", nearv(m.normalize({ 0, 0, 5 }), 0, 0, 1))
        ok("normalize zero refused", refused(m.normalize({ 0, 0, 0 })))
        ok("distance 3d", near(m.distance(a, b), 5))
        ok("distance 3d between entities", near(m.distance(make("A", 0, 0, 0), make("B", 0, 3, 4)), 5))
        ok("distance2d ignores height", near(m.distance2d({ 0, 0, 0 }, { 3, 100, 4 }), 5))
        ok("distance_sq", near(m.distance_sq(a, b), 25))
        ok("lerp points", nearv(m.lerp({ 0, 0, 0 }, { 10, 20, 30 }, 0.5), 5, 10, 15))
        ok("lerp numbers", near(m.lerp(10, 20, 0.25), 12.5))
        ok("lerp extrapolates", near(m.lerp(0, 10, 2), 20))
        ok("clamp", m.clamp(5, 0, 3) == 3 and m.clamp(-1, 0, 3) == 0 and m.clamp(2, 0, 3) == 2 and m.clamp(5, 3, 0) == 3)
        ok("sign / round", m.sign(-3) == -1 and m.sign(0) == 0 and m.round(2.567, 1) == 2.6 and m.round(2.5) == 3)
        ok("deg / rad", near(m.deg(math.pi), 180) and near(m.rad(180), math.pi))
        ok("direction", nearv(m.direction({ 0, 0, 0 }, { 0, 0, 7 }), 0, 0, 1))
        ok("direction of the same point refused", refused(m.direction({ 1, 1, 1 }, { 1, 1, 1 })))
        -- bad input: nil, reason -- never an error, never a number
        local nan, inf = 0 / 0, 1 / 0
        for _, bad in ipairs { "x", true, nan, inf, {}, { 1, 2 }, { x = 1, y = 2 }, { 1, 2, "z" }, { 1, 2, nan }, { x = inf, y = 0, z = 0 }, function() end } do
            ok("bad point " .. tostring(bad), refused(m.point(bad)) and refused(m.length(bad)) and refused(m.add(bad, a)) and refused(m.add(a, bad)) and refused(m.distance(a, bad)) and refused(m.yaw_to(a, bad)))
        end
        ok("nil point", refused(m.length(nil)) and refused(m.add()))
        ok("bad factor", refused(m.scale(a, nan)) and refused(m.scale(a, "2")) and refused(m.lerp(a, b, inf)) and refused(m.vec(1, 2, nan)) and refused(m.clamp(1, nan, 2)))
        local throwing = setmetatable({}, { __index = function() error("boom") end })
        ok("a table with a throwing __index is just not a point", refused(m.point(throwing)) and refused(m.distance(throwing, a)))
        local gone = make("G", 0, 0, 0, { gone = true })
        ok("a stale entity is refused", refused(m.point(gone)) and refused(m.distance(gone, a)))
        ok("a table that is not mutated", a[1] == 1 and a[2] == 2 and a[3] == 3 and b.x == 4)
    )LUA");
}

TEST(geometry_yaw_conventions_angles_offsets_and_cones) {
    sol::state lua;
    setupGeometryMock(lua);
    geometryChecks(lua, R"LUA(
        local m = risen.math
        -- the engine's convention (verified in the game, docs/RE.md section 38): +y up, yaw 0 looks along +z, yaw 90 along +x (to the right), degrees
        ok("yaw 0 is +z", nearv(m.from_yaw(0), 0, 0, 1))
        ok("yaw 90 is +x", nearv(m.from_yaw(90), 1, 0, 0))
        ok("yaw 180 is -z", nearv(m.from_yaw(180), 0, 0, -1))
        ok("yaw 270 is -x", nearv(m.from_yaw(270), -1, 0, 0))
        ok("pitch up is +y", nearv(m.from_yaw(0, 90), 0, 1, 0, 1e-9) and nearv(m.from_yaw(90, 45), math.sqrt(0.5), math.sqrt(0.5), 0))
        for yaw = 0, 359, 7 do ok("yaw round trip " .. yaw, near(m.yaw_of(m.from_yaw(yaw)), yaw, 1e-6), m.yaw_of(m.from_yaw(yaw))) end
        ok("yaw_of a vector", near(m.yaw_of({ 1, 0, 0 }), 90) and near(m.yaw_of({ 0, 5, -1 }), 180) and near(m.yaw_of({ -2, 0, 0 }), 270))
        ok("yaw_of straight up is 0 (no horizontal direction)", near(m.yaw_of({ 0, 1, 0 }), 0))
        ok("yaw_of / pitch_of an entity", near(m.yaw_of(make("E", 0, 0, 0, { yaw = 123 })), 123, 1e-4) and near(m.pitch_of(make("F", 0, 0, 0, { pitch = 20 })), 20, 1e-4))
        ok("pitch_of", near(m.pitch_of({ 0, 1, 0 }), 90) and near(m.pitch_of({ 0, -1, 1 }), -45) and near(m.pitch_of({ 0, 0, 1 }), 0))
        ok("yaw_to", near(m.yaw_to({ 0, 0, 0 }, { 10, 0, 0 }), 90) and near(m.yaw_to({ 5, 5, 5 }, { 5, 9, 1 }), 180) and near(m.yaw_to({ 0, 0, 0 }, { 0, 0, 0 }), 0))
        ok("pitch_to", near(m.pitch_to({ 0, 0, 0 }, { 0, 5, 5 }), 45) and near(m.pitch_to({ 0, 0, 0 }, { 0, 0, 0 }), 0))
        ok("facing of an entity", nearv(m.facing(make("E2", 0, 0, 0, { yaw = 90 })), 1, 0, 0))
        ok("angle_diff takes the short way, + = right", near(m.angle_diff(350, 10), 20) and near(m.angle_diff(10, 350), -20) and near(m.angle_diff(0, 180), 180) and near(m.angle_diff(90, 90), 0))
        ok("wrap", near(m.wrap_angle(190), -170) and near(m.wrap_angle(-190), 170) and near(m.wrap_yaw(-10), 350) and near(m.wrap_yaw(725), 5))
        ok("angle_between", near(m.angle_between({ 1, 0, 0 }, { 0, 1, 0 }), 90) and near(m.angle_between({ 0, 0, 1 }, { 0, 0, -3 }), 180) and near(m.angle_between({ 2, 0, 0 }, { 5, 0, 0 }), 0))
        ok("rotate_yaw turns to the right", nearv(m.rotate_yaw({ 0, 7, 1 }, 90), 1, 7, 0, 1e-9) and nearv(m.rotate_yaw({ 1, 0, 0 }, 90), 0, 0, -1, 1e-9))
        -- offset: forward / right / up in the frame of a facing (yaw 90 looks along +x, its right is -z)
        ok("offset forward", nearv(m.offset({ 10, 0, 10 }, 0, 5), 10, 0, 15) and nearv(m.offset({ 10, 0, 10 }, 90, 5), 15, 0, 10))
        ok("offset behind", nearv(m.offset({ 0, 0, 0 }, 0, -100), 0, 0, -100) and nearv(m.offset({ 0, 0, 0 }, 90, -100), -100, 0, 0))
        ok("offset right", nearv(m.offset({ 0, 0, 0 }, 0, 0, 7), 7, 0, 0) and nearv(m.offset({ 0, 0, 0 }, 90, 0, 7), 0, 0, -7))
        ok("offset up and entity facing", nearv(m.offset({ 0, 1, 0 }, make("E3", 0, 0, 0, { yaw = 180 }), 3, 0, 4), 0, 5, -3))
        ok("polar", nearv(m.polar({ 1, 2, 3 }, 90, 10), 11, 2, 3) and nearv(m.polar({ 0, 0, 0 }, 0, 4, 6), 0, 6, 4))
        -- areas
        ok("in_circle / in_sphere", m.in_circle({ 3, 100, 4 }, { 0, 0, 0 }, 5) == true and m.in_circle({ 3, 0, 4.1 }, { 0, 0, 0 }, 5) == false and m.in_sphere({ 3, 100, 4 }, { 0, 0, 0 }, 5) == false)
        ok("relative_angle: right is positive", near(m.relative_angle({ 10, 0, 0 }, { 0, 0, 0 }, 0), 90) and near(m.relative_angle({ -10, 0, 0 }, { 0, 0, 0 }, 0), -90)
            and near(m.relative_angle({ 0, 0, -10 }, { 0, 0, 0 }, 0), 180) and near(m.relative_angle({ 0, 0, 10 }, { 0, 0, 0 }, 0), 0) and near(m.relative_angle({ 0, 0, 10 }, { 0, 0, 0 }, 90), -90))
        ok("is_behind", m.is_behind({ 0, 0, -1 }, { 0, 0, 0 }, 0) == true and m.is_behind({ 0, 0, 1 }, { 0, 0, 0 }, 0) == false and m.is_behind({ 1, 0, -0.1 }, { 0, 0, 0 }, 0) == true
            and m.is_behind({ 1, 0, -0.1 }, { 0, 0, 0 }, 0, 10) == false and m.is_behind({ -5, 0, 0 }, { 0, 0, 0 }, make("W", 0, 0, 0, { yaw = 90 })) == true)
        -- cones
        ok("in_cone ahead / behind / side (fov 180 = the engine's HasInFOV)", m.in_cone({ 0, 0, 10 }, { 0, 0, 0 }, 0, 180) == true and m.in_cone({ 0, 0, -10 }, { 0, 0, 0 }, 0, 180) == false
            and m.in_cone({ 10, 0, 0 }, { 0, 0, 0 }, 0, 180) == true and m.in_cone({ 10, 0, -1 }, { 0, 0, 0 }, 0, 180) == false)
        ok("in_cone narrow", m.in_cone({ 10, 0, 10 }, { 0, 0, 0 }, 0, 80) == false and m.in_cone({ 10, 0, 10 }, { 0, 0, 0 }, 0, 100) == true and m.in_cone({ 10, 0, 10 }, { 0, 0, 0 }, 45, 10) == true)
        local r1, why1 = m.in_cone({ 0, 0, 100 }, { 0, 0, 0 }, 0, 90, 50)
        local r2, why2 = m.in_cone({ 0, 0, -10 }, { 0, 0, 0 }, 0, 90, 50)
        ok("in_cone reasons", r1 == false and why1 == "range" and r2 == false and why2 == "fov")
        ok("in_cone vertical limit (only for entity / vector facings)", m.in_cone({ 0, 100, 10 }, { 0, 0, 0 }, { 0, 0, 1 }, 90) == false and m.in_cone({ 0, 100, 10 }, { 0, 0, 0 }, 0, 90) == true
            and m.in_cone({ 0, 100, 10 }, { 0, 0, 0 }, { 0, 0, 1 }, 90, nil, 180) == true and m.in_cone({ 0, 5, 10 }, { 0, 0, 0 }, { 0, 0, 1 }, 90) == true)
        ok("in_cone straight above with a yaw facing", m.in_cone({ 0, 50, 0 }, { 0, 0, 0 }, 0, 90) == true)
        ok("in_cone / is_behind refuse bad input", refused(m.in_cone({ 0, 0, 1 }, { 0, 0, 0 }, 0, 0)) and refused(m.in_cone({ 0, 0, 1 }, { 0, 0, 0 }, 0, 400)) and refused(m.in_cone({ 0, 0, 1 }, { 0, 0, 0 }, "x", 90))
            and refused(m.in_cone({ 0, 0, 1 }, { 0, 0, 0 }, 0, 90, -1)) and refused(m.is_behind({ 0, 0, 1 }, { 0, 0, 0 }, nil)) and refused(m.in_cone({ 0, 0, 1 }, { 0, 0, 0 }, { 0, 0, 0 }, 90)))
        ok("angles refuse nan", refused(m.from_yaw(0 / 0)) and refused(m.polar({ 0, 0, 0 }, 1 / 0, 1)) and refused(m.angle_diff(0, 0 / 0)) and refused(m.rotate_yaw({ 0, 0, 1 }, "x")) and refused(m.offset({ 0, 0, 0 }, 0, 0 / 0)))
    )LUA");
}

TEST(geometry_line_of_sight_heights_ignores_and_options) {
    sol::state lua;
    setupGeometryMock(lua);
    geometryChecks(lua, R"LUA(
        local w = risen.world
        walls[1] = { 100, -1000, -1000, 120, 1000, 1000, entity = nil }       -- a wall across x = 100..120
        -- points
        local clear, hit = w.line_of_sight({ 0, 50, 0 }, { 50, 50, 0 })
        ok("clear in front of the wall", clear == true and hit.blocked == false and near(hit.distance, 50) and near(hit.length, 50), tostring(clear))
        local blocked, hit2 = w.line_of_sight({ 0, 50, 0 }, { 200, 50, 0 })
        ok("the wall blocks", blocked == false and hit2.blocked == true and near(hit2.distance, 100) and near(hit2.x, 100) and near(hit2.length, 200))
        ok("point results", nearv(hit2.from, 0, 50, 0) and nearv(hit2.to, 200, 50, 0))
        ok("a point is not changed by opts heights", w.raycast({ 0, 50, 0 }, { 50, 60, 0 }, { eye_height = 100, target_height = 100 }).from.y == 50)
        -- entities: the default aim point is the engine's (box centre, raised to 3/4 of the height below 350 units); the entity itself is ignored
        local a = make("A", 0, 0, 0, { height = 200 })
        local b = make("B", 300, 0, 0, { height = 100, half = 10 })
        local r = w.raycast(a, b)
        ok("default eye = 3/4 of the height", near(r.from.y, 150) and near(r.to.y, 75), r.from.y)
        ok("both entities are ignored", #traces[#traces].ignore == 2 and traces[#traces].ignore[1] == a and traces[#traces].ignore[2] == b)
        local tall = make("T", 0, 10, 0, { height = 400 })
        ok("a tall box is aimed at its centre", near(w.raycast(tall, { 5, 5, 5 }).from.y, 10 + 200))
        local flat = make("F", 0, 5, 0)
        ok("an entity without a box: its position", near(w.raycast(flat, { 5, 5, 5 }).from.y, 5))
        local r2 = w.raycast(a, b, { eye_height = 30, target_height = 40, from_height = 20 })
        ok("heights above the position (from_height wins)", near(r2.from.y, 20) and near(r2.to.y, 40))
        ok("ignore_endpoints = false", (function() w.raycast(a, b, { ignore_endpoints = false }) return traces[#traces].ignore[1] == nil end)())
        local c = make("C", 0, 0, 0)
        w.raycast(a, b, { ignore = c })
        ok("ignore adds an entity", #traces[#traces].ignore == 3 and traces[#traces].ignore[3] == c)
        w.raycast(a, b, { ignore = { c, c } })
        ok("ignore takes a list", #traces[#traces].ignore == 4)
        w.raycast(a, b, { bodies = true })
        ok("bodies is passed on", traces[#traces].bodies == true)
        w.raycast(a, b)
        ok("bodies is off by default", traces[#traces].bodies == false)
        -- a wall between two entities
        local d = make("D", 250, 0, 0, { height = 200 })
        local ok1, h1 = w.line_of_sight(a, d)
        ok("entities behind a wall", ok1 == false and h1.blocked and near(h1.x, 100))
        -- characters block only with bodies = true
        local npc = make("N", -100, 0, 0, { height = 200, solid = true })
        local e = make("E", -300, 0, 0, { height = 200 })
        local o1 = w.line_of_sight(e, a)
        local o2 = w.line_of_sight(e, a, { bodies = true })
        local o3, h3 = w.line_of_sight(e, a, { bodies = true, ignore = npc })
        ok("a body in the way: ignored by default, blocks with bodies, unless ignored", o1 == true and o2 == false and o3 == true)
        -- bad arguments: nil, reason
        local nan = 0 / 0
        for i, args in ipairs {
            { nil, { 1, 2, 3 } }, { { 1, 2, 3 }, nil }, { "x", { 1, 2, 3 } }, { { 1, 2, 3 }, { 1, 2 } }, { make("G", 0, 0, 0, { gone = true }), { 1, 2, 3 } }, { { 1, 2, 3 }, { 4, 5, nan } },
            { { 1, 2, 3 }, { 4, 5, 6 }, 5 }, { { 1, 2, 3 }, { 4, 5, 6 }, { eye_height = "x" } }, { { 1, 2, 3 }, { 4, 5, 6 }, { ignore = 5 } }, { { 1, 2, 3 }, { 4, 5, 6 }, { fov = nan } },
        } do
            ok("bad raycast " .. i, refused(w.raycast(args[1], args[2], args[3])) and refused(w.line_of_sight(args[1], args[2], args[3])))
        end
        ok("an opts table with a throwing __index is read raw", w.raycast({ 0, 0, 0 }, { 1, 0, 0 }, setmetatable({}, { __index = function() error("boom") end })) ~= nil)
        local before = #traces
        w.raycast({ 1, 2, 3 }, { 4, 5, nan })
        ok("the physics is not asked about bad input", #traces == before)
    )LUA");
}

TEST(geometry_can_see_and_is_seen_by) {
    sol::state lua;
    setupGeometryMock(lua);
    geometryChecks(lua, R"LUA(
        local w = risen.world
        local viewer = make("V", 0, 0, 0, { yaw = 0, height = 200 })            -- looks along +z; eye at y = 150
        local function tgt(x, z, o) o = o or {} o.height = o.height or 200 return make("T" .. x .. "_" .. z, x, 0, z, o) end
        -- field of view: the engine's NPC test is 180 degrees (HasInFOV: at most 90 to each side)
        ok("ahead", w.can_see(viewer, tgt(0, 500)) == true)
        ok("behind: out of the field of view", select(2, w.can_see(viewer, tgt(0, -500))) == "fov" and w.can_see(viewer, tgt(0, -500)) == false)
        ok("to the side, within 90", w.can_see(viewer, tgt(500, 10)) == true and w.can_see(viewer, tgt(-500, 10)) == true)
        ok("to the side, just behind", w.can_see(viewer, tgt(500, -50)) == false)
        ok("fov 360 sees everything", w.can_see(viewer, tgt(0, -500), { fov = 360 }) == true)
        ok("a narrow fov", w.can_see(viewer, tgt(500, 500), { fov = 60 }) == false and w.can_see(viewer, tgt(500, 500), { fov = 100 }) == true and w.can_see(viewer, tgt(100, 500), { fov = 60 }) == true)
        local r, why = w.can_see(viewer, tgt(0, 500), { range = 400 })
        ok("range", r == false and why == "range" and w.can_see(viewer, tgt(0, 500), { range = 600 }) == true)
        -- vertical: a target far above is outside a narrow vertical opening, not inside a 180 one
        ok("vfov", w.can_see(viewer, tgt(0, 100, { height = 200 }), { fov = 180 }) == true and w.can_see(viewer, make("Hi", 0, 3000, 100, { height = 200 }), { vfov = 40 }) == false
            and w.can_see(viewer, make("Hi2", 0, 3000, 100, { height = 200 }), { vfov = 180 }) == true)
        -- walls
        walls[1] = { -1000, -10, 300, 1000, 1000, 320 }                          -- a wall across z = 300..320
        local rb, whyb, info = w.can_see(viewer, tgt(0, 500))
        ok("a wall blocks the view", rb == false and whyb == "blocked" and info.blocked and near(info.distance, 300) and near(info.y, 150))
        ok("... but not in front of it", w.can_see(viewer, tgt(0, 200)) == true)
        ok("a low target behind a low wall", (function() walls[2] = { -1000, -10, 600, 1000, 60, 620 } return w.can_see(viewer, tgt(0, 800, { height = 40 }), { eye_height = 10 }) == false end)())
        walls = {}
        -- the ray never starts inside the viewer: both ends are ignored
        w.can_see(viewer, tgt(0, 500))
        ok("viewer and target are ignored", #traces[#traces].ignore == 2 and traces[#traces].ignore[1] == viewer)
        -- eye height
        w.can_see(viewer, { 0, 0, 500 }, { eye_height = 100 })
        ok("eye_height and a plain target", near(traces[#traces].from[2], 100) and near(traces[#traces].to[2], 0))
        -- a viewer that is a table: an observer that is not in the world
        ok("table viewer", w.can_see({ 0, 0, 0, yaw = 0 }, { 0, 0, 100 }) == true and w.can_see({ x = 0, y = 0, z = 0, yaw = 180 }, { 0, 0, 100 }) == false and w.can_see({ 0, 0, 0, yaw = 90 }, { 100, 0, 0 }, { fov = 10 }) == true)
        ok("table viewer eye height", (function() w.can_see({ 0, 0, 0, yaw = 0 }, { 0, 0, 100 }, { eye_height = 160 }) return near(traces[#traces].from[2], 160) end)())
        ok("a table viewer needs a yaw", refused(w.can_see({ 0, 0, 0 }, { 0, 0, 100 })) and refused(w.can_see({ 0, 0, 0, yaw = "x" }, { 0, 0, 100 })))
        -- the camera (the player's view): it looks along its own direction, from its own position
        camera = { x = 100, y = 170, z = 0, dx = 1, dy = 0, dz = 0, fov = 60, aspect = 0.5625 }
        local pl = risen.entity.player()
        ok("camera view: along +x though the body looks along +z", w.can_see(pl, { 500, 100, 0 }, { camera = true, fov = 60 }) == true and w.can_see(pl, { 0, 100, 500 }, { camera = true, fov = 60 }) == false)
        ok("camera view: starts at the camera", (function() w.can_see(pl, { 500, 100, 0 }, { camera = true }) return near(traces[#traces].from[1], 100) and near(traces[#traces].from[2], 170) end)())
        ok("camera view: the player is ignored", traces[#traces].ignore[1] == pl)
        -- default opening from the camera: vertical 60 degrees, horizontal 2 atan(tan(30) / 0.5625) = 91.6 degrees
        ok("camera view: the default opening", w.can_see(pl, { 500, 170, 300 }, { camera = true }) == true and w.can_see(pl, { 500, 170, 520 }, { camera = true }) == false
            and w.can_see(pl, { 500, 170 + 300, 0 }, { camera = true }) == false and w.can_see(pl, { 500, 170 + 200, 0 }, { camera = true }) == true)
        camera = nil
        ok("camera view without a camera", refused(w.can_see(pl, { 500, 100, 0 }, { camera = true })))
        -- bad arguments
        for i, args in ipairs { { nil, { 1, 2, 3 } }, { 5, { 1, 2, 3 } }, { viewer, nil }, { viewer, "x" }, { viewer, { 1, 2, 3 }, 5 }, { viewer, { 1, 2, 3 }, { fov = 0 } }, { viewer, { 1, 2, 3 }, { fov = 400 } },
            { viewer, { 1, 2, 3 }, { vfov = -1 } }, { viewer, { 1, 2, 3 }, { range = -5 } }, { viewer, { 1, 2, 3 }, { fov = "x" } }, { make("G", 0, 0, 0, { gone = true }), { 1, 2, 3 } } } do
            ok("bad can_see " .. i, refused(w.can_see(args[1], args[2], args[3])))
        end
        -- is_seen_by
        local v1 = make("V1", 0, 0, 0, { yaw = 270, height = 200 })
        local v2 = make("V2", 1000, 0, 0, { yaw = 270, height = 200 })
        local point = { 500, 50, 0 }
        local seen, who = w.is_seen_by(point, { v1, v2 })
        ok("seen by the second viewer only", seen == true and who == v2)
        ok("seen by nobody", w.is_seen_by({ 500, 50, 5000 }, { v1, v2 }, { fov = 60 }) == false)
        ok("a single viewer", w.is_seen_by({ -300, 50, 0 }, v1) == true and w.is_seen_by({ 300, 50, 0 }, v1) == false)
        ok("nil = the player", (function() player = make("P2", 0, 0, 0, { yaw = 0, height = 200 }) return w.is_seen_by({ 0, 50, 300 }) == true and w.is_seen_by({ 0, 50, -300 }) == false end)())
        ent(v2).dead = true
        ok("dead viewers do not see", w.is_seen_by(point, { v1, v2 }) == false and w.is_seen_by(point, { v1, v2 }, { include_dead = true }) == true)
        ent(v2).dead = false
        ok("the first seeing viewer is returned", select(2, w.is_seen_by({ 500, 50, 0 }, { v2, v1 })) == v2)
        ok("stale viewers are skipped", w.is_seen_by(point, { make("S", 0, 0, 0, { gone = true }), v2 }) == true)
        ok("options reach can_see", w.is_seen_by({ 500, 50, 0 }, { v2 }, { range = 100 }) == false)
        ok("is_seen_by checks its arguments", refused(w.is_seen_by(nil, { v1 })) and refused(w.is_seen_by(point, 5)) and refused(w.is_seen_by(point, { v1 }, { fov = 0 })) and refused(w.is_seen_by(point, { v1 }, 7)))
        local many = {}
        for i = 1, 300 do many[i] = v1 end
        ok("too many viewers", refused(w.is_seen_by(point, many)))
    )LUA");
}

// A mod's view: handlers, a save store.
static void setupRandomApi(sol::state& lua) {
    lua.script(R"LUA(
        handlers, store = {}, {}
        api = setmetatable({
            on = function(event, fn) handlers[#handlers + 1] = { event = event, fn = fn } return #handlers end,
            save = { get = function(k) return store[k] end, set = function(k, v) store[k] = v end },
        }, { __index = risen })
        function fire(event, ...) for _, h in ipairs(handlers) do if h.event == event then h.fn(...) end end end
    )LUA");
    spass::game::installRandomModApi(lua, lua["api"], "testmod");
}

TEST(random_streams_are_seedable_repeatable_and_in_range) {
    sol::state lua;
    setupGeometryMock(lua);
    setupRandomApi(lua);
    geometryChecks(lua, R"LUA(
        local R = api.random
        local function draw(rng, n) local t = {} for i = 1, n do t[i] = rng:int(1, 1000000) end return table.concat(t, ",") end
        local a, b, c = R.new(42), R.new(42), R.new(43)
        local sa = draw(a, 20)
        ok("the same seed gives the same sequence", sa == draw(b, 20))
        ok("another seed another sequence", sa ~= draw(c, 20))
        ok("a string seed", draw(R.new("seed"), 5) == draw(R.new("seed"), 5) and draw(R.new("seed"), 5) ~= draw(R.new("seeD"), 5))
        -- known answers: the sequence of a seed is fixed forever (a mod's tests may depend on it)
        local k = R.new(1)
        ok("known sequence of seed 1", draw(k, 5) == "980586,955662,663653,336448,659054", draw(R.new(1), 5))
        local f = R.new(7)
        ok("float in [0, 1)", (function() for _ = 1, 5000 do local x = f:float() if x < 0 or x >= 1 then return false end end return true end)())
        ok("float(lo, hi) and float(hi)", (function() for _ = 1, 2000 do local x, y = f:float(-5, 5), f:float(10) if x < -5 or x >= 5 or y < 0 or y >= 10 then return false end end return true end)())
        local counts = { 0, 0, 0, 0, 0, 0 }
        local d = R.new(99)
        for _ = 1, 12000 do local v = d:int(1, 6) counts[v] = counts[v] + 1 end
        ok("a die is fair enough", (function() for i = 1, 6 do if counts[i] < 1800 or counts[i] > 2200 then return false end end return true end)(), table.concat(counts, ","))
        ok("int(n) is 1..n and int(lo, hi) is inclusive", (function() local g = R.new(5) local lo, hi = 99, -99 for _ = 1, 3000 do local v = g:int(-3, 3) lo, hi = math.min(lo, v), math.max(hi, v) local w = g:int(4) if w < 1 or w > 4 then return false end end return lo == -3 and hi == 3 end)())
        ok("int with a swapped range is refused", refused(d:int(5, 1)) and refused(d:int(0 / 0)) and refused(d:int(1, 1 / 0)) and refused(d:int(1, 1e300)) and refused(d:int("x")))
        ok("int(5, 5)", d:int(5, 5) == 5)
        ok("chance", (function() local g = R.new(8) local n = 0 for _ = 1, 10000 do if g:chance(0.25) then n = n + 1 end end return n > 2300 and n < 2700 and g:chance(1) == true and g:chance(0) == false end)())
        -- state
        local s = R.new(11)
        draw(s, 3)
        local st = s:state()
        ok("state is four integers", #st == 4 and st[1] == math.floor(st[1]))
        local after = draw(s, 10)
        local t = R.new(st)
        ok("a stream made from a state continues the sequence", draw(t, 10) == after)
        ok("set_state restores", (function() s:set_state(st) return draw(s, 10) == after end)())
        ok("a bad state is refused", refused(R.new({ 1, 2, 3 })) and refused(s:set_state({ 1, 2, 3, "x" })) and refused(s:set_state({ 1, 2, 3, 0.5 })) and refused(s:set_state(5)))
        ok("a bad seed is refused", refused(R.new(0 / 0)) and refused(R.new(true)) and refused(R.new(function() end)))
        ok("reseeding", (function() s:seed(42) local x = draw(s, 4) s:seed(42) return x == draw(s, 4) end)())
        -- the stream of the mod
        R.seed(5)
        local x1 = R.int(1, 1000)
        R.seed(5)
        ok("the mod's own stream is seedable and repeatable", R.int(1, 1000) == x1)
        ok("unseeded streams differ", draw(R.new(), 8) ~= draw(R.new(), 8))
        ok("methods need a stream", refused(R.new(1).int(nil, 1, 5)) and refused(R.new(1).float({})))
    )LUA");
}

TEST(random_choice_shuffle_sample_weighted_and_points) {
    sol::state lua;
    setupGeometryMock(lua);
    setupRandomApi(lua);
    geometryChecks(lua, R"LUA(
        local R = api.random
        local g = R.new(2024)
        local list = { "a", "b", "c", "d", "e" }
        ok("choice picks every element", (function() local seen = {} for _ = 1, 500 do seen[g:choice(list)] = true end for _, v in ipairs(list) do if not seen[v] then return false end end return true end)())
        ok("choice returns the index too", (function() local v, i = g:choice(list) return list[i] == v end)())
        ok("choice of an empty list", refused(g:choice({})) and refused(g:choice(5)) and refused(g:choice(nil)))
        local sh = g:shuffle({ 1, 2, 3, 4, 5, 6, 7, 8 })
        table.sort(sh)
        ok("shuffle keeps the elements", table.concat(sh, ",") == "1,2,3,4,5,6,7,8")
        local orig = { 1, 2, 3, 4, 5, 6, 7, 8 }
        local copy = g:shuffled(orig)
        ok("shuffled makes a copy", table.concat(orig, ",") == "1,2,3,4,5,6,7,8" and #copy == 8)
        ok("shuffle is in place and returns the list", (function() local t = { 1, 2, 3 } return g:shuffle(t) == t end)())
        ok("shuffle moves things (not always the identity)", (function() for _ = 1, 20 do if table.concat(g:shuffled({ 1, 2, 3, 4, 5, 6 }), "") ~= "123456" then return true end end return false end)())
        ok("shuffle of an empty / single list", #g:shuffle({}) == 0 and g:shuffle({ 7 })[1] == 7 and refused(g:shuffle(5)) and refused(g:shuffle("x")))
        ok("same seed same shuffle", table.concat(R.new(3):shuffled({ 1, 2, 3, 4, 5, 6, 7, 8, 9 }), "") == table.concat(R.new(3):shuffled({ 1, 2, 3, 4, 5, 6, 7, 8, 9 }), ""))
        local sm = g:sample({ 1, 2, 3, 4, 5, 6 }, 3)
        ok("sample: k different elements", #sm == 3 and sm[1] ~= sm[2] and sm[2] ~= sm[3] and sm[1] ~= sm[3])
        ok("sample: more than there are, none, bad", #g:sample({ 1, 2 }, 5) == 2 and #g:sample({ 1, 2 }, 0) == 0 and refused(g:sample({ 1 }, -1)) and refused(g:sample({ 1 }, 0 / 0)))
        -- weighted
        local counts = { 0, 0, 0 }
        for _ = 1, 8000 do local _, i = g:weighted({ "x", "y", "z" }, { 1, 3, 0 }) counts[i] = counts[i] + 1 end
        ok("weighted: proportional, zero never", counts[3] == 0 and counts[2] > 5600 and counts[2] < 6400 and counts[1] > 1600 and counts[1] < 2400, table.concat(counts, ","))
        ok("weighted with a function", (function() local v = g:weighted({ 1, 2, 3 }, function(item) return item == 2 and 1 or 0 end) return v == 2 end)())
        ok("weighted: the last element is reachable", (function() for _ = 1, 100 do if g:weighted({ "a", "b" }, { 0, 1 }) ~= "b" then return false end end return true end)())
        ok("weighted refuses bad input", refused(g:weighted({}, {})) and refused(g:weighted({ 1, 2 }, { 0, 0 })) and refused(g:weighted({ 1, 2 }, { 1, -1 })) and refused(g:weighted({ 1, 2 }, { 1 })) and refused(g:weighted({ 1 }, 5))
            and refused(g:weighted({ 1 }, function() return 0 / 0 end)) and refused(g:weighted({ 1 }, function() error("boom") end)) and refused(g:weighted(5, {})))
        -- point_in_ring
        local c = { 100, 5, -200 }
        ok("ring: radius and height", (function()
            for _ = 1, 500 do
                local p = g:point_in_ring(c, 300, 500)
                local d = math.sqrt((p.x - 100) ^ 2 + (p.z + 200) ^ 2)
                if d < 299.999 or d > 500.001 or p.y ~= 5 or p[1] ~= p.x then return false end
            end
            return true
        end)())
        ok("ring: a sector (yaw 90 +-30 around +x)", (function()
            for _ = 1, 500 do
                local p = g:point_in_ring({ 0, 0, 0 }, 100, 200, { yaw = 90, arc = 60 })
                local yaw = risen.math.yaw_of({ p.x, 0, p.z })
                if yaw < 59.999 or yaw > 120.001 then return false end
            end
            return true
        end)())
        ok("ring: dy and entity centre", near(g:point_in_ring(make("C", 1, 2, 3), 0, 0, { dy = 10 }).y, 12))
        ok("ring refuses bad input", refused(g:point_in_ring(c, 5, 1)) and refused(g:point_in_ring(c, -1, 5)) and refused(g:point_in_ring("x", 1, 2)) and refused(g:point_in_ring(c, 1, 2, { arc = 0 })) and refused(g:point_in_ring(c, 1, 2, 5))
            and refused(g:point_in_ring(c, 1, 2, { yaw = "x", arc = 10 })) and refused(g:point_in_ring(c, 1, 2, { dy = 0 / 0 })))
        -- a huge or hostile list never hangs
        local big = {}
        for i = 1, 100001 do big[i] = i end
        ok("a list that is too long is refused", refused(g:shuffle(big)) and refused(g:choice(big)))
        ok("a throwing proxy is not a list", pcall(function() return g:choice(setmetatable({}, { __index = function() error("boom") end })) end))
    )LUA");
}

TEST(random_persist_state_in_the_savegame_and_math_random_of_the_sandbox) {
    sol::state lua;
    setupGeometryMock(lua);
    setupRandomApi(lua);
    geometryChecks(lua, R"LUA(
        local R = api.random
        local g = R.new(77)
        ok("persist needs a name", refused(g:persist("")) and refused(g:persist(5)) and refused(g:persist(string.rep("x", 100))))
        ok("persist", g:persist("loot") == true)
        g:int(1, 100)
        g:int(1, 100)
        fire("save.before", "QuickSave")
        ok("save.before writes the state into the mod's store", type(store["random:loot"]) == "table" and #store["random:loot"] == 4)
        local a1 = { g:int(1, 1000), g:int(1, 1000), g:int(1, 1000) }
        -- the game goes on, then a savegame is loaded: the store comes back, the stream continues the saved sequence
        g:int(1, 1000)
        fire("load.before", "QuickSave")
        local a2 = { g:int(1, 1000), g:int(1, 1000), g:int(1, 1000) }
        ok("a load continues the saved sequence", table.concat(a1, ",") == table.concat(a2, ","), table.concat(a1, ",") .. " / " .. table.concat(a2, ","))
        -- a new game (empty store) keeps the running stream
        store = {}
        fire("load.before", "NewGame")
        ok("an empty store changes nothing", type(g:int(1, 10)) == "number")
        -- a stream made after the store exists picks it up at once
        store["random:other"] = g:state()
        local h = R.new(1)
        h:persist("other")
        ok("persist restores a stored state at once", h:int(1, 1000000) == R.new(store["random:other"]):int(1, 1000000))
        ok("the mod's own stream can persist", R.persist("main") == true)
        -- the sandbox's math.random: ONE stream shared by every mod; documented in LUA_API.md
    )LUA");
    // math.random of LuaJIT starts from the same state in every Lua state (not seeded from the clock); it is shared by every mod
    sol::state s1, s2;
    s1.open_libraries(sol::lib::base, sol::lib::math);
    s2.open_libraries(sol::lib::base, sol::lib::math);
    const double a = s1.script("return math.random()");
    const double b = s2.script("return math.random()");
    std::printf("   math.random() in two fresh LuaJIT states: %.17g %.17g\n", a, b);
    CHECK(a == b);
}

TEST(sandbox_math_random_is_one_shared_stream_with_a_fixed_start) {
    // The sandbox copies the `math` table per mod, but math.random / math.randomseed are the same LuaJIT functions: ONE generator for all mods, started from the
    // same fixed state in every process (so an unseeded mod gets the same numbers on every run) and reseeded by any mod's math.randomseed. risen.random is the
    // per-mod alternative (docs/LUA_API.md "Randomness").
    sol::state ref;
    ref.open_libraries(sol::lib::base, sol::lib::math);
    ref.script("math.randomseed(5)");
    const double r1 = ref.script("return math.random()");
    const double r2 = ref.script("return math.random()");
    TempDir t;
    t.write("a/mod.toml", "id = \"a\"\n");
    t.write("a/main.lua",
            "math.randomseed(5)\n"
            "first = math.random()\n"
            "risen.on('test.second', function() return math.random() end)\n"
            "risen.on('test.first', function() return first end)\n");
    t.write("b/mod.toml", "id = \"b\"\nload_after = [\"a\"]\n");
    t.write("b/main.lua", "math.randomseed(99)\nlocal ignored = math.random()\n");
    auto& rt = spass::lua::Runtime::instance();
    CHECK(rt.start(t.path, /*unsafeLua=*/false));
    {
        const sol::object first = rt.emitUntilHandled("test.first");
        CHECK(first.valid() && first.is<double>() && first.as<double>() == r1);
        // mod b reseeded the generator after mod a drew its first number: a's second number is not the second number of seed 5
        const sol::object second = rt.emitUntilHandled("test.second");
        CHECK(second.valid() && second.is<double>() && second.as<double>() != r2);
    }
    rt.stop();
}
