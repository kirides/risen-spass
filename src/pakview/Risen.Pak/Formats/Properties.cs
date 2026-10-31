using System.Buffers.Binary;
using System.Globalization;
using System.Text;
using System.Text.RegularExpressions;

namespace Risen.Pak.Formats;

/// <summary>One property of a template or material.</summary>
public sealed record PropRow(string Group, string Name, string Type, string Value);

/// <summary>What the preview shows for an engine file made of properties: a few header lines and the property list.</summary>
public sealed record PropertyTable(string Kind, List<string> Notes, List<PropRow> Rows);

/// <summary>Formats a raw property payload by its type name (shared by templates and materials).</summary>
static class PropValue
{
    static readonly CultureInfo Inv = CultureInfo.InvariantCulture;

    public static string Float(float f) => f.ToString("0.#####", Inv);

    public static string Floats(ReadOnlySpan<byte> p)
    {
        var sb = new StringBuilder("(");
        for (int i = 0; i + 4 <= p.Length; i += 4)
        {
            if (i > 0) sb.Append(", ");
            sb.Append(Float(BinaryPrimitives.ReadSingleLittleEndian(p[i..])));
        }
        return sb.Append(')').ToString();
    }

    /// <summary>Numbers, vectors, colours, enums. Returns null when the type/size is not one of the known shapes.</summary>
    public static string? Scalar(string type, ReadOnlySpan<byte> p)
    {
        int n = p.Length;
        switch (type)
        {
            case "int" or "long" when n == 4: return BinaryPrimitives.ReadInt32LittleEndian(p).ToString(Inv);
            case "unsigned long" or "unsigned int" when n == 4: return BinaryPrimitives.ReadUInt32LittleEndian(p).ToString(Inv);
            case "short" when n == 2: return BinaryPrimitives.ReadInt16LittleEndian(p).ToString(Inv);
            case "float" when n == 4: return Float(BinaryPrimitives.ReadSingleLittleEndian(p));
            case "bool" when n == 1: return p[0] != 0 ? "true" : "false";
        }
        if (type is "bCVector" or "bCFloatColor" or "bCEulerAngles" or "bCQuaternion" or "bCFloatAlphaColor" or "bCRange1"
            or "bCRange3" or "bCVector2" or "bCBox" or "bCMatrix" && n % 4 == 0)
            return Floats(p);
        if (type.StartsWith("bTPropertyContainer<enum", StringComparison.Ordinal) && n == 6)
        {
            uint v = BinaryPrimitives.ReadUInt32LittleEndian(p[2..]);
            return $"{v} (0x{v:x})";
        }
        return null;
    }

    public static string Hex(ReadOnlySpan<byte> p) =>
        p.Length == 0 ? "(empty)" : (p.Length > 32 ? Convert.ToHexString(p[..32]).ToLowerInvariant() + $"... ({p.Length} bytes)" : Convert.ToHexString(p).ToLowerInvariant());
}

/// <summary>
/// Entity templates (<c>*.tple</c>, "GENOMFLE" + "GENOMETP"). A tolerant scanner like <c>tools/g3tple.py</c> (RE.md section 43):
/// the property data refers to a string table at the end of the file; property sets start after a fixed six byte marker.
/// </summary>
public static class TemplateReader
{
    static readonly HashSet<string> Prims =
    [
        "int", "long", "short", "float", "bool", "bCVector", "bCFloatColor", "bCString", "unsigned long", "unsigned int", "char",
        "eCTemplateEntityProxy", "eCEntityProxy", "eCGuiBitmapProxy2", "eCScriptProxyScript", "gCScriptProxyAIState",
        "gCScriptProxyAIFunction", "gCScriptProxyAICallback", "bCMatrix", "bCQuaternion", "bCBox", "bCRect", "bCEulerAngles",
        "bCFloatAlphaColor", "bCImageResourceString", "bCRange1", "bCRange3", "bCVector2", "bCImageOrMaterialResourceString",
        "bCMeshResourceString", "bCSpeedTreeResourceString",
    ];

    static readonly HashSet<string> StringTypes =
    [
        "bCString", "eCScriptProxyScript", "gCScriptProxyAIState", "gCScriptProxyAIFunction", "gCScriptProxyAICallback",
        "eCTemplateEntityProxy", "eCEntityProxy", "eCGuiBitmapProxy2", "bCImageResourceString", "bCImageOrMaterialResourceString",
        "bCMeshResourceString", "bCSpeedTreeResourceString",
    ];

    static ReadOnlySpan<byte> Marker => [0x01, 0x00, 0x01, 0x01, 0x00, 0x01];

    public static bool IsTemplate(ReadOnlySpan<byte> b) =>
        b.Length > 0x30 && b[..8].SequenceEqual("GENOMFLE"u8) && b.Slice(14, 8).SequenceEqual("GENOMETP"u8);

