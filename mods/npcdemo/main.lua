-- Spawns "SPASS_Bob", a peasant with custom stats, name label and inventory, ~5 s after a game session starts.
local log = risen.log

-- The label shown when the player focuses an NPC is looked up by id "FO_<entity name>" (verified with
-- risen.loc.trace: focusing the NPC makes the engine ask for FO_SPASS_Bob).
risen.loc.set("FO_SPASS_Bob", "Bob the Lua Peasant")

local frames = 0
risen.on("tick", function()
    frames = frames + 1
    if frames ~= 300 then return end

    -- Entities spawned at runtime may be part of a savegame; do not create a second Bob after loading one.
    if risen.entity.get("SPASS_Bob") then
        log.info("npcdemo: SPASS_Bob already exists, not spawning")
        return
    end

    local player = risen.entity.player()
    if not player then return end
    local x, y, z = player:position()
    local bob = risen.npc.spawn("Peasant_1_01", x + 150, y + 20, z + 150, {
        name = "SPASS_Bob",
        level = 10,
        hp_max = 400,
        group = "SPASS",
        items = { { "It_1H_Machete", 1 } },
        equip = true,
    })
    if bob then
        log.info(("npcdemo: spawned %s guild=%s level=%s hp=%s/%s"):format(bob:name(), tostring(bob:guild()),
            tostring(bob:level()), tostring(bob:hp()), tostring(bob:hp_max())))
    end
end)
