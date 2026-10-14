-- Explores the engine's virtual file system (development aid): crawls the directory tree and logs how many files each
-- directory holds, plus a few sample names. Run it with: scripts/test-run.ps1 -Mods vfsprobe
local log = risen.log
local MAX_DEPTH = 3

local function crawl(dir, depth)
    local files = risen.vfs.find(dir, "*") or {}
    local sample = {}
    for i = 1, math.min(#files, 4) do sample[#sample + 1] = files[i] end
    log.info(("vfsprobe %-40s files=%-5d %s"):format(dir, #files, table.concat(sample, " | ")))
    if depth >= MAX_DEPTH then return end
    for _, sub in ipairs(risen.vfs.dirs(dir) or {}) do
        crawl(dir .. "/" .. sub, depth + 1)
    end
end

local done = false
risen.on("tick", function()
    if done then return end
    done = true
    crawl("data", 0)
end)
