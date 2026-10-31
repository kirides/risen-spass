using System.Text.RegularExpressions;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.Input;
using PakView.Models;
using Risen.Pak;
using Risen.Pak.Formats;

namespace PakView.ViewModels;

// The commands behind the menu, toolbar, shortcuts and context menu. Each one that can fail on I/O runs inside Guard,
// which turns an exception into a message box instead of a crash.
public sealed partial class MainViewModel
{
    async Task Guard(Func<Task> work)
    {
        try { await work(); }
        catch (OperationCanceledException) { Status = "Cancelled."; }
        catch (Exception ex) { await _shell.Info("Error", ex.Message); }
    }

    CancellationTokenSource? _busyCts;

    /// <summary>Runs long work off the UI thread with a progress bar; the window disables itself while <see cref="IsBusy"/>.</summary>
    async Task RunBusy(string label, Func<IProgress<double>, CancellationToken, Task> work, bool cancellable = true)
    {
        IsBusy = true;
        BusyProgress = 0;
        BusyIndeterminate = true;
        CanCancel = cancellable;
        Status = label;
        _busyCts = new CancellationTokenSource();
        var progress = new Progress<double>(v => { BusyIndeterminate = false; BusyProgress = v; });
        try { await work(progress, _busyCts.Token); }
        finally
        {
            _busyCts.Dispose();
            _busyCts = null;
            IsBusy = false;
            CanCancel = false;
        }
    }

    [RelayCommand]
    void Cancel() => _busyCts?.Cancel();

    static IProgress<(int done, int total)> AsFraction(IProgress<double> progress) =>
        new Progress<(int done, int total)>(t => progress.Report(t.total == 0 ? 1 : (double)t.done / t.total));

    /// <summary>For the window's closing event: false keeps it open.</summary>
    public async Task<bool> CanCloseAsync()
    {
        if (IsBusy)
        {
            await _shell.Info("Busy", "An operation is running. Cancel it first.");
            return false;
        }
        return await ConfirmDiscard();
    }

    // ------------------------------------------------------------------------------------------ archive: new, open, save, close

    async Task<bool> ConfirmDiscard()
    {
        if (_archive is not { IsDirty: true }) return true;
        int answer = await _shell.Ask("Unsaved changes", $"'{ArchiveName}' has {_archive.ChangeCount} unsaved change(s).", "Save", "Don't save", "Cancel");
        return answer switch { 0 => await SaveTo(null), 1 => true, _ => false };
    }

    /// <summary>Replaces the open archive (null = none) and shows it from the root.</summary>
    void SetArchive(PakArchive? archive)
    {
        _archive?.Dispose();
        _archive = archive;
        _back.Clear();
        _forward.Clear();
        FilterText = "";
        RefreshAll("");
    }

    [RelayCommand(CanExecute = nameof(CanUseFiles))]
    async Task NewArchive() => await Guard(async () =>
    {
        if (!await ConfirmDiscard()) return;
        SetArchive(PakArchive.CreateNew());
        Status = "New empty archive. Add files, then Save.";
    });

    [RelayCommand(CanExecute = nameof(CanUseFiles))]
    async Task Open() => await Guard(async () =>
    {
        if (await _shell.PickArchiveToOpen() is { } path) await OpenArchive(path);
    });

    public async Task OpenArchive(string path)
    {
        if (IsBusy || !await ConfirmDiscard()) return;
        PakArchive? opened = null;
        await RunBusy($"Opening {Path.GetFileName(path)}...", async (_, _) => opened = await Task.Run(() => PakArchive.Open(path)), cancellable: false);
        SetArchive(opened);
        Status = $"{Path.GetFileName(path)}: {opened!.AllFiles().Count():N0} files";
    }

    [RelayCommand(CanExecute = nameof(CanEditArchive))]
    async Task CloseArchive() => await Guard(async () =>
    {
        if (!await ConfirmDiscard()) return;
        SetArchive(null);
        Status = "";
    });

    [RelayCommand(CanExecute = nameof(CanDiscard))]
    async Task Discard() => await Guard(async () =>
    {
        if (!await _shell.Confirm("Discard changes", "Reload the archive from disk and drop all unsaved changes?", "Discard", "Keep")) return;
        var path = _archive!.FilePath!;
        _archive.Dispose();
        _archive = PakArchive.Open(path);
        RefreshAll();
    });

