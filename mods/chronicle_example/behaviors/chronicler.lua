-- chronicler: a behaviour of the example project chronicle.rmk.json (risen.behavior, docs/LUA_API.md "Behaviours").
-- Attached to the template MKC_Maren_T, so every NPC of it (Maren) runs it. Reports through the mod event "chronicle.behavior" (the live scenario
-- `chronicle` follows it). `self.state` is saved with the game: the visit counter survives a load.
return {
    description = "The chronicler greets the player the first time he comes close and counts the visits and the conversations.",
    params = { greeting = "Welcome, traveller. Mind the ash." },
    near = 450,

    start = function(self)
        self.state.visits = self.state.visits or 0
        self.state.talks = self.state.talks or 0
        risen.emit("chronicle.behavior", "start", self.entity:name(), self.resumed, self.state.visits)
    end,

    on_player_near = function(self, player)
        self.state.visits = self.state.visits + 1
        if self.state.visits == 1 then risen.hud.log(self.params.greeting) end
        risen.emit("chronicle.behavior", "near", self.entity:name(), self.state.visits)
    end,

    on_talk = function(self, info)
        self.state.talks = self.state.talks + 1
        risen.emit("chronicle.behavior", "talk", self.entity:name(), self.state.talks)
    end,
}
