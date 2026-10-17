---@diagnostic disable: need-check-nil
-- Live test mod of risen.behavior (scenario `behaviors`, scripts/smoke-test.ps1). It defines behaviours, attaches them while the mod loads and records every callback
-- in `rec`; test_behaviors.lua drives the NPCs (dialogue, items, a kill, spawns, a quick save + load) and checks.
local T = risen.test

local rec = { log = {} }
local function note(...)
    local parts = { ... }
    for i, v in ipairs(parts) do parts[i] = tostring(v) end
    rec.log[#rec.log + 1] = { t = T.elapsed(), text = table.concat(parts, " ") }
end

-- counts its updates in its saved state; params merged over the defaults
assert(risen.behavior.define("bhv_counter", {
    description = "counts",
    params = { step = 1, label = "default" },
    interval = 0.5,
    start = function(self) note("start", self.name, self.entity:name(), self.resumed, self.params.step, self.params.label, self.state.n or 0) end,
    update = function(self, dt)
        self.state.n = (self.state.n or 0) + self.params.step
        rec.counter_dt = rec.counter_dt or {}
        rec.counter_dt[#rec.counter_dt + 1] = dt
    end,
    stop = function(self, why) note("stop", self.name, self.key, why) end,
}))

-- the NPC's events
assert(risen.behavior.define("bhv_events", {
    near = 400,
    start = function(self) note("start", self.name, self.entity:name()) end,
    on_player_near = function(self, player, d) note("near", self.entity:name(), player:is_player(), math.floor(d)) end,
    on_player_far = function(self, player, d) note("far", self.entity:name(), math.floor(d)) end,
    on_talk = function(self, info) note("talk", self.entity:name(), info) end,
    on_items = function(self, tpl, delta, total, cause) note("items", self.entity:name(), tpl, delta, total, cause) end,
    on_damage = function(self, attacker, damage) note("damage", self.entity:name(), damage) end,
    on_quest = function(self, quest, status, previous) note("quest", self.entity:name(), quest, status) end,
    events = { ["behaviors.ping"] = function(self, value) note("ping", self.entity:name(), value) end },
    on_death = function(self, killer) note("death", self.entity:name(), killer and killer:is_player()) end,
    stop = function(self, why) note("stop", self.name, self.entity:name(), why) end,
}))

-- raises in every third update (its counter is saved): switched off, the others go on
assert(risen.behavior.define("bhv_broken", {
    interval = 0,
    update = function(self) self.state.k = (self.state.k or 0) + 1 if self.state.k % 3 == 0 then error("boom in update") end end,
}))

-- on every NPC of a template (the scenario spawns them)
assert(risen.behavior.define("bhv_tagged", {
    start = function(self) note("start", self.name, self.key) self.state.tag = (self.state.tag or 0) + 1 end,
    stop = function(self, why) note("stop", self.name, self.key, why) end,
}))

-- made during the session by the scenario
assert(risen.behavior.define("bhv_runtime", {
    start = function(self) note("start", self.name, self.entity:name(), self.resumed, self.params.tag) self.state.seen = true end,
}))

risen.npc.persistent({ name = "BHV_Guard", template = "Peasant_1_01", offset = { 1500, 20, 0 }, peaceful = true, label = "Behaviour Guard",
    define = function()
        risen.dialog.create({
            name = "BHV_Guard_Hello", owner = "BHV_Guard",
            lines = { { speaker = "player", listener = "npc", text = "Hello." }, { speaker = "npc", listener = "player", text = "Hm." }, { command = "End" } },
        })
    end })
risen.npc.persistent({ name = "BHV_Victim", template = "Peasant_1_02", offset = { -300, 20, 0 }, peaceful = true, label = "Behaviour Victim" })

-- appears only after a quest of the ORIGINAL game was solved (the scenario solves it), and gets a behaviour like any persistent NPC
risen.spawn.when({ name = "BHV_Ambusher", template = "Peasant_1_01", offset = { 0, 20, -600 }, peaceful = true, label = "Ambusher",
    when = risen.cond.quest("Rufo_GoToDruidHut", "Success") })
assert(risen.behavior.attach("BHV_Ambusher", "bhv_events"))

assert(risen.behavior.attach("BHV_Guard", "bhv_counter", { step = 2 }))
assert(risen.behavior.attach("BHV_Guard", "bhv_events"))
assert(risen.behavior.attach("BHV_Guard", "bhv_broken"))
assert(risen.behavior.attach("BHV_Victim", "bhv_events"))
assert(risen.behavior.attach_template("Peasant_1_03", "bhv_tagged"))

if T.is_active() then
    require("test_behaviors")(rec)
end