    public static PropertyTable Read(byte[] b)
    {
        if (!IsTemplate(b)) throw new InvalidDataException("Not a GENOMETP template.");
        uint tbl = BinaryPrimitives.ReadUInt32LittleEndian(b.AsSpan(10));
        if (tbl + 9 > b.Length || BinaryPrimitives.ReadUInt32LittleEndian(b.AsSpan((int)tbl)) != 0xDEADBEEF)
            throw new InvalidDataException("The string table is missing.");
        uint count = BinaryPrimitives.ReadUInt32LittleEndian(b.AsSpan((int)tbl + 5));
        var strs = new List<string>();
        int q = (int)tbl + 9;
        for (uint i = 0; i < count && q + 2 <= b.Length; i++)
        {
            int len = BinaryPrimitives.ReadUInt16LittleEndian(b.AsSpan(q)); q += 2;
            if (q + len > b.Length) break;
            strs.Add(Encoding.Latin1.GetString(b, q, len)); q += len;
        }
        int n = strs.Count, end = (int)tbl;

        var notes = new List<string> { $"Template {(n > 0 ? strs[0] : "?")}", $"Entity {(n > 1 ? strs[1] : "?")}" };
        var guids = new List<string>();
        for (int i = 0x1c; i < Math.Min(end, 0x200) && i + 18 <= b.Length; i++)
            if (b[i] == 0xDB && b[i + 1] == 0) guids.Add("@" + Convert.ToHexString(b.AsSpan(i + 2, 16)).ToLowerInvariant());
        if (guids.Count > 0) notes.Add($"Reference id (how other templates name this one): {(guids.Count > 1 ? guids[1] : guids[0])}");

        // where the property sets start
        var psAt = new SortedDictionary<int, string>();
        for (int i = 0x2e; i + 8 <= end;)
        {
            int k = b.AsSpan(i, end - i).IndexOf(Marker);
            if (k < 0) break;
            i += k;
            if (i + 8 <= end)
            {
                int idx = BinaryPrimitives.ReadUInt16LittleEndian(b.AsSpan(i + 6));
                if (idx < n && (strs[idx].EndsWith("_PS", StringComparison.Ordinal) || strs[idx].StartsWith("eC") || strs[idx].StartsWith("gC")))
                    psAt[i] = strs[idx];
            }
            i++;
        }
        var pos = psAt.Keys.ToList();

        var rows = new List<PropRow>();
        string cur = "";
        int pi = 0, p = 0x2e;
        var stack = new List<(int end, string name)>();
        while (p + 10 <= end && rows.Count < 50000)
        {
            while (stack.Count > 0 && p >= stack[^1].end) stack.RemoveAt(stack.Count - 1);
            while (pi < pos.Count && pos[pi] <= p) { cur = psAt[pos[pi]]; pi++; }
            int ni = BinaryPrimitives.ReadUInt16LittleEndian(b.AsSpan(p));
            int ti = BinaryPrimitives.ReadUInt16LittleEndian(b.AsSpan(p + 2));
            int mk = BinaryPrimitives.ReadUInt16LittleEndian(b.AsSpan(p + 4));
            uint sz = BinaryPrimitives.ReadUInt32LittleEndian(b.AsSpan(p + 6));
            if (mk == 0x1E && ni < n && ti < n && sz <= (uint)(end - p - 10)
                && !strs[ni].StartsWith("bT") && !strs[ni].StartsWith("bC")
                && (Prims.Contains(strs[ti]) || strs[ti].StartsWith("bTPropertyContainer<") || strs[ti].StartsWith("bTObjArray")
                    || strs[ti].StartsWith("bTValArray") || strs[ti].StartsWith("class ") || strs[ti].StartsWith("gC") || strs[ti].StartsWith("eC")))
            {
                string typ = strs[ti];
                if (Prims.Contains(typ) || typ.StartsWith("bTPropertyContainer<"))
                {
                    var payload = b.AsSpan(p + 10, (int)sz);
                    string name = string.Concat(stack.Select(s => s.name + ".")) + strs[ni];
                    rows.Add(new PropRow(cur, name, typ, Decode(typ, payload, strs)));
                    p += 10 + (int)sz;
                    continue;
                }
                stack.Add((p + 10 + (int)sz, strs[ni] + "[]"));
                p += 10;
                continue;
            }
            p++;
        }
        notes.Add($"{rows.Count} properties in {rows.Select(r => r.Group).Distinct().Count()} property sets");
        return new PropertyTable("Template", notes, rows);
    }

