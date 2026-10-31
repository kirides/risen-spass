using System.Buffers.Binary;

namespace Risen.Pak.Formats;

/// <summary>A decoded picture: BGRA, 8 bits per channel, not premultiplied.</summary>
public sealed record Bitmap32(int Width, int Height, byte[] Bgra, string Description);

/// <summary>Decoder for the DDS images Risen uses (top mip level only): DXT1/3/5, 32-bit RGB(A), L8, A8.</summary>
public static class Dds
{
    /// <summary>Finds the DDS inside a compiled image (<c>._ximg</c>, "GR01" "IM04": the payload offset is the u32 at 0x10)
    /// or takes a plain .dds file as it is. Returns -1 when there is none.</summary>
    public static int FindPayload(ReadOnlySpan<byte> file)
    {
        if (file.Length >= 4 && file[..4].SequenceEqual("DDS "u8)) return 0;
        if (file.Length > 0x20 && file[..4].SequenceEqual("GR01"u8))
        {
            uint off = BinaryPrimitives.ReadUInt32LittleEndian(file[0x10..]);
            if (off + 4 <= (uint)file.Length && file.Slice((int)off, 4).SequenceEqual("DDS "u8)) return (int)off;
            var window = file[..Math.Min(file.Length, 2048)];
            int i = window.IndexOf("DDS "u8);
            return i;
        }
        return -1;
    }

    public static Bitmap32 Decode(ReadOnlySpan<byte> file, int offset = 0, int maxPixels = 64 << 20)
    {
        var d = file[offset..];
        if (d.Length < 128 || !d[..4].SequenceEqual("DDS "u8)) throw new InvalidDataException("Not a DDS image.");
        uint headerSize = BinaryPrimitives.ReadUInt32LittleEndian(d[4..]);
        if (headerSize != 124) throw new InvalidDataException("Unexpected DDS header size.");
        int h = (int)BinaryPrimitives.ReadUInt32LittleEndian(d[12..]);
        int w = (int)BinaryPrimitives.ReadUInt32LittleEndian(d[16..]);
        if (w <= 0 || h <= 0 || (long)w * h > maxPixels) throw new InvalidDataException($"Image size {w}x{h} is not supported.");
        uint pfFlags = BinaryPrimitives.ReadUInt32LittleEndian(d[80..]);
        var fourCC = d.Slice(84, 4);
        uint bits = BinaryPrimitives.ReadUInt32LittleEndian(d[88..]);
        uint rMask = BinaryPrimitives.ReadUInt32LittleEndian(d[92..]);
        uint gMask = BinaryPrimitives.ReadUInt32LittleEndian(d[96..]);
        uint bMask = BinaryPrimitives.ReadUInt32LittleEndian(d[100..]);
        uint aMask = BinaryPrimitives.ReadUInt32LittleEndian(d[104..]);
        var data = d[128..];
        var px = new byte[checked(w * h * 4)];
        string desc;

        if ((pfFlags & 0x4) != 0) // FOURCC
        {
            string cc = System.Text.Encoding.ASCII.GetString(fourCC);
            int block = cc == "DXT1" ? 8 : 16;
            if (cc is not ("DXT1" or "DXT3" or "DXT5")) throw new InvalidDataException($"Compression '{cc}' is not supported.");
            int bw = (w + 3) / 4, bh = (h + 3) / 4;
            if (data.Length < (long)bw * bh * block) throw new InvalidDataException("The image data is truncated.");
            Span<byte> rgba = stackalloc byte[64];
            for (int by = 0; by < bh; by++)
                for (int bx = 0; bx < bw; bx++)
                {
                    var b = data.Slice((by * bw + bx) * block, block);
                    DecodeBlock(cc, b, rgba);
                    for (int y = 0; y < 4 && by * 4 + y < h; y++)
                        for (int x = 0; x < 4 && bx * 4 + x < w; x++)
                        {
                            int s = (y * 4 + x) * 4, t = ((by * 4 + y) * w + bx * 4 + x) * 4;
                            px[t] = rgba[s]; px[t + 1] = rgba[s + 1]; px[t + 2] = rgba[s + 2]; px[t + 3] = rgba[s + 3];
                        }
                }
            desc = $"{cc} {w}x{h}";
        }
        else if ((pfFlags & 0x40) != 0 && bits == 32 || (pfFlags & 0x40) != 0 && bits == 24) // RGB(A)
        {
            int bpp = (int)bits / 8;
            if (data.Length < (long)w * h * bpp) throw new InvalidDataException("The image data is truncated.");
            for (int i = 0; i < w * h; i++)
            {
                uint v = 0;
                for (int k = 0; k < bpp; k++) v |= (uint)data[i * bpp + k] << (8 * k);
                px[i * 4] = Ch(v, bMask); px[i * 4 + 1] = Ch(v, gMask); px[i * 4 + 2] = Ch(v, rMask);
                px[i * 4 + 3] = aMask != 0 && (pfFlags & 0x1) != 0 ? Ch(v, aMask) : (byte)255;
            }
            desc = $"{bits}-bit RGB{((pfFlags & 0x1) != 0 ? "A" : "")} {w}x{h}";
        }
        else if (bits == 8 && (pfFlags & 0x20000) != 0) // luminance
        {
            if (data.Length < w * h) throw new InvalidDataException("The image data is truncated.");
            for (int i = 0; i < w * h; i++) { byte v = data[i]; px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = v; px[i * 4 + 3] = 255; }
            desc = $"L8 {w}x{h}";
        }
        else if (bits == 8 && (pfFlags & 0x2) != 0) // alpha only: shown as gray
        {
            if (data.Length < w * h) throw new InvalidDataException("The image data is truncated.");
            for (int i = 0; i < w * h; i++) { byte v = data[i]; px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = v; px[i * 4 + 3] = 255; }
            desc = $"A8 {w}x{h}";
        }
        else throw new InvalidDataException("This DDS pixel format is not supported.");

        uint mips = BinaryPrimitives.ReadUInt32LittleEndian(d[28..]);
        if (mips > 1) desc += $", {mips} mip levels";
        return new Bitmap32(w, h, px, desc);
    }

