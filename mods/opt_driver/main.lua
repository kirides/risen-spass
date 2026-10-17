---@diagnostic disable: need-check-nil, param-type-mismatch, undefined-field, undefined-global
-- Live scenario `options` (scripts/smoke-test.ps1): the options page of the in-game mod manager, driven through the engine's own input path (window messages for ESC and the
-- mouse) against the real mod `quickloot`, which declares 15 options. Needs the Lua sandbox off (it reads the store file). Phases (risen.test param "phase"):
--   main     new game: Mods tab > Quick Loot > Options; toggle, drop-down, slider, pages, default, all defaults, back; a change is live (quickloot declines while it is switched off)
--            and kept in <spass>\options\quickloot.toml; a reload of the mod keeps the choices
--   restart  a fresh process that keeps spass\options: the choices of the menu are in force without touching config.toml
local T = risen.test
T.reset("options")
local phase = T.param("phase", "main")
if not T.is_active() then return end

local PAGE = 25

-- ---- helpers (the same path as mm_driver) -------------------------------------------------------------------------------------------------------------------------------------
local function pages() return table.concat(risen.hud.state().pages, ",") end
local function visible(id) for _, p in ipairs(risen.hud.state().pages) do if p == id then return true end end return false end
local function state() local out = {} risen.emit("modmanager.state", out) return out end
local function snapshot() return state().snapshot end
local function model() return state().model end
local function mods_page() return risen.gui.page(PAGE) end
local function widget(name) local p = mods_page() return p and p:find(name) end
local function tab_on(page_id) local p = risen.gui.page(page_id) return p and p:find("spass_tab_mods_" .. page_id) end
local function text(name) local w = widget(name) return w and w:text() or "" end
local function shown(name) local w = widget(name) return w ~= nil and w:visible() == true end
local function read_file(path) local f = io.open(path, "rb") if not f then return nil end local s = f:read("*a") f:close() return s end
local function shot(name) risen.debug.screenshot(name, 6) T.sleep(12) end
local function store_path() return (risen.mods.config().path:gsub("mods%.toml$", "options/quickloot.toml")) end
local function store() return read_file(store_path()) end
local function value(id) return (risen.options.get("quickloot", id)) end

local function click(w, frames)
    local ok, why = w:click()
    if not ok then T.expect("click on " .. tostring(w:name()), false, why) end
    T.sleep(frames or 4)
    return ok
end
local function click_named(name, frames)
    local w = widget(name)
    if not w then T.expect("widget " .. name .. " exists", false, "not found") return false end
    return click(w, frames)
end
-- one more GUI frame after a click: the page is redrawn in the frame after the script ran
local function settle() T.sleep(6) end

local function row_index(id)
    for i, r in ipairs(snapshot().rows) do if r.id == id then return i end end
end
local function select_mod(id)
    local m = model()
    for i, v in ipairs(m.order) do
        if v == id then m:page_to(math.floor((i - 1) / m.rows_per_page) + 1) end
    end
    state().ui:render(m:snapshot())
    T.sleep(2)
    local i = row_index(id)
    if not i then T.expect("row for " .. id, false, "no row on the page") return false end
    click_named("spass_row" .. i)
    return snapshot().selected == id
end
-- shows the page an option is on
local function show_option(id)
    local m = model()
    m:page_to(m:option_page(id) or 1)
    state().ui:render(m:snapshot())
    T.sleep(2)
end
-- selects an option by clicking its row (on the page it is on)
local function select_option(id)
    show_option(id)
    local i = row_index(id)
    if not i then T.expect("row for option " .. id, false, "no row on the page") return false end
    click_named("spass_row" .. i)
    settle()
    return snapshot().selected == id
end

-- an arrow of the option's row (dir -1 = "<", 1 = ">") or, with dir 0, the value itself (a click takes the next one)
local function step(id, dir)
    show_option(id)
    local i = row_index(id)
    if not i then T.expect("row for option " .. id, false, "not on this page") return false end
    click_named((dir < 0 and "spass_oprev" or dir > 0 and "spass_onext" or "spass_oval") .. i, 8)
    return true
end

local function open_menu_with_escape()
    T.wait_until(function() return not visible(29) end, 60, "the loading page is gone")
    risen.gui.close(26)
    T.sleep(30)
    local ok, why = risen.gui.key(0x1B)
    T.expect("ESC was delivered", ok, why)
    T.wait_until(function() return visible(13) end, 10, "the main menu opens on ESC")
    T.sleep(20)
end
local function open_mods_page()
    T.wait_until(function() return tab_on(13) and tab_on(13):visible() end, 10, "the Mods tab is visible")
    click(tab_on(13), 4)
    T.wait_until(function() return visible(PAGE) end, 10, "the Mods page opens")
    T.sleep(20)
end
local function wait_page_built()
    T.wait_until(function() return mods_page() and mods_page():find("spass_panel") and tab_on(13) end, 30, "the Mods page is built")
end

local last
risen.on("quickloot.result", function(res) last = res end)
-- what quick loot does with a target while it is switched off or on: the player itself is a target that always exists (it is declined, only the reason differs)
local function try_quickloot()
    last = nil
    risen.emit("quickloot.try", risen.entity.player())
    return last and last.reason or "<no result>"
