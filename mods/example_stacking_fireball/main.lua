-- EXAMPLE 17: custom damage -- a Fireball that does more damage the more often it has hit the same target.
--
-- What you learn
--   * risen.combat.on_damage(handler, filter) sees every hit before the engine applies it: who hit whom, with what (`hit.weapon` is the projectile's template, `hit.spell` says "fireball"),
--     the damage that is about to go in (`hit.damage`) and returns what to do: nothing (nil) leaves it, a number replaces it, false (or 0) cancels the hit.
--   * The number you return is the damage BEFORE the engine subtracts the victim's protection (`hit.protection`) and applies its minimum, so protection, the Fireball skill bonus (+25 per level
--     is already in `hit.damage`) and the AREA FALLOFF (`hit.multiplier` < 1 at the edge of the blast, already in `hit.damage`) keep working: multiply, do not replace by a constant.
--   * A counter per victim lives in risen.save (written next to every savegame, restored by a load, empty in a new game), so it continues after a save and a load in a fresh process.
--   * The filter ({ attacker = "player", spell = "fireball" }) keeps the handler out of everything else: monsters' bites, the player's sword, a mage's fireball.
--
-- The rules (change the constants):
--   * every hit of the player's Fireball counts for the victim it hits, also the weak hits at the edge of the blast (their bonus is as weak as their damage);
--   * the damage is the stock damage times (1 + STEP * earlier hits), at most MAX_STACKS earlier hits (STEP 0.5: 100%, 150%, 200% ... 450%);
--   * the counter of a victim is forgotten when it dies or is knocked out (`entity.died`, `entity.defeated`), and a new game starts with no counters. It does NOT run out with time;
--   * counters are keyed by the unique name of the victim (every NPC of the world has one).
--
-- Run the live check (about 100 s; two game processes, see docs/EXAMPLES.md):
--   .\scripts\smoke-test.ps1 -Scenario ex_combat

local STEP = 0.5
local MAX_STACKS = 8

local function stacks() return risen.save.get("stacks", {}) end

-- the handler: `hit.damage` is what the engine is about to apply (before protection), we return the new number
local function on_fireball_hit(hit)
    local name = hit.victim:name()
    local all = stacks()
    local n = all[name] or 0
    all[name] = n + 1
    risen.save.set("stacks", all)
    local bonus = math.min(n, MAX_STACKS)
    if bonus == 0 then return nil end                     -- the first hit is the stock hit
    return hit.damage * (1 + STEP * bonus)
end

local handle = assert(risen.combat.on_damage(on_fireball_hit, { attacker = "player", spell = "fireball" }))

-- a victim that died or was knocked out starts again from zero
local function forget(entity)
    local all = stacks()
    local name = entity:name()
    if all[name] then
        all[name] = nil
        risen.save.set("stacks", all)
    end
end
risen.on("entity.died", forget)
risen.on("entity.defeated", forget)

-- a new game starts with no counters (a load brings back the ones of the savegame by itself)
risen.on("session.start", function(mode)
    if mode == "new" then risen.save.set("stacks", {}) end
end)

if risen.test.is_active() then
    require("test_ex_combat")({ step = STEP, max = MAX_STACKS, handle = handle })
end
