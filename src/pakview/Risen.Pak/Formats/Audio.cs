using System.Buffers.Binary;
using NLayer;

namespace Risen.Pak.Formats;

/// <summary>Decoded audio as 32-bit float samples, read sequentially, with seeking.</summary>
public interface IAudioStream : IDisposable
{
    int SampleRate { get; }
    int Channels { get; }
    TimeSpan Duration { get; }
    TimeSpan Position { get; set; }
    /// <summary>Fills up to <paramref name="count"/> interleaved samples; 0 at the end.</summary>
    int Read(float[] buffer, int offset, int count);
}

/// <summary>What the preview needs to show a sound without playing it.</summary>
public sealed record AudioInfo(string Format, int SampleRate, int Channels, TimeSpan Duration, float[] Peaks);

/// <summary>
/// Sounds of the engine: compiled <c>._xsnd</c> ("GR01" "SN04": a property header, then the audio file as it was imported — in
/// Risen's archives always MP3), plus plain .mp3 / PCM .wav files.
/// </summary>
public static class AudioReader
{
    public static bool IsAudioName(string name) =>
        name.EndsWith("._xsnd", StringComparison.OrdinalIgnoreCase) || name.EndsWith(".mp3", StringComparison.OrdinalIgnoreCase)
        || name.EndsWith(".wav", StringComparison.OrdinalIgnoreCase);

    /// <summary>The audio bytes inside a file: the payload of a GR01 sound, otherwise the file itself.</summary>
    public static ReadOnlyMemory<byte> Payload(byte[] file)
    {
        if (file.Length > 0x20 && file.AsSpan(0, 4).SequenceEqual("GR01"u8))
        {
            uint off = BinaryPrimitives.ReadUInt32LittleEndian(file.AsSpan(0x10));
            uint size = BinaryPrimitives.ReadUInt32LittleEndian(file.AsSpan(0x14));
            if (off < file.Length && size <= file.Length - off) return file.AsMemory((int)off, (int)size);
            throw new InvalidDataException("The sound payload lies outside the file.");
        }
        return file;
    }

    public static string Sniff(ReadOnlySpan<byte> p)
    {
        if (p.Length >= 12 && p[..4].SequenceEqual("RIFF"u8) && p.Slice(8, 4).SequenceEqual("WAVE"u8)) return "wav";
        if (p.Length >= 4 && p[..4].SequenceEqual("OggS"u8)) return "ogg";
        if (p.Length >= 3 && p[..3].SequenceEqual("ID3"u8)) return "mp3";
        if (p.Length >= 2 && p[0] == 0xFF && (p[1] & 0xE0) == 0xE0) return "mp3";
        return "unknown";
    }

    /// <summary>Opens a decoder over the audio of <paramref name="file"/>. Throws <see cref="NotSupportedException"/> for other formats.</summary>
    public static IAudioStream Open(byte[] file, out string format)
    {
        var payload = Payload(file);
        format = Sniff(payload.Span);
        return format switch
        {
            "mp3" => new Mp3Stream(payload),
            "wav" => WavStream.Create(payload.Span),
            "ogg" => throw new NotSupportedException("Ogg Vorbis is not supported by the preview."),
            _ => throw new NotSupportedException("Unknown audio format."),
        };
    }

    /// <summary>Format, length and a waveform outline (max amplitude per bucket) from one decoding pass.</summary>
    public static AudioInfo Analyze(byte[] file, int buckets = 600, CancellationToken ct = default)
    {
        using var s = Open(file, out var format);
        const int Chunk = 256;
        var peaksPerChunk = new List<float>();
        var buf = new float[Chunk * Math.Max(1, s.Channels)];
        long total = 0;
        int n;
        while ((n = s.Read(buf, 0, buf.Length)) > 0)
        {
            ct.ThrowIfCancellationRequested();
            float max = 0;
            for (int i = 0; i < n; i++) max = Math.Max(max, Math.Abs(buf[i]));
            peaksPerChunk.Add(max);
            total += n;
        }
        var peaks = new float[buckets];
        if (peaksPerChunk.Count > 0)
            for (int b = 0; b < buckets; b++)
            {
                int from = (int)((long)b * peaksPerChunk.Count / buckets);
                int to = Math.Max(from + 1, (int)((long)(b + 1) * peaksPerChunk.Count / buckets));
                float m = 0;
                for (int i = from; i < to && i < peaksPerChunk.Count; i++) m = Math.Max(m, peaksPerChunk[i]);
                peaks[b] = m;
            }
        var duration = TimeSpan.FromSeconds((double)total / s.Channels / s.SampleRate);
        return new AudioInfo(format, s.SampleRate, s.Channels, duration, peaks);
    }

