using System.Buffers.Binary;
using System.Text;

namespace Risen.Pak;

public sealed class PakException(string message, Exception? inner = null) : IOException(message, inner);

public static class PakFormat
{
    public const int HeaderSize = 0x30;
    public const uint DirAttr = 0x20010;
    public const uint FileAttr = 0x20020;
    public const ulong CompressedFlag = 0x2_0000_0000;
    public static readonly Encoding NameEncoding = Encoding.Latin1;

    /// <summary>Checks a single path segment; returns null when fine, else the reason.</summary>
    public static string? ValidateName(string name)
    {
        if (string.IsNullOrWhiteSpace(name)) return "The name is empty.";
        if (name is "." or "..") return "'.' and '..' are not valid names.";
        if (name.IndexOfAny(['/', '\\', '\0', ':', '*', '?', '"', '<', '>', '|']) >= 0)
            return "A name must not contain / \\ : * ? \" < > |.";
        foreach (var c in name)
            if (c > 0xFF || char.IsControl(c)) return "The archive stores names as Latin-1 (no characters above U+00FF).";
        if (name.Length > 250) return "The name is too long.";
        return null;
    }
}

/// <summary>Bounds-checked little-endian reader over the table of contents.</summary>
internal ref struct TocReader(byte[] data)
{
    readonly byte[] _d = data;
    public int Pos;
    public readonly int Remaining => _d.Length - Pos;

    void Need(int n)
    {
        if (n < 0 || Pos + n > _d.Length) throw new PakException("The table of contents is truncated or damaged.");
    }

    public uint U32() { Need(4); var v = BinaryPrimitives.ReadUInt32LittleEndian(_d.AsSpan(Pos)); Pos += 4; return v; }
    public ulong U64() { Need(8); var v = BinaryPrimitives.ReadUInt64LittleEndian(_d.AsSpan(Pos)); Pos += 8; return v; }
    public void Skip(int n) { Need(n); Pos += n; }

    public string Name(uint len)
    {
        if (len > 4096) throw new PakException("A name in the table of contents is implausibly long.");
        Need((int)len + 1);
        var s = PakFormat.NameEncoding.GetString(_d, Pos, (int)len);
        Pos += (int)len + 1; // trailing NUL
        return s;
    }
}

/// <summary>A read-only window on a file handle (thread safe: positional reads).</summary>
internal sealed class SliceStream : Stream
{
    readonly Microsoft.Win32.SafeHandles.SafeFileHandle _h;
    readonly long _start, _length;
    long _pos;

    public SliceStream(Microsoft.Win32.SafeHandles.SafeFileHandle h, long start, long length)
    {
        _h = h; _start = start; _length = length;
    }

    public override bool CanRead => true;
    public override bool CanSeek => true;
    public override bool CanWrite => false;
    public override long Length => _length;
    public override long Position { get => _pos; set => _pos = value; }
    public override void Flush() { }

    public override int Read(byte[] buffer, int offset, int count) => Read(buffer.AsSpan(offset, count));

    public override int Read(Span<byte> buffer)
    {
        var left = _length - _pos;
        if (left <= 0) return 0;
        if (buffer.Length > left) buffer = buffer[..(int)left];
        int total = 0;
        while (total < buffer.Length)
        {
            int n = RandomAccess.Read(_h, buffer[total..], _start + _pos + total);
            if (n == 0) throw new PakException("The archive ends before the member does (truncated file?).");
            total += n;
        }
        _pos += total;
        return total;
    }

    public override long Seek(long offset, SeekOrigin origin)
    {
        _pos = origin switch { SeekOrigin.Begin => offset, SeekOrigin.Current => _pos + offset, _ => _length + offset };
        return _pos;
    }

    public override void SetLength(long value) => throw new NotSupportedException();
    public override void Write(byte[] buffer, int offset, int count) => throw new NotSupportedException();
}

/// <summary>Bytes that go into the archive on the next save.</summary>
public abstract class PakContent
{
    public abstract long Length { get; }
    public abstract Stream Open();
    public abstract DateTime LastWriteUtc { get; }
}

public sealed class MemoryContent(byte[] data, DateTime? when = null) : PakContent
{
    readonly DateTime _when = when ?? DateTime.UtcNow;
    public override long Length => data.Length;
    public override Stream Open() => new MemoryStream(data, writable: false);
    public override DateTime LastWriteUtc => _when;
}

/// <summary>A file on disk, read when the archive is saved. The size is checked again then.</summary>
public sealed class DiskContent : PakContent
{
    public string FilePath { get; }
    readonly long _length;
    readonly DateTime _when;

    public DiskContent(string path)
    {
        var fi = new FileInfo(path);
        if (!fi.Exists) throw new FileNotFoundException("File not found.", path);
        if (fi.Length >= uint.MaxValue) throw new PakException($"{fi.Name} is 4 GiB or larger; the archive format stores sizes in 32 bits.");
        FilePath = fi.FullName; _length = fi.Length; _when = fi.LastWriteTimeUtc;
    }

    public override long Length => _length;
    public override DateTime LastWriteUtc => _when;

    public override Stream Open()
    {
        var fs = new FileStream(FilePath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete, 1 << 16, FileOptions.SequentialScan);
        if (fs.Length != _length)
        {
            fs.Dispose();
            throw new PakException($"{FilePath} changed on disk since it was added (size {_length} -> {new FileInfo(FilePath).Length}). Add it again.");
        }
        return fs;
    }
}

public enum PakCompression { Auto, Never }

public sealed class SaveOptions
{
    /// <summary>Auto = deflate new/replaced members when that makes them smaller.</summary>
    public PakCompression Compression { get; set; } = PakCompression.Auto;
    /// <summary>Keep the previous archive as "name.bak" next to the new one.</summary>
    public bool KeepBackup { get; set; }
}
