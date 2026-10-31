using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Input.Platform;
using PakView.ViewModels;

namespace PakView.Views;

/// <summary>Small helpers that give a DataGrid the behaviour its own machinery cannot (reliably) provide.</summary>
public static class GridSorting
{
    /// <summary>
    /// A header click calls <paramref name="sort"/> with the column's header text; the arrow follows <paramref name="state"/>.
    /// The grid's built-in sorting finds sortable columns by reflection, which does not survive Native AOT (nothing sorts, no error).
    /// </summary>
    public static void Attach(DataGrid grid, SortState state, Action<string> sort)
    {
        var titles = grid.Columns.Where(c => c.Header is string).ToDictionary(c => c, c => (string)c.Header!);
        foreach (var column in titles.Keys) column.CanUserSort = true;

        grid.Sorting += (_, e) =>
        {
            e.Handled = true;   // we sort, not the grid
            if (titles.TryGetValue(e.Column, out var key)) sort(key);
        };

        state.PropertyChanged += (_, _) =>
        {
            foreach (var (column, title) in titles)
                column.Header = title == state.SortKey ? title + (state.Descending ? " ▼" : " ▲") : title;
        };
    }
}

public static class GridCopy
{
    /// <summary>Ctrl+C copies the selected rows as tab separated text (columns made from templates have no clipboard binding).</summary>
    public static void Attach<T>(DataGrid grid, Func<T, string> line) where T : class
    {
        grid.KeyDown += async (_, e) =>
        {
            if (e.Key != Key.C || !e.KeyModifiers.HasFlag(KeyModifiers.Control)) return;
            var text = string.Join(Environment.NewLine, grid.SelectedItems.OfType<T>().Select(line));
            if (text.Length > 0 && TopLevel.GetTopLevel(grid)?.Clipboard is { } clipboard) await clipboard.SetTextAsync(text);
            e.Handled = true;
        };
    }
}
