-- assets.lua -- the files of the "Ember Club": a club with its own mesh, material and texture (a reddened copy of the shipped ones).
--
-- A real mod ships the artist's files in `<mod>/assets/<any sub-directory>/Name._xmsh | ._xmat | ._ximg` (mounted at startup, nothing to write in Lua; see
-- docs/LUA_API.md "New meshes, textures and icons"). This showcase has no artwork and must not redistribute the game's files, so it DERIVES three files from the
-- shipped ones the first time it runs and writes them with risen.assets.write into <Risen>\bin\spass\generated\balance_example, a directory that is mounted at
-- startup in every later game (so the second run finds them and writes nothing).
--
--   mesh      BX_Club._xmsh        the club mesh, its material name replaced by the new one (same length: the file is a serialized object with sizes in front of the strings)
--   material  BXmod_SwordMisc_01_Diffuse_01._xmat   the sword material, pointing at the new texture name
--   texture   BXmod_SwordMisc_01_Diffuse_01._ximg   the shipped texture with red and blue swapped in every DXT1 block (same size, so the header stays valid)
--
-- The engine finds a resource by the bare name of its compiled file in a cache of everything mounted, so these names are all `risen.template.create` needs:
-- `mesh = "BX_Club.xmsh"` (the layer refuses a name that resolves to nothing).
local A = risen.assets
local bit = bit

local M = {}

M.MESH = "BX_Club.xmsh"                                 -- the name an item template uses
local SHIPPED_MESH, SHIPPED_MAT, SHIPPED_IMG = "It_Wpn_1H_Club.xmsh", "ItWpn_SwordMisc_01_Diffuse_01.xmat", "ItWpn_SwordMisc_01_Diffuse_01.tga"
local OLD_MAT, NEW_MAT = "ItWpn_SwordMisc_01_Diffuse_01._xmat", "BXmod_SwordMisc_01_Diffuse_01._xmat"
local OLD_IMG, NEW_IMG = "ItWpn_SwordMisc_01_Diffuse_01.tga", "BXmod_SwordMisc_01_Diffuse_01.tga"

-- Replaces every occurrence of `old` by `new`, which has to be exactly as long.
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
    return table.concat(out)
end

-- A compiled image is "GR01IM04" + header + property stream, then an ordinary DDS file. In a DXT1 DDS every 8-byte block holds two RGB565 colours and 16 two-bit
-- indices: swap red and blue of both colours; when that reverses their order (the order selects 4-colour or 3-colour+transparent mode) put them back and map the indices.
local swap4, swap3 = {}, {}
for b = 0, 255 do
    swap4[b] = bit.bxor(b, 0x55)
    local v = 0
    for k = 0, 3 do
        local pair = bit.band(bit.rshift(b, 2 * k), 3)
        if pair == 0 then pair = 1 elseif pair == 1 then pair = 0 end
        v = v + bit.lshift(pair, 2 * k)
    end
    swap3[b] = v
end

local function swap_rb(c)
    return bit.bor(bit.lshift(bit.band(c, 31), 11), bit.band(c, 0x7E0), bit.rshift(c, 11))
end

local function recolor_dxt1(data)
    local dds = data:find("DDS ", 1, true)
    assert(dds and data:sub(dds + 84, dds + 87) == "DXT1", "expected a DXT1 image")
    local first = dds + 128
    local out, parts = { data:sub(1, first - 1) }, {}
    local byte, char = string.byte, string.char
    local i, n = first, #data
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

-- Makes sure the three files exist (writes them when they do not). Returns true, or false and the reason; never raises.
function M.ensure()
    local info = A.info(M.MESH)
    if info and info.found and info.loose then return true end            -- an earlier run wrote them; they were mounted at startup
    local mesh, mat, img = A.read(SHIPPED_MESH), A.read(SHIPPED_MAT), A.read(SHIPPED_IMG)
    if not (mesh and mat and img) then return false, "a shipped source file cannot be read" end
    local ok, err = pcall(function()
        local files = {
            ["meshes/BX_Club._xmsh"] = rename_in(mesh, OLD_MAT, NEW_MAT),
            ["materials/BXmod_SwordMisc_01_Diffuse_01._xmat"] = rename_in(mat, OLD_IMG, NEW_IMG),
            ["images/BXmod_SwordMisc_01_Diffuse_01._ximg"] = recolor_dxt1(img),
        }
        for rel, data in pairs(files) do
            local written, why = A.write(rel, data)
            if not written then error(rel .. ": " .. tostring(why)) end
        end
    end)
    if not ok then return false, tostring(err) end
    return true
end

return M
