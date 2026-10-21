-- risen-modkit extras example 1.0.0: exported by risen-modkit. Generated: the next export replaces this file, data/, lua/, strings/ and modkit/;
-- hand-written code goes into the project's Lua files (the editor's Lua panel, exported into lua/) or custom.lua (created once, never overwritten).
local runtime = require("modkit.runtime")
local data = {
    balance = require("data.balance"),
    npcs = require("data.npcs"),
    quests = require("data.quests"),
    dialogs = require("data.dialogs"),
    world = require("data.world"),
    assets = require("data.assets"),
    patches = require("data.patches"),
    dialog_edits = require("data.dialog_edits"),
    behaviors = require("data.behaviors"),
}
local lua = runtime.extensions({})
lua.call("before_install", data)
runtime.install(data)
lua.call("after_install", runtime)
runtime.extensions({ "custom" }).call("after_install", runtime)
