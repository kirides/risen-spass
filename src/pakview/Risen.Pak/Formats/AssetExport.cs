using System.Buffers.Binary;
using System.IO.Compression;

namespace Risen.Pak.Formats;

public enum ExportFormat { Mp3, Wav, Dds, Tga, Png }

/// <summary>
/// "Extract as": turns the engine's compiled <c>._xsnd</c> / <c>._ximg</c> files back into ordinary files.
/// MP3 and DDS are the stored payload (byte for byte what the game plays / loads); WAV, TGA and PNG are decoded.
/// </summary>
public static class AssetExport
{
    public static bool IsSound(string name) => name.EndsWith("._xsnd", StringComparison.OrdinalIgnoreCase);
    public static bool IsImage(string name) => name.EndsWith("._ximg", StringComparison.OrdinalIgnoreCase);

    public static bool CanConvert(string name, ExportFormat f) => f switch
    {
        ExportFormat.Mp3 or ExportFormat.Wav => IsSound(name),
        _ => IsImage(name),
    };

    public static string Extension(ExportFormat f) => f switch
    {
        ExportFormat.Mp3 => ".mp3", ExportFormat.Wav => ".wav", ExportFormat.Dds => ".dds", ExportFormat.Tga => ".tga", _ => ".png",
    };

    /// <summary>"Name._xsnd" -> "Name.mp3".</summary>
    public static string TargetName(string name, ExportFormat f)
    {
        int i = name.LastIndexOf("._x", StringComparison.OrdinalIgnoreCase);
        return (i > 0 ? name[..i] : name) + Extension(f);
    }

    public static byte[] Convert(byte[] file, ExportFormat f) => f switch
    {
        ExportFormat.Mp3 => Mp3(file),
        ExportFormat.Wav => Wav(file),
        ExportFormat.Dds => DdsFile(file),
        ExportFormat.Tga => Tga(Decode(file)),
        _ => Png(Decode(file)),
    };

    /// <summary>
    /// What a file can be extracted as, judged from its first bytes (at least ~1 KiB): a sound offers the format of the audio it
    /// holds (MP3 -> only MP3, no pointless "decoded WAV" of an MP3); a texture offers DDS (as stored), TGA and PNG.
    /// Anything else, or a sound in a format the tool cannot name, offers nothing.
    /// </summary>
    public static IReadOnlyList<ExportFormat> AvailableFormats(string name, ReadOnlySpan<byte> head)
    {
        if (IsSound(name))
        {
            if (head.Length < 0x18 || !head[..4].SequenceEqual("GR01"u8)) return [];
            uint off = BinaryPrimitives.ReadUInt32LittleEndian(head[0x10..]);
            if (off >= (uint)head.Length) return [];
            return AudioReader.Sniff(head[(int)off..]) switch { "mp3" => [ExportFormat.Mp3], "wav" => [ExportFormat.Wav], _ => [] };
        }
        if (IsImage(name)) return Dds.FindPayload(head) >= 0 ? [ExportFormat.Dds, ExportFormat.Tga, ExportFormat.Png] : [];
        return [];
    }

    // ------------------------------------------------------------------------------------------ sounds

    static byte[] Mp3(byte[] file)
    {
        var payload = AudioReader.Payload(file);
        if (AudioReader.Sniff(payload.Span) != "mp3") throw new NotSupportedException("The sound does not contain MP3 data.");
        return payload.ToArray();
    }