end

local function enter_options()
    open_menu_with_escape()
    open_mods_page()
    select_mod("quickloot")
    click_named("spass_options", 6)
    settle()
end

-- ---- phase main ------------------------------------------------------------------------------------------------------------------------------------------------------------------
local function main()
    T.section("start")
    T.wait_event("session.start", 180, function(m) return m == "new" end)
    T.wait_event("world.ready", 180)
    T.sleep(60)
    wait_page_built()
    T.expect("quickloot declared its options", #risen.options.list("quickloot") == 1 and #risen.options.list("quickloot")[1].entries == 15, #risen.options.list("quickloot"))
    T.eq("a fresh run starts at the defaults", { value("enabled"), value("key"), value("pickup_sound_gap"), store() }, { true, "SecondaryAction", 600, nil })

    T.section("the Options button")
    open_menu_with_escape()
    open_mods_page()
    select_mod("modmanager")
    T.expect("a mod without options has the button off", snapshot().buttons.options.enabled == false and snapshot().buttons.options.label == "Options", snapshot().buttons.options.label)
    select_mod("quickloot")
    T.eq("the button counts the options", text("spass_options"), "Options (15)")
    click_named("spass_options", 6)
    settle()
    T.eq("the model shows the options", model().view, "options")
    T.eq("the heading", text("spass_title"), "Options: Quick Loot")
    T.expect("the Options button is gone in the options view", not shown("spass_options"))
    T.eq("the first row is a heading without controls", { text("spass_row1"), shown("spass_oval1"), shown("spass_oprev1"), shown("spass_onext1") }, { "General", false, false, false })
    T.eq("the second row: a label and its value", { text("spass_row2"), text("spass_oval2") }, { "Quick loot", "on" })
    T.eq("the third row names the key", { text("spass_row3"), text("spass_oval3") }, { "Key", "Right mouse button" })
    T.eq("the next heading", { text("spass_row5"), shown("spass_oval5") }, { "What to take", false })
    T.expect("a number is a slider: the knob and the amount on the right, an empty track", shown("spass_oknob10") and text("spass_oamount10") == "450" and text("spass_oval10") == "" and not shown("spass_oknob2"),
        text("spass_oamount10") .. "/" .. text("spass_oval10"))
    T.eq("10 settings on the first page, a page label with the summary", { snapshot().page, snapshot().pages, #snapshot().rows, text("spass_pagelabel") }, { 1, 2, 10, "1 / 2    15 options, 0 changed" })
    T.expect("the arrows and values of the last row are shown, there is no 11th row", shown("spass_oprev10") and shown("spass_onext10") and shown("spass_oval10") and not shown("spass_row11"))
    T.eq("the buttons of the page", { text("spass_up"), text("spass_down"), text("spass_reset") }, { "Default value", "All defaults", "Back to the mods" })
    T.expect("the list's own buttons are gone", not shown("spass_toggle") and not shown("spass_reload") and not shown("spass_detail"))
    T.expect("the description is shown under the rows", text("spass_ohint"):find("Take what you look at", 1, true) ~= nil and text("spass_ohint"):find("Default: on", 1, true) ~= nil, text("spass_ohint"))
    shot("opt_page_toggle")

    T.section("a toggle is live")
    T.expect("quick loot reacts while it is on", try_quickloot() ~= "switched off", try_quickloot())
    step("enabled", 1)
    T.wait_until(function() return value("enabled") == false end, 10, "enabled is off")
    T.eq("the value follows", text("spass_oval2"), "off")
    T.eq("quick loot declines at once", try_quickloot(), "switched off")
    T.expect("the choice is stored", (store() or ""):find("enabled = false", 1, true) ~= nil, store())
    T.expect("the row is marked as changed", snapshot().rows[2].tone == "warn")
    step("enabled", -1)   -- a toggle flips with either arrow
    T.wait_until(function() return value("enabled") == true end, 10, "enabled is on")
    T.expect("quick loot reacts again", try_quickloot() ~= "switched off", try_quickloot())
    T.eq("a value that is back at its default leaves no store", store(), nil)

    T.section("a drop-down")
    T.expect("the key option is selected", select_option("key"))
    step("key", 1)
    T.wait_until(function() return value("key") == "PrimaryAction" end, 10, "next key")
    T.eq("the value shows the label of the choice", text("spass_oval3"), "Left mouse button")
    step("key", -1)
    T.wait_until(function() return value("key") == "SecondaryAction" end, 10, "previous key")
    step("key", -1)   -- wraps around to the last choice
    T.wait_until(function() return value("key") ~= "SecondaryAction" end, 10, "wrapped around")
    T.eq("previous from the first wraps to the last", value("key"), "L")
    step("key", 1)
    T.eq("next from the last wraps to the first", value("key"), "SecondaryAction")
    step("key", 0)   -- a click on the value takes the next one too
    T.eq("a click on the value is the next value", value("key"), "PrimaryAction")
    for _ = 1, 2 do step("key", 1) end
    T.wait_until(function() return value("key") == "MButton" end, 10, "the middle mouse button")
    shot("opt_page_dropdown")

    T.section("a slider")
    T.expect("pickup_sound_gap is selected", select_option("pickup_sound_gap"))
    step("pickup_sound_gap", 1)
    T.wait_until(function() return value("pickup_sound_gap") == 700 end, 10, "increased by a step")
    T.eq("the amount shows the number, the knob moved", { text("spass_row4"), text("spass_oamount4") }, { "Sound spacing (ms)", "700" })
    local knob_x = widget("spass_oknob4"):position()
    step("pickup_sound_gap", 1)
    T.expect("the knob moves with the value", (widget("spass_oknob4"):position()) > knob_x, tostring(knob_x) .. " -> " .. tostring((widget("spass_oknob4"):position())))
    step("pickup_sound_gap", -1)
    step("pickup_sound_gap", -1)
    T.wait_until(function() return value("pickup_sound_gap") == 600 end, 10, "decreased by a step")
    step("repeat_ms", 1)
    T.wait_until(function() return value("repeat_ms") == 100 end, 10, "repeat_ms increased")
    T.expect("the stored choices differ from the defaults only", (function() local s = store() or "" return s:find("key = ", 1, true) and s:find("repeat_ms = 100", 1, true) and not s:find("pickup_sound_gap", 1, true) and not s:find("enabled", 1, true) end)(), store())

    T.section("the second page")
    click_named("spass_next", 6)
    settle()
    T.eq("page 2 shows the rest", { snapshot().page, #snapshot().rows, text("spass_row1"), text("spass_row8") }, { 2, 10, "Feedback", "Witnesses" })
    T.expect("a slider of page 2", text("spass_row7") == "Repeat interval (ms)" and text("spass_oamount7") == "100", text("spass_row7") .. "/" .. text("spass_oamount7"))
    T.expect("a click selects an option of page 2", select_option("max_distance"))
    step("max_distance", -1)
    T.wait_until(function() return value("max_distance") == 400 end, 10, "reach decreased")
    click_named("spass_up", 8)   -- "Default value"
    T.wait_until(function() return value("max_distance") == 450 end, 10, "reach back at its default")
    shot("opt_page_two")

    T.section("all defaults, then the choices that stay")
    click_named("spass_down", 8)
    T.wait_until(function() return value("key") == "SecondaryAction" and value("repeat_ms") == 80 end, 10, "everything is at its default")
    T.eq("the store is gone", store(), nil)
    T.expect("the message says how many", text("spass_note"):find("Reset 2 option", 1, true) ~= nil, text("spass_note"))
    for _ = 1, 3 do step("key", 1) end
    step("repeat_ms", 1)
    T.wait_until(function() return value("key") == "MButton" and value("repeat_ms") == 100 end, 10, "the choices that stay")

    T.section("back to the list, a reload keeps the choices")
    click_named("spass_reset", 6)
    settle()
    T.eq("the list is shown again", { model().view, text("spass_title"), shown("spass_options") }, { "mods", "Mods", true })
    select_mod("quickloot")
    local before = risen.mods.get("quickloot").load_count
    click_named("spass_reload", 10)
    T.wait_until(function() return risen.mods.get("quickloot").load_count == before + 1 end, 15, "quickloot reloaded")
    T.eq("the reloaded mod starts with the choices", { value("key"), value("repeat_ms"), value("enabled") }, { "MButton", 100, true })
    T.expect("the stored file is still there", (store() or ""):find("MButton", 1, true) ~= nil, store())
end

-- ---- phase restart ---------------------------------------------------------------------------------------------------------------------------------------------------------------
local function restart()
    T.section("the choices of the menu are in force after a restart")
    T.wait_event("session.start", 180, function(m) return m == "new" end)
    T.wait_event("world.ready", 180)
    T.sleep(60)
    wait_page_built()
    T.eq("the stored choices were read", { value("key"), value("repeat_ms"), value("enabled"), value("pickup_sound_gap") }, { "MButton", 100, true, 600 })
    T.expect("risen.config of the mod saw the same file", (store() or ""):find("MButton", 1, true) ~= nil, store())
    enter_options()
    T.eq("the page shows them", { text("spass_oval3"), snapshot().rows[3].tone }, { "Middle mouse button", "warn" })
    show_option("repeat_ms")
    T.eq("the other choice is on page 2", { text("spass_row7"), text("spass_oamount7") }, { "Repeat interval (ms)", "100" })
    show_option("key")
    T.expect("unchanged options are not marked", snapshot().rows[1].tone == "ok")
    T.expect("the summary counts them", text("spass_pagelabel"):find("2 changed", 1, true) ~= nil, text("spass_pagelabel"))
    select_option("key")
    click_named("spass_up", 8)   -- back to this option's default
    T.wait_until(function() return value("key") == "SecondaryAction" end, 10, "the key is back at its default")
    T.expect("only the other choice is stored now", (function() local s = store() or "" return s:find("repeat_ms", 1, true) and not s:find("key", 1, true) end)(), store())
end

if phase == "main" then
    T.run(main, "options")
else
    T.run(restart, "options")
end
