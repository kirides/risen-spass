-- Logs every lifecycle/gameplay event with its arguments; enable it to see when the engine fires what.
local EVENTS = {
    "session.start", "session.stop", "world.changed", "save.before", "save.after", "load.before", "load.after",
    "quest.status_changed", "npc.killed", "npc.defeated", "info.started", "info.finished", "item.picked_up",
    "world.ready", "inventory.added", "inventory.removed", "inventory.changed", "item.equipped", "item.unequipped", "item.consumed",
    "entity.died", "entity.defeated", "entity.damaged", "entity.spawned", "entity.destroyed",
}
for _, ev in ipairs(EVENTS) do
    risen.on(ev, function(...)
        local args = {}
        for i = 1, select("#", ...) do args[#args + 1] = tostring((select(i, ...))) end
        risen.log.info("eventlog " .. ev .. "(" .. table.concat(args, ", ") .. ")")
    end)
end
