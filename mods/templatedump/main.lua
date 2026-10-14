-- Logs "TPL <group>/<Name>" for every entity template found in the packed data, and checks that
-- risen.template.exists agrees. scripts/test-run.ps1 -Mods templatedump, then grep "TPL " in spass.log.
local done = false
risen.on("tick", function()
    if done then return end
    done = true
    local root = "data/common/templates"
    local total, missing = 0, 0
    for _, path in ipairs(risen.vfs.walk(root, "*.tple")) do
        local group, name = path:sub(#root + 2):match("^(.*)/([^/]+)%.[^./]+$")
        if name then
            total = total + 1
            local known = risen.template.exists(name)
            if not known then missing = missing + 1 end
            risen.log.info("TPL " .. group .. "/" .. name .. (known and "" or " ?"))
        end
    end
    risen.log.info(("TPLSUMMARY total=%d unknown_to_engine=%d"):format(total, missing))
end)
