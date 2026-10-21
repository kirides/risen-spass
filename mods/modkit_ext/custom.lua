-- custom.lua of the exported mod modkit_ext: fails on purpose. The mod logs "[modkit_ext] custom: ..." and keeps the content and the
-- project's Lua files; the scenario modkit_ext (mods/modkit_ext_driver) checks that. A real mod's custom.lua is hand-written code like any other.
risen.script.define("MKX_Custom_Ran", function() return 1 end)   -- what ran before the error stays registered
-- the test framework counts error lines: this deliberate one is exempt (modkit_ext_driver switches the counting on again when it loads)
if risen.test then risen.test.allow_errors(true) end
error("deliberately broken: custom.lua must not take the mod down")
