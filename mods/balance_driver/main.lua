-- Plays the item balancing save/load scenario unattended and logs "baldriver ..." lines (docs/RE.md section 23).
--   new game : census -> pick an item template with many world instances and skill modifiers -> risen.balance.apply with instances = true
--              -> quick save -> edit the *template* back (a "mod that was removed") -> quick load -> what do the loaded instances hold?
--              -> new game: do the level's items follow the edited template?
local log = risen.log
local function L(...)
    local t = {}
    for i = 1, select("#", ...) do t[#t + 1] = tostring((select(i, ...))) end
    log.info("baldriver " .. table.concat(t, " "))
end

local PICK = "It_Plant_Mint"   -- re-picked by the census of the first session
local SKIP = { It_1H_Sword = true, It_2H_TwoHandedSword = true, It_Armor_Guard = true, It_Po_Health_01 = true, It_Ri_STR_Small = true, It_Plant_Berry = true }

local function mods_text(list)
    local parts = {}
    for _, m in ipairs(list or {}) do parts[#parts + 1] = ("%s%+d"):format(m.skill, m.amount) end
    return table.concat(parts, ",")
end

-- "value/modifiers x count" over every live instance, plus what the template says
local function histogram(name)
    local t = risen.template.get(name)
    if not t then return "no template" end
    local list = t:instances(100000) or {}
    local h = {}
    for _, e in ipairs(list) do
        local key = ("%s|%s"):format(tostring(e:item_value()), mods_text(e:modifiers()))
        h[key] = (h[key] or 0) + 1
    end
    local parts = {}
    for key, n in pairs(h) do parts[#parts + 1] = ("{%s}x%d"):format(key, n) end
    table.sort(parts)
    return ("n=%d instances=%s template={%s|%s}"):format(#list, table.concat(parts, ","), tostring(t:get_property("gCItem_PS", "GoldValue")), mods_text(risen.item.modifiers(name)))
end

local step, wait = "idle", 0
local function go(s) step, wait = s, 0 end
local new_games = 0
local saved = false
risen.on("save.after", function(name, ok) saved = ok; L("save.after", name, ok) end)
risen.on("session.start", function(mode)
    L("session.start", mode, "step", step)
    if mode == "new" then
        new_games = new_games + 1
        if new_games == 1 then go("census") else go("second_new_game") end
    elseif mode == "load" and step == "idle_after_load" then
        go("after_load")
    end
end)

risen.on("tick", function()
    wait = wait + 1
    if step == "census" and wait == 150 then
        local c, total = risen.template.census()
        local best, bestn = nil, 0
        for name, n in pairs(c or {}) do
            if name:match("^It_") and not SKIP[name] and n > bestn then
                local m = risen.item.modifiers(name)
                if m and #m > 0 then best, bestn = name, n end
            end
        end
        L("census entities", total, "best item", best, bestn)
        PICK = best or PICK
        L("baseline", PICK, histogram(PICK))
        local report, why = risen.balance.apply({ items = { [PICK] = { value = 77, modifies = { Strength = 7 } } } }, { instances = true })
        L("apply instances=true", report and report.ok, report and #report.applied, report and report.instances, why)
        L("after apply", PICK, histogram(PICK))
        go("save")
    elseif step == "save" and wait == 60 then
        L("quick_save", risen.world.quick_save())
        go("wait_save")
    elseif step == "wait_save" and saved and wait > 120 then
        -- "the mod is gone": put the template back without touching any entity
        L("template edit back", risen.item.set_modifiers(PICK, {}), risen.template.get(PICK):set_property("gCItem_PS", "GoldValue", "1"))
        L("template now", PICK, histogram(PICK))
        go("load")
    elseif step == "load" and wait == 30 then
        L("quick_load", risen.world.quick_load())
        go("idle_after_load")
    elseif step == "after_load" and wait == 400 then
        L("after load (saved values vs edited template)", PICK, histogram(PICK))
        L("new_game", risen.world.new_game())
        go("idle_new_game")
    elseif step == "second_new_game" and wait == 400 then
        L("second new game (template edited earlier in this process)", PICK, histogram(PICK))
        go("done")
    end
end)
