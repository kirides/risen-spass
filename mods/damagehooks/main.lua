-- Live scenario `damagehooks` (docs/EXAMPLES.md "Testing"): risen.combat.on_damage against real fireball casts, wolf bites and club blows. Only active in a test run.
-- Phase `load` (a fresh process that loads the quick save of phase `save`): this mod registers its damage handler at start, like a real mod does.
if not risen.test.is_active() then return end
if risen.test.param("phase", "save") == "load" then
    risen.combat.on_damage(function(hit) return hit.damage * 2 end, { spell = "fireball" })
end
require("test_damagehooks")
