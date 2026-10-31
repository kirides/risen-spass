using System.Text;
using Risen.Pak;
using Risen.Pak.Formats;
using Xunit;

namespace Risen.Pak.Tests;

public sealed class TempDir : IDisposable
{
    public string Path { get; } = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "pakview-test-" + Guid.NewGuid().ToString("N"));
    public TempDir() => Directory.CreateDirectory(Path);
    public string File(string name) => System.IO.Path.Combine(Path, name);
    public void Dispose() { try { Directory.Delete(Path, true); } catch { } }
}

public class PakTests
{
    static byte[] Text(string s) => Encoding.UTF8.GetBytes(s);
    static byte[] Noise(int n, int seed) { var b = new byte[n]; new Random(seed).NextBytes(b); return b; }

    static PakArchive Build(string path)
    {
        var a = PakArchive.CreateNew();
        var items = a.EnsureDirectory("Items/Weapons");
        a.AddFile(items, "sword.tple", new MemoryContent(Text(new string('a', 5000))));
        a.AddFile(a.Root, "readme.txt", new MemoryContent(Text("hello")));
        a.AddFile(items, "noise.bin", new MemoryContent(Noise(3000, 1)));
        a.Save(path);
        return a;
    }

    [Fact]
    public void NewArchiveRoundTrips()
    {
        using var t = new TempDir();
        var path = t.File("a.pak");
        using (Build(path)) { }
        using var b = PakArchive.Open(path);
        Assert.Equal(3, b.AllFiles().Count());
        Assert.Equal("hello", Encoding.UTF8.GetString(b.ReadAll((PakFile)b.Find("README.TXT")!)));
        var sword = (PakFile)b.Find("items/weapons/sword.tple")!;
        Assert.True(sword.IsCompressed);
        Assert.Equal(new string('a', 5000), Encoding.UTF8.GetString(b.ReadAll(sword)));
        Assert.False(((PakFile)b.Find("Items/Weapons/noise.bin")!).IsCompressed);
        Assert.Empty(Directory.GetFiles(t.Path, "*.tmp"));
    }

    [Fact]
    public void UntouchedMembersAreCopiedVerbatim()
    {
        using var t = new TempDir();
        var path = t.File("a.pak");
        using (Build(path)) { }
        byte[] before;
        using (var a = PakArchive.Open(path))
        {
            var sword = (PakFile)a.Find("Items/Weapons/sword.tple")!;
            before = new byte[sword.StoredSize];
            using (var fs = File.OpenRead(path)) { fs.Position = sword.Offset; fs.ReadExactly(before); }
            a.AddFile(a.Root, "new.txt", new MemoryContent(Text("x")));
            a.Remove(a.Find("readme.txt")!);
            a.Save();
        }
        using var b = PakArchive.Open(path);
        Assert.Null(b.Find("readme.txt"));
        Assert.NotNull(b.Find("new.txt"));
        var s2 = (PakFile)b.Find("Items/Weapons/sword.tple")!;
        var after = new byte[s2.StoredSize];
        using (var fs = File.OpenRead(path)) { fs.Position = s2.Offset; fs.ReadExactly(after); }
        Assert.Equal(before, after);
    }

    [Fact]
    public void ReplaceRenameMoveSurviveSave()
    {
        using var t = new TempDir();
        var path = t.File("a.pak");
        using var a = Build(path);
        a.ReplaceContent((PakFile)a.Find("readme.txt")!, new MemoryContent(Text("changed")));
        a.Rename(a.Find("Items")!, "Stuff");
        a.Move(a.Find("Stuff/Weapons/noise.bin")!, a.Root);
        Assert.True(a.IsDirty);
        a.Save();
        Assert.False(a.IsDirty);
        Assert.Equal("changed", Encoding.UTF8.GetString(a.ReadAll((PakFile)a.Find("readme.txt")!)));
        Assert.NotNull(a.Find("Stuff/Weapons/sword.tple"));
        Assert.Equal(Noise(3000, 1), a.ReadAll((PakFile)a.Find("noise.bin")!));
    }

