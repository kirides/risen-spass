-- Live check of example_material (only loaded in a test run). The test removes the derived texture at the end so that other scenarios in the same
-- test copy (mods/newassets reads the sword textures) never see a recoloured file.
local T = risen.test
local A = risen.assets

return function(TEXTURE, FILE, stats)
    T.plan { "potion variant on the template", "potion variant on a new spawn", "black wolf has the shipped black skin", "plain wolf is untouched", "black wolf lives",
             "texture file written", "derived file differs and keeps its size", "engine resolves the loose file", "engine loaded the loose texture", "shadow removed again" }

    T.run(function()
        T.wait_event("session.start", 180, function(m) return m == "new" end)
        T.sleep(150)
        local player = assert(risen.entity.player())
        local x, y, z = player:position()
        x, y, z = x or 0, y or 0, z or 0

        -- A. variants
        local health, mana = assert(risen.template.get("It_Po_Health_01")), assert(risen.template.get("It_Po_Mana_01"))
        T.eq("potion variant on the template", health:get_property("eCMesh_PS", "MaterialSwitch"), mana:get_property("eCMesh_PS", "MaterialSwitch"))
        local potion = assert(risen.entity.spawn("It_Po_Health_01", x + 100, y + 10, z))
        T.eq("potion variant on a new spawn", potion:get_property("eCMesh_PS", "MaterialSwitch"), "1")
        local shipped_black = assert(risen.template.get("Wolf_Black"))
        local black = assert(risen.npc.spawn("Spass_Black_Wolf", x + 1500, y + 20, z, { name = "T_BlackWolf" }))
        T.eq("black wolf has the shipped black skin", { black:get_property("eCAnimation_PS", "MaterialSwitch"), black:get_property("eCAnimation_PS", "ResourceFilePath") },
            { shipped_black:get_property("eCAnimation_PS", "MaterialSwitch"), shipped_black:get_property("eCAnimation_PS", "ResourceFilePath") })
        T.eq("plain wolf is untouched", risen.template.get("Wolf"):get_property("eCAnimation_PS", "MaterialSwitch"), "0")
        T.sleep(200)
        T.expect("black wolf lives", not black:is_dead() and (black:hp() or 0) > 0, tostring(black:hp()))

        -- B. the texture
        T.expect("texture file written", stats.texture == "written" or stats.texture == "already present", stats.texture)
        T.expect("derived file differs and keeps its size", stats.changed ~= false, tostring(stats.changed))
        local info = A.info(TEXTURE)
        T.expect("engine resolves the loose file", info.found and info.loose and info.source == "example_material:generated", ("%s %s"):format(tostring(info.source), tostring(info.physical)))
        local sword = assert(risen.entity.spawn("It_1H_Sword", x + 150, y + 20, z))   -- its blades use the texture
        T.sleep(150)
        local res = A.resource("ItWpn_SwordBlades_01_Diffuse_01._ximg")
        T.expect("engine loaded the loose texture", res and res.state >= 1 and res.path:find("generated", 1, true) ~= nil and sword:valid(),
            res and ("state %d %s"):format(res.state, res.path) or "no resource")

        A.remove("textures")
        T.eq("shadow removed again", A.info(TEXTURE).source, "game")
    end, "example_material")
end
