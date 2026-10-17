-- greeter: a behaviour of the risen-modkit showcase (risen.behavior, docs/LUA_API.md "Behaviours").
-- Attached to the placed NPC MK_Watcher (his own text and a gift) and to the template MK_Hob_T (so Hob greets with the default text).
-- `self`: self.entity (the NPC), self.params, self.state (saved with the game), self.resumed (true after a load).
-- The mod event "modkit_showcase.behavior" lets other mods (and the test driver mods/modkit_driver) follow what it does.
return {
    description = "Greets the player when he comes close; hands out a gift the first time.",
    params = { text = "Hello, stranger!", gift = "" },
    near = 500,

    start = function(self)
        self.state.greeted = self.state.greeted or 0
        risen.emit("modkit_showcase.behavior", "start", self.entity:name(), self.params.text, self.resumed, self.state.greeted)
    end,

    on_player_near = function(self, player)
        risen.hud.log(self.params.text)
        self.state.greeted = self.state.greeted + 1
        local gave = false
        if self.params.gift ~= "" and self.state.greeted == 1 then gave = risen.item.give(player, self.params.gift, 1) and true or false end
        risen.emit("modkit_showcase.behavior", "near", self.entity:name(), self.state.greeted, gave)
    end,
}
