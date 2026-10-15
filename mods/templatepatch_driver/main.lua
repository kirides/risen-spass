-- Plays the template editing scenarios unattended and logs "tpdriver ..." lines (docs/RE.md section 22).
--   new game   : census -> pick an item template with many world instances -> targeted edit of the live instances (patch with
--                instances = true) and of the NPC template -> full Template:patch_instances() compared -> quick save -> edit the
--                template again without touching the instances -> quick load -> what do the loaded instances look like? -> patch them
--                -> new game: does the edit survive, and do the new world's instances use it?
--   -Start load: fresh process, the savegame of the run above: what the loaded world looks like before and after re-applying.
local log = risen.log
local function L(...)
    local t = {}
    for i = 1, select("#", ...) do t[#t + 1] = tostring((select(i, ...))) end
    log.info("tpdriver " .. table.concat(t, " "))
end

local PICK = "It_Plant_Berry"   -- re-picked by the census of the first session
local NPC = "Wolf"
local SET, PROP = "gCItem_PS", "GoldValue"

local function get(name) return assert(risen.template.get(name)) end

local function histogram(name)
    local t = risen.template.get(name)
    if not t then return "no template" end
    local list = t:instances(1000) or {}
    local h = {}
    for _, e in ipairs(list) do
        local v = tostring(e:item_value()) .. "/a" .. tostring(e:item_amount())
        h[v] = (h[v] or 0) + 1
    end
    local parts = {}
    for v, n in pairs(h) do parts[#parts + 1] = v .. "x" .. n end
    table.sort(parts)
    return ("n=%d count=%s values={%s} template_value=%s"):format(#list, tostring(t:instance_count()), table.concat(parts, ","), tostring(t:get_property(SET, PROP)))
end

local function npc_snapshot(label)
    local t = get(NPC)
    local parts = {}
    for i, e in ipairs(t:instances(4) or {}) do
        parts[#parts + 1] = ("[%s guild=%s hp=%s/%s stacks=%s grp=%s]"):format(i, tostring(e:get_property("gCNPC_PS", "Guild")), tostring(e:hp()), tostring(e:hp_max()), tostring(e:stack_count()), tostring(e:get_property("gCNPC_PS", "Group")))
    end
    L("npc " .. label, table.concat(parts, " "))
end

local function availability(where)
    local t = risen.template.get("It_Apple")
    L("available@" .. where, t ~= nil)
end
for _, ev in ipairs({ "mods.loaded", "session.start", "world.changed", "load.before", "load.after" }) do
    risen.on(ev, function() availability(ev) end)
end

local step, wait = "idle", 0
local function go(s) step, wait = s, 0 end
local first_tick_done = false
local new_games = 0
local saved = false
risen.on("save.after", function(name, ok) saved = ok; L("save.after", name, ok) end)
risen.on("session.start", function(mode)
    L("session.start", mode, "step", step)
    if mode == "new" then
        new_games = new_games + 1
        if new_games == 1 then go("census") else go("second_new_game") end
    elseif mode == "load" then
        if step == "idle_after_load" then go("after_load")
        else go("fresh_after_load") end  -- the first session of this process is a load
    end
end)
risen.on("tick", function()
    if not first_tick_done then first_tick_done = true; availability("first tick") end
    wait = wait + 1
    if step == "census" and wait == 150 then
        local c, total = risen.template.census()
        local best, bestn = nil, 0
        for name, n in pairs(c or {}) do
            local first = get(name):instances(1)
            if name:match("^It_") and n > bestn and first and first[1] and first[1]:is_item() then best, bestn = name, n end
        end
        L("census entities", total, "best item", best, bestn)
        PICK = best or PICK
        L("baseline", PICK, histogram(PICK))
        local ok, applied, touched = risen.template.patch(PICK, { [SET] = { [PROP] = 999 } }, { instances = true })
        L("patch 999 instances=true", ok, applied, touched)
        L("after patch", histogram(PICK))
        go("npc")
    elseif step == "npc" and wait == 5 then
        npc_snapshot("baseline")
        local victim = assert(get(NPC):instances(1))[1]
        victim:set_hp(7); victim:set_group("TPG"); risen.item.give(victim, "It_Apple", 3)
        npc_snapshot("personalised instance 1")
        local ok, applied, touched = risen.template.patch(NPC, { gCNPC_PS = { Guild = "gEGuild_Pir" } }, { instances = true })
        L("npc patch Guild=Pir instances=true", ok, applied, touched)
        npc_snapshot("after targeted patch")
        L("full patch_instances", get(NPC):patch_instances())
        npc_snapshot("after full patch_instances (whole template copied over)")
        -- item amounts under the full patch
        local gold = assert(risen.entity.spawn("It_Gold", 0, 0, 0))
        gold:set_item_amount(25)
        L("gold amount before patch_entity", gold:item_amount(), "template", get("It_Gold"):get_property("gCItem_PS", "Amount"))
        get("It_Gold"):patch_entity(gold)
        L("gold amount after patch_entity", gold:item_amount())
        go("save")
    elseif step == "save" and wait == 60 then
        risen.save.set("pick", PICK)
        L("quick_save", risen.world.quick_save())
        go("wait_save")
    elseif step == "wait_save" and saved and wait > 120 then
        local t = get(PICK)
        L("edit to 555 without instances", t:set_property(SET, PROP, "555"))
        go("load")
    elseif step == "load" and wait == 30 then
        L("quick_load", risen.world.quick_load())
        go("idle_after_load")
    elseif step == "after_load" and wait == 400 then
        L("after load (template 555)", histogram(PICK))
        npc_snapshot("after load")
        local ok, applied, touched = risen.template.patch(PICK, { [SET] = { [PROP] = 555 } }, { instances = true })
        L("patch 555 instances=true", ok, applied, touched)
        L("after patch", histogram(PICK))
        L("new_game", risen.world.new_game())
        go("idle_new_game")
    elseif step == "second_new_game" and wait == 400 then
        L("second new game (template edited earlier in this process)", histogram(PICK))
        go("done")
    elseif step == "fresh_after_load" and wait == 400 then
        PICK = risen.save.get("pick", PICK)
        L("fresh process after load", PICK, histogram(PICK))
        npc_snapshot("fresh after load (template says " .. tostring(get(NPC):get_property("gCNPC_PS", "Guild")) .. ")")
        local ok, applied, touched = risen.template.patch(PICK, { [SET] = { [PROP] = 555 } }, { instances = true })
        L("fresh: patch 555 instances=true", ok, applied, touched)
        L("fresh: after patch", histogram(PICK))
        go("done")
    end
end)