    [RelayCommand(CanExecute = nameof(CanSave))]
    async Task Save() => await Guard(() => SaveTo(null));

    [RelayCommand(CanExecute = nameof(CanEditArchive))]
    async Task SaveAs() => await Guard(async () =>
    {
        var path = await _shell.PickArchiveToSave(_archive!.FilePath is { } p ? Path.GetFileName(p) : "new.pak");
        if (path is not null) await SaveTo(path);
    });

    /// <summary>Writes a temp file next to the target and switches it in atomically (see <see cref="PakArchive.Save"/>).</summary>
    async Task<bool> SaveTo(string? destination)
    {
        var archive = _archive!;
        if (destination is null && archive.FilePath is null)
        {
            destination = await _shell.PickArchiveToSave("new.pak");
            if (destination is null) return false;
        }
        var options = new SaveOptions { Compression = CompressNewFiles ? PakCompression.Auto : PakCompression.Never, KeepBackup = KeepBackup };
        try
        {
            await RunBusy("Saving (writing a temporary file, then switching it in)...",
                (progress, ct) => Task.Run(() => archive.Save(destination, options, progress, ct), ct));
        }
        catch (OperationCanceledException) { Status = "Save cancelled; the archive on disk is unchanged."; return false; }
        catch (Exception ex)
        {
            await _shell.Info("Save failed", ex.Message + "\n\nThe archive on disk was not changed.");
            return false;
        }
        RefreshAll();
        Status = $"Saved {ArchiveName}.";
        return true;
    }

    [RelayCommand]
    void Exit() => _shell.Close();

    // ------------------------------------------------------------------------------------------ editing

    [RelayCommand(CanExecute = nameof(CanEditArchive))]
    async Task AddFiles() => await Guard(async () => await AddPaths(await _shell.PickFiles("Add files", multiple: true)));

    [RelayCommand(CanExecute = nameof(CanEditArchive))]
    async Task AddFolder() => await Guard(async () => await AddPaths(await _shell.PickFolders("Add folder", multiple: true)));

    async Task AddPaths(IReadOnlyList<string> paths)
    {
        if (_archive is null || paths.Count == 0) return;
        var archive = _archive;
        var target = CurrentDir.Path;
        var clashes = paths.Where(File.Exists).Select(Path.GetFileName).Where(n => CurrentDir.Find(n!) is PakFile).ToList();
        if (clashes.Count > 0 && !await _shell.Confirm("Replace existing files",
                $"{clashes.Count} file(s) exist already in this folder ({string.Join(", ", clashes.Take(3))}{(clashes.Count > 3 ? ", ..." : "")}). Replace them?", "Replace", "Cancel"))
            return;
        PakArchive.AddResult total = new(0, 0);
        await RunBusy("Adding...", (_, ct) => Task.Run(() =>
        {
            var dir = archive.EnsureDirectory(target);
            foreach (var path in paths)
            {
                var result = archive.AddFromDisk(dir, path, overwrite: true, ct);
                total = new(total.Added + result.Added, total.Replaced + result.Replaced);
            }
        }, ct));
        RefreshAll();
        Status = $"Added {total.Added:N0}, replaced {total.Replaced:N0} file(s). Not saved yet.";
    }

    /// <summary>Files dropped on the window: open an archive, or add the files to the open one.</summary>
    [RelayCommand(CanExecute = nameof(CanUseFiles))]
    async Task DropPaths(IReadOnlyList<string> paths) => await Guard(async () =>
    {
        if (paths.Count == 0) return;
        bool looksLikeArchive = paths.Count == 1 && File.Exists(paths[0]) && Regex.IsMatch(Path.GetExtension(paths[0]), @"^\.(pak|p\d\d)$", RegexOptions.IgnoreCase);
        if (_archive is null)
        {
            if (looksLikeArchive) await OpenArchive(paths[0]);
            else await _shell.Info("No archive", "Open or create an archive first.");
            return;
        }
        if (looksLikeArchive)
        {
            int answer = await _shell.Ask("Dropped archive", $"{Path.GetFileName(paths[0])}: open it, or add it as a file to this archive?", "Open", "Add as file", "Cancel");
            if (answer == 0) { await OpenArchive(paths[0]); return; }
            if (answer != 1) return;
        }
        await AddPaths(paths);
    });

    [RelayCommand(CanExecute = nameof(CanEditArchive))]
    async Task NewFolder() => await Guard(async () =>
    {
        var name = await _shell.Input("New folder", "Name of the new folder:");
        if (string.IsNullOrWhiteSpace(name)) return;
        _archive!.AddDirectory(CurrentDir, name.Trim());
        RefreshAll();
    });

