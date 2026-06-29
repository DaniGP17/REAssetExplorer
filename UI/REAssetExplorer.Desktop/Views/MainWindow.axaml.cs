using Avalonia;
using Avalonia.Controls;
using Avalonia.Input.Platform;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;
using REAssetExplorer.Desktop.ViewModels;

namespace REAssetExplorer.Desktop.Views;

public partial class MainWindow : Window, IShellServices
{
    private readonly Dictionary<AssetEditorViewModel, AssetEditorWindow> editorWindows = [];
    private bool closeRequested;

    public MainWindow()
    {
        InitializeComponent();
    }

    public IReadOnlyList<string> StartupArgs { get; init; } = [];

    private MainViewModel? ViewModel => DataContext as MainViewModel;

    protected override void OnOpened(EventArgs e)
    {
        base.OnOpened(e);
        if (ViewModel is not { } vm) return;
        vm.Shell = this;
        vm.Start(StartupArgs);
    }

    protected override void OnClosing(WindowClosingEventArgs e)
    {
        base.OnClosing(e);
        if (ViewModel is not { } vm) return;
        if (!closeRequested && !vm.WhenIdle().IsCompleted)
        {
            // A load in flight still uses the viewport; close once it finishes.
            e.Cancel = true;
            closeRequested = true;
            vm.WhenIdle().ContinueWith(_ => Close(), TaskScheduler.FromCurrentSynchronizationContext());
            return;
        }
        _ = vm.CloseEditorsAsync();
        vm.CloseLayout();
    }

    public void ActivateMainWindow()
    {
        if (WindowState == WindowState.Minimized) WindowState = WindowState.Normal;
        Activate();
    }

    public void ShowAssetEditor(AssetEditorViewModel editor)
    {
        if (editorWindows.TryGetValue(editor, out AssetEditorWindow? open))
        {
            if (open.WindowState == WindowState.Minimized) open.WindowState = WindowState.Normal;
            open.Activate();
            return;
        }
        var window = new AssetEditorWindow { DataContext = editor };
        window.Closed += (_, _) => editorWindows.Remove(editor);
        editorWindows.Add(editor, window);
        window.Show();
    }

    public async Task<string?> PickSaveFileAsync(string suggestedName)
    {
        IStorageFile? file = await StorageProvider.SaveFilePickerAsync(new FilePickerSaveOptions
        {
            Title = "Extract file",
            SuggestedFileName = suggestedName,
            ShowOverwritePrompt = true
        });
        return file?.TryGetLocalPath();
    }

    public async Task<string?> PickFolderAsync(string title, string? start)
    {
        IStorageFolder? startFolder = !string.IsNullOrEmpty(start) && Directory.Exists(start)
            ? await StorageProvider.TryGetFolderFromPathAsync(start)
            : null;
        IReadOnlyList<IStorageFolder> folders = await StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions
        {
            Title = title,
            AllowMultiple = false,
            SuggestedStartLocation = startFolder
        });
        return folders.Count > 0 ? folders[0].TryGetLocalPath() : null;
    }

    public async Task CopyTextAsync(string text)
    {
        if (Clipboard != null) await Clipboard.SetTextAsync(text);
    }

    private void OnExitClick(object? sender, RoutedEventArgs e) => Close();

    private void OnMinimizeClick(object? sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;

    private void OnMaximizeClick(object? sender, RoutedEventArgs e) =>
        WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;

    private void OnCloseClick(object? sender, RoutedEventArgs e) => Close();

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property != WindowStateProperty || MaximizeIcon == null) return;
        bool maximized = WindowState == WindowState.Maximized;
        MaximizeIcon.IsVisible = !maximized;
        RestoreIcon.IsVisible = maximized;
    }
}