    static byte Ch(uint v, uint mask)
    {
        if (mask == 0) return 0;
        int shift = System.Numerics.BitOperations.TrailingZeroCount(mask);
        uint max = mask >> shift;
        return (byte)(((v & mask) >> shift) * 255 / max);
    }

    static void DecodeBlock(string cc, ReadOnlySpan<byte> b, Span<byte> outBgra)
    {
        var color = cc == "DXT1" ? b : b[8..];
        ushort c0 = BinaryPrimitives.ReadUInt16LittleEndian(color);
        ushort c1 = BinaryPrimitives.ReadUInt16LittleEndian(color[2..]);
        Span<byte> pal = stackalloc byte[16]; // 4 x BGRA
        Rgb565(c0, pal[..4]); Rgb565(c1, pal.Slice(4, 4));
        bool oneBitAlpha = false;
        if (c0 > c1 || cc != "DXT1")
        {
            for (int i = 0; i < 3; i++)
            {
                pal[8 + i] = (byte)((2 * pal[i] + pal[4 + i]) / 3);
                pal[12 + i] = (byte)((pal[i] + 2 * pal[4 + i]) / 3);
            }
            pal[11] = pal[15] = 255;
        }
        else
        {
            for (int i = 0; i < 3; i++) pal[8 + i] = (byte)((pal[i] + pal[4 + i]) / 2);
            pal[11] = 255;
            pal[12] = pal[13] = pal[14] = pal[15] = 0;
            oneBitAlpha = true;
        }
        uint idx = BinaryPrimitives.ReadUInt32LittleEndian(color[4..]);
        for (int i = 0; i < 16; i++)
        {
            int c = (int)((idx >> (2 * i)) & 3);
            outBgra[i * 4] = pal[c * 4]; outBgra[i * 4 + 1] = pal[c * 4 + 1]; outBgra[i * 4 + 2] = pal[c * 4 + 2];
            outBgra[i * 4 + 3] = oneBitAlpha || cc == "DXT1" ? pal[c * 4 + 3] : (byte)255;
        }
        if (cc == "DXT3")
        {
            for (int i = 0; i < 16; i++)
            {
                int nib = (b[i / 2] >> (4 * (i & 1))) & 0xF;
                outBgra[i * 4 + 3] = (byte)(nib * 17);
            }
        }
        else if (cc == "DXT5")
        {
            byte a0 = b[0], a1 = b[1];
            Span<byte> al = stackalloc byte[8];
            al[0] = a0; al[1] = a1;
            if (a0 > a1) for (int i = 1; i <= 6; i++) al[1 + i] = (byte)(((7 - i) * a0 + i * a1) / 7);
            else
            {
                for (int i = 1; i <= 4; i++) al[1 + i] = (byte)(((5 - i) * a0 + i * a1) / 5);
                al[6] = 0; al[7] = 255;
            }
            ulong bitsA = 0;
            for (int i = 0; i < 6; i++) bitsA |= (ulong)b[2 + i] << (8 * i);
            for (int i = 0; i < 16; i++) outBgra[i * 4 + 3] = al[(int)((bitsA >> (3 * i)) & 7)];
        }
    }

    static void Rgb565(ushort c, Span<byte> bgra)
    {
        int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
        bgra[0] = (byte)(b * 255 / 31); bgra[1] = (byte)(g * 255 / 63); bgra[2] = (byte)(r * 255 / 31); bgra[3] = 255;
    }
}
