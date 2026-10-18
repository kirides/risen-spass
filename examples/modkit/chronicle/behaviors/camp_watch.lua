-- camp_watch: a behaviour of the example project chronicle.rmk.json, attached to the template MKC_Ash_Warlord_T with a changed text.
return {
    description = "The warlord taunts the player once when he comes near and reports his death.",
    params = { taunt = "Go away!" },
    near = 1500,

    start = function(self)
        self.state.taunted = self.state.taunted or false
        risen.emit("chronicle.behavior", "start", self.entity:name(), self.resumed, self.state.taunted)
    end,

    on_player_near = function(self, player)
        if self.state.taunted then return end
        self.state.taunted = true
        risen.hud.log(self.params.taunt)
        risen.emit("chronicle.behavior", "taunt", self.entity:name(), self.params.taunt)
    end,

    on_death = function(self, killer)
        risen.emit("chronicle.behavior", "death", self.entity:name())
    end,
}
