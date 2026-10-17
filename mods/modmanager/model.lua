-- The model of the in-game mod manager: everything the "Mods" page shows and does, with no GUI in it. It only talks to a `host` table
-- ({ mods = <risen.mods>, version = "<layer version>" }), so the logic is tested without the game (tests/modmanager_tests.inc with a fake host)
-- and in the game by driving the page itself (scenario `modmanager`).
--
-- The page (ui.lua) asks the model for a `snapshot()` and draws it; a click calls `select(id)`, `press(action)`, `answer(choice)`, `page_to(n)`.
-- Every function returns quickly and never raises on a host that misbehaves: a failing host call becomes a message in the snapshot.

local M = {}
M.__index = M

M.ROWS = 12
M.OPTION_ROWS = 10   -- rows of the options page (one setting per row, like the game's own Settings pages)

local function join(list, sep)
    return table.concat(list or {}, sep or ", ")
end

local function plural(n, one, many)
    return n == 1 and one or many
end

-- "3 persistent NPCs, 1 spawn rule" for what a mod left in the savegame; nil when there is nothing.
function M.describe_data(data)
    if type(data) ~= "table" or not data.present then
        return nil
    end
    local parts = {}
    local function add(n, one, many)
        if type(n) == "number" and n > 0 then
            parts[#parts + 1] = ("%d %s"):format(n, plural(n, one, many))
        end
    end
    add(data.npcs, "persistent NPC", "persistent NPCs")
    add(data.spawn_rules, "spawn rule", "spawn rules")
    add(data.ai_states, "saved AI state", "saved AI states")
    add(data.routines, "routine record", "routine records")
    -- other keys of the mod's own store
    local other = 0
    for k, n in pairs(data.keys or {}) do
        if k ~= "persistent_npcs" and k ~= "spawn_rules" and k ~= "ai_states" and k ~= "npc_routines" and type(n) == "number" then
            other = other + 1
        end
    end
    add(other, "saved value", "saved values")
    if #parts == 0 then
        return nil
    end
    return join(parts)
end

-- Something in the world that "switch off and remove its NPCs" can act on (a mere saved value of the mod's own store is only mentioned).
local function has_world_data(entry)
    local d = entry.data
    return type(d) == "table" and d.present == true and ((d.npcs or 0) + (d.spawn_rules or 0) + (d.ai_states or 0) + (d.routines or 0)) > 0
end

local function tone_of(entry, dirty)
    if entry.state == "failed" or entry.state == "skipped" then
        return "fail"
    end
    if entry.state == "disabled" or entry.state == "unloaded" then
        return "off"
    end
    if (entry.restart_required and #entry.restart_required > 0) or dirty then
        return "warn"
    end
    return "ok"
end

local STATE_TAG = { loaded = "", failed = "failed", skipped = "skipped", disabled = "off", unloaded = "unloaded" }

---@param host { mods: table, version: string }
---@param opts? { rows?: integer }
function M.new(host, opts)
    local self = setmetatable({}, M)
    self.host = host
    self.rows_per_page = (opts and opts.rows) or M.ROWS
    self.opt_rows_per_page = (opts and (opts.option_rows or opts.rows)) or M.OPTION_ROWS
    self.entries = {}        -- id -> entry
    self.order = {}          -- ids as listed (display order)
    self.selected = nil
    self.page = 1
    self.message = nil       -- { text, tone = "info" | "warn" | "error" }
    self.prompt = nil        -- see ask()
    self.order_changed = false  -- the custom order was changed in this session: it applies at the next start
    self.block_cache = {}
    self.view = "mods"       -- "mods" | "options" (the options of the selected mod, risen.options)
    self.opts = {}           -- mod id -> { mod, title, entries } (risen.options.list)
    self.opt_sel = nil       -- id of the selected option
    self.opt_page = 1
    self:refresh()
    return self
end

local function safe(fn, ...)
    local ok, a, b = pcall(fn, ...)
    if not ok then
        return nil, tostring(a)
    end
    return a, b
end

---Reads the mod list and the load order from the host again; keeps the selection where possible.
function M:refresh()
    local list, err = safe(self.host.mods.list)
    if type(list) ~= "table" then
        -- keep what was shown; say why it is not up to date
        self:_say("Could not read the mod list: " .. tostring(err), "error")
        return
    end
    local ord = safe(self.host.mods.order) or {}
    self.entries = {}
    for _, e in ipairs(list) do
        if type(e) == "table" and type(e.id) == "string" then
            self.entries[e.id] = e
        end
    end
    -- display order: what the next start would load, in that order; then everything else in the host's order
    local seen, out = {}, {}
    for _, id in ipairs(ord.planned or {}) do
        if self.entries[id] and not seen[id] then
            seen[id] = true
            out[#out + 1] = id
        end
    end
    for _, e in ipairs(list) do
        if type(e) == "table" and type(e.id) == "string" and not seen[e.id] then
            seen[e.id] = true
            out[#out + 1] = e.id
        end
    end
    self.order = out
    self.current_order = ord.current or {}
    self.planned_order = ord.planned or {}
    self.custom_order = ord.custom or {}
    self.block_cache = {}
    if not (self.selected and self.entries[self.selected]) then
        self.selected = out[1]
    end
    self:_keep_selection_visible()
    self:_refresh_options()
end

-- The options the loaded mods defined (risen.options.list). A host without them has no options page.
function M:_refresh_options()
    self.opts = {}
    local list = self.host.options and safe(self.host.options.list)
    for _, item in ipairs(type(list) == "table" and list or {}) do
        if type(item) == "table" and type(item.mod) == "string" and type(item.entries) == "table" and #item.entries > 0 then
            self.opts[item.mod] = item
        end
    end
    if self.view == "options" then
        local item = self.selected and self.opts[self.selected]
        if not item then
            self.view = "mods"
        elseif not self:_opt_entry(self.opt_sel) then
            self.opt_sel = item.entries[1].id
        end
    end
end

-- The rows of the options page in pages: one per option, plus a heading row where the `group` of the entries changes (like "Display" on the game's Graphics page).
-- A heading is never the last row of a page.
function M:_opt_pages()
    local item = self:_opt_item()
    local items, last = {}, nil
    for _, e in ipairs(item and item.entries or {}) do
        if e.group and e.group ~= "" and e.group ~= last then
            items[#items + 1] = { heading = e.group }
        end
        last = e.group
        items[#items + 1] = { entry = e }
    end
    local pages, page = {}, {}
    local per = math.max(2, self.opt_rows_per_page)
    for i, it in ipairs(items) do
        if #page >= per or (it.heading and #page == per - 1 and items[i + 1]) then
            pages[#pages + 1] = page
            page = {}
        end
        page[#page + 1] = it
    end
    if #page > 0 or #pages == 0 then
        pages[#pages + 1] = page
    end
    return pages
end

---The page of the options list that shows option `id` (nil for an unknown id).
function M:option_page(id)
    for n, page in ipairs(self:_opt_pages()) do
        for _, it in ipairs(page) do
            if it.entry and it.entry.id == id then
                return n
            end
        end
    end
end

function M:option_count(id)
    local item = self.opts[id or self.selected]
    return item and #item.entries or 0
end

---@return OptionsOfMod|nil
function M:_opt_item()
    return self.selected and self.opts[self.selected] or nil
end

function M:_opt_entry(id)
    local item = self:_opt_item()
    for _, e in ipairs(item and item.entries or {}) do
        if e.id == id then
            return e
        end
    end
end

---Shows the options of the selected mod (false and a message when it has none).
function M:open_options()
    self.prompt = nil
    self.message = nil
    local item = self:_opt_item()
    if not item then
        self:_say("This mod has no options.", "info")
        return false
    end
    self.view = "options"
    self.opt_sel = item.entries[1].id
    self.opt_page = 1
    return true
end

function M:close_options()
    self.view = "mods"
    self.prompt = nil
    self.message = nil
    return true
end

function M:count()
    return #self.order
end

function M:pages()
    if self.view == "options" then
        return #self:_opt_pages()
    end
    return math.max(1, math.ceil(#self.order / self.rows_per_page))
end

function M:_index_of(id)
    for i, v in ipairs(self.order) do
        if v == id then
            return i
        end
    end
end

function M:_keep_selection_visible()
    local i = self.selected and self:_index_of(self.selected)
    if i then
        self.page = math.floor((i - 1) / self.rows_per_page) + 1
    end
    self.page = math.max(1, math.min(self.page, math.max(1, math.ceil(#self.order / self.rows_per_page))))
end

---The page of the list that is shown (the mods, or the options of the selected mod).
function M:current_page()
    return self.view == "options" and self.opt_page or self.page
end

function M:page_to(n)
    if type(n) ~= "number" then
        return false
    end
    n = math.max(1, math.min(math.floor(n), self:pages()))
    if self.view == "options" then
        self.opt_page = n
    else
        self.page = n
    end
    return true
end

---Selects a mod by id (a click on its row). Returns false for an unknown id.
function M:select(id)
    if self.view == "options" then
        if not self:_opt_entry(id) then
            return false
        end
        self.opt_sel = id
        self.message = nil
        return true
    end
    if not self.entries[id] then
        return false
    end
    self.selected = id
    self.prompt = nil
    self.message = nil
    return true
end

---Selects the mod `delta` rows away (keyboard navigation in a test); the page follows.
function M:move_selection(delta)
    local i = self.selected and self:_index_of(self.selected) or 0
    local j = math.max(1, math.min(#self.order, i + delta))
    if self.order[j] then
        self:select(self.order[j])
        self:_keep_selection_visible()
    end
    return self.selected
end

function M:selected_entry()
    return self.selected and self.entries[self.selected] or nil
end

function M:_say(text, tone)
    -- one readable line: the first line of the host's text (an error comes with a traceback), at most 220 characters
    text = tostring(text):match("^[^\r\n]*") or ""
    if #text > 220 then
        text = text:sub(1, 217) .. "..."
    end
    self.message = { text = text, tone = tone or "info" }
    if self.host.log then
        pcall(self.host.log, text, tone or "info")
    end
end

---------------------------------------------------------------------------------------------------------------------
-- Load order
---------------------------------------------------------------------------------------------------------------------

-- The ids that take part in an order: everything with a valid manifest.
function M:_orderable()
    local out = {}
    for _, id in ipairs(self.order) do
        local e = self.entries[id]
        if e and id:sub(1, 1) ~= "!" and e.kind ~= "manifest" then
            out[#out + 1] = id
        end
    end
    return out
end

-- The list with `id` moved one place (dir = -1 up, +1 down) among the orderable mods; nil at the end of the list.
function M:_moved(id, dir)
    local ids = self:_orderable()
    local at
    for i, v in ipairs(ids) do
        if v == id then
            at = i
        end
    end
    if not at or not ids[at + dir] then
        return nil
    end
    ids[at], ids[at + dir] = ids[at + dir], ids[at]
    return ids
end

---Whether `id` could move one place; false plus the host's reason when a requirement forbids it.
---@return boolean|nil ok  nil: nowhere to go (first / last)
---@return string|nil why
function M:can_move(id, dir)
    local key = id .. ":" .. dir
    local c = self.block_cache[key]
    if c then
        return c[1], c[2]
    end
    local ids = self:_moved(id, dir)
    local ok, why
    if not ids then
        ok, why = nil, dir < 0 and "it is the first mod in the load order" or "it is the last mod in the load order"
    else
        local good, reason = safe(self.host.mods.check_order, ids)
        if good then
            ok = true
        else
            ok, why = false, reason or "the load order would break a dependency"
        end
    end
    self.block_cache[key] = { ok, why }
    return ok, why
end

---------------------------------------------------------------------------------------------------------------------
-- Actions
---------------------------------------------------------------------------------------------------------------------

local function enabled_dependents(self, entry)
    local out = {}
    for _, id in ipairs(entry.needed_by or {}) do
        local e = self.entries[id]
        if e and e.enabled ~= false and e.state ~= "disabled" then
            out[#out + 1] = id
        end
    end
    return out
end

local function disabled_requirements(self, entry)
    local out = {}
    for _, id in ipairs(entry.requires or {}) do
        local e = self.entries[id]
        if e and (e.enabled == false or e.state == "disabled") then
            out[#out + 1] = id
        end
    end
    return out
end

function M:_report_message(report, reason, what)
    if not report then
        self:_say(what .. ": " .. tostring(reason or "refused"), "error")
        return false
    end
    local tone = "info"
    local text = what .. ": " .. tostring(report.summary or "done")
    local restart = 0
    for _, a in ipairs(report.actions or {}) do
        restart = restart + #(a.restart_required or {})
        if a.action == "failed" then
            tone = "error"
        end
    end
    if reason or report.ok == false then
        tone = "error"
        if reason and not text:find(reason, 1, true) then
            text = what .. ": " .. tostring(reason)
        end
    elseif restart > 0 then
        tone = "warn"
    end
    self:_say(text, tone)
    return tone ~= "error"
end

function M:_do_disable(id, cascade)
    local report, reason = safe(self.host.mods.disable, id, { cascade = cascade or false, now = true })
    local ok = self:_report_message(report, reason, "Switched off " .. id)
    self:refresh()
    return ok
end

function M:_do_enable(id, cascade)
    local report, reason = safe(self.host.mods.enable, id, { cascade = cascade or false, now = true })
    local ok = self:_report_message(report, reason, "Switched on " .. id)
    self:refresh()
    return ok
end

---The ask() of a confirmation: the page shows `lines` and the buttons yes / alt / no (alt only when given).
function M:_ask(kind, id, lines, yes, alt, no)
    self.prompt = { kind = kind, id = id, lines = lines, yes = yes, alt = alt, no = no or "Cancel" }
end

---Switch the selected mod off or on (asks first when others need it, or when it left data in the savegame).
function M:toggle()
    local e = self:selected_entry()
    if not e then
        return false
    end
    self.prompt = nil
    local id = e.id
    if e.enabled ~= false and e.state ~= "disabled" then
        if self.host.self_id and id == self.host.self_id and not self.self_warned then
            self.self_warned = true
            self:_ask("self_off", id, {
                ("%s is this page. Switched off, the Mods tab disappears for good."):format(e.name),
                "Switch it on again by editing mods.toml (delete the entry) and restarting.",
            }, "Switch off", nil, "Cancel")
            return "prompt"
        end
        local deps = enabled_dependents(self, e)
        if #deps > 0 then
            self:_ask("cascade_off", id, {
                ("%s is needed by %s."):format(e.name, join(deps)),
                ("Switch off %s too?"):format(plural(#deps, "that mod", "all of them")),
            }, "Switch all off", nil, "Cancel")
            return "prompt"
        end
        if has_world_data(e) then
            self:_ask("data_off", id, {
                ("%s left data in this game: %s."):format(e.name, M.describe_data(e.data)),
                "Switched off, they stay in the savegame (nameless and without their mod).",
            }, "Switch off and remove its NPCs", "Switch off only", "Cancel")
            return "prompt"
        end
        return self:_do_disable(id, false)
    end
    local need = disabled_requirements(self, e)
    if #need > 0 then
        self:_ask("cascade_on", id, {
            ("%s needs %s, which is switched off."):format(e.name, join(need)),
            "Switch on the required mods too?",
        }, "Switch all on", nil, "Cancel")
        return "prompt"
    end
    return self:_do_enable(id, false)
end

---Answer the open question: "yes", "alt" or "no".
function M:answer(choice)
    local p = self.prompt
    if not p then
        return false
    end
    self.prompt = nil
    if choice == "no" or choice == nil then
        self:_say("Cancelled.", "info")
        return true
    end
    if p.kind == "self_off" and choice == "yes" then
        return self:toggle()
    elseif p.kind == "cascade_off" and choice == "yes" then
        return self:_do_disable(p.id, true)
    elseif p.kind == "cascade_on" and choice == "yes" then
        return self:_do_enable(p.id, true)
    elseif p.kind == "data_off" then
        if choice == "yes" then
            -- switch it off first (the mod stops declaring its NPCs), then remove what it left in the world: the `orphans` of a mod that is not running
            local ok = self:_do_disable(p.id, false)
            if not ok then
                return false
            end
            local list, reason = safe(self.host.mods.orphans, p.id)
            if not list then
                self:_say(("Switched off %s, but its leftovers could not be listed: %s"):format(p.id, tostring(reason)), "warn")
                return false
            end
            ---@type integer|nil, string|nil
            local removed, why = 0, nil
            if #list > 0 then
                removed, why = safe(self.host.mods.remove_orphans, p.id, list)
            end
            if removed == nil then
                self:_say(("Switched off %s, but its leftovers could not be removed: %s"):format(p.id, tostring(why)), "warn")
                return false
            end
            self:_say(("Switched off %s and removed %d of its NPC(s) from this game."):format(p.id, removed), "info")
            self:refresh()
            return true
        elseif choice == "alt" then
            return self:_do_disable(p.id, false)
        end
    end
    return false
end

function M:reload()
    local e = self:selected_entry()
    if not e then
        return false
    end
    self.prompt = nil
    if e.reloadable == false then
        self:_say("Cannot reload " .. e.name .. ": " .. tostring(e.reload_note or e.reason or "not possible"), "error")
        return false
    end
    local report, reason = safe(self.host.mods.reload, e.id, { now = true })
    local ok = self:_report_message(report, reason, "Reloaded " .. e.name)
    self:refresh()
    return ok
end

function M:move(dir)
    local e = self:selected_entry()
    if not e then
        return false
    end
    self.prompt = nil
    local ok, why = self:can_move(e.id, dir)
    if ok == nil then
        self:_say(e.name .. ": " .. why .. ".", "info")
        return false
    end
    if not ok then
        self:_say(("%s cannot move %s: %s"):format(e.name, dir < 0 and "up" or "down", why), "error")
        return false
    end
    local ids = self:_moved(e.id, dir)
    local saved, reason = safe(self.host.mods.set_order, ids)
    if not saved then
        self:_say("Could not save the order: " .. tostring(reason), "error")
        return false
    end
    self.order_changed = true
    self:refresh()
    self:_say(("%s moved %s. The new load order applies at the next start."):format(e.name, dir < 0 and "up" or "down"), "warn")
    return true
end

function M:reset_order()
    self.prompt = nil
    local saved, reason = safe(self.host.mods.set_order, nil)
    if not saved then
        self:_say("Could not reset the order: " .. tostring(reason), "error")
        return false
    end
    self.order_changed = true
    self:refresh()
    self:_say("Load order reset to the default (alphabetical, dependencies first). It applies at the next start.", "warn")
    return true
end

---------------------------------------------------------------------------------------------------------------------
-- Options of the selected mod
---------------------------------------------------------------------------------------------------------------------

function M:_say_change(id, report)
    self:refresh()
    local e = self:_opt_entry(id)
    local label = e and e.name or id
    if not report.changed then
        self:_say(label .. ": no change.", "info")
    elseif report.error then
        self:_say(label .. " changed, but: " .. report.error, "error")
    elseif report.restart then
        self:_say(("%s: %s. Applies at the next start of the game."):format(label, e and e.text or tostring(report.value)), "warn")
    else
        self:_say(("%s: %s"):format(label, e and e.text or tostring(report.value)), "info")
    end
    return not report.error
end

---One step of the selected option: dir = 1 next value / switch / increase, dir = -1 previous value / decrease.
function M:option_step(dir)
    local e = self:_opt_entry(self.opt_sel)
    if not e then
        return false
    end
    self.prompt = nil
    if e.kind == "text" then
        self:_say(e.name .. " is free text: change it in the mod's config.toml.", "info")
        return false
    end
    if e.kind == "slider" and type(e.value) == "number" then
        if dir > 0 and e.value >= e.max then
            self:_say(e.name .. " is at its maximum.", "info")
            return false
        elseif dir < 0 and e.value <= e.min then
            self:_say(e.name .. " is at its minimum.", "info")
            return false
        end
    end
    local ok, report = safe(self.host.options.step, self.selected, e.id, dir)
    if not ok then
        self:_say(e.name .. ": " .. tostring(report or "refused"), "error")
        return false
    end
    return self:_say_change(e.id, report)
end

---The selected option back to its default.
function M:option_default()
    local e = self:_opt_entry(self.opt_sel)
    if not e then
        return false
    end
    self.prompt = nil
    local ok, report = safe(self.host.options.reset, self.selected, e.id)
    if not ok then
        self:_say(e.name .. ": " .. tostring(report or "refused"), "error")
        return false
    end
    return self:_say_change(e.id, report)
end

---Every option of the selected mod back to its default.
function M:option_defaults_all()
    if not self:_opt_item() then
        return false
    end
    self.prompt = nil
    local ok, report = safe(self.host.options.reset, self.selected)
    if not ok then
        self:_say("Could not reset: " .. tostring(report or "refused"), "error")
        return false
    end
    self:refresh()
    ---@type OptionChange
    local done = type(report) == "table" and report or {}
    local n = done.count or 0
    if done.error then
        self:_say(("Reset %d option(s), but: %s"):format(n, done.error), "error")
        return false
    end
    self:_say(n == 0 and "Everything is at its default already." or ("Reset %d option(s) to their defaults."):format(n), "info")
    return true
end

---Runs an action by name: "toggle", "reload", "up", "down", "reset"; "options" opens the options of the selected mod. In the options view the buttons mean: next value
---(toggle), previous value (reload), this option's default (up), every default (down), back to the mods (reset).
function M:press(action)
    if action == "options" then
        return self:open_options()
    end
    if self.view == "options" then
        if action == "toggle" then
            return self:option_step(1)
        elseif action == "reload" then
            return self:option_step(-1)
        elseif action == "up" then
            return self:option_default()
        elseif action == "down" then
            return self:option_defaults_all()
        elseif action == "reset" then
            return self:close_options()
        end
        return false
    end
    if action == "toggle" then
        return self:toggle()
    elseif action == "reload" then
        return self:reload()
    elseif action == "up" then
        return self:move(-1)
    elseif action == "down" then
        return self:move(1)
    elseif action == "reset" then
        return self:reset_order()
    end
    return false
end

---------------------------------------------------------------------------------------------------------------------
-- What to draw
---------------------------------------------------------------------------------------------------------------------

function M:order_pending()
    -- the order the next start would use differs from the one the running game used
    local a, b = {}, {}
    for _, id in ipairs(self.current_order or {}) do
        if self.entries[id] and self.entries[id].state == "loaded" then
            a[#a + 1] = id
        end
    end
    for _, id in ipairs(self.planned_order or {}) do
        if self.entries[id] and self.entries[id].state == "loaded" then
            b[#b + 1] = id
        end
    end
    if #a ~= #b then
        return true
    end
    for i = 1, #a do
        if a[i] ~= b[i] then
            return true
        end
    end
    return false
end

local function clip(text, n)
    if #text <= n then
        return text
    end
    return text:sub(1, n - 2) .. ".."
end
M.clip = clip

function M:row_label(e)
    local text = ("%s  %s"):format(clip(e.name ~= "" and e.name or e.id, 30), e.version or "")
    local tag = STATE_TAG[e.state]
    if tag and tag ~= "" then
        text = text .. "  (" .. tag .. ")"
    end
    if e.state == "loaded" and e.restart_required and #e.restart_required > 0 then
        text = text .. "  *"
    end
    return text
end

function M:detail(e)
    local lines = {}
    local function add(text, tone)
        lines[#lines + 1] = { text = text, tone = tone }
    end
    if e.name and #e.name > 27 then
        add(e.name)
    end
    add(("%s %s%s"):format(e.id, e.version or "", (e.author and e.author ~= "") and ("   by " .. e.author) or ""))
    local state = e.state
    if e.state == "loaded" and e.load_order then
        state = ("loaded, load order %d"):format(e.load_order)
        if (e.load_count or 1) > 1 then
            state = state .. (", reloaded %dx"):format(e.load_count - 1)
        end
    end
    add("State: " .. state, tone_of(e, false) == "fail" and "fail" or nil)
    if e.reason and e.reason ~= "" then
        add(e.reason, "fail")
    end
    if #(e.requires or {}) > 0 then
        add("Requires: " .. join(e.requires))
    end
    if #(e.load_after or {}) > 0 then
        add("Loads after: " .. join(e.load_after))
    end
    if #(e.needed_by or {}) > 0 then
        add("Needed by: " .. join(e.needed_by))
    end
    if e.reloadable == false and e.reload_note and e.reload_note ~= "" then
        add("Reload: " .. e.reload_note)
    elseif e.reload_note and e.reload_note ~= "" then
        add("Reload: " .. e.reload_note)
    end
    for _, r in ipairs(e.restart_required or {}) do
        add("Restart required: " .. r, "warn")
    end
    local data = M.describe_data(e.data)
    if data then
        add("Savegame data: " .. data .. (e.data.saved_version and (" (written by " .. e.data.saved_version .. ")") or ""))
    end
    if e.description and e.description ~= "" then
        add(e.description)
    end
    return lines
end

local function option_detail(e)
    local lines = {}
    local function add(text)
        lines[#lines + 1] = { text = text }
    end
    if e.description and e.description ~= "" then
        add(e.description)
        add("")
    end
    add("Value: " .. e.text .. (e.custom and "  (not in the list)" or ""))
    add("Default: " .. e.default_text)
    if e.kind == "slider" then
        add(("Range: %s to %s, step %s"):format(tostring(e.min), tostring(e.max), tostring(e.step)))
    elseif e.kind == "dropdown" and e.values then
        add(("Choice %s of %d"):format(e.index and tostring(e.index) or "-", #e.values))
    elseif e.kind == "text" then
        add("Free text: change it in config.toml.")
    end
    if e.restart then
        add("Applies at the next start of the game.")
    end
    return lines
end

-- the one paragraph under the rows: what the selected option is for, its default and when it applies
local function option_hint(e)
    local parts = {}
    if e.description and e.description ~= "" then
        parts[#parts + 1] = e.description
    end
    local tail = "Default: " .. e.default_text .. "."
    if e.kind == "slider" then
        tail = ("Default: %s (%s to %s, step %s)."):format(e.default_text, tostring(e.min), tostring(e.max), tostring(e.step))
    elseif e.kind == "text" then
        tail = tail .. " Free text: change it in config.toml."
    end
    parts[#parts + 1] = tail
    if e.custom then
        parts[#parts + 1] = "The current value is not in the list."
    end
    if e.restart then
        parts[#parts + 1] = "Applies at the next start of the game."
    end
    return table.concat(parts, " ")
end

local NEXT_LABEL = { dropdown = "Next value >", slider = "Increase >" }
local PREV_LABEL = { dropdown = "< Previous value", slider = "< Decrease" }

-- the snapshot of the options of the selected mod
function M:_options_snapshot()
    local item = self:_opt_item() or { mod = self.selected or "", entries = {} }
    local entries = item.entries
    local rows, changed = {}, 0
    for _, e in ipairs(entries) do
        if e.changed then
            changed = changed + 1
        end
    end
    self.opt_page = math.max(1, math.min(self.opt_page, self:pages()))
    for n, it in ipairs(self:_opt_pages()[self.opt_page] or {}) do
        local e = it.entry
        if not e then
            rows[#rows + 1] = { heading = true, text = it.heading, label = clip(it.heading, 40), tone = "ok", selected = false, index = n }
        else
            local slider = e.kind == "slider" and type(e.value) == "number"
            rows[#rows + 1] = {
                id = e.id,
                index = n,
                text = ("%s:  %s"):format(clip(e.name, 32), e.text),
                label = clip(e.name, 40),
                value_text = clip(e.text, 34),
                kind = e.kind,
                fraction = slider and math.max(0, math.min(1, (e.value - e.min) / (e.max - e.min))) or nil,
                tone = e.changed and "warn" or "ok",
                selected = e.id == self.opt_sel,
                editable = e.kind ~= "text",
                can_prev = e.kind == "toggle" or e.kind == "dropdown" or (slider and e.value > e.min),
                can_next = e.kind == "toggle" or e.kind == "dropdown" or (slider and e.value < e.max),
            }
        end
    end
    local mod = self.entries[self.selected]
    local snap = {
        view = "options",
        title = clip("Options: " .. (item.title or (mod and mod.name ~= "" and mod.name) or item.mod), 30),
        summary = ("%d %s, %d changed"):format(#entries, plural(#entries, "option", "options"), changed),
        version = self.host.version,
        rows = rows,
        page = self.opt_page,
        pages = self:pages(),
        count = #entries,
        selected = self.opt_sel,
        message = self.message,
    }
    local e = self:_opt_entry(self.opt_sel)
    local free = e == nil or e.kind == "text"
    snap.detail_title = e and clip(e.name, 27) or ""
    snap.detail = e and option_detail(e) or {}
    snap.hint = e and option_hint(e) or ""
    local slider = e ~= nil and e.kind == "slider" and type(e.value) == "number"
    local toggle = e ~= nil and e.kind == "toggle"
    snap.buttons = {
        toggle = { label = toggle and (e.value and "Switch off" or "Switch on") or (e and NEXT_LABEL[e.kind]) or "Next value >", enabled = not free and not (slider and e.value >= e.max) },
        reload = { label = (e and PREV_LABEL[e.kind]) or "< Previous value", enabled = not free and not toggle and not (slider and e.value <= e.min) },
        up = { label = "Default value", enabled = e ~= nil and e.changed == true },
        down = { label = "All defaults", enabled = changed > 0 },
        reset = { label = "Back to the mods", enabled = true },
        options = { label = "Options", enabled = false },
    }
    return snap
end

---Everything the page draws.
function M:snapshot()
    if self.view == "options" and self:_opt_item() then
        return self:_options_snapshot()
    end
    local rows = {}
    local first = (self.page - 1) * self.rows_per_page + 1
    local dirty = self:order_pending()
    for i = first, math.min(#self.order, first + self.rows_per_page - 1) do
        local id = self.order[i]
        local e = self.entries[id]
        rows[#rows + 1] = {
            id = id,
            index = i,
            text = self:row_label(e),
            tone = tone_of(e, false),
            selected = id == self.selected,
        }
    end
    local loaded, failed, off = 0, 0, 0
    for _, id in ipairs(self.order) do
        local s = self.entries[id].state
        if s == "loaded" then
            loaded = loaded + 1
        elseif s == "failed" or s == "skipped" then
            failed = failed + 1
        else
            off = off + 1
        end
    end
    local summary = ("%d %s: %d loaded"):format(#self.order, plural(#self.order, "mod", "mods"), loaded)
    if failed > 0 then
        summary = summary .. (", %d failed"):format(failed)
    end
    if off > 0 then
        summary = summary .. (", %d off"):format(off)
    end
    local snap = {
        view = "mods",
        title = "Mods",
        summary = summary,
        version = self.host.version,
        rows = rows,
        page = self.page,
        pages = self:pages(),
        count = #self.order,
        selected = self.selected,
        message = self.message,
        prompt = self.prompt,
        restart_note = dirty and "The load order changes at the next start." or nil,
    }
    local e = self:selected_entry()
    if e then
        snap.detail_title = clip(e.name ~= "" and e.name or e.id, 27)
        snap.detail = self:detail(e)
        local on = e.enabled ~= false and e.state ~= "disabled"
        local up_ok, up_why = self:can_move(e.id, -1)
        local down_ok, down_why = self:can_move(e.id, 1)
        snap.buttons = {
            toggle = { label = on and "Switch off" or "Switch on", enabled = true },
            reload = { label = "Reload", enabled = e.reloadable ~= false, why = e.reload_note },
            up = { label = up_ok == false and "Move up (blocked)" or "Move up", enabled = up_ok ~= nil, why = up_why, blocked = up_ok == false },
            down = { label = down_ok == false and "Move down (blocked)" or "Move down", enabled = down_ok ~= nil, why = down_why, blocked = down_ok == false },
            reset = { label = "Default order", enabled = #self.custom_order > 0 or self.order_changed },
            options = { label = "Options", enabled = false },
        }
        local n = self:option_count(e.id)
        if n > 0 then
            snap.buttons.options = { label = ("Options (%d)"):format(n), enabled = true }
        end
    else
        snap.detail_title = "No mods installed"
        snap.detail = { { text = "Put mod folders into bin\\spass\\mods." } }
        snap.buttons = {
            toggle = { label = "Switch off", enabled = false },
            reload = { label = "Reload", enabled = false },
            up = { label = "Move up", enabled = false },
            down = { label = "Move down", enabled = false },
            reset = { label = "Default order", enabled = false },
            options = { label = "Options", enabled = false },
        }
    end
    return snap
end

return M
