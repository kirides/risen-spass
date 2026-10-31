namespace PakView;

/// <summary>Command line: PakView [archive] [--dir path] [--filter text] [--select path] [--shot out.png [--delay ms]] [--nav "dir:a/b;alt+up;x1;dbl:name" --nav-log f.txt]
/// (--shot renders the window to a PNG after the preview settled and exits; used to check the UI without a screen grab).</summary>
public sealed record StartupOptions
{
    public string? OpenPath { get; init; }
    public string? Dir { get; init; }
    public string? Filter { get; init; }
    public string? Select { get; init; }
    public string? ShotPath { get; init; }
    public string? Nav { get; init; }
    public string? NavLog { get; init; }
    public int ShotDelayMs { get; init; } = 2500;

    public static StartupOptions Parse(string[] args)
    {
        var o = new StartupOptions();
        for (int i = 0; i < args.Length; i++)
        {
            string a = args[i];
            string? Next() => i + 1 < args.Length ? args[++i] : null;
            o = a switch
            {
                "--dir" => o with { Dir = Next() },
                "--filter" => o with { Filter = Next() },
                "--select" => o with { Select = Next() },
                "--shot" => o with { ShotPath = Next() is { } sp ? Path.GetFullPath(sp) : null },
                "--nav" => o with { Nav = Next() },
                "--nav-log" => o with { NavLog = Next() is { } nl ? Path.GetFullPath(nl) : null },
                "--delay" => o with { ShotDelayMs = int.TryParse(Next(), out var d) ? d : 2500 },
                _ when !a.StartsWith("--") && File.Exists(a) => o with { OpenPath = Path.GetFullPath(a) },
                _ => o,
            };
        }
        return o;
    }
}
