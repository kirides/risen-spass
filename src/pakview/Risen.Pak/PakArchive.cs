using System.Buffers.Binary;
using System.IO.Compression;
using Microsoft.Win32.SafeHandles;

namespace Risen.Pak;

/// <summary>
/// A Risen G3V0 archive (*.pak, *.p00 ...): table of contents tree in memory, member bytes read on demand.
/// Edits (add / replace / rename / delete) only change the tree; <see cref="Save"/> writes a complete new file
/// next to the archive and switches it in atomically.
/// </summary>
public sealed partial class PakArchive : IDisposable
{
    SafeFileHandle? _handle;
    long _fileLength;
    int _changes;

    PakArchive() { Root = NewRoot(); }

    /// <summary>Full path of the file on disk; null for an archive that was never saved.</summary>
    public string? FilePath { get; private set; }
    public PakDirectory Root { get; private set; }
    public ulong HeaderFlags { get; private set; } = 1;
    public ulong HeaderReserved { get; private set; }
    public bool IsDirty => _changes > 0;
    public int ChangeCount => _changes;

    static PakDirectory NewRoot() => new("");

    public static PakArchive CreateNew() => new();

    public static PakArchive Open(string path)
    {
        var a = new PakArchive();
        a.Load(Path.GetFullPath(path));
        return a;
    }

    void Load(string path)
    {
        var handle = File.OpenHandle(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete, FileOptions.RandomAccess);
        try
        {
            long len = RandomAccess.GetLength(handle);
            if (len < PakFormat.HeaderSize) throw new PakException($"{Path.GetFileName(path)}: not a G3V0 archive (too short).");
            var head = new byte[PakFormat.HeaderSize];
            ReadAt(handle, head, 0);
            if (BinaryPrimitives.ReadUInt32LittleEndian(head) != 1 || !head.AsSpan(4, 4).SequenceEqual("G3V0"u8))
                throw new PakException($"{Path.GetFileName(path)}: not a G3V0 archive.");
            ulong reserved = BinaryPrimitives.ReadUInt64LittleEndian(head.AsSpan(8));
            ulong flags = BinaryPrimitives.ReadUInt64LittleEndian(head.AsSpan(0x10));
            ulong tocOff = BinaryPrimitives.ReadUInt64LittleEndian(head.AsSpan(0x20));
            if (tocOff < PakFormat.HeaderSize || tocOff > (ulong)len) throw new PakException($"{Path.GetFileName(path)}: the table of contents lies outside the file.");

            var toc = new byte[len - (long)tocOff];
            ReadAt(handle, toc, (long)tocOff);

            var root = NewRoot();
            var r = new TocReader(toc);
            uint rootName = r.U32();
            if (rootName != 0) r.Skip((int)rootName + 1);
            root.Created = r.U64(); root.Accessed = r.U64(); root.Written = r.U64();
            root.Attributes = root.InnerAttributes = r.U32();
            uint count = r.U32();
            ParseDir(root, count, ref r, 0, len);

            _handle?.Dispose();
            _handle = handle;
            _fileLength = len;
            Root = root;
            HeaderFlags = flags;
            HeaderReserved = reserved;
            FilePath = path;
            _changes = 0;
        }
        catch
        {
            handle.Dispose();
            throw;
        }
    }

    static void ParseDir(PakDirectory dir, uint count, ref TocReader r, int depth, long fileLength)
    {
        if (depth > 128) throw new PakException("The directory tree is nested too deeply.");
        if (count > r.Remaining / 13) throw new PakException("A directory claims more entries than the table of contents can hold.");
        for (uint i = 0; i < count; i++)
        {
            uint attr = r.U32();
            string name = r.Name(r.U32());
            if ((attr & 0x10) != 0)
            {
                var d = new PakDirectory(name) { Attributes = attr };
                d.Created = r.U64(); d.Accessed = r.U64(); d.Written = r.U64();
                d.InnerAttributes = r.U32();
                uint cnt = r.U32();
                dir.AddChild(d);
                ParseDir(d, cnt, ref r, depth + 1, fileLength);
            }
            else
            {
                var f = new PakFile(name) { Attributes = attr };
                ulong off = r.U64();
                f.Created = r.U64(); f.Accessed = r.U64(); f.Written = r.U64();
                f.InnerAttributes = r.U32();
                f.MemberFlags = r.U64();
                f.StoredSize = r.U32();
                f.Size = r.U32();
                if (off > (ulong)fileLength || off + f.StoredSize > (ulong)fileLength)
                    throw new PakException($"Member '{name}' lies outside the archive file.");
                f.Offset = (long)off;
                dir.AddChild(f);
            }
        }
    }

    static void ReadAt(SafeFileHandle h, Span<byte> buf, long offset)
    {
        int total = 0;
        while (total < buf.Length)
        {
            int n = RandomAccess.Read(h, buf[total..], offset + total);
            if (n == 0) throw new PakException("Unexpected end of file.");
            total += n;
        }
    }

    // ------------------------------------------------------------------------------------------ queries

    public IEnumerable<PakFile> AllFiles() => Root.Files();

