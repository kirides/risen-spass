namespace PakView.Models;

/// <summary>Human readable texts about files: sizes, kinds, list icons.</summary>
public static class Describe
{
    public static string Bytes(long n) => n switch
    {
        < 1024 => $"{n} B",
        < 1024 * 1024 => $"{n / 1024.0:0.#} KiB",
        < 1024L * 1024 * 1024 => $"{n / 1048576.0:0.#} MiB",
        _ => $"{n / 1073741824.0:0.##} GiB",
    };

    static string Kind(string name)
    {
        var l = name.ToLowerInvariant();
        if (l.EndsWith("._ximg") || l.EndsWith(".png") || l.EndsWith(".jpg") || l.EndsWith(".jpeg") || l.EndsWith(".bmp") || l.EndsWith(".dds") || l.EndsWith(".tga") || l.EndsWith(".gif")) return "image";
        if (l.EndsWith("._xsnd") || l.EndsWith(".wav") || l.EndsWith(".ogg") || l.EndsWith(".mp3")) return "sound";
        if (l.EndsWith(".tab") || l.EndsWith(".csv")) return "table";
        if (l.EndsWith(".tple")) return "template";
        if (l.Contains("._x")) return "resource";
        return "file";
    }

    public static string IconFor(string name) => Kind(name) switch
    {
        "image" => "▨", "sound" => "♪", "table" => "▦", "template" => "◆", "resource" => "▣", _ => "▫",
    };

    public static string IconBrushFor(string name) => Kind(name) switch
    {
        "image" => "#5BB974", "sound" => "#C77DFF", "table" => "#4DA6FF", "template" => "#FF8C5A", "resource" => "#9AA5B1", _ => "#8A8F98",
    };

    public static string TypeOf(string name)
    {
        var lower = name.ToLowerInvariant();
        int us = lower.LastIndexOf("._x", StringComparison.Ordinal);
        if (us >= 0) return lower[(us + 3)..] switch
        {
            "img" => "Compiled image",
            "msh" => "Compiled mesh",
            "mat" => "Compiled material",
            "snd" => "Compiled sound",
            "mac" => "Compiled actor",
            var o => "Compiled " + o,
        };
        var e = Path.GetExtension(lower);
        return e switch
        {
            ".tple" => "Template",
            ".tab" => "String table",
            ".bin" => "Library data",
            "" => "File",
            _ => e[1..].ToUpperInvariant() + " file",
        };
    }
}
