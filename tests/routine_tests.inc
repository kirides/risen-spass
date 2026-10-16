// ---- NPC routines: the Lua part (risen.world.points/add_point, risen.npc.set_routine/get_routine/clear_routine, patrol groups, restore after a load, the `routine` option of
// persistent NPCs); the engine is mocked. Included by test_main.cpp inside its anonymous namespace.

void setupRoutineMock(sol::state& lua, bool withPersistent = false) {
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math);
    lua.script(R"LUA(
        -- a tiny world of entities. Every entity is a userdata (the routine API checks for that, like it does for the engine's Entity); methods read the table behind it.
        world = { ents = {}, by_name = {}, nextid = 1, spawned = 0, spawn_calls = {}, kills = 0, applies = 0, nav_calls = 0, nav_fail_x = nil, templates = { Peasant = true },
                  player = { 0, 0, 0 }, point_calls = {}, select_calls = {} }
        local M = {}
        local function wrap(ent)
            if not ent then return nil end
            if not ent.handle then
                local u = newproxy(true)
                getmetatable(u).__index = function(_, k) return M[k] end
                ent.handle = u
                ent.alive = true
                world.of = world.of or setmetatable({}, { __mode = "k" })
                world.of[u] = ent
            end
            return ent.handle
        end
        local function of(u) return world.of and world.of[u] end
        function M.name(u) return of(u).name end
        function M.id(u) return of(u).id end
        function M.valid(u) return of(u).alive == true end
        function M.is_npc(u) return of(u).npc == true end
        function M.is_player(u) return of(u).player == true end
        function M.is_dead(u) return of(u).dead == true end
        function M.position(u) local e = of(u) return e.x, e.y, e.z end
        function M.distance_to(u, o) local a, b = of(u), of(o) return math.sqrt((a.x - b.x) ^ 2 + (a.z - b.z) ^ 2) end
        function M.set_save_relevant(u, on) of(u).saved = on return true end
        function M.save_relevant(u) return of(u).saved == true end
        function M.set_name(u, n)
            local e = of(u)
            if world.by_name[e.name] == e then world.by_name[e.name] = nil end
            e.name = n
            world.by_name[n] = e
            return true
        end
        function M.set_property(u, set, name, value) of(u).props[set .. "." .. name] = value return true end
        function M.get_property(u, set, name) return of(u).props[set .. "." .. name] end
        function M.kill(u)
            local e = of(u)
            if not e.alive then return false end
            e.alive = false
            world.kills = world.kills + 1
            world.ents[e.id] = nil
            if world.by_name[e.name] == e then world.by_name[e.name] = nil end
            return true
        end
        function M.attach(u, child) local p, c = of(u), of(child) p.kids[#p.kids + 1] = c c.parent = p return true end
        function M.children(u) local out = {} for i, k in ipairs(of(u).kids) do out[i] = wrap(k) end return out end
        function M.template(u) local e = of(u) return { name = function() return e.template end } end
        local function make(tpl, x, y, z, extra)
            local ent = { id = "id" .. world.nextid, template = tpl, name = tpl, x = x, y = y, z = z, props = {}, kids = {}, rp = { routine = "Start", index = 0, count = 1 } }
            world.nextid = world.nextid + 1
            for k, v in pairs(extra or {}) do ent[k] = v end
            world.ents[ent.id] = ent
            world.by_name[ent.name] = ent
            return wrap(ent)
        end

        risen = { log = {}, loc = {}, template = {}, entity = {}, npc = {}, world = {}, trace = nil }
        lines = {}
        for _, l in ipairs { "info", "warn", "error" } do risen.log[l] = function(m) lines[#lines + 1] = l:sub(1, 1):upper() .. " " .. m end end
        function risen.loc.set(id, text) end
        local groups = { ["Navigation/Freepoints"] = { "FP_Guard", "FP_Stand", "FP_LookAt" }, ["Navigation/Anchors"] = { "Anchor_Patrol", "Anchor_Roam" } }
        function risen.template.list(g) return groups[g] end
        function risen.template.get(n) if world.templates[n] then return { name = function() return n end } end return nil, "unknown" end
        function risen.entity.player() return make("Player", world.player[1], world.player[2], world.player[3], { player = true, npc = true }) end
        function risen.entity.get(n) return wrap(world.by_name[n]) end
        function risen.entity.by_id(id) return wrap(world.ents[id]) end

        -- natives of lua_routine.cpp
        function risen.world.nav_point(x, y, z, r)
            world.nav_calls = world.nav_calls + 1
            if world.nav_fail_x and x >= world.nav_fail_x then return nil, "no walkable point near the position" end
            return { x + 1, y, z + 1 }
        end
        function risen.world.spawn_point(tpl, x, y, z, yaw)
            world.spawn_calls[#world.spawn_calls + 1] = tpl .. "@" .. x .. "," .. z .. (yaw and ("/" .. yaw) or "")
            world.spawned = world.spawned + 1
            local h = make(tpl, x, y, z)
            of(h).saved = true
            of(h).yaw = yaw
            return h
        end
        function risen.world.scan(templates, opts)
            local out = {}
            local want = {}
            for _, t in ipairs(templates) do want[t] = true end
            for _, e in pairs(world.ents) do
                if want[e.template] and (not opts.name or opts.name == e.name) then
                    out[#out + 1] = { entity = wrap(e), name = e.name, template = e.template, x = e.x, y = e.y, z = e.z }
                end
            end
            return out
        end
        function risen.npc.routine_points(npc)
            local e = of(npc)
            if not e or not e.npc then return nil, "entity has no navigation property set (not an NPC)" end
            return { routine = e.rp.routine, index = 0, count = 1, work = e.rp.work and wrap(e.rp.work), relax = e.rp.relax and wrap(e.rp.relax), sleep = e.rp.sleep and wrap(e.rp.sleep), guard = e.rp.guard and wrap(e.rp.guard) }
        end
        function risen.npc.set_routine_points(npc, t)
            local e = of(npc)
            world.point_calls[#world.point_calls + 1] = t
            for _, k in ipairs { "work", "relax", "sleep", "guard" } do
                local v = t[k]
                if v == false then e.rp[k] = nil elseif v ~= nil then e.rp[k] = of(v) end
            end
            return true
        end
        function risen.npc.select_routine(npc, name)
            world.select_calls[#world.select_calls + 1] = name
            if name == "Start" then return true end
            return nil, "the NPC has no routine named '" .. name .. "'"
        end
        function risen.npc.apply_routine(npc) world.applies = world.applies + 1 return true end
        function risen.npc.spawn(tpl, x, y, z, o)
            if not world.templates[tpl] then return nil end
            world.spawned = world.spawned + 1
            return make(tpl, x, y, z, { npc = true, name = o.name or tpl, opts = o })
        end
        function new_npc(name, x, y, z) return risen.npc.spawn("Peasant", x or 0, y or 0, z or 0, { name = name }) end

        -- the per-mod api: handlers, a save store that "load" swaps, off
        handlers, nexth = {}, 1
        store = {}
        api = setmetatable({ log = risen.log,
            on = function(event, fn) local h = nexth nexth = nexth + 1 handlers[#handlers + 1] = { event = event, fn = fn, h = h } return h end,
            off = function(h) for i = #handlers, 1, -1 do if handlers[i].h == h then table.remove(handlers, i) end end end,
            save = { get = function(k) return store[k] end, set = function(k, v) store[k] = v end } }, { __index = risen })
        function fire(event, ...)
            local snapshot = {}
            for i, h in ipairs(handlers) do snapshot[i] = h end
            for _, h in ipairs(snapshot) do if h.event == event then h.fn(...) end end
        end
        function ticks(n) for _ = 1, n do fire("tick") end end
        function session(mode) fire("session.start", mode) fire("world.ready", mode) ticks(31) end
        function count(tpl) local n = 0 for _, e in pairs(world.ents) do if e.template == tpl then n = n + 1 end end return n end
        function serialize(v)
            if type(v) == "table" then
                local out = {}
                for k, x in pairs(v) do out[#out + 1] = "[" .. (type(k) == "number" and tostring(k) or string.format("%q", k)) .. "]=" .. serialize(x) end
                return "{" .. table.concat(out, ",") .. "}"
            elseif type(v) == "string" then return string.format("%q", v) end
            return tostring(v)
        end
        -- what a quick save + quick load does: entities (and the references the NPCs hold) come back, nameless; the properties of an anchor are lost; the store comes back from the sidecar
        function save_game()
            fire("save.before", "QuickSave")
            saved = { ents = {}, store = load("return " .. serialize(store))() }
            for id, e in pairs(world.ents) do
                local c = {}
                for k, v in pairs(e) do c[k] = v end
                c.handle = nil
                c.props = {}
                for k, v in pairs(e.props) do c.props[k] = v end
                saved.ents[id] = c
            end
        end
        function load_game()
            fire("load.before", "QuickSave")
            local old = world.ents
            world.ents, world.by_name = {}, {}
            local fresh = {}
            for id, e in pairs(saved.ents) do
                local c = {}
                for k, v in pairs(e) do c[k] = v end
                c.name = c.template
                if c.template == "Anchor_Patrol" or c.props["gCAnchor_PS.PatrolIndex"] then c.props = {} end    -- not part of a savegame
                c.alive = nil
                c.handle = nil
                world.ents[id] = c
                world.by_name[c.name] = world.by_name[c.name] or c
                fresh[id] = c
            end
            -- the references of the NPCs must point at the new tables
            for id, c in pairs(fresh) do
                for _, k in ipairs { "work", "relax", "sleep", "guard" } do
                    if c.rp and c.rp[k] then c.rp[k] = fresh[c.rp[k].id] end
                end
                if c.parent then c.parent = fresh[c.parent.id] end
                local kids = {}
                for _, kid in ipairs(c.kids or {}) do kids[#kids + 1] = fresh[kid.id] end
                c.kids = kids
            end
            store = load("return " .. serialize(saved.store))()
        end
        function new_game() world.ents, world.by_name = {}, {} store = {} end
    )LUA");
    spass::game::installRoutineHelpers(lua, lua["risen"]);
    spass::game::installRoutineModApi(lua, lua["api"], "testmod");
    if (withPersistent) {
        spass::game::installPersistentNpcModApi(lua, lua["api"], "testmod");
    }
}

TEST(routine_validates_every_spec_before_doing_anything) {
    sol::state lua;
    setupRoutineMock(lua);
    auto run = [&](const char* code) { std::string r = lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); if (std::getenv("ROUTINE_DEBUG")) { std::printf("   run -> %s\n", r.c_str()); } return r; };
    lua.script("npc = new_npc('N', 10, 0, 10)");
    const char* bad[] = {
        "5", "{}", "{ colour = 1 }", "{ at = 5 }", "{ at = {1, 2} }", "{ at = {1,2,3}, work = {1,2,3} }", "{ at = {1,2,3,kind='nonsense'} }", "{ at = {1,2,3,kind=5} }", "{ at = {1,2,3,yaw='left'} }",
        "{ at = 'NoSuchEntity' }", "{ at = '' }", "{ patrol = { {1,2,3} } }", "{ patrol = { {1,2,3}, {4,5,6} }, at = {1,2,3} }", "{ patrol = { {1,2,3}, {4,5,6} }, work = {1,2,3} }", "{ patrol = 'NoSuchGroup' }",
        "{ patrol = { {1,2,3}, 5 } }", "{ patrol = { {1,2,3}, {4,5,6}, name = 'bad name' } }", "{ patrol = { {1,2,3}, {4,5,6}, max_users = 0 } }", "{ patrol = { {1,2,3}, {4,5,6}, speed = 1 } }",
        "{ at = {1,2,3}, routine = 5 }", "{ at = {1,2,3}, apply = 'yes' }", "{ at = {1,2,3}, snap = 1 }", "{ at = {1,2,3}, kind = 'x' }",
        "{ at = {dx=1,dy=2,dz=3,kind='x'} }", "{ at = {1,2,3}, guard = 7 }", "{ guard = {1,2,3}, at = {1,2,3}, sleep = {1,2,3} }",
    };
    for (const char* spec : bad) {
        const std::string code = std::string("local r, why = api.npc.set_routine(npc, ") + spec + ") return tostring(r) .. '|' .. tostring(type(why) == 'string' and why ~= '')";
        CHECK(run(code.c_str()) == "nil|true");
    }
    // the name of a routine can only be checked by the engine: the points made for it are taken away again
    CHECK(run("local r, why = api.npc.set_routine(npc, { at = { 1, 2, 3 }, routine = 'NoSuchRoutine' }) return tostring(r) .. '|' .. why .. '|' .. count('FP_Guard') .. '|' .. world.kills") == "nil|the NPC has no routine named 'NoSuchRoutine'|0|1");
    lua.script("api.npc.clear_routine(npc) ticks(50) world.spawn_calls = {} world.spawned = 1 world.kills = 0 world.point_calls = {} world.applies = 0 store.npc_routines = nil");
    // arguments that are not what they should be
    CHECK(run("local r, why = api.npc.set_routine(nil, { at = {1,2,3} }) return tostring(r) .. '|' .. why") == "nil|the first argument must be an NPC entity");
    CHECK(run("local r, why = api.npc.set_routine('x', { at = {1,2,3} }) return tostring(r) .. '|' .. why") == "nil|the first argument must be an NPC entity");
    CHECK(run("local r, why = api.npc.set_routine(risen.entity.player(), { at = {1,2,3} }) return tostring(r) .. '|' .. why") == "nil|the entity is not an NPC");
    CHECK(run("world.ents[npc:id()].dead = true local r, why = api.npc.set_routine(npc, { at = {1,2,3} }) world.ents[npc:id()].dead = false return tostring(r) .. '|' .. why") == "nil|the NPC is dead");
    CHECK(run("local r, why = api.npc.set_routine(npc, { at = {1,2,3} }, 5) return tostring(r) .. '|' .. why") == "nil|opts must be a table");
    CHECK(run("local r, why = api.npc.set_routine(npc, { at = {1,2,3} }, { origin = 5 }) return tostring(r) .. '|' .. why") == "nil|origin must be {x, y, z}");
    // nothing was made, nothing was set, nothing recorded
    CHECK(run("return world.spawned .. ',' .. #world.spawn_calls .. ',' .. #world.point_calls .. ',' .. world.applies .. ',' .. tostring(store.npc_routines)") == "1,0,0,0,nil");
    // the world functions refuse too
    CHECK(run("local r, why = risen.world.add_point('nonsense', 1, 2, 3) return tostring(r) .. '|' .. (why:find('unknown point kind') and 'ok' or why)") == "nil|ok");
    CHECK(run("local r, why = risen.world.add_point('guard', 'x', 2, 3) return tostring(r) .. '|' .. why") == "nil|the position must be three numbers");
    CHECK(run("local r, why = risen.world.add_point('guard', 1, 2, 3, 5) return tostring(r) .. '|' .. why") == "nil|opts must be a table");
    CHECK(run("local r, why = risen.world.points(5) return tostring(r) .. '|' .. why") == "nil|opts must be a table");
    CHECK(run("local r, why = risen.world.points({ kind = 'nonsense' }) return tostring(r) .. '|' .. (why:find('unknown point kind') and 'ok' or why)") == "nil|ok");
    CHECK(run("local r, why = api.world.add_patrol('bad name', { {1,2,3}, {4,5,6} }) return tostring(r) .. '|' .. why") == "nil|the patrol name must be a string of letters, digits, '_' and '-'");
    CHECK(run("local r, why = api.world.add_patrol('P', { {1,2,3} }) return tostring(r) .. '|' .. why") == "nil|patrol needs at least two waypoints (use `at` for a single post)");
    CHECK(run("local r, why = api.world.add_patrol('P', { {dx=1,dy=2,dz=3}, {dx=1,dy=2,dz=3} }) return tostring(r) .. '|' .. why") == "nil|relative waypoints need opts.origin");
    CHECK(run("return tostring(api.world.remove_patrol('Nope')) .. tostring(risen.world.remove_point(5)) .. tostring(api.npc.clear_routine(5))") == "falsefalsenil");
    CHECK(run("return risen.world.point_template('Guard') .. ',' .. risen.world.point_template('FP_Dance') .. ',' .. tostring(select(1, risen.world.point_template('x')))") == "FP_Guard,FP_Dance,nil");
    CHECK(run("return world.spawned .. ',' .. #world.spawn_calls") == "1,0");
}

TEST(routine_makes_points_snaps_them_and_sets_the_slots) {
    sol::state lua;
    setupRoutineMock(lua);
    auto run = [&](const char* code) { std::string r = lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); if (std::getenv("ROUTINE_DEBUG")) { std::printf("   run -> %s\n", r.c_str()); } return r; };
    lua.script("npc = new_npc('N', 10, 0, 10)");
    // at = one point for all three times of day; absolute position, yaw; the position is moved to the walkable point the engine names
    CHECK(run("local r = assert(api.npc.set_routine(npc, { at = { 100, 0, 200, kind = 'guard', yaw = 90 } })) "
              "return #r.created .. ',' .. tostring(r.work == r.relax and r.relax == r.sleep) .. ',' .. world.spawn_calls[1] .. ',' .. world.applies .. ',' .. tostring(r.reused)") == "1,true,FP_Guard@101,201/90,1,false");
    CHECK(run("local p = world.point_calls[1] return tostring(p.work ~= false) .. tostring(p.relax ~= false) .. tostring(p.sleep ~= false) .. tostring(p.guard)") == "truetruetruenil");
    CHECK(run("local g = api.npc.get_routine(npc) return g.routine .. ',' .. g.created .. ',' .. tostring(g.work:save_relevant()) .. ',' .. tostring(g.patrol)") == "Start,1,true,nil");
    CHECK(run("return store.npc_routines.npcs[npc:id()].created[1] == api.npc.get_routine(npc).work:id() and 'recorded' or 'not recorded'") == "recorded");
    // three different points; relative to the origin; kinds by name or template; guard point; snap = false keeps the position; apply = false does not look
    lua.script("npc2 = new_npc('M', 0, 0, 0)");
    CHECK(run("local r = assert(api.npc.set_routine(npc2, { work = { dx = 10, dy = 0, dz = 20, kind = 'guard' }, relax = { 50, 0, 60, kind = 'lookat' }, sleep = { 70, 0, 80, kind = 'FP_Sleep_Ground' }, "
              "guard = { 90, 0, 91 }, snap = false, apply = false }, { origin = { 100, 0, 100 } })) "
              "return table.concat(world.spawn_calls, ' ', 2) .. '|' .. #r.created .. '|' .. world.applies .. '|' .. tostring(r.guard ~= nil)") ==
          "FP_Guard@110,120 FP_LookAt@50,60 FP_Sleep_Ground@70,80 FP_Guard@90,91|4|1|true");
    CHECK(run("local g = api.npc.get_routine(npc2) return tostring(g.work ~= nil) .. tostring(g.relax ~= nil) .. tostring(g.sleep ~= nil) .. tostring(g.guard ~= nil)") == "truetruetruetrue");
    // a routine of the NPC can be chosen by name
    CHECK(run("assert(api.npc.set_routine(npc, { at = { 3, 0, 3 }, routine = 'Start' })) return table.concat(world.select_calls, ',')") == "Start");
    // a slot that is not named is emptied (the engine falls back to the working point)
    lua.script("npc3 = new_npc('O', 0, 0, 0)");
    CHECK(run("assert(api.npc.set_routine(npc3, { work = { 1, 0, 1 } })) local p = world.point_calls[#world.point_calls] return tostring(p.work ~= false) .. ',' .. tostring(p.relax) .. ',' .. tostring(p.sleep)") == "true,false,false");
    // an existing entity (by handle or by name) is used as it is, and not recorded as created
    CHECK(run("local e = risen.world.add_point('stand', 5, 0, 5, { snap = false }) world.spawn_calls = {} "
              "local r = assert(api.npc.set_routine(npc3, { work = e, relax = 'FP_Stand', sleep = e })) "
              "return #world.spawn_calls .. ',' .. #r.created .. ',' .. tostring(r.work == e) .. tostring(r.relax == e)") == "0,0,truetrue");
    // a position the engine cannot snap is refused and leaves nothing behind
    CHECK(run("world.nav_fail_x = 5000 world.spawn_calls = {} local before = world.kills "
              "local r, why = api.npc.set_routine(npc, { work = { 100, 0, 100 }, relax = { 6000, 0, 6000 } }) "
              "local n = 0 for _ in pairs(world.ents) do n = n + 1 end "
              "return tostring(r) .. '|' .. (why:find('relax: no walkable point') and 'why' or why) .. '|' .. (world.kills - before) .. '|' .. tostring(api.npc.get_routine(npc).created)") == "nil|why|1|1");
    CHECK(run("world.nav_fail_x = nil return 'ok'") == "ok");
}

TEST(routine_is_idempotent_replaces_and_clears_its_points_a_moment_after_the_npc_went_on) {
    sol::state lua;
    setupRoutineMock(lua);
    auto run = [&](const char* code) { std::string r = lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); if (std::getenv("ROUTINE_DEBUG")) { std::printf("   run -> %s\n", r.c_str()); } return r; };
    lua.script("npc = new_npc('N', 10, 0, 10) spec = { at = { dx = 100, dy = 0, dz = 0, kind = 'guard' } } origin = { origin = { 0, 0, 0 } }");
    CHECK(run("first = assert(api.npc.set_routine(npc, spec, origin)) return #world.spawn_calls .. ',' .. tostring(first.reused)") == "1,false");
    // the same spec again: nothing is made and the NPC is not told again
    CHECK(run("local again = assert(api.npc.set_routine(npc, spec, origin)) return #world.spawn_calls .. ',' .. tostring(again.reused) .. ',' .. #again.created .. ',' .. world.applies .. ',' .. tostring(again.work == first.work)") == "1,true,0,1,true");
    // the same spec but a different origin is another routine
    CHECK(run("local r = assert(api.npc.set_routine(npc, spec, { origin = { 500, 0, 0 } })) return #world.spawn_calls .. ',' .. tostring(r.reused)") == "2,false");
    // a lost point (the savegame did not have it) is made again by the same call
    lua.script("assert(api.npc.set_routine(npc, spec, origin)) lost = api.npc.get_routine(npc).work lost:kill()");
    CHECK(run("local before = #world.spawn_calls local r = assert(api.npc.set_routine(npc, spec, origin)) return (#world.spawn_calls - before) .. ',' .. tostring(r.reused) .. ',' .. tostring(r.work:valid())") == "1,false,true");
    // the old points of a replaced routine go 40 frames later, not at once
    CHECK(run("old = api.npc.get_routine(npc).work local oldid = old:id() "
              "local r = assert(api.npc.set_routine(npc, { at = { dx = 0, dy = 0, dz = 100, kind = 'lookat' } }, origin)) "
              "local still = old:valid() ticks(39) local mid = old:valid() ticks(2) "
              "return tostring(still) .. ',' .. tostring(mid) .. ',' .. tostring(old:valid()) .. ',' .. tostring(r.work:valid()) .. ',' .. tostring(api.entity.by_id(oldid))") == "true,true,false,true,nil");
    // the tick handler removed itself when there was nothing left to remove
    CHECK(run("local n = 0 for _, h in ipairs(handlers) do if h.event == 'tick' then n = n + 1 end end return tostring(n)") == "0");
    // clear_routine: the slots are emptied, the made points go, the record is gone, the NPC looks again
    CHECK(run("local pt = api.npc.get_routine(npc).work local a = world.applies "
              "assert(api.npc.clear_routine(npc)) local g = api.npc.get_routine(npc) ticks(41) "
              "return tostring(g.work) .. ',' .. g.created .. ',' .. tostring(pt:valid()) .. ',' .. (world.applies - a) .. ',' .. tostring(store.npc_routines.npcs[npc:id()])") == "nil,0,false,1,nil");
    // opts.remove = false keeps the points; opts.apply = false does not look
    CHECK(run("local r = assert(api.npc.set_routine(npc, spec, origin)) local a = world.applies assert(api.npc.clear_routine(npc, { remove = false, apply = false })) ticks(50) "
              "return tostring(r.work:valid()) .. ',' .. (world.applies - a)") == "true,0");
}

TEST(routine_patrol_groups_are_built_shared_removed_and_restored_after_a_load) {
    sol::state lua;
    setupRoutineMock(lua);
    auto run = [&](const char* code) { std::string r = lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); if (std::getenv("ROUTINE_DEBUG")) { std::printf("   run -> %s\n", r.c_str()); } return r; };
    lua.script("a = new_npc('A', 0, 0, 0) b = new_npc('B', 0, 0, 0) origin = { origin = { 1000, 0, 1000 } }");
    // an NPC with an inline patrol: three anchors of one group, numbered, each with its own freepoint as the child; the NPC works at the first one
    CHECK(run("r = assert(api.npc.set_routine(a, { patrol = { { dx = 0, dy = 0, dz = 0 }, { dx = 300, dy = 0, dz = 0, kind = 'pace' }, { dx = 300, dy = 0, dz = 300 }, name = 'Ring', kind = 'guard' } }, origin)) "
              "return r.patrol .. ',' .. #world.spawn_calls .. ',' .. count('Anchor_Patrol') .. ',' .. count('FP_Guard') .. ',' .. count('FP_Pace') .. ',' .. #r.created") == "Ring,6,3,2,1,0");
    CHECK(run("local names, idx, kids = {}, {}, {} for _, e in pairs(world.ents) do if e.template == 'Anchor_Patrol' then names[e.name] = true idx[#idx + 1] = tonumber(e.props['gCAnchor_PS.PatrolIndex']) kids[#kids + 1] = #e.kids end end "
              "table.sort(idx) local n = 0 for _ in pairs(names) do n = n + 1 end "
              "return n .. ',' .. next(names) .. ',' .. table.concat(idx, '') .. ',' .. table.concat(kids, '') ") == "1,testmod_Patrol_Ring,012,111");
    CHECK(run("local g = api.npc.get_routine(a) local anchors = {} for _, e in pairs(world.ents) do if e.template == 'Anchor_Patrol' and e.props['gCAnchor_PS.PatrolIndex'] == '0' then anchors[1] = e end end "
              "return tostring(g.work == g.relax and g.relax == g.sleep) .. ',' .. tostring(anchors[1].kids[1].id == g.work:id()) .. ',' .. g.patrol .. ',' .. g.created") == "true,true,Ring,0");
    CHECK(run("local g = store.npc_routines.patrols.Ring return g.kind .. ',' .. g.max .. ',' .. #g.anchors .. ',' .. g.anchors[3].index") == "guard,1,3,2");
    // the position of a waypoint: relative to the origin, snapped (+1)
    CHECK(run("return world.spawn_calls[1] .. ' ' .. world.spawn_calls[3] .. ' ' .. world.spawn_calls[5]") == "Anchor_Patrol@1001,1001 Anchor_Patrol@1301,1001 Anchor_Patrol@1301,1301");
    // a second NPC joins the group by name: nothing is made
    CHECK(run("local before = #world.spawn_calls local r = assert(api.npc.set_routine(b, { patrol = 'Ring' })) return (#world.spawn_calls - before) .. ',' .. r.patrol .. ',' .. tostring(r.work == api.npc.get_routine(a).work)") == "0,Ring,true");
    // the first one leaves: the group stays while B uses it; when B leaves too it goes (after the delay)
    CHECK(run("assert(api.npc.clear_routine(a)) ticks(50) return count('Anchor_Patrol') .. ',' .. count('FP_Guard') .. ',' .. tostring(store.npc_routines.patrols.Ring ~= nil)") == "3,2,true");
    CHECK(run("assert(api.npc.clear_routine(b)) local at_once = count('Anchor_Patrol') ticks(50) return at_once .. ',' .. count('Anchor_Patrol') .. ',' .. count('FP_Guard') .. ',' .. count('FP_Pace') .. ',' .. tostring(store.npc_routines.patrols.Ring)") == "3,0,0,0,nil");
    // add_patrol without an NPC; remove_patrol; the same name again returns the same group
    CHECK(run("local g = assert(api.world.add_patrol('Wall', { { 1, 0, 1 }, { 2, 0, 2 } }, { kind = 'stand', max_users = 2 })) local again = assert(api.world.add_patrol('Wall', { { 1, 0, 1 }, { 2, 0, 2 } })) "
              "return #g.anchors .. ',' .. #g.points .. ',' .. tostring(again.anchors[1] == g.anchors[1]) .. ',' .. count('FP_Stand') .. ',' .. store.npc_routines.patrols.Wall.max") == "2,2,true,2,2");
    CHECK(run("return tostring(api.world.remove_patrol('Wall')) .. ',' .. tostring(api.world.remove_patrol('Wall')) .. ',' .. (function() ticks(50) return count('Anchor_Patrol') end)()") == "true,false,0");

    // restore: a savegame keeps anchors and freepoints, but not their name and numbers; the mod's record gives them back 30 frames after world.ready
    lua.script("c = new_npc('C', 0, 0, 0) assert(api.npc.set_routine(c, { patrol = { { 10, 0, 10 }, { 20, 0, 20 }, { 30, 0, 30 }, name = 'Loop' } })) save_game() load_game()");
    CHECK(run("local n, unnamed = 0, 0 for _, e in pairs(world.ents) do if e.template == 'Anchor_Patrol' then n = n + 1 if e.name == 'Anchor_Patrol' then unnamed = unnamed + 1 end end end return n .. ',' .. unnamed") == "3,3");
    CHECK(run("fire('session.start', 'load') fire('world.ready', 'load') ticks(29) local early = 0 for _, e in pairs(world.ents) do if e.name == 'testmod_Patrol_Loop' then early = early + 1 end end return tostring(early)") == "0");
    CHECK(run("ticks(1) local named, idx, max = 0, {}, 0 for _, e in pairs(world.ents) do if e.name == 'testmod_Patrol_Loop' then named = named + 1 idx[#idx + 1] = tonumber(e.props['gCAnchor_PS.PatrolIndex']) "
              "max = tonumber(e.props['gCAnchor_PS.MaxUsers']) end end table.sort(idx) return named .. ',' .. table.concat(idx, '') .. ',' .. max") == "3,012,1");
    // a savegame that lost a waypoint: a warning; naming a group that is not whole fails, giving the waypoints again builds it anew
    lua.script("for id, e in pairs(world.ents) do if e.template == 'Anchor_Patrol' and e.props['gCAnchor_PS.PatrolIndex'] == '2' then e.alive = false world.ents[id] = nil end end lines = {} "
               "fire('world.ready', 'load') ticks(31)");
    CHECK(run("return tostring(lines[1] ~= nil and lines[1]:find(\"patrol 'Loop': 1 of 3 waypoints are missing\", 1, true) ~= nil)") == "true");
    CHECK(run("local d = new_npc('D', 0, 0, 0) local r, why = api.npc.set_routine(d, { patrol = 'Loop' }) return tostring(r) .. '|' .. (why:find(\"no patrol group named 'Loop'\", 1, true) and 'refused' or why)") == "nil|refused");
    CHECK(run("local d = new_npc('E', 0, 0, 0) local r = assert(api.npc.set_routine(d, { patrol = { { 10, 0, 10 }, { 20, 0, 20 }, { 30, 0, 30 }, name = 'Loop' } })) ticks(50) "
              "local n = 0 for _, e in pairs(world.ents) do if e.template == 'Anchor_Patrol' then n = n + 1 end end return n .. ',' .. r.patrol") == "3,Loop");
}

TEST(routine_option_of_a_persistent_npc_is_applied_once_checked_on_every_restore_and_removed_with_the_npc) {
    sol::state lua;
    setupRoutineMock(lua, true);
    auto run = [&](const char* code) { std::string r = lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); if (std::getenv("ROUTINE_DEBUG")) { std::printf("   run -> %s\n", r.c_str()); } return r; };
    lua.script(R"LUA(
        decl = { at = { dx = 100, dy = 0, dz = 0, kind = 'guard', yaw = 90 } }
        h = api.npc.persistent({ name = 'Gate', template = 'Peasant', offset = { 50, 20, 60 }, routine = decl })
        bad = api.npc.persistent({ name = 'Bad', template = 'Peasant', offset = { 0, 0, 0 }, routine = { at = { kind = 'nonsense', 1, 2, 3 } } })
    )LUA");
    CHECK(run("local r, why = api.npc.persistent({ name = 'X', template = 'Peasant', offset = {1,2,3}, routine = 5 }) return tostring(r) .. '|' .. why") == "nil|'X': routine must be a table (see risen.npc.set_routine)");
    CHECK(run("session('new') return world.spawn_calls[1]") == "FP_Guard@151,61/90");     // origin = the NPC's home (50, 60) + dx 100, snapped
    CHECK(run("local e = h:entity() local g = api.npc.get_routine(e) return tostring(g.work ~= nil) .. g.created .. ',' .. tostring(g.relax == g.work)") == "true1,true");
    CHECK(run("local e = api.entity.get('Bad') local warned = false for _, l in ipairs(lines) do if l:find(\"'Bad': routine: at: unknown point kind\", 1, true) then warned = true end end "
              "return tostring(api.npc.get_routine(e).work) .. ',' .. tostring(warned)") == "nil,true");
    // the same session again: found intact, nothing made
    CHECK(run("local n = #world.spawn_calls session('new') return #world.spawn_calls - n") == "0");
    // save + load: the engine kept the point and the reference, so nothing is made again
    CHECK(run("local id = api.npc.get_routine(h:entity()).work:id() save_game() load_game() session('load') local g = api.npc.get_routine(h:entity()) "
              "return #world.spawn_calls .. ',' .. tostring(g.work:id() == id) .. ',' .. count('FP_Guard')") == "1,true,1");
    // the savegame lost the point: restoring the NPC makes it again
    CHECK(run("api.npc.get_routine(h:entity()).work:kill() session('load') local g = api.npc.get_routine(h:entity()) return #world.spawn_calls .. ',' .. tostring(g.work ~= nil) .. ',' .. count('FP_Guard')") == "2,true,1");
    // the declaration changed (the table is the one the mod passed): the old point goes, a new one is made
    CHECK(run("local old = api.npc.get_routine(h:entity()).work decl.at.kind = 'lookat' session('load') ticks(50) "
              "local g = api.npc.get_routine(h:entity()) return #world.spawn_calls .. ',' .. world.spawn_calls[#world.spawn_calls] .. ',' .. tostring(old:valid()) .. ',' .. count('FP_Guard') .. ',' .. count('FP_LookAt')") == "3,FP_LookAt@151,61/90,false,0,1");
    // remove(): the NPC and the points of its routine are taken away
    CHECK(run("h:remove() ticks(50) return count('Peasant') .. ',' .. count('FP_Guard') .. ',' .. count('FP_LookAt') .. ',' .. count('FP_Stand')") == "1,0,0,0");     // (the Bad NPC is still there)
}
