-- Plays the NPC balancing save/load scenario unattended and logs "npcdriver ..." lines (docs/RE.md section 24).
--   new game : census -> pick an NPC template with many live instances -> risen.npc.set_stats(..., { instances = true }) -> damage one instance
--              -> quick save -> edit the *template* back (a "mod that was removed") -> quick load -> what do the loaded NPCs hold (saved values?)
--              -> edit the template again (without instances) -> new game: do the level's NPCs follow the edited template?
local log = risen.log
local function L(...)
    local t = {}
    for i = 1, select("#", ...) do t[#t + 1] = tostring((select(i, ...))) end
    log.info("npcdriver " .. table.concat(t, " "))
end

local function stats(x) return assert(risen.npc.stats(x)) end
local function tpl_of(name) return assert(risen.template.get(name)) end

local SKIP = { Wolf = true, Boar = true, Stingrat = true, Ghul = true, Peasant_1_01 = true, Gnome = true, PC_Hero = true }
local PICK
local BASE   -- { hp_max, level, str } of the shipped template
local EDIT   -- the spec written by the driver

-- "hp/hp_max level str x count" over every live instance of the template, plus what the template says
local function histogram(name)
    local t = risen.template.get(name)
    if not t then return "no template" end
    local list = t:instances(100000) or {}
    local h = {}
    for _, e in ipairs(list) do
        local key = ("%d/%d|%d|%d"):format(e:hp(), e:hp_max(), e:level(), e:skill_total("Strength"))
        h[key] = (h[key] or 0) + 1
    end
    local parts = {}
    for key, n in pairs(h) do parts[#parts + 1] = ("{%s}x%d"):format(key, n) end
    table.sort(parts)
    local s = stats(name)
    return ("n=%d instances=%s template={%d|%d|%d}"):format(#list, table.concat(parts, ","), s.hp_max, s.level, s.skills.Strength)
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
    -- balance_npcs (if it runs too) verifies in the first session and then raises this flag; the driver would otherwise load/save under its feet
    local others_done = risen.gamevar.get("SPASS_BALANCE_NPCS_DONE") == 1 or wait > 6000
    if step == "census" and wait >= 150 and others_done then
        for _, n in ipairs(risen.template.list("Monster/Kapitel 2") or {}) do SKIP[n] = true end   -- touched by balance_npcs's npc_groups
        local c, total = risen.template.census()
        local best, bestn = nil, 0
        for name, n in pairs(c or {}) do
            if not SKIP[name] and not name:find("^Stingrat") and n > bestn and risen.npc.stats(name) then best, bestn = name, n end
        end
        PICK = assert(best, "no NPC template with live instances")
        L("census entities", total, "best npc template", best, bestn)
        local s = stats(PICK)
        BASE = { hp_max = s.hp_max, level = s.level, str = s.skills.Strength }
        EDIT = { hp_max = BASE.hp_max + 100, level = BASE.level + 5, skills = { Strength = BASE.str + 10 } }
        L("baseline", PICK, histogram(PICK))
        local report, why = risen.npc.set_stats(PICK, EDIT, { instances = true })
        L("set_stats instances=true", report and report.written, report and report.instances, why)
        local live = tpl_of(PICK):instances(10) or {}
        local victim = live[1]
        if victim then victim:set_hp(3) end
        L("after set_stats (one NPC damaged to 3)", PICK, histogram(PICK))
        go("save")
    elseif step == "save" and wait == 60 then
        L("quick_save", risen.world.quick_save())
        go("wait_save")
    elseif step == "wait_save" and saved and wait > 120 then
        -- "the mod is gone": put the template back without touching any entity
        local r, why = risen.npc.set_stats(PICK, { hp_max = BASE.hp_max, level = BASE.level, skills = { Strength = BASE.str } })
        L("template edit back", r and r.written, why)
        L("template now", PICK, histogram(PICK))
        go("load")
    elseif step == "load" and wait == 30 then
        L("quick_load", risen.world.quick_load())
        go("idle_after_load")
    elseif step == "after_load" and wait == 400 then
        L("after load (saved values vs the reverted template)", PICK, histogram(PICK))
        -- the mod is back: template only, as install() does on the first tick
        risen.npc.set_stats(PICK, EDIT)
        L("template edited again", PICK, histogram(PICK))
        L("new_game", risen.world.new_game())
        go("idle_new_game")
    elseif step == "second_new_game" and wait == 400 then
        L("second new game (template edited earlier in this process)", PICK, histogram(PICK))
        local report = risen.npc.set_stats(PICK, EDIT, { instances = true })
        L("propagated into the new world", report and report.instances, PICK, histogram(PICK))
        go("done")
    end
end)
