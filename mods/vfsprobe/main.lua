local log = risen.log
local done = false
risen.on("tick", function()
    if done then return end
    done = true
    log.info("vfsprobe missing symbols: " .. table.concat(risen.engine.missing(), ", "))
    for name, m in pairs(risen.engine.build()) do log.info(("vfsprobe build %s ts=%d base=%s"):format(name, m.timestamp, m.base)) end
end)
