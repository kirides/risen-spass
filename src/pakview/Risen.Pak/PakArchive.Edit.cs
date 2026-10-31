namespace Risen.Pak;

public sealed partial class PakArchive
{
    void Touch() => _changes++;

    static ulong Ft(DateTime utc) => (ulong)utc.ToFileTimeUtc();

    static void CheckName(string name)
    {
        if (PakFormat.ValidateName(name) is { } why) throw new PakException($"'{name}': {why}");
    }

    static void CheckContent(PakContent c)
    {
        if (c.Length >= uint.MaxValue) throw new PakException("A member must be smaller than 4 GiB.");
    }

    /// <summary>Adds a file (or replaces a same-named file when <paramref name="overwrite"/>).</summary>
    public PakFile AddFile(PakDirectory dir, string name, PakContent content, bool overwrite = true)
    {
        CheckName(name);
        CheckContent(content);
        if (dir.Find(name) is { } existing)
        {
            if (existing is PakDirectory) throw new PakException($"'{name}' is a directory.");
            if (!overwrite) throw new PakException($"'{name}' exists already.");
            ReplaceContent((PakFile)existing, content);
            return (PakFile)existing;
        }
        var ft = Ft(content.LastWriteUtc);
        var f = new PakFile(name)
        {
            Created = ft, Accessed = ft, Written = ft,
            Size = (uint)content.Length, NewContent = content, State = NodeState.Added,
        };
        dir.AddChild(f);
        Touch();
        return f;
    }

    public void ReplaceContent(PakFile f, PakContent content)
    {
        CheckContent(content);
        f.NewContent = content;
        f.Size = (uint)content.Length;
        f.Written = Ft(content.LastWriteUtc);
        if (f.State == NodeState.Original || f.State == NodeState.Renamed) f.State = NodeState.Replaced;
        Touch();
    }

    public PakDirectory AddDirectory(PakDirectory parent, string name)
    {
        CheckName(name);
        if (parent.Find(name) is { } e)
            return e as PakDirectory ?? throw new PakException($"'{name}' is a file.");
        var now = Ft(DateTime.UtcNow);
        var d = new PakDirectory(name) { Created = now, Accessed = now, Written = now, State = NodeState.Added };
        parent.AddChild(d);
        Touch();
        return d;
    }

    /// <summary>Creates the directory chain "a/b/c" below the root (or <paramref name="start"/>).</summary>
    public PakDirectory EnsureDirectory(string path, PakDirectory? start = null)
    {
        var cur = start ?? Root;
        foreach (var seg in path.Split('/', StringSplitOptions.RemoveEmptyEntries)) cur = AddDirectory(cur, seg);
        return cur;
    }

    public void Remove(PakNode node)
    {
        if (node.Parent is null) throw new PakException("The root cannot be removed.");
        node.Parent.RemoveChild(node);
        Touch();
    }

    public void Rename(PakNode node, string newName)
    {
        CheckName(newName);
        var parent = node.Parent ?? throw new PakException("The root cannot be renamed.");
        if (parent.Find(newName) is { } other && !ReferenceEquals(other, node))
            throw new PakException($"'{newName}' exists already in this directory.");
        if (node.Name == newName) return;
        var old = node.Name;
        node.Name = newName;
        parent.Reindex(node, old);
        if (node.State == NodeState.Original) node.State = NodeState.Renamed;
        Touch();
    }

    /// <summary>Moves a node into another directory (not into itself).</summary>
    public void Move(PakNode node, PakDirectory target)
    {
        var from = node.Parent ?? throw new PakException("The root cannot be moved.");
        for (PakDirectory? p = target; p is not null; p = p.Parent)
            if (ReferenceEquals(p, node)) throw new PakException("A directory cannot be moved into itself.");
        if (ReferenceEquals(from, target)) return;
        if (target.Find(node.Name) is not null) throw new PakException($"'{node.Name}' exists already in the target directory.");
        from.RemoveChild(node);
        target.AddChild(node);
        if (node.State == NodeState.Original) node.State = NodeState.Renamed;
        Touch();
    }

    public sealed record AddResult(int Added, int Replaced);

    /// <summary>Adds a file or a whole directory tree from disk below <paramref name="target"/>.</summary>
    public AddResult AddFromDisk(PakDirectory target, string diskPath, bool overwrite = true, CancellationToken ct = default)
    {
        int added = 0, replaced = 0;
        void AddOne(PakDirectory dir, string file)
        {
            ct.ThrowIfCancellationRequested();
            bool exists = dir.Find(Path.GetFileName(file)) is PakFile;
            AddFile(dir, Path.GetFileName(file), new DiskContent(file), overwrite);
            if (exists) replaced++; else added++;
        }
        void AddDir(PakDirectory dir, string folder)
        {
            var sub = AddDirectory(dir, Path.GetFileName(folder.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)));
            foreach (var f in Directory.EnumerateFiles(folder).OrderBy(x => x, StringComparer.OrdinalIgnoreCase)) AddOne(sub, f);
            foreach (var d in Directory.EnumerateDirectories(folder).OrderBy(x => x, StringComparer.OrdinalIgnoreCase)) AddDir(sub, d);
        }
        if (Directory.Exists(diskPath)) AddDir(target, diskPath);
        else AddOne(target, diskPath);
        return new AddResult(added, replaced);
    }
}
