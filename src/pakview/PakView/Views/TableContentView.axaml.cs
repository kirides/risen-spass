using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Templates;
using Avalonia.Layout;
using Avalonia.Media;
using PakView.ViewModels;

namespace PakView.Views;

public partial class TableContentView : UserControl
{
    public TableContentView() => InitializeComponent();

    protected override void OnDataContextChanged(EventArgs e)
    {
        base.OnDataContextChanged(e);
        if (DataContext is not TableContent table || Grid.Columns.Count > 0) return;
        for (int i = 0; i < table.Columns.Length; i++)
        {
            int index = i;
            // a template column: no reflection binding, so it also works in the Native AOT build
            Grid.Columns.Add(new DataGridTemplateColumn
            {
                Header = table.Columns[i], Width = new DataGridLength(i == 0 ? 180 : 260), MinWidth = 80, IsReadOnly = true,
                CellTemplate = new FuncDataTemplate<string[]>((row, _) => new TextBlock
                {
                    Text = row is null ? "" : TableContent.Cell(row, index), VerticalAlignment = VerticalAlignment.Center,
                    Margin = new Thickness(12, 0), TextTrimming = TextTrimming.CharacterEllipsis,
                }),
            });
        }
        GridSorting.Attach(Grid, table.Rows, table.Rows.Sort);
        GridCopy.Attach<string[]>(Grid, row => string.Join("\t", row));
    }
}
