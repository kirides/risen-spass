---@diagnostic disable: need-check-nil, param-type-mismatch, cast-local-type, assign-type-mismatch, undefined-field, undefined-global
-- Live scenario `hardening` (docs/DEVELOPMENT.md "Hardening"): every public function of the Lua API is called under pcall with a battery of wrong-typed
-- and hostile arguments (nil, numbers incl. NaN/inf/huge, strings incl. NUL and very long ones, tables incl. cyclic / very deep / throwing proxies and
-- spec-like tables with wrong-typed fields, functions, valid / stale / freed entity handles, other userdata) and the scenario passes when the game is
-- still alive and ticking afterwards. A crash cannot be caught, so the run reports no result and the last "[hardening] fuzz <function>" log line names
-- the culprit; rerun just that one with  -Param only=<Lua pattern(s) of the name, separated by ;> -Param verbose=1  (every call is logged before it is made).
--
-- The functions are ENUMERATED at run time (the `risen` table and the metatables of the Entity / Info / Quest / Template objects), so a new binding is
-- covered automatically. What is excluded is listed below, with the reason.
--
--   .\scripts\test-run.ps1 -Mods hardening -WaitForResult -Seconds 400 -Scenario hardening [-Param only=npc -Param verbose=1 -Param random=200 -Param seed=7]
local T = risen.test
if not T.is_active() then return end

-- ---- what is NOT called, and why ----------------------------------------------------------------------------------------------------------
local EXCLUDED = {
    ["risen.world.new_game"] = "starts a new game (resets the world the test is in)",
    ["risen.world.quick_load"] = "loads a savegame (the setup uses it once itself)",
    ["risen.world.quick_save"] = "writes a savegame (the setup uses it once itself)",
    ["risen.world.auto_save"] = "writes a savegame",
    ["risen.off"] = "a number is a handler id: it would unhook the layer's own handlers (checked with non-number arguments only, below)",
    ["risen.assets.clear"] = "deletes the mod's generated files",
    ["risen.log.error"] = "writes [error] lines on purpose, which the test harness counts as a failure (the log functions take any values; checked once below with the others)",
}
-- Every usertype's destructor (`<Type>.__gc`): calling it by hand frees the object's memory under its owner's feet. It is the garbage collector's entry, not a native a mod calls.
local function excluded_destructor(name) return name:sub(-5) == ".__gc" end
local EXCLUDED_PREFIX = {
    ["risen.test."] = "the test harness itself (finish/fail would end the run)",
}
-- Functions that write or mount files or paths: called with every wrong TYPE, but never with a string (a string could be a real path).
local NO_STRINGS = {
    ["risen.assets.write"] = true, ["risen.assets.remove"] = true, ["risen.assets.mount"] = true, ["risen.assets.import_sound"] = true,
    ["risen.assets.dump_collection"] = true, ["risen.assets.load_icons"] = true, ["risen.assets.load_image_list"] = true,
    ["risen.vfs.mount"] = true, ["risen.vfs.unmount"] = true, ["risen.loc.load_file"] = true, ["risen.voice.load_dir"] = true,
}

local RANDOM_CALLS = tonumber(T.param("random", "60")) or 60     -- random-argument calls per function (plus the systematic ones)
local SEED = tonumber(T.param("seed", "12345")) or 12345
local ONLY = T.param("only", nil)                                -- Lua patterns, separated by ;: fuzz only the functions whose name matches one of them
local VERBOSE = T.param("verbose", nil) ~= nil                   -- log every call before it is made (find the exact crashing call)
if ONLY and ONLY:sub(-2) == "#v" then VERBOSE, ONLY = true, ONLY:sub(1, -3) end   -- test-run.ps1 takes one -Param per call in a script: "only=<pattern>#v" is "only" plus "verbose"
local PER_FUNCTION_SECONDS = 4                                   -- wall-clock cap per function (a slow engine call is not a reason to wait)
local FRAME_BUDGET = 0.025                                       -- seconds of fuzzing per frame, the game renders in between

T.plan {
    "world ready", "fixtures exist", "handles went stale across the load", "handle to a deleted item is stale", "functions enumerated",
    "fuzz finished", "the player survived every function", "hostile callbacks survived", "game still ticks", "player still reachable", "engine still usable", "typed natives refuse wrong types cleanly",
}

local seed = SEED
local function rnd(n) seed = (seed * 1664525 + 1013904223) % 4294967296 return (seed % n) + 1 end

