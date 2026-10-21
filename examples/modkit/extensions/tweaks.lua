-- tweaks: hand-written Lua of examples/modkit/extensions.rmk.json (the editor's Lua panel), exported as lua/tweaks.lua.
-- main.lua loads it before the editor's content is installed and calls the hooks it returns.

-- a script for the editor's Run script nodes (Events) and dialogue Script commands; the scenario modkit_ext calls it
risen.script.define("MKX_Ext_Ping", function(ctx) return 42 end)   -- ctx.self, ctx.other, ctx.arg

return {
    -- the generated tables, before they are installed: double the value of every new item of the mod
    before_install = function(data)
        for _, item in pairs(data.balance.new_items or {}) do
            if item.value then item.value = item.value * 2 end
        end
    end,
    -- the content is installed: `runtime` is require("modkit.runtime")
    after_install = function(runtime)
        risen.script.define("MKX_Ext_After", function() return runtime.VERSION end)
    end,
}
