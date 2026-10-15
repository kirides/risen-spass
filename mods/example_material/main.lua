-- EXAMPLE 3: change the material or texture of an existing template.
--
-- Two techniques, from the simplest to the one that ships a file:
--
--   A. MATERIAL VARIANTS the game already has (no files). A mesh can carry several materials ("skins") that the property `MaterialSwitch` selects:
--      the shipped black wolf `Wolf_Black` is the `Wolf` actor with eCAnimation_PS.MaterialSwitch = 1, the mana potion is the health potion mesh with
--      eCMesh_PS.MaterialSwitch = 1 (blue liquid). Set the number on an existing template (patch) or on a new clone (`material =` in create).
--      Find the variants of a mesh by scanning the templates: docs/EXAMPLES.md "Patterns".
--
--   B. YOUR OWN TEXTURE FILE. The engine finds a resource by the bare name of its compiled file in a cache of everything that is mounted; a loose file
--      with the name of a shipped one SHADOWS it. A real mod ships the artist's file as `<mod>/assets/<any sub-directory>/ItWpn_SwordBlades_01_Diffuse_01._ximg`
--      (the `assets` directory is mounted at startup, nothing to write in Lua). This example has no artwork and must not redistribute the game's files, so
--      it derives the file from the shipped texture at run time (recolor.lua: red and blue swapped) and writes it with risen.assets.write into
--      <Risen>\bin\spass\generated\example_material, which is mounted at startup in every later game. A texture that is already loaded when the file appears is
--      not replaced (the engine keeps the loaded object), so write files as early as possible (the first `tick`) -- or ship them in `assets/`.
--
-- Timing: A on the first `tick` (templates) and every `session.start` (live entities), like every template edit; B once on the first `tick`.
-- What cannot be verified here: how it looks (screenshots of the game window are black). The checks prove the properties, the file the engine resolves
-- and the resource it loaded (docs/RE.md section 26).
--
-- Run the live check (about a minute; see docs/EXAMPLES.md):
--   .\scripts\test-run.ps1 -Instance 3 -Mods example_material -WaitForResult -Seconds 240 -Scenario example_material -ShowChecks

local A = risen.assets
local recolor = require("recolor")

local TEXTURE = "ItWpn_SwordBlades_01_Diffuse_01.tga"                  -- the blades of every one-handed sword
local FILE = "textures/ItWpn_SwordBlades_01_Diffuse_01._ximg"          -- where the derived file goes below generated\example_material
local stats = { texture = "not run", changed = nil }                   -- what B did, for the test

-- ---- A. material variants of existing meshes --------------------------------------------------------------------------------------------------
local function apply_variants(into_live_entities)
    -- the health potion gets the other potion material (the blue one)
    local ok, why = risen.template.patch("It_Po_Health_01", { eCMesh_PS = { MaterialSwitch = 1 } }, { instances = into_live_entities })
    if not ok then risen.log.warn("example_material: " .. tostring(why)) end
    -- a new creature: the wolf actor with its black skin (what the shipped Wolf_Black does), its own name and a few more hit points
    local wolf, created = risen.template.create("Spass_Black_Wolf", "Wolf", { name = "Black Wolf", material = 1, hp_max = 250 })
    if not wolf then risen.log.warn("example_material: " .. tostring(created)) end
end

-- ---- B. a texture file that shadows a shipped one -----------------------------------------------------------------------------------------------
local function ensure_texture()
    local info = A.info(TEXTURE)
    if info and info.loose then                                          -- written by an earlier run: already mounted at startup
        stats.texture = "already present"
        return
    end
    local original = A.read(TEXTURE)
    if not original then stats.texture = "shipped texture not readable" return end
    local ok, derived = pcall(recolor.red_blue_swap, original)
    if not ok then stats.texture = tostring(derived) return end
    local written, err = A.write(FILE, derived)
    stats.texture = written and "written" or tostring(err)
    stats.changed = derived ~= original and #derived == #original
end

local first = true
risen.on("tick", function()
    if not first then return end
    first = false
    apply_variants(false)
    ensure_texture()
end)
risen.on("session.start", function() apply_variants(true) end)

-- the live check belongs to the test run only
if risen.test.is_active() then
    require("test_material")(TEXTURE, FILE, stats)
end
