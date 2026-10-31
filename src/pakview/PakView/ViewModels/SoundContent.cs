using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using PakView.Services;
using Risen.Pak.Formats;

namespace PakView.ViewModels;

/// <summary>Preview of a sound: facts, waveform, transport. Dispose stops the sound.</summary>
public sealed partial class SoundContent : ObservableObject, IDisposable
{
    static bool _autoPlay;   // remembered from one selected sound to the next

    readonly AudioPlayer? _player;
    readonly DispatcherTimer _timer = new() { Interval = TimeSpan.FromMilliseconds(50) };
    readonly TimeSpan _duration;

    public SoundContent(byte[] file, AudioInfo info)
    {
        _duration = info.Duration;
        Peaks = info.Peaks;
        Facts = $"{info.Format.ToUpperInvariant()}, {info.SampleRate:N0} Hz, " +
                $"{(info.Channels == 1 ? "mono" : info.Channels == 2 ? "stereo" : info.Channels + " channels")}, {Format(info.Duration)}";
        TimeText = $"{Format(TimeSpan.Zero)} / {Format(_duration)}";

        try
        {
            _player = new AudioPlayer(file, (float)Volume);
            _player.Stopped += () => Dispatcher.UIThread.Post(Rewind);
        }
        catch (Exception ex)
        {
            Unavailable = "Playback unavailable: " + ex.Message;
            return;
        }
        _timer.Tick += (_, _) => ShowPosition();
        _timer.Start();
        if (_autoPlay) _player.Play();
        IsPlaying = _player.IsPlaying;
    }

    public string Facts { get; }
    public float[] Peaks { get; }

    /// <summary>Why the buttons are disabled (no audio device, not Windows ...); empty when playback works.</summary>
    public string Unavailable { get; } = "";

    [ObservableProperty]
    public partial double Fraction { get; set; }

    [ObservableProperty]
    public partial string TimeText { get; set; }

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(PlayLabel))]
    public partial bool IsPlaying { get; set; }

    public string PlayLabel => IsPlaying ? "Pause" : "Play";

    [ObservableProperty]
    public partial double Volume { get; set; } = 0.8;

    partial void OnVolumeChanged(double value)
    {
        if (_player is not null) _player.Volume = (float)value;
    }

    public bool AutoPlay
    {
        get => _autoPlay;
        set { if (_autoPlay != value) { _autoPlay = value; OnPropertyChanged(); } }
    }

    bool CanPlay => _player is not null;

    [RelayCommand(CanExecute = nameof(CanPlay))]
    void PlayPause()
    {
        if (_player!.IsPlaying) _player.Pause(); else _player.Play();
        IsPlaying = _player.IsPlaying;
    }

    [RelayCommand(CanExecute = nameof(CanPlay))]
    void Stop()
    {
        _player!.Stop();
        Rewind();
    }

    /// <summary>A click on the waveform: 0..1 of the length. Starts playing when stopped.</summary>
    [RelayCommand(CanExecute = nameof(CanPlay))]
    void Seek(double fraction)
    {
        _player!.Position = TimeSpan.FromSeconds(_duration.TotalSeconds * fraction);
        ShowPosition();
        if (!_player.IsPlaying) _player.Play();
        IsPlaying = _player.IsPlaying;
    }

    /// <summary>From the beginning (a double-click on a playing sound restarts it).</summary>
    public void Play()
    {
        if (_player is null) return;
        _player.Position = TimeSpan.Zero;
        _player.Play();
        IsPlaying = _player.IsPlaying;
    }

    /// <summary>The device reached the end (or was stopped): back to the start so Play starts again.</summary>
    void Rewind()
    {
        if (_player is null) return;
        _player.Position = TimeSpan.Zero;
        ShowPosition();
        IsPlaying = _player.IsPlaying;
    }

    void ShowPosition()
    {
        var position = _player?.Position ?? TimeSpan.Zero;
        IsPlaying = _player?.IsPlaying == true;
        TimeText = $"{Format(position)} / {Format(_duration)}";
        Fraction = _duration.TotalSeconds > 0 ? position.TotalSeconds / _duration.TotalSeconds : 0;
    }

    static string Format(TimeSpan t) => $"{(int)t.TotalMinutes}:{t.Seconds:00}.{t.Milliseconds / 100}";

    public void Dispose()
    {
        _timer.Stop();
        _player?.Dispose();
    }
}