    [Fact]
    public void SaveInPlaceKeepsBackupWhenAsked()
    {
        using var t = new TempDir();
        var path = t.File("a.pak");
        using var a = Build(path);
        var oldBytes = File.ReadAllBytes(path);
        a.AddFile(a.Root, "n.txt", new MemoryContent(Text("n")));
        a.Save(null, new SaveOptions { KeepBackup = true });
        Assert.Equal(oldBytes, File.ReadAllBytes(path + ".bak"));
        Assert.NotEqual(oldBytes, File.ReadAllBytes(path));
    }

    [Fact]
    public void FailedSaveLeavesOldArchiveAndNoTempFile()
    {
        using var t = new TempDir();
        var path = t.File("a.pak");
        using var a = Build(path);
        var before = File.ReadAllBytes(path);
        var src = t.File("src.bin");
        File.WriteAllBytes(src, new byte[100]);
        a.AddFile(a.Root, "late.bin", new DiskContent(src));
        File.WriteAllBytes(src, new byte[50]);
        Assert.Throws<PakException>(() => a.Save());
        Assert.Equal(before, File.ReadAllBytes(path));
        Assert.Empty(Directory.GetFiles(t.Path, "*.tmp"));
        using var again = PakArchive.Open(path);
        Assert.Equal(3, again.AllFiles().Count());
        Assert.Equal("hello", Encoding.UTF8.GetString(a.ReadAll((PakFile)a.Find("readme.txt")!)));
    }

    [Fact]
    public void CancelledSaveLeavesNothingBehind()
    {
        using var t = new TempDir();
        var path = t.File("a.pak");
        using var a = Build(path);
        a.AddFile(a.Root, "n.txt", new MemoryContent(Text("n")));
        var cts = new CancellationTokenSource(); cts.Cancel();
        Assert.ThrowsAny<OperationCanceledException>(() => a.Save(null, null, null, cts.Token));
        Assert.Empty(Directory.GetFiles(t.Path, "*.tmp"));
        Assert.True(a.IsDirty);
    }

    [Fact]
    public void NamesAreValidatedAndCaseInsensitive()
    {
        var a = PakArchive.CreateNew();
        Assert.Throws<PakException>(() => a.AddFile(a.Root, "a/b", new MemoryContent([])));
        Assert.Throws<PakException>(() => a.AddFile(a.Root, "\u20ac.txt", new MemoryContent([])));
        a.AddFile(a.Root, "Hello.txt", new MemoryContent(Text("1")));
        a.AddFile(a.Root, "HELLO.TXT", new MemoryContent(Text("2")));
        Assert.Single(a.AllFiles());
        Assert.Throws<PakException>(() => a.AddFile(a.Root, "HELLO.TXT", new MemoryContent([]), overwrite: false));
        var d = a.AddDirectory(a.Root, "D");
        Assert.Throws<PakException>(() => a.Move(d, d));
        Assert.Throws<PakException>(() => a.Rename(d, "hello.txt"));
    }

    [Fact]
    public void ExtractNeverEscapesTheTarget()
    {
        using var t = new TempDir();
        Assert.Null(PakArchive.SafeTargetPath(t.Path, "../evil.txt"));
        Assert.Null(PakArchive.SafeTargetPath(t.Path, "a/../../evil.txt"));
        Assert.Null(PakArchive.SafeTargetPath(t.Path, "C:/evil.txt"));
        Assert.NotNull(PakArchive.SafeTargetPath(t.Path, "a/b/c.txt"));
    }

    [Fact]
    public void AddFromDiskKeepsDirectoryStructure()
    {
        using var t = new TempDir();
        var src = Path.Combine(t.Path, "mod"); Directory.CreateDirectory(Path.Combine(src, "sub"));
        File.WriteAllText(Path.Combine(src, "a.txt"), "A");
        File.WriteAllText(Path.Combine(src, "sub", "b.txt"), "B");
        var a = PakArchive.CreateNew();
        var r = a.AddFromDisk(a.Root, src);
        Assert.Equal(2, r.Added);
        Assert.NotNull(a.Find("mod/sub/b.txt"));
        var outDir = Path.Combine(t.Path, "out");
        a.Extract([a.Find("mod")!], outDir);
        Assert.Equal("B", File.ReadAllText(Path.Combine(outDir, "mod", "sub", "b.txt")));
    }