    /// <summary>The stored WAV, or (for other audio) 16-bit PCM WAV of the decoded sound.</summary>
    static byte[] Wav(byte[] file)
    {
        var payload = AudioReader.Payload(file);
        if (AudioReader.Sniff(payload.Span) == "wav") return payload.ToArray();
        using var s = AudioReader.Open(file, out _);
        int ch = s.Channels;
        var pcm = new MemoryStream();
        var buf = new float[8192 * ch];
        int n;
        Span<byte> two = stackalloc byte[2];
        while ((n = s.Read(buf, 0, buf.Length)) > 0)
            for (int i = 0; i < n; i++)
            {
                BinaryPrimitives.WriteInt16LittleEndian(two, (short)Math.Clamp((int)MathF.Round(buf[i] * 32767f), -32768, 32767));
                pcm.Write(two);
            }
        var o = new MemoryStream();
        var w = new BinaryWriter(o);
        w.Write("RIFF"u8.ToArray()); w.Write((int)(36 + pcm.Length)); w.Write("WAVEfmt "u8.ToArray());
        w.Write(16); w.Write((short)1); w.Write((short)ch); w.Write(s.SampleRate); w.Write(s.SampleRate * ch * 2); w.Write((short)(ch * 2)); w.Write((short)16);
        w.Write("data"u8.ToArray()); w.Write((int)pcm.Length);
        pcm.Position = 0; pcm.CopyTo(o);
        return o.ToArray();
    }

    // ------------------------------------------------------------------------------------------ images

    static byte[] DdsFile(byte[] file)
    {
        int off = Dds.FindPayload(file);
        if (off < 0) throw new InvalidDataException("No DDS data found in this image file.");
        return off == 0 ? file : file.AsSpan(off).ToArray();
    }

    static Bitmap32 Decode(byte[] file)
    {
        int off = Dds.FindPayload(file);
        if (off < 0) throw new InvalidDataException("No DDS data found in this image file.");
        return Dds.Decode(file, off);
    }

    /// <summary>Uncompressed 32-bit TGA, origin top left, with alpha.</summary>
    public static byte[] Tga(Bitmap32 b)
    {
        var o = new byte[18 + b.Bgra.Length];
        o[2] = 2;                                   // uncompressed true colour
        BinaryPrimitives.WriteUInt16LittleEndian(o.AsSpan(12), (ushort)b.Width);
        BinaryPrimitives.WriteUInt16LittleEndian(o.AsSpan(14), (ushort)b.Height);
        o[16] = 32;
        o[17] = 0x28;                               // 8 alpha bits, top-left origin
        b.Bgra.CopyTo(o, 18);
        return o;
    }

    /// <summary>RGBA PNG (filter 0, one IDAT).</summary>
    public static byte[] Png(Bitmap32 b)
    {
        int stride = b.Width * 4;
        var raw = new byte[(stride + 1) * b.Height];
        for (int y = 0; y < b.Height; y++)
        {
            int d = y * (stride + 1) + 1;           // filter byte 0 in front of each row
            for (int x = 0; x < b.Width; x++)
            {
                int s = y * stride + x * 4;
                raw[d + x * 4] = b.Bgra[s + 2]; raw[d + x * 4 + 1] = b.Bgra[s + 1]; raw[d + x * 4 + 2] = b.Bgra[s]; raw[d + x * 4 + 3] = b.Bgra[s + 3];
            }
        }
        var z = new MemoryStream();
        using (var zs = new ZLibStream(z, CompressionLevel.Optimal, leaveOpen: true)) zs.Write(raw);

        var o = new MemoryStream();
        o.Write([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]);
        var ihdr = new byte[13];
        BinaryPrimitives.WriteUInt32BigEndian(ihdr, (uint)b.Width);
        BinaryPrimitives.WriteUInt32BigEndian(ihdr.AsSpan(4), (uint)b.Height);
        ihdr[8] = 8; ihdr[9] = 6;                   // 8 bit, RGBA
        Chunk(o, "IHDR"u8, ihdr);
        Chunk(o, "IDAT"u8, z.ToArray());
        Chunk(o, "IEND"u8, []);
        return o.ToArray();
    }

    static void Chunk(Stream o, ReadOnlySpan<byte> type, byte[] data)
    {
        Span<byte> len = stackalloc byte[4];
        BinaryPrimitives.WriteUInt32BigEndian(len, (uint)data.Length);
        o.Write(len);
        o.Write(type);
        o.Write(data);
        uint crc = Crc32(Crc32(0, type), data);
        BinaryPrimitives.WriteUInt32BigEndian(len, crc);
        o.Write(len);
    }

    static readonly uint[] CrcTable = Enumerable.Range(0, 256).Select(n =>
    {
        uint c = (uint)n;
        for (int k = 0; k < 8; k++) c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        return c;
    }).ToArray();

