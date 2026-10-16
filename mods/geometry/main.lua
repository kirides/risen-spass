-- Live test mod of M13 "Math, line of sight, randomness" (scenario `geometry`, scripts/smoke-test.ps1; the readable example is mods/example_unseen_spawn).
-- It only does something in a test run; the checks are in test_geometry.lua.
if risen.test.is_active() then
    require("test_geometry")()
end
