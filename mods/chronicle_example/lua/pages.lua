-- pages: the hand-written Lua of the Ashen Chronicle (examples/modkit/chronicle.rmk.json, the editor's Lua panel), exported as lua/pages.lua.
--
-- Maren writes the island's story page by page. What no panel of the editor makes lives here:
--   * the script MKC_WritePage: the Run script nodes of the event graphs "Ink delivered" (page 1) and "Camp falls" (page 2) call it;
--   * the Chronicle's last line, written when the main quest Q_MKC_Chronicle ends (the mod event modkit.quest_done);
--   * before_install(data): the Amulet of the Ashen Tide is worth 250 gold for every quest of the Chronicle (computed from the generated quest list,
--     so a quest added in the Quests panel raises it without touching this file);
--   * after_install(runtime): the script MKC_PagesWritten counts the Chronicle's quests that ended well, with the runtime's own condition builder.
-- The texts are the project's free string rows MKC_PAGE_WRITTEN and MKC_LAST_LINE (Strings panel): translated like every other text of the mod.
--
-- How main.lua runs it: this file's top-level code first (before the editor's content exists: define scripts and event handlers here), then
-- before_install(data), then the content and the event graphs are installed, then after_install(runtime). An error here is logged as
-- "[chronicle_example] lua.pages: ..." and skips this file only; the rest of the mod keeps working.

local MOD = "chronicle_example"

-- a free string row of the project in the player's language; "{page}" becomes the page number
local function text(id, page)
    local s = risen.loc.get(id)
    if not s or s == "" then s = id end
    return (s:gsub("{page}", tostring(page or "")))
end

-- Page n of the Chronicle (n = the Run script node's argument): a line in the HUD and the game variable mkc_pages, the highest page written so far
-- (a game variable is part of the savegame, so a load rolls it back together with the quest that wrote it).
risen.script.define("MKC_WritePage", function(ctx)
    local page = tonumber(ctx.arg) or 0
    if page > (risen.gamevar.get("mkc_pages") or 0) then risen.gamevar.set("mkc_pages", page) end
    risen.hud.log(text("MKC_PAGE_WRITTEN", page), 1)
    return page
end)

-- The last line: the main quest's flow ended with success (the Lord of the Tide is dead and the epilogue is told).
risen.on("modkit.quest_done", function(id, quest, result)
    if id ~= MOD or quest ~= "Q_MKC_Chronicle" or result ~= "success" then return end
    risen.hud.log(text("MKC_LAST_LINE"), 1)
    risen.gamevar.set("mkc_last_line", 1)
end)

local chronicle_quests = {}   -- the names of the mod's quests, read from the generated data

return {
    -- the generated tables, before they are installed (data.balance is the risen.balance.install spec, data.quests the quest list, ...)
    before_install = function(data)
        for _, q in ipairs(data.quests or {}) do chronicle_quests[#chronicle_quests + 1] = q.name end
        local amulet = data.balance and data.balance.new_items and data.balance.new_items.MKC_Tide_Amulet
        if amulet then amulet.value = 250 * #chronicle_quests end
    end,
    -- the content is installed: `runtime` is require("modkit.runtime"); runtime.cond turns the editor's condition data into a risen.cond condition
    after_install = function(runtime)
        risen.script.define("MKC_PagesWritten", function()
            local n = 0
            for _, q in ipairs(chronicle_quests) do
                if risen.cond.test(runtime.cond({ "quest", q, "Success" })) then n = n + 1 end
            end
            return n
        end)
    end,
}
