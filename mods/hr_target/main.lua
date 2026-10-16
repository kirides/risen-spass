-- Test mod of M15 "hot reload" (scenario `hotreload`, scripts/smoke-test.ps1; the readable guide is docs/MODDING_GUIDE.md "Hot reload"), version 1.
-- It registers one of every kind of thing and tells the driver mod about every call through mod events, tagged with its version, so the scenario can see
-- that after a reload only the new version answers and nothing of the old one is left behind.
local V = require("version")
local carried = mod.reload and mod.reload.state or nil
local instance = (carried and carried.instance or 0) + 1
local ticks = carried and carried.ticks or 0
local function report(kind) risen.emit("hr.hit", kind, V.tag, instance) end

-- ---- event handlers, and the state that is carried over to the next instance ------------------------------------------------------------------------------
risen.on("hr.who", function(list)
    list[#list + 1] = { who = "target", tag = V.tag, instance = instance, ticks = ticks, carried_reason = carried and carried.reason, from_version = mod.reload and mod.reload.from_version,
        runs = risen.save.get("runs"), migrated_from = risen.save.get("migrated_from"), runs_x100 = risen.save.get("runs_x100") }
end)
risen.on("tick", function() ticks = ticks + 1 end)
risen.on_unload(function(reason) return { ticks = ticks, instance = instance, reason = reason } end)

-- the mod's savegame data: how often main.lua ran (a reload must not lose it)
risen.save.set("runs", (risen.save.get("runs") or 0) + 1)
-- a new game starts without mod data: count the session too
risen.on("session.start", function() risen.save.set("runs", (risen.save.get("runs") or 0) + 1) end)

-- ---- scripts: a defined one and an override of a stock script -------------------------------------------------------------------------------------------------
risen.script.define("HR_Script", function() report("script") return V.script end)
risen.script.override("GiveXP", function(ctx) report("override") return ctx:call_original() end)

-- ---- AI: a state and a perception handler for every NPC ----------------------------------------------------------------------------------------------------------
risen.ai.state("ZS_HR_Mark", {
    interval = 0.5,
    task = "OnRoutine",                      -- the stock task callback: sweeps AssessActive for the NPC about once a second
    begin = function(ctx) report("state_begin") end,
    loop = function(ctx) report("state_loop") end,
    finish = function(ctx) report("state_finish") end,
})
risen.ai.perceive("notice", function(ctx) report("perceive") return nil end, { all = true })

-- ---- a damage handler -------------------------------------------------------------------------------------------------------------------------------------------------------------
risen.combat.on_damage(function(hit) report("damage") return nil end)

-- ---- content: balance, two persistent NPCs ----------------------------------------------------------------------------------------------------------------------------
risen.balance.install({
    items = { It_Apple = { value = V.apple } },
    new_npcs = {
        Spass_HR_Guy = { base = "Peasant_1_01", name = "Hot Guy", level = 3, hp_max = 200, guild = "Cit" },
        Spass_HR_Old = { base = "Peasant_1_02", name = "Old Hot Guy", level = 3, hp_max = 200, guild = "Cit" },
    },
})
-- engine-owned content (a quest and a dialogue option): made in `define`, which runs on load.before / session.start; a reload cannot change them
local function define_content()
    risen.quest.define({ name = "Q_HR_Hot", title = "Hot quest (v1)", type = "Report", xp = 1 })
    risen.dialog.create({ name = "HR_Hot_Info", owner = "HR_Guy", lines = { { speaker = "player", listener = "npc", text = "Hello (v1)." } } })
end
risen.npc.persistent({ name = "HR_Guy", template = "Spass_HR_Guy", offset = { 220, 20, 0 }, peaceful = true, label = "Hot Guy", define = define_content })
risen.npc.persistent({ name = "HR_Old", template = "Spass_HR_Old", offset = { -220, 20, 0 }, peaceful = true, label = "Old Hot Guy" })
