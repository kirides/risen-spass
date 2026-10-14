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

-- Entities spawned from Lua are not saved: spawn them again when a game starts or a savegame was loaded, if they are missing.
local function ensure_entities()
    -- if not risen.entity.get("MyMod_Bob") then risen.npc.spawn("Peasant_1_01", x, y, z, { name = "MyMod_Bob" }) end
end

risen.on("load.before", define_content)
risen.on("session.start", function(mode)
    define_content()
    ensure_entities()
end)
risen.on("load.after", function(name, ok)
    if ok then ensure_entities() end
end)

-- ---- your own data in the savegame -----------------------------------------------------------------------------------------
-- risen.save.set("visits", risen.save.get("visits", 0) + 1)   -- restored when the savegame is loaded

-- ---- texts -----------------------------------------------------------------------------------------------------------------
-- Put them into strings/all.csv or strings/<Language>.csv (see mods/stringsdemo) or call risen.loc.set(id, text).
