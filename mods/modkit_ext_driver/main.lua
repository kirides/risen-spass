-- Checks hand-written Lua in an exported risen-modkit project (mods/modkit_ext, exported from examples/modkit/extensions.rmk.json) in a running game:
--   .\scripts\test-run.ps1 -Mods modkit_ext,modkit_ext_driver -WaitForResult -Seconds 200 -Scenario modkit_ext
-- The project has two Lua files (lua/tweaks.lua with both hooks, lua/faulty.lua that raises an error) and the mod folder a custom.lua that raises
-- one too: the mod must load, its content must be there, and the hooks of the good file must have run.
local T = risen.test
-- lua/faulty.lua and custom.lua exempted their own deliberate error lines from the test's error count: count from here on again
T.allow_errors(false)

T.plan {
    "the mod loaded despite the failing files",
    "the export was applied without failures",
    "the new item exists",
    "before_install changed the generated data",
    "a script of a Lua file is callable",
    "after_install ran with the runtime",
    "custom.lua ran up to its error",
}

local applied
risen.on("modkit.applied", function(id, when, report)
    if id == "modkit_ext" then applied = report end
end)

T.run(function()
    local info = risen.mods.get("modkit_ext")
    T.expect("the mod loaded despite the failing files", info ~= nil and info.state == "loaded", info and (info.state .. " " .. tostring(info.reason)) or "no such mod")
    T.wait_event("session.start", 240, function(mode) return mode == "new" end)
    T.wait_until(function() return applied ~= nil end, 30, "modkit.applied")
    T.expect("the export was applied without failures", applied.ok and #(applied.failed or {}) == 0, table.concat(applied.failed or {}, "; "))
    T.expect("the new item exists", risen.template.exists("MKX_Ext_Blade"))
    local t = risen.template.get("MKX_Ext_Blade")
    -- the project says 100; tweaks.lua's before_install doubles the value of every new item
    T.eq("before_install changed the generated data", t and tonumber(t:get_property("gCItem_PS", "GoldValue")), 200)
    local player = assert(risen.entity.player(), "no player")
    T.eq("a script of a Lua file is callable", risen.script.call("MKX_Ext_Ping", player, player, 0), 42)
    local v = risen.script.call("MKX_Ext_After", player, player, 0)
    T.expect("after_install ran with the runtime", type(v) == "number" and v >= 13, tostring(v))
    T.eq("custom.lua ran up to its error", risen.script.call("MKX_Custom_Ran", player, player, 0), 1)
    T.finish()
end, "modkit_ext")
