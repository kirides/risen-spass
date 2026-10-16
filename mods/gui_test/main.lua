---@diagnostic disable: need-check-nil, param-type-mismatch, undefined-field, undefined-global
-- Live scenario `gui` (M16, scripts/smoke-test.ps1): the building blocks of the in-game mod manager against the engine's own menu, with no mod manager involved:
-- risen.gui pages and windows (tree, properties, clone, a Lua script as a button's OnClick), window messages (ESC, a click), the `ui.frame` and `ui.page` events while the world is
-- paused, a leaf page of our own, risen.debug.screenshot, and the refusals of bad arguments. Run with -Unsafe (it reads the screenshot file).
local T = risen.test
if not T.is_active() then return end

local ticks, uiframes = 0, 0
risen.on("tick", function() ticks = ticks + 1 end)
risen.on("ui.frame", function() uiframes = uiframes + 1 end)
local page_events = {}
risen.on("ui.page", function(ev) page_events[#page_events + 1] = ev.page .. ":" .. ev.kind end)

local function pages() return table.concat(risen.hud.state().pages, ",") end
local function visible(id) for _, p in ipairs(risen.hud.state().pages) do if p == id then return true end end return false end
local function find_by_click(page, script)
    for _, c in ipairs(page:children()) do if c:get("OnClick") == script then return c end end
end

T.run(function()
    T.section("start")
    T.wait_event("session.start", 180, function(m) return m == "new" end)
    T.wait_event("world.ready", 180)
    T.sleep(60)
    T.wait_until(function() return not visible(29) end, 60, "the loading page is gone")

    T.section("the page list")
    local list, why = risen.gui.pages()
    T.expect("risen.gui.pages lists the pages", list ~= nil and #list >= 30, why)
    local by_name = {}
    for _, p in ipairs(list) do by_name[p.name] = p end
    T.eq("page ids and names", { by_name.Game.id, by_name.Menu_Main.id, by_name.Menu_Cheats.id, by_name.Loading.id }, { 1, 13, 25, 29 })
    T.expect("the game view is visible, the menu is not", by_name.Game.visible == true and by_name.Menu_Main.visible == false, pages())
    T.expect("a page has a window", by_name.Menu_Main.window ~= nil and by_name.Menu_Main.window:valid())
    T.expect("risen.gui.page finds a page by id and by name", risen.gui.page(13) == risen.gui.page("Menu_Main") and risen.gui.page("gEHudPage_Menu_Main") == risen.gui.page(13))
    T.expect("an unknown page is nil", risen.gui.page(99) == nil and risen.gui.page("Nope") == nil and risen.gui.page(-1) == nil)

    T.section("windows")
    local main = risen.gui.page("Menu_Main")
    local d = main:describe()
    T.eq("the page window", { d.class, d.name, d.w, d.h }, { "gCHudPage2", "DLG_MENU_MAIN", 1365, 768 })
    local kids = main:children()
    T.expect("the main menu has its five tabs", #kids == 5 and kids[1]:class() == "eCScriptRadioButton", #kids)
    local credits = find_by_click(main, "OnClick_Menu_Credits")
    T.expect("a tab is found by its OnClick script, whatever its (translated) text", credits ~= nil and credits:text() == "Credits", credits and credits:text())
    T.expect("a child knows its parent", credits:parent() == main and credits:parent():name() == "DLG_MENU_MAIN")
    T.expect("find by name and by ID", main:find("nothing") == nil and risen.gui.page("Menu_Settings"):find(1006) ~= nil and risen.gui.page("Menu_Settings"):find(1006):class() == "gCSpinButton")
    local props = credits:properties()
    local names = {}
    for _, p in ipairs(props) do names[p.name] = p.type end
    T.expect("the reflection lists the properties", names.OnClick ~= nil and names.Text ~= nil and names.Position ~= nil and names.FontColorNormal ~= nil, #props)
    T.eq("a property value", credits:get("Position"), "763; 653")
    T.expect("an unknown property is nil", credits:get("NoSuchProperty") == nil)

    T.section("clone and change")
    local copy, cwhy = credits:clone(main)
    T.expect("a window can be cloned", copy ~= nil and copy:valid() and copy:text() == "Credits", cwhy)
    T.expect("the clone is a new window below the same page", copy ~= credits and copy:parent() == main and #main:children() == 6)
    T.expect("text, position, size and visibility can be set", copy:set_text("Clone") and copy:set_position(100, 200) and copy:set_size(120, 30) and copy:set_visible(false))
    local dc = copy:describe()
    T.eq("the changes are in the engine", { dc.text, dc.x, dc.y, dc.w, dc.h, dc.visible }, { "Clone", 100, 200, 120, 30, false })
    T.expect("a property can be set by name", copy:set("FontColorNormal", "10; 20; 30; 255") and copy:get("FontColorNormal") == "10; 20; 30; 255")
    local ok_set, why_set = copy:set("NoSuchProperty", "1")
    T.expect("a property that does not exist is refused", ok_set == nil and why_set ~= nil, why_set)
    local stock = credits:describe()
    T.eq("the original is untouched", { stock.text, stock.x, stock.y }, { "Credits", 763, 653 })

    T.section("ESC opens the menu, the world pauses, ui.frame goes on")
    risen.gui.close(26)
    T.sleep(20)
    local before = ticks
    local ok, kwhy = risen.gui.key(0x1B)
    T.expect("ESC was delivered as a window message", ok, kwhy)
    T.wait_until(function() return visible(13) end, 10, "the main menu opens")
    T.sleep(10)
    T.eq("the main menu and its background", pages(), "13,27")
    local t1, f1 = ticks, uiframes
    T.sleep(30)
    T.expect("no session tick while the menu is open, but ui.frame (and the test's own waits) run", ticks == t1 and uiframes >= f1 + 30, ("ticks %d -> %d, ui.frame %d -> %d"):format(t1, ticks, f1, uiframes))

    T.section("a click reaches a stock tab and a tab of ours")
    copy:set_visible(true)
    copy:set_position(credits:position())
    credits:set_visible(false)
    local clicked = 0
    risen.script.define("Spass_GuiTest_Click", function() clicked = clicked + 1 return 0 end)
    T.expect("a Lua script becomes the OnClick of a window", copy:set("OnClick", "Spass_GuiTest_Click") and copy:get("OnClick") == "Spass_GuiTest_Click")
    local cx, cy = copy:center()
    local w, h = risen.gui.client_size()
    T.expect("the centre of a window is in client pixels", cx and cy and w and cx > 0 and cx < w and cy > 0 and cy < h, ("%s,%s in %sx%s"):format(tostring(cx), tostring(cy), tostring(w), tostring(h)))
    local okc, whyc = copy:click()
    T.expect("click delivers mouse messages", okc, whyc)
    T.wait_until(function() return clicked > 0 end, 5, "the Lua script ran")
    T.eq("the click ran the Lua script once", clicked, 1)
    credits:set_visible(true)
    copy:set_visible(false)
    T.expect("a hidden window cannot be clicked", select(1, copy:click()) == nil)
    local okc2 = credits:click()
    T.wait_until(function() return visible(24) end, 5, "the credits page opens")
    T.expect("a click on a stock tab runs the game's own script", okc2 and visible(24), pages())
    risen.gui.open(13)
    T.sleep(10)

    T.section("a leaf page of our own")
    T.expect("leaf_page gives the unused page 25 a controller", risen.gui.leaf_page(25, 13) == true and risen.gui.page(25):get("PageControl") == "Spass_Leaf_25", risen.gui.page(25):get("PageControl"))
    T.expect("it can be set up again", risen.gui.leaf_page(25, 13) == true)
    T.expect("the main menu hides when it opens", risen.gui.hide_page_on(13, 25) == true)
    page_events = {}
    T.expect("the page opens", risen.gui.open(25) == true)
    T.wait_until(function() return visible(25) end, 5, "page 25 visible")
    T.sleep(10)
    T.eq("only the page and the background are shown", pages(), "25,27")
    T.expect("the page reported itself", page_events[1] == "25:opened", table.concat(page_events, ","))
    local okk = risen.gui.key(0x1B)
    T.wait_until(function() return visible(13) and not visible(25) end, 5, "ESC goes back to the parent")
    T.expect("ESC went back to the main menu", okk and pages() == "13,27", pages() .. " " .. table.concat(page_events, ","))
    T.expect("the page reported back and closed", table.concat(page_events, ","):find("25:back", 1, true) ~= nil and table.concat(page_events, ","):find("25:closed", 1, true) ~= nil, table.concat(page_events, ","))

    T.section("screenshot")
    T.expect("a screenshot is asked for", risen.debug.screenshot("gui_test", 2) == true and risen.debug.screenshot_status("gui_test") == "pending")
    T.wait_until(function() return risen.debug.screenshot_status("gui_test") ~= "pending" end, 10, "the screenshot is taken")
    T.eq("the screenshot status", risen.debug.screenshot_status("gui_test"), "done")
    local f = io.open((risen.mods.config().path:gsub("mods%.toml$", "screenshots/gui_test.png")), "rb")
    local head = f and f:read(8) or ""
    local size = f and f:seek("end") or 0
    if f then f:close() end
    T.expect("it is a PNG of the window", head == "\137PNG\r\n\26\n" and size > 100000, #head .. " bytes head, size " .. tostring(size))
    T.expect("a bad name is refused", select(1, risen.debug.screenshot("../x")) == nil and select(1, risen.debug.screenshot("")) == nil)

    T.section("back to the game")
    local cont = find_by_click(main, "OnClick_Continue")
    T.expect("Continue is a stock tab", cont ~= nil)
    cont:click()
    T.wait_until(function() return pages() == "1" end, 10, "the game view returns")
    local t2 = ticks
    T.wait_until(function() return ticks > t2 + 20 end, 20, "the world runs again")
    T.expect("the world ticks again", ticks > t2 + 20)

    T.section("bad arguments are refused")
    T.expect("open / close / show need a known page", select(1, risen.gui.open(999)) == nil and select(1, risen.gui.close(-3)) == nil and select(1, risen.gui.show(77, true)) == nil)
    T.expect("click / mouse / key validate their arguments", select(1, risen.gui.click(-5, 10)) == nil and select(1, risen.gui.click(10, 99999)) == nil
        and select(1, risen.gui.key(0)) == nil and select(1, risen.gui.key(999)) == nil and select(1, risen.gui.mouse("hover", 1, 1)) == nil)
    T.expect("leaf_page / hide_page_on validate their pages", select(1, risen.gui.leaf_page(0)) == nil and select(1, risen.gui.leaf_page(13, 13)) == nil and select(1, risen.gui.hide_page_on(13, 99)) == nil)
    T.expect("size and position are range checked", select(1, copy:set_size(-1, 5)) == nil and select(1, copy:set_position(1e9, 0)) == nil)
end, "gui")
