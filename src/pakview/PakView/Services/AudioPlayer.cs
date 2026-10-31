using NAudio.Wave;
using Risen.Pak.Formats;

namespace PakView.Services;

/// <summary>
/// Plays one decoded sound through the Windows audio device (NAudio WinMM). Not a UI class: the sound preview
/// view model drives it. <see cref="Stopped"/> is raised on the audio thread when playback ends or is stopped.
/// </summary>
public sealed class AudioPlayer : IDisposable
{
    /// <summary>Reads and seeks come from different threads, hence the lock.</summary>
    sealed class Provider(IAudioStream stream) : ISampleProvider
    {
        readonly object _gate = new();
        public float Volume { get; set; } = 0.8f;
        public WaveFormat WaveFormat { get; } = WaveFormat.CreateIeeeFloatWaveFormat(stream.SampleRate, stream.Channels);

        public int Read(Span<float> buffer)
        {
            var temp = System.Buffers.ArrayPool<float>.Shared.Rent(buffer.Length);
            try
            {
                int n;
                lock (_gate) n = stream.Read(temp, 0, buffer.Length);
                float volume = Volume;
                for (int i = 0; i < n; i++) buffer[i] = temp[i] * volume;
                return n;
            }
            finally { System.Buffers.ArrayPool<float>.Shared.Return(temp); }
        }

        public TimeSpan Position
        {
            get { lock (_gate) return stream.Position; }
            set { lock (_gate) stream.Position = value; }
        }
    }

    readonly IAudioStream _stream;
    readonly Provider _provider;
    readonly WaveOut _out;

    /// <summary>Throws (NotSupported / PlatformNotSupported / device errors) when the sound cannot be played here.</summary>
    public AudioPlayer(byte[] file, float volume)
    {
        if (!OperatingSystem.IsWindows()) throw new PlatformNotSupportedException("Playback is only available on Windows.");
        _stream = AudioReader.Open(file, out _);
        _provider = new Provider(_stream) { Volume = volume };
        _out = new WaveOut();
        _out.Init(_provider);
        _out.PlaybackStopped += (_, _) => Stopped?.Invoke();
    }

    public event Action? Stopped;

    public bool IsPlaying => _out.PlaybackState == PlaybackState.Playing;
    public float Volume { get => _provider.Volume; set => _provider.Volume = value; }
    public TimeSpan Position { get => _provider.Position; set => _provider.Position = value; }

    public void Play() => _out.Play();
    public void Pause() => _out.Pause();
    public void Stop() => _out.Stop();

    public void Dispose()
    {
        try { _out.Stop(); _out.Dispose(); } catch { /* the device may be gone */ }
        _stream.Dispose();
    }
}
