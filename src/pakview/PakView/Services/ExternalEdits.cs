using System.Diagnostics;
using Risen.Pak;

namespace PakView.Services;

/// <summary>
/// "Edit externally" like WinRAR / 7-Zip: the member is extracted to a private temp folder and opened with the associated
/// program; every save of that file in the editor is reported through <see cref="Changed"/> so the archive can stage it.
/// </summary>
public sealed class ExternalEdits : IDisposable
{
    static readonly HashSet<string> Dangerous = new(StringComparer.OrdinalIgnoreCase)
    {
        ".exe", ".com", ".bat", ".cmd", ".scr", ".msi", ".msp", ".ps1", ".psm1", ".vbs", ".vbe", ".js", ".jse", ".wsf", ".wsh",
        ".lnk", ".hta", ".cpl", ".reg", ".dll", ".jar", ".sh", ".app", ".appx", ".msix", ".url", ".pif", ".gadget", ".inf",
    };

    sealed class Session
    {
        public required PakArchive Archive { get; init; }
        public required string ArchivePath { get; init; }
        public required string TempFile { get; init; }
        public required FileSystemWatcher Watcher { get; init; }
        public DateTime LastSeen;
        public int Pending;
    }

    readonly string _root = Path.Combine(Path.GetTempPath(), "PakView-" + Guid.NewGuid().ToString("N")[..10]);
    readonly List<Session> _sessions = new();

    /// <summary>(archive, path inside it, new bytes). Raised on a thread pool thread.</summary>
    public event Action<PakArchive, string, byte[]>? Changed;

    public static bool IsRisky(string fileName) => Dangerous.Contains(Path.GetExtension(fileName));

    public void Open(PakArchive archive, PakFile file)
    {
        if (IsRisky(file.Name)) throw new PakException($"'{file.Name}' is a program or script type; it is not opened from an archive. Extract it instead.");
        if (PakFormat.ValidateName(file.Name) is { } why) throw new PakException(why);

        var dir = Path.Combine(_root, Guid.NewGuid().ToString("N")[..8]);
        Directory.CreateDirectory(dir);
        var temp = Path.Combine(dir, file.Name);
        using (var src = archive.OpenRead(file))
        using (var dst = new FileStream(temp, FileMode.CreateNew, FileAccess.Write, FileShare.Read))
            src.CopyTo(dst);

        var s = new Session
        {
            Archive = archive, ArchivePath = file.Path, TempFile = temp, LastSeen = File.GetLastWriteTimeUtc(temp),
            Watcher = new FileSystemWatcher(dir, file.Name) { NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.Size | NotifyFilters.FileName },
        };
        s.Watcher.Changed += (_, _) => Debounce(s);
        s.Watcher.Created += (_, _) => Debounce(s);   // editors that save by rename
        s.Watcher.Renamed += (_, _) => Debounce(s);
        s.Watcher.EnableRaisingEvents = true;
        lock (_sessions) _sessions.Add(s);

        Launch(temp);
    }

    static void Launch(string path)
    {
        var psi = OperatingSystem.IsWindows()
            ? new ProcessStartInfo(path) { UseShellExecute = true }
            : new ProcessStartInfo(OperatingSystem.IsMacOS() ? "open" : "xdg-open") { ArgumentList = { path }, UseShellExecute = false };
        Process.Start(psi)?.Dispose();
    }

    async void Debounce(Session s)
    {
        if (Interlocked.Exchange(ref s.Pending, 1) == 1) return;
        try
        {
            await Task.Delay(500);
            Interlocked.Exchange(ref s.Pending, 0);
            for (int attempt = 0; attempt < 10; attempt++)
            {
                try
                {
                    var stamp = File.GetLastWriteTimeUtc(s.TempFile);
                    if (stamp == s.LastSeen) return;
                    var bytes = File.ReadAllBytes(s.TempFile);
                    s.LastSeen = stamp;
                    Changed?.Invoke(s.Archive, s.ArchivePath, bytes);
                    return;
                }
                catch (IOException) { await Task.Delay(300); }   // the editor still holds the file
                catch (UnauthorizedAccessException) { await Task.Delay(300); }
            }
        }
        catch { Interlocked.Exchange(ref s.Pending, 0); }
    }

    public void Dispose()
    {
        lock (_sessions)
        {
            foreach (var s in _sessions) s.Watcher.Dispose();
            _sessions.Clear();
        }
        try { if (Directory.Exists(_root)) Directory.Delete(_root, true); } catch { /* an editor may still hold a file */ }
    }
}