    public PakNode? Find(string path)
    {
        PakNode cur = Root;
        foreach (var seg in path.Split('/', StringSplitOptions.RemoveEmptyEntries))
        {
            if (cur is not PakDirectory d || d.Find(seg) is not { } next) return null;
            cur = next;
        }
        return cur;
    }

    public static long TotalSize(PakNode n) => n is PakFile f ? f.Size : ((PakDirectory)n).Files().Sum(x => (long)x.Size);

    // ------------------------------------------------------------------------------------------ reading

    /// <summary>The (inflated) bytes of a member. New content comes from its source; other members from the archive file.</summary>
    public Stream OpenRead(PakFile f)
    {
        if (f.NewContent is { } c) return c.Open();
        var h = _handle ?? throw new ObjectDisposedException(nameof(PakArchive));
        Stream raw = new SliceStream(h, f.Offset, f.StoredSize);
        if (!f.IsCompressed) return raw;
        return new BoundedStream(new ZLibStream(raw, CompressionMode.Decompress), f.Size);
    }

    public byte[] ReadAll(PakFile f)
    {
        using var s = OpenRead(f);
        var buf = new byte[f.Size];
        s.ReadExactly(buf);
        return buf;
    }

    /// <summary>Reads up to <paramref name="max"/> bytes from the start of a member (preview).</summary>
    public byte[] ReadHead(PakFile f, int max)
    {
        using var s = OpenRead(f);
        var buf = new byte[Math.Min((long)max, f.Size)];
        int total = 0;
        while (total < buf.Length)
        {
            int n = s.Read(buf, total, buf.Length - total);
            if (n == 0) break;
            total += n;
        }
        return total == buf.Length ? buf : buf[..total];
    }

    /// <summary>Stops a zlib stream at the size the table of contents promises (a lying TOC must not inflate forever).</summary>
    sealed class BoundedStream(Stream inner, long limit) : Stream
    {
        readonly long _limit = limit;
        long _left = limit;
        public override bool CanRead => true;
        public override bool CanSeek => false;
        public override bool CanWrite => false;
        public override long Length => _limit;
        public override long Position { get => _limit - _left; set => throw new NotSupportedException(); }
        public override void Flush() { }
        public override int Read(byte[] buffer, int offset, int count) => Read(buffer.AsSpan(offset, count));
        public override int Read(Span<byte> buffer)
        {
            if (_left <= 0) return 0;
            if (buffer.Length > _left) buffer = buffer[..(int)_left];
            int n = inner.Read(buffer);
            _left -= n;
            return n;
        }
        public override long Seek(long offset, SeekOrigin origin) => throw new NotSupportedException();
        public override void SetLength(long value) => throw new NotSupportedException();
        public override void Write(byte[] buffer, int offset, int count) => throw new NotSupportedException();
        protected override void Dispose(bool disposing) { if (disposing) inner.Dispose(); base.Dispose(disposing); }
    }

    // ------------------------------------------------------------------------------------------ extraction

    /// <summary>Turns an archive path into a path below <paramref name="root"/>; null when it would escape or is not a legal name.</summary>
    public static string? SafeTargetPath(string root, string archivePath)
    {
        var full = Path.GetFullPath(root);
        var parts = archivePath.Split('/', StringSplitOptions.RemoveEmptyEntries);
        foreach (var p in parts)
            if (p is "." or ".." || p.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0) return null;
        var target = Path.GetFullPath(Path.Combine([full, .. parts]));
        var prefix = full.EndsWith(Path.DirectorySeparatorChar) ? full : full + Path.DirectorySeparatorChar;
        return target.StartsWith(prefix, StringComparison.OrdinalIgnoreCase) ? target : null;
    }

    public sealed record ExtractResult(int Files, int Skipped, long Bytes);

    /// <summary>Extracts nodes (files or whole directories) below <paramref name="destDir"/>, keeping their path relative to
    /// <paramref name="basePath"/>. Never writes outside destDir. Existing files are overwritten.</summary>
    public ExtractResult Extract(IEnumerable<PakNode> nodes, string destDir, string basePath = "",
        IProgress<(int done, int total)>? progress = null, CancellationToken ct = default)
    {
        var files = nodes.SelectMany(n => n is PakFile f ? [f] : ((PakDirectory)n).Files()).Distinct().ToList();
        int done = 0, skipped = 0; long bytes = 0;
        foreach (var f in files)
        {
            ct.ThrowIfCancellationRequested();
            var rel = f.Path;
            if (basePath.Length > 0 && rel.StartsWith(basePath + "/", StringComparison.OrdinalIgnoreCase)) rel = rel[(basePath.Length + 1)..];
            var target = SafeTargetPath(destDir, rel);
            if (target is null) { skipped++; progress?.Report((++done, files.Count)); continue; }
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            using (var src = OpenRead(f))
            using (var dst = new FileStream(target, FileMode.Create, FileAccess.Write, FileShare.None, 1 << 16))
                src.CopyTo(dst);
            if (f.WrittenTime is { } t) File.SetLastWriteTime(target, t);
            bytes += f.Size;
            progress?.Report((++done, files.Count));
        }
        return new ExtractResult(files.Count - skipped, skipped, bytes);
    }

    public void Dispose()
    {
        _handle?.Dispose();
        _handle = null;
    }
}
