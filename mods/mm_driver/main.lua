---@diagnostic disable: need-check-nil, param-type-mismatch, undefined-field, undefined-global
-- Live scenario `modmanager` (M16, scripts/smoke-test.ps1): the in-game mod manager, driven through the engine's own input path (window messages for ESC and the
-- mouse). Needs the Lua sandbox off (it reads mods.toml). Phases (risen.test param "phase"):
--   main     new game: the Mods tab, ESC, the real click on the tab, the page lists the mods with their states, select / reload / reorder (refused with the reason and
--            accepted) / switch off with a cascade / the data prompt / switch on, closing the menu (the game ticks again), quick save + load, the page survives
--   restart  a fresh process that keeps mods.toml: what the page did is still in force; switching a mod on asks for its requirement
--   menu     the main menu, no game: the tab and the page work without a world
local T = risen.test
T.reset("modmanager")   -- the fixtures that fail or are skipped on purpose logged their errors while loading: the scenario starts counting after them
local phase = T.param("phase", "main")
if not T.is_active() then return end

local PAGE = 25
local ticks, uiframes = 0, 0
risen.on("tick", function() ticks = ticks + 1 end)
risen.on("ui.frame", function() uiframes = uiframes + 1 end)

local RED = "170; 20; 20; 255"

-- ---- helpers -----------------------------------------------------------------------------------------------------------------------------------------------------------------------
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
local function mod(id) return risen.mods.get(id) end
local function read_file(path) local f = io.open(path, "rb") if not f then return nil end local s = f:read("*a") f:close() return s end
local function write_file(path, s) local f = assert(io.open(path, "wb")) f:write(s) f:close() end
local function shot(name) risen.debug.screenshot(name, 6) T.sleep(12) end

-- a click with the real mouse path (window messages), then a few frames for the engine to run the script
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

local function row_index(id)
    for i, r in ipairs(snapshot().rows) do if r.id == id then return i end end
end
-- selects a mod by clicking its row (the page the row is on is shown first, like a player would)
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

local function open_menu_with_escape()
    T.wait_until(function() return not visible(29) end, 60, "the loading page is gone")   -- the page 29 "Loading" ignores ESC
    risen.gui.close(26)                       -- the "Press START" page the test start leaves open
    T.sleep(30)
    T.expect("the menu is closed before ESC", not visible(13), pages())
    local ok, why = risen.gui.key(0x1B)       -- WM_KEYDOWN / WM_KEYUP of the Escape key through the application's message path
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

local function wait_saved()
    T.wait_until(function() return risen.world.is_save_allowed() end, 30, "saving allowed")
    local saved = false
    local h = risen.on("save.after", function(_, ok) saved = ok end)
    risen.world.quick_save()
    T.wait_until(function() return saved end, 60, "savegame written")
    risen.off(h)
    T.sleep(40)
end

