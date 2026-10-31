using Avalonia.Controls;
using PakView.ViewModels;
using Risen.Pak.Formats;

namespace PakView.Views;

public partial class PropsContentView : UserControl
{
    bool _attached;

    public PropsContentView() => InitializeComponent();

    protected override void OnDataContextChanged(EventArgs e)
    {
        base.OnDataContextChanged(e);
        if (_attached || DataContext is not PropsContent content) return;
        _attached = true;   // once: a view is made for each previewed file
        GridSorting.Attach(Grid, content.Rows, content.Rows.Sort);
        GridCopy.Attach<PropRow>(Grid, r => $"{r.Name}\t{r.Value}\t{r.Group}\t{r.Type}");
    }
}
