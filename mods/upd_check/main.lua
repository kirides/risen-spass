---@diagnostic disable: need-check-nil, param-type-mismatch, undefined-field, undefined-global
-- Live scenario `updsafe` (M15, scripts/smoke-test.ps1): what happens to a savegame when the mod that made it is updated or removed. The mod `upd_content` is played in its
-- old version (phase `save`), the savegame is kept (a pristine copy, restored before every later phase so each case starts from the same file), and each later phase is a
-- FRESH PROCESS with another version of the mod or without it. Every case has to load, tick several hundred frames, quick save and load again, and nothing may hang, crash or open a message box.
--
--   save            v1 content: NPCs with dialogue and a quest line (one running, one finished), a conditional NPC that appeared, new items / a smithing plan / a spell scroll in the
--                   inventory, a game variable, saved mod data; quick save + a pristine copy
--   same            the same version again (the control)
--   empty           the mod is still installed but declares nothing any more
--   renamed         quest line and items renamed, other dialogue options, two NPCs not declared any more
--   retire          like empty, plus risen.retire for everything the old version made
--   gone            the mod is not installed (its sidecar record is in the savegame data)
--   gone_nosidecar  not installed and the savegame has no sidecar file at all
-- The scenario needs the sandbox off (it copies the savegame files): smoke-test.ps1 runs it with -Unsafe.
local T = risen.test
local phase = T.param("phase", "save")
if not T.is_active() then return end

local GAMEDIR = mod.dir .. "/../../../.."
local SAVEFILE = GAMEDIR .. "/localappdata/Local/Risen/SaveGames/QuickSave.save"
local SIDECAR = mod.dir .. "/../../saves/QuickSave.lua"
local PRISTINE_SAVE = mod.dir .. "/../../pristine.QuickSave.save"
local PRISTINE_SIDECAR = mod.dir .. "/../../pristine.QuickSave.lua"

local function copy(from, to)
    local f = io.open(from, "rb")
    if not f then return false end
    local data = f:read("*a")
    f:close()
    local t = assert(io.open(to, "wb"))
    t:write(data)
    t:close()
    return true
end

-- every load phase starts from the pristine copy of the savegame (the phases save over it)
if phase ~= "save" and phase ~= "questprobe" then
    local a = copy(PRISTINE_SAVE, SAVEFILE)
    local b = copy(PRISTINE_SIDECAR, SIDECAR)
    risen.log.info(("updsafe: pristine savegame restored (save %s, sidecar %s)"):format(tostring(a), tostring(b)))
    if phase == "gone_nosidecar" or phase == "nosidecar" then
        os.remove(SIDECAR)
        risen.log.info("updsafe: the sidecar file was removed")
    end
end

local frames = 0
risen.on("tick", function() frames = frames + 1 end)
local function wait_world()
    local ready = false
    local h = risen.on("world.ready", function() ready = true end)
    T.wait_until(function() return ready or (T.lifecycle().world ~= nil and risen.entity.player() ~= nil) end, 180, "world.ready")
    risen.off(h)
    T.sleep(100)
end
local function save_game()
    T.wait_until(function() return risen.world.is_save_allowed() end, 30, "saving allowed")
    local saved = false
    local h = risen.on("save.after", function(_, ok) saved = ok end)
    risen.world.quick_save()
    T.wait_until(function() return saved end, 60, "savegame written")
    risen.off(h)
    T.sleep(60)
    return saved
end
local function status(q)
    local o = risen.quest.get(q)
    if not o then return "undefined" end
    return (tostring(o:get_property("Status")):gsub("^gEQuestStatus_", ""))
end
local function count_template(tpl)
    local t = risen.template.get(tpl)
    if not t then return -1 end
    local l = t:instances(200)
    return l and #l or -1
