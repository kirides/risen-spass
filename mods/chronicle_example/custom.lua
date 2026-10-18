-- custom.lua: hand-written additions to the mod. risen-modkit creates this file once and never overwrites it.
-- Everything of the layer's API is available (types/risen.d.lua, docs/MODDING_GUIDE.md); the exported content is
-- installed before this file runs.
--
-- The one piece of hand-written code of the Ashen Chronicle: the script the dialogue's Script node (Maren, "Is the Chronicle complete?") and the
-- event graph "Dusk bell" (Run script) call by name. A script is the editor's escape hatch for what no node does.
risen.script.define("MKC_TideBell", function(ctx)
    risen.hud.log("The tide bell tolls.")
    risen.emit("chronicle.bell", ctx.arg)
    return 0
end)
