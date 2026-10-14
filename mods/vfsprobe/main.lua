-- Development aid: crawls the engine's virtual file system and logs how many files each directory holds, plus a few sample
-- names (this is how docs/RE.md §16 was written). Run: scripts/test-run.ps1 -Mods vfsprobe -NoAutostart
local log = risen.log
local ROOT, MAX_DEPTH = "data", 3

local function crawl(dir, depth)
    local files = risen.vfs.find(dir, "*") or {}
    local sample = {}
    for i = 1, math.min(#files, 4) do sample[#sample + 1] = files[i] end
    log.info(("vfsprobe %-48s files=%-5d %s"):format(dir, #files, table.concat(sample, " | ")))
    if depth >= MAX_DEPTH then return end
    for _, sub in ipairs(risen.vfs.dirs(dir) or {}) do
        crawl(dir .. "/" .. sub, depth + 1)
    end
end

local done = false
risen.on("tick", function()
    if done then return end
    done = true
    crawl(ROOT, 0)
end)
