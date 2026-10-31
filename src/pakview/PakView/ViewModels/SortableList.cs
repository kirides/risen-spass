using CommunityToolkit.Mvvm.ComponentModel;

namespace PakView.ViewModels;

/// <summary>Which column a list is sorted by (the views draw the arrow from this).</summary>
public abstract partial class SortState : ObservableObject
{
    [ObservableProperty]
    public partial string? SortKey { get; set; }

    [ObservableProperty]
    public partial bool Descending { get; set; }
}

/// <summary>
/// The rows of a grid with explicit sorting. The DataGrid decides by reflection whether a column can sort, which silently
/// fails under Native AOT, so the grid only reports header clicks and the sorting happens here.
/// Sort keys are the column headers; <paramref name="pinned"/> (folders first) is applied before and never reversed.
/// </summary>
public sealed partial class SortableList<T>(Dictionary<string, Comparison<T>> comparisons, Comparison<T>? pinned = null) : SortState where T : class
{
    IEnumerable<T> _source = [];

    [ObservableProperty]
    public partial IReadOnlyList<T> Items { get; private set; } = [];

    public void SetSource(IEnumerable<T> source)
    {
        _source = source;
        Refresh();
    }

    /// <summary>Sorts by the column; the same column again reverses the order.</summary>
    public void Sort(string key)
    {
        Descending = SortKey == key && !Descending;
        SortKey = key;
        Refresh();
    }

    void Refresh()
    {
        var items = _source.ToList();
        if (pinned is not null || SortKey is not null)
        {
            var compare = SortKey is not null && comparisons.TryGetValue(SortKey, out var c) ? c : (_, _) => 0;
            int direction = Descending ? -1 : 1;
            // List.Sort is not stable, so equal rows keep their original order through the index
            var indexed = items.Select((item, index) => (item, index)).ToList();
            indexed.Sort((a, b) =>
            {
                int result = pinned?.Invoke(a.item, b.item) ?? 0;
                if (result == 0) result = direction * compare(a.item, b.item);
                return result != 0 ? result : a.index.CompareTo(b.index);
            });
            items = indexed.Select(x => x.item).ToList();
        }
        Items = items;
    }
}