-- ---- the battery ---------------------------------------------------------------------------------------------------------------------------
local fixtures = {}

local function deep(n)
    local t = {}
    local cur = t
    for _ = 1, n do local c = {} cur[1] = c cur = c end
    return t
end

local VOCAB = {   -- keys that specs of the API use; values come from the battery, so most calls fail with a wrong-typed field
    "name", "template", "owner", "lines", "when", "despawn_when", "once", "text", "speaker", "listener", "command", "script", "condition", "quest", "title", "type", "xp",
    "offset", "label", "define", "on_ready", "routine", "work", "relax", "sleep", "patrol", "at", "kind", "dx", "dy", "dz", "yaw", "x", "y", "z", "radius", "near",
    "value", "damage", "requires", "modifies", "items", "npcs", "new_items", "new_npcs", "spells", "new_spells", "recipes", "new_recipes", "npc_groups", "item_groups",
    "base", "forms", "ingredients", "result", "station", "skills", "hp_max", "hp", "level", "loot", "properties", "mana_cost", "mesh", "icon", "material", "sound",
    "state", "begin", "loop", "finish", "on", "task", "interval", "args", "persist", "peaceful", "instances", "limit", "match", "group", "entity", "count", "amount",
    "permanent", "id", "default", "skill", "guild", "who", "holder", "temporary", "all", "npc", "notice", "damage_mult", "hp_mult", "learn", "english", "German", "en", "de",
}

local battery   -- list of { label, value }

