-- New meshes, textures and icons from loose files (docs/LUA_API.md "New meshes, textures and icons", docs/RE.md section 26).
--
-- The engine finds a resource by the bare file name of its compiled file (X.xmsh -> X._xmsh, X.tga -> X._ximg, X.xac -> X._xmac) in a name cache of everything
-- that is mounted, so a mod only has to put the files into a directory the layer mounts: <mod>/assets (mounted at startup) or, for files a mod makes itself,
-- the directory risen.assets.write fills (<Risen>\bin\spass\generated\<mod>, also mounted at startup).
--
-- Without a modelling toolchain this example *derives* its assets from shipped files at run time (nothing of the game is stored in this repository):
--   * a mesh   = the club mesh with its material name changed (same length: the file is a serialized object with sizes in front of the strings)
--   * a material = the sword material pointing at a new texture name
--   * a texture  = the sword texture with red and blue swapped in every DXT1 block (same size, so the header stays valid)
--   * an icon  = an XML image list (Spass_Icons.ximl) over a copy of the stock icon atlas, with a new image name for the club's icon cell; or simply a
--     whole image file registered as GUI bitmap (risen.assets.load_bitmap) and used as the icon
--   * an actor = the wolf's actor file under a new name
-- and then creates an item and a creature that use them (risen.template.create checks the names against the archives and the mounted directories).
--
-- Phases (-Param phase=...): "build" derives the files and plays everything; "use" runs in a fresh process, where the generated directory is already mounted at
-- startup, and needs no building (the scenario newassets in scripts/smoke-test.ps1 runs both).
local log = risen.log
local T = risen.test
local A = risen.assets
local bit = bit
local phase = T.param("phase", "build")

