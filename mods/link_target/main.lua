---@diagnostic disable: undefined-field
-- Fixture of the scenario `link`: commands the python client calls; the client also reloads this mod over the link.
risen.link.on("echo", function(args)
    local n = type(args.list) == "table" and #args.list or 0
    return { x = args.x, n = n, s = args.s, mod = mod.id }
end)
risen.link.on("fail", function() return nil, "boom" end)
risen.link.on("crash", function() error("oops") end)
risen.link.on("push", function()
    risen.link.send("hello", { n = 1, text = "hi" })
    return true
end)
