using System.Buffers.Binary;
using System.IO.Compression;

namespace Risen.Pak;

public sealed partial class PakArchive
{
    /// <summary>Members above this size are stored as they are instead of being tried with deflate (kept in memory while compressing).</summary>
    const long CompressLimit = 256L << 20;

    sealed record Placed(long Offset, uint Stored, ulong Flags);

    /// <summary>
    /// Writes the archive with all edits into a temporary file in the target directory, checks it, then switches it in:
    /// <c>File.Replace</c> over an existing archive (NTFS ReplaceFile, rename(2) elsewhere), so a reader sees either the old
    /// or the new file, never a half-written one. A failure leaves the old archive untouched and removes the temp file.
    /// Afterwards the archive object is reloaded from the new file (nodes obtained earlier are stale).
    /// </summary>
    public void Save(string? destPath = null, SaveOptions? options = null, IProgress<double>? progress = null, CancellationToken ct = default)
    {
        options ??= new SaveOptions();
        var dest = Path.GetFullPath(destPath ?? FilePath ?? throw new PakException("No file name: use Save As."));
        var dir = Path.GetDirectoryName(dest)!;
        Directory.CreateDirectory(dir);
        var tmp = Path.Combine(dir, $".{Path.GetFileName(dest)}.{Guid.NewGuid():N}.tmp");

        try
        {
            var placed = WriteTemp(tmp, options, progress, ct);
            Verify(tmp, placed);
        }
        catch
        {
            TryDelete(tmp);
            throw;
        }

        // The old file must be closed for the switch on Windows (we may be reading from it); reopen it if the switch fails.
        var wasPath = FilePath;
        bool sameFile = wasPath is not null && string.Equals(Path.GetFullPath(wasPath), dest, StringComparison.OrdinalIgnoreCase);
        var oldHandle = _handle;
        if (sameFile) { _handle = null; oldHandle?.Dispose(); }
        try
        {
            if (File.Exists(dest))
            {
                string? backup = options.KeepBackup ? dest + ".bak" : null;
                File.Replace(tmp, dest, backup, ignoreMetadataErrors: true);
            }
            else
            {
                File.Move(tmp, dest);
            }
        }
        catch
        {
            TryDelete(tmp);
            if (sameFile && wasPath is not null)
            {
                try { ReopenHandleOnly(wasPath); } catch { /* the archive object is unusable then; the caller sees the original exception */ }
            }
            throw;
        }

        // Reload from the file that now exists: nodes, offsets and the dirty counter all start fresh.
        var oldRoot = Root;
        try
        {
            Load(dest);
        }
        catch
        {
            Root = oldRoot;
            throw;
        }
        if (!sameFile) oldHandle?.Dispose();
    }

    void ReopenHandleOnly(string path)
    {
        _handle = File.OpenHandle(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete, FileOptions.RandomAccess);
        _fileLength = RandomAccess.GetLength(_handle);
    }

    static void TryDelete(string path)
    {
        try { File.Delete(path); } catch { /* best effort */ }
    }

    Dictionary<PakFile, Placed> WriteTemp(string tmp, SaveOptions options, IProgress<double>? progress, CancellationToken ct)
    {
        var files = AllFiles().ToList();
        // Untouched members keep their physical order (sequential reads of the source); new ones follow.
        var ordered = files.Where(f => f.NewContent is null).OrderBy(f => f.Offset)
            .Concat(files.Where(f => f.NewContent is not null)).ToList();
        long totalBytes = Math.Max(1, ordered.Sum(f => f.NewContent is null ? (long)f.StoredSize : f.NewContent.Length));
        long doneBytes = 0;
        var placed = new Dictionary<PakFile, Placed>(files.Count);
        var shared = new Dictionary<(long, uint), Placed>();

        using var fs = new FileStream(tmp, FileMode.CreateNew, FileAccess.ReadWrite, FileShare.None, 1 << 16, FileOptions.None);
        fs.Position = PakFormat.HeaderSize;
        var buffer = new byte[1 << 20];

        foreach (var f in ordered)
        {
            ct.ThrowIfCancellationRequested();
            Placed p;
            if (f.NewContent is null)
            {
                // Members that shared one body in the source keep sharing it.
                if (f.StoredSize > 0 && shared.TryGetValue((f.Offset, f.StoredSize), out var prev)) { placed[f] = prev; continue; }
                long start = fs.Position;
                var h = _handle ?? throw new ObjectDisposedException(nameof(PakArchive));
                long left = f.StoredSize, pos = f.Offset;
                while (left > 0)
                {
                    int n = RandomAccess.Read(h, buffer.AsSpan(0, (int)Math.Min(left, buffer.Length)), pos);
                    if (n == 0) throw new PakException($"'{f.Path}': the source archive ends early.");
                    fs.Write(buffer, 0, n);
                    pos += n; left -= n; doneBytes += n;
                    progress?.Report((double)doneBytes / totalBytes);
                }
                p = new Placed(start, f.StoredSize, f.MemberFlags);
                if (f.StoredSize > 0) shared[(f.Offset, f.StoredSize)] = p;
            }
            else
            {
                long start = fs.Position;
                var c = f.NewContent;
                uint stored = WriteNew(fs, c, options, buffer, out bool deflated);
                p = new Placed(start, stored, deflated ? PakFormat.CompressedFlag : 0);
                doneBytes += c.Length;
                progress?.Report((double)doneBytes / totalBytes);
            }
            placed[f] = p;
        }

        long tocOffset = fs.Position;
        var toc = new MemoryStream();
        WriteToc(toc, placed);
        toc.Position = 0;
        toc.CopyTo(fs);
        long total = fs.Position;

        var head = new byte[PakFormat.HeaderSize];
        BinaryPrimitives.WriteUInt32LittleEndian(head, 1);
        "G3V0"u8.CopyTo(head.AsSpan(4));
        BinaryPrimitives.WriteUInt64LittleEndian(head.AsSpan(8), HeaderReserved);
        BinaryPrimitives.WriteUInt64LittleEndian(head.AsSpan(0x10), HeaderFlags);
        BinaryPrimitives.WriteUInt64LittleEndian(head.AsSpan(0x18), PakFormat.HeaderSize);
        BinaryPrimitives.WriteUInt64LittleEndian(head.AsSpan(0x20), (ulong)tocOffset);
        BinaryPrimitives.WriteUInt64LittleEndian(head.AsSpan(0x28), (ulong)total);
        fs.Position = 0;
        fs.Write(head);
        fs.Flush(flushToDisk: true); // on disk before the switch, or a power cut could leave an empty "new" archive
        progress?.Report(1);
        return placed;
    }

