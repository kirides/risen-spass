-- modkit/runtime.lua: the generic run-time part of a mod exported by risen-modkit (docs/editor/REQUIREMENTS.md section 7).
-- The same file for every exported mod; the mod's content is plain data in data/*.lua. Do not edit: the next export replaces it
-- (hand-written additions go into custom.lua).
--
--   balance   risen.balance.install: items, item groups, new items (and in later versions recipes, NPC templates); the layer applies it on
--             the first tick and on every session.start
--
-- Event for other mods and tests: "modkit.applied" (mod id, when, report) after every application of the balance data
-- (when = "first tick", "session.start (new)", "session.start (load)"; report = risen.balance's BalanceReport).
local M = { VERSION = 1 }

local function nonempty(t) return type(t) == "table" and next(t) ~= nil end

function M.install(data)
    local id = mod and mod.id or "?"
    if nonempty(data.balance) then
        risen.balance.install(data.balance, {
            on_applied = function(when, report)
                if report and not report.ok then
                    for _, f in ipairs(report.failed or {}) do risen.log.warn(id .. ": " .. tostring(f)) end
                end
                risen.emit("modkit.applied", id, when, report)
            end,
        })
    end
end

return M
