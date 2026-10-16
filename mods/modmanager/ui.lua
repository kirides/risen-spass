-- The "Mods" page of the game's menu, built at run time from the engine's own widgets (risen.gui): a sixth tab next to Continue / Game / Settings / Credits / Quit on
-- every menu page that has the tab bar, and the page itself (`Menu_Cheats`, 25: the retail game never opens it) with a list of the mods, the selected mod's details and its
-- action buttons. Everything is cloned from stock widgets (so fonts, images and sounds are the game's), found by what they are, not by their (translated) text.
-- Positions are in the GUI's virtual resolution 1365 x 768 (the engine scales it to the window).
--
-- This file knows widgets; model.lua knows mods. `UI.install(controller)` returns the page object or nil, reason.

local UI = {}
UI.__index = UI

local PAGE_ID = 25     -- gEHudPage_Menu_Cheats
local PARENT_ID = 13   -- gEHudPage_Menu_Main: where ESC / "back" goes
local TAB_PAGES = { 13, 14, 18, 24 }  -- Menu_Main, Menu_Game, Menu_Options, Menu_Credits: the pages with the tab bar

-- Stock tab bar: slot -> the OnClick script of the stock button. The new tab is slot 4, Quit moves to slot 5.
local TAB_SLOT = { OnClick_Continue = 0, OnClick_Menu_Game = 1, OnClick_Menu_Options = 2, OnClick_Menu_Credits = 3, OnClick_ExitGame = 5 }
local MODS_SLOT = 4
local TAB_X0, TAB_STEP = 211, 158

local COLOR = {
    ok = "61; 44; 0; 255",
    warn = "150; 85; 0; 255",
    fail = "170; 20; 20; 255",
    off = "115; 105; 90; 255",
    selected = "237; 226; 198; 255",
    info = "61; 44; 0; 255",
    error = "170; 20; 20; 255",
}

-- ids of the controls on the page (the engine's keyboard focus walks IDUp / IDDown / IDLeft / IDRight)
local ID = { row = 101, prev = 120, next = 121, toggle = 130, reload = 131, up = 132, down = 133, reset = 134, yes = 140, alt = 141, no = 142 }

local ROW = { x = 312, y = 170, w = 396, h = 23, step = 26 }
local DETAIL = { x = 735, w = 325 }

local function log(...)
    risen.log.info("modmanager: " .. table.concat({ ... }, " "))
end

local function kids(w)
    return w:children() or {}
end

local function names_of(w)
    local out = {}
    for _, c in ipairs(kids(w)) do
        out[c:name() or ""] = c
    end
    return out
end

local function by_class(page, class, pred)
    for _, c in ipairs(kids(page)) do
        if c:class() == class and (not pred or pred(c)) then
            return c
        end
    end
end

local function point(s)
    local x, y = tostring(s):match("(-?%d+)%s*;%s*(-?%d+)")
    return tonumber(x), tonumber(y)
end

---------------------------------------------------------------------------------------------------------------------

---Creates (or finds again, after a reload of the mod) everything and wires the click scripts to `controller`:
---controller.on_row(i), on_action(name), on_page(delta), on_answer(choice), on_open().
function UI.install(controller)
    local self = setmetatable({ controller = controller, rows = {}, tabs = {}, w = {} }, UI)
    local ok, why = pcall(self._build, self)
    if not ok then
        return nil, tostring(why)
    end
    if why then
        return nil, why
    end
    return self
end

function UI:_tab_script()
    return "Spass_ModMgr_Tab"
end

function UI:_define_scripts()
    local c = self.controller
    local function define(name, fn)
        risen.script.define(name, function()
            local ok, err = pcall(fn)
            if not ok then
                risen.log.error("modmanager: " .. name .. ": " .. tostring(err))
            end
            return 0
        end)
    end
    define(self:_tab_script(), function() c.on_open() end)
    for i = 1, #self.rows_wanted do
        define("Spass_ModMgr_Row" .. i, function() c.on_row(i) end)
    end
    for _, a in ipairs({ "toggle", "reload", "up", "down", "reset" }) do
        define("Spass_ModMgr_" .. a, function() c.on_action(a) end)
    end
    define("Spass_ModMgr_Prev", function() c.on_page(-1) end)
    define("Spass_ModMgr_Next", function() c.on_page(1) end)
    define("Spass_ModMgr_Yes", function() c.on_answer("yes") end)
    define("Spass_ModMgr_Alt", function() c.on_answer("alt") end)
    define("Spass_ModMgr_No", function() c.on_answer("no") end)
end

-- Where to take the stock widgets from, by what they are.
function UI:_sources()
    local P = risen.gui.page
    local main, credits, load, settings, options, input = P(13), P(24), P(15), P(22), P(18), P(21)
    if not (main and credits and load and settings and options and input and P(PAGE_ID)) then
        return nil, "the menu pages do not exist yet"
    end
    local src = {}
    src.panel = by_class(load, "eCGuiPanel2")
    src.title = by_class(load, "eCGuiStatic2", function(c) return select(1, c:position()) == 522 end)
    src.label = by_class(settings, "eCGuiStatic2", function(c) return c:get("Font") == "fnt_menu" and c:get("FontColorNormal") == "43; 28; 0; 255" end)
    src.button = by_class(input, "eCScriptRadioButton")
    src.row = by_class(options, "eCScriptRadioButton", function(c) return c:get("ImgPushFaceHot") == "menue_subbutton_light" end)
    src.big_tab = by_class(credits, "eCScriptRadioButton", function(c) return c:get("OnClick") == "OnClick_Menu_Credits" end)
    for name, w in pairs(src) do
        if not w then
            return nil, "no stock widget for " .. name
        end
    end
    return src
end

local function setp(w, name, value)
    local ok = w:set(name, value)
    return ok
end

function UI:_build()
    self.rows_wanted = {}
    for i = 1, 12 do
        self.rows_wanted[i] = i
    end
    local src, why = self:_sources()
    if not src then
        return why
    end
    local page = risen.gui.page(PAGE_ID)
    self.page = page
    local have = names_of(page)

    -- the page controller (ESC goes back to the main menu) and the main page's reaction to this page
    if not have["spass_panel"] then
        local ok, e = risen.gui.leaf_page(PAGE_ID, PARENT_ID)
        if not ok then
            return "leaf_page: " .. tostring(e)
        end
        local ok2, e2 = risen.gui.hide_page_on(PARENT_ID, PAGE_ID)
        if not ok2 then
            return "hide_page_on: " .. tostring(e2)
        end
    end
    self:_define_scripts()

    local function make(name, source, spec)
        local w = have[name]
        if not w then
            w = source:clone(page)
            if not w then
                error("clone failed for " .. name)
            end
            setp(w, "Name", name)
        end
        if spec then
            if spec.id then setp(w, "ID", tostring(spec.id)) end
            if spec.x then w:set_position(spec.x, spec.y) end
            if spec.w then w:set_size(spec.w, spec.h) end
            if spec.text then w:set_text(spec.text) end
            if spec.script then setp(w, "OnClick", spec.script) end
            for k, v in pairs(spec.props or {}) do setp(w, k, v) end
        end
        self.w[name] = w
        return w
    end
    local function button(name, x, y, w, h, text, script, id)
        return make(name, src.button, { x = x, y = y, w = w, h = h, text = text, script = script, id = id })
    end

    -- rows: the stock sub-button (text only, light when hot, dark when pushed), left aligned
    for i = 1, #self.rows_wanted do
        local y = ROW.y + (i - 1) * ROW.step
        local w = make("spass_row" .. i, src.row, {
            x = ROW.x, y = y, w = ROW.w, h = ROW.h, text = "", script = "Spass_ModMgr_Row" .. i, id = ID.row + i - 1,
            props = { TextAlign = "eETextAlign_Left_Middle", BorderExtent = "10; 0; 4; 0" },
        })
        self.rows[i] = w
    end
    button("spass_prev", ROW.x, 490, 120, 26, "< Prev", "Spass_ModMgr_Prev", ID.prev)
    button("spass_next", ROW.x + ROW.w - 120, 490, 120, 26, "Next >", "Spass_ModMgr_Next", ID.next)
    make("spass_pagelabel", src.label, { x = ROW.x + 124, y = 491, w = ROW.w - 248, h = 24, text = "", props = { TextAlign = "eETextAlign_Center_Middle" } })
    make("spass_summary", src.label, { x = ROW.x, y = 530, w = ROW.w, h = 24, text = "", props = { TextAlign = "eETextAlign_Left_Middle" } })
    make("spass_version", src.label, { x = ROW.x, y = 556, w = ROW.w, h = 24, text = "", props = { TextAlign = "eETextAlign_Left_Middle", FontColorNormal = COLOR.off } })
    make("spass_note", src.label, { x = ROW.x, y = 584, w = ROW.w, h = 42, text = "", props = { TextAlign = "eETextAlign_Left_Top", Singleline = "False", Font = "fnt_small", FontColorNormal = COLOR.warn } })

    make("spass_dtitle", src.label, { x = DETAIL.x, y = 168, w = DETAIL.w, h = 28, text = "", props = { TextAlign = "eETextAlign_Left_Middle", Font = "fnt_large" } })
    make("spass_detail", src.label, { x = DETAIL.x, y = 200, w = DETAIL.w, h = 225, text = "", props = { TextAlign = "eETextAlign_Left_Top", Singleline = "False", Font = "fnt_small" } })
    make("spass_prompt", src.label, { x = DETAIL.x, y = 200, w = DETAIL.w, h = 225, text = "", props = { TextAlign = "eETextAlign_Left_Top", Singleline = "False" } })
    button("spass_toggle", DETAIL.x, 432, DETAIL.w, 28, "Switch off", "Spass_ModMgr_toggle", ID.toggle)
    button("spass_reload", DETAIL.x, 464, DETAIL.w, 28, "Reload", "Spass_ModMgr_reload", ID.reload)
    button("spass_up", DETAIL.x, 496, 160, 28, "Move up", "Spass_ModMgr_up", ID.up)
    button("spass_down", DETAIL.x + 165, 496, 160, 28, "Move down", "Spass_ModMgr_down", ID.down)
    button("spass_reset", DETAIL.x, 528, DETAIL.w, 28, "Default order", "Spass_ModMgr_reset", ID.reset)
    button("spass_yes", DETAIL.x, 432, DETAIL.w, 28, "Yes", "Spass_ModMgr_Yes", ID.yes)
    button("spass_alt", DETAIL.x, 464, DETAIL.w, 28, "Alt", "Spass_ModMgr_Alt", ID.alt)
    button("spass_no", DETAIL.x, 496, DETAIL.w, 28, "Cancel", "Spass_ModMgr_No", ID.no)

    -- keyboard focus: rows up / down, rows -> the action buttons, the buttons up / down
    local function link(name, up, down, left, right)
        local w = self.w[name]
        if up then setp(w, "IDUp", tostring(up)) end
        if down then setp(w, "IDDown", tostring(down)) end
        if left then setp(w, "IDLeft", tostring(left)) end
        if right then setp(w, "IDRight", tostring(right)) end
    end
    for i = 1, #self.rows_wanted do
        link("spass_row" .. i, i > 1 and ID.row + i - 2 or ID.next, i < #self.rows_wanted and ID.row + i or ID.prev, nil, ID.toggle)
    end
    link("spass_prev", ID.row + #self.rows_wanted - 1, nil, nil, ID.next)
    link("spass_next", ID.row + #self.rows_wanted - 1, nil, ID.prev, nil)
    link("spass_toggle", nil, ID.reload, ID.row, nil)
    link("spass_reload", ID.toggle, ID.up, ID.row, nil)
    link("spass_up", ID.reload, ID.reset, ID.row, ID.down)
    link("spass_down", ID.reload, ID.reset, ID.up, nil)
    link("spass_reset", ID.up, nil, ID.row, nil)
    link("spass_yes", nil, ID.alt, ID.row, nil)
    link("spass_alt", ID.yes, ID.no, ID.row, nil)
    link("spass_no", ID.alt, nil, ID.row, nil)
    setp(page, "DefaultCtrlID", tostring(ID.row))

    -- the tab bar of this page: the five stock tabs (cloned from the main menu) and the selected "Mods" tab
    local main = risen.gui.page(PARENT_ID)
    local present = names_of(page)
    for _, t in ipairs(kids(main)) do
        local script = t:get("OnClick")
        local slot = TAB_SLOT[script]
        if slot then
            local name = "spass_tab" .. slot
            local c = present[name] or t:clone(page)
            if c then
                setp(c, "Name", name)
                c:set_position(TAB_X0 + slot * TAB_STEP, 653)
            end
            self.tabs[#self.tabs + 1] = c
        end
    end
    local big = present["spass_tab_mods"] or src.big_tab:clone(page)
    setp(big, "Name", "spass_tab_mods")
    setp(big, "OnClick", self:_tab_script())
    big:set_text("Mods")
    big:set_position(TAB_X0 + MODS_SLOT * TAB_STEP, 650)
    self.big_tab = big

    -- the heading and the panel come last: a clone goes to the back of the page, and the panel has to be behind everything
    make("spass_title", src.title, { x = 522, y = 139, w = 310, h = 24, text = "Mods" })
    make("spass_panel", src.panel, {})

    -- the Mods tab on every page that has a tab bar
    for _, id in ipairs(TAB_PAGES) do
        self:_add_tab(id, src)
    end
    self:show_tabs(true)
    return nil
end

-- A tab labelled "Mods" in the bar of page `id` (a copy of that page's Quit tab); the other tabs move to the new six-slot grid.
function UI:_add_tab(id, src)
    local p = risen.gui.page(id)
    if not p then
        return
    end
    local have = names_of(p)
    local quit
    for _, c in ipairs(kids(p)) do
        if c:class() == "eCScriptRadioButton" and c:get("OnClick") == "OnClick_ExitGame" then
            quit = c
        end
    end
    if not quit then
        return
    end
    local tab = have["spass_tab_mods_" .. id]
    if not tab then
        tab = quit:clone(p)
        if not tab then
            return
        end
        setp(tab, "Name", "spass_tab_mods_" .. id)
    end
    tab:set_text("Mods")
    setp(tab, "OnClick", self:_tab_script())
    for _, c in ipairs(kids(p)) do
        if c:class() == "eCScriptRadioButton" then
            local slot = TAB_SLOT[c:get("OnClick")]
            if slot and c:get("Name") ~= "spass_tab_mods_" .. id then
                local _, y = c:position()
                c:set_position(TAB_X0 + slot * TAB_STEP, y or 653)
            end
        end
    end
    local _, y = quit:position()
    tab:set_position(TAB_X0 + MODS_SLOT * TAB_STEP, y or 653)
    self.tabs_by_page = self.tabs_by_page or {}
    self.tabs_by_page[id] = tab
end

function UI:show_tabs(visible)
    for _, t in pairs(self.tabs_by_page or {}) do
        if t:valid() then
            t:set_visible(visible)
        end
    end
end

---Puts the layout back as the game made it (when the mod is unloaded): the added tabs are hidden and the stock ones return to their slots.
function UI:remove()
    self:show_tabs(false)
    for _, id in ipairs(TAB_PAGES) do
        local p = risen.gui.page(id)
        if p then
            local x0 = { [0] = 289, 447, 605, 763, 0, 921 }
            for _, c in ipairs(kids(p)) do
                if c:class() == "eCScriptRadioButton" then
                    local slot = TAB_SLOT[c:get("OnClick")]
                    if slot then
                        local _, y = c:position()
                        c:set_position(x0[slot], y or 653)
                    end
                end
            end
        end
    end
end

function UI:valid()
    return self.page ~= nil and self.page:valid() and self.w.spass_panel ~= nil and self.w.spass_panel:valid()
end

---------------------------------------------------------------------------------------------------------------------
-- Drawing the model
---------------------------------------------------------------------------------------------------------------------

local function color(w, c)
    setp(w, "FontColorNormal", c)
    setp(w, "FontColorHot", c)
    setp(w, "FontColorPushed", c)
end

local function set_text(w, text)
    if w:text() ~= text then
        w:set_text(text)
    end
end

function UI:render(snap)
    local W = self.w
    set_text(W.spass_title, snap.title)
    for i, w in ipairs(self.rows) do
        local r = snap.rows[i]
        if r then
            w:set_visible(true)
            set_text(w, r.text)
            color(w, r.selected and COLOR.selected or COLOR[r.tone] or COLOR.ok)
            setp(w, "ImgPushFaceNormal", r.selected and "menue_subbutton_dark" or "")
        else
            w:set_visible(false)
        end
    end
    set_text(W.spass_pagelabel, snap.pages > 1 and ("%d / %d"):format(snap.page, snap.pages) or "")
    W.spass_prev:set_visible(snap.pages > 1)
    W.spass_next:set_visible(snap.pages > 1)
    set_text(W.spass_summary, snap.summary)
    set_text(W.spass_version, "risen-spass " .. tostring(snap.version or "?"))
    -- the last message of an action (in its colour), else the note that the load order applies at the next start
    if snap.message then
        set_text(W.spass_note, snap.message.text)
        color(W.spass_note, COLOR[snap.message.tone] or COLOR.info)
    else
        set_text(W.spass_note, snap.restart_note or "")
        color(W.spass_note, COLOR.warn)
    end

    local prompt = snap.prompt
    local in_prompt = prompt ~= nil
    for _, n in ipairs({ "spass_detail", "spass_toggle", "spass_reload", "spass_up", "spass_down", "spass_reset" }) do
        W[n]:set_visible(not in_prompt)
    end
    for _, n in ipairs({ "spass_prompt", "spass_yes", "spass_no" }) do
        W[n]:set_visible(in_prompt)
    end
    W.spass_alt:set_visible(in_prompt and prompt.alt ~= nil)
    set_text(W.spass_dtitle, snap.detail_title or "")
    if in_prompt then
        set_text(W.spass_prompt, table.concat(prompt.lines, "\n"))
        set_text(W.spass_yes, prompt.yes or "Yes")
        set_text(W.spass_alt, prompt.alt or "")
        set_text(W.spass_no, prompt.no or "Cancel")
    else
        local lines = {}
        for _, l in ipairs(snap.detail or {}) do
            lines[#lines + 1] = l.text
        end
        set_text(W.spass_detail, table.concat(lines, "\n"))
        local b = snap.buttons
        for _, n in ipairs({ "toggle", "reload", "up", "down", "reset" }) do
            local w, spec = W["spass_" .. n], b[n]
            set_text(w, spec.label)
            w:set_disabled(not spec.enabled)
        end
    end
end

return UI