    [RelayCommand(CanExecute = nameof(OneSelected))]
    async Task Rename() => await Guard(async () =>
    {
        var row = Selection[0];
        var name = await _shell.Input("Rename", "New name:", row.Name);
        if (string.IsNullOrWhiteSpace(name) || name == row.Name) return;
        _archive!.Rename(row.Node, name.Trim());
        RefreshAll();
    });

    [RelayCommand(CanExecute = nameof(HasSelection))]
    async Task Delete() => await Guard(async () =>
    {
        var rows = Selection;
        int files = rows.Sum(r => r.Node is PakFile ? 1 : ((PakDirectory)r.Node).Files().Count());
        var what = rows.Count == 1 ? $"'{rows[0].Name}'" : $"{rows.Count} items";
        if (!await _shell.Confirm("Delete", $"Remove {what} ({files:N0} file(s)) from the archive?\nThe file on disk stays as it is until you save.", "Delete", "Cancel")) return;
        foreach (var row in rows) _archive!.Remove(row.Node);
        RefreshAll();
    });

    [RelayCommand(CanExecute = nameof(OneFileSelected))]
    async Task Replace() => await Guard(async () =>
    {
        var file = (PakFile)Selection[0].Node;
        var path = (await _shell.PickFiles($"Replace {file.Name} with...", multiple: false)).FirstOrDefault();
        if (path is null) return;
        _archive!.ReplaceContent(file, new DiskContent(path));
        var archivePath = file.Path;
        RefreshAll();
        SelectPaths([archivePath]);
        Status = $"{archivePath} will be replaced on save.";
    });

    // ------------------------------------------------------------------------------------------ extracting and opening

    [RelayCommand(CanExecute = nameof(HasSelection))]
    async Task Extract() => await Guard(() => ExtractNodes(Selection.Select(r => r.Node).ToList(), everything: false));

    [RelayCommand(CanExecute = nameof(CanEditArchive))]
    async Task ExtractAll() => await Guard(() => ExtractNodes(_archive!.Root.Children.ToList(), everything: true));

    async Task ExtractNodes(List<PakNode> nodes, bool everything)
    {
        if (nodes.Count == 0) return;
        var destination = (await _shell.PickFolders("Extract to folder", multiple: false)).FirstOrDefault();
        if (destination is null) return;
        var archive = _archive!;
        string basePath = everything || FilterText.Length > 0 ? "" : CurrentDir.Path;
        PakArchive.ExtractResult? result = null;
        await RunBusy("Extracting...", async (progress, ct) =>
            result = await Task.Run(() => archive.Extract(nodes, destination, basePath, AsFraction(progress), ct), ct));
        Status = $"Extracted {result!.Files:N0} file(s) ({Describe.Bytes(result.Bytes)}) to {destination}" +
                 (result.Skipped > 0 ? $" - {result.Skipped} skipped (unsafe names)" : "");
    }

    /// <summary>"Extract as": the selected sounds / images (folders are walked) as MP3, WAV, DDS, TGA or PNG.</summary>
    [RelayCommand(CanExecute = nameof(CanEditArchive))]
    async Task ExtractAs(ExportFormat format) => await Guard(() => ExtractAsTo(format, null));

    /// <param name="destination">null asks for a folder.</param>
    public async Task ExtractAsTo(ExportFormat format, string? destination)
    {
        if (_archive is null || IsBusy) return;
        var nodes = Selection.Select(r => r.Node).ToList();
        var ext = AssetExport.Extension(format);
        var kind = format is ExportFormat.Mp3 or ExportFormat.Wav ? "sound (._xsnd)" : "image (._ximg)";
        int applicable = nodes.SelectMany(n => n is PakFile f ? [f] : ((PakDirectory)n).Files()).Count(f => AssetExport.CanConvert(f.Name, format));
        if (applicable == 0)
        {
            await _shell.Info("Extract as " + ext, $"Nothing in the selection can be extracted as {ext}: select {kind} files or folders that contain them.");
            return;
        }
        destination ??= (await _shell.PickFolders($"Extract {applicable:N0} file(s) as {ext} to folder", multiple: false)).FirstOrDefault();
        if (destination is null) return;
        var archive = _archive;
        string basePath = FilterText.Length > 0 ? "" : CurrentDir.Path;
        ConvertResult? result = null;
        await RunBusy($"Extracting as {ext}...", async (progress, ct) =>
            result = await Task.Run(() => archive.ExtractAs(nodes, destination, format, basePath, AsFraction(progress), ct), ct));
        Status = $"Extracted {result!.Written:N0} file(s) as {ext} ({Describe.Bytes(result.Bytes)}) to {destination}" +
                 (result.NotApplicable > 0 ? $" - {result.NotApplicable:N0} other file(s) not applicable" : "") + (result.Failed > 0 ? $" - {result.Failed} failed" : "");
        if (result.Failed > 0) await _shell.Info("Some files failed", string.Join(Environment.NewLine, result.Errors.Take(15)));
    }

