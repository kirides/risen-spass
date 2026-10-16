-- Mod template: copy this folder (without the leading underscore, which disables a mod), change `id` in mod.toml and start here.
-- API reference: docs/LUA_API.md; editor completion: types/README.md.

local log = risen.log

log.info(("%s %s loaded"):format(mod.name, mod.version))

-- ---- content that must survive saving and loading ------------------------------------------------------------------------
-- Quests and dialogue options have to exist before the engine reads a savegame (load.before) and again after a new game
-- (session.start). risen.quest.define and risen.dialog.create are idempotent, so one function serves both events.
local function define_content()
    -- risen.quest.define({ name = "Q_MyMod_Intro", title = "Hello World", type = "Report" })
end

-- NPCs: declare them once. The engine saves spawned entities (but not their unique names); risen.npc.persistent spawns the NPC in a new game, finds and names it
-- again after every load and calls `define` on load.before and every session.start. Do NOT "spawn it again if risen.entity.get(name) is nil": that duplicates it.
-- risen.npc.persistent({ name = "MyMod_Bob", template = "Peasant_1_01", offset = { 150, 20, 150 }, label = "Bob", peaceful = true, define = define_content })

risen.on("load.before", define_content)
risen.on("session.start", function(mode)
    define_content()
end)

-- ---- your own data in the savegame -----------------------------------------------------------------------------------------
-- risen.save.set("visits", risen.save.get("visits", 0) + 1)   -- restored when the savegame is loaded

-- ---- texts -----------------------------------------------------------------------------------------------------------------
-- Put them into strings/all.csv or strings/<Language>.csv (see mods/stringsdemo) or call risen.loc.set(id, text).
