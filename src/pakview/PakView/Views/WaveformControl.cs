using System.Windows.Input;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;

namespace PakView.Views;

/// <summary>Draws a waveform outline and a playhead. A click runs <see cref="SeekCommand"/> with the position (0..1).</summary>
public sealed class WaveformControl : Control
{
    public static readonly StyledProperty<float[]?> PeaksProperty = AvaloniaProperty.Register<WaveformControl, float[]?>(nameof(Peaks));
    public static readonly StyledProperty<double> FractionProperty = AvaloniaProperty.Register<WaveformControl, double>(nameof(Fraction));
    public static readonly StyledProperty<ICommand?> SeekCommandProperty = AvaloniaProperty.Register<WaveformControl, ICommand?>(nameof(SeekCommand));

    static WaveformControl() => AffectsRender<WaveformControl>(PeaksProperty, FractionProperty);

    public float[]? Peaks { get => GetValue(PeaksProperty); set => SetValue(PeaksProperty, value); }
    public double Fraction { get => GetValue(FractionProperty); set => SetValue(FractionProperty, value); }
    public ICommand? SeekCommand { get => GetValue(SeekCommandProperty); set => SetValue(SeekCommandProperty, value); }

    static readonly IBrush Background = new SolidColorBrush(Color.Parse("#1E2229"));
    static readonly Pen Wave = new(new SolidColorBrush(Color.Parse("#5C9DE8")), 1);
    static readonly Pen Played = new(new SolidColorBrush(Color.Parse("#A8D0FF")), 1);
    static readonly Pen Playhead = new(Brushes.White, 1.5);

    public override void Render(DrawingContext context)
    {
        var area = new Rect(Bounds.Size);
        context.DrawRectangle(Background, null, area, 4, 4);
        double middle = area.Height / 2;
        var peaks = Peaks ?? [];
        for (int x = 0; x < (int)area.Width && peaks.Length > 0; x++)
        {
            double position = x / area.Width;
            float peak = peaks[Math.Min(peaks.Length - 1, (int)(position * peaks.Length))];
            double half = Math.Max(1, Math.Min(1.0, peak) * (middle - 3));
            context.DrawLine(position <= Fraction ? Played : Wave, new Point(x + 0.5, middle - half), new Point(x + 0.5, middle + half));
        }
        double head = Math.Clamp(Fraction, 0, 1) * area.Width;
        context.DrawLine(Playhead, new Point(head, 0), new Point(head, area.Height));
    }

    protected override void OnPointerPressed(PointerPressedEventArgs e)
    {
        base.OnPointerPressed(e);
        if (Bounds.Width <= 0) return;
        double fraction = Math.Clamp(e.GetPosition(this).X / Bounds.Width, 0, 1);
        if (SeekCommand?.CanExecute(fraction) == true) SeekCommand.Execute(fraction);
    }
}