local function spec(depth)
    local t = {}
    for _ = 1, rnd(4) do
        local key = VOCAB[rnd(#VOCAB)]
        local roll = rnd(10)
        if roll <= 6 or depth >= 3 then
            t[key] = battery[rnd(#battery)][2]
        elseif roll <= 8 then
            t[key] = spec(depth + 1)
        else
            local list = {}
            for i = 1, rnd(3) do list[i] = (rnd(2) == 1) and spec(depth + 1) or battery[rnd(#battery)][2] end
            t[key] = list
        end
    end
    return t
end

local function build_battery(opts)
    local b = {}
    battery = b      -- spec() draws from the battery while it is being built
    local function add(label, v) b[#b + 1] = { label, v } end
    add("nil", nil)
    for _, n in ipairs { 0, -1, 1, 3, 1.5, -0.5, 255, 65536, 2147483647, 2147483648, -2147483649, 4294967296, 9007199254740992, 1e15, 1e300, -1e300 } do add(tostring(n), n) end
    add("nan", 0 / 0) add("inf", 1 / 0) add("-inf", -1 / 0)
    if opts.strings then
        add("''", "") add("'x'", "x") add("' '", " ") add("template", "It_Apple") add("npc template", "Peasant_1_01") add("monster", "Wolf") add("skill", "Strength")
        add("long", string.rep("a", 70000)) add("nul", "a\0b") add("percent", "%s%d%n%") add("dots", "../../x") add("unicode", "\xC3\xA4\xE2\x82\xAC") add("number string", "5") add("event", "x.y")
    end
    add("true", true) add("false", false)
    add("{}", {}) add("array", { 1, 2, 3 }) add("map", { a = 1, b = "x" })
    add("nested", { {}, { {} } })
    add("deep", deep(3000))
    local cyc = {} cyc.self = cyc cyc[1] = cyc
    add("cyclic", cyc)
    add("throwing proxy", setmetatable({}, { __index = function() error("proxy boom") end, __len = function() error("len boom") end, __newindex = function() error("new boom") end }))
    add("self-returning proxy", (function() local p = {} setmetatable(p, { __index = function() return p end }) return p end)())
    if opts.strings then
        add("strings", { "It_Apple", "Wolf", "x" }) add("spec 1", spec(0)) add("spec 2", spec(0)) add("spec 3", spec(0))
        add("quest spec", { name = 5, title = {}, type = true }) add("npc spec", { name = {}, template = 5, offset = "x", routine = { work = 5, patrol = "x" } })
    end
    add("function", function() end)
    add("erroring function", function() error("callback boom") end)
    add("function returning table", function() return {} end)
    add("risen", risen)
    add("_G", _G)
    for _, e in ipairs(fixtures) do add(e[1], e[2]) end
    return b
end

-- ---- enumerating the API ---------------------------------------------------------------------------------------------------------------------
local function enumerate()
    local list, seen = {}, {}
    local function walk(tbl, prefix, depth)
        if seen[tbl] or depth > 3 then return end
        seen[tbl] = true
        local t = tbl
        local visited = {}
        -- the per-mod view of `risen` reads through to the shared table: follow the __index chain
        while type(t) == "table" and not visited[t] do
            visited[t] = true
            for k, v in pairs(t) do
                if type(k) == "string" then
                    if type(v) == "function" then
                        if not list[prefix .. k] then list[prefix .. k] = v end      -- the mod's own version wins over the shared one
                    elseif type(v) == "table" and not seen[v] and k ~= "__index" then
                        walk(v, prefix .. k .. ".", depth + 1)
                    end
                end
            end
            local mt = getmetatable(t)
            t = mt and type(mt.__index) == "table" and mt.__index or nil
        end
    end
    walk(risen, "risen.", 0)
    return list
end

local function enumerate_methods(object, prefix)
    local list = {}
    local mt = getmetatable(object)
    if type(mt) ~= "table" then return list end
    for k, v in pairs(mt) do
        if type(k) == "string" and type(v) == "function" then list[prefix .. k] = v end
    end
    return list
end

-- ---- running the calls ----------------------------------------------------------------------------------------------------------------------
local stats = { functions = 0, calls = 0, ok = 0, refused = 0, slow = {}, skipped = {} }
local frame_start = os.clock()
local function yield_if_needed()
    if os.clock() - frame_start > FRAME_BUDGET then
        T.sleep(1)
        frame_start = os.clock()
    end
end

local function labels_of(a)
    local parts = {}
    for i = 1, a.n do parts[i] = a.lab[i] end
    return table.concat(parts, ", ")
end

local function fuzz_function(name, fn, no_strings)
    stats.functions = stats.functions + 1
    risen.log.info(("[hardening] fuzz %s"):format(name))
    battery = build_battery { strings = not no_strings }
    local started = os.clock()
    local calls = 0
    local function call(a)
        calls = calls + 1
        stats.calls = stats.calls + 1
        if VERBOSE then risen.log.info(("[hardening] call %s(%s)"):format(name, labels_of(a))) end
        -- the one call that does what it says on a valid player: killing him
        if name == "Entity.kill" and a[1] ~= nil and type(a[1]) == "userdata" and a.lab[1] == "player" then return end
        local ok = pcall(fn, unpack(a, 1, a.n))
        if ok then stats.ok = stats.ok + 1 else stats.refused = stats.refused + 1 end
    end
    local function args(...)   -- args(entry1, entry2, ...) -> { n, values..., lab = labels }
        local a = { n = select("#", ...), lab = {} }
        for i = 1, a.n do
            local e = select(i, ...)
            a[i] = e[2]
            a.lab[i] = e[1]
        end
        return a
    end
    local function too_slow()
        if os.clock() - started > PER_FUNCTION_SECONDS then
            stats.slow[#stats.slow + 1] = name
            return true
        end
        return false
    end
    local function step()
        yield_if_needed()
        return not too_slow()
    end

    call({ n = 0, lab = {} })
    -- one argument, then the same value repeated 2..4 times
    for _, e in ipairs(battery) do
        if not step() then return end
        call(args(e))
        call(args(e, e))
        call(args(e, e, e))
        call(args(e, e, e, e))
    end
    -- an object (valid / stale / freed handle, another userdata) in each of the first three positions, the rest from the battery
    for _, obj in ipairs(fixtures) do
        for _, e in ipairs(battery) do
            if not step() then return end
            call(args(obj, e))
            call(args(e, obj))
            call(args(obj, e, e))
            call(args(e, obj, e))
            call(args(obj, obj, e))
            call(args(obj, e, obj))
        end
    end
    -- random arity 1..5 from the whole battery
    for _ = 1, RANDOM_CALLS do
        if not step() then return end
        local n = rnd(5)
        local picked = {}
        for i = 1, n do picked[i] = battery[rnd(#battery)] end
        call(args(unpack(picked, 1, n)))
    end
end

-- ---- the scenario ------------------------------------------------------------------------------------------------------------------------------
T.run(function()
    T.section("setup")
    T.wait_event("session.start", 180, function(m) return m == "new" end)
    T.sleep(150)
    local player = risen.entity.player()
    T.expect("world ready", player ~= nil)
    if not player then return end
    local px, py, pz = player:position()

    -- handles that will be stale after the load: one entity of each kind
    local stale_npc = risen.npc.spawn("Peasant_1_01", px + 500, py + 20, pz, { name = "HARDEN_StaleNpc" })
    local stale_item = risen.entity.spawn("It_Apple", px + 400, py + 20, pz)
    T.wait_until(function() return risen.world.is_save_allowed() end, 60, "saving allowed")
    local saved = false
    local save_handle = risen.on("save.after", function(_, ok) saved = ok end)
    risen.world.quick_save()
    T.wait_until(function() return saved end, 60, "savegame written")
    risen.off(save_handle)
    risen.world.quick_load()
    T.wait_event("session.start", 120, function(m) return m == "load" end)
    T.sleep(120)

    player = risen.entity.player()
    -- (a spawned NPC is nameless after a load, so its handle cannot be re-resolved: ptr() is null; a spawned item may keep a name, so it may still resolve)
    T.expect("handles went stale across the load", player ~= nil and stale_item ~= nil and stale_npc ~= nil and not stale_npc:is_npc(),
        ("npc is_npc=%s item is_item=%s"):format(tostring(stale_npc and stale_npc:is_npc()), tostring(stale_item and stale_item:is_item())))
    if not player then return end
    px, py, pz = player:position()
    local npc = risen.npc.spawn("Peasant_1_01", px + 300, py + 20, pz, { name = "HARDEN_Npc", peaceful = true })
    local item = risen.entity.spawn("It_Apple", px + 200, py + 20, pz)
    local freed = risen.entity.spawn("It_Apple", px + 100, py + 20, pz)
    if freed then risen.item.pick_up(player, freed) end        -- the world item is deleted by the engine: the handle now dangles (same world generation)
    T.sleep(10)
    T.expect("handle to a deleted item is stale", freed ~= nil and freed.address == 0 and not freed:is_item(),
        ("address=%s is_item=%s"):format(tostring(freed and freed.address), tostring(freed and freed:is_item())))
    local info = risen.dialog.new_info("HARDEN_Info", "HARDEN_Npc")
    local quest = risen.quest.new("Q_HARDEN")
    local template = risen.template.get("It_Apple")

    fixtures = {}
    local function fixture(label, v) if v ~= nil then fixtures[#fixtures + 1] = { label, v } end end
    fixture("player", player) fixture("npc", npc) fixture("item", item) fixture("stale npc", stale_npc) fixture("stale item", stale_item)
    fixture("freed item", freed) fixture("info", info) fixture("quest", quest) fixture("template", template)
    T.expect("fixtures exist", npc ~= nil and item ~= nil and info ~= nil and quest ~= nil and template ~= nil,
        ("npc=%s item=%s info=%s quest=%s template=%s"):format(tostring(npc), tostring(item), tostring(info), tostring(quest), tostring(template)))

    -- ---- enumerate ----
    T.section("enumerate")
    local functions = enumerate()
    for k, v in pairs(enumerate_methods(player, "Entity.")) do functions[k] = v end
    if info then for k, v in pairs(enumerate_methods(info, "Info.")) do functions[k] = v end end
    if quest then for k, v in pairs(enumerate_methods(quest, "Quest.")) do functions[k] = v end end
    if template then for k, v in pairs(enumerate_methods(template, "Template.")) do functions[k] = v end end
    local names = {}
    for k in pairs(functions) do
        local why = EXCLUDED[k] or (excluded_destructor(k) and "a destructor (the garbage collector's entry point, see EXCLUDED)")
        if not why then
            for prefix, reason in pairs(EXCLUDED_PREFIX) do if k:sub(1, #prefix) == prefix then why = reason end end
        end
        if why then
            stats.skipped[#stats.skipped + 1] = k
            risen.log.info(("[hardening] excluded %s: %s"):format(k, why))
        else
            local wanted = ONLY == nil
            if ONLY then for pattern in ONLY:gmatch("[^;]+") do if k:find(pattern) then wanted = true end end end
            if wanted then names[#names + 1] = k end
        end
    end
    table.sort(names)
    risen.log.info(("[hardening] %d functions to fuzz, %d excluded"):format(#names, #stats.skipped))
    T.expect("functions enumerated", #names >= 250 or ONLY ~= nil, tostring(#names))

    -- ---- the fuzz ----
    T.section("fuzz")
    T.allow_errors(true)       -- bad specs are logged as errors by design; the count is not a check here
    local damage = {}      -- functions after which the player was gone: a legitimate call can remove the player, a wrong-typed one should not
    for _, name in ipairs(names) do
        fuzz_function(name, functions[name], NO_STRINGS[name])
        local p = risen.entity.player()
        if not (p and p:position()) then
            damage[#damage + 1] = name
            risen.log.warn(("[hardening] the player is gone after %s"):format(name))
            break
        end
    end
    T.expect("the player survived every function", #damage == 0, table.concat(damage, ","))
    T.allow_errors(false)
    -- risen.off takes handle ids: only types that cannot be an id
    for _, v in ipairs { "x", true, {}, function() end, 0 / 0, -1 } do pcall(risen.off, v) end
    T.expect("fuzz finished", true, ("%d functions, %d calls: %d accepted, %d refused with an error, %d too slow (%s); excluded: %s"):format(
        stats.functions, stats.calls, stats.ok, stats.refused, #stats.slow, table.concat(stats.slow, ","), table.concat(stats.skipped, ",")))

    -- ---- callbacks that return or raise whatever they like ----------------------------------------------------------------------------------------------------
    -- The same battery, now as the RETURN VALUES of the functions a mod hands to the engine: a script, an AI state's begin / loop / finish, a tick handler, a condition.
    T.section("callbacks")
    T.allow_errors(true)       -- a callback that raises is logged as an error by design
    local values = build_battery { strings = true }
    local scripts, states, conds = 0, 0, 0
    for i, e in ipairs(values) do
        local v = e[2]
        if e[1] ~= "long" and e[1] ~= "deep" then                 -- (not worth a script name / a tick of copying)
            local name = "HARDEN_Ret_" .. i
            if pcall(risen.script.define, name, function() return v end) then
                scripts = scripts + 1
                pcall(risen.script.call, name)
            end
            local c = pcall(risen.cond.test, risen.cond.fn(function() return v end))
            if c then conds = conds + 1 end
        end
    end
    local seen_ticks = 0
    local garbage_handle = risen.on("tick", function() seen_ticks = seen_ticks + 1 return values[(seen_ticks % #values) + 1][2] end)
    T.sleep(#values + 5)       -- one garbage return value per frame
    risen.off(garbage_handle)
    local npc_now = npc
    for i, e in ipairs(values) do
        local v = e[2]
        if npc_now and e[1] ~= "long" and e[1] ~= "deep" and states < 24 then
            local name = "ZS_HARDEN_" .. i
            local def = { interval = 0.2, begin = function() return v end, loop = function(ctx) if ctx.count >= 2 then return v end end, finish = function() return v end }
            if risen.ai.state(name, def) then
                states = states + 1
                pcall(risen.ai.start, npc_now, name)
                T.sleep(25)
            end
        end
    end
    pcall(risen.ai.stop, npc_now)
    T.allow_errors(false)
    T.expect("hostile callbacks survived", scripts > 10 and conds > 10, ("%d scripts, %d conditions, %d AI states, %d garbage ticks"):format(scripts, conds, states, seen_ticks))

    -- ---- the game must have survived ----
    T.section("afterwards")
    local ticks = 0
    local handle = risen.on("tick", function() ticks = ticks + 1 end)
    T.sleep(60)
    risen.off(handle)
    T.expect("game still ticks", ticks >= 50, tostring(ticks))
    local again = risen.entity.player()
    T.expect("player still reachable", again ~= nil and again:position() ~= nil)
    local probe = risen.entity.spawn("It_Apple", px, py + 20, pz)
    T.expect("engine still usable", probe ~= nil and probe:is_item() and risen.template.get("It_Apple") ~= nil)

    -- the exact case that crashed the game before the argument checks were switched on: a native with a typed entity parameter
    local clean = true
    for _, f in ipairs { risen.npc.routine_points, risen.item.count, risen.item.give, risen.dialog.has_infos, risen.interact.info, risen.entity.get } do
        for _, bad in ipairs { 5, "x", true, {}, function() end } do
            local ok, a, b = pcall(f, bad, bad)
            if ok and a ~= nil and a ~= false and type(a) ~= "number" and type(a) ~= "table" then clean = false end
        end
    end
    T.expect("typed natives refuse wrong types cleanly", clean)
    pcall(risen.assets.clear)
end, "hardening")
