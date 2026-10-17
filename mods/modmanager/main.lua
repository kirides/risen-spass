-- In-game mod manager: a "Mods" tab in the game's menu (ESC) with a page that lists the mods and manages them (docs/LUA_API.md "The mod manager page").
-- model.lua is the logic (testable without the game), ui.lua draws it with the engine's own widgets.
local Model = require("model")
local UI = require("ui")

local model, ui
local attempts, frames = 0, 0

local dirty = false

local function draw()
    dirty = false
    if ui and ui:valid() then
        local ok, err = pcall(function() ui:render(model:snapshot()) end)
        if not ok then
            risen.log.error("modmanager: drawing failed: " .. tostring(err))
        end
    end
end

-- A click handler must not change the clicked button itself: when the script returns, the engine puts the visible / disabled flags of the click event back on the window
-- (eCGuiWindow2::OnScriptEvent). So the page is redrawn in the next GUI frame.
local function redraw()
    dirty = true
end

local controller = {}

function controller.on_open()
    model:close_options()
    model:refresh()
    risen.gui.close_menu()
    local ok, why = risen.gui.open(25)
    if not ok then
        risen.log.warn("modmanager: the Mods page could not be opened: " .. tostring(why))
    end
    redraw()
end

function controller.on_row(i)
    local r = model:snapshot().rows[i]
    if r then
        model:select(r.id)
    end
    redraw()
end

-- an arrow (dir -1 / 1) or the value (1) of row i of the options page: select the option, then one step
function controller.on_opt(i, dir)
    local r = model:snapshot().rows[i]
    if r and model.view == "options" then
        model:select(r.id)
        model:option_step(dir)
    end
    redraw()
end

function controller.on_action(name)
    model:press(name)
    redraw()
end

function controller.on_page(delta)
    model:page_to(model:current_page() + delta)
    redraw()
end

function controller.on_answer(choice)
    model:answer(choice)
    redraw()
end

local function install()
    local built, why = UI.install(controller)
    if not built then
        return false, why
    end
    ui = built
    model:refresh()  -- the mods are all loaded by now (while this mod loads, it is not "loaded" itself yet)
    draw()
    return true
end

-- The menu pages exist from the start of the application, but not necessarily when the mods load: try on the first frames, then keep an eye on the page (a changed
-- language rebuilds the menu pages) and put the tabs back when they are gone.
risen.on("ui.frame", function()
    frames = frames + 1
    if dirty then
        draw()
    end
    if ui == nil then
        if frames % 20 == 1 and attempts < 10 then
            attempts = attempts + 1
            local ok, why = install()
            if not ok and attempts == 10 then
                risen.log.warn("modmanager: the Mods page could not be built: " .. tostring(why))
            end
        end
    elseif frames % 120 == 0 and not ui:valid() then
        risen.log.info("modmanager: the menu pages were rebuilt, building the Mods page again")
        ui = nil
        attempts = 0
    end
end)

risen.on("mods.loaded", function()
    model:refresh()
    redraw()
end)

risen.on("ui.page", function(ev)
    if ev.page == 25 and ev.kind == "opened" then
        model:close_options()
        model:refresh()
        redraw()
    end
end)

risen.on_unload(function()
    if ui and ui:valid() then
        ui:remove()
    end
end)

model = Model.new({
    mods = risen.mods,
    options = risen.options,
    version = risen.version,
    self_id = mod.id,
    log = function(text, tone) risen.log[tone == "info" and "info" or "warn"]("modmanager: " .. text) end,
}, { rows = 12, option_rows = 10 })

-- Automation interface (the scenario `modmanager`, other mods): `risen.emit("modmanager.state", out)` fills `out.snapshot` (what the page shows), `out.model` and `out.ui`.
risen.on("modmanager.state", function(out)
    if type(out) == "table" then
        out.snapshot = model:snapshot()
        out.model = model
        out.ui = ui
    end
end)
