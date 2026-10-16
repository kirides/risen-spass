-- Live test mod of M10 "NPC routines" (scenario `routines`, scripts/smoke-test.ps1; the readable example is mods/example_routines).
--
-- The mod declares one persistent NPC, RT_Gate, whose declaration carries a `routine`: it stands guard at a post 250 units from its home. The scenario
-- (test_routines.lua) makes the other NPCs itself (a guard, a day/night cycle, a patrol) with risen.npc.set_routine.
-- The phase (risen.test param "phase") changes what this process declares:
--   save, load       RT_Gate guards a post facing south (yaw 180)
--   load_changed     RT_Gate's routine changed: it looks at a point 250 units away instead (the old post has to go)
local phase = risen.test.param("phase", "save")

local routine
if phase == "load_changed" then
    routine = { at = { dx = 0, dy = 0, dz = 250, kind = "lookat" } }
else
    routine = { at = { dx = 250, dy = 0, dz = 0, kind = "guard", yaw = 180 } }
end

risen.npc.persistent({
    name = "RT_Gate", template = "Peasant_1_02", offset = { 650, 20, 650 }, label = "Gate Keeper", peaceful = true,
    routine = routine,
})

if risen.test.is_active() then
    require("test_routines")(phase)
end