    [Fact]
    public void DamagedArchivesAreRejected()
    {
        using var t = new TempDir();
        var path = t.File("a.pak");
        using (Build(path)) { }
        var bytes = File.ReadAllBytes(path);
        File.WriteAllBytes(t.File("short.pak"), bytes[..20]);
        Assert.Throws<PakException>(() => PakArchive.Open(t.File("short.pak")));
        File.WriteAllBytes(t.File("cut.pak"), bytes[..(bytes.Length - 10)]);
        Assert.ThrowsAny<IOException>(() => PakArchive.Open(t.File("cut.pak")));
        bytes[4] = (byte)'X';
        File.WriteAllBytes(t.File("magic.pak"), bytes);
        Assert.Throws<PakException>(() => PakArchive.Open(t.File("magic.pak")));
    }

    [Fact]
    public void TabAndHexHelpers()
    {
        using var ms = new MemoryStream();
        var w = new BinaryWriter(ms, Encoding.Unicode);
        w.Write("TAB0"u8.ToArray()); w.Write((ushort)1); w.Write((ushort)1); w.Write(0L); w.Write(2u);
        foreach (var (name, cells) in new[] { ("ID", new[] { "k1", "k2" }), ("English_Text", new[] { "one", "zwei \u00fc" }) })
        {
            w.Write((byte)1); w.Write((byte)1); w.Write((byte)0);
            w.Write((ushort)name.Length); w.Write(Encoding.Unicode.GetBytes(name));
            w.Write((uint)cells.Length);
            foreach (var c in cells) { w.Write((ushort)c.Length); w.Write(Encoding.Unicode.GetBytes(c)); }
        }
        var t = TabReader.Read(ms.ToArray());
        Assert.Equal(["ID", "English_Text"], t.Columns);
        Assert.Equal("zwei \u00fc", t.Rows[1][1]);
        Assert.True(HexDump.LooksLikeText("plain text\r\n"u8));
        Assert.False(HexDump.LooksLikeText([1, 2, 0, 3]));
        Assert.StartsWith("00000000  41 42", HexDump.Format("AB"u8));
    }

    [Fact]
    public void DdsDxt1Decodes()
    {
        var dds = new byte[128 + 8];
        "DDS "u8.CopyTo(dds); BitConverter.GetBytes(124).CopyTo(dds, 4);
        BitConverter.GetBytes(4).CopyTo(dds, 12); BitConverter.GetBytes(4).CopyTo(dds, 16);
        BitConverter.GetBytes(0x4).CopyTo(dds, 80); "DXT1"u8.CopyTo(dds.AsSpan(84));
        BitConverter.GetBytes((ushort)0xF800).CopyTo(dds, 128);
        BitConverter.GetBytes((ushort)0xF800).CopyTo(dds, 130);
        var bmp = Dds.Decode(dds);
        Assert.Equal((4, 4), (bmp.Width, bmp.Height));
        Assert.Equal(new byte[] { 0, 0, 255, 255 }, bmp.Bgra[..4]);
        Assert.Equal(0, Dds.FindPayload(dds));
    }

    /// <summary>Re-saves a copy of a real game archive and checks every member reads back identically.</summary>
    [Fact]
    public void RealArchiveResaveKeepsEveryMember()
    {
        var gameDir = Environment.GetEnvironmentVariable("RISEN_DIR") ?? @"B:\SteamLibrary\steamapps\common\Risen";
        var src = Path.Combine(gameDir, "data", "common", "library.pak");
        if (!File.Exists(src)) return;
        using var t = new TempDir();
        var copy = t.File("library.pak");
        File.Copy(src, copy);
        using var a = PakArchive.Open(copy);
        static string Sha(byte[] b) => Convert.ToHexString(System.Security.Cryptography.SHA1.HashData(b));
        var want = a.AllFiles().ToDictionary(f => f.Path, f => (f.Size, f.IsCompressed, Sha(a.ReadAll(f))));
        a.AddFile(a.Root, "extra.txt", new MemoryContent(Text("x")));
        a.Save();
        foreach (var f in a.AllFiles().Where(f => f.Path != "extra.txt"))
        {
            var (size, comp, sha) = want[f.Path];
            Assert.Equal(size, f.Size);
            Assert.Equal(comp, f.IsCompressed);
            Assert.Equal(sha, Sha(a.ReadAll(f)));
        }
        Assert.Equal(want.Count + 1, a.AllFiles().Count());
    }

