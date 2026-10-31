using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Input.Platform;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Platform.Storage;

namespace PakView.Services;

/// <summary><see cref="IShell"/> on top of a window. Avalonia ships no message box, so the small dialogs are built in code.</summary>
public sealed class WindowShell(Window owner) : IShell
{
    static readonly FilePickerFileType ArchiveTypes = new("Risen archives")
    {
        Patterns = ["*.pak", "*.p00", "*.p01", "*.p02", "*.p03", "*.p04", "*.p05", "*.p06", "*.p07", "*.p08", "*.p09"],
    };

    public async Task<int> Ask(string title, string message, params string[] buttons)
    {
        if (buttons.Length == 0) buttons = ["OK"];
        int result = -1;
        var dialog = NewDialog(title);
        var row = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right, Spacing = 8 };
        for (int i = 0; i < buttons.Length; i++)
        {
            int index = i;
            var button = new Button { Content = buttons[i], MinWidth = 80, IsDefault = i == 0, HorizontalContentAlignment = HorizontalAlignment.Center };
            button.Click += (_, _) => { result = index; dialog.Close(); };
            row.Children.Add(button);
        }
        dialog.Content = new StackPanel
        {
            Margin = new Thickness(18), Spacing = 16,
            Children = { new TextBlock { Text = message, TextWrapping = TextWrapping.Wrap, MaxWidth = 560 }, row },
        };
        await dialog.ShowDialog(owner);
        return result;
    }

    public async Task<bool> Confirm(string title, string message, string yes = "OK", string no = "Cancel") =>
        await Ask(title, message, yes, no) == 0;

    public Task Info(string title, string message) => Ask(title, message, "OK");

    public async Task<string?> Input(string title, string prompt, string initial = "")
    {
        string? result = null;
        var dialog = NewDialog(title);
        var box = new TextBox { Text = initial, MinWidth = 380 };
        var ok = new Button { Content = "OK", MinWidth = 80, IsDefault = true };
        var cancel = new Button { Content = "Cancel", MinWidth = 80, IsCancel = true };
        ok.Click += (_, _) => { result = box.Text; dialog.Close(); };
        cancel.Click += (_, _) => dialog.Close();
        dialog.Content = new StackPanel
        {
            Margin = new Thickness(18), Spacing = 12,
            Children =
            {
                new TextBlock { Text = prompt },
                box,
                new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right, Spacing = 8, Children = { ok, cancel } },
            },
        };
        dialog.Opened += (_, _) =>
        {
            box.Focus();
            int dot = initial.LastIndexOf('.');   // select the name, not the extension
            box.SelectionStart = 0;
            box.SelectionEnd = dot > 0 ? dot : initial.Length;
        };
        await dialog.ShowDialog(owner);
        return result;
    }

    public async Task<string?> PickArchiveToOpen()
    {
        var files = await owner.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "Open Risen archive", AllowMultiple = false, FileTypeFilter = [ArchiveTypes, FilePickerFileTypes.All],
        });
        return files.Count > 0 ? files[0].TryGetLocalPath() : null;
    }

    public async Task<string?> PickArchiveToSave(string suggestedName)
    {
        var file = await owner.StorageProvider.SaveFilePickerAsync(new FilePickerSaveOptions
        {
            Title = "Save archive as", SuggestedFileName = suggestedName, DefaultExtension = "pak",
            FileTypeChoices = [ArchiveTypes, FilePickerFileTypes.All], ShowOverwritePrompt = true,
        });
        return file?.TryGetLocalPath();
    }

    public async Task<IReadOnlyList<string>> PickFiles(string title, bool multiple)
    {
        var files = await owner.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions { Title = title, AllowMultiple = multiple });
        return files.Select(f => f.TryGetLocalPath()).OfType<string>().ToList();
    }

    public async Task<IReadOnlyList<string>> PickFolders(string title, bool multiple)
    {
        var folders = await owner.StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions { Title = title, AllowMultiple = multiple });
        return folders.Select(f => f.TryGetLocalPath()).OfType<string>().ToList();
    }

    public async Task CopyToClipboard(string text)
    {
        if (owner.Clipboard is { } clipboard) await clipboard.SetTextAsync(text);
    }

    public void Close() => owner.Close();

    static Window NewDialog(string title)
    {
        var dialog = new Window
        {
            Title = title, SizeToContent = SizeToContent.WidthAndHeight, CanResize = false,
            WindowStartupLocation = WindowStartupLocation.CenterOwner, ShowInTaskbar = false,
        };
        dialog.KeyDown += (_, e) => { if (e.Key == Key.Escape) dialog.Close(); };
        return dialog;
    }
}