    /// <summary>Enter / double-click: a folder opens, a sound plays (its preview is already showing), anything else is edited externally.</summary>
    [RelayCommand(CanExecute = nameof(OneSelected))]
    async Task OpenSelected() => await Guard(() =>
    {
        var row = Selection[0];
        if (row.Node is PakDirectory dir) NavigateTo(dir.Path);
        else if (AudioReader.IsAudioName(row.Name)) Preview.RequestPlay();
        else EditExternally();
        return Task.CompletedTask;
    });

    /// <summary>Extracts the file to a temp folder and opens it with its program; every save there is staged into the archive.</summary>
    [RelayCommand(CanExecute = nameof(OneFileSelected))]
    void EditExternally()
    {
        var file = (PakFile)Selection[0].Node;
        _external.Open(_archive!, file);
        Status = $"Opened {file.Name} externally. Saving it in the editor stages the change here (then Save the archive).";
    }

    void OnExternalChanged(PakArchive archive, string path, byte[] data) => Dispatcher.UIThread.Post(() =>
    {
        if (!ReferenceEquals(archive, _archive) || IsBusy) { Status = $"Ignored an external edit of {path}: that archive is not the open one (or busy)."; return; }
        if (archive.Find(path) is not PakFile file) { Status = $"Ignored an external edit of {path}: it is not in the archive any more."; return; }
        archive.ReplaceContent(file, new MemoryContent(data));
        var keep = Selection.Select(r => r.Node.Path).Take(1).ToList();
        RefreshAll();
        SelectPaths(keep);
        Status = $"Staged the edited {path} ({Describe.Bytes(data.Length)}). Save the archive to keep it.";
    });

    [RelayCommand(CanExecute = nameof(HasSelection))]
    async Task CopyPath() => await Guard(async () =>
    {
        await _shell.CopyToClipboard(string.Join(Environment.NewLine, Selection.Select(r => r.Node.Path)));
        Status = "Path copied.";
    });

    // ------------------------------------------------------------------------------------------ tools

    [RelayCommand(CanExecute = nameof(CanEditArchive))]
    async Task Verify() => await Guard(async () =>
    {
        var archive = _archive!;
        var problems = new List<string>();
        int count = 0;
        await RunBusy("Verifying...", (progress, ct) => Task.Run(() =>
        {
            var files = archive.AllFiles().ToList();
            for (int i = 0; i < files.Count; i++)
            {
                ct.ThrowIfCancellationRequested();
                try
                {
                    using var stream = archive.OpenRead(files[i]);
                    long n = 0;
                    var buffer = new byte[1 << 16];
                    int read;
                    while ((read = stream.Read(buffer, 0, buffer.Length)) > 0) n += read;
                    if (n != files[i].Size) problems.Add($"{files[i].Path}: {n} bytes inflated, table says {files[i].Size}");
                }
                catch (Exception ex) { problems.Add($"{files[i].Path}: {ex.Message}"); }
                if (i % 64 == 0) progress.Report((double)i / files.Count);
                count++;
            }
        }, ct));
        await _shell.Info("Verify", problems.Count == 0
            ? $"All {count:N0} members read back with the size the table of contents promises."
            : $"{problems.Count} problem(s) in {count:N0} members:\n\n" + string.Join("\n", problems.Take(25)));
    });

    [RelayCommand]
    async Task About() => await _shell.Info("Risen PakView",
        "Browser and editor for Risen archives (*.pak, *.p00 ...).\n\n" +
        "Select a file to preview it without extracting. Edits (add, replace, rename, delete) are collected and written when you save: " +
        "the whole archive is written to a temporary file next to the original, checked, and only then switched in atomically.");
}
