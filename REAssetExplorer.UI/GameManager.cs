using REAssetExplorer.Core.Games;
using Microsoft.Extensions.DependencyInjection;
using REAssetExplorer.App.Views;
using REAssetExplorer.Core.Pak;
using REAssetExplorer.UI.Enums;
using REAssetExplorer.UI.Helpers;
using REAssetExplorer.UI.Services;

namespace REAssetExplorer.UI;

/// <summary>
/// Manages game loading workflow and UI coordination.
/// </summary>
public static class GameManager
{
    private static StatusWindow? _statusWindow;
    private static Dictionary<string, PakFile> _loadedPakFiles = new();
    private static IGameProvider? _currentGameProvider;
    
    /// <summary>
    /// Gets the currently loaded game provider.
    /// </summary>
    public static IGameProvider? CurrentGameProvider => _currentGameProvider;
    
    /// <summary>
    /// Gets the loaded PAK files.
    /// </summary>
    public static IReadOnlyDictionary<string, PakFile> LoadedPakFiles => _loadedPakFiles;

    /// <summary>
    /// Loads a game and displays its file explorer.
    /// </summary>
    /// <param name="gameProvider">The game provider.</param>
    /// <param name="gameDirectory">The game installation directory.</param>
    public static async Task LoadGame(IGameProvider gameProvider, string gameDirectory)
    {
        ArgumentNullException.ThrowIfNull(gameProvider);
        ArgumentException.ThrowIfNullOrWhiteSpace(gameDirectory);

        gameProvider.GameDirectory = gameDirectory;
        _currentGameProvider = gameProvider;

        var loadingService = App.Services.GetRequiredService<GameLoadingService>();

        // Ensure file list is available
        var success = await ExecuteWithStatus(
            StatusType.Loading,
            "Loading game file list...",
            () => loadingService.EnsureFileListAsync(gameProvider.Id, UpdateStatus)
        );

        if (!success)
        {
            ShowError("Failed to load game file list.");
            return;
        }

        // Load PAK files
        var pakFiles = await ExecuteWithStatus(
            StatusType.Loading,
            "Loading PAK files...",
            () => loadingService.LoadPakFilesAsync(gameProvider, UpdateStatus)
        );

        if (pakFiles == null)
        {
            ShowError("Failed to load PAK files.");
            return;
        }

        _loadedPakFiles = pakFiles;

        // Build tree structure
        BuildFileTree();

        // Show file explorer
        ShowFileExplorer();
    }

    private static void BuildFileTree()
    {
        var treeManager = TreeManager.Instance;
        treeManager.Clear();

        foreach (var pakFile in _loadedPakFiles.Values)
        {
            treeManager.BuildTree(pakFile);
        }

        treeManager.SortTree(treeManager.Root);
    }

    private static void ShowFileExplorer()
    {
        var fileExplorer = new FileExplorer();
        fileExplorer.Show();
    }

    private static async Task<T?> ExecuteWithStatus<T>(
        StatusType statusType,
        string initialMessage,
        Func<Task<T>> action)
    {
        _statusWindow?.Close();
        _statusWindow = new StatusWindow(statusType, initialMessage);
        _statusWindow.Show();

        try
        {
            var result = await action();
            _statusWindow?.Close();
            return result;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Error in ExecuteWithStatus: {ex.Message}");
            _statusWindow?.Close();
            ShowError($"An error occurred: {ex.Message}");
            return default;
        }
    }

    private static void UpdateStatus(string message)
    {
        _statusWindow?.UpdateMessage(message);
    }

    private static void ShowError(string message)
    {
        var errorWindow = new StatusWindow(StatusType.Error, message);
        errorWindow.Show();
    }
}