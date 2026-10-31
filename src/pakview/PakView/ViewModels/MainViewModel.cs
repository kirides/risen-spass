using System.Text.RegularExpressions;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using PakView.Models;
using PakView.Services;
using Risen.Pak;
using Risen.Pak.Formats;

namespace PakView.ViewModels;

/// <summary>One entry of the "Extract as" submenu.</summary>
public sealed record ExtractAsOption(string Label, ExportFormat Format, System.Windows.Input.ICommand Command);

/// <summary>
/// Everything the main window shows and does: the open archive, the folder tree, the file list with its search and sorting,
/// the selection, the busy state, the undo-less editing commands. This part is the state and the navigation;
/// the commands are in MainViewModel.Commands.cs. The window only binds to it and implements <see cref="IShell"/>.
/// </summary>
public sealed partial class MainViewModel : ObservableObject, IDisposable
{
    static int Text(string a, string b) => string.Compare(a, b, StringComparison.OrdinalIgnoreCase);

    static readonly Dictionary<string, Comparison<EntryRow>> Comparisons = new()
    {
        ["Name"] = (a, b) => Text(a.Name, b.Name),
        ["Size"] = (a, b) => a.Size.CompareTo(b.Size),
        ["Packed"] = (a, b) => a.Packed.CompareTo(b.Packed),
        ["%"] = (a, b) => (a.Size == 0 ? 0 : (double)a.Packed / a.Size).CompareTo(b.Size == 0 ? 0 : (double)b.Packed / b.Size),
        ["Modified"] = (a, b) => Comparer<DateTime>.Default.Compare(a.Modified ?? DateTime.MinValue, b.Modified ?? DateTime.MinValue),
        ["Type"] = (a, b) => Text(a.TypeText, b.TypeText),
        ["Status"] = (a, b) => Text(a.StatusText, b.StatusText),
        ["Folder"] = (a, b) => Text(a.Folder, b.Folder),
    };

    readonly IShell _shell;
    readonly ExternalEdits _external = new();
    readonly Stack<string> _back = new(), _forward = new();
    PakArchive? _archive;
    DirItem? _rootItem;
    bool _navigating, _updatingTree, _swappingList;

    public MainViewModel(IShell shell)
    {
        _shell = shell;
        Preview = new PreviewViewModel();
        List = new SortableList<EntryRow>(Comparisons, pinned: (a, b) => a.IsDirectory == b.IsDirectory ? 0 : a.IsDirectory ? -1 : 1);
        _external.Changed += OnExternalChanged;
        ShowNoArchive();
    }

    public PreviewViewModel Preview { get; }
    public SortableList<EntryRow> List { get; }

    // ------------------------------------------------------------------------------------------ what the window shows

    [ObservableProperty]
    public partial string Title { get; set; } = "Risen PakView";

    [ObservableProperty]
    public partial string Status { get; set; } = "";

    [ObservableProperty]
    public partial string PendingText { get; set; } = "";

    [ObservableProperty]
    public partial string PathText { get; set; } = "";

    [ObservableProperty]
    public partial string FilterText { get; set; } = "";

    [ObservableProperty]
    public partial IReadOnlyList<DirItem> TreeRoots { get; set; } = [];

    [ObservableProperty]
    public partial DirItem? SelectedDir { get; set; }

    /// <summary>The "Folder" column only makes sense in search results.</summary>
    [ObservableProperty]
    public partial bool ShowFolderColumn { get; set; }

    [ObservableProperty]
    public partial bool CompressNewFiles { get; set; } = true;

    [ObservableProperty]
    public partial bool KeepBackup { get; set; }

    // busy state: the window disables its body and shows a progress bar
    [ObservableProperty]
    public partial bool IsBusy { get; set; }

    [ObservableProperty]
    public partial double BusyProgress { get; set; }

    [ObservableProperty]
    public partial bool BusyIndeterminate { get; set; }

    [ObservableProperty]
    public partial bool CanCancel { get; set; }

    partial void OnIsBusyChanged(bool value) => RefreshCommands();

    string ArchiveName => _archive?.FilePath is { } p ? Path.GetFileName(p) : "(new archive)";

    public string CurrentPath { get; private set; } = "";

    PakDirectory CurrentDir => (_archive?.Find(CurrentPath) as PakDirectory) ?? _archive!.Root;