    static string? GamePak(string rel)
    {
        var gameDir = Environment.GetEnvironmentVariable("RISEN_DIR") ?? @"B:\SteamLibrary\steamapps\common\Risen";
        var p = Path.Combine(gameDir, "data", "common", rel);
        return File.Exists(p) ? p : null;
    }

    [Fact]
    public void RealTemplatesParse()
    {
        if (GamePak("templates.pak") is not { } path) return;
        using var a = PakArchive.Open(path);
        var sword = TemplateReader.Read(a.ReadAll((PakFile)a.Find("Items/Weapons_Swords/It_1H_Sword.tple")!));
        Assert.Contains(sword.Rows, r => r.Group == "eCMesh_PS" && r.Name == "MeshFileName" && r.Value == "It_Wpn_1H_Sword.xmsh");
        Assert.Contains(sword.Rows, r => r.Name == "TotalMass" && r.Value == "100");
        foreach (var f in a.AllFiles().Where(f => f.Name.EndsWith(".tple", StringComparison.OrdinalIgnoreCase)))
            TemplateReader.Read(a.ReadAll(f));                  // none may throw
    }

    [Fact]
    public void RealMaterialsParse()
    {
        if (GamePak("materials.pak") is not { } path) return;
        using var a = PakArchive.Open(path);
        int n = 0;
        foreach (var f in a.AllFiles().Where(f => f.Name.EndsWith("._xmat", StringComparison.OrdinalIgnoreCase)))
        {
            var t = MaterialReader.Read(a.ReadAll(f));
            Assert.NotEmpty(t.Rows);
            n++;
        }
        Assert.True(n > 100);
        var bark = MaterialReader.Read(a.ReadAll((PakFile)a.Find("Static_Bark_07_Diffuse_01._xmat")!));
        Assert.Contains(bark.Rows, r => r.Type == "bCImageResourceString" && r.Value == "st_bark_07_diffuse_01.tga");
    }

    static byte[] SineWav(int rate, double seconds, double freq, short amp)
    {
        int n = (int)(rate * seconds);
        using var ms = new MemoryStream();
        var w = new BinaryWriter(ms);
        w.Write("RIFF"u8.ToArray()); w.Write(36 + n * 2); w.Write("WAVEfmt "u8.ToArray());
        w.Write(16); w.Write((short)1); w.Write((short)1); w.Write(rate); w.Write(rate * 2); w.Write((short)2); w.Write((short)16);
        w.Write("data"u8.ToArray()); w.Write(n * 2);
        for (int i = 0; i < n; i++) w.Write((short)(amp * Math.Sin(2 * Math.PI * freq * i / rate)));
        return ms.ToArray();
    }

    [Fact]
    public void WavIsAnalyzedAndSeekable()
    {
        var wav = SineWav(8000, 1.0, 440, 16000);
        var info = AudioReader.Analyze(wav, 50);
        Assert.Equal("wav", info.Format);
        Assert.Equal(8000, info.SampleRate);
        Assert.InRange(info.Duration.TotalSeconds, 0.99, 1.01);
        Assert.InRange(info.Peaks.Max(), 0.45, 0.5);                // 16000 / 32768
        using var s = AudioReader.Open(wav, out _);
        s.Position = TimeSpan.FromSeconds(0.5);
        var buf = new float[100000];
        Assert.InRange(s.Read(buf, 0, buf.Length), 3990, 4010);     // the second half
        Assert.Throws<NotSupportedException>(() => AudioReader.Open(new byte[100], out _));
    }