    sealed class Mp3Stream : IAudioStream
    {
        readonly MpegFile _file;
        public Mp3Stream(ReadOnlyMemory<byte> data) => _file = new MpegFile(new MemoryStream(data.ToArray(), writable: false));
        public int SampleRate => _file.SampleRate;
        public int Channels => _file.Channels;
        public TimeSpan Duration => _file.Duration;
        public TimeSpan Position { get => _file.Time; set => _file.Time = value < TimeSpan.Zero ? TimeSpan.Zero : value; }
        public int Read(float[] buffer, int offset, int count) => _file.ReadSamples(buffer, offset, count);
        public void Dispose() => _file.Dispose();
    }

    /// <summary>PCM (8/16/24/32 bit) and 32-bit float WAV, decoded up front.</summary>
    sealed class WavStream : IAudioStream
    {
        readonly float[] _samples;
        long _pos;
        public int SampleRate { get; }
        public int Channels { get; }
        WavStream(float[] samples, int rate, int channels) { _samples = samples; SampleRate = rate; Channels = channels; }
        public TimeSpan Duration => TimeSpan.FromSeconds((double)_samples.Length / Channels / SampleRate);
        public TimeSpan Position
        {
            get => TimeSpan.FromSeconds((double)_pos / Channels / SampleRate);
            set => _pos = Math.Clamp((long)(value.TotalSeconds * SampleRate) * Channels, 0, _samples.Length);
        }
        public int Read(float[] buffer, int offset, int count)
        {
            int n = (int)Math.Min(count, _samples.Length - _pos);
            Array.Copy(_samples, _pos, buffer, offset, n);
            _pos += n;
            return n;
        }
        public void Dispose() { }

        public static WavStream Create(ReadOnlySpan<byte> w)
        {
            int p = 12, tag = 0, ch = 0, rate = 0, bits = 0;
            int dataOff = -1, dataLen = 0;
            while (p + 8 <= w.Length)
            {
                var id = w.Slice(p, 4);
                int size = (int)Math.Min(BinaryPrimitives.ReadUInt32LittleEndian(w[(p + 4)..]), (uint)(w.Length - p - 8));
                if (id.SequenceEqual("fmt "u8) && size >= 16)
                {
                    tag = BinaryPrimitives.ReadUInt16LittleEndian(w[(p + 8)..]);
                    ch = BinaryPrimitives.ReadUInt16LittleEndian(w[(p + 10)..]);
                    rate = (int)BinaryPrimitives.ReadUInt32LittleEndian(w[(p + 12)..]);
                    bits = BinaryPrimitives.ReadUInt16LittleEndian(w[(p + 22)..]);
                }
                else if (id.SequenceEqual("data"u8)) { dataOff = p + 8; dataLen = size; break; }
                p += 8 + size + (size & 1);
            }
            if (dataOff < 0 || ch is < 1 or > 8 || rate <= 0) throw new InvalidDataException("Damaged WAV file.");
            bool pcm = tag is 1 or 0xFFFE, flt = tag == 3 && bits == 32;
            if (!(pcm && bits is 8 or 16 or 24 or 32) && !flt) throw new NotSupportedException($"WAV format {tag} ({bits} bit) is not supported.");
            int bytes = bits / 8, count = dataLen / bytes;
            var s = new float[count];
            var d = w.Slice(dataOff, dataLen);
            for (int i = 0; i < count; i++)
            {
                var q = d[(i * bytes)..];
                s[i] = flt ? BinaryPrimitives.ReadSingleLittleEndian(q) : bits switch
                {
                    8 => (q[0] - 128) / 128f,
                    16 => BinaryPrimitives.ReadInt16LittleEndian(q) / 32768f,
                    24 => ((q[0] | q[1] << 8 | (sbyte)q[2] << 16)) / 8388608f,
                    _ => BinaryPrimitives.ReadInt32LittleEndian(q) / 2147483648f,
                };
            }
            return new WavStream(s, rate, ch);
        }
    }
}
