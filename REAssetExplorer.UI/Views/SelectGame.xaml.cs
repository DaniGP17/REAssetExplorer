using REAssetExplorer.Core.Games;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.Windows;
using System.Windows.Controls;
using Microsoft.Win32;
using REAssetExplorer.App.Views;
using REAssetExplorer.Core.Steam;
using REAssetExplorer.UI.Enums;
using Wpf.Ui.Controls;

namespace REAssetExplorer.UI;

/// <summary>
/// Interaction logic for SelectGame.xaml
/// </summary>
public partial class SelectGame : FluentWindow, INotifyPropertyChanged
{
    public ObservableCollection<IGameProvider> Games { get; }
    
    public string AppVersion => FileVersionInfo
        .GetVersionInfo(System.Reflection.Assembly.GetExecutingAssembly().Location)
        .FileVersion ?? "Unknown";

    private IGameProvider? _selectedGame;

    public event PropertyChangedEventHandler? PropertyChanged;

    public SelectGame()
    {
        InitializeComponent();
        Games = new ObservableCollection<IGameProvider>(GameProviderRegistry.Providers);
        DataContext = this;
    }

    private void GameRadioButton_Checked(object sender, RoutedEventArgs e)
    {
        if (sender is not RadioButton { Tag: IGameProvider game })
        {
            return;
        }

        _selectedGame = game;
        UpdateGamePath();
    }

    private void OnUpdatePathClick(object sender, RoutedEventArgs e)
    {
        UpdateGamePath();
    }

    private void OnSelectFolderClick(object sender, RoutedEventArgs e)
    {
        var folderPath = ShowFolderDialog();
        
        if (!string.IsNullOrEmpty(folderPath))
        {
            GamePath.Text = folderPath;
        }
    }

    private async void OnContinueClick(object sender, RoutedEventArgs e)
    {
        if (!ValidateSelection(out var error))
        {
            ShowValidationError(error!);
            return;
        }

        await GameManager.LoadGame(_selectedGame!, GamePath.Text);
        Close();
    }

    private void UpdateGamePath()
    {
        if (_selectedGame != null)
        {
            GamePath.Text = SteamLocator.FindGame(_selectedGame.SteamAppId) ?? string.Empty;
        }
    }

    private string? ShowFolderDialog()
    {
        var dialog = new OpenFolderDialog
        {
            Multiselect = false,
            InitialDirectory = SteamLibraryParser.GetSteamPathFromRegistry()
        };

        return dialog.ShowDialog() == true && dialog.FolderNames.Length > 0 
            ? dialog.FolderNames[0] 
            : null;
    }

    private bool ValidateSelection(out string? error)
    {
        if (_selectedGame == null)
        {
            error = "You haven't selected a game.";
            return false;
        }

        if (string.IsNullOrWhiteSpace(GamePath.Text))
        {
            error = "You haven't specified a game path.";
            return false;
        }

        error = null;
        return true;
    }

    private static void ShowValidationError(string message)
    {
        var errorWindow = new StatusWindow(StatusType.Error, message);
        errorWindow.Show();
    }

    private void OnPropertyChanged(string propertyName)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }
}