using System.Globalization;
using Risen.Pak;

namespace PakView.Models;

/// <summary>One row of the file list: the archive node plus its display texts, computed once.</summary>
public sealed class EntryRow
{
    public EntryRow(PakNode node, bool showFolder)
    {
        Node = node;
        IsDirectory = node.IsDirectory;
        Name = node.Name;
        Icon = IsDirectory ? "▰" : Describe.IconFor(node.Name);
        IconBrush = IsDirectory ? "#E0A83A" : Describe.IconBrushFor(node.Name);
        Size = node is PakFile f ? f.Size : 0;
        SizeText = node is PakFile ? Describe.Bytes(Size) : "";
        if (node is PakFile pf && pf.NewContent is null)
        {
            Packed = pf.StoredSize;
            PackedText = Describe.Bytes(pf.StoredSize);
            RatioText = pf.Size == 0 ? "" : (100.0 * pf.StoredSize / pf.Size).ToString("0", CultureInfo.CurrentCulture) + " %";
        }
        else { PackedText = RatioText = ""; }
        Modified = node.WrittenTime;
        ModifiedText = Modified?.ToString("g", CultureInfo.CurrentCulture) ?? "";
        TypeText = IsDirectory ? "Folder" : Describe.TypeOf(node.Name);
        StatusText = node.State switch { NodeState.Added => "new", NodeState.Replaced => "replaced", NodeState.Renamed => "moved/renamed", _ => "" };
        Folder = showFolder ? (node.Parent?.Path ?? "") : "";
    }

    public PakNode Node { get; }
    public bool IsDirectory { get; }
    public string Icon { get; }
    public string IconBrush { get; }
    public string Name { get; }
    public long Size { get; }
    public long Packed { get; }
    public string SizeText { get; }
    public string PackedText { get; }
    public string RatioText { get; }
    public DateTime? Modified { get; }
    public string ModifiedText { get; }
    public string TypeText { get; }
    public string StatusText { get; }
    public string Folder { get; }
}
