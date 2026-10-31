namespace Risen.Pak;

/// <summary>Where a node stands compared to the archive on disk.</summary>
public enum NodeState { Original, Added, Replaced, Renamed }

/// <summary>A directory or file of a G3V0 archive. Names are stored as Latin-1 in the file.</summary>
public abstract class PakNode
{
    protected PakNode(string name) => Name = name;

    public string Name { get; internal set; }
    public PakDirectory? Parent { get; internal set; }

    /// <summary>The attribute word in front of the name (0x20010 directory, 0x20020 file).</summary>
    public uint Attributes { get; internal set; }

    /// <summary>The attribute word after the FILETIMEs (normally equal to <see cref="Attributes"/>).</summary>
    public uint InnerAttributes { get; internal set; }

    public ulong Created { get; internal set; }
    public ulong Accessed { get; internal set; }
    public ulong Written { get; internal set; }

    public NodeState State { get; internal set; }

    public abstract bool IsDirectory { get; }

    /// <summary>Path inside the archive with '/' separators, empty for the root.</summary>
    public string Path => Parent is null ? "" : (Parent.Parent is null ? Name : Parent.Path + "/" + Name);

    public DateTime? WrittenTime => FileTimeToDateTime(Written);

    internal static DateTime? FileTimeToDateTime(ulong ft)
    {
        if (ft == 0 || ft > (ulong)DateTime.MaxValue.ToFileTimeUtc()) return null;
        return DateTime.FromFileTimeUtc((long)ft).ToLocalTime();
    }
}

public sealed class PakDirectory : PakNode
{
    readonly List<PakNode> _children = new();
    readonly Dictionary<string, PakNode> _byName = new(StringComparer.OrdinalIgnoreCase);

    public PakDirectory(string name) : base(name)
    {
        Attributes = InnerAttributes = PakFormat.DirAttr;
    }

    public override bool IsDirectory => true;
    public IReadOnlyList<PakNode> Children => _children;

    /// <summary>Case-insensitive lookup, like the engine does.</summary>
    public PakNode? Find(string name) => _byName.GetValueOrDefault(name);

    internal void AddChild(PakNode node)
    {
        node.Parent = this;
        _children.Add(node);
        _byName.TryAdd(node.Name, node);
    }

    internal void RemoveChild(PakNode node)
    {
        if (!_children.Remove(node)) return;
        node.Parent = null;
        if (_byName.TryGetValue(node.Name, out var cur) && ReferenceEquals(cur, node))
        {
            _byName.Remove(node.Name);
            var other = _children.FirstOrDefault(c => string.Equals(c.Name, node.Name, StringComparison.OrdinalIgnoreCase));
            if (other is not null) _byName[other.Name] = other;
        }
    }

    internal void Reindex(PakNode node, string oldName)
    {
        if (_byName.TryGetValue(oldName, out var cur) && ReferenceEquals(cur, node)) _byName.Remove(oldName);
        _byName.TryAdd(node.Name, node);
    }

    public IEnumerable<PakNode> Descendants()
    {
        var stack = new Stack<PakNode>(Enumerable.Reverse(_children));
        while (stack.Count > 0)
        {
            var n = stack.Pop();
            yield return n;
            if (n is PakDirectory d)
                for (int i = d._children.Count - 1; i >= 0; i--) stack.Push(d._children[i]);
        }
    }

    public IEnumerable<PakFile> Files() => Descendants().OfType<PakFile>();
}

public sealed class PakFile : PakNode
{
    public PakFile(string name) : base(name)
    {
        Attributes = InnerAttributes = PakFormat.FileAttr;
    }

    public override bool IsDirectory => false;

    /// <summary>Position of the stored bytes in the archive on disk (unused for new content).</summary>
    public long Offset { get; internal set; }

    /// <summary>Bytes occupied in the archive (a zlib stream when it differs from <see cref="Size"/>).</summary>
    public uint StoredSize { get; internal set; }

    /// <summary>Size after inflating.</summary>
    public uint Size { get; internal set; }

    /// <summary>The u64 member flag in front of the sizes (0x2_0000_0000 for compressed members).</summary>
    public ulong MemberFlags { get; internal set; }

    /// <summary>Content that replaces / adds the member on the next save; null = the bytes in the archive.</summary>
    public PakContent? NewContent { get; internal set; }

    public bool IsCompressed => NewContent is null && StoredSize != Size;
}
