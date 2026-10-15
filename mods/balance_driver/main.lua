-- Plays the item balancing save/load scenario unattended (docs/RE.md section 23) as a risen.test scenario: scripts/smoke-test.ps1 -Scenario balance.
-- The "baldriver ..." log lines stay as the narrative; the checks of the balance_items mod belong to the same run.
--   new game : wait for balance_items' verification -> census -> pick an item template with many world instances and skill modifiers
--              -> risen.balance.apply with instances = true -> quick save -> edit the *template* back (a "mod that was removed")
--              -> quick load -> what do the loaded instances hold? -> new game: do the level's items follow the edited template?
local T = risen.test
local log = risen.log
local function L(...)
    local t = {}
    for i = 1, select("#", ...) do t[#t + 1] = tostring((select(i, ...))) end
    log.info("baldriver " .. table.concat(t, " "))
end

T.plan {
    "the spec is applied on the first tick",                 -- balance_items (risen.balance.install on_applied)
    "session.start writes the live entities",                -- balance_items
    "the template carries the new numbers",                  -- balance_items verification (22 checks in all)
    "the armor modifiers read back by name",
    "a rune: mana cost property and circle requirement",
    "a new spawn uses the new numbers",
    "an inventory stack follows the template",
    "every world berry was rewritten",
    "the engine check passes with enough Strength",
    "the engine check fails for 999 Strength",
    "raising the requirement fails the check",
    "lowering the requirement passes the check",
    "wearing armor applies its modifiers",
    "a lowered requirement lets the engine equip it",
    "an entity-level edit leaves the template alone",
    "using a potion applies its modifiers",
    "a bad skill name is refused with the list",
    "an unknown template and section are refused",
    "a rejected spec writes nothing",
    "apply with instances rewrites the world",
    "saved values win over an edited template",
    "a new game world keeps the shipped values",
}

local PICK = "It_Plant_Mint"   -- re-picked by the census of the first session
local SKIP = { It_1H_Sword = true, It_2H_TwoHandedSword = true, It_Armor_Guard = true, It_Po_Health_01 = true, It_Ri_STR_Small = true, It_Plant_Berry = true }

local function mods_text(list)
    local parts = {}
    for _, m in ipairs(list or {}) do parts[#parts + 1] = ("%s%+d"):format(m.skill, m.amount) end
    return table.concat(parts, ",")
end

-- "value|modifiers x count" over every live instance, plus what the template says
-- data = { n, keys = { distinct "value|modifiers", sorted }, template = "value|modifiers" }
local function histogram(name)
    local t = risen.template.get(name)
    if not t then return "no template", { n = 0, keys = {}, template = "" } end
    local list = t:instances(100000) or {}
    local h = {}
    for _, e in ipairs(list) do
        local key = ("%s|%s"):format(tostring(e:item_value()), mods_text(e:modifiers()))
        h[key] = (h[key] or 0) + 1
    end
    local keys, parts = {}, {}
    for key in pairs(h) do keys[#keys + 1] = key end
    table.sort(keys)
    for i, key in ipairs(keys) do parts[i] = ("{%s}x%d"):format(key, h[key]) end
    local template = ("%s|%s"):format(tostring(t:get_property("gCItem_PS", "GoldValue")), mods_text(risen.item.modifiers(name)))
    return ("n=%d instances=%s template={%s}"):format(#list, table.concat(parts, ","), template), { n = #list, keys = keys, template = template }
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
    -- balance_items verifies in the first session and then raises this flag: the driver would otherwise load/save under its feet
    T.wait_until(function() return risen.gamevar.get("SPASS_BALANCE_ITEMS_DONE") == 1 end, 180, "balance_items verification")
    T.sleep(10)

    T.section("census and apply")
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
    L("baseline", PICK, (histogram(PICK)))
    local report, why = risen.balance.apply({ items = { [PICK] = { value = 77, modifies = { Strength = 7 } } } }, { instances = true })
    L("apply instances=true", report and report.ok, report and #report.applied, report and report.instances, why)
    local text, data = histogram(PICK)
    L("after apply", PICK, text)
    T.expect("apply with instances rewrites the world", data.n >= 1 and #data.keys == 1 and data.keys[1] == "77|Strength+7" and data.template == "77|Strength+7", text)

    T.section("save, edit the template back, load")
    T.sleep(60)
    L("quick_save", risen.world.quick_save())
    T.wait_until(function() return saved end, 120, "save.after")
    T.sleep(120)
    -- "the mod is gone": put the template back without touching any entity
    L("template edit back", risen.item.set_modifiers(PICK, {}), risen.template.get(PICK):set_property("gCItem_PS", "GoldValue", "1"))
    L("template now", PICK, (histogram(PICK)))
    T.sleep(30)
    L("quick_load", risen.world.quick_load())
    wait_session("load", 1, 240)
    T.sleep(400)
    text, data = histogram(PICK)
    L("after load (saved values vs edited template)", PICK, text)
    T.expect("saved values win over an edited template", data.n >= 1 and #data.keys == 1 and data.keys[1] == "77|Strength+7" and data.template == "1|", text)

    T.section("second new game")
    L("new_game", risen.world.new_game())
    wait_session("new", 2, 240)
    T.sleep(400)
    text, data = histogram(PICK)
    L("second new game (template edited earlier in this process)", PICK, text)
    T.expect("a new game world keeps the shipped values", data.n >= 1 and #data.keys == 1 and data.keys[1]:match("^77") == nil and data.template == "1|", text)
end, "balance")
