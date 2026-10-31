using System.Buffers.Binary;
using System.Text;

namespace Risen.Pak.Formats;

/// <summary>A string table (<c>*.tab</c>, "TAB0"): column names and rows of cells.</summary>
public sealed record StringTable(string[] Columns, List<string[]> Rows);

public static class TabReader
{
    public static bool IsTab(ReadOnlySpan<byte> b) => b.Length >= 20 && b[..4].SequenceEqual("TAB0"u8);

    /// <summary>Layout (RE.md section 43): "TAB0", u16, u16, FILETIME, u32 columns, then per column
    /// u8 1, u8 1, u8 0, u16 name length, UTF-16 name, u32 row count, rows as u16 length + UTF-16 text.</summary>
    public static StringTable Read(ReadOnlySpan<byte> b)
    {
        if (!IsTab(b)) throw new InvalidDataException("Not a TAB0 string table.");
        int p = 16;
        uint ncols = U32(b, ref p);
        if (ncols is 0 or > 64) throw new InvalidDataException("Implausible column count.");
        var names = new string[ncols];
        var cols = new List<string[]>();
        for (int c = 0; c < ncols; c++)
        {
            p += 3;
            int n = U16(b, ref p);
            names[c] = Utf16(b, ref p, n);
            uint cnt = U32(b, ref p);
            if (cnt > (uint)(b.Length - p) / 2) throw new InvalidDataException("Row count exceeds the file.");
            var col = new string[cnt];
            for (int r = 0; r < cnt; r++) col[r] = Utf16(b, ref p, U16(b, ref p));
            cols.Add(col);
        }
        int rows = cols.Max(c => c.Length);
        var table = new List<string[]>(rows);
        for (int r = 0; r < rows; r++)
        {
            var row = new string[ncols];
            for (int c = 0; c < ncols; c++) row[c] = r < cols[c].Length ? cols[c][r] : "";
            table.Add(row);
        }
        return new StringTable(names, table);
    }

    static int U16(ReadOnlySpan<byte> b, ref int p)
    {
        if (p + 2 > b.Length) throw new InvalidDataException("Truncated string table.");
        int v = BinaryPrimitives.ReadUInt16LittleEndian(b[p..]); p += 2; return v;
    }

    static uint U32(ReadOnlySpan<byte> b, ref int p)
    {
        if (p + 4 > b.Length) throw new InvalidDataException("Truncated string table.");
        uint v = BinaryPrimitives.ReadUInt32LittleEndian(b[p..]); p += 4; return v;
    }

    static string Utf16(ReadOnlySpan<byte> b, ref int p, int chars)
    {
        if (p + chars * 2 > b.Length) throw new InvalidDataException("Truncated string table.");
        var s = Encoding.Unicode.GetString(b.Slice(p, chars * 2));
        p += chars * 2;
        return s;
    }
}

/// <summary>Quick looks at the engine's binary containers without a full parser.</summary>
public static class EngineFiles
{
    /// <summary>"GENOMFLE" files (templates, world files, library files): header summary and the string table at the end.</summary>
    public static string? DescribeGenome(ReadOnlySpan<byte> b, int maxStrings = 400)
    {
        if (b.Length < 14 || !b[..8].SequenceEqual("GENOMFLE"u8)) return null;
        var sb = new StringBuilder();
        uint tableOff = BinaryPrimitives.ReadUInt32LittleEndian(b[10..]);
        string kind = b.Length >= 18 && b.Slice(14, 4).SequenceEqual("GENO"u8) ? Encoding.ASCII.GetString(b.Slice(14, Math.Min(8, b.Length - 14))) : "stream";
        sb.AppendLine($"GENOMFLE container, version {BinaryPrimitives.ReadUInt16LittleEndian(b[8..])}, object: {kind}");
        sb.AppendLine($"string table at 0x{tableOff:x} of 0x{b.Length:x}");
        if (tableOff + 9 <= (uint)b.Length && BinaryPrimitives.ReadUInt32LittleEndian(b[(int)tableOff..]) == 0xDEADBEEF)
        {
            int p = (int)tableOff + 5;
            uint count = BinaryPrimitives.ReadUInt32LittleEndian(b[p..]);
            p += 4;
            sb.AppendLine($"{count} strings:");
            sb.AppendLine();
            for (uint i = 0; i < count && i < maxStrings && p + 2 <= b.Length; i++)
            {
                int n = BinaryPrimitives.ReadUInt16LittleEndian(b[p..]); p += 2;
                if (p + n > b.Length) break;
                sb.AppendLine($"{i,5}  {Encoding.Latin1.GetString(b.Slice(p, n))}");
                p += n;
            }
            if (count > maxStrings) sb.AppendLine($"... {count - maxStrings} more");
        }
        return sb.ToString();
    }

