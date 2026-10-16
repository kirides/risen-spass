-- EXAMPLE 14: NPC routines -- a gatekeeper with a day, an evening and a night post, and a patrol.
--
-- What you learn
--   * `routine = { ... }` in risen.npc.persistent gives the NPC a daily routine. The layer applies it once and checks it on every load (the engine saves the points and the
--     NPC's references to them, so nothing is made twice). Positions with dx / dy / dz are relative to the NPC's HOME, so the same declaration works wherever it is placed.
--   * work / relax / sleep are the three times of day of the engine's own routine system: a human works from 6:00 to 20:00, relaxes from 20:00 and sleeps before 6:00.
--     `at = point` stands for all three (a post he never leaves).
--   * A point is a freepoint of a kind: "guard" (stand guard, hands on hips, faces `yaw`), "lookat" (think), "sleep" (lie down), "spectator", "dance", ... (risen.world.point_kinds).
--     Positions are moved onto the walkable ground; a position the NPC cannot reach is refused with a reason.
--   * `patrol = { waypoint, waypoint, ... }`: the NPC walks the waypoints one after the other and round again. The engine keeps him a while at each one (a guard: a minute or
--     two); risen.npc.apply_routine(npc) sends him on at once.
--   * The same calls work for any NPC at run time: risen.npc.set_routine(npc, spec), get_routine, clear_routine; risen.world.points() lists the game's own freepoints.
--
-- Run the live check (about two minutes; see docs/EXAMPLES.md):
--   .\scripts\test-run.ps1 -Instance 2 -Mods example_routines -WaitForResult -Seconds 400 -Scenario example_routines -ShowChecks

risen.npc.persistent({
    name = "EX_Gatekeeper", template = "Peasant_1_01", offset = { 250, 20, 350 }, label = "Gatekeeper", peaceful = true,
    routine = {
        work  = { dx = 350, dy = 0, dz = -100, kind = "guard", yaw = 90 },    -- by day: stands guard at the gate, facing east
        relax = { dx = 500, dy = 0, dz = 150, kind = "lookat" },              -- in the evening: stares into the fire
        sleep = { dx = -100, dy = 0, dz = 250, kind = "sleep" },              -- at night: sleeps on the ground
    },
})

risen.npc.persistent({
    name = "EX_Patrol", template = "Peasant_1_02", offset = { 600, 20, 100 }, label = "Night Watch", peaceful = true,
    routine = {
        patrol = {
            { dx = -400, dy = 0, dz = 0 },       -- the first waypoint
            { dx = -100, dy = 0, dz = 450 },
            { dx = 300, dy = 0, dz = -250 },
            name = "Camp", kind = "guard",       -- group name (shared by NPCs that patrol the same ring) and what the NPC does at each waypoint
        },
    },
})

if risen.test.is_active() then
    require("test_example_routines")()
end
