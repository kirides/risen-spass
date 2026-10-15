-- Plays the template editing scenarios unattended (docs/RE.md section 22) as a risen.test scenario: scripts/smoke-test.ps1 -Scenario templates.
-- Every verification is a risen.test check; the "tpdriver ..." log lines stay as the narrative of the run.
--   new game   : census -> pick an item template with many world instances -> targeted edit of the live instances (patch with
--                instances = true) and of the NPC template -> full Template:patch_instances() compared -> quick save -> edit the
--                template again without touching the instances -> quick load -> what do the loaded instances look like? -> patch them
--                -> new game: does the edit survive, and do the new world's instances use it?
--   -Start load: fresh process, the savegame of the run above: what the loaded world looks like before and after re-applying.
-- The checks of the templatepatch mod (spawn, inventory, instances, NPC template) belong to the same run.
local T = risen.test
local log = risen.log
local function L(...)
    local t = {}
    for i = 1, select("#", ...) do t[#t + 1] = tostring((select(i, ...))) end
    log.info("tpdriver " .. table.concat(t, " "))
end

T.plan {
    "templates resolve from the first tick on",
    "a new spawn uses the edited template",       -- templatepatch
    "an inventory stack follows the template",    -- templatepatch
    "existing instances were patched",            -- templatepatch
    "an NPC template property applies to spawns", -- templatepatch
    "patch with instances=true updates the world",
    "the targeted NPC patch keeps hp and group",
    "the full patch_instances resets them",
    "a full patch also resets item amounts",
    "saved values win over an edited template",
    "patching after the load updates them",
    "a new game world does not follow the edit",
}

local PICK = "It_Plant_Berry"   -- re-picked by the census of the first session
local NPC = "Wolf"
local SET, PROP = "gCItem_PS", "GoldValue"

local function get(name) return assert(risen.template.get(name)) end

-- the live instances of an item template: text for the log, data for the checks
-- data = { n, count, keys = { "value/aAmount", ... distinct, sorted }, template_value }
local function histogram(name)
    local t = risen.template.get(name)
    if not t then return "no template", { n = 0, keys = {} } end
    local list = t:instances(1000) or {}
    local h = {}
    for _, e in ipairs(list) do
        local v = tostring(e:item_value()) .. "/a" .. tostring(e:item_amount())
        h[v] = (h[v] or 0) + 1
    end
    local keys, parts = {}, {}
    for v in pairs(h) do keys[#keys + 1] = v end
    table.sort(keys)
    for i, v in ipairs(keys) do parts[i] = v .. "x" .. h[v] end
    local data = { n = #list, count = t:instance_count(), keys = keys, template_value = tostring(t:get_property(SET, PROP)) }
    return ("n=%d count=%s values={%s} template_value=%s"):format(#list, tostring(data.count), table.concat(parts, ","), data.template_value), data
end

-- every live instance carries `value` (amount 1) and the template says `template_value`
local function uniform(data, value, template_value)
    return data.n >= 1 and #data.keys == 1 and data.keys[1] == value .. "/a1" and data.template_value == tostring(template_value)
end

-- the first NPC instances: log text, entries { guild, hp, hp_max, stacks, grp }
local function npc_snapshot(label)
    local t = get(NPC)
    local parts, entries = {}, {}
    for i, e in ipairs(t:instances(4) or {}) do
        local entry = { guild = e:get_property("gCNPC_PS", "Guild"), hp = e:hp(), hp_max = e:hp_max(), stacks = e:stack_count(), grp = e:get_property("gCNPC_PS", "Group") }
        entries[i] = entry
        parts[#parts + 1] = ("[%s guild=%s hp=%s/%s stacks=%s grp=%s]"):format(i, tostring(entry.guild), tostring(entry.hp), tostring(entry.hp_max), tostring(entry.stacks), tostring(entry.grp))
    end
    L("npc " .. label, table.concat(parts, " "))
    return entries
end

local avail = {}
local function availability(where)
    local t = risen.template.get("It_Apple")
    avail[where] = t ~= nil
    L("available@" .. where, t ~= nil)
end
for _, ev in ipairs({ "mods.loaded", "session.start", "world.changed", "load.before", "load.after" }) do
    risen.on(ev, function() availability(ev) end)
end

local sessions, saved = {}, false   -- sessions[mode] = how many times session.start(mode) fired
risen.on("save.after", function(name, ok) saved = ok; L("save.after", name, ok) end)
risen.on("session.start", function(mode) sessions[mode] = (sessions[mode] or 0) + 1; L("session.start", mode, "#" .. sessions[mode]) end)
local function wait_session(mode, n, timeout)
    T.wait_until(function() return (sessions[mode] or 0) >= n end, timeout, "session.start(" .. mode .. ") #" .. n)
end

T.run(function()
    T.section("first tick")
    availability("first tick")
    T.expect("templates resolve from the first tick on", avail["mods.loaded"] == false and avail["first tick"] == true,
        ("available at mods.loaded=%s, at the first tick=%s"):format(tostring(avail["mods.loaded"]), tostring(avail["first tick"])))

    -- the autostart helper requests a new game (or a quick load) on the first tick
    T.wait_until(function() return (sessions.new or 0) + (sessions.load or 0) >= 1 end, 240, "the first session")
    if sessions.load then
        -- -Start load: a fresh process that loaded the savegame of an earlier run (an experiment, not part of the smoke test)
        T.section("fresh process after load")
        T.sleep(400)
        PICK = risen.save.get("pick", PICK)
        L("fresh process after load", PICK, (histogram(PICK)))
        npc_snapshot("fresh after load (template says " .. tostring(get(NPC):get_property("gCNPC_PS", "Guild")) .. ")")
        local ok, applied, touched = risen.template.patch(PICK, { [SET] = { [PROP] = 555 } }, { instances = true })
        L("fresh: patch 555 instances=true", ok, applied, touched)
        L("fresh: after patch", (histogram(PICK)))
        return
    end

    T.section("census and patch")
    T.sleep(150)
    local c, total = risen.template.census()
    local best, bestn = nil, 0
    for name, n in pairs(c or {}) do
        local first = get(name):instances(1)
        if name:match("^It_") and n > bestn and first and first[1] and first[1]:is_item() then best, bestn = name, n end
    end
    L("census entities", total, "best item", best, bestn)
    PICK = best or PICK
    L("baseline", PICK, (histogram(PICK)))
    local ok, applied, touched = risen.template.patch(PICK, { [SET] = { [PROP] = 999 } }, { instances = true })
    L("patch 999 instances=true", ok, applied, touched)
    local text, data = histogram(PICK)
    L("after patch", text)
    T.expect("patch with instances=true updates the world", uniform(data, 999, 999), text)

    T.section("npc")
    T.sleep(5)
    npc_snapshot("baseline")
    local victim = assert(get(NPC):instances(1))[1]
    victim:set_hp(7); victim:set_group("TPG"); risen.item.give(victim, "It_Apple", 3)
    npc_snapshot("personalised instance 1")
    ok, applied, touched = risen.template.patch(NPC, { gCNPC_PS = { Guild = "gEGuild_Pir" } }, { instances = true })
    L("npc patch Guild=Pir instances=true", ok, applied, touched)
    local after_targeted = npc_snapshot("after targeted patch")
    T.eq("the targeted NPC patch keeps hp and group", after_targeted[1], { guild = "gEGuild_Pir", hp = 7, hp_max = 180, stacks = 1, grp = "TPG" })
    L("full patch_instances", get(NPC):patch_instances())
    local after_full = npc_snapshot("after full patch_instances (whole template copied over)")
    T.eq("the full patch_instances resets them", after_full[1], { guild = "gEGuild_Pir", hp = 180, hp_max = 180, stacks = 1, grp = "" })
    -- item amounts under the full patch
    local gold = assert(risen.entity.spawn("It_Gold", 0, 0, 0))
    gold:set_item_amount(25)
    local before_amount = gold:item_amount()
    L("gold amount before patch_entity", before_amount, "template", get("It_Gold"):get_property("gCItem_PS", "Amount"))
    get("It_Gold"):patch_entity(gold)
    local after_amount = gold:item_amount()
    L("gold amount after patch_entity", after_amount)
    T.expect("a full patch also resets item amounts", before_amount == 25 and after_amount == 1, ("amount %s -> %s"):format(tostring(before_amount), tostring(after_amount)))

    T.section("save, edit the template, load")
    T.sleep(60)
    risen.save.set("pick", PICK)
    L("quick_save", risen.world.quick_save())
    T.wait_until(function() return saved end, 120, "save.after")
    T.sleep(120)
    L("edit to 555 without instances", get(PICK):set_property(SET, PROP, "555"))
    T.sleep(30)
    L("quick_load", risen.world.quick_load())
    wait_session("load", 1, 240)
    T.sleep(400)
    text, data = histogram(PICK)
    L("after load (template 555)", text)
    T.expect("saved values win over an edited template", uniform(data, 999, 555), text)
    npc_snapshot("after load")
    ok, applied, touched = risen.template.patch(PICK, { [SET] = { [PROP] = 555 } }, { instances = true })
    L("patch 555 instances=true", ok, applied, touched)
    text, data = histogram(PICK)
    L("after patch", text)
    T.expect("patching after the load updates them", uniform(data, 555, 555), text)

    T.section("second new game")
    L("new_game", risen.world.new_game())
    wait_session("new", 2, 240)
    T.sleep(400)
    text, data = histogram(PICK)
    L("second new game (template edited earlier in this process)", text)
    T.expect("a new game world does not follow the edit",
        data.n >= 1 and #data.keys == 1 and data.keys[1]:match("^%d+/a1$") ~= nil and data.keys[1]:match("^555") == nil and data.template_value == "555", text)
end, "templates")
