-- faulty: fails on purpose while it loads. The exported mod logs "[modkit_ext] lua.faulty: ..." and keeps everything else
-- (the item, tweaks.lua and its hooks); the scenario modkit_ext checks that.
-- The test framework counts error lines: this deliberate one is exempt (modkit_ext_driver switches the counting on again when it loads).
if risen.test then risen.test.allow_errors(true) end
error("deliberately broken: an error in hand-written Lua must not take the mod down")