    static string Decode(string typ, ReadOnlySpan<byte> p, List<string> strs)
    {
        if (PropValue.Scalar(typ, p) is { } s) return s;
        if (StringTypes.Contains(typ))
        {
            int i;
            switch (p.Length)
            {
                case 2: i = BinaryPrimitives.ReadUInt16LittleEndian(p); break;
                case 3: return "";
                case 5 when p[2] == 1: i = BinaryPrimitives.ReadUInt16LittleEndian(p[3..]); break;
                case 23 when p[2] == 1: return "@" + Convert.ToHexString(p.Slice(3, 16)).ToLowerInvariant();
                default: return PropValue.Hex(p);
            }
            return i == 0xFFFF ? "" : i < strs.Count ? strs[i] : $"#{i}";
        }
        return PropValue.Hex(p);
    }
}

/// <summary>
/// Compiled materials (<c>._xmat</c>, "GR01" "SM01"): a 0x28 byte header and property objects with inline strings,
/// as in <c>tools/g3mat.py</c> (RE.md section 60).
/// </summary>
public static partial class MaterialReader
{
    static readonly byte[] Hdr = [0x01, 0x00, 0x01, 0x01, 0x00, 0x01];

    [GeneratedRegex(@"\x01\x00\x01\x01\x00\x01[\x08-\x40]\x00(?:eC|gC)[A-Za-z0-9_]+\x01\x00\x00", RegexOptions.Singleline)]
    private static partial Regex NextObject();

    public static bool IsMaterial(ReadOnlySpan<byte> b) => b.Length > 0x28 && b[..8].SequenceEqual("GR01SM01"u8);

    static ushort U16(byte[] b, int p) => p + 2 > b.Length ? throw new InvalidDataException("Truncated material.") : BinaryPrimitives.ReadUInt16LittleEndian(b.AsSpan(p));
    static uint U32(byte[] b, int p) => p + 4 > b.Length ? throw new InvalidDataException("Truncated material.") : BinaryPrimitives.ReadUInt32LittleEndian(b.AsSpan(p));

    static string Str(byte[] b, int p, out int next)
    {
        int n = U16(b, p);
        if (p + 2 + n > b.Length) throw new InvalidDataException("Truncated material.");
        next = p + 2 + n;
        return Encoding.Latin1.GetString(b, p + 2, n);
    }

    public static PropertyTable Read(byte[] b)
    {
        if (!IsMaterial(b)) throw new InvalidDataException("Not a GR01/SM01 material.");
        var rows = new List<PropRow>();
        var notes = new List<string>();
        var images = new List<string>();
        var text = Encoding.Latin1.GetString(b);
        var next = NextObject();
        int p = 0x28, objects = 0;
        while (p < b.Length)
        {
            if (!b.AsSpan(p, Math.Min(6, b.Length - p)).SequenceEqual(Hdr)) throw new InvalidDataException($"No object header at 0x{p:x}.");
            string cls = Str(b, p + 6, out int q);
            if (!b.AsSpan(q, 3).SequenceEqual("\x01\x00\x00"u8.ToArray())) throw new InvalidDataException($"Bad class record at 0x{q:x}.");
            int version = U16(b, q + 3);
            q += 5;
            if (U16(b, q) != 0xC9) throw new InvalidDataException("Object body marker missing.");
            int bodyEnd = q + 6 + (int)U32(b, q + 2);
            q += 6;
            if (U16(b, q) != 0xC9) throw new InvalidDataException("Property list marker missing.");
            uint count = U32(b, q + 2);
            q += 6;
            string group = $"{cls} (v{version})";
            for (uint i = 0; i < count; i++)
            {
                string name = Str(b, q, out q);
                string typ = Str(b, q, out q);
                if (U16(b, q) != 0x1E) throw new InvalidDataException($"Property {name}: marker missing.");
                int size = (int)U32(b, q + 2);
                int s = q + 6;
                if (s + size > b.Length) throw new InvalidDataException("Truncated material.");
                q = s + size;
                string value;
                if (typ is "bCImageResourceString" or "bCString" or "bCImageOrMaterialResourceString" or "bCMeshResourceString" && size >= 2)
                {
                    value = Str(b, s, out _);
                    if (typ == "bCImageResourceString" && value.Length > 0) images.Add(value);
                }
                else value = PropValue.Scalar(typ, b.AsSpan(s, size)) ?? PropValue.Hex(b.AsSpan(s, size));
                rows.Add(new PropRow(group, name, typ, value));
            }
            objects++;
            var m = next.Match(text, Math.Max(q, 0));
            p = m.Success ? m.Index : b.Length;
        }
        notes.Add($"{objects} objects (material, shader and sampler nodes), {rows.Count} properties");
        if (images.Count > 0) notes.Add("Images used: " + string.Join(", ", images.Distinct()));
        return new PropertyTable("Material", notes, rows);
    }
}