    /// <summary>"GR01" resource files (<c>._xmsh</c>, <c>._xmat</c>, <c>._ximg</c>, <c>._xsnd</c> ...): header and readable strings.</summary>
    public static string? DescribeResource(ReadOnlySpan<byte> b)
    {
        if (b.Length < 0x20 || !b[..4].SequenceEqual("GR01"u8)) return null;
        var sb = new StringBuilder();
        string kind = Encoding.ASCII.GetString(b.Slice(4, 4));
        sb.AppendLine($"GR01 resource, content type {kind}");
        sb.AppendLine($"header size 0x{BinaryPrimitives.ReadUInt32LittleEndian(b[8..]):x}, property stream {BinaryPrimitives.ReadUInt32LittleEndian(b[12..])} bytes, payload at 0x{BinaryPrimitives.ReadUInt32LittleEndian(b[16..]):x}");
        sb.AppendLine();
        sb.AppendLine("Readable strings in the first 64 KiB:");
        foreach (var (off, s) in AsciiRuns(b[..Math.Min(b.Length, 65536)], 5).Take(200))
            sb.AppendLine($"  0x{off:x6}  {s}");
        return sb.ToString();
    }

    public static IEnumerable<(int offset, string text)> AsciiRuns(ReadOnlySpan<byte> b, int minLength)
    {
        var list = new List<(int, string)>();
        int start = -1;
        for (int i = 0; i <= b.Length; i++)
        {
            bool ok = i < b.Length && b[i] >= 0x20 && b[i] < 0x7F;
            if (ok && start < 0) start = i;
            else if (!ok && start >= 0)
            {
                if (i - start >= minLength) list.Add((start, Encoding.ASCII.GetString(b[start..i])));
                start = -1;
            }
        }
        return list;
    }
}

public static class HexDump
{
    public static string Format(ReadOnlySpan<byte> b, long baseOffset = 0)
    {
        var sb = new StringBuilder(b.Length * 4 + 64);
        for (int i = 0; i < b.Length; i += 16)
        {
            sb.Append((baseOffset + i).ToString("x8")).Append("  ");
            for (int k = 0; k < 16; k++)
            {
                if (i + k < b.Length) sb.Append(b[i + k].ToString("x2")).Append(' '); else sb.Append("   ");
                if (k == 7) sb.Append(' ');
            }
            sb.Append(' ');
            for (int k = 0; k < 16 && i + k < b.Length; k++)
            {
                byte c = b[i + k];
                sb.Append(c is >= 0x20 and < 0x7F ? (char)c : '.');
            }
            sb.Append('\n');
        }
        return sb.ToString();
    }

    /// <summary>True when the bytes look like text (no NULs, mostly printable) — UTF-8, UTF-16 with BOM or Latin-1.</summary>
    public static bool LooksLikeText(ReadOnlySpan<byte> b)
    {
        if (b.Length == 0) return true;
        if (b.Length >= 2 && ((b[0] == 0xFF && b[1] == 0xFE) || (b[0] == 0xFE && b[1] == 0xFF))) return true;
        var n = Math.Min(b.Length, 4096);
        int bad = 0;
        for (int i = 0; i < n; i++)
        {
            byte c = b[i];
            if (c == 0) return false;
            if (c < 0x09 || (c > 0x0D && c < 0x20)) bad++;
        }
        return bad * 50 < n;
    }

    public static string DecodeText(ReadOnlySpan<byte> b)
    {
        if (b.Length >= 2 && b[0] == 0xFF && b[1] == 0xFE) return Encoding.Unicode.GetString(b[2..]);
        if (b.Length >= 2 && b[0] == 0xFE && b[1] == 0xFF) return Encoding.BigEndianUnicode.GetString(b[2..]);
        if (b.Length >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) return Encoding.UTF8.GetString(b[3..]);
        try { return new UTF8Encoding(false, true).GetString(b); }
        catch (DecoderFallbackException) { return Encoding.Latin1.GetString(b); }
    }
}
