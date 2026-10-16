---@diagnostic disable: need-check-nil, param-type-mismatch, undefined-field, undefined-global
-- Live scenario `hotreload` (M15, scripts/smoke-test.ps1; guide: docs/MODDING_GUIDE.md "Hot reload"). Needs the Lua sandbox off: the scenario edits the
-- files of the mod hr_target on disk like a modder would. Phases (risen.test param "phase"):
--   main      new game: hr_target v1 + hr_dependent; every kind of registration is exercised, then the files of hr_target are rewritten (version 1.1.0) and
--             the mod is reloaded; failed reloads, the file watcher, unload / disable / enable, a savegame round trip in the same process
--   save_v1   new game, nothing is edited: leaves a quick save written by version 1.0.0
--   load_v2   a fresh process whose hr_target is version 1.1.0 (the fixture copied over it) loads that save: the migration runs, nothing is doubled
local T = risen.test
local phase = T.param("phase", "main")
if not T.is_active() then return end

-- ---- what the target mod reports -------------------------------------------------------------------------------------------------------------------------
local hits = {}
risen.on("hr.hit", function(kind, tag) local k = kind .. ":" .. tag hits[k] = (hits[k] or 0) + 1 end)
local function n(kind, tag) return hits[kind .. ":" .. tag] or 0 end

local function who()
    local list = {}
    risen.emit("hr.who", list)
    table.sort(list, function(a, b) return a.who < b.who end)
    return list
