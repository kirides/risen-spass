-- modkit/layers.lua: the mod layers of a mod exported by risen-modkit (docs/editor/REQUIREMENTS.md sections 3.12, 7.6). Written only for projects with layers.
-- Do not edit: the next export replaces it (custom.lua may use `require("modkit.layers")`: enabled, set, toggle, names).
--
-- A layer is a named group of the mod's placed NPCs, world spawns and container fills (the members carry `layer = "<name>"` in data/npcs.lua and data/world.lua).
--   state    risen.save "modkit.layer.<name>" = true | false; absent = the layer's `enabled` at the start. Part of every savegame, rolled back by a load.
--   NPCs     made while the layer is on, removed when it goes off (a tracking risen.spawn.when rule: a new body each time it is switched on again);
--            an NPC's own appear_when / leave_when still apply on top
--   spawns   made when the layer is on (once per switch-on), removed again when it goes off; container fills happen once, the first time the layer is on
--   switch   enable_when / disable_when: a condition set of the event graphs' Condition node; the first time it holds the layer switches (once per savegame),
--            looked at about twice a second; Events graphs switch it as often as they like (Layer node)
-- Events for other mods and tests: "modkit.layer_changed" (name, enabled) after every change.
local L = {}

local defs = {}     -- name -> definition
local order = {}    -- names in the project's order
local ev            -- modkit.events (condition sets)
local frame = 0

local function key(name) return "modkit.layer." .. name end

function L.enabled(name)
    local d = defs[name]
    if not d then return false end
    local v = risen.save.get(key(name))
    if v == nil then return d.enabled and true or false end
    return v and true or false
end

-- returns true when the state changed
function L.set(name, on)
    local d = defs[name]
    if not d then
        risen.log.warn(("layers: no layer called %s"):format(tostring(name)))
        return false
    end
    on = on and true or false
    if L.enabled(name) == on then return false end
    risen.save.set(key(name), on)   -- first the state, then the effects
    risen.emit("modkit.layer_changed", name, on)
    return true
end

function L.toggle(name) return L.set(name, not L.enabled(name)) end

function L.names() return order end

-- a risen.cond condition that holds while the layer is on (re-evaluated when any layer changes)
function L.cond(name)
    return risen.cond.fn(function() return L.enabled(name) end, { on = "modkit.layer_changed" })
end

local function fired(name, which) return risen.save.get(key(name) .. "." .. which) end

local function look()
    for _, name in ipairs(order) do
        local d = defs[name]
        if d.enable_when and not fired(name, "on") then
            if ev.condition_holds(d.enable_when) then
                risen.save.set(key(name) .. ".on", true)
                L.set(name, true)
            end
        end
        if d.disable_when and not fired(name, "off") then
            if ev.condition_holds(d.disable_when) then
                risen.save.set(key(name) .. ".off", true)
                L.set(name, false)
            end
        end
    end
end

function L.install(list, events)
    ev = events
    for _, d in ipairs(list or {}) do
        defs[d.name] = d
        order[#order + 1] = d.name
    end
    local any = false
    for _, d in pairs(defs) do any = any or d.enable_when ~= nil or d.disable_when ~= nil end
    if any then
        risen.on("tick", function()
            frame = frame + 1
            if frame % 30 == 0 and risen.entity.player() then look() end
        end)
    end
end

return L