    /// <summary>Writes one new/replaced member, deflated when allowed and smaller. Returns the stored size.</summary>
    static uint WriteNew(FileStream fs, PakContent c, SaveOptions options, byte[] buffer, out bool deflated)
    {
        deflated = false;
        long len = c.Length;
        if (options.Compression == PakCompression.Auto && len > 0 && len <= CompressLimit)
        {
            byte[] raw = new byte[len];
            using (var s = c.Open()) s.ReadExactly(raw);
            using var ms = new MemoryStream();
            using (var z = new ZLibStream(ms, CompressionLevel.Optimal, leaveOpen: true)) z.Write(raw);
            if (ms.Length < len - len / 32) // at least ~3 % smaller, else not worth the inflate time at load
            {
                ms.Position = 0;
                ms.CopyTo(fs);
                deflated = true;
                return (uint)ms.Length;
            }
            fs.Write(raw);
            return (uint)len;
        }
        long written = 0;
        using var src = c.Open();
        int n;
        while ((n = src.Read(buffer, 0, buffer.Length)) > 0) { fs.Write(buffer, 0, n); written += n; }
        if (written != len) throw new PakException("The source changed while it was being written.");
        return (uint)written;
    }

    void WriteToc(Stream o, Dictionary<PakFile, Placed> placed)
    {
        var w = new BinaryWriter(o, PakFormat.NameEncoding, leaveOpen: true);
        w.Write(0u);
        w.Write(Root.Created); w.Write(Root.Accessed); w.Write(Root.Written);
        w.Write(Root.Attributes);
        WriteChildren(w, Root, placed);
        w.Flush();
    }

    static void WriteChildren(BinaryWriter w, PakDirectory dir, Dictionary<PakFile, Placed> placed)
    {
        w.Write((uint)dir.Children.Count);
        foreach (var n in dir.Children)
        {
            w.Write(n.Attributes);
            var name = PakFormat.NameEncoding.GetBytes(n.Name);
            w.Write((uint)name.Length);
            w.Write(name);
            w.Write((byte)0);
            if (n is PakDirectory d)
            {
                w.Write(d.Created); w.Write(d.Accessed); w.Write(d.Written);
                w.Write(d.InnerAttributes);
                WriteChildren(w, d, placed);
            }
            else
            {
                var f = (PakFile)n;
                var p = placed[f];
                w.Write((ulong)p.Offset);
                w.Write(f.Created); w.Write(f.Accessed); w.Write(f.Written);
                w.Write(f.InnerAttributes);
                w.Write(p.Flags);
                w.Write(p.Stored);
                w.Write(f.Size);
            }
        }
    }

    /// <summary>Re-reads the temp file and compares it with the plan; new members are inflated and measured.</summary>
    void Verify(string tmp, Dictionary<PakFile, Placed> placed)
    {
        using var check = Open(tmp);
        var expected = AllFiles().ToDictionary(f => f.Path, StringComparer.OrdinalIgnoreCase);
        int seen = 0;
        foreach (var f in check.AllFiles())
        {
            seen++;
            if (!expected.TryGetValue(f.Path, out var e) || e.Size != f.Size)
                throw new PakException($"Verification failed: '{f.Path}' differs from the plan. The old archive was not touched.");
            if (e.NewContent is not null)
            {
                using var s = check.OpenRead(f);
                long n = 0; var buf = new byte[1 << 16]; int r;
                while ((r = s.Read(buf, 0, buf.Length)) > 0) n += r;
                if (n != f.Size) throw new PakException($"Verification failed: '{f.Path}' does not read back. The old archive was not touched.");
            }
        }
        if (seen != expected.Count) throw new PakException("Verification failed: the file count differs. The old archive was not touched.");
    }
}
