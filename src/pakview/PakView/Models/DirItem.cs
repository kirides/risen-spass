using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using Risen.Pak;

namespace PakView.Models;

/// <summary>A directory in the tree on the left.</summary>
public sealed partial class DirItem(PakDirectory dir, string? displayName = null, DirItem? parent = null) : ObservableObject
{
    public PakDirectory Dir { get; } = dir;
    public DirItem? Parent { get; } = parent;
    public string Name { get; } = displayName ?? dir.Name;
    public ObservableCollection<DirItem> Children { get; } = [];

    [ObservableProperty]
    public partial bool IsExpanded { get; set; }

    public DirItem? FindByPath(string path)
    {
        var cur = this;
        foreach (var segment in path.Split('/', StringSplitOptions.RemoveEmptyEntries))
        {
            cur = cur.Children.FirstOrDefault(c => string.Equals(c.Dir.Name, segment, StringComparison.OrdinalIgnoreCase));
            if (cur is null) return null;
        }
        return cur;
    }

    public void ExpandParents()
    {
        for (var p = Parent; p is not null; p = p.Parent) p.IsExpanded = true;
    }

    public static DirItem Build(PakDirectory dir, string? name = null, DirItem? parent = null)
    {
        var item = new DirItem(dir, name, parent);
        foreach (var d in dir.Children.OfType<PakDirectory>().OrderBy(d => d.Name, StringComparer.OrdinalIgnoreCase))
            item.Children.Add(Build(d, null, item));
        return item;
    }
}