    // ------------------------------------------------------------------------------------------ "Extract as" menu

    // offers only what the selected files are (AssetExport.AvailableFormats): one format = one plain "Extract as MP3..." item,
    // several = a submenu, none = hidden

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasExtractAs))]
    public partial string ExtractAsHeader { get; set; } = "";

    /// <summary>The submenu entries; null when there is at most one format (then <see cref="SingleExtractFormat"/> is the parameter).</summary>
    [ObservableProperty]
    public partial IReadOnlyList<ExtractAsOption>? ExtractAsOptions { get; set; }

    [ObservableProperty]
    public partial ExportFormat? SingleExtractFormat { get; set; }

    public bool HasExtractAs => ExtractAsHeader.Length > 0;

    void UpdateExtractAs()
    {
        var formats = _archive is null || Selection.Count == 0 ? [] : _archive.FormatsFor(Selection.Select(r => r.Node).ToList());
        SingleExtractFormat = formats.Count == 1 ? formats[0] : null;
        ExtractAsOptions = formats.Count > 1 ? formats.Select(f => new ExtractAsOption(f.ToString().ToUpperInvariant(), f, ExtractAsCommand)).ToList() : null;
        ExtractAsHeader = formats.Count switch { 0 => "", 1 => $"Extract as {formats[0].ToString().ToUpperInvariant()}...", _ => "Extract selected as" };
    }

    // ------------------------------------------------------------------------------------------ selection

    /// <summary>The rows selected in the grid. The grid reports changes with <see cref="SetSelection"/>; the model asks the grid to select with <see cref="SelectionRequested"/>.</summary>
    public IReadOnlyList<EntryRow> Selection { get; private set; } = [];

    public event Action<IReadOnlyList<EntryRow>>? SelectionRequested;

    public void SetSelection(IReadOnlyList<EntryRow> rows)
    {
        if (_swappingList || new HashSet<EntryRow>(Selection).SetEquals(rows)) return;   // the grid echoing what we asked for
        Selection = rows;
        OnSelectionChanged();
    }

    void RequestSelection(IReadOnlyList<EntryRow> rows)
    {
        Selection = rows;
        SelectionRequested?.Invoke(rows);
        OnSelectionChanged();
    }

    /// <summary>Selects the rows (of the current list) with these archive paths.</summary>
    public void SelectPaths(IEnumerable<string> paths)
    {
        var wanted = paths.ToHashSet(StringComparer.OrdinalIgnoreCase);
        RequestSelection(List.Items.Where(r => wanted.Contains(r.Node.Path)).ToList());
    }

    [RelayCommand(CanExecute = nameof(CanEditArchive))]
    void SelectAll() => RequestSelection(List.Items);

    /// <summary>The grid reported a header click: sort, and keep the selection (the grid drops it when its items change).</summary>
    public void SortBy(string key)
    {
        var keep = Selection;
        _swappingList = true;
        try { List.Sort(key); }
        finally { _swappingList = false; }
        RequestSelection(keep);
    }

    void OnSelectionChanged()
    {
        RefreshCommands();
        UpdateExtractAs();
        if (_archive is null) return;
        var sel = Selection;
        if (sel.Count == 1 && sel[0].Node is PakFile f)
        {
            _ = Preview.ShowFileAsync(_archive, f);
            Status = $"{f.Path}  -  {Describe.Bytes(f.Size)}";
        }
        else if (sel.Count == 0)
        {
            Preview.ShowSummary(ArchiveName, ArchiveSummary());
            Status = DirStatus();
        }
        else
        {
            long bytes = sel.Sum(r => PakArchive.TotalSize(r.Node));
            int files = sel.Sum(r => r.Node is PakFile ? 1 : ((PakDirectory)r.Node).Files().Count());
            Preview.ShowSummary(sel.Count == 1 ? sel[0].Node.Path : $"{sel.Count} items selected", $"{files:N0} files, {Describe.Bytes(bytes)} uncompressed.");
            Status = $"{sel.Count} selected - {files:N0} files, {Describe.Bytes(bytes)}";
        }
    }

    string DirStatus()
    {
        if (FilterText.Length > 0) return $"{List.Items.Count:N0} match(es) in the whole archive";
        var d = CurrentDir;
        int dirs = d.Children.Count(c => c.IsDirectory), files = d.Children.Count - dirs;
        return $"{files:N0} files, {dirs:N0} folders here - {List.Items.Count:N0} shown";
    }

    string ArchiveSummary()
    {
        if (_archive is null) return "";
        var files = _archive.AllFiles().ToList();
        long total = files.Sum(f => (long)f.Size);
        int deflated = files.Count(f => f.IsCompressed);
        return $"{ArchiveName}\n\n{files.Count:N0} files in {_archive.Root.Descendants().Count(n => n.IsDirectory):N0} folders\n" +
               $"{Describe.Bytes(total)} uncompressed, {deflated:N0} files deflated\n" +
               (_archive.FilePath is { } p ? $"\n{p}\n{Describe.Bytes(new FileInfo(p).Length)} on disk\nheader flags {_archive.HeaderFlags}\n" : "\n(not saved yet)\n") +
               (_archive.IsDirty ? $"\n{_archive.ChangeCount} unsaved change(s)" : "");
    }

    // ------------------------------------------------------------------------------------------ folder tree and file list

    /// <summary>Shows the archive after it was opened or changed: rebuilds the tree and the list, keeping the current folder.</summary>
    void RefreshAll(string? dirPath = null)
    {
        if (dirPath is not null) CurrentPath = dirPath;
        if (_archive is null) { ShowNoArchive(); return; }
        _rootItem = DirItem.Build(_archive.Root, ArchiveName);
        _rootItem.IsExpanded = true;
        var item = _rootItem.FindByPath(CurrentPath) ?? _rootItem;
        CurrentPath = item.Dir.Path;
        item.ExpandParents();
        _updatingTree = true;
        try { TreeRoots = [_rootItem]; SelectedDir = item; }
        finally { _updatingTree = false; }
        RebuildList();
        UpdateState();
    }

    void ShowNoArchive()
    {
        _rootItem = null;
        _updatingTree = true;
        try { TreeRoots = []; SelectedDir = null; }
        finally { _updatingTree = false; }
        _swappingList = true;
        try { List.SetSource([]); }
        finally { _swappingList = false; }
        Selection = [];
        PathText = "";
        ShowFolderColumn = false;
        Preview.Clear("Open an archive (File > Open, or drop a .pak / .p00 file here).");
        UpdateState();
    }

    partial void OnSelectedDirChanged(DirItem? value)
    {
        if (_updatingTree || value is null) return;
        RecordNav(CurrentPath, value.Dir.Path);
        CurrentPath = value.Dir.Path;
        if (FilterText.Length > 0) FilterText = "";   // its change handler rebuilds the list
        else { RebuildList(); UpdateState(); }
    }

    partial void OnFilterTextChanged(string value)
    {
        RebuildList();
        UpdateState();
    }

    static Regex GlobToRegex(string pattern)
    {
        if (!pattern.Contains('*') && !pattern.Contains('?')) pattern = "*" + pattern + "*";
        var rx = "^" + Regex.Escape(pattern).Replace("\\*", ".*").Replace("\\?", ".") + "$";
        return new Regex(rx, RegexOptions.IgnoreCase | RegexOptions.CultureInvariant | RegexOptions.Singleline);
    }

    /// <summary>The list shows the current folder, or with a search text every match in the whole archive.</summary>
    void RebuildList()
    {
        if (_archive is null) return;
        var keep = Selection.Select(r => r.Node).ToHashSet();
        var text = FilterText.Trim();
        bool search = text.Length > 0;
        IEnumerable<PakNode> nodes;
        if (search)
        {
            var rx = GlobToRegex(text);
            bool byPath = text.Contains('/');
            nodes = _archive.Root.Descendants().Where(n => rx.IsMatch(byPath ? n.Path : n.Name)).Take(20000);
        }
        else nodes = CurrentDir.Children.OrderBy(n => n.Name, StringComparer.OrdinalIgnoreCase);   // folders first is the list's own rule

        _swappingList = true;   // the grid empties its selection when the items change; that is not the user's doing
        try { List.SetSource(nodes.Select(n => new EntryRow(n, search)).ToList()); }
        finally { _swappingList = false; }
        ShowFolderColumn = search;
        PathText = search ? $"Search results for '{text}' ({List.Items.Count}{(List.Items.Count >= 20000 ? "+" : "")})" : $"{ArchiveName}/{CurrentPath}";
        RequestSelection(List.Items.Where(r => keep.Contains(r.Node)).ToList());
    }

    /// <summary>Title, status line, which commands are available.</summary>
    void UpdateState()
    {
        Title = _archive is null ? "Risen PakView" : $"{ArchiveName}{(_archive.IsDirty ? " *" : "")} - Risen PakView";
        PendingText = _archive is { IsDirty: true } ? $"{_archive.ChangeCount} unsaved change(s)" : "";
        RefreshCommands();
    }

    // ------------------------------------------------------------------------------------------ history (back / forward / up)

    /// <summary>(back, forward) entries; for the developer script's log.</summary>
    public (int Back, int Forward) History => (_back.Count, _forward.Count);

    void RecordNav(string from, string to)
    {
        if (_navigating || string.Equals(from, to, StringComparison.OrdinalIgnoreCase)) return;
        _back.Push(from);
        _forward.Clear();
    }

    void GoHistory(Stack<string> from, Stack<string> to)
    {
        while (from.Count > 0)
        {
            var target = from.Pop();
            if (_archive?.Find(target) is not PakDirectory) continue;   // deleted or renamed since: skip it
            to.Push(CurrentPath);
            _navigating = true;
            try { NavigateTo(target); }
            finally { _navigating = false; }
            UpdateState();
            return;
        }
    }

    public void NavigateTo(string dirPath)
    {
        var item = _rootItem?.FindByPath(dirPath);
        if (item is null) return;
        item.ExpandParents();
        if (FilterText.Length > 0)
        {
            // leaving search results: the tree selection must not rebuild the list twice
            RecordNav(CurrentPath, item.Dir.Path);
            CurrentPath = item.Dir.Path;
            _updatingTree = true;
            try { SelectedDir = item; }
            finally { _updatingTree = false; }
            FilterText = "";
            return;
        }
        SelectedDir = item;
    }

    bool CanGoBack => CanEditArchive && _back.Count > 0;
    bool CanGoForward => CanEditArchive && _forward.Count > 0;
    bool CanGoUp => CanEditArchive && FilterText.Length == 0 && CurrentPath.Length > 0;

    [RelayCommand(CanExecute = nameof(CanGoBack))]
    void Back() => GoHistory(_back, _forward);

    [RelayCommand(CanExecute = nameof(CanGoForward))]
    void Forward() => GoHistory(_forward, _back);

    /// <summary>With a search text the first "up" ends the search; then it goes to the parent folder.</summary>
    [RelayCommand(CanExecute = nameof(CanEditArchive))]
    void Up()
    {
        if (FilterText.Length > 0) FilterText = "";
        else if (CurrentDir.Parent is { } parent) NavigateTo(parent.Path);
    }

    // ------------------------------------------------------------------------------------------ command availability

    bool CanUseFiles => !IsBusy;
    bool CanEditArchive => _archive is not null && !IsBusy;
    bool CanSave => CanEditArchive && (_archive!.IsDirty || _archive.FilePath is null);
    bool CanDiscard => CanEditArchive && _archive!.IsDirty && _archive.FilePath is not null;
    bool HasSelection => CanEditArchive && Selection.Count > 0;
    bool OneSelected => CanEditArchive && Selection.Count == 1;
    bool OneFileSelected => OneSelected && Selection[0].Node is PakFile;

    void RefreshCommands()
    {
        foreach (var command in new IRelayCommand[]
                 {
                     NewArchiveCommand, OpenCommand, SaveCommand, SaveAsCommand, DiscardCommand, CloseArchiveCommand,
                     AddFilesCommand, AddFolderCommand, NewFolderCommand, ReplaceCommand, RenameCommand, DeleteCommand,
                     ExtractCommand, ExtractAllCommand, ExtractAsCommand, EditExternallyCommand, CopyPathCommand, SelectAllCommand,
                     VerifyCommand, OpenSelectedCommand, DropPathsCommand, BackCommand, ForwardCommand, UpCommand,
                 })
            command.NotifyCanExecuteChanged();
    }

    public void Dispose()
    {
        _external.Dispose();
        Preview.Dispose();
        _archive?.Dispose();
    }
}
