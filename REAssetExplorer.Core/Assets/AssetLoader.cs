using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Core.Common;
using REAssetExplorer.Core.Games;
using REAssetExplorer.Core.Pak;

namespace REAssetExplorer.Core.Assets;

/// <summary>
/// Manages loading assets and their dependencies from PAK files.
/// </summary>
public class AssetLoader : IAssetLoader
{
    private readonly IGameProvider _gameProvider;
    private readonly Dictionary<string, PakFile> _pakFiles;
    private readonly MaterialsCache? _materialsCache;
    private readonly Dictionary<string, AssetData> _loadedAssets = new();
    private readonly DependencyResolverRegistry _resolverRegistry;
    
    public AssetLoader(
        IGameProvider gameProvider, 
        Dictionary<string, PakFile> pakFiles,
        MaterialsCache? materialsCache = null,
        DependencyResolverRegistry? resolverRegistry = null)
    {
        _gameProvider = gameProvider ?? throw new ArgumentNullException(nameof(gameProvider));
        _pakFiles = pakFiles ?? throw new ArgumentNullException(nameof(pakFiles));
        _materialsCache = materialsCache;
        _resolverRegistry = resolverRegistry ?? CreateDefaultResolverRegistry();
    }
    
    /// <summary>
    /// Creates a default resolver registry with standard resolvers.
    /// </summary>
    private static DependencyResolverRegistry CreateDefaultResolverRegistry()
    {
        var registry = new DependencyResolverRegistry();
        registry.RegisterResolvers(
            new Resolvers.MeshDependencyResolver(),
            new Resolvers.MaterialDependencyResolver(),
            new Resolvers.TextureDependencyResolver(),
            new Resolvers.SdfDependencyResolver()
        );
        return registry;
    }
    
    /// <summary>
    /// Gets the resolver registry to allow registering custom resolvers.
    /// </summary>
    public DependencyResolverRegistry ResolverRegistry => _resolverRegistry;
    
    /// <inheritdoc/>
    public IGameProvider GameProvider => _gameProvider;
    
    /// <summary>
    /// Loads an asset and optionally its dependencies.
    /// </summary>
    /// <typeparam name="T">The type of asset to load.</typeparam>
    /// <param name="filePath">The path to the asset within the PAK files.</param>
    /// <param name="loadDependencies">Whether to also load dependencies.</param>
    /// <param name="onProgress">Optional callback for progress updates.</param>
    /// <returns>Result containing the loaded asset or an error.</returns>
    public Result<T> LoadAsset<T>(
        string filePath, 
        bool loadDependencies = true,
        Action<string>? onProgress = null) where T : AssetData
    {
        onProgress?.Invoke($"Loading {filePath}...");
        
        // Check if already loaded
        if (_loadedAssets.TryGetValue(filePath, out var cached) && cached is T cachedTyped)
        {
            onProgress?.Invoke($"Using cached {filePath}");
            return Result<T>.Success(cachedTyped);
        }
        
        // Find the asset in PAK files
        var (pakFile, entry) = FindAssetEntry(filePath);
        if (pakFile == null || entry == null)
        {
            onProgress?.Invoke($"Asset not found in PAK: {filePath}");
            return Result<T>.Failure($"Asset not found: {filePath}");
        }
        
        // Extract and read the asset
        try
        {
            var data = _gameProvider.PakReader.ExtractFile(pakFile, entry.Value);
            var reader = _gameProvider.AssetReaders.GetReader<T>(filePath);
            
            if (reader == null)
            {
                return Result<T>.Failure($"No reader found for: {filePath}");
            }
            
            var result = reader.Read(data, filePath);
            
            if (result.IsFailure)
            {
                return Result<T>.Failure(result.Error!);
            }
            
            var asset = result.Value!;
            
            // Set basic properties if not already set
            if (string.IsNullOrEmpty(asset.Name))
            {
                asset.Name = System.IO.Path.GetFileNameWithoutExtension(filePath);
            }
            if (string.IsNullOrEmpty(asset.FilePath))
            {
                asset.FilePath = filePath;
            }
            if (string.IsNullOrEmpty(asset.Extension))
            {
                asset.Extension = System.IO.Path.GetExtension(filePath);
            }
            
            // Resolve dependencies using the appropriate resolver
            var assetType = GetAssetType<T>();
            var resolver = _resolverRegistry.GetResolver(assetType);
            
            if (resolver != null)
            {
                var resolutionContext = new DependencyResolutionContext
                {
                    MaterialsCache = _materialsCache
                };
                
                resolver.ResolveDependencies(asset, resolutionContext);
                
                if (asset.Dependencies.Count > 0)
                {
                    onProgress?.Invoke($"Resolved {asset.Dependencies.Count} dependencies for {filePath}");
                }
            }
            
            // Cache the asset
            _loadedAssets[filePath] = asset;
            
            // Load dependencies if requested
            if (loadDependencies && asset.Dependencies.Count > 0)
            {
                onProgress?.Invoke($"Loading {asset.Dependencies.Count} dependencies for {filePath}...");
                LoadDependencies(asset, onProgress);
            }
            
            return Result<T>.Success(asset);
        }
        catch (Exception ex)
        {
            return Result<T>.Failure($"Error loading {filePath}: {ex.Message}");
        }
    }
    
