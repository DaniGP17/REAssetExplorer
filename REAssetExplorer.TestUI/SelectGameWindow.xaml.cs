using System;
using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Media;
using Microsoft.Win32;
using REAssetExplorer.Core.Games;
using REAssetExplorer.Core.Steam;

namespace REAssetExplorer.TestUI;

public class GameViewModel
{
    public IGameProvider Provider { get; }
    public string        Name     => Provider.Name;
    public string        ShortId  => Provider.Id.ToUpperInvariant()[..Math.Min(Provider.Id.Length, 4)];
    public string        Subtitle => $"Steam App ID: {Provider.SteamAppId}";

    public GameViewModel(IGameProvider provider) => Provider = provider;
}

public partial class SelectGameWindow : Window
{
    private GameViewModel? _selectedViewModel;

    public SelectGameWindow()
    {
        InitializeComponent();

        var vms = new List<GameViewModel>();
        foreach (var p in GameProviderRegistry.Providers)
            vms.Add(new GameViewModel(p));

        GameList.ItemsSource = vms;
    }

    // ── Window chrome ────────────────────────────────────────────────────

    private void OnMinimize(object sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;
    private void OnClose(object sender, RoutedEventArgs e)    => Close();

    // ── Game selection ───────────────────────────────────────────────────

    private void OnGameChecked(object sender, RoutedEventArgs e)
    {
        if (sender is System.Windows.Controls.RadioButton { DataContext: GameViewModel vm })
        {
            _selectedViewModel = vm;
            TryAutoFillSteamPath(vm.Provider);
        }
        UpdateContinueState();
    }

    private void TryAutoFillSteamPath(IGameProvider provider)
    {
        var detected = SteamLocator.FindGame(provider.SteamAppId);
        if (!string.IsNullOrEmpty(detected))
        {
            _pathIsPlaceholder    = false;
            GamePathBox.Text      = detected;
            GamePathBox.Foreground = new SolidColorBrush(Color.FromRgb(0xa0, 0xa0, 0xa0));
        }
    }

    // ── Path input ───────────────────────────────────────────────────────

    private bool _pathIsPlaceholder = true;

    private void OnPathBoxGotFocus(object sender, RoutedEventArgs e)
    {
        if (_pathIsPlaceholder)
        {
            GamePathBox.Text      = string.Empty;
            GamePathBox.Foreground = new SolidColorBrush(Color.FromRgb(0x60, 0x60, 0x60));
        }
    }

    private void OnPathChanged(object sender, System.Windows.Controls.TextChangedEventArgs e)
    {
        _pathIsPlaceholder = string.IsNullOrWhiteSpace(GamePathBox.Text);
        GamePathBox.Foreground = _pathIsPlaceholder
            ? new SolidColorBrush(Color.FromRgb(0x44, 0x44, 0x44))
            : new SolidColorBrush(Color.FromRgb(0xa0, 0xa0, 0xa0));
        UpdateContinueState();
    }

    private void OnBrowse(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFolderDialog
        {
            Title            = "Select game installation folder",
            InitialDirectory = SteamLibraryParser.GetSteamPathFromRegistry() ?? string.Empty,
        };

        if (dialog.ShowDialog() == true && dialog.FolderNames.Length > 0)
        {
            _pathIsPlaceholder    = false;
            GamePathBox.Text      = dialog.FolderNames[0];
            GamePathBox.Foreground = new SolidColorBrush(Color.FromRgb(0xa0, 0xa0, 0xa0));
        }
    }

    // ── Footer buttons ───────────────────────────────────────────────────

    private void OnHelp(object sender, RoutedEventArgs e) { }

    private async void OnContinue(object sender, RoutedEventArgs e)
    {
        if (_selectedViewModel == null) return;

        var path = GamePathBox.Text.Trim();

        if (string.IsNullOrWhiteSpace(path))
        {
            new StatusWindow(StatusType.Error, "Please specify the game installation folder.").Show();
            return;
        }

        if (!Directory.Exists(path))
        {
            new StatusWindow(StatusType.Error, "The selected directory does not exist.").Show();
            return;
        }

        var provider = _selectedViewModel.Provider;
        provider.GameDirectory = path;
        App.CurrentProvider    = provider;

        ContinueButton.IsEnabled = false;

        var status = new StatusWindow(StatusType.Loading, "Preparing to load...");
        status.Show();

        var success = await GameLoader.LoadGameAsync(provider, msg =>
            Dispatcher.Invoke(() => status.UpdateMessage(msg)));

        status.Close();

        if (!success)
        {
            ContinueButton.IsEnabled = true;
            new StatusWindow(StatusType.Error, "Failed to load game data. Check that the game directory is correct.").Show();
            return;
        }

        new MainWindow().Show();
        Close();
    }

    // ── Helpers ──────────────────────────────────────────────────────────

    private void UpdateContinueState()
        => ContinueButton.IsEnabled = _selectedViewModel != null;
}