-- ---- phase main ------------------------------------------------------------------------------------------------------------------------------------------------------------------
local function main()
    T.section("start")
    T.wait_event("session.start", 180, function(m) return m == "new" end)
    T.wait_event("world.ready", 180)
    T.sleep(60)
    T.wait_until(function() return mods_page() and mods_page():find("spass_panel") and tab_on(13) end, 30, "the Mods page is built")

    T.section("the tab and the page exist")
    for _, id in ipairs({ 13, 14, 18, 24 }) do
        local t = tab_on(id)
        T.expect("Mods tab on page " .. id, t ~= nil and t:text() == "Mods" and t:get("OnClick") == "Spass_ModMgr_Tab", t and t:text())
    end
    T.expect("the page has a controller", mods_page():get("PageControl") == "Spass_Leaf_25", mods_page():get("PageControl"))
    local m = model()
    T.eq("fixture states", { m.entries.mm_base.state, m.entries.mm_ext.state, m.entries.mm_data.state, m.entries.mm_broken.state, m.entries.mm_skipped.state, m.entries.modmanager.state },
        { "loaded", "loaded", "loaded", "failed", "skipped", "loaded" })
    local snap = snapshot()
    T.expect("the summary counts every mod", snap.count == 8 and snap.summary:find("2 failed", 1, true) ~= nil, snap.summary)

    T.section("ESC opens the menu, the world pauses")
    open_menu_with_escape()
    T.eq("main menu and its background are shown", pages(), "13,27")
    local t1, f1 = ticks, uiframes
    T.sleep(40)
    T.expect("the world is paused while the menu is open (no tick), the GUI frame event goes on", ticks == t1 and uiframes > f1, ("ticks %d -> %d, ui.frame %d -> %d"):format(t1, ticks, f1, uiframes))
    shot("mm_menu_main")

    T.section("the Mods tab opens the page (a real mouse click)")
    open_mods_page()
    T.eq("only the Mods page and the background are shown", pages(), "25,27")
    T.expect("the title says Mods", text("spass_title") == "Mods", text("spass_title"))
    shot("mm_page_open")
    local snap2 = snapshot()
    local drawn_ok = true
    for i, r in ipairs(snap2.rows) do
        if text("spass_row" .. i) ~= r.text then drawn_ok = false end
    end
    T.expect("the row widgets show the model's rows", drawn_ok and #snap2.rows == 8, #snap2.rows)

    T.section("selecting mods shows their details")
    local ok = select_mod("mm_broken")
    T.expect("a click on a row selects the mod", ok, snapshot().selected)
    T.expect("a failed mod shows the reason", text("spass_detail"):find("deliberate failure", 1, true) ~= nil and text("spass_detail"):find("failed", 1, true) ~= nil, text("spass_detail"))
    T.expect("a failed mod's tone is red in the model", snapshot().rows[row_index("mm_broken")].tone == "fail", "tone")
    select_mod("mm_skipped")
    T.expect("a skipped mod shows the missing requirement", text("spass_detail"):find("mm_ghost", 1, true) ~= nil, text("spass_detail"))
    select_mod("mm_ext")
    T.expect("requirements are listed", text("spass_detail"):find("Requires: mm_base", 1, true) ~= nil, text("spass_detail"))
    select_mod("mm_base")
    T.expect("what needs a mod is listed", text("spass_detail"):find("Needed by: mm_ext", 1, true) ~= nil, text("spass_detail"))
    -- unselected failed rows are red
    local red_ok = false
    for i, r in ipairs(snapshot().rows) do
        if r.id == "mm_broken" then red_ok = widget("spass_row" .. i):get("FontColorNormal") == RED end
    end
    T.expect("the failed mod's row is drawn in red when it is not selected", red_ok)
    shot("mm_page_details")

    T.section("reload from the page")
    select_mod("mm_data")
    local count_before = mod("mm_data").load_count
    click_named("spass_reload", 10)
    T.wait_until(function() return mod("mm_data").load_count == count_before + 1 end, 15, "mm_data reloaded")
    T.expect("the page reports the reload", text("spass_note"):find("Reloaded", 1, true) ~= nil, text("spass_note"))
    T.expect("the mod is still loaded", mod("mm_data").state == "loaded")
    T.allow_errors(true)   -- the failing mod logs its error again
    select_mod("mm_broken")
    click_named("spass_reload", 10)
    T.allow_errors(false)
    T.expect("a reload that fails is reported in red, the mod stays failed", mod("mm_broken").state == "failed" and snapshot().message and snapshot().message.tone == "error", text("spass_note"))
    T.expect("the game keeps running (the page is still open)", visible(PAGE), pages())

    T.section("load order: dependencies are enforced")
    -- make mm_ext follow mm_base directly, then try to move it up
    local ids = { "autostart", "mm_base", "mm_ext", "mm_broken", "mm_data", "mm_driver", "modmanager" }
    T.expect("a custom order can be saved", risen.mods.set_order(ids))
    model():refresh()
    state().ui:render(model():snapshot())
    select_mod("mm_ext")
    T.expect("the Move up button is marked as blocked", text("spass_up"):find("blocked", 1, true) ~= nil, text("spass_up"))
    local before = risen.mods.order().custom
    click_named("spass_up", 6)
    local snap3 = snapshot()
    T.expect("moving a mod before its requirement is refused with the reason", snap3.message and snap3.message.tone == "error" and snap3.message.text:find("mm_base", 1, true) ~= nil
        and snap3.message.text:find("requires", 1, true) ~= nil, snap3.message and snap3.message.text)
    T.expect("the reason is on the page", text("spass_note"):find("requires", 1, true) ~= nil, text("spass_note"))
    T.eq("the order is unchanged after the refusal", risen.mods.order().custom, before)
    shot("mm_page_refused")
    select_mod("mm_base")
    click_named("spass_down", 6)
    T.expect("moving a requirement below its dependant is refused too", snapshot().message.tone == "error" and snapshot().message.text:find("mm_ext", 1, true) ~= nil, snapshot().message.text)
    select_mod("mm_driver")
    local before2 = risen.mods.order().custom
    click_named("spass_up", 6)
    local after2 = risen.mods.order().custom
    T.expect("an unrelated mod moves up and the order is saved", after2[5] == "mm_driver" and after2[6] == "mm_data" and #after2 >= #before2, table.concat(after2, ","))
    T.expect("the page says it applies at the next start", snapshot().message.tone == "warn" and text("spass_note"):find("next start", 1, true) ~= nil, text("spass_note"))
    click_named("spass_reset", 6)
    T.eq("the default order button clears the custom order", risen.mods.order().custom, {})
    -- keep one moved mod for the restart phase
    select_mod("mm_driver")
    click_named("spass_up", 6)
    T.expect("a moved mod is remembered", #risen.mods.order().custom > 0 and risen.mods.order().custom[1] ~= nil)
    local planned = risen.mods.order().planned
    write_file((risen.mods.config().path:gsub("mods%.toml$", "mm_expect.txt")), table.concat(planned, ","))

    T.section("switching mods off: a cascade, the answer no, the answer yes")
    select_mod("mm_base")
    click_named("spass_toggle", 6)
    T.expect("switching off a required mod asks first", snapshot().prompt ~= nil and snapshot().prompt.kind == "cascade_off", text("spass_prompt"))
    T.expect("the question names the dependant", text("spass_prompt"):find("mm_ext", 1, true) ~= nil, text("spass_prompt"))
    T.expect("the question shows its buttons and hides the others", shown("spass_yes") and shown("spass_no") and not shown("spass_alt") and not shown("spass_toggle"),
        ("yes %s no %s alt %s toggle %s"):format(tostring(shown("spass_yes")), tostring(shown("spass_no")), tostring(shown("spass_alt")), tostring(shown("spass_toggle"))))
    shot("mm_page_prompt")
    click_named("spass_no", 6)
    T.expect("No changes nothing", mod("mm_base").state == "loaded" and mod("mm_ext").state == "loaded" and snapshot().prompt == nil)
    click_named("spass_toggle", 6)
    click_named("spass_yes", 10)
    T.wait_until(function() return mod("mm_base").state == "disabled" end, 15, "mm_base switched off")
    T.expect("the cascade switched the dependant off too", mod("mm_ext").state == "disabled", mod("mm_ext").state)
    local toml = read_file(risen.mods.config().path) or ""
    T.expect("mods.toml keeps the choice", toml:find("mm_base%s*=%s*false") ~= nil and toml:find("mm_ext%s*=%s*false") ~= nil, toml)
    T.expect("the rows show them as off", text("spass_row" .. row_index("mm_base")):find("off", 1, true) ~= nil, text("spass_row" .. row_index("mm_base")))
    local who = {}
    risen.emit("mm.who", who)
    T.expect("their handlers are gone", not table.concat(who, ","):find("mm_base", 1, true) and not table.concat(who, ","):find("mm_ext", 1, true), table.concat(who, ","))

    T.section("a mod that left data in the savegame")
    T.wait_until(function() return risen.entity.get("MM_Guy") ~= nil end, 30, "the persistent NPC of mm_data exists")
    model():refresh()
    select_mod("mm_data")
    T.expect("the data is described", text("spass_detail"):find("1 persistent NPC", 1, true) ~= nil, text("spass_detail"))
    click_named("spass_toggle", 6)
    T.expect("switching it off warns about the data and offers to remove it", snapshot().prompt and snapshot().prompt.kind == "data_off" and text("spass_prompt"):find("1 persistent NPC", 1, true) ~= nil
        and shown("spass_alt"), text("spass_prompt"))
    shot("mm_page_data")
    click_named("spass_yes", 10)
    T.wait_until(function() return mod("mm_data").state == "disabled" end, 15, "mm_data switched off")
    T.wait_until(function() return risen.entity.get("MM_Guy") == nil end, 15, "the NPC of mm_data is removed")
    T.expect("the page says what was removed", text("spass_note"):find("removed 1", 1, true) ~= nil, text("spass_note"))
    click_named("spass_toggle", 6)
    T.wait_until(function() return mod("mm_data").state == "loaded" end, 15, "mm_data switched on again")
    T.expect("switching a mod on needs no question when nothing is missing", snapshot().prompt == nil and mod("mm_data").enabled)

    T.section("switching on asks for the requirement")
    select_mod("mm_ext")
    click_named("spass_toggle", 6)
    T.expect("switching on a mod whose requirement is off asks", snapshot().prompt and snapshot().prompt.kind == "cascade_on" and text("spass_prompt"):find("mm_base", 1, true) ~= nil, text("spass_prompt"))
    click_named("spass_yes", 10)
    T.wait_until(function() return mod("mm_ext").state == "loaded" and mod("mm_base").state == "loaded" end, 15, "both switched on")
    T.expect("both are loaded again and mods.toml says so", (read_file(risen.mods.config().path) or ""):find("mm_base%s*=%s*false") == nil)
    -- leave mm_base and mm_ext switched off for the restart phase
    select_mod("mm_base")
    click_named("spass_toggle", 6)
    click_named("spass_yes", 10)
    T.wait_until(function() return mod("mm_base").state == "disabled" end, 15, "mm_base off for the restart")

    T.section("robustness")
    local mm = model()
    T.expect("an unknown mod cannot be selected", mm:select("no_such_mod") == false)
    T.expect("an unknown action is ignored", mm:press("bogus") == false and mm:answer("yes") == false)
    local real_list = mm.host.mods.list
    mm.host.mods = setmetatable({ list = function() error("the host failed") end }, { __index = risen.mods })
    T.expect("a failing host does not break the model", pcall(function() mm:refresh() return mm:snapshot() end))
    mm.host.mods = risen.mods
    mm:refresh()
    T.expect("the page is still there after all this", mods_page():valid() and visible(PAGE), pages())

    T.section("paging (a long list)")
    mm.rows_per_page = 4
    mm:refresh()
    mm:select(mm.order[1])
    mm:page_to(1)
    state().ui:render(mm:snapshot())
    T.sleep(3)
    T.expect("with four rows per page the list has two pages and the paging buttons show", mm:pages() == 2 and shown("spass_next") and shown("spass_prev") and text("spass_pagelabel") == "1 / 2",
        mm:pages() .. " " .. text("spass_pagelabel"))
    T.expect("the rows beyond the page are hidden", shown("spass_row4") and not shown("spass_row5"))
    click_named("spass_next", 6)
    T.expect("Next shows the second page", snapshot().page == 2 and text("spass_pagelabel") == "2 / 2" and text("spass_row1") == snapshot().rows[1].text, text("spass_pagelabel"))
    click_named("spass_prev", 6)
    T.expect("Prev goes back", snapshot().page == 1 and text("spass_pagelabel") == "1 / 2")
    mm.rows_per_page = 12
    mm:refresh()
    state().ui:render(mm:snapshot())
    T.sleep(3)
    T.expect("the paging buttons hide again with one page", not shown("spass_next") and mm:pages() == 1)

    T.section("closing: ESC goes back, Continue returns to the game")
    local ok2, why2 = risen.gui.key(0x1B)
    T.expect("ESC was delivered", ok2, why2)
    T.wait_until(function() return visible(13) and not visible(PAGE) end, 10, "ESC goes back to the main menu")
    T.eq("the main menu is shown again", pages(), "13,27")
    local cont
    for _, c in ipairs(risen.gui.page(13):children()) do if c:get("OnClick") == "OnClick_Continue" then cont = c end end
    click(cont, 4)
    T.wait_until(function() return pages() == "1" end, 10, "the game view returns")
    local t2 = ticks
    T.wait_until(function() return ticks > t2 + 30 end, 20, "the world runs again")
    T.expect("the game keeps ticking after the page was closed", ticks > t2 + 30, ("ticks %d -> %d"):format(t2, ticks))

    T.section("the page follows a hot reload of the manager")
    local panel_children = #mods_page():children()
    local tabs_before = 0
    for _, c in ipairs(risen.gui.page(13):children()) do if c:name() == "spass_tab_mods_13" then tabs_before = tabs_before + 1 end end
    local rep = risen.mods.reload("modmanager", { now = true })
    T.expect("the manager reloads", rep ~= nil and rep.ok == true and mod("modmanager").load_count == 2, rep and rep.summary)
    T.sleep(30)
    T.wait_until(function() return state().ui ~= nil and state().ui:valid() end, 15, "the page is built again")
    local tabs_after = 0
    for _, c in ipairs(risen.gui.page(13):children()) do if c:name() == "spass_tab_mods_13" then tabs_after = tabs_after + 1 end end
    T.eq("no windows were duplicated by the reload", { #mods_page():children(), tabs_after }, { panel_children, tabs_before })
    T.expect("the tab is visible again", tab_on(13):visible() == true)
    local unl = risen.mods.unload("modmanager")
    T.sleep(5)
    T.expect("an unloaded manager takes its tab away", unl ~= nil and tab_on(13):visible() == false)
    local quit
    for _, c in ipairs(risen.gui.page(13):children()) do if c:get("OnClick") == "OnClick_ExitGame" then quit = c end end
    T.eq("Quit is back at its stock position", { quit:position() }, { 921, 653 })
    risen.mods.load("modmanager")
    T.sleep(30)
    T.wait_until(function() return state().ui ~= nil and state().ui:valid() end, 15, "the page is built after load")
    T.expect("loaded again: the tab is back and Quit moved over", tab_on(13):visible() == true and select(1, quit:position()) == 1001, tostring(select(1, quit:position())))

    T.section("quick save and load")
    wait_saved()
    local seen = 0
    local h = risen.on("world.ready", function() seen = seen + 1 end)
    risen.world.quick_load()
    T.wait_until(function() return seen > 0 end, 120, "world.ready after the quick load")
    risen.off(h)
    T.sleep(60)
    T.expect("the tab and the page survive a savegame load", tab_on(13) ~= nil and mods_page():valid() and tab_on(13):valid(), "")
    open_menu_with_escape()
    open_mods_page()
    T.expect("the page opens after the load and shows the mods", visible(PAGE) and #snapshot().rows == 8, pages())
    T.expect("the choice of the page is still in force after the load", mod("mm_base").state == "disabled")
    select_mod("mm_data")
    T.expect("a click on a row works in the loaded world", snapshot().selected == "mm_data")
end

-- ---- phase restart ---------------------------------------------------------------------------------------------------------------------------------------------------------------
local function restart()
    T.section("what the page chose is in force after a restart")
    T.wait_event("session.start", 180, function(m) return m == "new" end)
    T.wait_event("world.ready", 180)
    T.sleep(60)
    T.wait_until(function() return mods_page() and mods_page():find("spass_panel") and tab_on(13) end, 30, "the Mods page is built")
    T.eq("mm_base and mm_ext are still switched off", { mod("mm_base").state, mod("mm_ext").state }, { "disabled", "disabled" })
    T.expect("mods.toml has the choice", (read_file(risen.mods.config().path) or ""):find("mm_base%s*=%s*false") ~= nil)
    local expected = read_file((risen.mods.config().path:gsub("mods%.toml$", "mm_expect.txt"))) or ""
    local want = {}
    for id in expected:gmatch("[^,]+") do
        local e = risen.mods.get(id)
        if e and e.state == "loaded" then want[#want + 1] = id end
    end
    T.expect("the load order of this run is the saved custom order", table.concat(risen.mods.order().current, ",") == table.concat(want, ","),
        table.concat(risen.mods.order().current, ",") .. " / " .. table.concat(want, ","))
    T.expect("the custom order was read", #risen.mods.order().custom > 0, table.concat(risen.mods.order().custom, ","))

    open_menu_with_escape()
    open_mods_page()
    local snap = snapshot()
    T.expect("the page shows them as off", snap.summary:find("off", 1, true) ~= nil, snap.summary)
    select_mod("mm_ext")
    click_named("spass_toggle", 6)
    T.expect("switching on mm_ext asks for mm_base", snapshot().prompt and snapshot().prompt.kind == "cascade_on", text("spass_prompt"))
    click_named("spass_yes", 10)
    T.wait_until(function() return mod("mm_ext").state == "loaded" and mod("mm_base").state == "loaded" end, 15, "both loaded")
    local who = {}
    risen.emit("mm.who", who)
    local s = table.concat(who, ",")
    T.expect("both run again", s:find("mm_base", 1, true) ~= nil and s:find("mm_ext", 1, true) ~= nil, s)
end

-- ---- phase menu ------------------------------------------------------------------------------------------------------------------------------------------------------------------
local function menu()
    T.section("the main menu, no game")
    T.wait_until(function() return mods_page() and mods_page():find("spass_panel") and tab_on(13) end, 90, "the Mods page is built at the main menu")
    T.wait_until(function() return visible(13) or visible(26) or visible(1) end, 30, "a menu page is visible")
    if not visible(13) then risen.gui.close(26) T.sleep(10) risen.gui.open(13) T.sleep(20) end
    T.wait_until(function() return visible(13) end, 20, "the main menu")
    open_mods_page()
    local snap = snapshot()
    T.expect("the page lists the mods without a world", #snap.rows == 7 and visible(PAGE), snap.summary)
    select_mod("mm_data")
    T.expect("no NPCs in the savegame data without a world", text("spass_detail"):find("Savegame data: 1 persistent NPC", 1, true) == nil, text("spass_detail"))
    click_named("spass_toggle", 6)
    T.wait_until(function() return mod("mm_data").state == "disabled" end, 15, "switched off without a world")
    T.expect("switching off needs no question without data", snapshot().prompt == nil)
    click_named("spass_toggle", 6)
    T.wait_until(function() return mod("mm_data").state == "loaded" end, 15, "switched on without a world")
    select_mod("mm_ext")
    click_named("spass_reload", 10)
    T.wait_until(function() return mod("mm_ext").load_count >= 2 end, 15, "reloaded without a world")
    T.expect("a reload works without a world", mod("mm_ext").state == "loaded")
    risen.gui.key(0x1B)
    T.wait_until(function() return visible(13) and not visible(PAGE) end, 10, "ESC goes back")
    T.expect("ESC at the page goes back to the main menu", visible(13) and not visible(PAGE), pages())
end

if phase == "main" then
    T.run(main, "modmanager")
elseif phase == "restart" then
    T.run(restart, "modmanager")
else
    T.run(menu, "modmanager")
end
