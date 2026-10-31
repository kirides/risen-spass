using System.Text;
using PakView.Models;
using Risen.Pak;
using Risen.Pak.Formats;

namespace PakView.ViewModels;

/// <summary>Raw material for the views that must be made on the UI thread.</summary>
public sealed record ImageData(Bitmap32? Pixels, byte[]? Encoded);
public sealed record AudioData(byte[] File, AudioInfo Info);

/// <summary>Decides what the preview shows for a file and decodes it (runs on a worker thread, no UI types needed).</summary>
public static class PreviewBuilder
{
    public const int HexLimit = 64 * 1024;
    const int TextLimit = 512 * 1024;
    const int ImageLimit = 96 * 1024 * 1024;
    const int TableLimit = 24 * 1024 * 1024;

    /// <summary>One of <see cref="MessageContent"/>, <see cref="TextContent"/>, <see cref="TableContent"/>, <see cref="PropsContent"/>, <see cref="ImageData"/>, <see cref="AudioData"/>.</summary>
    public static object Build(PakArchive archive, PakFile file, byte[] head, CancellationToken ct)
    {
        var name = file.Name.ToLowerInvariant();
        var ext = Path.GetExtension(name);

        if (file.Size == 0) return new MessageContent("(empty file)");

        if (AudioReader.IsAudioName(name) && file.Size <= ImageLimit)
        {
            var all = archive.ReadAll(file);
            try { return new AudioData(all, AudioReader.Analyze(all, 600, ct)); }
            catch (NotSupportedException ex) { return new MessageContent(ex.Message + " See the Hex tab."); }
            catch (InvalidDataException ex) { return new MessageContent("Cannot decode this sound: " + ex.Message); }
        }

        if (name.EndsWith("._ximg") || ext == ".dds")
        {
            if (file.Size > ImageLimit) return new MessageContent("The image is too large to preview.");
            var all = archive.ReadAll(file);
            int offset = Dds.FindPayload(all);
            if (offset < 0) return new MessageContent("No DDS data found in this image file.");
            ct.ThrowIfCancellationRequested();
            return new ImageData(Dds.Decode(all, offset), null);
        }
        if (ext is ".png" or ".jpg" or ".jpeg" or ".bmp" or ".gif" or ".webp")
        {
            if (file.Size > ImageLimit) return new MessageContent("The image is too large to preview.");
            return new ImageData(null, archive.ReadAll(file));
        }

        if (TabReader.IsTab(head))
        {
            if (file.Size > TableLimit) return new MessageContent("The table is too large to preview.");
            return new TableContent(TabReader.Read(archive.ReadAll(file)));
        }

        // templates and materials: the property list
        if (file.Size <= TableLimit && (TemplateReader.IsTemplate(head) || MaterialReader.IsMaterial(head)))
        {
            var all = archive.ReadAll(file);
            try
            {
                var table = TemplateReader.IsTemplate(all) ? TemplateReader.Read(all) : MaterialReader.Read(all);
                if (table.Rows.Count > 0) return new PropsContent(table);
            }
            catch (InvalidDataException) { /* fall through to the summary below */ }
        }

        // other engine containers: summaries instead of binary noise
        if (head.Length >= 8 && head.AsSpan(0, 8).SequenceEqual("GENOMFLE"u8))
        {
            var all = file.Size <= TableLimit ? archive.ReadAll(file) : head;
            if (EngineFiles.DescribeGenome(all) is { } genome) return new TextContent(genome);
        }
        if (head.Length >= 4 && head.AsSpan(0, 4).SequenceEqual("GR01"u8))
            return new TextContent(EngineFiles.DescribeResource(head) ?? "");

        if (HexDump.LooksLikeText(head))
        {
            var chunk = file.Size > TextLimit ? archive.ReadHead(file, TextLimit) : archive.ReadAll(file);
            var text = HexDump.DecodeText(chunk);
            return new TextContent(file.Size > TextLimit ? text + $"\n\n... (preview limited to the first {Describe.Bytes(TextLimit)})" : text);
        }
        return new MessageContent("Binary data - see the Hex tab.");
    }

    public static string HexText(PakFile file, byte[] head) =>
        HexDump.Format(head) + (file.Size > head.Length ? $"\n... {Describe.Bytes(file.Size - head.Length)} more" : "");

    public static string InfoText(PakFile f)
    {
        var sb = new StringBuilder();
        sb.AppendLine($"Name        {f.Name}");
        sb.AppendLine($"Path        {f.Path}");
        sb.AppendLine($"Type        {Describe.TypeOf(f.Name)}");
        sb.AppendLine($"Size        {f.Size:N0} bytes ({Describe.Bytes(f.Size)})");
        if (f.NewContent is null)
        {
            sb.AppendLine($"Packed      {f.StoredSize:N0} bytes ({(f.Size == 0 ? 0 : 100.0 * f.StoredSize / f.Size):0.#} %)");
            sb.AppendLine($"Method      {(f.IsCompressed ? "zlib (deflate)" : "stored")}");
            sb.AppendLine($"Offset      0x{f.Offset:x}");
            sb.AppendLine($"Member flag 0x{f.MemberFlags:x}");
        }
        else
        {
            sb.AppendLine("Packed      (decided when the archive is saved)");
            sb.AppendLine($"Source      {(f.NewContent is DiskContent d ? d.FilePath : "memory")}");
        }
        sb.AppendLine($"Modified    {f.WrittenTime:g}");
        sb.AppendLine($"Attributes  0x{f.Attributes:x}");
        sb.AppendLine($"Status      {(f.State == NodeState.Original ? "unchanged" : f.State.ToString().ToLowerInvariant())}");
        return sb.ToString();
    }
}
