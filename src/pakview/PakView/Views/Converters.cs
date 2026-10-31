using Avalonia.Controls.Primitives;
using Avalonia.Data.Converters;
using Avalonia.Media;

namespace PakView.Views;

/// <summary>Value converters for the picture preview ("Fit to window").</summary>
public static class Converters
{
    public static readonly FuncValueConverter<bool, Stretch> FitToStretch = new(fit => fit ? Stretch.Uniform : Stretch.None);

    public static readonly FuncValueConverter<bool, ScrollBarVisibility> FitToScrollBars =
        new(fit => fit ? ScrollBarVisibility.Disabled : ScrollBarVisibility.Auto);
}
