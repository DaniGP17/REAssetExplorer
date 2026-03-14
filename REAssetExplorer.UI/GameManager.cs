using REAssetExplorer.Core.Common;
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
    private static PakLoader? _pakLoader;
    
    /// <summary>
    /// Gets the currently loaded game provider.
    /// </summary>
    public static IGameProvider? CurrentGameProvider => _currentGameProvider;
    
    /// <summary>
    /// Gets the loaded PAK files.
    /// </summary>
    public static IReadOnlyDictionary<string, PakFile> LoadedPakFiles => _loadedPakFiles;
    
    /// <summary>
    /// Gets the materials cache.
    /// </summary>
    public static MaterialsCache? MaterialsCache => _pakLoader?.GetMaterialsCache();
    
    /// <summary>
    /// Tries to get the file path for a material by name.
    /// </summary>
    /// <param name="materialName">The name of the material.</param>
    /// <param name="path">The file path if found.</param>
    /// <returns>True if the material was found, false otherwise.</returns>
    public static bool TryGetMaterialPath(string materialName, out string? path)
    {
        path = null;
        return _pakLoader?.TryGetMaterialPath(materialName, out path) ?? false;
    }

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
        
        // Store the PakLoader reference to access materials cache
        _pakLoader = loadingService.PakLoader;

        // Build tree structure
        BuildFileTree();

        // Load game resources
        await LoadGameResourcesAsync();

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
    
    private static async Task LoadGameResourcesAsync()
    {
        var loaderRegistry = App.Services.GetRequiredService<GameResourceLoaderRegistry>();
        
        if (_currentGameProvider == null)
        {
            return;
        }
        
        var success = await ExecuteWithStatus(
            StatusType.Loading,
            "Loading game resources...",
            () => loaderRegistry.LoadAllAsync(_currentGameProvider, UpdateStatus)
        );
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