-- EXAMPLE 22: Random Encounters / live respawn -- the world is not empty behind the player's back. Table driven: the data is in encounters.lua, the options in config.toml.
--
-- What you learn
--   * a data-driven mod: `encounters.lua` (pools, zones, categories) is plain Lua data loaded with require; this file is only the engine. Change the tables, reload (`risen.mods.reload`)
--   * risen.config: scalars with defaults (config.toml next to the mod overrides them, the player's options menu choices on top)
--   * play-time cooldowns that do not run while a menu pauses the world (a counter that grows by the time between two `tick`s, capped)
--   * chapter dependent weighted pools: `risen.quest.chapter()` + `risen.random.weighted`
--   * risen.world.points + risen.world.is_seen_by (camera): spawn only where the player cannot see it (see example_unseen_spawn)
--   * counting "our" monsters near a spot with risen.entity.npcs, and clean-up of far-away leftovers with Entity:kill (live respawn: the world refills while you move on)
--
-- Run the live check (about a minute):
--   .\scripts\test-run.ps1 -Instance 1 -Mods example_random_encounters -WaitForResult -Seconds 300 -Scenario ex_random_encounters -ShowChecks

local M, W, R = risen.math, risen.world, risen.random

local cfg = risen.config({
    enabled = true,
    chance_scale = 1.0,     -- multiplies every zone's chance (2 = twice as often, 0 = never)
    cooldown_scale = 1.0,   -- multiplies every category's cooldown (0.5 = half as long)
    debug = false,          -- log every attempt
})

local data = require("encounters")
local habitats = require("habitats")    -- GENERATED: where the game itself places each monster (tools/gen_habitats.py)
local S = data.settings

local Enc = { habitats = habitats, zones = data.zones, pools = data.pools, categories = data.categories, settings = S, cfg = cfg }

local spawned = {}          -- the monsters this mod made
local serial = 0            -- numbers their unique names: Entity:valid() looks an entity up BY NAME, an unnamed spawn is called like its template and would match any other wolf
local ready_at = {}         -- category -> play time at which it may spawn again
local play_time = 0         -- seconds of play (grows only while the world runs)
local last_clock
local cleanup_at = 0
local sweep_at = 0

-- every template that is part of a pool counts as "ours / hostile" when monsters are counted near a spot
local hostile = {}
for _, pool in pairs(data.pools) do
    for _, e in ipairs(pool) do hostile[e.template] = true end
end

local function debug_log(...) if cfg.debug then risen.log.info("[encounters] " .. string.format(...)) end end

local function in_range(range, value)
    if not range then return true end
    return value >= (range[1] or -math.huge) and value <= (range[2] or math.huge)
end

local function chapter() return risen.quest.chapter() or 1 end

local function alive_list()
    local out = {}
    for _, e in ipairs(spawned) do
        if e:valid() and not e:is_dead() then out[#out + 1] = e end
    end
    spawned = out
    return out
end

-- The entry of a pool to spawn in a chapter (weighted), or nil when none fits.
function Enc.pick_entry(pool_name, ch)
    local pool = data.pools[pool_name]
    if not pool then return nil, "no pool " .. tostring(pool_name) end
    local items, weights = {}, {}
    for _, e in ipairs(pool) do
        if in_range(e.chapters, ch) then
            items[#items + 1] = e
            weights[#weights + 1] = e.weight or 1
        end
    end
    if #items == 0 then return nil, "pool " .. pool_name .. " is empty in chapter " .. ch end
    return (R.weighted(items, weights))
end

-- Every entry of the pool that fits the chapter, in a random order that follows the weights (the first is what pick_entry would give).
function Enc.ordered_entries(pool_name, ch)
    local pool = data.pools[pool_name]
    local items, weights, out = {}, {}, {}
    for _, e in ipairs(pool or {}) do
        if in_range(e.chapters, ch) then items[#items + 1] = e; weights[#weights + 1] = e.weight or 1 end
    end
    while #items > 0 do
        local e, i = R.weighted(items, weights)
        if not e or type(i) ~= "number" then break end
        out[#out + 1] = e
        table.remove(items, i)
        table.remove(weights, i)
    end
    return out
end

-- Where the game itself places a species (habitats.lua), usable in this chapter, optionally only in some of its outdoor regions (a list of names like "Swamp_Outdoor").
function Enc.habitat_points(template, ch, regions)
    local want
    if regions then want = {}; for _, r in ipairs(regions) do want[r] = true end end
    local out = {}
    for _, h in ipairs(habitats[template] or {}) do
        if (h[5] or 1) <= ch and (not want or want[h[4]]) then out[#out + 1] = { x = h[1], y = h[2], z = h[3], region = h[4] } end
    end
    return out
end

local function count_of(entry)
    local c = entry.count or 1
    if type(c) == "table" then return R.int(c[1], c[2]) end
    return c
end

-- Our monsters (hostile templates, alive) within `radius` of a position: the game's own wolves count as well.
local function monsters_near(p, radius, npcs)
    local n = 0
    for _, e in ipairs(npcs) do
        if not e:is_player() and not e:is_dead() then
            local t = e:template()
            if t and hostile[t:name()] then
                local x, y, z = e:position()
                local d = x and M.distance2d({ x = x, y = y, z = z }, p)
                if d and d <= radius then n = n + 1 end
            end
        end
    end
    return n
end

-- The spots of a zone that qualify: in distance range, hidden from the player. Returns the list sorted nearest first, or nil and a reason.
function Enc.spots(zone, player, min_distance, given)
    local max_d = zone.max_distance or 3000
    local min_d = min_distance or zone.min_distance or 600
    local points = given or zone.spots
    if given then
        -- a ready list of { x, y, z } (the habitat of one species)
    elseif points then
        local list = {}
        for _, s in ipairs(points) do list[#list + 1] = { x = s.x or s[1], y = s.y or s[2], z = s.z or s[3] } end
        points = list
    else
        local f = zone.points or {}
        points = W.points({ near = player, radius = max_d, limit = 300, kind = f.kind, template = f.template, name_prefix = f.name_prefix }) or {}
    end
    local out = {}
    for _, p in ipairs(points) do
        if p.kind ~= "helper" then
            local d = M.distance2d(p, player)
            if d and d >= min_d and d <= max_d then
                local seen, why = W.is_seen_by({ p.x, p.y + 100, p.z }, player, { camera = true, range = S.sight })
                if seen == nil then return nil, why end
                if not seen then out[#out + 1] = { x = p.x, y = p.y, z = p.z, d = d, region = p.region } end
            end
        end
    end
    table.sort(out, function(a, b) return a.d < b.d end)
    return out
end

-- Spawns the pool's pick at (or around) a spot. Returns the list of entities, or nil and a reason.
function Enc.spawn(zone, spot, ch, entry)
    local why
    if not entry then entry, why = Enc.pick_entry(zone.pool, ch) end
    if not entry then return nil, why end
    local made = {}
    for i = 1, count_of(entry) do
        local at = spot
        if i > 1 then at = M.polar(spot, R.float(0, 360) or 0, R.float(80, 220) or 150); at.y = spot.y end
        local opts = {}
        for k, v in pairs(entry.opts or {}) do opts[k] = v end
        serial = serial + 1
        opts.name = ("ENC_%s_%d"):format(entry.template, serial)
        local e = risen.npc.spawn(entry.template, at.x, spot.y + 5, at.z, opts)
        if e then
            spawned[#spawned + 1] = e
            made[#made + 1] = e
        end
    end
    if #made == 0 then return nil, "could not spawn " .. entry.template end
    return made, entry.template
end

-- One try of one zone. Returns the entities made, or nil and a reason.
function Enc.try_zone(zone, player, ch, npcs, forced_surprise)
    if zone.chapters and not in_range(zone.chapters, ch) then return nil, "chapter" end
    if zone.area then
        local px, py, pz = player:position()
        local d = M.distance2d({ x = px, y = py, z = pz }, { x = zone.area.x, y = py, z = zone.area.z })
        if not d or d > zone.area.radius then return nil, "outside area" end
    end
    local cat = zone.category or "small"
    if (ready_at[cat] or 0) > play_time then return nil, "cooldown" end
    if #alive_list() >= S.max_alive then return nil, "max_alive" end
    if not R.chance(math.min(1, (zone.chance or 0.2) * cfg.chance_scale)) then return nil, "chance" end

    local surprise = forced_surprise
    if surprise == nil then surprise = S.surprise_chance > 0 and R.chance(S.surprise_chance) end
    local min_d = surprise and S.surprise_min_distance or nil
    -- spots that already have their share of monsters around them are no use
    local function usable_of(list)
        local usable = {}
        for _, s in ipairs(list) do
            if monsters_near(s, zone.near_radius or 1500, npcs) < (zone.max_near or 2) then usable[#usable + 1] = s end
        end
        return usable
    end
    local usable, entry
    if zone.habitat then
        -- the monster is chosen first (weighted, chapter), then a hidden spot where the GAME places that species; a species with no such spot near the player is skipped
        for _, e in ipairs(Enc.ordered_entries(zone.pool, ch)) do
            local pts = Enc.habitat_points(e.template, ch, zone.regions)
            if #pts > 0 then
                local list, why = Enc.spots(zone, player, min_d, pts)
                if not list then return nil, why end
                local u = usable_of(list)
                if #u > 0 then usable, entry = u, e break end
            end
        end
    else
        local list, why = Enc.spots(zone, player, min_d)
        if not list then return nil, why end
        usable = usable_of(list)
    end
    local cooldown = ((data.categories[cat] or {}).cooldown or 240) * cfg.cooldown_scale
    if not usable or #usable == 0 then
        ready_at[cat] = play_time + S.retry_after
        return nil, "no hidden spot"
    end
    local spot = surprise and usable[1] or R.choice(usable)   -- surprise: the nearest hidden spot, otherwise any
    if not spot then return nil, "no spot" end
    local made, what = Enc.spawn(zone, spot, ch, entry)
    if not made then return nil, what end
    ready_at[cat] = play_time + cooldown
    debug_log("%s: %s x%d at (%.0f, %.0f, %.0f), %.0f from the player%s", zone.name or zone.pool, what, #made, spot.x, spot.y, spot.z, spot.d, surprise and " (surprise)" or "")
    risen.emit("encounter.spawned", made, zone, spot, surprise and true or false)
    return made, spot
end

-- All zones once (the order of the table: earlier zones win a shared category).
function Enc.sweep()
    local player = risen.entity.player()
    if not player then return end
    local ch = chapter()
    local npcs = risen.entity.npcs()
    for _, zone in ipairs(data.zones) do
        local made, spot = Enc.try_zone(zone, player, ch, npcs)
        if made then npcs = risen.entity.npcs() end
        if made and type(spot) == "table" then
            -- Gothic: one spawn per sweep
            return made, spot
        end
    end
end

-- Live respawn: our monsters left far behind and out of sight are removed again.
function Enc.cleanup()
    if S.despawn_distance <= 0 then return 0 end
    local player = risen.entity.player()
    if not player then return 0 end
    local removed = 0
    for _, e in ipairs(alive_list()) do
        local d = e:distance_to(player)
        if d and d > S.despawn_distance then
            local x, y, z = e:position()
            local seen = W.is_seen_by({ x, y + 100, z }, player, { camera = true, range = S.sight })
            if seen == false and e:kill() then removed = removed + 1 end
        end
    end
    if removed > 0 then debug_log("cleanup: %d removed", removed) end
    return removed
end

function Enc.alive() return #alive_list() end
function Enc.reset() ready_at = {} end
function Enc.cooldown_left(cat) return math.max(0, (ready_at[cat] or 0) - play_time) end

local function active_here()
    if not cfg.enabled then return false end
    if S.world and W.name() ~= S.world then return false end
    return true
end

if not risen.test.is_active() then
    risen.on("tick", function()
        local c = risen.clock()
        play_time = play_time + math.min(1, c - (last_clock or c))   -- a menu pauses the world, not the clock: cap what one frame may add
        last_clock = c
        if play_time < sweep_at or not active_here() then return end
        sweep_at = play_time + S.interval
        local player = risen.entity.player()
        if not player or not (player:state() or ""):find(S.only_states) then return end
        Enc.sweep()
        if S.despawn_distance > 0 and play_time >= cleanup_at then
            cleanup_at = play_time + S.despawn_check
            Enc.cleanup()
        end
    end)
else
    require("test_example_random_encounters")(Enc)
end

return Enc
