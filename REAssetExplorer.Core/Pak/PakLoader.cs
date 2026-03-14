using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Core.Common;
using REAssetExplorer.Core.Games;

namespace REAssetExplorer.Core.Pak;

/// <summary>
/// Handles loading and management of PAK files for a game.
/// </summary>
public class PakLoader
{
    private readonly PakFileList _globalPakFileList = new();
    private MaterialsCache? _materialsCache;
    
    /// <summary>
    /// Loads all PAK files specified in the game provider.
    /// </summary>
    /// <param name="gameProvider">The game provider containing PAK locations.</param>
    /// <param name="fileListPath">Path to the file list for hash resolution.</param>
    /// <param name="cachePath">Optional path to the materials cache file.</param>
    /// <param name="loadMaterials">Whether to load and cache materials.</param>
    /// <param name="onProgress">Optional callback for progress updates.</param>
    /// <returns>Result containing dictionary of loaded PAK files or an error message.</returns>
    public async Task<Result<Dictionary<string, PakFile>>> LoadPakFilesAsync(
        IGameProvider gameProvider, 
        string fileListPath, 
        string? cachePath = null,
        bool loadMaterials = true,
        Action<string>? onProgress = null)
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

        var result = await LoadPakFilesInternalAsync(gameProvider);
        
        if (result.IsSuccess && loadMaterials)
        {
            await LoadMaterialsCacheAsync(gameProvider, result.Value!, cachePath, onProgress);
        }
        
        return result;
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

    /// <summary>
    /// Loads or creates the materials cache from PAK files.
    /// </summary>
    /// <param name="gameProvider">The game provider.</param>
    /// <param name="pakFiles">The loaded PAK files.</param>
    /// <param name="cachePath">Optional path to the cache file.</param>
    /// <param name="onProgress">Optional callback for progress updates.</param>
    private async Task LoadMaterialsCacheAsync(
        IGameProvider gameProvider, 
        Dictionary<string, PakFile> pakFiles, 
        string? cachePath,
        Action<string>? onProgress)
    {
        try
        {
            onProgress?.Invoke("Loading materials cache...");
            
            // Try to load existing cache
            if (!string.IsNullOrEmpty(cachePath) && File.Exists(cachePath))
            {
                try
                {
                    _materialsCache = CacheFile.LoadFromFile<MaterialsCache>(cachePath);
                    if (_materialsCache != null)
                    {
                        onProgress?.Invoke($"Loaded materials cache with {_materialsCache.Count} materials");
                        return;
                    }
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"Failed to load materials cache: {ex.Message}");
                }
            }
            
            // Create new cache
            onProgress?.Invoke("Creating new materials cache...");
            _materialsCache = new MaterialsCache();
            
            int totalMaterials = 0;
            
            await Task.Run(() =>
            {
                foreach (var pakFile in pakFiles)
                {
                    var entries = pakFile.Value.Entries
                        .Where(e => e.FilePath.Contains(".mdf2", StringComparison.OrdinalIgnoreCase))
                        .ToList();
                    
                    int processedCount = 0;
                    
                    foreach (var entry in entries)
                    {
                        onProgress?.Invoke($"Processing {processedCount}/{entries.Count} in {pakFile.Key} - Total materials: {totalMaterials}");
                        
                        try
                        {
                            var materialReader = gameProvider.AssetReaders.GetReader<MaterialData>(entry.FilePath);
                            
                            if (materialReader == null)
                            {
                                processedCount++;
                                continue;
                            }
                            
                            var entryData = gameProvider.PakReader.ExtractFile(pakFile.Value, entry);
                            var result = materialReader.Read(entryData, entry.FilePath);
                            
                            if (result.IsSuccess && result.Value != null)
                            {
                                foreach (var matHeader in result.Value.MaterialHeaders)
                                {
                                    _materialsCache.AddMaterial(matHeader.MaterialName, entry.FilePath);
                                }
                                totalMaterials++;
                            }
                        }
                        catch (Exception ex)
                        {
                            Console.WriteLine($"Error loading material {entry.FilePath}: {ex.Message}");
                        }
                        
                        processedCount++;
                    }
                }
            });
            
            onProgress?.Invoke($"Loaded {totalMaterials} material files");
            
            // Save cache if path is provided
            if (!string.IsNullOrEmpty(cachePath))
            {
                try
                {
                    _materialsCache.SaveToFile(cachePath);
                    onProgress?.Invoke("Materials cache saved successfully");
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"Failed to save materials cache: {ex.Message}");
                }
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Error loading materials cache: {ex.Message}");
            Console.WriteLine($"Stack trace: {ex.StackTrace}");
        }
    }
    
    /// <summary>
    /// Gets the materials cache.
    /// </summary>
    /// <returns>The materials cache if loaded, null otherwise.</returns>
    public MaterialsCache? GetMaterialsCache()
    {
        return _materialsCache;
    }
    
    /// <summary>
    /// Tries to get the file path for a material by name.
    /// </summary>
    /// <param name="materialName">The name of the material.</param>
    /// <param name="path">The file path if found.</param>
    /// <returns>True if the material was found, false otherwise.</returns>
    public bool TryGetMaterialPath(string materialName, out string? path)
    {
        path = null;
        return _materialsCache?.TryGetMaterial(materialName, out path) ?? false;
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
