using REAssetExplorer.Core.Common;
using REAssetExplorer.Core.Games;

namespace REAssetExplorer.Core.Pak;

/// <summary>
/// Handles loading and management of PAK files for a game.
/// </summary>
public class PakLoader
{
    private readonly PakFileList _globalPakFileList = new();
    
    /// <summary>
    /// Loads all PAK files specified in the game provider.
    /// </summary>
    /// <param name="gameProvider">The game provider containing PAK locations.</param>
    /// <param name="fileListPath">Path to the file list for hash resolution.</param>
    /// <returns>Result containing dictionary of loaded PAK files or an error message.</returns>
    public async Task<Result<Dictionary<string, PakFile>>> LoadPakFilesAsync(IGameProvider gameProvider, string fileListPath)
    {
        ArgumentNullException.ThrowIfNull(gameProvider);
        ArgumentException.ThrowIfNullOrEmpty(fileListPath);

        var validationResult = ValidateGameProvider(gameProvider);
        if (validationResult.IsFailure)
        {
            return Result<Dictionary<string, PakFile>>.Failure(validationResult.Error!);
        }

        var fileListResult = LoadFileList(fileListPath);
        if (fileListResult.IsFailure)
        {
            return Result<Dictionary<string, PakFile>>.Failure(fileListResult.Error!);
        }

        return await LoadPakFilesInternalAsync(gameProvider);
    }

    private Result ValidateGameProvider(IGameProvider gameProvider)
    {
        if (string.IsNullOrWhiteSpace(gameProvider.GameDirectory))
        {
            return Result.Failure("Game directory is not set in the game provider.");
        }

        if (!Directory.Exists(gameProvider.GameDirectory))
        {
            return Result.Failure($"Game directory does not exist: {gameProvider.GameDirectory}");
        }

        return Result.Success();
    }

    private Result LoadFileList(string fileListPath)
    {
        try
        {
            if (!File.Exists(fileListPath))
            {
                return Result.Failure($"File list not found: {fileListPath}");
            }

            _globalPakFileList.SetupFromFile(fileListPath);
            return Result.Success();
        }
        catch (Exception ex)
        {
            return Result.Failure($"Failed to load file list: {ex.Message}");
        }
    }

    private async Task<Result<Dictionary<string, PakFile>>> LoadPakFilesInternalAsync(IGameProvider gameProvider)
    {
        var loadedPaks = new Dictionary<string, PakFile>();
        var errors = new List<string>();

        foreach (var pakLocation in gameProvider.PaksLocations)
        {
            var result = await TryLoadSinglePakAsync(gameProvider, pakLocation);
            
            if (result.IsSuccess)
            {
                loadedPaks[pakLocation] = result.Value!;
                Console.WriteLine($"Loaded PAK file: {pakLocation} ({result.Value!.Entries.Count} entries)");
            }
            else
            {
                errors.Add(result.Error!);
                Console.WriteLine($"Warning: {result.Error}");
            }
        }

        if (loadedPaks.Count == 0 && errors.Count > 0)
        {
            return Result<Dictionary<string, PakFile>>.Failure($"Failed to load any PAK files. Errors: {string.Join("; ", errors)}");
        }

        return Result<Dictionary<string, PakFile>>.Success(loadedPaks);
    }

    private async Task<Result<PakFile>> TryLoadSinglePakAsync(IGameProvider gameProvider, string pakLocation)
    {
        var fullPath = ResolvePakPath(gameProvider.GameDirectory, pakLocation);

        if (!File.Exists(fullPath))
        {
            return Result<PakFile>.Failure($"PAK file not found: {fullPath}");
        }

        try
        {
            var pakFile = await LoadSinglePakFileAsync(fullPath, gameProvider.PakReader);
            return Result<PakFile>.Success(pakFile);
        }
        catch (Exception ex)
        {
            return Result<PakFile>.Failure($"Error loading PAK file {pakLocation}: {ex.Message}");
        }
    }

    private async Task<PakFile> LoadSinglePakFileAsync(string fullPath, IPakReader pakReader)
    {
        return await Task.Run(() => pakReader.Open(fullPath, _globalPakFileList));
    }

    private static string ResolvePakPath(string gameDirectory, string pakLocation)
    {
        return Path.IsPathRooted(pakLocation) 
            ? pakLocation 
            : Path.Combine(gameDirectory, pakLocation);
    }

    /// <summary>
    /// Gets information about PAK files without loading them completely.
    /// </summary>
    /// <param name="gameProvider">The game provider containing PAK locations.</param>
    /// <returns>List of PAK file information.</returns>
    public List<PakFileInfo> GetPakFilesInfo(IGameProvider gameProvider)
    {
        ArgumentNullException.ThrowIfNull(gameProvider);

        if (string.IsNullOrWhiteSpace(gameProvider.GameDirectory))
        {
            return new List<PakFileInfo>();
        }

        return gameProvider.PaksLocations
            .Select(pakLocation => CreatePakFileInfo(gameProvider.GameDirectory, pakLocation))
            .ToList();
    }

    private static PakFileInfo CreatePakFileInfo(string gameDirectory, string pakLocation)
    {
        var fullPath = ResolvePakPath(gameDirectory, pakLocation);
        var fileInfo = new FileInfo(fullPath);

        return new PakFileInfo(
            pakLocation,
            fullPath,
            fileInfo.Exists,
            fileInfo.Exists ? fileInfo.Length : 0
        );
    }
}

/// <summary>
/// Information about a PAK file.
/// </summary>
/// <param name="Name">The relative name/location of the PAK file.</param>
/// <param name="FullPath">The full absolute path to the PAK file.</param>
/// <param name="Exists">Whether the file exists on disk.</param>
/// <param name="Size">The size of the file in bytes (0 if it doesn't exist).</param>
public record PakFileInfo(string Name, string FullPath, bool Exists, long Size);