    [Fact]
    public void RealSoundsDecode()
    {
        var gameDir = Environment.GetEnvironmentVariable("RISEN_DIR") ?? @"B:\SteamLibrary\steamapps\common\Risen";
        var path = Path.Combine(gameDir, "data", "compiled", "sounds.pak");
        if (!File.Exists(path)) return;
        using var a = PakArchive.Open(path);
        var sounds = a.AllFiles().Where(f => f.Name.EndsWith("._xsnd", StringComparison.OrdinalIgnoreCase)).Take(150).ToList();
        Assert.NotEmpty(sounds);
        foreach (var f in sounds)
        {
            var info = AudioReader.Analyze(a.ReadAll(f), 100);
            Assert.Equal("mp3", info.Format);
            Assert.True(info.Duration > TimeSpan.Zero, f.Path);
            Assert.True(info.Peaks.Max() > 0, f.Path + " is silent");
        }
        var open = AudioReader.Analyze(a.ReadAll((PakFile)a.Find("ui/sfx_ui_window_open_02._xsnd")!));
        Assert.InRange(open.Duration.TotalSeconds, 0.05, 10);
    }

    [Fact]
    public void ExportNamesAndApplicability()
    {
        Assert.Equal("Layout_Gamepad.png", AssetExport.TargetName("Layout_Gamepad._ximg", ExportFormat.Png));
        Assert.Equal("a.b.mp3", AssetExport.TargetName("a.b._XSND", ExportFormat.Mp3));
        Assert.True(AssetExport.CanConvert("x._xsnd", ExportFormat.Wav));
        Assert.False(AssetExport.CanConvert("x._ximg", ExportFormat.Mp3));
        Assert.False(AssetExport.CanConvert("x.txt", ExportFormat.Tga));
    }

    [Fact]
    public void TgaAndPngHaveTheRightStructure()
    {
        var bgra = new byte[] { 1, 2, 3, 255, 10, 20, 30, 128 };           // 2x1: pixels B,G,R,A
        var bmp = new Bitmap32(2, 1, bgra, "test");
        var tga = AssetExport.Tga(bmp);
        Assert.Equal(18 + 8, tga.Length);
        Assert.Equal(2, tga[2]); Assert.Equal(32, tga[16]);
        Assert.Equal(bgra, tga[18..]);
        var png = AssetExport.Png(bmp);
        Assert.Equal(new byte[] { 0x89, 0x50, 0x4E, 0x47 }, png[..4]);
        // IHDR: width 2, height 1, 8 bit RGBA; the IDAT inflates to filter byte + R,G,B,A of both pixels
        Assert.Equal(2u, System.Buffers.Binary.BinaryPrimitives.ReadUInt32BigEndian(png.AsSpan(16)));
        Assert.Equal(6, png[25]);
        int idat = png.AsSpan().IndexOf("IDAT"u8);
        int len = (int)System.Buffers.Binary.BinaryPrimitives.ReadUInt32BigEndian(png.AsSpan(idat - 4));
        using var z = new System.IO.Compression.ZLibStream(new MemoryStream(png, idat + 4, len), System.IO.Compression.CompressionMode.Decompress);
        var raw = new byte[9]; z.ReadExactly(raw);
        Assert.Equal(new byte[] { 0, 3, 2, 1, 255, 30, 20, 10, 128 }, raw);
        Assert.Equal(AssetExport.Crc32Of("IEND"u8), System.Buffers.Binary.BinaryPrimitives.ReadUInt32BigEndian(png.AsSpan(png.Length - 4)));
    }

    [Fact]
    public void RealAssetsExport()
    {
        var gameDir = Environment.GetEnvironmentVariable("RISEN_DIR") ?? @"B:\SteamLibrary\steamapps\common\Risen";
        var images = Path.Combine(gameDir, "data", "compiled", "images.pak");
        var sounds = Path.Combine(gameDir, "data", "compiled", "sounds.pak");
        if (!File.Exists(images) || !File.Exists(sounds)) return;
        using var t = new TempDir();
        using (var a = PakArchive.Open(images))
        {
            var f = (PakFile)a.Find("GUI/GUI_SmallNumbers._ximg")!;
            foreach (var fmt in new[] { ExportFormat.Dds, ExportFormat.Tga, ExportFormat.Png })
                Assert.Equal(1, a.ExtractAs([f], t.Path, fmt).Written);
            Assert.True(File.Exists(t.File("GUI/GUI_SmallNumbers.dds")));
            Assert.Equal("DDS ", Encoding.ASCII.GetString(File.ReadAllBytes(t.File("GUI/GUI_SmallNumbers.dds")), 0, 4));
            Assert.Equal(0, a.ExtractAs([f], t.Path, ExportFormat.Mp3).Written);     // an image is not a sound
            Assert.Equal(1, a.ExtractAs([f], t.Path, ExportFormat.Mp3).NotApplicable);
        }
        using var s = PakArchive.Open(sounds);
        var r = s.ExtractAs([s.Find("ui")!], t.Path, ExportFormat.Mp3, "");
        Assert.True(r.Written >= 5 && r.Failed == 0);
        var mp3 = File.ReadAllBytes(Path.Combine(t.Path, "ui", "sfx_ui_window_open_02.mp3"));
        Assert.Equal(0xFF, mp3[0]);
        var w = s.ExtractAs([s.Find("ui")!], t.Path, ExportFormat.Wav, "");
        Assert.Equal(r.Written, w.Written);
    }

