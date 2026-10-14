-- Smoke-test mod. Runs inside the per-mod sandbox (see docs/LUA_API.md).
risen.log.info("hello from " .. mod.id .. " " .. mod.version .. " (risen-spass " .. risen.version .. ")")

risen.on("mods.loaded", function()
    risen.log.info("all mods loaded")
end)