    /// <summary>
    /// Loads all dependencies of an asset.
    /// </summary>
    /// <param name="asset">The asset whose dependencies to load.</param>
    /// <param name="onProgress">Optional callback for progress updates.</param>
    public void LoadDependencies(AssetData asset, Action<string>? onProgress = null)
    {
        var assetType = GetAssetTypeFromData(asset);
        var resolver = _resolverRegistry.GetResolver(assetType);
        
        if (resolver == null)
        {
            Console.WriteLine($"No resolver found for asset type: {assetType}");
            return;
        }
        
        var loadContext = new DependencyLoadContext(this)
        {
            MaterialsCache = _materialsCache,
            PakFiles = _pakFiles.Values.ToList(),
            GameProvider = _gameProvider
        };
        
        foreach (var dependency in asset.Dependencies)
        {
            dependency.FilePath = dependency.FilePath?.ToLowerInvariant();
            if (string.IsNullOrEmpty(dependency.FilePath))
            {
                onProgress?.Invoke($"Skipping dependency '{dependency.Name}': path not resolved");
                continue;
            }
            
            // Skip if already loaded
            if (_loadedAssets.ContainsKey(dependency.FilePath))
            {
                asset.ResolvedDependencies[dependency.FilePath] = _loadedAssets[dependency.FilePath];
                continue;
            }
            
            onProgress?.Invoke($"Loading dependency: {dependency.FilePath} ({dependency.Purpose})");
            
            try
            {
                var loadedDependency = resolver.LoadDependency(dependency, loadContext, onProgress);
                
                if (loadedDependency == null)
                {
                    if (dependency.IsRequired)
                    {
                        Console.WriteLine($"Failed to load REQUIRED dependency: {dependency.FilePath}");
                    }
                }
                else
                {
                    // Store the resolved dependency in the parent asset
                    asset.ResolvedDependencies[dependency.FilePath] = loadedDependency;
                }
            }
            catch (Exception ex)
            {
                if (dependency.IsRequired)
                {
                    Console.WriteLine($"Failed to load required dependency {dependency.FilePath}: {ex.Message}");
                }
                else
                {
                    Console.WriteLine($"Failed to load optional dependency {dependency.FilePath}: {ex.Message}");
                }
            }
        }
    }
    
    /// <summary>
    /// Gets the asset type for a generic type parameter.
    /// </summary>
    private static AssetType GetAssetType<T>() where T : AssetData
    {
        return typeof(T).Name switch
        {
            nameof(MeshData) => AssetType.Mesh,
            nameof(MaterialData) => AssetType.Material,
            nameof(TextureData) => AssetType.Texture,
            _ => AssetType.Unknown
        };
    }
    
    /// <summary>
    /// Gets the asset type from an asset data instance.
    /// </summary>
    private static AssetType GetAssetTypeFromData(AssetData asset)
    {
        return asset switch
        {
            MeshData => AssetType.Mesh,
            MaterialData => AssetType.Material,
            TextureData => AssetType.Texture,
            _ => AssetType.Unknown
        };
    }
    
    /// <summary>
    /// Gets a loaded asset from the cache.
    /// </summary>
    /// <typeparam name="T">The type of asset to get.</typeparam>
    /// <param name="filePath">The path to the asset.</param>
    /// <returns>The cached asset, or null if not found or wrong type.</returns>
    public T? GetLoadedAsset<T>(string filePath) where T : AssetData
    {
        if (_loadedAssets.TryGetValue(filePath, out var asset) && asset is T typedAsset)
        {
            return typedAsset;
        }
        return null;
    }
    
    /// <summary>
    /// Checks if an asset is already loaded.
    /// </summary>
    /// <param name="filePath">The path to the asset.</param>
    /// <returns>True if the asset is loaded, false otherwise.</returns>
    public bool IsAssetLoaded(string filePath)
    {
        return _loadedAssets.ContainsKey(filePath);
    }
    
    /// <summary>
    /// Clears all loaded assets from the cache.
    /// </summary>
    public void ClearCache()
    {
        _loadedAssets.Clear();
    }
    
    /// <summary>
    /// Gets all loaded assets of a specific type.
    /// </summary>
    /// <typeparam name="T">The type of assets to get.</typeparam>
    /// <returns>Collection of loaded assets of the specified type.</returns>
    public IEnumerable<T> GetLoadedAssets<T>() where T : AssetData
    {
        return _loadedAssets.Values.OfType<T>();
    }
    
    /// <summary>
    /// Finds an asset entry in the PAK files.
    /// Uses contains matching to handle paths with different prefixes and version suffixes.
    /// </summary>
    /// <param name="filePath">The path to search for.</param>
    /// <returns>A tuple with the PAK file and entry, or (null, null) if not found.</returns>
    private (PakFile? pakFile, PakEntry? entry) FindAssetEntry(string filePath)
    {
        foreach (var pakFile in _pakFiles.Values)
        {
            // Try exact match first
            var entry = pakFile.Entries.FirstOrDefault(e => 
                e.FilePath.Equals(filePath, StringComparison.OrdinalIgnoreCase));
            
            if (!string.IsNullOrEmpty(entry.FilePath))
            {
                return (pakFile, entry);
            }
            
            entry = pakFile.Entries.FirstOrDefault(e => 
                e.FilePath.Contains(filePath, StringComparison.OrdinalIgnoreCase));
            
            if (!string.IsNullOrEmpty(entry.FilePath))
            {
                return (pakFile, entry);
            }
        }
        
        return (null, null);
    }
}