    static byte[] FakeGr01(string kind, byte[] payload)
    {
        var b = new byte[0x30 + payload.Length];
        "GR01"u8.CopyTo(b); Encoding.ASCII.GetBytes(kind).CopyTo(b, 4);
        BitConverter.GetBytes(0x28).CopyTo(b, 8);
        BitConverter.GetBytes(0x30).CopyTo(b, 0x10);             // payload offset
        BitConverter.GetBytes(payload.Length).CopyTo(b, 0x14);   // payload size
        payload.CopyTo(b, 0x30);
        return b;
    }

    [Fact]
    public void ExtractAsOffersOnlyWhatTheFilesAre()
    {
        var a = PakArchive.CreateNew();
        var mp3 = FakeGr01("SN04", [0xFF, 0xFB, 0x90, 0x00, 1, 2, 3]);
        var dds = FakeGr01("IM04", Encoding.ASCII.GetBytes("DDS ").Concat(new byte[200]).ToArray());
        var snd = (PakFile)a.AddFile(a.Root, "a._xsnd", new MemoryContent(mp3));
        var img = (PakFile)a.AddFile(a.Root, "b._ximg", new MemoryContent(dds));
        var txt = (PakFile)a.AddFile(a.Root, "c.txt", new MemoryContent(Text("hi")));
        var weird = (PakFile)a.AddFile(a.Root, "d._xsnd", new MemoryContent(FakeGr01("SN04", [0x12, 0x34, 0x56, 0x78])));   // unknown audio
        string path = Path.Combine(Path.GetTempPath(), "pv-as-" + Guid.NewGuid().ToString("N") + ".pak");
        try
        {
            a.Save(path);
            using var r = PakArchive.Open(path);
            PakNode N(string n) => r.Find(n)!;
            Assert.Equal([ExportFormat.Mp3], r.FormatsFor([N("a._xsnd")]));                                   // one format: a plain menu item
            Assert.Equal([ExportFormat.Dds, ExportFormat.Tga, ExportFormat.Png], r.FormatsFor([N("b._ximg")]));
            Assert.Empty(r.FormatsFor([N("c.txt")]));
            Assert.Empty(r.FormatsFor([N("d._xsnd")]));                                                       // audio the tool cannot name
            Assert.Equal([ExportFormat.Mp3, ExportFormat.Dds, ExportFormat.Tga, ExportFormat.Png],            // a mix: the union
                r.FormatsFor([N("a._xsnd"), N("b._ximg"), N("c.txt")]));
            Assert.Equal([ExportFormat.Mp3, ExportFormat.Dds, ExportFormat.Tga, ExportFormat.Png], r.FormatsFor([r.Root]));   // a folder is walked
        }
        finally { File.Delete(path); }
    }

    [Fact]
    public void RealSoundsOfferOnlyMp3()
    {
        var gameDir = Environment.GetEnvironmentVariable("RISEN_DIR") ?? @"B:\SteamLibrary\steamapps\common\Risen";
        var path = Path.Combine(gameDir, "data", "compiled", "sounds.pak");
        if (!File.Exists(path)) return;
        using var a = PakArchive.Open(path);
        Assert.Equal([ExportFormat.Mp3], a.FormatsFor([a.Find("ui")!]));
    }
}
