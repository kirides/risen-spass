#pragma once

// The read-only inspection commands of the live link ("inspect.state", "inspect.nearby", "inspect.entity", "inspect.props", "inspect.inventory",
// "inspect.quests", "inspect.gamevars", "inspect.actors"): the editor's Live panel asks for them. Run with the shared `risen` table as the chunk argument; every
// function takes the request's `args` table and returns a JSON-able result or `nil, reason`. The `inspect.*` ones change nothing in the game; the few
// write commands of the editor live in `M.edit` ("edit.move").

namespace spass::game {

inline constexpr const char* kLinkInspectLua = R"LUA(
local risen = ...
local M = {}

-- pcall wrapper: the value or nil
local function try(f, ...)
    local ok, a, b = pcall(f, ...)
    if ok then return a, b end
end

-- only plain values go over the link
local function prim(v)
    local t = type(v)
    if t == "number" or t == "string" or t == "boolean" then return v end
    if v ~= nil then return tostring(v) end
end

local function clip(s, n)
    s = prim(s)
    if type(s) == "string" and #s > n then return s:sub(1, n) .. "..." end
    return s
end

local function template_name(e)
    local t = try(e.template, e)
    local n = t and try(t.name, t)
    return prim(n) or prim(try(e.name, e))
end

local function brief(e, center)
    local x, y, z = try(e.position, e)
    local t = {
        name = prim(try(e.name, e)),
        label = prim(try(e.display_name, e)),
        template = template_name(e),
        id = prim(try(e.id, e)),
        dead = try(e.is_dead, e) == true,
        npc = try(e.is_npc, e) == true,
        player = try(e.is_player, e) == true,
        hp = prim(try(e.hp, e)), hp_max = prim(try(e.hp_max, e)),
        mana = prim(try(e.mana, e)), mana_max = prim(try(e.mana_max, e)),
        level = prim(try(e.level, e)),
        guild = prim(try(e.guild, e)),
        group = prim(try(e.group, e)),
        state = prim(try(e.state, e)),
        x = prim(x), y = prim(y), z = prim(z),
    }
    if center then t.distance = prim(try(e.distance_to, e, center)) end
    return t
end

local function find(args)
    if type(args.id) == "string" and args.id ~= "" then
        local e = try(risen.entity.by_id, args.id)
        if e then return e end
    end
    if type(args.name) == "string" and args.name ~= "" then
        return try(risen.entity.get, args.name)
    end
end

local SETS = { "gCNPC_PS", "gCInventory_PS", "gCSkills_PS", "gCItem_PS", "gCScriptRoutine_PS", "gCCharacterControl_PS", "gCInteraction_PS" }

-- the world and the player
function M.state(args)
    local player = try(risen.entity.player)
    local out = { running = try(risen.world.is_game_running) == true, world = prim(try(risen.world.name)), chapter = prim(try(risen.quest.chapter)) }
    local hour, minute = try(risen.world.time)
    out.hour, out.minute = prim(hour), prim(minute)
    if player then out.player = brief(player) end
    return out
end

-- NPCs and monsters around the player (or around args.x / y / z when given: not supported, the player is the center)
function M.nearby(args)
    local player = try(risen.entity.player)
    if not player then return nil, "there is no player (no game running)" end
    local radius = tonumber(args.radius) or 2500
    if radius < 100 then radius = 100 elseif radius > 200000 then radius = 200000 end
    local limit = math.floor(tonumber(args.limit) or 80)
    if limit < 1 then limit = 1 elseif limit > 300 then limit = 300 end
    local list = try(risen.entity.npcs, player, radius) or {}
    local out = {}
    for _, e in ipairs(list) do
        if not (try(e.is_player, e) == true) then out[#out + 1] = brief(e, player) end
    end
    table.sort(out, function(a, b) return (a.distance or 1e12) < (b.distance or 1e12) end)
    while #out > limit do out[#out] = nil end
    return { center = brief(player), radius = radius, entities = out }
end

-- the live mirror of the editor's World view (E35): the player and the NPCs around, only what is needed to draw them (id, template, pose, dead); polled several times a second
function M.actors(args)
    local player = try(risen.entity.player)
    if not player then return nil, "there is no player (no game running)" end
    local radius = tonumber(args.radius) or 6000
    if radius < 100 then radius = 100 elseif radius > 200000 then radius = 200000 end
    local limit = math.floor(tonumber(args.limit) or 150)
    if limit < 1 then limit = 1 elseif limit > 400 then limit = 400 end
    local function row(e, is_player)
        local p = try(e.pose, e)
        if not p then return nil end
        return { id = prim(try(e.id, e)), name = prim(try(e.name, e)), template = template_name(e), x = p.x, y = p.y, z = p.z, yaw = p.yaw,
                 dead = try(e.is_dead, e) == true, player = is_player or nil }
    end
    local out = {}
    local me = row(player, true)
    if me then out[1] = me end
    local list = try(risen.entity.npcs, player, radius) or {}
    for _, e in ipairs(list) do
        if #out >= limit then break end
        if not (try(e.is_player, e) == true) then
            local r = row(e, false)
            if r then out[#out + 1] = r end
        end
    end
    return { world = prim(try(risen.world.name)), time = prim(try(risen.world.time)), actors = out }
end

-- ---- the editor's write commands ("edit.*", stage E35b): kept apart from the read-only ones above, narrow on purpose --------------------------------------
M.edit = {}

local function finite(v, limit)
    return type(v) == "number" and v == v and v > -limit and v < limit
end

-- edit.move { id, x, y, z, yaw? }: puts an NPC or monster of the running game at a new place (a teleport like `Entity:set_pose`; what the editor's gizmo does to a
-- placed NPC is shown in the game at once). Never the player; the game keeps no record of it (a load brings the saved place back).
function M.edit.move(args)
    local e = type(args.id) == "string" and try(risen.entity.by_id, args.id) or nil
    if not e then return nil, "no such entity" end
    if try(e.is_player, e) == true then return nil, "the player cannot be moved from the editor" end
    if try(e.is_npc, e) ~= true then return nil, "only NPCs and monsters can be moved" end
    if not (finite(args.x, 1e6) and finite(args.y, 1e6) and finite(args.z, 1e6)) then return nil, "x, y and z must be numbers" end
    local spec = { x = args.x, y = args.y, z = args.z }
    if args.yaw ~= nil then
        if not finite(args.yaw, 1e6) then return nil, "yaw must be a number" end
        spec.yaw = args.yaw
    end
    local ok, reason = e:set_pose(spec)
    if not ok then return nil, reason or "the engine refused the move" end
    return { id = args.id }
end

-- one entity: the summary and which property sets it has
function M.entity(args)
    local e = find(args)
    if not e then return nil, "no such entity" end
    local out = brief(e, try(risen.entity.player))
    out.sets = {}
    for _, set in ipairs(SETS) do
        local list = try(e.list_properties, e, set)
        if type(list) == "table" then out.sets[#out.sets + 1] = { name = set, count = #list } end
    end
    out.stacks = prim(try(e.stack_count, e))
    return out
end

-- the properties of one set with their current values (as the engine's text)
function M.props(args)
    local e = find(args)
    if not e then return nil, "no such entity" end
    if type(args.set) ~= "string" then return nil, "args.set is the property set (gCNPC_PS, ...)" end
    local list = try(e.list_properties, e, args.set)
    if type(list) ~= "table" then return nil, "the entity has no " .. args.set end
    local out = {}
    for i, p in ipairs(list) do
        if i > 400 then break end
        out[#out + 1] = { name = prim(p.name), type = prim(p.type), value = clip(try(e.get_property, e, args.set, p.name), 200) }
    end
    return { set = args.set, properties = out }
end

-- the inventory stacks (template and amount)
function M.inventory(args)
    local e = find(args)
    if not e then return nil, "no such entity" end
    local n = try(e.stack_count, e) or 0
    local out = {}
    for i = 0, math.min(n, 200) - 1 do
        local item = try(e.stack_item, e, i)
        if item then
            out[#out + 1] = { template = prim(try(item.name, item)), amount = prim(try(item.item_amount, item)), value = prim(try(item.item_value, item)), category = prim(try(item.item_category, item)) }
        end
    end
    return { count = n, stacks = out, gold = prim(try(risen.item.money, e)) }
end

-- the state of the named quests
function M.quests(args)
    local out = {}
    for i, name in ipairs(type(args.names) == "table" and args.names or {}) do
        if i > 200 then break end
        if type(name) == "string" then
            local q = try(risen.quest.get, name)
            out[#out + 1] = {
                name = name, exists = q ~= nil,
                running = try(risen.quest.is_running, name) == true,
                status = q and prim(try(q.get_property, q, "Status")) or nil,
                log = q and prim(try(q.log_count, q)) or nil,
            }
        end
    end
    return { quests = out }
end

-- the values of the named game variables
function M.gamevars(args)
    local out = {}
    for i, name in ipairs(type(args.names) == "table" and args.names or {}) do
        if i > 300 then break end
        if type(name) == "string" then
            local exists = try(risen.gamevar.exists, name) == true
            out[#out + 1] = { name = name, exists = exists, value = exists and prim(try(risen.gamevar.get, name)) or nil }
        end
    end
    return { vars = out }
end

return M
)LUA";

}  // namespace spass::game
