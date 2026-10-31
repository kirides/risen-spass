namespace PakView.Services;

/// <summary>
/// Everything the view models need from the window: message boxes, file pickers, the clipboard, closing.
/// The window implements it (<see cref="WindowShell"/>), so view models never touch a control.
/// </summary>
public interface IShell
{
    Task Info(string title, string message);
    Task<bool> Confirm(string title, string message, string yes = "OK", string no = "Cancel");

    /// <summary>Message with custom buttons; returns the index of the pressed one (-1 when dismissed).</summary>
    Task<int> Ask(string title, string message, params string[] buttons);

    Task<string?> Input(string title, string prompt, string initial = "");

    Task<string?> PickArchiveToOpen();
    Task<string?> PickArchiveToSave(string suggestedName);
    Task<IReadOnlyList<string>> PickFiles(string title, bool multiple);
    Task<IReadOnlyList<string>> PickFolders(string title, bool multiple);

    Task CopyToClipboard(string text);
    void Close();
}
