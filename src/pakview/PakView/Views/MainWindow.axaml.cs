using System.Windows.Input;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Platform.Storage;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.VisualTree;
using PakView.Models;
using PakView.Services;
using PakView.ViewModels;

namespace PakView.Views;

/// <summary>
/// The window. The behaviour lives in <see cref="MainViewModel"/>; what is left here is what only a control can do:
/// mirroring the grid's selection, keys the grid would swallow, mouse side buttons, drag and drop, the closing question.
/// </summary>
public partial class MainWindow : Window
{
    readonly MainViewModel _vm;
    bool _mirroringSelection;
    bool _closeConfirmed;

    public MainWindow() : this(new StartupOptions()) { }

    public MainWindow(StartupOptions options)
    {
        InitializeComponent();
        _vm = new MainViewModel(new WindowShell(this));
        DataContext = _vm;

        GridSorting.Attach(FileGrid, _vm.List, _vm.SortBy);

        // selection: the grid tells the model, the model can ask the grid to select
        FileGrid.SelectionChanged += (_, _) =>
        {
            if (!_mirroringSelection) _vm.SetSelection(FileGrid.SelectedItems.OfType<EntryRow>().ToList());
        };
        _vm.SelectionRequested += SelectInGrid;

        // the "Folder" column only exists in search results
        _vm.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName != nameof(MainViewModel.ShowFolderColumn)) return;
            var folder = FileGrid.Columns[^1];
            folder.IsVisible = _vm.ShowFolderColumn;
            if (_vm.ShowFolderColumn) folder.DisplayIndex = 2;
        };

        FileGrid.DoubleTapped += (_, e) =>
        {
            if (e.Source is Visual v && v.FindAncestorOfType<DataGridRow>() is not null) _vm.OpenSelectedCommand.Execute(null);
        };

        // Alt+arrows and the mouse side buttons are tunnelled: the grid and the tree would handle the arrows first
        AddHandler(KeyDownEvent, OnNavigationKey, RoutingStrategies.Tunnel);
        AddHandler(PointerPressedEvent, OnNavigationButton, RoutingStrategies.Tunnel, handledEventsToo: true);

        AddHandler(DragDrop.DragOverEvent, (_, e) => e.DragEffects = e.DataTransfer.Contains(DataFormat.File) ? DragDropEffects.Copy : DragDropEffects.None);
        AddHandler(DragDrop.DropEvent, (_, e) =>
        {
            var paths = (e.DataTransfer.TryGetFiles() ?? []).Select(f => f.TryGetLocalPath()).OfType<string>().ToList();
            _vm.DropPathsCommand.Execute(paths);
        });

        if (options.OpenPath is not null)
            Opened += async (_, _) => await new DevScript(this, _vm, FileGrid).Run(options);   // --shot / --nav, see StartupOptions
    }

    void SelectInGrid(IReadOnlyList<EntryRow> rows)
    {
        _mirroringSelection = true;
        try
        {
            if (rows.Count > 1 && rows.Count == _vm.List.Items.Count) FileGrid.SelectAll();
            else
            {
                FileGrid.SelectedItems.Clear();
                foreach (var row in rows) FileGrid.SelectedItems.Add(row);
            }
        }
        finally { _mirroringSelection = false; }
        if (rows.Count > 0) FileGrid.ScrollIntoView(rows[0], null);
    }

    /// <summary>
    /// Tunnelled, so it runs before the grid or the tree: Alt+arrows navigate, and the file list's own keys are mapped to commands.
    /// (A KeyBinding on the grid loses against the DataGrid's handling: it takes Enter to move the selection down.)
    /// </summary>
    void OnNavigationKey(object? sender, KeyEventArgs e)
    {
        ICommand? command = null;
        if (e.KeyModifiers == KeyModifiers.Alt)
            command = e.Key switch { Key.Up => _vm.UpCommand, Key.Left => _vm.BackCommand, Key.Right => _vm.ForwardCommand, _ => null };
        else if (e.KeyModifiers == KeyModifiers.None && InFileGrid(e.Source))
            command = e.Key switch
            {
                Key.Enter => _vm.OpenSelectedCommand, Key.Delete => _vm.DeleteCommand, Key.F2 => _vm.RenameCommand,
                Key.Insert => _vm.AddFilesCommand, Key.Back => _vm.UpCommand, _ => null,
            };
        if (command is null) return;
        if (command.CanExecute(null)) command.Execute(null);
        e.Handled = true;
    }

    bool InFileGrid(object? source) => source is Visual v && (ReferenceEquals(v, FileGrid) || v.FindAncestorOfType<DataGrid>() == FileGrid);

    void OnNavigationButton(object? sender, PointerPressedEventArgs e)
    {
        var kind = e.GetCurrentPoint(this).Properties.PointerUpdateKind;
        if (kind == PointerUpdateKind.XButton1Pressed) { _vm.BackCommand.Execute(null); e.Handled = true; }
        else if (kind == PointerUpdateKind.XButton2Pressed) { _vm.ForwardCommand.Execute(null); e.Handled = true; }
    }

    protected override void OnKeyDown(KeyEventArgs e)
    {
        base.OnKeyDown(e);
        if (e.Key == Key.F && e.KeyModifiers.HasFlag(KeyModifiers.Control))
        {
            FilterBox.Focus();
            e.Handled = true;
        }
    }

    protected override void OnClosing(WindowClosingEventArgs e)
    {
        base.OnClosing(e);
        if (_closeConfirmed) return;
        e.Cancel = true;   // decided after the question; the answer arrives asynchronously
        _ = AskThenClose();
    }

    async Task AskThenClose()
    {
        if (!await _vm.CanCloseAsync()) return;
        _closeConfirmed = true;
        Close();
    }

    protected override void OnClosed(EventArgs e)
    {
        base.OnClosed(e);
        _vm.Dispose();
    }
}
