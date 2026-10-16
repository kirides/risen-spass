-- The model of the in-game mod manager: everything the "Mods" page shows and does, with no GUI in it. It only talks to a `host` table
-- ({ mods = <risen.mods>, version = "<layer version>" }), so the logic is tested without the game (tests/modmanager_tests.inc with a fake host)
-- and in the game by driving the page itself (scenario `modmanager`).
--
-- The page (ui.lua) asks the model for a `snapshot()` and draws it; a click calls `select(id)`, `press(action)`, `answer(choice)`, `page_to(n)`.
-- Every function returns quickly and never raises on a host that misbehaves: a failing host call becomes a message in the snapshot.

local M = {}
M.__index = M

M.ROWS = 12

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
    self.entries = {}        -- id -> entry
    self.order = {}          -- ids as listed (display order)
    self.selected = nil
    self.page = 1
    self.message = nil       -- { text, tone = "info" | "warn" | "error" }
    self.prompt = nil        -- see ask()
    self.order_changed = false  -- the custom order was changed in this session: it applies at the next start
    self.block_cache = {}
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
end

function M:count()
    return #self.order
end

function M:pages()
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
    self.page = math.max(1, math.min(self.page, self:pages()))
end

function M:page_to(n)
    if type(n) ~= "number" then
        return false
    end
    n = math.max(1, math.min(math.floor(n), self:pages()))
    self.page = n
    return true
end

---Selects a mod by id (a click on its row). Returns false for an unknown id.
function M:select(id)
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

---Runs an action by name: "toggle", "reload", "up", "down", "reset".
function M:press(action)
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

---Everything the page draws.
function M:snapshot()
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
        }
    else
        snap.detail_title = "No mods installed"
        snap.detail = { { text = "Put mod folders into bin\\spass\\mods." } }
        snap.buttons = {
            toggle = { label = "Switch off", enabled = false },
            reload = { label = "Reload", enabled = false },
            up = { label = "Move up", enabled = false },
            down = { label = "Move down", enabled = false },
            reset = { label = "Default order", enabled = false },
        }
    end
    return snap
end

return M
