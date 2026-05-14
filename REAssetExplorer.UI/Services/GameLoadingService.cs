using REAssetExplorer.Core.Games;
using REAssetExplorer.Core.Pak;
using REAssetExplorer.UI.Local;
using REAssetExplorer.UI.Remote;
using System.IO;

namespace REAssetExplorer.UI.Services;

/// <summary>
/// Handles the loading and initialization of game data.
/// </summary>
public class GameLoadingService
{
    private readonly FileListStorage _storage;
    private readonly FileListApiClient _apiClient;
    private readonly PakLoader _pakLoader;

    public GameLoadingService(
        FileListStorage storage,
        FileListApiClient apiClient)
    {
        _storage = storage ?? throw new ArgumentNullException(nameof(storage));
        _apiClient = apiClient ?? throw new ArgumentNullException(nameof(apiClient));
        _pakLoader = new PakLoader();
    }

    /// <summary>
    /// Ensures the file list is available locally and up-to-date.
    /// </summary>
    /// <param name="gameId">The game identifier.</param>
    /// <param name="onProgress">Progress callback.</param>
    /// <returns>True if successful, false otherwise.</returns>
    public async Task<bool> EnsureFileListAsync(string gameId, Action<string>? onProgress = null)
    {
        onProgress?.Invoke("Checking file list...");

        if (!_storage.Exists(gameId))
        {
            return await DownloadFileListAsync(gameId, onProgress);
        }

        return await UpdateFileListIfNeededAsync(gameId, onProgress);
    }

    /// <summary>
    /// Loads PAK files for the specified game.
    /// </summary>
    /// <param name="gameProvider">The game provider.</param>
    /// <param name="onProgress">Progress callback.</param>
    /// <returns>The loaded PAK files or null if failed.</returns>
    public async Task<Dictionary<string, PakFile>?> LoadPakFilesAsync(
        IGameProvider gameProvider, 
        Action<string>? onProgress = null)
    {
        ArgumentNullException.ThrowIfNull(gameProvider);

        onProgress?.Invoke("Loading PAK files...");

        var fileListPath = _storage.GetPath(gameProvider.Id);
        var rszFilePath = Path.Combine(AppContext.BaseDirectory, "FileLists", gameProvider.RszFile);

        var result = await _pakLoader.LoadPakFilesAsync(
            gameProvider,
            fileListPath,
            rszFilePath: File.Exists(rszFilePath) ? rszFilePath : null,
            onProgress: onProgress);

        if (result.IsFailure)
        {
            Console.WriteLine($"Failed to load PAK files: {result.Error}");
            return null;
        }

        return result.Value;
    }
    
    private async Task<bool> DownloadFileListAsync(string gameId, Action<string>? onProgress)
    {
        onProgress?.Invoke("Downloading file list from API...");

        try
        {
            var fileList = await _apiClient.GetFileListAsync(gameId);

            if (fileList == null)
            {
                Console.WriteLine("Failed to download file list from API.");
                return false;
            }

            onProgress?.Invoke("Saving file list...");
            _storage.Save(gameId, fileList);
            onProgress?.Invoke("File list downloaded successfully.");
            
            return true;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Error downloading file list: {ex.Message}");
            return false;
        }
    }

    private async Task<bool> UpdateFileListIfNeededAsync(string gameId, Action<string>? onProgress)
    {
        onProgress?.Invoke("Checking for updates...");

        var remoteHash = await _apiClient.GetHashAsync(gameId);
        
        if (remoteHash == null)
        {
            onProgress?.Invoke("Couldn't connect to API. Using local file list...");
            Console.WriteLine("Couldn't connect to API. Skipping update check.");
            return true;
        }

        var localHash = _storage.LoadHash(gameId);

        if (localHash != remoteHash)
        {
            onProgress?.Invoke("File list is outdated. Downloading new version...");
            return await DownloadFileListAsync(gameId, onProgress);
        }

        onProgress?.Invoke("File list is up to date!");
        return true;
    }
}
