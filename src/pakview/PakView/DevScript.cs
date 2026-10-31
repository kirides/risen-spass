using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media.Imaging;
using Avalonia.VisualTree;
using PakView.ViewModels;
using Risen.Pak.Formats;

namespace PakView;

/// <summary>
/// Developer switches (see <see cref="StartupOptions"/>): open an archive, navigate / select / sort / extract with scripted
/// steps, log what the window shows, render the window to a PNG. Key and mouse steps raise real routed events, so the
/// window's input handling is what gets tested. Used to check the UI (also in the AOT build) without a screen grab.
/// </summary>
public sealed class DevScript(Window window, MainViewModel vm, DataGrid fileGrid)
{
    public async Task Run(StartupOptions o)
    {
        await vm.OpenArchive(o.OpenPath!);
        if (o.Dir is not null) vm.NavigateTo(o.Dir);
        if (o.Filter is not null) { vm.FilterText = o.Filter; await Task.Delay(300); }
        if (o.Select is not null) vm.SelectPaths([o.Select]);
        if (o.Nav is not null) await RunSteps(o.Nav, o.NavLog);
        if (o.ShotPath is null) return;

        await Task.Delay(o.ShotDelayMs);
        using var bitmap = new RenderTargetBitmap(new PixelSize((int)window.Bounds.Width, (int)window.Bounds.Height));
        bitmap.Render(window);
        bitmap.Save(o.ShotPath, new PngBitmapEncoderOptions());
        window.Close();
    }

    /// <summary>Steps separated by ';': dir:a/b  select:a|b  dbl:name  sort:Column  extas:png:destdir  key:Enter  alt+up|left|right  x1  x2.</summary>
    async Task RunSteps(string script, string? logPath)
    {
        var log = new List<string>();
        foreach (var raw in script.Split(';', StringSplitOptions.RemoveEmptyEntries))
        {
            var step = raw.Trim();
            if (step.StartsWith("dir:")) vm.NavigateTo(step[4..]);
            else if (step.StartsWith("select:")) vm.SelectPaths(step[7..].Split('|'));
            else if (step.StartsWith("dbl:")) { vm.SelectPaths([step[4..]]); await Task.Delay(200); vm.OpenSelectedCommand.Execute(null); }
            else if (step.StartsWith("sort:")) vm.SortBy(step[5..]);
            else if (step.StartsWith("extas:"))
            {
                var parts = step.Split(':', 3);   // extas:png:<destination folder>
                await vm.ExtractAsTo(Enum.Parse<ExportFormat>(parts[1], true), parts[2]);
                log.Add("status: " + vm.Status);
            }
            else if (step == "probe:menu")
            {
                // opens Edit > "Extract selected as" and reads what the items made from the view model contain
                var edit = window.GetVisualDescendants().OfType<MenuItem>().First(m => m.Header as string == "_Edit");
                edit.IsSubMenuOpen = true;
                await Task.Delay(300);
                var submenu = edit.Items.OfType<MenuItem>().First(m => m.ItemsSource is not null);
                submenu.IsSubMenuOpen = true;
                await Task.Delay(300);
                var items = submenu.GetRealizedContainers().OfType<MenuItem>().Select(m => $"{m.Header} (command={m.Command is not null}, enabled={m.IsEffectivelyEnabled}, parameter={m.CommandParameter})");
                log.Add($"  (menu '{submenu.Header}' visible={submenu.IsVisible}: {string.Join("; ", items)})");
                submenu.IsSubMenuOpen = false;
                edit.IsSubMenuOpen = false;
            }
            else if (step.StartsWith("key:"))
            {
                // a KeyDown on the grid itself: do its key bindings run, or does the DataGrid swallow the key first?
                var e = new KeyEventArgs { RoutedEvent = InputElement.KeyDownEvent, Key = Enum.Parse<Key>(step[4..], true), Source = fileGrid };
                fileGrid.RaiseEvent(e);
                log.Add($"  (key {e.Key}: handled={e.Handled})");
            }
            else if (step.StartsWith("alt+"))
            {
                var key = step[4..] switch { "up" => Key.Up, "left" => Key.Left, _ => Key.Right };
                window.RaiseEvent(new KeyEventArgs { RoutedEvent = InputElement.KeyDownEvent, Key = key, KeyModifiers = KeyModifiers.Alt });
            }
            else if (step is "x1" or "x2")
            {
                var kind = step == "x1" ? PointerUpdateKind.XButton1Pressed : PointerUpdateKind.XButton2Pressed;
                var properties = new PointerPointProperties(RawInputModifiers.None, kind);
                var pointer = new Pointer(Pointer.GetNextFreeId(), PointerType.Mouse, true);
                window.RaiseEvent(new PointerPressedEventArgs(window, pointer, window, new Point(300, 300), 0, properties, KeyModifiers.None) { RoutedEvent = InputElement.PointerPressedEvent });
            }
            await Task.Delay(250);
            log.Add(Describe(step));
        }
        if (logPath is not null) File.WriteAllLines(logPath, log);
    }

    string Describe(string step)
    {
        var (back, forward) = vm.History;
        var extractAs = vm.ExtractAsOptions is { } options ? $"'{vm.ExtractAsHeader}' [{string.Join(", ", options.Select(x => x.Label))}]"
                      : vm.SingleExtractFormat is not null ? $"'{vm.ExtractAsHeader}'" : "hidden";
        return $"{step} -> '{vm.CurrentPath}' back={back} fwd={forward} filter='{vm.FilterText}' sel={vm.Selection.FirstOrDefault()?.Name} " +
               $"extractAs={extractAs} playing={vm.Preview.IsPlaying} first={vm.List.Items.FirstOrDefault()?.Name} " +
               $"shown={(fileGrid.ItemsSource as IEnumerable<Models.EntryRow>)?.FirstOrDefault()?.Name}";
    }
}