local function L(...)
    local t = {}
    for i = 1, select("#", ...) do t[#t + 1] = tostring((select(i, ...))) end
    log.info("newassets: " .. table.concat(t, " "))
end

-- ---- deriving files ------------------------------------------------------------------------------------------------------------------------------------

-- Replaces every occurrence of `old` by `new`, which has to be exactly as long (names are stored with length prefixes and surrounded by sizes).
local function rename_in(data, old, new)
    assert(#old == #new, "rename_in: '" .. old .. "' and '" .. new .. "' differ in length")
    local out, from, n = {}, 1, 0
    while true do
        local a, b = data:find(old, from, true)
        if not a then break end
        out[#out + 1] = data:sub(from, a - 1)
        out[#out + 1] = new
        from, n = b + 1, n + 1
    end
    assert(n > 0, "rename_in: '" .. old .. "' not found")
    out[#out + 1] = data:sub(from)
    return table.concat(out), n
end

-- The compiled image is "GR01IM04" + header + property stream, then an ordinary DDS file. For a DXT1 DDS: swap red and blue in the two 565 colours of every
-- 8-byte block; when that reverses their order (the order selects the 4-colour or the 3-colour+transparent mode) put them back and swap the indices.
local swap4, swap3 = {}, {}
for b = 0, 255 do
    swap4[b] = bit.bxor(b, 0x55)                                  -- 4-colour mode: 0<->1, 2<->3
    local v = 0
    for k = 0, 3 do
        local pair = bit.band(bit.rshift(b, 2 * k), 3)
        if pair == 0 then pair = 1 elseif pair == 1 then pair = 0 end   -- 3-colour mode: 0<->1, 2 and 3 stay
        v = v + bit.lshift(pair, 2 * k)
    end
    swap3[b] = v
end

local function recolor_dxt1(data)
    local dds = data:find("DDS ", 1, true)
    assert(dds and data:sub(dds + 84, dds + 87) == "DXT1", "recolor_dxt1: expected a DXT1 image")
    local first = dds + 128
    local out = { data:sub(1, first - 1) }
    local function swap_rb(c) return bit.bor(bit.lshift(bit.band(c, 31), 11), bit.band(c, 0x7E0), bit.rshift(c, 11)) end
    local byte, char = string.byte, string.char
    local i, n = first, #data
    local parts = {}
    while i + 7 <= n do
        local a, b, c, d, e, f, g, h = byte(data, i, i + 7)
        local c0, c1 = a + b * 256, c + d * 256
        local n0, n1 = swap_rb(c0), swap_rb(c1)
        if (c0 > c1) ~= (n0 > n1) and n0 ~= n1 then
            n0, n1 = n1, n0
            local map = c0 > c1 and swap4 or swap3
            e, f, g, h = map[e], map[f], map[g], map[h]
        end
        parts[#parts + 1] = char(n0 % 256, math.floor(n0 / 256), n1 % 256, math.floor(n1 / 256), e, f, g, h)
        if #parts == 4096 then out[#out + 1] = table.concat(parts) parts = {} end
        i = i + 8
    end
    out[#out + 1] = table.concat(parts)
    out[#out + 1] = data:sub(i)
    return table.concat(out)
end

local ICON_LIST = [[<ImageList
	ClassName="eCGuiImageList2"
	ImageSize="0; 0"
	TextureName="Spass_IconAtlas.dds"
	>
	<Images>
		<Image
			ClassName="eCGuiImage2"
			ImageName="IC_Spass_Club"
			RectOnTexture="123; 534; 163; 574"
			BlendMode="eEImageBlend_AlphaBlend"
		>
		</Image>
	</Images>
</ImageList>
]]

-- Writes every derived file; returns the number of files and the first problem.
local function build_assets()
    local written, problem = 0, nil
    local function put(rel, data)
        if not data then problem = problem or ("no data for " .. rel) return end
        local ok, err = A.write(rel, data)
        if ok then written = written + 1 else problem = problem or err end
    end
    A.clear()
    local mesh = A.read("It_Wpn_1H_Club.xmsh")
    local mat = A.read("ItWpn_SwordMisc_01_Diffuse_01.xmat")
    local img = A.read("ItWpn_SwordMisc_01_Diffuse_01.tga")
    local atlas = A.read("GUI_IconSets01.dds")
    local wolf = A.read("Ani_Wolf_Monster_Wolf.xac")
    if not (mesh and mat and img and atlas and wolf) then return 0, "a shipped source file is missing" end
    local ok, err = pcall(function()
        put("meshes/Spass_Club._xmsh", (rename_in(mesh, "ItWpn_SwordMisc_01_Diffuse_01._xmat", "Spass_SwordMisc_01_Diffuse_01._xmat")))
        put("materials/Spass_SwordMisc_01_Diffuse_01._xmat", (rename_in(mat, "ItWpn_SwordMisc_01_Diffuse_01.tga", "Spass_SwordMisc_01_Diffuse_01.tga")))
        put("images/Spass_SwordMisc_01_Diffuse_01._ximg", recolor_dxt1(img))
        put("gui/Spass_IconAtlas._ximg", atlas)
        put("gui/Spass_Icon._ximg", recolor_dxt1(img))
        put("gui/Spass_Icons.ximl", ICON_LIST)
        put("actors/Spass_Wolf._xmac", wolf)
    end)
    if not ok then problem = problem or tostring(err) end
    return written, problem
end

-- ---- the new content ----------------------------------------------------------------------------------------------------------------------------------

local function create_content()
    local made, problems = {}, {}
    local function note(what, t, err) if t then made[what] = t else problems[#problems + 1] = what .. ": " .. tostring(err) end end
    local ok, err = A.load_icons("Spass_Icons")           -- the icon names must exist before a template uses them (they are checked)
    if not ok then problems[#problems + 1] = "icons: " .. tostring(err) end
    note("sword", risen.template.create("Spass_Loose_Sword", "It_1H_Club", {
        name = "Reddened Club", mesh = "Spass_Club.xmsh", icon = "IC_Spass_Club", value = 77,
    }))
    made.bitmap_ok = A.load_bitmap("Spass_Icon.ximg")      -- a single image file as GUI bitmap; its file name is the icon name
    note("sword2", risen.template.create("Spass_Loose_Sword2", "It_1H_Club", { name = "Reddened Club II", icon = "Spass_Icon.ximg" }))
    note("wolf", risen.template.create("Spass_Loose_Wolf", "Wolf", { name = "Loose Wolf", mesh = "Spass_Wolf.xac", hp_max = 220 }))
    return made, problems
end

local setup_done = false
local function setup()
    if setup_done then return end
    setup_done = true
    if phase == "build" then
        local n, problem = build_assets()
        L("built", n, "files", problem or "")
        T.expect("assets generated", n == 7 and not problem, tostring(problem))
    end
    local made, problems = create_content()
    for _, p in ipairs(problems) do L("problem", p) end
    T.expect("item and creature created", made.sword ~= nil and made.sword2 ~= nil and made.wolf ~= nil and made.bitmap_ok, table.concat(problems, "; "))
end
risen.on("tick", setup)   -- the first tick: the GUI (icons) and the world-independent engine parts are up

-- ---- the scenario -------------------------------------------------------------------------------------------------------------------------------------

local PLAN = {
    "item and creature created",
    "mesh resolves to a loose file", "material and texture resolve to loose files", "stock names still resolve to the game",
    "unknown mesh is refused", "wrong kind of file is refused", "unknown icon is refused", "check_assets = false skips the check", "new_items spec checks the names",
    "spawned item names the loose mesh", "loose mesh data loaded", "loose material loaded", "loose texture loaded",
    "icon is a registered bitmap", "item reports the new icon", "a single image file works as icon",
    "spawned creature names the loose actor", "loose actor loaded", "creature lives on",
}
local PLAN_BUILD = { "assets generated", "loose file shadows a shipped one", "shadow removed again" }
local plan = {}
for _, n in ipairs(PLAN) do plan[#plan + 1] = n end
if phase == "build" then for _, n in ipairs(PLAN_BUILD) do plan[#plan + 1] = n end end
T.plan(plan)

local function has(list, text)
    for _, v in ipairs(list or {}) do if tostring(v):lower() == text:lower() then return true end end
    return false
end

T.run(function()
    T.wait_event("session.start", 180, function(m) return m == "new" end)
    T.sleep(100)
    setup()   -- no-op normally (the first tick did it)

    T.section("where the files come from")
    local mesh = A.info("Spass_Club.xmsh")
    T.expect("mesh resolves to a loose file", mesh.found and mesh.loose == true and mesh.source == "newassets:generated", ("%s %s %s"):format(tostring(mesh.found), tostring(mesh.physical), tostring(mesh.source)))
    local mat, img = A.info("Spass_SwordMisc_01_Diffuse_01.xmat"), A.info("Spass_SwordMisc_01_Diffuse_01.tga")
    T.expect("material and texture resolve to loose files", mat.found and mat.loose and img.found and img.loose and img.compiled == "Spass_SwordMisc_01_Diffuse_01._ximg", tostring(img.physical))
    local club = A.info("It_Wpn_1H_Club.xmsh")
    T.expect("stock names still resolve to the game", club.found and not club.loose and club.source == "game", tostring(club.physical))

    T.section("checks in risen.template.create")
    local t, why = risen.template.create("Spass_Nope", "It_1H_Club", { mesh = "Spass_Nowhere.xmsh" })
    T.expect("unknown mesh is refused", t == nil and tostring(why):find("Spass_Nowhere._xmsh", 1, true) ~= nil, tostring(why))
    t, why = risen.template.create("Spass_Nope", "It_1H_Club", { mesh = "Spass_SwordMisc_01_Diffuse_01.xmat" })
    T.expect("wrong kind of file is refused", t == nil and tostring(why):find("expected a mesh", 1, true) ~= nil, tostring(why))
    t, why = risen.template.create("Spass_Nope", "It_1H_Club", { icon = "IC_Spass_Nowhere" })
    T.expect("unknown icon is refused", t == nil and tostring(why):find("IC_Spass_Nowhere", 1, true) ~= nil, tostring(why))
    t, why = risen.template.create("Spass_Unchecked", "It_1H_Club", { mesh = "Spass_Nowhere.xmsh", icon = "IC_Spass_Nowhere", check_assets = false })
    T.expect("check_assets = false skips the check", t ~= nil, tostring(why))
    local report, reason, problems = risen.balance.apply({ new_items = { Spass_Spec_Nope = { base = "It_1H_Club", mesh = "Spass_Nowhere.xmsh" } } }, { dry_run = true })
    local first = tostring(problems and problems[1] or reason)
    T.expect("new_items spec checks the names", report == nil and first:find("Spass_Nowhere._xmsh", 1, true) ~= nil, first)

    T.section("an item with a loose mesh and icon")
    local p = assert(risen.entity.player())
    local x, y, z = p:position()
    x, y, z = x or 0, y or 0, z or 0
    local item = risen.entity.spawn("Spass_Loose_Sword", x + 150, y + 20, z)
    local deps = item and item:dependencies() or {}
    T.expect("spawned item names the loose mesh", item ~= nil and has(deps, "Spass_Club._xmsh"), table.concat(deps, ","))
    T.sleep(120)   -- the resource system loads in the background
    local function state(name) local r = A.resource(name) return r and r.state or -1, r and r.path or "" end
    local st, path = state("Spass_Club.xmsh")
    T.expect("loose mesh data loaded", st == 3 and path:find("generated", 1, true) ~= nil, ("state %d %s"):format(st, path))
    st, path = state("Spass_SwordMisc_01_Diffuse_01._xmat")
    T.expect("loose material loaded", st == 3 and path:find("generated", 1, true) ~= nil, ("state %d %s"):format(st, path))
    st, path = state("Spass_SwordMisc_01_Diffuse_01._ximg")
    T.expect("loose texture loaded", st >= 1 and path:find("generated", 1, true) ~= nil, ("state %d %s"):format(st, path))
    local bmp = A.bitmap("IC_Spass_Club")
    T.expect("icon is a registered bitmap", bmp ~= nil and bmp.attached == true)
    T.eq("item reports the new icon", (item and item:item_icon() or ""):lower(), "ic_spass_club")

    T.section("a creature with a loose actor")
    local wolf = risen.entity.spawn("Spass_Loose_Wolf", x - 400, y + 20, z)
    local wdeps = wolf and wolf:dependencies() or {}
    T.expect("spawned creature names the loose actor", wolf ~= nil and has(wdeps, "Spass_Wolf.xac"), table.concat(wdeps, ","))
    st, path = state("Spass_Wolf._xmac")
    T.expect("loose actor loaded", st >= 1 and path:find("generated", 1, true) ~= nil, ("state %d %s"):format(st, path))
    T.sleep(300)
    T.expect("creature lives on", wolf ~= nil and not wolf:is_dead() and (wolf:hp() or 0) > 0, wolf and tostring(wolf:hp()) or "no wolf")

    if phase == "build" then
        T.section("a loose file shadows a shipped one")
        local before = A.info("Item_Apple.xmsh")
        local club_mesh = assert(A.read("It_Wpn_1H_Club.xmsh"))
        assert(A.write("shadow/Item_Apple._xmsh", club_mesh))
        local after = A.info("Item_Apple.xmsh")   -- info refreshes the mounted directory first
        T.expect("loose file shadows a shipped one", before.source == "game" and after.source == "newassets:generated", tostring(before.source) .. " -> " .. tostring(after.source))
        A.remove("shadow")
        local restored = A.info("Item_Apple.xmsh")
        T.expect("shadow removed again", restored.source == "game", tostring(restored.source))
    end
    local item2 = risen.entity.spawn("Spass_Loose_Sword2", x + 200, y + 20, z)
    local bmp2 = A.bitmap("Spass_Icon.ximg")
    T.expect("a single image file works as icon", item2 ~= nil and bmp2 ~= nil and bmp2.attached == true and (item2:item_icon() or ""):lower() == "spass_icon.ximg", item2 and tostring(item2:item_icon()) or "no item")
    T.finish("newassets " .. phase)
end, "newassets")
