using Avalonia.Media.Imaging;
using CommunityToolkit.Mvvm.ComponentModel;
using Risen.Pak.Formats;

namespace PakView.ViewModels;

// What the preview pane can show. PreviewView picks the view for each type with a DataTemplate.

/// <summary>A sentence instead of a preview ("Loading...", "Binary data ...").</summary>
public sealed record MessageContent(string Text);

public sealed record TextContent(string Text);

public sealed partial class ImageContent(Bitmap picture, string description) : ObservableObject
{
    public Bitmap Picture { get; } = picture;
    public string Description { get; } = description;

    [ObservableProperty]
    public partial bool Fit { get; set; } = true;
}

/// <summary>A string table (<c>*.tab</c>): the columns come from the file, so the view builds them.</summary>
public sealed class TableContent
{
    public TableContent(StringTable table)
    {
        Columns = table.Columns;
        var comparisons = new Dictionary<string, Comparison<string[]>>();
        for (int i = 0; i < table.Columns.Length; i++)
        {
            int column = i;
            comparisons.TryAdd(table.Columns[i], (a, b) => string.Compare(Cell(a, column), Cell(b, column), StringComparison.OrdinalIgnoreCase));
        }
        Rows = new SortableList<string[]>(comparisons);
        Rows.SetSource(table.Rows);
    }

    public string[] Columns { get; }
    public SortableList<string[]> Rows { get; }

    public static string Cell(string[] row, int column) => column < row.Length ? row[column] : "";
}

/// <summary>The property list of a template or material, with a filter box.</summary>
public sealed partial class PropsContent : ObservableObject
{
    static int Text(string a, string b) => string.Compare(a, b, StringComparison.OrdinalIgnoreCase);

    readonly List<PropRow> _all;

    public PropsContent(PropertyTable table)
    {
        _all = table.Rows;
        Notes = string.Join(Environment.NewLine, table.Notes);
        Rows = new SortableList<PropRow>(new()
        {
            ["Property"] = (a, b) => Text(a.Name, b.Name),
            ["Value"] = (a, b) => Text(a.Value, b.Value),
            ["Set"] = (a, b) => Text(a.Group, b.Group),
            ["Type"] = (a, b) => Text(a.Type, b.Type),
        });
        Rows.SetSource(_all);
    }

    public string Notes { get; }
    public SortableList<PropRow> Rows { get; }

    [ObservableProperty]
    public partial string Filter { get; set; } = "";

    partial void OnFilterChanged(string value)
    {
        var f = value.Trim();
        Rows.SetSource(f.Length == 0 ? _all : _all.Where(r =>
            r.Name.Contains(f, StringComparison.OrdinalIgnoreCase) || r.Value.Contains(f, StringComparison.OrdinalIgnoreCase) ||
            r.Group.Contains(f, StringComparison.OrdinalIgnoreCase) || r.Type.Contains(f, StringComparison.OrdinalIgnoreCase)));
    }
}
