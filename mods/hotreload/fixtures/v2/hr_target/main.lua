-- version 1.1.0 of the hot-reload test mod: no override of GiveXP, no damage handler, a changed AI state, HR_Old is gone, HR_Guy2 is new, a migration for old data
local V = require("version")
local carried = mod.reload and mod.reload.state or nil
local instance = (carried and carried.instance or 0) + 1
local ticks = carried and carried.ticks or 0
local function report(kind) risen.emit("hr.hit", kind, V.tag, instance) end

risen.on("hr.who", function(list)
    list[#list + 1] = { who = "target", tag = V.tag, instance = instance, ticks = ticks, carried_reason = carried and carried.reason, from_version = mod.reload and mod.reload.from_version,
        runs = risen.save.get("runs"), migrated_from = risen.save.get("migrated_from"), runs_x100 = risen.save.get("runs_x100") }
end)
risen.on("tick", function() ticks = ticks + 1 end)
risen.on_unload(function(reason) return { ticks = ticks, instance = instance, reason = reason } end)

risen.save.set("runs", (risen.save.get("runs") or 0) + 1)

-- data written by 1.0.x gets a new key derived from the old one (runs once per savegame / reload that crosses the version)
risen.migrate("<1.1.0", function(ctx)
    ctx.save.set("migrated_from", ctx.from)
    ctx.save.set("runs_x100", (ctx.save.get("runs") or 0) * 100)
end)

risen.script.define("HR_Script", function() report("script") return V.script end)

risen.ai.state("ZS_HR_Mark", {
    interval = 0.5,
    task = "OnRoutine",                      -- the stock task callback: sweeps AssessActive for the NPC about once a second
    begin = function(ctx) report("state_begin") end,
    loop = function(ctx) report("state_loop") end,
    finish = function(ctx) report("state_finish") end,
})
risen.ai.perceive("notice", function(ctx) report("perceive") return nil end, { all = true })

risen.balance.install({
    items = { It_Apple = { value = V.apple } },
    new_npcs = {
        Spass_HR_Guy = { base = "Peasant_1_01", name = "Hot Guy", level = 3, hp_max = 200, guild = "Cit" },
        Spass_HR_Guy2 = { base = "Peasant_1_03", name = "Second Hot Guy", level = 3, hp_max = 200, guild = "Cit" },
    },
})
-- engine-owned content (a quest and a dialogue option): made in `define`, which runs on load.before / session.start; a reload cannot change them
local function define_content()
    risen.quest.define({ name = "Q_HR_Hot", title = "Hot quest (v2)", type = "Report", xp = 1 })
    risen.dialog.create({ name = "HR_Hot_Info", owner = "HR_Guy", lines = { { speaker = "player", listener = "npc", text = "Hello (v2)." } } })
end
risen.npc.persistent({ name = "HR_Guy", template = "Spass_HR_Guy", offset = { 220, 20, 0 }, peaceful = true, label = "Hot Guy", define = define_content })
risen.npc.persistent({ name = "HR_Guy2", template = "Spass_HR_Guy2", offset = { 0, 20, 260 }, peaceful = true, label = "Second Hot Guy" })