    /// <summary>CRC-32 (PNG / zlib) of a block; exposed for tests.</summary>
    public static uint Crc32Of(ReadOnlySpan<byte> data) => Crc32(0, data);

    static uint Crc32(uint crc, ReadOnlySpan<byte> data)
    {
        crc ^= 0xFFFFFFFFu;
        foreach (var b in data) crc = CrcTable[(crc ^ b) & 0xFF] ^ (crc >> 8);
        return crc ^ 0xFFFFFFFFu;
    }
}

/// <summary>Result of <see cref="PakArchiveExport.ExtractAs"/>.</summary>
public sealed record ConvertResult(int Written, int NotApplicable, int Failed, long Bytes, List<string> Errors);

public static class PakArchiveExport
{
    /// <summary>
    /// The formats "Extract as" should offer for a selection: the union over its files (folders are walked), in the order
    /// MP3, WAV, DDS, TGA, PNG. Only the first <paramref name="limit"/> sounds / images are looked at (a whole archive selected).
    /// </summary>
    public static List<ExportFormat> FormatsFor(this PakArchive archive, IEnumerable<PakNode> nodes, int limit = 2000)
    {
        var found = new HashSet<ExportFormat>();
        int looked = 0;
        foreach (var f in nodes.SelectMany(n => n is PakFile pf ? [pf] : ((PakDirectory)n).Files()))
        {
            if (!AssetExport.IsSound(f.Name) && !AssetExport.IsImage(f.Name)) continue;
            if (++looked > limit) break;
            try { foreach (var fmt in AssetExport.AvailableFormats(f.Name, archive.ReadHead(f, 1024))) found.Add(fmt); }
            catch (IOException) { /* unreadable member: offers nothing */ }
            if (found.Count == 5) break;   // MP3, WAV, DDS, TGA, PNG: nothing more to find
        }
        return found.OrderBy(x => (int)x).ToList();
    }

    /// <summary>
    /// Extracts the files among <paramref name="nodes"/> (directories are walked) that <paramref name="format"/> applies to,
    /// converted, below <paramref name="destDir"/> with their path relative to <paramref name="basePath"/>.
    /// Other files are counted, not written. Never writes outside destDir; existing files are overwritten.
    /// </summary>
    public static ConvertResult ExtractAs(this PakArchive archive, IEnumerable<PakNode> nodes, string destDir, ExportFormat format,
        string basePath = "", IProgress<(int done, int total)>? progress = null, CancellationToken ct = default)
    {
        var all = nodes.SelectMany(n => n is PakFile f ? [f] : ((PakDirectory)n).Files()).Distinct().ToList();
        var todo = all.Where(f => AssetExport.CanConvert(f.Name, format)).ToList();
        int written = 0, failed = 0, done = 0; long bytes = 0;
        var errors = new List<string>();
        foreach (var f in todo)
        {
            ct.ThrowIfCancellationRequested();
            try
            {
                var rel = f.Path;
                if (basePath.Length > 0 && rel.StartsWith(basePath + "/", StringComparison.OrdinalIgnoreCase)) rel = rel[(basePath.Length + 1)..];
                var dir = rel.Contains('/') ? rel[..rel.LastIndexOf('/')] + "/" : "";
                var target = PakArchive.SafeTargetPath(destDir, dir + AssetExport.TargetName(f.Name, format));
                if (target is null) throw new InvalidDataException("unsafe name");
                var data = AssetExport.Convert(archive.ReadAll(f), format);
                Directory.CreateDirectory(Path.GetDirectoryName(target)!);
                File.WriteAllBytes(target, data);
                if (f.WrittenTime is { } t) File.SetLastWriteTime(target, t);
                bytes += data.Length; written++;
            }
            catch (Exception ex) when (ex is IOException or InvalidDataException or NotSupportedException or UnauthorizedAccessException)
            {
                failed++;
                if (errors.Count < 20) errors.Add($"{f.Path}: {ex.Message}");
            }
            progress?.Report((++done, todo.Count));
        }
        return new ConvertResult(written, all.Count - todo.Count, failed, bytes, errors);
    }
}
