-- Plays the NPC balancing save/load scenario unattended (docs/RE.md section 24) as a risen.test scenario: scripts/smoke-test.ps1 -Scenario npcs.
-- The "npcdriver ..." log lines stay as the narrative; the checks of the balance_npcs mod belong to the same run.
--   new game : wait for balance_npcs' verification -> census -> pick an NPC template with many live instances -> risen.npc.set_stats(..., { instances = true })
--              -> damage one instance -> quick save -> edit the *template* back (a "mod that was removed") -> quick load -> what do the loaded NPCs hold
--              (saved values?) -> edit the template again (without instances) -> new game: do the level's NPCs follow the edited template?
local T = risen.test
local log = risen.log
local function L(...)
    local t = {}
    for i = 1, select("#", ...) do t[#t + 1] = tostring((select(i, ...))) end
    log.info("npcdriver " .. table.concat(t, " "))
end

T.plan {
    "the spec is applied on the first tick",               -- balance_npcs (risen.balance.install on_applied)
    "session.start writes the live NPCs",                  -- balance_npcs
    "absolute settings reach the template",                -- balance_npcs verification (36 checks in all)
    "relative settings are computed from the shipped values",
    "a pattern group scales hit points and damage",
    "a template group path works",
    "guild, attributes and loot of a human",
    "the template inventory carries the loot",
    "any property by name",
    "item groups scale the shipped gold value",
    "a new spawn has the template numbers",
    "...also through the entity API",
    "the engine pays 10 * level for a kill",
    "spawns carry the loot",
    "every world wolf was rewritten",
    "every world boar was rewritten",
    "every world stingrat was rewritten",
    "applying again keeps a damaged NPC damaged",
    "applying again does not compound (template)",
    "applying again does not compound (world)",
    "set_stats on an entity touches only it",
    "conflicting settings are refused",
    "unknown settings are refused",
    "unknown skills are refused with the list",
    "out-of-range values are refused",
    "non-NPC templates are refused",
    "unknown enum names are refused",
    "an empty group is refused",
    "rejected specs write nothing",
    "combat: the shipped wolf bites for Strength - protection",
    "combat: the balanced wolf bites harder",
    "the edit survives spawn init and AI ticks",
    "driver: set_stats writes the live NPCs",
    "driver: saved values win over a reverted template",
    "driver: a new game world keeps the shipped values",
    "driver: propagation fixes the new world",
}

local function stats(x) return assert(risen.npc.stats(x)) end
local function tpl_of(name) return assert(risen.template.get(name)) end

local SKIP = { Wolf = true, Boar = true, Stingrat = true, Ghul = true, Peasant_1_01 = true, Gnome = true, PC_Hero = true }
local PICK
local BASE   -- { hp_max, level, str } of the shipped template
local EDIT   -- the spec written by the driver

-- every live instance of a template (hp, hp_max, level, Strength) plus what the template says: text for the log, data for the checks
local function histogram(name)
    local t = risen.template.get(name)
    if not t then return "no template", { items = {}, n = 0, template = {} } end
    local list = t:instances(100000) or {}
    local h, items = {}, {}
    for i, e in ipairs(list) do
        local item = { hp = e:hp(), hp_max = e:hp_max(), level = e:level(), str = (e:skill_total("Strength")) }
        items[i] = item
        local key = ("%d/%d|%d|%d"):format(item.hp, item.hp_max, item.level, item.str)
        h[key] = (h[key] or 0) + 1
    end
    local parts = {}
    for key, n in pairs(h) do parts[#parts + 1] = ("{%s}x%d"):format(key, n) end
    table.sort(parts)
    local s = stats(name)
    local template = { hp_max = s.hp_max, level = s.level, str = s.skills.Strength }
    return ("n=%d instances=%s template={%d|%d|%d}"):format(#list, table.concat(parts, ","), s.hp_max, s.level, s.skills.Strength),
        { items = items, n = #list, template = template }
end

-- how the instances relate to `want` = { hp_max, level, str }: how many carry exactly these numbers at full health, how many are damaged
-- to `damaged_hp` (same numbers otherwise), how many differ
local function classify(data, want, damaged_hp)
    local full, damaged, other = 0, 0, 0
    for _, e in ipairs(data.items) do
        if e.hp_max == want.hp_max and e.level == want.level and e.str == want.str then
            if e.hp == want.hp_max then full = full + 1 elseif e.hp == damaged_hp then damaged = damaged + 1 else other = other + 1 end
        else
            other = other + 1
        end
    end
    return full, damaged, other
end

local sessions, saved = {}, false   -- sessions[mode] = how many times session.start(mode) fired
risen.on("save.after", function(name, ok) saved = ok; L("save.after", name, ok) end)
risen.on("session.start", function(mode) sessions[mode] = (sessions[mode] or 0) + 1; L("session.start", mode, "#" .. sessions[mode]) end)
local function wait_session(mode, n, timeout)
    T.wait_until(function() return (sessions[mode] or 0) >= n end, timeout, "session.start(" .. mode .. ") #" .. n)
end

T.run(function()
    T.section("first session")
    wait_session("new", 1, 240)  -- the autostart helper requests the new game on the first tick
    -- balance_npcs (if it runs too) verifies in the first session and then raises this flag; the driver would otherwise load/save under its feet
    T.wait_until(function() return risen.gamevar.get("SPASS_BALANCE_NPCS_DONE") == 1 end, 300, "balance_npcs verification")
    T.sleep(5)

    T.section("census and set_stats")
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
    local want = { hp_max = EDIT.hp_max, level = EDIT.level, str = EDIT.skills.Strength }
    L("baseline", PICK, (histogram(PICK)))
    local report, why = risen.npc.set_stats(PICK, EDIT, { instances = true })
    L("set_stats instances=true", report and report.written, report and report.instances, why)
    local live = tpl_of(PICK):instances(10) or {}
    local victim = live[1]
    if victim then victim:set_hp(3) end
    local text, data = histogram(PICK)
    L("after set_stats (one NPC damaged to 3)", PICK, text)
    local full, damaged, other = classify(data, want, 3)
    T.expect("driver: set_stats writes the live NPCs", data.n >= 2 and damaged == 1 and other == 0 and full == data.n - 1
        and data.template.hp_max == want.hp_max and data.template.level == want.level and data.template.str == want.str,
        ("%s (full %d, damaged %d, other %d)"):format(text, full, damaged, other))

    T.section("save, edit the template back, load")
    T.sleep(60)
    L("quick_save", risen.world.quick_save())
    T.wait_until(function() return saved end, 120, "save.after")
    T.sleep(120)
    -- "the mod is gone": put the template back without touching any entity
    local r, why2 = risen.npc.set_stats(PICK, { hp_max = BASE.hp_max, level = BASE.level, skills = { Strength = BASE.str } })
    L("template edit back", r and r.written, why2)
    L("template now", PICK, (histogram(PICK)))
    T.sleep(30)
    L("quick_load", risen.world.quick_load())
    wait_session("load", 1, 240)
    T.sleep(400)
    text, data = histogram(PICK)
    L("after load (saved values vs the reverted template)", PICK, text)
    full, damaged, other = classify(data, want, 3)
    T.expect("driver: saved values win over a reverted template", data.n >= 2 and damaged == 1 and other == 0 and full == data.n - 1
        and data.template.hp_max == BASE.hp_max and data.template.hp_max ~= want.hp_max,
        ("%s (full %d, damaged %d, other %d)"):format(text, full, damaged, other))
    -- the mod is back: template only, as install() does on the first tick
    risen.npc.set_stats(PICK, EDIT)
    L("template edited again", PICK, (histogram(PICK)))

    T.section("second new game")
    L("new_game", risen.world.new_game())
    wait_session("new", 2, 240)
    T.sleep(400)
    text, data = histogram(PICK)
    L("second new game (template edited earlier in this process)", PICK, text)
    local first = data.items[1]
    local shipped = data.n >= 1
    for _, e in ipairs(data.items) do
        shipped = shipped and e.hp == e.hp_max and e.hp_max == first.hp_max and e.level == first.level and e.str == first.str
    end
    T.expect("driver: a new game world keeps the shipped values", shipped and first.hp_max ~= data.template.hp_max, text)
    report = risen.npc.set_stats(PICK, EDIT, { instances = true })
    text, data = histogram(PICK)
    L("propagated into the new world", report and report.instances, PICK, text)
    full = classify(data, want, 3)
    T.expect("driver: propagation fixes the new world", report ~= nil and data.n >= 1 and report.instances == data.n and full == data.n
        and data.template.hp_max == want.hp_max, text)
end, "npcs")