end
local function who_str()
    local parts = {}
    for _, e in ipairs(who()) do parts[#parts + 1] = e.who .. ":" .. e.tag end
    return table.concat(parts, ",")
end
local function target_entry() for _, e in ipairs(who()) do if e.who == "target" then return e end end end

local function read(path) local f = assert(io.open(path, "rb")) local s = f:read("*a") f:close() return s end
local function write(path, text) local f = assert(io.open(path, "wb")) f:write(text) f:close() end
local function install_fixture(version_dir)
    local dir = risen.mods.get("hr_target").dir
    for _, name in ipairs { "mod.toml", "version.lua", "main.lua" } do
        write(dir .. "/" .. name, read(mod.dir .. "/fixtures/" .. version_dir .. "/hr_target/" .. name))
    end
    return dir
end

local function count_template(tpl) local t = risen.template.get(tpl) local l = t and t:instances(200) return l and #l or -1 end
local function save_game()
    T.wait_until(function() return risen.world.is_save_allowed() end, 20, "saving allowed")
    local saved = false
    local h = risen.on("save.after", function(_, ok) saved = ok end)
    risen.world.quick_save()
    T.wait_until(function() return saved end, 60, "savegame written")
    risen.off(h)
    T.sleep(60)   -- the game must not be killed right after the write
end
local world_ready, world_seen = 0, 0
risen.on("world.ready", function() world_ready = world_ready + 1 end)
local function wait_world()
    T.wait_until(function() return world_ready > world_seen end, 180, "world.ready")
    world_seen = world_ready
    T.sleep(100)
end
local function wait_npcs(names)
    T.wait_until(function() for _, nm in ipairs(names) do if not risen.entity.get(nm) then return false end end return true end, 60, "persistent NPCs exist")
    T.sleep(30)
end

-- ---- phase main --------------------------------------------------------------------------------------------------------------------------------------------------------------------
local function main()
    T.section("baseline: version 1")
    T.wait_event("session.start", 180, function(m) return m == "new" end)
    wait_world()
    wait_npcs { "HR_Guy", "HR_Old" }
    T.eq("both mods answer, once each", who_str(), "dependent:dep1,target:v1")
    local e1 = target_entry()
    T.eq("first instance, no carried state", { instance = e1.instance, carried = e1.carried_reason or "none", runs = e1.runs }, { instance = 1, carried = "none", runs = 1 })
    local view = risen.mods.get("hr_target")
    T.eq("the mod list describes it", { view.state, view.version, view.load_count, view.needed_by, view.requires, view.reloadable, view.enabled },
        { "loaded", "1.0.0", 1, { "hr_dependent" }, {}, true, true })
    T.eq("data left in the savegame: two persistent NPCs", { present = view.data.present, npcs = view.data.npcs, runs = view.data.keys.runs }, { present = true, npcs = 2, runs = 1 })
    T.eq("the engine-owned content the mod defined is listed", { quests = view.content.quests, dialogs = view.content.dialogs }, { quests = { "Q_HR_Hot" }, dialogs = { "HR_Hot_Info" } })
    T.eq("hr_dependent is described with its dependency", { risen.mods.get("hr_dependent").requires, risen.mods.get("hr_dependent").state }, { { "hr_target" }, "loaded" })
    local order = risen.mods.order().current
    local pos = {}
    for i, id in ipairs(order) do pos[id] = i end
    T.expect("hr_target is ahead of hr_dependent in the load order", pos.hr_target and pos.hr_dependent and pos.hr_target < pos.hr_dependent, table.concat(order, ","))

    local player = risen.entity.player()
    local guy, old = risen.entity.get("HR_Guy"), risen.entity.get("HR_Old")
    local guy_id, old_id = guy:id(), old:id()
    T.eq("registrations of version 1 work", risen.script.call("HR_Script"), 11)
    risen.script.call("GiveXP", player, nil, 0)
    T.eq("the script override of version 1 saw the call", { n("script", "v1"), n("override", "v1") }, { 1, 1 })
    local victim = assert(risen.npc.spawn("Peasant_1_01", select(1, player:position()) + 700, select(2, player:position()), select(3, player:position()), { name = "HR_Victim", hp_max = 4000 }))
    risen.script.call("DoDamage", player, victim, 5)
    T.eq("the damage handler of version 1 saw the hit", n("damage", "v1"), 1)
    T.eq("the AI state starts", risen.ai.start(guy, "ZS_HR_Mark"), true)
    T.wait_until(function() return n("state_loop", "v1") >= 2 and n("perceive", "v1") >= 1 end, 30, "state loop and perception of version 1")
    T.eq("the Lua state runs on the NPC", risen.ai.current(guy) and risen.ai.current(guy).state, "ZS_HR_Mark")
    T.eq("the version 1 numbers are in the game", tonumber(risen.template.get("It_Apple"):get_property("gCItem_PS", "GoldValue")), 17)

    T.section("edit the files on disk, reload")
    local ticks_before = target_entry().ticks
    local dir = install_fixture("v2")
    write(risen.mods.get("hr_dependent").dir .. "/main.lua", read(risen.mods.get("hr_dependent").dir .. "/main.lua"):gsub('"dep1"', '"dep2"'))
    local snapshot = {}
    for k, v in pairs(hits) do snapshot[k] = v end
    local rep, err = risen.mods.reload("hr_target", { now = true })
    T.expect("the reload succeeded", rep and rep.ok, err)
    local ids = {}
    for _, a in ipairs(rep.actions) do ids[#ids + 1] = a.id .. ":" .. a.action end
    T.eq("hr_target is reloaded first, then the mod that requires it", ids, { "hr_target:reloaded", "hr_dependent:reloaded" })
    local a = rep.actions[1]
    T.eq("versions in the report", { a.version_before, a.version_after }, { "1.0.0", "1.1.0" })
    T.expect("handlers were removed and added", a.handlers_removed >= 3 and a.handlers_added >= 3, ("-%d +%d"):format(a.handlers_removed, a.handlers_added))
    local released = table.concat(a.released, "|")
    T.expect("the report lists what was released", released:find("script override GiveXP", 1, true) and released:find("AI state ZS_HR_Mark", 1, true) and released:find("perception handler", 1, true)
        and released:find("damage handler", 1, true) and released:find("script HR_Script", 1, true), released)
    T.expect("the quest and the dialogue info the engine owns cannot be edited: the report says so", #a.restart_required == 2 and table.concat(a.restart_required, "|"):find("quest 'Q_HR_Hot'", 1, true) and table.concat(a.restart_required, "|"):find("dialogue info 'HR_Hot_Info'", 1, true), table.concat(a.restart_required, "|"))
    T.eq("... and the list carries them", #risen.mods.get("hr_target").restart_required, 2)
    T.eq("only the new version answers, once", who_str(), "dependent:dep2,target:v2")
    local e2 = target_entry()
    T.eq("the new instance got the state of the old one", { e2.instance, e2.carried_reason, e2.from_version }, { 2, "reload", "1.0.0" })
    T.expect("ticks were carried over", e2.ticks >= ticks_before, ("%d -> %d"):format(ticks_before, e2.ticks))
    T.eq("risen.save data survived and the migration ran", { e2.runs, e2.migrated_from, e2.runs_x100 }, { 2, "1.0.0", 200 })
    view = risen.mods.get("hr_target")
    T.eq("the list shows the new manifest", { view.version, view.load_count, risen.mods.get("hr_dependent").load_count }, { "1.1.0", 2, 2 })

    T.section("nothing of version 1 is left")
    T.eq("the script is the new one", risen.script.call("HR_Script"), 22)
    risen.script.call("GiveXP", player, nil, 0)
    risen.script.call("DoDamage", player, victim, 5)
    T.sleep(5)
    T.eq("version 2 answered, version 1 did not", { n("script", "v2"), n("script", "v1"), n("override", "v1"), n("override", "v2"), n("damage", "v1"), n("damage", "v2") },
        { 1, snapshot["script:v1"], snapshot["override:v1"], 0, snapshot["damage:v1"], 0 })
    T.eq("the NPC left the retired state (back to the routine)", risen.ai.current(guy), nil)
    T.eq("no finish callback of version 1 ran during the teardown", n("state_finish", "v1"), snapshot["state_finish:v1"] or 0)
    local notices = n("perceive", "v1")
    T.eq("the state can be started again with its new definition", risen.ai.start(guy, "ZS_HR_Mark"), true)
    T.wait_until(function() return n("state_loop", "v2") >= 2 and n("perceive", "v2") >= 1 end, 30, "state loop and perception of version 2")
    T.eq("version 1 loops and perception handlers stayed silent", { n("state_loop", "v1"), n("perceive", "v1"), n("state_begin", "v1") }, { snapshot["state_loop:v1"], notices, snapshot["state_begin:v1"] })
    risen.ai.stop(guy)
    T.eq("the version 2 numbers are in the game", tonumber(risen.template.get("It_Apple"):get_property("gCItem_PS", "GoldValue")), 23)

    T.section("persistent NPCs across the reload")
    wait_npcs { "HR_Guy", "HR_Guy2" }
    T.eq("HR_Guy is the same entity", risen.entity.get("HR_Guy"):id(), guy_id)
    T.wait_until(function() return risen.entity.get("HR_Old") == nil end, 20, "HR_Old (no longer declared) is removed")
    T.eq("one live entity per template", { count_template("Spass_HR_Guy"), count_template("Spass_HR_Guy2") }, { 1, 1 })
    local guy2_id = risen.entity.get("HR_Guy2"):id()

    T.section("failed reloads")
    local main_path = dir .. "/main.lua"
    local good = read(main_path)
    write(main_path, "risen.on('hr.who', function(list) list[#list + 1] = { who = 'target', tag = 'broken' } end\n")
    local r1, e1text = risen.mods.reload("hr_target", { now = true })
    T.expect("a syntax error is refused and the old version keeps running", r1 and not r1.ok and e1text and #r1.actions == 0 and who_str() == "dependent:dep2,target:v2", tostring(e1text))
    write(main_path, "risen.on('hr.who', function(list) list[#list + 1] = { who = 'target', tag = 'half' } end)\nerror('boom at load')\n")
    T.allow_errors(true)   -- the failed load is logged as an error on purpose
    local r2, e2text = risen.mods.reload("hr_target", { now = true })
    T.expect("a runtime error leaves the mod failed, with nothing half-registered", r2 and not r2.ok and tostring(e2text):find("boom at load") and who_str() == "", tostring(e2text) .. " / " .. who_str())
    view = risen.mods.get("hr_target")
    T.eq("the list says failed and why", { view.state, view.kind, tostring(view.reason):find("boom at load") ~= nil }, { "failed", "load_error", true })
    T.eq("the dependent was taken down with it", { risen.mods.get("hr_dependent").state, risen.mods.get("hr_dependent").kind }, { "skipped", "dependency_unavailable" })
    T.expect("the world is still there (HR_Guy)", risen.entity.get("HR_Guy") ~= nil)
    write(main_path, good)
    T.allow_errors(false)
    local r3 = risen.mods.reload("hr_target", { now = true })
    T.expect("fixed on disk: the reload works from the failed state", r3 and r3.ok and who_str() == "target:v2", who_str())
    local r4 = risen.mods.load("hr_dependent")
    T.expect("the skipped dependent is loaded again", r4 and r4.ok and who_str() == "dependent:dep2,target:v2", who_str())
    T.eq("a refused reload names the problem", select(2, risen.mods.reload("nobody_here", { now = true })) ~= nil, true)

    T.section("the file watcher")
    local dep_path = risen.mods.get("hr_dependent").dir .. "/main.lua"
    T.eq("the watcher starts", risen.mods.watch(true, 0.3), true)
    T.sleep(40)
    write(dep_path, read(dep_path):gsub('"dep2"', '"dep3"'))
    T.wait_until(function() return who_str():find("dependent:dep3", 1, true) ~= nil end, 30, "the watcher reloads the changed mod")
    T.eq("only the changed mod was reloaded", { risen.mods.get("hr_dependent").load_count, who_str() }, { risen.mods.get("hr_dependent").load_count, "dependent:dep3,target:v2" })
    risen.mods.watch(false)

    T.section("a request file from outside the game")
    local request = mod.dir .. "/../../reload.txt"
    local count_before, target_before = risen.mods.get("hr_dependent").load_count, risen.mods.get("hr_target").load_count
    write(request, "hr_dependent\n")
    T.wait_until(function() return risen.mods.get("hr_dependent").load_count > count_before end, 20, "the request file is picked up")
    T.expect("the request file was consumed and only that mod reloaded", io.open(request, "rb") == nil and risen.mods.get("hr_target").load_count == target_before and who_str() == "dependent:dep3,target:v2", who_str())

    T.section("unload, disable, enable")
    local refused, why = risen.mods.disable("hr_target")
    T.expect("disabling a mod that others require is refused", refused and not refused.ok and tostring(why):find("hr_dependent", 1, true), tostring(why))
    local un, why2 = risen.mods.unload("hr_target")
    T.expect("so is unloading it", un and not un.ok and tostring(why2):find("hr_dependent", 1, true), tostring(why2))
    local d = risen.mods.disable("hr_target", { cascade = true })
    T.expect("with cascade both are disabled and unloaded", d and d.ok and #d.actions == 2 and who_str() == "", d and d.summary)
    T.eq("the list and mods.toml say so", { risen.mods.get("hr_target").state, risen.mods.get("hr_target").enabled, risen.mods.get("hr_dependent").state, risen.mods.config().enabled.hr_target },
        { "disabled", false, "disabled", false })
    T.expect("the NPC stays in the world while the mod is off", risen.entity.get("HR_Guy") ~= nil and risen.entity.get("HR_Guy"):id() == guy_id)
    local ge = risen.mods.enable("hr_dependent")
    T.expect("enabling the dependent alone is refused (its requirement is off)", ge and not ge.ok, ge and ge.error)
    local en = risen.mods.enable("hr_dependent", { cascade = true })
    T.expect("with cascade both come back, requirement first", en and en.ok and who_str() == "dependent:dep3,target:v2", who_str() .. " / " .. tostring(en and en.summary))
    T.eq("the disabled flags are gone from the file", { risen.mods.config().enabled.hr_target, risen.mods.config().enabled.hr_dependent }, { true, true })
    T.eq("a custom order that breaks a dependency is refused", select(1, risen.mods.check_order({ "hr_dependent", "hr_target" })), nil)
    T.eq("a valid one is accepted", risen.mods.check_order({ "hr_target", "hr_dependent" }), true)
    T.eq("set_order(nil) clears it", risen.mods.set_order(nil), true)

    T.section("savegame round trip after all that")
    wait_npcs { "HR_Guy", "HR_Guy2" }
    local runs = target_entry().runs
    save_game()
    risen.world.quick_load()
    T.wait_event("session.start", 120, function(m) return m == "load" end)
    wait_world()
    wait_npcs { "HR_Guy", "HR_Guy2" }
    T.eq("after the load: each mod answers once", who_str(), "dependent:dep3,target:v2")
    T.eq("the NPCs are the saved entities, none made twice", { risen.entity.get("HR_Guy"):id(), risen.entity.get("HR_Guy2"):id(), count_template("Spass_HR_Guy"), count_template("Spass_HR_Guy2") }, { guy_id, guy2_id, 1, 1 })
    T.eq("the mod's data came back", target_entry().runs, runs)
    T.eq("the reloaded mod still works", risen.script.call("HR_Script"), 22)
    T.expect("no leftovers of the removed NPC", risen.entity.get("HR_Old") == nil)
    T.finish("hot reload done")
end

-- ---- phase save_v1 -----------------------------------------------------------------------------------------------------------------------------------------------------------
local function save_v1()
    T.section("version 1 saves")
    T.wait_event("session.start", 180, function(m) return m == "new" end)
    wait_world()
    wait_npcs { "HR_Guy", "HR_Old" }
    T.eq("version 1 runs", who_str(), "dependent:dep1,target:v1")
    risen.save.set("guy_id", risen.entity.get("HR_Guy"):id())
    risen.save.set("old_id", risen.entity.get("HR_Old"):id())
    save_game()
    T.finish("saved by 1.0.0")
end

-- ---- phase load_v2 -----------------------------------------------------------------------------------------------------------------------------------------------------------
local function load_v2()
    T.section("version 1.1.0 loads a save of 1.0.0")
    T.wait_event("session.start", 180, function(m) return m == "load" end)
    wait_world()
    wait_npcs { "HR_Guy", "HR_Guy2" }
    T.eq("version 1.1.0 runs, once", who_str(), "dependent:dep1,target:v2")
    local e = target_entry()
    T.eq("the migration ran at load.before and rewrote the saved data", { e.migrated_from, e.runs_x100, e.runs }, { "1.0.0", 100, 1 })
    T.eq("the savegame remembers which version wrote it", risen.mods.saved_version("hr_target") ~= nil, true)
    T.eq("HR_Guy is the saved entity", risen.entity.get("HR_Guy"):id(), risen.save.get("guy_id"))
    T.wait_until(function() return risen.entity.get("HR_Old") == nil end, 20, "HR_Old (no longer declared) is removed")
    T.eq("one entity per template (the removed NPC's template does not exist any more)", { count_template("Spass_HR_Guy"), count_template("Spass_HR_Guy2"), count_template("Spass_HR_Old") }, { 1, 1, -1 })
    T.eq("the new version's content", { risen.script.call("HR_Script"), tonumber(risen.template.get("It_Apple"):get_property("gCItem_PS", "GoldValue")) }, { 22, 23 })
    T.finish("loaded by 1.1.0")
end

T.run(phase == "save_v1" and save_v1 or phase == "load_v2" and load_v2 or main, "hotreload")
