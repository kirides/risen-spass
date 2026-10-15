-- recolor.lua -- turns a shipped DXT1 texture into a differently coloured one (module of example_material).
--
-- A real mod would simply ship the artist's texture (`<mod>/assets/<anything>/Name._ximg`, mounted at startup). This example has no artwork and
-- must not redistribute the game's files, so it derives its texture from the shipped one at run time: it swaps red and blue in every colour block.
--
-- Format (docs/RE.md section 26): a compiled image is "GR01IM04" + header + property stream, then an ordinary DDS file. In a DXT1 DDS every 8-byte
-- block holds two RGB565 colours and 16 two-bit indices. Swapping red and blue of both colours keeps size and header valid. The order of the two
-- colours selects the block mode (c0 > c1: four colours, else three colours + transparent), so when the swap reverses the order the two colours are
-- exchanged back and the indices are mapped accordingly.
local bit = bit
local M = {}

local swap4, swap3 = {}, {}                        -- index byte maps (four indices of two bits each)
for b = 0, 255 do
    swap4[b] = bit.bxor(b, 0x55)                   -- 4-colour mode: colour 0 <-> 1 and 2 <-> 3 (the interpolated ones follow)
    local v = 0
    for k = 0, 3 do
        local pair = bit.band(bit.rshift(b, 2 * k), 3)
        if pair == 0 then pair = 1 elseif pair == 1 then pair = 0 end   -- 3-colour mode: only 0 <-> 1 (2 is the mix, 3 transparent)
        v = v + bit.lshift(pair, 2 * k)
    end
    swap3[b] = v
end

local function swap_rb(c)
    return bit.bor(bit.lshift(bit.band(c, 31), 11), bit.band(c, 0x7E0), bit.rshift(c, 11))
end

---@param data string compiled image (._ximg) with a DXT1 payload
---@return string recolored same size, red and blue swapped
function M.red_blue_swap(data)
    local dds = data:find("DDS ", 1, true)
    assert(dds and data:sub(dds + 84, dds + 87) == "DXT1", "recolor: expected a DXT1 image")
    local first = dds + 128                        -- 4 bytes magic + 124 bytes DDS header
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
    out[#out + 1] = data:sub(i)                    -- trailing bytes (none for a plain DDS)
    return table.concat(out)
end

return M
