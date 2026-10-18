-- risen-modkit extras example 1.0.0: exported by risen-modkit. Generated: the next export replaces this file, data/, strings/ and modkit/;
-- hand-written additions go into custom.lua (created once, never overwritten).
local runtime = require("modkit.runtime")
runtime.install({
    balance = require("data.balance"),
    npcs = require("data.npcs"),
    quests = require("data.quests"),
    dialogs = require("data.dialogs"),
    world = require("data.world"),
    assets = require("data.assets"),
    patches = require("data.patches"),
    dialog_edits = require("data.dialog_edits"),
    behaviors = require("data.behaviors"),
})
require("custom")
