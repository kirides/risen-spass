using Avalonia;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using CommunityToolkit.Mvvm.ComponentModel;
using Risen.Pak;
using Risen.Pak.Formats;

namespace PakView.ViewModels;

/// <summary>
/// Live preview of the selected member, decoded without extracting it. A newer selection cancels the work of the older one.
/// <see cref="Content"/> is one of the types in PreviewContents.cs / <see cref="SoundContent"/>; the view maps each to a template.
/// </summary>
public sealed partial class PreviewViewModel : ObservableObject, IDisposable
{
    CancellationTokenSource? _cts;
    bool _playPending;

    [ObservableProperty]
    public partial string Caption { get; set; } = "";

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(Picture))]
    public partial object Content { get; set; } = new MessageContent("Select a file to preview it.");

    [ObservableProperty]
    public partial string Hex { get; set; } = "";

    [ObservableProperty]
    public partial string Info { get; set; } = "";

    /// <summary>Set while the content is a picture (the "Fit to window" box lives in the pane's header).</summary>
    public ImageContent? Picture => Content as ImageContent;

    public bool IsPlaying => Content is SoundContent { IsPlaying: true };

    partial void OnContentChanged(object oldValue, object newValue)
    {
        (oldValue as IDisposable)?.Dispose();   // stops a sound that is replaced by the next selection
    }

    public void Clear(string message = "Select a file to preview it.")
    {
        _cts?.Cancel();
        Caption = Hex = Info = "";
        Content = new MessageContent(message);
    }

    /// <summary>Summary page for the archive, a directory or several selected items.</summary>
    public void ShowSummary(string title, string text)
    {
        _cts?.Cancel();
        Caption = title;
        Hex = "";
        Info = text;
        Content = new MessageContent(text);
    }

    /// <summary>Plays the selected sound from the start; if it is still being decoded, plays as soon as it is shown.</summary>
    public void RequestPlay()
    {
        if (Content is SoundContent sound) sound.Play(); else _playPending = true;
    }

    public async Task ShowFileAsync(PakArchive archive, PakFile file)
    {
        _cts?.Cancel();
        var cts = _cts = new CancellationTokenSource();
        var ct = cts.Token;
        _playPending = false;
        Caption = file.Path;
        Info = PreviewBuilder.InfoText(file);
        Hex = "";
        Content = new MessageContent("Loading...");
        try
        {
            var (head, built) = await Task.Run(() =>
            {
                var head = archive.ReadHead(file, PreviewBuilder.HexLimit);
                ct.ThrowIfCancellationRequested();
                return (head, PreviewBuilder.Build(archive, file, head, ct));
            }, ct);
            if (ct.IsCancellationRequested) return;
            Hex = PreviewBuilder.HexText(file, head);
            Content = ToContent(built);
            if (Content is SoundContent sound && _playPending) { _playPending = false; sound.Play(); }
        }
        catch (OperationCanceledException) { }
        catch (ObjectDisposedException) { }
        catch (Exception ex) when (!ct.IsCancellationRequested)
        {
            Content = new MessageContent("Cannot preview: " + ex.Message);
            try { Hex = PreviewBuilder.HexText(file, archive.ReadHead(file, PreviewBuilder.HexLimit)); } catch { Hex = ""; }
        }
    }

    /// <summary>Bitmaps and the audio device are made here, on the UI thread.</summary>
    static object ToContent(object built) => built switch
    {
        ImageData { Encoded: { } bytes } => PictureOf(new Bitmap(new MemoryStream(bytes)), null),
        ImageData { Pixels: { } pixels } => PictureOf(ToBitmap(pixels), pixels.Description),
        AudioData audio => new SoundContent(audio.File, audio.Info),
        _ => built,
    };

    static ImageContent PictureOf(Bitmap bitmap, string? description) =>
        new(bitmap, description ?? $"{bitmap.PixelSize.Width} x {bitmap.PixelSize.Height}");

    static WriteableBitmap ToBitmap(Bitmap32 p)
    {
        var bitmap = new WriteableBitmap(new PixelSize(p.Width, p.Height), new Vector(96, 96), PixelFormat.Bgra8888, AlphaFormat.Unpremul);
        using var buffer = bitmap.Lock();
        for (int y = 0; y < p.Height; y++)
            System.Runtime.InteropServices.Marshal.Copy(p.Bgra, y * p.Width * 4, buffer.Address + y * buffer.RowBytes, p.Width * 4);
        return bitmap;
    }

    public void Dispose()
    {
        _cts?.Cancel();
        (Content as IDisposable)?.Dispose();
    }
}