end
local function items(player)
    local out = {}
    for _, tpl in ipairs { "Spass_UP_Token", "Spass_UP_Blade", "Spass_UP_Bolt_Scroll", "Spass_UP_Plan", "Spass_UP2_Token", "Spass_UP2_Blade" } do
        local n = risen.item.count(player, tpl)
        out[#out + 1] = tpl:gsub("^Spass_", "") .. "=" .. tostring(n)
    end
    return table.concat(out, " ")
end
local function npcs()
    local out = {}
    for _, n in ipairs { "UP_Giver", "UP_Extra", "UP_Late", "UP_Plain" } do
        local e = risen.entity.get(n)
        out[#out + 1] = n .. "=" .. (e and (e:is_dead() and "dead" or "alive") or "absent")
    end
    out[#out + 1] = "census:" .. table.concat({ count_template("Spass_UP_Giver"), count_template("Spass_UP_Extra"), count_template("Spass_UP_Late") }, "/")
    return table.concat(out, " ")
end
local function infos()
    local out = {}
    for _, n in ipairs { "UP_Info_Start", "UP_Info_Running", "UP_Info_Thanks", "UP2_Info_Start", "UP2_Info_Running", "UP2_Info_Thanks" } do
        if risen.dialog.get(n) then out[#out + 1] = n end
    end
    return table.concat(out, ",")
end
local function records_state()
    -- the persistent NPCs the sidecar remembers: is the entity still in the world (nameless, without its template)?
    local rec = risen.mods.record("upd_content")
    local out = {}
    for name, r in pairs(rec and rec.persistent_npcs or {}) do
        local e = type(r) == "table" and r.id and risen.entity.by_id(r.id) or nil
        out[#out + 1] = name .. "->" .. (e and ((e:is_dead() and "dead" or "alive") .. ":" .. tostring(e:name())) or "none") .. (r.removed and "(removed)" or "")
    end
    table.sort(out)
    return table.concat(out, ",")
end
local function world_state()
    local player = risen.entity.player()
    local d = risen.mods.data("upd_content")
    return ("quests Main=%s Done=%s Main2=%s Done2=%s | var UP_Flag=%s | %s | items %s | infos [%s] | stacks %d | sidecar present=%s npcs=%s rules=%s keys=%d saved_version=%s | records [%s] | recorded %s"):format(
        status("Q_UP_Main"), status("Q_UP_Done"), status("Q_UP_Main2"), status("Q_UP_Done2"), tostring(risen.gamevar.get("UP_Flag")), npcs(), items(player), infos(), player:stack_count() or -1,
        tostring(d.present), d.npcs, d.spawn_rules, d.key_count, tostring(d.saved_version), records_state(), table.concat(risen.mods.recorded(), ","))
end
local function observe(tag)
    local s = world_state()
    risen.log.info("UPD " .. phase .. " " .. tag .. ": " .. s)
    T.expect("observe " .. tag, true, s)
    return s
end

local function converse(player, npc, info_name)
    risen.dialog.start(player, npc)
    T.sleep(90)
    assert(risen.dialog.get(info_name), info_name):run()
    T.wait_event("info.finished", 60, function(name) return name == info_name end)
    T.sleep(60)
end

-- ---- phase save ----------------------------------------------------------------------------------------------------------------------------
local function save_phase()
    T.section("the old version plays")
    T.wait_event("session.start", 180, function(m) return m == "new" end)
    wait_world()
    T.wait_until(function() return risen.entity.get("UP_Giver") and risen.entity.get("UP_Extra") and risen.entity.get("UP_Plain") end, 60, "the NPCs exist")
    T.sleep(30)
    local player = risen.entity.player()
    local giver = risen.entity.get("UP_Giver")
    converse(player, giver, "UP_Info_Start")                              -- starts Q_UP_Main (running, one log entry), the info is "given"
    T.eq("the quest line runs", status("Q_UP_Main"), "Running")
    risen.quest.open("Q_UP_Done") risen.quest.run("Q_UP_Done") risen.quest.add_log("Q_UP_Done", "An old errand.") risen.quest.succeed("Q_UP_Done")
    T.eq("the other quest is finished", status("Q_UP_Done"), "Success")
    T.wait_until(function() return risen.entity.get("UP_Late") ~= nil end, 40, "the conditional NPC appeared")
    risen.item.give(player, "Spass_UP_Token", 4)
    risen.item.give(player, "Spass_UP_Blade", 1)
    risen.item.give(player, "Spass_UP_Bolt_Scroll", 2)
    T.wait_until(function() return risen.item.count(player, "Spass_UP_Plan") > 0 end, 40, "the smithing plan was learned")
    risen.save.set("plain_id", risen.entity.get("UP_Plain"):id())
    risen.gamevar.set("UP_Flag", 3)
    risen.save.set("counter", 41)
    T.sleep(60)
    local before = observe("before saving")
    T.expect("the old version made everything", before:find("Main=Running", 1, true) and before:find("Done=Success", 1, true) and before:find("UP_Late=alive", 1, true)
        and before:find("Token=4", 1, true) and before:find("Plan=1", 1, true) and before:find("Scroll=2", 1, true), before)
    T.expect("savegame written", save_game())
    T.expect("a pristine copy of the savegame was made", copy(SAVEFILE, PRISTINE_SAVE) and copy(SIDECAR, PRISTINE_SIDECAR), SAVEFILE)
    T.finish("saved by the old version")
end

-- ---- what each case must look like (the facts of docs/RE.md section 41) -------------------------------------------------------------------------------------
local function set_of(list) local t = {} for _, v in ipairs(list) do t[v] = true end return t end

local function alive(name) local e = risen.entity.get(name) return e ~= nil and not e:is_dead() end

local function checks_for(phase)
    local d = risen.mods.data("upd_content")
    local player = risen.entity.player()
    local q = { Main = status("Q_UP_Main"), Done = status("Q_UP_Done"), Main2 = status("Q_UP_Main2"), Done2 = status("Q_UP_Done2") }
    local tpl = { count_template("Spass_UP_Giver"), count_template("Spass_UP_Extra"), count_template("Spass_UP_Late") }
    if phase == "same" then
        T.eq("same: the quests are as they were", q, { Main = "Running", Done = "Success", Main2 = "undefined", Done2 = "undefined" })
        T.expect("same: every NPC is there once (restored by ID)", alive("UP_Giver") and alive("UP_Extra") and alive("UP_Late") and alive("UP_Plain") and tpl[1] == 1 and tpl[2] == 1 and tpl[3] == 1, table.concat(tpl, "/"))
        T.eq("same: the items are back", { risen.item.count(player, "Spass_UP_Token"), risen.item.count(player, "Spass_UP_Blade"), risen.item.count(player, "Spass_UP_Bolt_Scroll"), risen.item.count(player, "Spass_UP_Plan") }, { 4, 1, 2, 1 })
        T.eq("same: the sidecar records", { d.npcs, d.spawn_rules }, { 4, 1 })
        T.eq("same: nothing is an orphan", #risen.mods.orphans("upd_content"), 0)
    elseif phase == "empty" then
        T.eq("empty: the engine dropped the statuses of the quests nobody defines", q, { Main = "undefined", Done = "undefined", Main2 = "undefined", Done2 = "undefined" })
        T.expect("empty: no NPC carries a name, the templates are gone", not alive("UP_Giver") and not alive("UP_Extra") and not alive("UP_Late") and tpl[1] == -1, table.concat(tpl, "/"))
        T.eq("empty: the game variable of the mod is still set (engine-owned)", risen.gamevar.get("UP_Flag"), 3)
        T.eq("empty: items of templates that are gone count 0", risen.item.count(player, "Spass_UP_Token"), 0)
        T.eq("empty: the mod's sidecar record is untouched (nothing declares NPCs, so nothing pruned)", { d.npcs, d.spawn_rules, d.present }, { 4, 1, true })
        local rep = risen.mods.retire("upd_content")
        T.expect("empty: a mod that declares no retirement has nothing to retire", rep and rep.removed == 0 and #rep.npcs == 0)
        local list = risen.mods.orphans("upd_content")
        T.expect("empty: the one NPC with a shipped template survived as a nameless entity: the only orphan", #list == 1 and list[1].kind == "undeclared" and list[1].name == "UP_Plain" and list[1].entity:name() ~= "UP_Plain",
            #list == 1 and (list[1].kind .. ":" .. list[1].name .. ":" .. tostring(list[1].entity:name())) or tostring(#list))
        T.eq("empty: removal needs an explicit call and removes the entity", { risen.mods.remove_orphans("upd_content", list), #risen.mods.orphans("upd_content") }, { 1, 0 })
        T.eq("empty: forget_data drops the stale record", { risen.mods.forget_data("upd_content"), risen.mods.data("upd_content").present }, { true, false })
    elseif phase == "renamed" then
        T.eq("renamed: the old quest line is gone, the new one starts fresh", q, { Main = "undefined", Done = "undefined", Main2 = "Open", Done2 = "Open" })
        T.expect("renamed: the giver is the saved NPC; the undeclared ones were pruned (M7)", alive("UP_Giver") and not alive("UP_Extra") and not alive("UP_Late") and not alive("UP_Plain") and tpl[1] == 1, table.concat(tpl, "/"))
        T.eq("renamed: the new dialogue options exist, the old ones do not", { risen.dialog.get("UP2_Info_Start") ~= nil, risen.dialog.get("UP_Info_Start") ~= nil }, { true, false })
        T.eq("renamed: records pruned, the spawn rule state of the dropped NPC lingers", { d.npcs, d.spawn_rules }, { 1, 1 })
        T.eq("renamed: no orphans", #risen.mods.orphans("upd_content"), 0)
    elseif phase == "retire" then
        T.eq("retire: the quests are gone", q, { Main = "undefined", Done = "undefined", Main2 = "undefined", Done2 = "undefined" })
        T.eq("retire: records, rule state and saved key were taken out", { d.npcs, d.spawn_rules, d.keys.counter }, { 0, 0, nil })
        T.eq("retire: the game variable was removed", risen.gamevar.exists("UP_Flag"), false)
        local pid = risen.save.get("plain_id")
        local pe = pid and risen.entity.by_id(pid)
        T.expect("retire: the nameless NPC of the shipped template was removed", pid ~= nil and pe == nil and #risen.mods.orphans("upd_content") == 0, ("id %s entity %s orphans %d"):format(tostring(pid), tostring(pe and (pe:name() .. "/" .. tostring(pe:is_dead()))), #risen.mods.orphans("upd_content")))
        local dry = risen.mods.retire("upd_content")
        T.expect("retire: a second dry run finds nothing left to remove", dry and #dry.npcs == 4 and dry.removed == 0, dry and tostring(dry.removed))
    elseif phase == "gone" then
        T.expect("gone: the mod is not installed but its record is in the savegame data", risen.mods.get("upd_content") == nil and d.present and table.concat(risen.mods.recorded(), ","):find("upd_content", 1, true) ~= nil)
        T.eq("gone: the quests are dropped", q, { Main = "undefined", Done = "undefined", Main2 = "undefined", Done2 = "undefined" })
        local list = risen.mods.orphans("upd_content")
        T.eq("gone: the manager finds the nameless NPC through the record", #list, 1)
        T.eq("gone: and removes it on request; the record goes with forget_data", { risen.mods.remove_orphans("upd_content", list), risen.mods.forget_data("upd_content"), #risen.mods.recorded() > 0 and table.concat(risen.mods.recorded(), ","):find("upd_content", 1, true) == nil }, { 1, true, true })
    elseif phase == "gone_nosidecar" then
        T.eq("gone_nosidecar: nothing is recorded about the mod", { d.present, risen.mods.orphans("upd_content") and #risen.mods.orphans("upd_content") }, { false, 0 })
    elseif phase == "nosidecar" then
        T.eq("nosidecar: the engine kept the saved NPCs (nameless) and the mod made a second one of each", tpl, { 2, 2, 2 })
        local list = risen.mods.orphans("upd_content")
        local kinds = {}
        for _, o in ipairs(list) do kinds[#kinds + 1] = o.kind end
        T.eq("nosidecar: the finder lists the three strays of the created templates, not the NPC with a shipped template", kinds, { "stray", "stray", "stray" })
        T.eq("nosidecar: removal on request", risen.mods.remove_orphans("upd_content", list), 3)
        T.sleep(30)
        T.eq("nosidecar: one NPC per template again", { count_template("Spass_UP_Giver"), count_template("Spass_UP_Extra"), count_template("Spass_UP_Late") }, { 1, 1, 1 })
        T.expect("nosidecar: the named NPCs are the new ones and alive", alive("UP_Giver") and alive("UP_Extra") and alive("UP_Late"))
    end
end

-- ---- the load phases ---------------------------------------------------------------------------------------------------------------------------
local function load_phase()
    T.section("load")
    local mode = T.wait_event("session.start", 240, function(m) return m == "load" end)
    T.eq("the savegame loaded", mode, "load")
    wait_world()
    local f0 = frames
    local first = observe("after the load")
    local player = risen.entity.player()
    T.expect("the player is alive and in the world", player ~= nil and not player:is_dead() and (player:hp() or 0) > 0, player and tostring(player:hp()) or "no player")

    T.section("several hundred frames")
    T.sleep(600)
    T.expect("the game ticked on", frames - f0 >= 590, ("%d frames"):format(frames - f0))
    observe("after 600 frames")

    T.section("what the case looks like")
    checks_for(phase)

    T.section("save and load again")
    T.expect("saved again", save_game())
    risen.world.quick_load()
    T.wait_event("session.start", 120, function(m) return m == "load" end)
    wait_world()
    local f1 = frames
    observe("after the second load")
    T.sleep(300)
    T.expect("the game ticked on after the second load", frames - f1 >= 290, ("%d frames"):format(frames - f1))
    observe("after 300 more frames")
    T.expect("the player is still alive", risen.entity.player() ~= nil and not risen.entity.player():is_dead())
    T.finish("loaded " .. phase)
end

-- ---- phase questprobe: what does the engine do when a running quest is removed while infos still depend on it? -------------------------------------------------------
local function questprobe_phase()
    T.section("remove a running quest")
    T.wait_event("session.start", 180, function(m) return m == "new" end)
    wait_world()
    T.wait_until(function() return risen.entity.get("UP_Giver") ~= nil end, 60, "the giver exists")
    T.sleep(30)
    local player = risen.entity.player()
    local giver = risen.entity.get("UP_Giver")
    converse(player, giver, "UP_Info_Start")
    T.eq("the quest runs", status("Q_UP_Main"), "Running")
    local q = risen.quest.get("Q_UP_Main")
    local log_entries = q:log_count()
    observe("before the removal")
    local removed = risen.quest.remove("Q_UP_Main")
    risen.log.info("UPD questprobe remove -> " .. tostring(removed))
    T.expect("remove answered", true, tostring(removed))
    T.sleep(30)
    observe("after the removal")
    T.expect("the quest is gone from the manager", not risen.quest.exists("Q_UP_Main"), tostring(risen.quest.exists("Q_UP_Main")))
    -- a conversation evaluates the info whose condition names the removed quest ("Running Q_UP_Main"): the engine opens a blocking message box for an unknown quest
    local f0 = frames
    risen.dialog.start(player, giver)
    T.sleep(150)
    local names = {}
    for _, info in ipairs(risen.dialog.available()) do names[#names + 1] = info:name() end
    T.expect("the conversation opened with the quest removed", true, table.concat(names, ","))
    T.expect("the game still ticks after the conversation", frames - f0 >= 140, tostring(frames - f0))
    T.sleep(200)
    T.expect("saved with the quest removed", save_game())
    risen.world.quick_load()
    T.wait_event("session.start", 120, function(m) return m == "load" end)
    wait_world()
    observe("after the load")
    T.sleep(200)
    T.finish("quest removal probed")
end

T.run(phase == "save" and save_phase or phase == "questprobe" and questprobe_phase or load_phase, "updsafe")
